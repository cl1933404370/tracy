#if defined(USE_TRACY_HCOM_BUNDLE)
#include "TracyHcom.hpp"
#else
#include "../../public/client/TracyLiteAll.hpp"
#if defined(TRACYLITE_PERFETTO)
#include "../../public/client/TracyLitePerfetto.hpp"
#endif
#include "../../public/client/TracyLiteChunkWriter.hpp"
#endif

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace
{
    struct PerfResult
    {
        long long elapsedNs = 0;
        double nsPerPair = 0.0;
        long long exportElapsedUs = 0;
        long long serializeElapsedUs = 0;
        long long writeElapsedUs = 0;
        size_t fileSize = 0;
        double throughputMbPerSec = 0.0;
    };

    void EmitWorkload(const int threadIndex, const int iterations)
    {
        static constexpr tracylite::SourceLocationDataLite srcloc{ "PerfExportZone", "EmitWorkload", "TracyNoSendPerfCase.cpp", 1, 0 };
        for (int i = 0; i < iterations; ++i)
        {
            tracylite::Collector::ZoneBegin(&srcloc);
            tracylite::Collector::Instant((i & 1) == 0 ? "InstantA" : "InstantB", "thread");
            tracylite::Collector::Counter((i & 1) == 0 ? "CounterA" : "CounterB", static_cast<int64_t>(i + threadIndex));
            tracylite::Collector::Counter((i & 1) == 0 ? "CounterFloatA" : "CounterFloatB", static_cast<double>(i) * 0.5);
            const auto ptrValue = (static_cast<uint64_t>(static_cast<uint32_t>(threadIndex)) << 32) | static_cast<uint32_t>(i + 1);
            const auto ptr = reinterpret_cast<const void*>(static_cast<uintptr_t>(ptrValue));
            tracylite::Collector::MemAlloc(ptr, static_cast<size_t>((i % 64) + 1));
            tracylite::Collector::MemFree(ptr);
            tracylite::Collector::ZoneEnd();
        }
    }

    void DrainCollector()
    {
#if defined(TRACYLITE_PERFETTO)
        (void)tracylite::PerfettoNativeExporter::ExportToBuffer(tracylite::Collector::Instance());
#endif
    }

    size_t GetFileSize(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            return 0;
        }

        return static_cast<size_t>(file.tellg());
    }

    bool WriteTraceToFile(const char* outputPath, const std::vector<uint8_t>& trace)
    {
        // Write to a sibling temp file, then atomically replace the destination.
        // This closes the TOCTOU window where a symlink could be planted between
        // an explicit unlink and the write to the final path.
        const std::string finalPath(outputPath);
        const std::string tmpPath = finalPath + ".tmp";

        {
            std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
            if (!file)
            {
                return false;
            }

            constexpr size_t writeBufSize = static_cast<size_t>(256) * 1024;
            char writeBuf[writeBufSize];
            file.rdbuf()->pubsetbuf(writeBuf, writeBufSize);
            file.write(reinterpret_cast<const char*>(trace.data()), static_cast<std::streamsize>(trace.size()));
            file.flush();
            if (!file.good())
            {
                std::remove(tmpPath.c_str());
                return false;
            }
        }

#ifdef _WIN32
        if (::MoveFileExA(tmpPath.c_str(), finalPath.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0)
        {
            std::remove(tmpPath.c_str());
            return false;
        }
#else
        if (std::rename(tmpPath.c_str(), finalPath.c_str()) != 0)
        {
            std::remove(tmpPath.c_str());
            return false;
        }
#endif
        return true;
    }

    PerfResult RunZoneBeginEnd(const int iterations)
    {
        PerfResult result{};
        tracylite::Collector::Initialize(32 * 1024 * 1024);
        DrainCollector();

        static constexpr tracylite::SourceLocationDataLite srcloc{ "ZoneBeginEndPerf", "RunZoneBeginEnd", "TracyNoSendPerfCase.cpp", 1, 0 };

        // Warmup: trigger ThreadState allocation (1 GB buffer + memset)
        // BEFORE the timing loop so it doesn't inflate per-pair cost.
        tracylite::Collector::ZoneBegin(&srcloc);
        tracylite::Collector::ZoneEnd();
        DrainCollector();

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i)
        {
            tracylite::Collector::ZoneBegin(&srcloc);
            tracylite::Collector::ZoneEnd();
        }
        const auto end = std::chrono::steady_clock::now();

        result.elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        result.nsPerPair = iterations > 0 ? static_cast<double>(result.elapsedNs) / static_cast<double>(iterations) : 0.0;

#if defined(TRACYLITE_PERFETTO)
        auto trace = tracylite::PerfettoNativeExporter::ExportToBuffer(tracylite::Collector::Instance());
        if (trace.empty())
        {
            result.elapsedNs = -1;
        }
#endif

        return result;
    }

    PerfResult RunExportToFile(const int threadCount, const int iterationsPerThread, const char* outputPath)
    {
        PerfResult result{};
        tracylite::Collector::Initialize(32 * 1024 * 1024);
        DrainCollector();

        std::vector<std::thread> workers;
        workers.reserve(threadCount);
        for (int t = 0; t < threadCount; ++t)
        {
            workers.emplace_back([t, iterationsPerThread]() { EmitWorkload(t, iterationsPerThread); });
        }

        for (auto& worker : workers)
        {
            worker.join();
        }

        // No need to unlink the destination first; WriteTraceToFile() writes
        // to a sibling .tmp file and atomically replaces outputPath.
    #if defined(TRACYLITE_PERFETTO)
        const auto serializeStart = std::chrono::steady_clock::now();
        auto trace = tracylite::PerfettoNativeExporter::ExportToBuffer(tracylite::Collector::Instance());
        const auto serializeEnd = std::chrono::steady_clock::now();

        result.serializeElapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(serializeEnd - serializeStart).count();
        result.fileSize = trace.size();

        const auto writeStart = std::chrono::steady_clock::now();
        const auto ok = !trace.empty() && WriteTraceToFile(outputPath, trace);
        const auto writeEnd = std::chrono::steady_clock::now();

        result.writeElapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(writeEnd - writeStart).count();
        result.exportElapsedUs = result.serializeElapsedUs + result.writeElapsedUs;
        result.fileSize = GetFileSize(outputPath);

        const auto seconds = static_cast<double>(result.writeElapsedUs) / 1'000'000.0;
        result.throughputMbPerSec = seconds > 0.0 ? (static_cast<double>(result.fileSize) / (1024.0 * 1024.0)) / seconds : 0.0;

        if (!ok || result.fileSize == 0)
        {
            result.exportElapsedUs = -1;
            result.serializeElapsedUs = -1;
            result.writeElapsedUs = -1;
        }
#else
        (void)outputPath;
        result.exportElapsedUs = -1;
        result.serializeElapsedUs = -1;
        result.writeElapsedUs = -1;
#endif

        return result;
    }

