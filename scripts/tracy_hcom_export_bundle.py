#!/usr/bin/env python3
"""
Export a minimal offline Tracy Hcom bundle for external repositories.

This script computes transitive dependencies of tracy_hcom/TracyHcom.cpp via
compiler-generated include deps, copies only required files to an output
folder, and optionally verifies the exported bundle can compile/link.

Examples:
  python3 scripts/tracy_hcom_export_bundle.py --mode perfetto --out /tmp/hcom_bundle
  python3 scripts/tracy_hcom_export_bundle.py --mode core --out /tmp/hcom_core --verify
"""

from __future__ import annotations

import argparse
import datetime
import os
import pathlib
import shlex
import shutil
import subprocess
import sys
from typing import Iterable, List, Set


COMMON_DEFS = [
    "-DTRACY_ENABLE=1",
    "-DTRACY_DELAYED_INIT=1",
    "-DTRACY_SAVE_NO_SEND=1",
    "-DTRACY_ON_DEMAND=1",
    "-DTRACY_NO_BROADCAST=1",
    "-DTRACY_ONLY_LOCALHOST=1",
    "-DTRACY_NO_SAMPLING=1",
    "-DTRACY_NO_CONTEXT_SWITCH=1",
    "-DTRACY_TIMER_FALLBACK=1",
    "-DTRACY_DISALLOW_HW_TIMER=1",
]

EXTRA_PUBLIC_FILES = [
    "tracy_hcom/TracyHcom.hpp",
    "tracy_hcom/TracyHcomApi.h",
    "tracy_hcom/TracyHcomBundle.cmake",
    "public/client/TracyLiteChunkWriter.hpp",
    # Keep compatibility header exported for downstream include stability.
    "public/tracy/TracyHcomm.hpp",
]

MANIFEST_SCHEMA_VERSION = "1"


def default_archive_version() -> str:
    now = datetime.datetime.now(datetime.timezone.utc)
    return "HCOMM-ARCHIVE-{:04d}.{:02d}.{:02d}-01".format(now.year, now.month, now.day)


def run(cmd: List[str], cwd: pathlib.Path) -> str:
    proc = subprocess.run(
        cmd,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            "Command failed (exit={}):\n{}\n{}".format(
                proc.returncode,
                " ".join(shlex.quote(c) for c in cmd),
                proc.stdout,
            )
        )
    return proc.stdout


def normalize_relpath(p: str) -> str:
    return str(pathlib.PurePosixPath(p))


def parse_mm_output(mm_text: str, prefixes: Iterable[str]) -> Set[str]:
    normalized = mm_text.replace("\\\n", " ").replace("\\", " ")
    tokens = normalized.split()

    out: Set[str] = set()
    wanted = tuple(prefixes)
    for tok in tokens:
        if tok.endswith(":"):
            continue
        tok = tok.strip()
        if not tok:
            continue
        if tok.startswith(wanted):
            out.add(normalize_relpath(tok))
    return out


def compute_manifest(repo: pathlib.Path, mode: str, cxx: str) -> List[str]:
    include_flags = ["-I./tracy_hcom", "-I./public"]
    defs = list(COMMON_DEFS)
    prefixes = ["tracy_hcom/", "public/"]

    if mode == "perfetto":
        defs.append("-DTRACYHCOM_ENABLE_PERFETTO=1")
        include_flags.append("-I./scripts/perfetto-sdk-offline")
        prefixes.append("scripts/perfetto-sdk-offline/")

    cmd = [
        cxx,
        "-std=c++17",
        *defs,
        *include_flags,
        "-MM",
        "tracy_hcom/TracyHcom.cpp",
    ]
    mm = run(cmd, repo)
    files = parse_mm_output(mm, prefixes)

    if mode == "perfetto":
        perfetto_dir = repo / "scripts/perfetto-sdk-offline"
        files.add("scripts/perfetto-sdk-offline/perfetto.h")

        # TracyHcomBundle.cmake requires at least one implementation TU.
        # Prefer protozero subset when present; keep perfetto.cc as fallback.
        has_impl = False
        for rel in (
            "scripts/perfetto-sdk-offline/perfetto_protozero.cc",
            "scripts/perfetto-sdk-offline/perfetto.cc",
        ):
            if (repo / rel).exists():
                files.add(rel)
                has_impl = True

        if (perfetto_dir / "VERSION").exists():
            files.add("scripts/perfetto-sdk-offline/VERSION")

        if not has_impl:
            raise FileNotFoundError(
                "Perfetto mode requires implementation source: "
                "scripts/perfetto-sdk-offline/perfetto_protozero.cc or perfetto.cc"
            )

    for f in EXTRA_PUBLIC_FILES:
        files.add(normalize_relpath(f))

    # Ensure entrypoint exists in manifest.
    files.add("tracy_hcom/TracyHcom.cpp")
    return sorted(files)


