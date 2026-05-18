#!/usr/bin/env python3
"""
End-to-end test for prefixed slog reconstruction:
1) Generate deterministic binary payload.
2) Encode into TRACYLITE_CHUNK lines with a realistic slog prefix.
3) Call scripts/tracylite_reconstruct.py to rebuild trace.
4) Byte-compare rebuilt output with original payload.
"""

from __future__ import annotations

import argparse
import base64
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path


def chunk_lines(data: bytes, trace_id: str, raw_chunk_bytes: int = 342):
    total = (len(data) + raw_chunk_bytes - 1) // raw_chunk_bytes
    for seq in range(total):
        begin = seq * raw_chunk_bytes
        end = min(begin + raw_chunk_bytes, len(data))
        chunk = data[begin:end]
        crc = zlib.crc32(chunk) & 0xFFFFFFFF
        b64 = base64.b64encode(chunk).decode("ascii")
        line = f"TRACYLITE_CHUNK|{trace_id[:8]}|{seq:08d}|{total:08d}|{crc:08X}|{b64}"
        # Simulate real slog prefix (timestamp/level/module)
        yield f"2026-05-18 21:00:00.000 [INFO] [HCCL] {line}"


def run_e2e(repo_root: Path) -> int:
    reconstruct = repo_root / "scripts" / "tracylite_reconstruct.py"
    if not reconstruct.exists():
        print(f"[error] reconstruct script not found: {reconstruct}", file=sys.stderr)
        return 1

    # Deterministic payload with mixed byte patterns.
    payload = bytes((i * 73 + 19) & 0xFF for i in range(100_000))

    with tempfile.TemporaryDirectory(prefix="tracy_prefixed_e2e_") as td:
        tmp = Path(td)
        in_log = tmp / "prefixed_device.log"
        out_trace = tmp / "rebuilt.perfetto-trace"

        lines = list(chunk_lines(payload, trace_id="A1B2C3D4", raw_chunk_bytes=342))
        in_log.write_text("\n".join(lines) + "\n", encoding="utf-8")

        proc = subprocess.run(
            [sys.executable, str(reconstruct), str(in_log), str(out_trace), "A1B2C3D4"],
            cwd=str(repo_root),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if proc.returncode != 0:
            print("[error] reconstruct failed", file=sys.stderr)
            if proc.stdout:
                print(proc.stdout, file=sys.stderr)
            if proc.stderr:
                print(proc.stderr, file=sys.stderr)
            return 1

        rebuilt = out_trace.read_bytes()
        if rebuilt != payload:
            print(
                f"[error] byte mismatch: rebuilt={len(rebuilt)} expected={len(payload)}",
                file=sys.stderr,
            )
            return 1

    print("[ok] prefixed slog reconstruct e2e passed")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Prefixed slog reconstruct E2E test")
    ap.add_argument("repo_root", help="Path to Tracy repository root")
    args = ap.parse_args()
    return run_e2e(Path(args.repo_root).resolve())


if __name__ == "__main__":
    sys.exit(main())
