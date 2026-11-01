#!/usr/bin/env python3
"""Turn LLVM profile data into an lcov report, then enforce thresholds.

Used by the coverage jobs on Linux, macOS and Windows so all three run the
same pipeline regardless of how LLVM is packaged on the platform:

    python tools/coverage_report.py \
        --objects build/test/boost_virtual_serial_tests \
        --profiles build/profiles \
        --llvm-profdata llvm-profdata --llvm-cov llvm-cov \
        --min-lines 90 --min-branches 80

The merged profile and the lcov file are written next to the build tree so
they can be uploaded as CI artifacts.
"""

from __future__ import annotations

import argparse
import glob
import os
import subprocess
import sys


def run(command: list[str]) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--objects", nargs="+", required=True, help="instrumented executables"
    )
    parser.add_argument(
        "--profiles", required=True, help="directory containing *.profraw"
    )
    parser.add_argument("--llvm-profdata", default="llvm-profdata")
    parser.add_argument("--llvm-cov", default="llvm-cov")
    parser.add_argument("--output-dir", default=None)
    parser.add_argument("--root", default=".")
    parser.add_argument("--min-lines", type=float, default=90.0)
    parser.add_argument("--min-branches", type=float, default=80.0)
    parser.add_argument("--exclude", action="append", default=[])
    args = parser.parse_args()

    output_dir = args.output_dir or os.path.dirname(os.path.abspath(args.profiles))
    os.makedirs(output_dir, exist_ok=True)

    raw = sorted(glob.glob(os.path.join(args.profiles, "*.profraw")))
    if not raw:
        print(f"error: no profile data in {args.profiles}", file=sys.stderr)
        return 1
    print(f"merging {len(raw)} profile file(s)")

    merged = os.path.join(output_dir, "merged.profdata")
    run([args.llvm_profdata, "merge", "-sparse", *raw, "-o", merged])

    lcov_path = os.path.join(output_dir, "coverage.info")
    with open(lcov_path, "w", encoding="utf-8") as handle:
        command = [
            args.llvm_cov,
            "export",
            "-format=lcov",
            f"-instr-profile={merged}",
            *args.objects,
        ]
        print("+ " + " ".join(command), flush=True)
        subprocess.run(command, check=True, stdout=handle)

    coverage_script = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "coverage.py"
    )
    command = [
        sys.executable,
        coverage_script,
        lcov_path,
        "--root",
        args.root,
        "--fail-under-lines",
        str(args.min_lines),
        "--fail-under-branches",
        str(args.min_branches),
    ]
    for pattern in args.exclude:
        command.extend(["--exclude", pattern])

    print(f"+ {' '.join(command)}", flush=True)
    return subprocess.run(command).returncode


if __name__ == "__main__":
    raise SystemExit(main())