#if defined(TRACYLITE_PERFETTO)
    struct ChunkSelfTestStats
    {
        int failures = 0;
        size_t traceSize = 0;
        size_t chunkCount = 0;
        size_t maxLineLength = 0;
        long long encodeElapsedUs = 0;
        long long decodeElapsedUs = 0;
        bool rebuiltTraceWritten = false;
    };

    // ── Chunk-transport demo ─────────────────────────────────────────────────
    //
    // Simulates an environment where the only egress path is a text log library
    // whose per-line limit is 511 characters (512 bytes including NUL).
    //
    // Replace the printf lambda with your real log-library call, e.g.:
    //   [](const char* line) { MY_LOG_INFO("%s", line); return true; }
    //
    // To reconstruct on the host:
    //   python scripts/tracylite_reconstruct.py device.log out.perfetto-trace
    //
    ChunkSelfTestStats RunChunkTransportDemo()
    {
        ChunkSelfTestStats stats{};
        auto CHECK = [&](bool cond, const char* msg)
        {
            if (!cond) { std::fprintf(stderr, "[chunk-self-test] FAIL: %s\n", msg); ++stats.failures; }
        };

        // A. Known-vector checks
        {
            const uint8_t kVec[] = "123456789";
            CHECK(tracylite::Crc32(kVec, 9) == 0xCBF43926u, "CRC32 known-vector");
        }
        struct B64Vec { const char* raw; size_t len; const char* expected; };
        static const B64Vec kB64[] = {
            { "f",      1, "Zg=="     },
            { "fo",     2, "Zm8="     },
            { "foo",    3, "Zm9v"     },
            { "foob",   4, "Zm9vYg==" },
            { "fooba",  5, "Zm9vYmE=" },
            { "foobar", 6, "Zm9vYmFy" },
        };
        for (const auto& v : kB64)
        {
            char buf[16] = {};
            const size_t outLen = tracylite::Base64Encode(
                reinterpret_cast<const uint8_t*>(v.raw), v.len, buf);
            buf[outLen] = '\0';
            CHECK(std::strcmp(buf, v.expected) == 0, v.expected);
        }

        // B+C. Line-budget + full roundtrip
        tracylite::Collector::Initialize(32 * 1024 * 1024);
        EmitWorkload(0, 1000);
        const auto trace =
            tracylite::PerfettoNativeExporter::ExportToBuffer(tracylite::Collector::Instance());
        stats.traceSize = trace.size();
        CHECK(!trace.empty(), "ExportToBuffer non-empty");

        // Inline Base64 decoder (RFC 4648, no newlines, strict validation)
        auto b64DecodeStrict = [](const char* src, size_t srcLen, std::vector<uint8_t>& dst) -> bool
        {
            if (!src || srcLen == 0 || (srcLen % 4) != 0)
            {
                return false;
            }

            static const int8_t kDec[256] = {
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
                52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
                -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
                15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
                -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
                41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
                -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
            };

            for (size_t i = 0; i + 3 < srcLen; i += 4)
            {
                const bool isLastQuartet = (i + 4 == srcLen);
                const char c0 = src[i];
                const char c1 = src[i + 1];
                const char c2 = src[i + 2];
                const char c3 = src[i + 3];

                if (c0 == '=' || c1 == '=')
                {
                    return false;
                }

                const int a = static_cast<int>(kDec[static_cast<uint8_t>(c0)]);
                const int b = static_cast<int>(kDec[static_cast<uint8_t>(c1)]);
                if (a < 0 || b < 0)
                {
                    return false;
                }

                if (c2 == '=')
                {
                    if (c3 != '=' || !isLastQuartet)
                    {
                        return false;
                    }
                    dst.push_back(static_cast<uint8_t>((a << 2) | (b >> 4)));
                    continue;
                }

                const int c = static_cast<int>(kDec[static_cast<uint8_t>(c2)]);
                if (c < 0)
                {
                    return false;
                }

                dst.push_back(static_cast<uint8_t>((a << 2) | (b >> 4)));
                dst.push_back(static_cast<uint8_t>(((b & 0xF) << 4) | (c >> 2)));

                if (c3 == '=')
                {
                    if (!isLastQuartet)
                    {
                        return false;
                    }
                    continue;
                }

                const int d = static_cast<int>(kDec[static_cast<uint8_t>(c3)]);
                if (d < 0)
                {
                    return false;
                }

                dst.push_back(static_cast<uint8_t>(((c & 0x3) << 6) | d));
            }

            return true;
        };

        // D. Strict decode negative cases
        {
            std::vector<uint8_t> out;
            CHECK(!b64DecodeStrict("Zm9*", 4, out), "strict b64 rejects invalid char");
            out.clear();
            CHECK(!b64DecodeStrict("Zg=", 3, out), "strict b64 rejects invalid length");
            out.clear();
            CHECK(!b64DecodeStrict("=m9v", 4, out), "strict b64 rejects leading padding");
            out.clear();
            CHECK(!b64DecodeStrict("Zm=v", 4, out), "strict b64 rejects mid padding");
        }

        struct ChunkRecord { size_t seq; uint32_t crc; std::string b64; };
        std::vector<ChunkRecord> records;
        bool budgetOk = true;

        const auto logFn = [&](const char* line) -> bool
        {
            constexpr size_t kLogLineMaxChars = 511;
            constexpr size_t kLogLineMaxBytesWithNul = 512;
            const size_t len = std::strlen(line);
            if (stats.maxLineLength < len) stats.maxLineLength = len;
            if (len > kLogLineMaxChars || (len + 1) > kLogLineMaxBytesWithNul)
            {
                budgetOk = false;
                return false;
            }

            // Format: TRACYLITE_CHUNK|<id>|<seq>|<total>|<crc32>|<base64>
            const char* p0 = std::strchr(line, '|');
            if (!p0) return false;
            const char* p1 = std::strchr(p0 + 1, '|');
            if (!p1) return false;
            const char* p2 = std::strchr(p1 + 1, '|');
            if (!p2) return false;
            const char* p3 = std::strchr(p2 + 1, '|');
            if (!p3) return false;
            const char* p4 = std::strchr(p3 + 1, '|');
            if (!p4) return false;

            std::string seqField(p1 + 1, p2);
            std::string crcField(p3 + 1, p4);
            const char* b64Str = p4 + 1;
            if (*b64Str == '\0') return false;

            ChunkRecord rec;
            rec.seq = static_cast<size_t>(std::strtoul(seqField.c_str(), nullptr, 10));
            rec.crc = static_cast<uint32_t>(std::strtoul(crcField.c_str(), nullptr, 16));
            rec.b64 = b64Str;
            records.push_back(std::move(rec));
            return true;
        };

        char traceId[9];
        std::snprintf(traceId, sizeof(traceId), "%08X", 0x5E1FU);
        const auto encodeStart = std::chrono::steady_clock::now();
        const bool writeOk = tracylite::ChunkWrite(trace.data(), trace.size(), traceId, logFn);
        const auto encodeEnd = std::chrono::steady_clock::now();
        stats.encodeElapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(encodeEnd - encodeStart).count();
        stats.chunkCount = records.size();

        CHECK(writeOk,          "ChunkWrite returned true");
        CHECK(budgetOk,         "all lines <= 511 chars (+NUL <= 512 bytes)");
        CHECK(!records.empty(), "at least one chunk emitted");

        std::vector<uint8_t> rebuilt;
        rebuilt.reserve(trace.size());
        bool crcAllOk = true;

        const auto decodeStart = std::chrono::steady_clock::now();
        for (const auto& rec : records)
        {
            std::vector<uint8_t> chunk;
            if (!b64DecodeStrict(rec.b64.c_str(), rec.b64.size(), chunk))
            {
                std::fprintf(stderr, "[chunk-self-test] strict base64 decode failed seq=%zu\n", rec.seq);
                crcAllOk = false;
                continue;
            }
            const uint32_t actual = tracylite::Crc32(chunk.data(), chunk.size());
            if (actual != rec.crc)
            {
                std::fprintf(stderr, "[chunk-self-test] CRC mismatch seq=%zu expected=%08X got=%08X\n",
                             rec.seq, rec.crc, actual);
                crcAllOk = false;
            }
            rebuilt.insert(rebuilt.end(), chunk.begin(), chunk.end());
        }
        const auto decodeEnd = std::chrono::steady_clock::now();
        stats.decodeElapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(decodeEnd - decodeStart).count();

        CHECK(crcAllOk,                       "per-chunk CRC all match");
        CHECK(rebuilt.size() == trace.size(), "reassembled size == original");
        CHECK(rebuilt == trace,               "reassembled bytes identical to original");

        static constexpr const char* kRebuiltTracePath = "tracylite_chunk_selftest_rebuilt.perfetto-trace";
        const bool rebuiltWriteOk = !rebuilt.empty() && WriteTraceToFile(kRebuiltTracePath, rebuilt);
        stats.rebuiltTraceWritten = rebuiltWriteOk;
        CHECK(rebuiltWriteOk, "rebuilt trace file written");

        if (stats.failures == 0)
            std::printf("[chunk-self-test] ALL PASS trace=%zu bytes chunks=%zu max_line=%zu encode_us=%lld decode_us=%lld out=%s\n",
                        stats.traceSize,
                        stats.chunkCount,
                        stats.maxLineLength,
                        stats.encodeElapsedUs,
                        stats.decodeElapsedUs,
                        kRebuiltTracePath);
        else
            std::fprintf(stderr,
                         "[chunk-self-test] %d FAILURE(S) trace=%zu bytes chunks=%zu max_line=%zu encode_us=%lld decode_us=%lld\n",
                         stats.failures,
                         stats.traceSize,
                         stats.chunkCount,
                         stats.maxLineLength,
                         stats.encodeElapsedUs,
                         stats.decodeElapsedUs);

        return stats;
    }