def copy_manifest(repo: pathlib.Path, out_dir: pathlib.Path, files: List[str]) -> None:
    for rel in files:
        src = repo / rel
        if not src.exists():
            raise FileNotFoundError("Missing file in manifest: {}".format(rel))
        dst = out_dir / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)


def write_manifest(
    out_dir: pathlib.Path,
    files: List[str],
    mode: str,
    archive_version: str,
    total_size_bytes: int,
) -> None:
    manifest = out_dir / "manifest.txt"
    generated_utc = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    with manifest.open("w", encoding="utf-8") as f:
        f.write("schema_version={}\n".format(MANIFEST_SCHEMA_VERSION))
        f.write("archive_version={}\n".format(archive_version))
        f.write("generated_utc={}\n".format(generated_utc))
        f.write("mode={}\n".format(mode))
        f.write("entrypoint=tracy_hcom/TracyHcom.cpp\n")
        f.write("file_count={}\n".format(len(files)))
        f.write("total_bytes={}\n".format(total_size_bytes))
        f.write("required_include_0=tracy_hcom\n")
        f.write("required_include_1=public\n")
        if mode == "perfetto":
            f.write("required_include_2=scripts/perfetto-sdk-offline\n")
        f.write("required_define_0=TRACY_ENABLE=1\n")
        f.write("required_define_1=TRACY_SAVE_NO_SEND=1\n")
        if mode == "perfetto":
            f.write("required_define_2=TRACYHCOM_ENABLE_PERFETTO=1\n")
        f.write("files:\n")
        for rel in files:
            f.write(rel)
            f.write("\n")


def write_integration_snippet(out_dir: pathlib.Path, mode: str) -> None:
    snippet = out_dir / "INTEGRATE_MINIMAL.cmake"
    lines = [
        "# Minimal integration snippet for external repo",
        "# Assume this exported folder is available as HCOM_BUNDLE_ROOT.",
        "set(TRACY_ROOT \"${HCOM_BUNDLE_ROOT}\")",
        "set(TRACY_HCOM_DIR \"${TRACY_ROOT}/tracy_hcom\")",
        "include(\"${TRACY_HCOM_DIR}/TracyHcomBundle.cmake\")",
        "",
        "# add_library(MyExternalSo SHARED ...)",
    ]
    if mode == "perfetto":
        lines.append("tracyhcom_embed(MyExternalSo PERFETTO)")
        lines.append("target_include_directories(MyExternalSo PRIVATE \"${TRACY_ROOT}/scripts/perfetto-sdk-offline\")")
    else:
        lines.append("tracyhcom_embed(MyExternalSo)")
    lines.extend(
        [
            "",
            "# Optional for host-side usage in your own sources:",
            "# target_compile_definitions(MyExternalSo PRIVATE USE_TRACY_HCOM_BUNDLE=1)",
        ]
    )
    snippet.write_text("\n".join(lines) + "\n", encoding="utf-8")


