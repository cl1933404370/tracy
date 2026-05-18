#!/usr/bin/env python3
"""
tracylite_reconstruct.py
------------------------
Reassemble a .perfetto-trace binary from log lines produced by
TracyLiteChunkWriter.hpp.

Chunk line format (ASCII, one per log line, max 511 chars + newline):
    TRACYLITE_CHUNK|<trace_id>|<seq>|<total>|<crc32_hex>|<base64_payload>

Usage:
    python tracylite_reconstruct.py  INPUT_LOG  OUTPUT_TRACE  [TRACE_ID]

    INPUT_LOG    – plain text log file (or '-' for stdin)
    OUTPUT_TRACE – destination .perfetto-trace file
    TRACE_ID     – optional; if omitted, the first trace_id found is used

Example:
    python tracylite_reconstruct.py device.log trace.perfetto-trace
    python tracylite_reconstruct.py device.log trace.perfetto-trace A3F8C1B0
"""

import argparse
import base64
import struct
import sys
import zlib
from pathlib import Path

MAGIC = "TRACYLITE_CHUNK"


def crc32_compat(data: bytes) -> int:
    """CRC-32 unsigned, matching C++ tracylite::Crc32()."""
    return zlib.crc32(data) & 0xFFFFFFFF


def parse_log(source, trace_id_filter: str | None):
    """
    Iterate over lines in *source* and collect chunk records that match
    *trace_id_filter* (or the first trace_id seen if None).

    Returns (trace_id, total, dict[seq -> bytes]).
    """
    wanted_id: str | None = trace_id_filter
    total_chunks: int | None = None
    chunks: dict[int, bytes] = {}

    for raw_line in source:
        line = raw_line.rstrip("\r\n")
        # Real-world slog lines often have prefixes like timestamp/level/module.
        # Accept any line that contains MAGIC and parse from that position.
        magic_pos = line.find(MAGIC)
        if magic_pos < 0:
            continue
        line = line[magic_pos:]

        parts = line.split("|", 5)
        if len(parts) != 6:
            continue  # malformed

        _, tid, seq_s, total_s, crc_s, b64 = parts

        if wanted_id is None:
            wanted_id = tid
        elif tid != wanted_id:
            continue  # belongs to a different trace

        try:
            seq   = int(seq_s)
            total = int(total_s)
            crc   = int(crc_s, 16)
        except ValueError:
            print(f"[warn] skipping malformed header fields: {line[:80]}", file=sys.stderr)
            continue

        if seq < 0 or total <= 0 or seq >= total:
            print(f"[warn] invalid seq/total at seq={seq} total={total}", file=sys.stderr)
            continue

        if total_chunks is None:
            total_chunks = total
        elif total != total_chunks:
            print(f"[warn] inconsistent total field at seq {seq}", file=sys.stderr)
            continue

        try:
            raw = base64.b64decode(b64, validate=True)
        except Exception as exc:
            print(f"[warn] base64 decode failed at seq {seq}: {exc}", file=sys.stderr)
            continue

        actual_crc = crc32_compat(raw)
        if actual_crc != crc:
            print(
                f"[error] CRC mismatch at seq {seq}: "
                f"expected {crc:08X}, got {actual_crc:08X}",
                file=sys.stderr,
            )
            continue

        if seq in chunks:
            print(f"[warn] duplicate seq {seq}, keeping first", file=sys.stderr)
            continue

        chunks[seq] = raw

    if wanted_id is None or total_chunks is None:
        return None, None, {}

    return wanted_id, total_chunks, chunks


def reassemble(total_chunks: int, chunks: dict[int, bytes]) -> bytes:
    missing = [i for i in range(total_chunks) if i not in chunks]
    if missing:
        raise RuntimeError(f"Missing {len(missing)} chunk(s): {missing[:10]}{'…' if len(missing)>10 else ''}")
    return b"".join(chunks[i] for i in range(total_chunks))


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Reassemble a Perfetto trace from TracyLite chunk log lines."
    )
    ap.add_argument("input_log",    help="Log file path, or '-' for stdin")
    ap.add_argument("output_trace", help="Output .perfetto-trace file path")
    ap.add_argument("trace_id",     nargs="?", default=None,
                    help="8-char trace_id to extract (default: first found)")
    args = ap.parse_args()

    if args.input_log == "-":
        source = sys.stdin
    else:
        try:
            source = open(args.input_log, "r", encoding="utf-8", errors="replace")
        except OSError as e:
            print(f"[error] cannot open input: {e}", file=sys.stderr)
            return 1

    with source:
        trace_id, total, chunks = parse_log(source, args.trace_id)

    if trace_id is None:
        print("[error] no TRACYLITE_CHUNK lines found", file=sys.stderr)
        return 1

    print(f"[info]  trace_id={trace_id}  found={len(chunks)}/{total} chunks")

    try:
        data = reassemble(total, chunks)
    except RuntimeError as e:
        print(f"[error] {e}", file=sys.stderr)
        return 1

    out_path = Path(args.output_trace)
    try:
        out_path.write_bytes(data)
    except OSError as e:
        print(f"[error] cannot write output: {e}", file=sys.stderr)
        return 1

    print(f"[ok]    wrote {len(data):,} bytes → {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
