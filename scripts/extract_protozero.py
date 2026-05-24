#!/usr/bin/env python3
"""
Extract the ProtoZero-only subset from the amalgamated Perfetto SDK perfetto.cc.

The amalgamated perfetto.cc has this layout:
  1. Preamble (#define PERFETTO_IMPLEMENTATION, #include "perfetto.h")
  2. Internal headers (ext/base/*.h - inline utils like AlignUp, logging)
  3. Base sources  (src/base/*.cc  - platform utilities)
  4. ProtoZero sources (src/protozero/*.cc - serializer implementation)
  5. gen.cc files  (gen/protos/*.gen.cc - protobuf code-gen, huge)
  6. Tracing runtime (src/tracing/*.cc - service, IPC, threads, huge)

Tracy only uses (4) at runtime, but (4) depends on inline functions from
(2) and may link to symbols from (3).  We keep (1)-(4) and strip (5)-(6).

Typical reduction: ~80% (v49: 11k/67k, v54: 15k/83k kept).

Usage:
    python scripts/extract_protozero.py [--input <perfetto.cc>] [--output <perfetto_protozero.cc>]

Defaults:
    --input  scripts/perfetto-sdk-offline/perfetto.cc
    --output scripts/perfetto-sdk-offline/perfetto_protozero.cc
"""

import argparse
import re
import sys
import os

SECTION_MARKER = re.compile(r"^// gen_amalgamated begin source: (.+)$")


def extract_protozero(input_path: str, output_path: str) -> None:
    with open(input_path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    # Find all source section boundaries.
    source_sections = []  # (name, start_line_idx)
    for i, line in enumerate(lines):
        m = SECTION_MARKER.match(line.rstrip())
        if m:
            source_sections.append((m.group(1), i))

    if not source_sections:
        print(f"ERROR: No source sections found in {input_path}", file=sys.stderr)
        sys.exit(1)

    # Find the cut-off: the first source section AFTER the last protozero
    # source that is NOT a protozero section itself.
    last_protozero_idx = -1
    for idx, (name, _) in enumerate(source_sections):
        if name.startswith("src/protozero/") and "filtering/" not in name:
            last_protozero_idx = idx

    if last_protozero_idx < 0:
        print(f"ERROR: No src/protozero/ sections found in {input_path}", file=sys.stderr)
        sys.exit(1)

    # Cut at the start of the next non-protozero section.
    if last_protozero_idx + 1 < len(source_sections):
        cut_line = source_sections[last_protozero_idx + 1][1]
    else:
        cut_line = len(lines)

    # Collect kept protozero section names for reporting.
    kept_protozero = [name for name, _ in source_sections[:last_protozero_idx + 1]
                      if name.startswith("src/protozero/")]

    extracted_lines = lines[:cut_line]
    total_out = len(extracted_lines)

    with open(output_path, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(extracted_lines)

    pct = (1.0 - total_out / len(lines)) * 100
    print(f"Kept preamble + headers + base + protozero (lines 1-{cut_line}).")
    print(f"Stripped {len(lines) - cut_line:,} lines of gen.cc / tracing / IPC code.")
    print(f"ProtoZero sections kept ({len(kept_protozero)}):")
    for s in kept_protozero:
        print(f"  {s}")
    print(f"\nOutput: {output_path}")
    print(f"  {total_out:,} lines (vs {len(lines):,} in full perfetto.cc, {pct:.1f}% reduction)")


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(script_dir)

    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", default=os.path.join(repo_root, "scripts", "perfetto-sdk-offline", "perfetto.cc"),
                        help="Path to the full amalgamated perfetto.cc")
    parser.add_argument("--output", default=os.path.join(repo_root, "scripts", "perfetto-sdk-offline", "perfetto_protozero.cc"),
                        help="Path to write the protozero-only subset")
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"ERROR: Input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    extract_protozero(args.input, args.output)


if __name__ == "__main__":
    main()
