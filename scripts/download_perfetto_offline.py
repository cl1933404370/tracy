#!/usr/bin/env python3
"""
Download Perfetto SDK amalgamated files for offline builds.

Usage:
    python scripts/download_perfetto_offline.py [--tag v49.0]

This downloads perfetto.cc + perfetto.h from a GitHub release tag that
includes the pre-built amalgamated SDK (v49.0 and earlier).

For v50+ tags the SDK must be generated with `tools/gen_amalgamated`
which requires the full source tree.  This script does not support that;
use v49.0 (the default) for offline builds.
"""

import argparse
import os
import sys
import tarfile
import tempfile
import urllib.request


def main():
    parser = argparse.ArgumentParser(description="Download Perfetto offline SDK")
    parser.add_argument("--tag", default="v49.0",
                        help="Git tag to download (default: v49.0)")
    parser.add_argument("--output", default=None,
                        help="Output directory (default: scripts/perfetto-sdk-offline)")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    out_dir = args.output or os.path.join(script_dir, "perfetto-sdk-offline")
    os.makedirs(out_dir, exist_ok=True)

    tag = args.tag
    url = f"https://github.com/nicuveo/nicuveo.github.io/archive/refs/tags/{tag}.tar.gz"
    # Correct URL for google/perfetto
    url = f"https://github.com/google/perfetto/archive/refs/tags/{tag}.tar.gz"

    print(f"Downloading Perfetto {tag} from GitHub...")
    print(f"  URL: {url}")

    try:
        with tempfile.TemporaryDirectory() as tmp:
            tar_path = os.path.join(tmp, "perfetto.tar.gz")
            urllib.request.urlretrieve(url, tar_path)
            print(f"  Downloaded {os.path.getsize(tar_path) // (1024*1024)} MB")

            # Extract only sdk/perfetto.cc and sdk/perfetto.h
            with tarfile.open(tar_path, "r:gz") as tf:
                members = tf.getmembers()
                sdk_prefix = None
                for m in members:
                    if m.name.endswith("/sdk/perfetto.cc"):
                        sdk_prefix = m.name.rsplit("/perfetto.cc", 1)[0]
                        break

                if not sdk_prefix:
                    print("ERROR: This tag does not contain sdk/perfetto.cc")
                    print("       Only v49.0 and earlier include the amalgamated SDK.")
                    print("       Use --tag v49.0")
                    sys.exit(1)

                for name in ["perfetto.cc", "perfetto.h"]:
                    member = f"{sdk_prefix}/{name}"
                    print(f"  Extracting {name}...")
                    f = tf.extractfile(member)
                    if f is None:
                        print(f"  ERROR: {member} not found in archive")
                        sys.exit(1)
                    dst = os.path.join(out_dir, name)
                    with open(dst, "wb") as out:
                        out.write(f.read())

            # Write version marker
            with open(os.path.join(out_dir, "VERSION"), "w") as vf:
                vf.write(tag + "\n")

            print(f"\nOffline SDK ready at: {out_dir}")
            for name in ["perfetto.cc", "perfetto.h", "VERSION"]:
                p = os.path.join(out_dir, name)
                sz = os.path.getsize(p) // 1024
                print(f"  {name}: {sz} KB")

    except Exception as e:
        print(f"ERROR: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