#endif // TRACYLITE_PERFETTO

} // namespace

#if defined(_WIN32)
#define TRACYNOSEND_API extern "C" __declspec(dllexport)
#else
#define TRACYNOSEND_API extern "C"
#endif

TRACYNOSEND_API int TracyNoSend_RunZoneBeginEndPerf(const int iterations, long long* elapsedNs, double* nsPerPair)
{
    const auto result = RunZoneBeginEnd(iterations);
    if (elapsedNs) *elapsedNs = result.elapsedNs;
    if (nsPerPair) *nsPerPair = result.nsPerPair;
    return result.elapsedNs >= 0 ? 0 : -1;
}

TRACYNOSEND_API int TracyNoSend_RunExportToFilePerf(const int threadCount,
                                                    const int iterationsPerThread,
                                                    const char* outputPath,
                                                    long long* exportElapsedUs,
                                                    long long* serializeElapsedUs,
                                                    long long* writeElapsedUs,
                                                    size_t* fileSize,
                                                    double* throughputMbPerSec)
{
    const auto result = RunExportToFile(threadCount, iterationsPerThread, outputPath);
    if (exportElapsedUs) *exportElapsedUs = result.exportElapsedUs;
    if (serializeElapsedUs) *serializeElapsedUs = result.serializeElapsedUs;
    if (writeElapsedUs) *writeElapsedUs = result.writeElapsedUs;
    if (fileSize) *fileSize = result.fileSize;
    if (throughputMbPerSec) *throughputMbPerSec = result.throughputMbPerSec;
    return result.exportElapsedUs >= 0 ? 0 : -1;
}

TRACYNOSEND_API int TracyNoSend_RunChunkSelfTest()
{
#if defined(TRACYLITE_PERFETTO)
    const auto stats = RunChunkTransportDemo();
    return stats.failures == 0 ? 0 : -1;
#else
    std::fprintf(stderr, "[chunk-self-test] TRACYLITE_PERFETTO not enabled\n");
    return -1;
#endif
}
