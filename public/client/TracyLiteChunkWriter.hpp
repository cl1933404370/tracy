// TracyLiteChunkWriter.hpp
//
// Utility for writing a Perfetto trace binary through a text-only log library
// that imposes a per-line length limit (e.g. sprintf_s 511+NUL = 512 bytes).
//
// Usage model:
//   1. C++ side  – call ChunkWrite() to Base64-encode the trace and emit
//                  one log line per chunk.
//   2. Host side – run scripts/tracylite_reconstruct.py to reassemble the
//                  binary .perfetto-trace file.
//
// Chunk line format (all fields on one line, separated by '|'):
//   TRACYLITE_CHUNK|<trace_id>|<seq>|<total>|<crc32_hex>|<base64_payload>
//
// Maximum Base64 payload per line:
//   Line budget  = 511 chars (sprintf_s limit before NUL)
//   Header fixed = len("TRACYLITE_CHUNK|") + uuid(8) + '|' + seq(8) +
//                  '|' + total(8) + '|' + crc32(8) + '|'
//                = 16 + 8 + 1 + 8 + 1 + 8 + 1 + 8 + 1 = 52 chars
//   Base64 budget = 511 - 52 = 459 chars  → floor(459/4)*3 = 342 raw bytes
//
// Compatible with: C++11 and above. No third-party dependencies.
// Base64 alphabet: RFC 4648 standard (with '=' padding).

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace tracylite {

// ── Minimal RFC-4648 Base64 encoder ─────────────────────────────────────────
// Encodes [src, src+len) into dst, which must be at least Base64EncodedSize()
// bytes long.  Returns number of bytes written (including '=' padding).
inline size_t Base64EncodedSize( size_t rawLen ) noexcept
{
    return ( ( rawLen + 2 ) / 3 ) * 4;
}

inline size_t Base64Encode( const uint8_t* src, size_t len, char* dst ) noexcept
{
    static constexpr char kAlpha[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out = 0;
    size_t i = 0;
    for( ; i + 2 < len; i += 3 )
    {
        const uint32_t v = ( static_cast<uint32_t>( src[i] )     << 16 )
                         | ( static_cast<uint32_t>( src[i + 1] ) <<  8 )
                         |   static_cast<uint32_t>( src[i + 2] );
        dst[out++] = kAlpha[( v >> 18 ) & 0x3F];
        dst[out++] = kAlpha[( v >> 12 ) & 0x3F];
        dst[out++] = kAlpha[( v >>  6 ) & 0x3F];
        dst[out++] = kAlpha[  v         & 0x3F];
    }
    if( i + 1 == len )
    {
        const uint32_t v = static_cast<uint32_t>( src[i] ) << 16;
        dst[out++] = kAlpha[( v >> 18 ) & 0x3F];
        dst[out++] = kAlpha[( v >> 12 ) & 0x3F];
        dst[out++] = '=';
        dst[out++] = '=';
    }
    else if( i + 2 == len )
    {
        const uint32_t v = ( static_cast<uint32_t>( src[i] )     << 16 )
                         | ( static_cast<uint32_t>( src[i + 1] ) <<  8 );
        dst[out++] = kAlpha[( v >> 18 ) & 0x3F];
        dst[out++] = kAlpha[( v >> 12 ) & 0x3F];
        dst[out++] = kAlpha[( v >>  6 ) & 0x3F];
        dst[out++] = '=';
    }
    return out;
}

// ── Minimal CRC-32 (ISO 3309 / IEEE 802.3) ──────────────────────────────────
inline uint32_t Crc32( const uint8_t* data, size_t len ) noexcept
{
    uint32_t crc = 0xFFFFFFFFu;
    while( len-- )
    {
        crc ^= static_cast<uint32_t>( *data++ );
        for( int k = 0; k < 8; ++k )
            crc = ( crc >> 1 ) ^ ( 0xEDB88320u & ~( ( crc & 1u ) - 1u ) );
    }
    return crc ^ 0xFFFFFFFFu;
}

// ── ChunkWriter ──────────────────────────────────────────────────────────────

// LogLineFn: called once per chunk with a NUL-terminated line <= 511 chars.
// Return false to abort early (remaining chunks will not be emitted).
using LogLineFn = std::function<bool( const char* line )>;

// Maximum raw bytes per chunk derived from the 511-char line budget:
//   Header overhead = 52 chars (see file header comment)
//   Base64 budget   = 511 - 52 = 459 chars → floor(459/4)*3 = 342 raw bytes
static constexpr size_t kChunkRawBytes = 342;

// Maximum total chunks encoded in the seq/total decimal fields (8 digits).
static constexpr size_t kMaxChunks = 99999999;

// Write |data| of |size| bytes as a sequence of log lines through |log_fn|.
// |trace_id| is an 8-character (max) ASCII identifier used to group chunks.
// Returns true if all chunks were emitted, false on |log_fn| rejection or
// overflow.
inline bool ChunkWrite( const uint8_t* data,
                        size_t          size,
                        const char*     trace_id,
                        const LogLineFn& log_fn )
{
    if( !data || size == 0 || !log_fn ) return false;
    const char* safeTraceId = ( trace_id && trace_id[0] ) ? trace_id : "00000000";

    const size_t total = ( size + kChunkRawBytes - 1 ) / kChunkRawBytes;
    if( total > kMaxChunks ) return false;  // trace too large for this protocol

    // Scratch buffer: header(52) + base64(459) + NUL(1) = 512
    char line[512];
    // Base64 scratch: kChunkRawBytes → ceil(*4/3) ≤ 456 chars
    char b64[468];

    for( size_t seq = 0; seq < total; ++seq )
    {
        const size_t offset    = seq * kChunkRawBytes;
        const size_t chunkLen  = ( offset + kChunkRawBytes <= size )
                                 ? kChunkRawBytes
                                 : ( size - offset );
        const uint32_t crc     = Crc32( data + offset, chunkLen );
        const size_t   b64Len  = Base64Encode( data + offset, chunkLen, b64 );
        b64[b64Len] = '\0';

        // Format: TRACYLITE_CHUNK|<id>|<seq>|<total>|<crc32>|<b64>
        // seq and total are zero-padded to 8 digits for lexicographic sort safety.
        const int written = std::snprintf(
            line, sizeof( line ),
            "TRACYLITE_CHUNK|%.8s|%08zu|%08zu|%08X|%s",
            safeTraceId,
            seq,
            total,
            static_cast<unsigned>( crc ),
            b64 );

        if( written <= 0 || written >= static_cast<int>( sizeof( line ) ) )
            return false;

        if( !log_fn( line ) ) return false;
    }
    return true;
}

} // namespace tracylite