def verify_export(out_dir: pathlib.Path, mode: str, cxx: str) -> None:
    defs = list(COMMON_DEFS)
    incs = ["-I../tracy_hcom", "-I../public"]
    if mode == "perfetto":
        defs.append("-DTRACYHCOM_ENABLE_PERFETTO=1")
        incs.append("-I../scripts/perfetto-sdk-offline")

    # Keep verification build products out of the exported bundle root.
    verify_dir = out_dir / ".verify-build"
    if verify_dir.exists():
        shutil.rmtree(verify_dir)
    verify_dir.mkdir(parents=True, exist_ok=True)

    compile_cmd = [
        cxx,
        "-fPIC",
        "-std=c++17",
        *defs,
        *incs,
        "-c",
        "../tracy_hcom/TracyHcom.cpp",
        "-o",
        "tracy_hcom.o",
    ]
    try:
        run(compile_cmd, verify_dir)

        extra_objects: List[str] = []
        if mode == "perfetto":
            perfetto_sources = [
                pathlib.Path("../scripts/perfetto-sdk-offline/perfetto_protozero.cc"),
                pathlib.Path("../scripts/perfetto-sdk-offline/perfetto.cc"),
            ]
            selected: pathlib.Path | None = None
            for src in perfetto_sources:
                if (verify_dir / src).exists():
                    selected = src
                    break

            if selected is None:
                raise RuntimeError(
                    "verify_export(perfetto): no perfetto implementation source found"
                )

            perfetto_compile_cmd = [
                cxx,
                "-fPIC",
                "-std=c++17",
                "-I../scripts/perfetto-sdk-offline",
                "-c",
                str(selected),
                "-o",
                "perfetto_impl.o",
            ]
            run(perfetto_compile_cmd, verify_dir)
            extra_objects.append("perfetto_impl.o")

        link_cmd = [
            cxx,
            "-shared",
            "tracy_hcom.o",
            *extra_objects,
            "-o",
            "libtracy_hcom_export.so",
            "-ldl",
            "-lpthread",
        ]
        run(link_cmd, verify_dir)
    finally:
        shutil.rmtree(verify_dir, ignore_errors=True)


def total_bytes(repo: pathlib.Path, files: Iterable[str]) -> int:
    s = 0
    for rel in files:
        s += (repo / rel).stat().st_size
    return s


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Export minimal offline Tracy Hcom bundle")
    p.add_argument("--mode", choices=["core", "perfetto"], default="perfetto", help="Export mode")
    p.add_argument("--out", required=True, help="Output directory")
    p.add_argument("--repo", default=".", help="Tracy repository root (default: cwd)")
    p.add_argument("--cxx", default=os.environ.get("CXX", "/usr/bin/c++"), help="C++ compiler")
    p.add_argument("--archive-version", default=default_archive_version(), help="Archive version label written into manifest")
    p.add_argument("--verify", action="store_true", help="Compile/link exported bundle to verify usability")
    p.add_argument("--force", action="store_true", help="Overwrite existing output directory")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    repo = pathlib.Path(args.repo).resolve()
    out_dir = pathlib.Path(args.out).resolve()

    if not (repo / "tracy_hcom/TracyHcom.cpp").exists():
        print("[error] Not a Tracy repo root: {}".format(repo), file=sys.stderr)
        return 2

    if out_dir.exists():
        if not args.force:
            print("[error] output exists, use --force: {}".format(out_dir), file=sys.stderr)
            return 2
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    files = compute_manifest(repo, args.mode, args.cxx)
    copy_manifest(repo, out_dir, files)

    size = total_bytes(repo, files)
    write_manifest(out_dir, files, args.mode, args.archive_version, size)
    write_integration_snippet(out_dir, args.mode)

    if args.verify:
        verify_export(out_dir, args.mode, args.cxx)

    print("[ok] export complete")
    print("archive_version={}".format(args.archive_version))
    print("mode={}".format(args.mode))
    print("out={}".format(out_dir))
    print("file_count={}".format(len(files)))
    print("total_bytes={}".format(size))
    print("manifest={}".format(out_dir / "manifest.txt"))
    print("snippet={}".format(out_dir / "INTEGRATE_MINIMAL.cmake"))
    if args.verify:
        print("verify=PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
