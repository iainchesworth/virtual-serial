#!/usr/bin/env python3
"""Enforce line and branch coverage thresholds from lcov data.

The library is header-only, so coverage is measured on the headers that the
test binaries actually instantiate.  lcov input is produced either by
``llvm-cov export -format=lcov`` (Clang, macOS and Windows) or by
``gcovr --lcov`` (GCC on Linux); this script is the single, platform neutral
gate that both CI jobs run.

Usage:
    python tools/coverage.py coverage.info --root . \
        --fail-under-lines 90 --fail-under-branches 80

Exit status is 0 only when both thresholds are met.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import sys
from collections import defaultdict


class FileCoverage:
    def __init__(self) -> None:
        self.lines: dict[int, int] = {}
        self.branches: dict[tuple[int, int, int], int] = {}

    def add_line(self, line: int, count: int) -> None:
        # A header is instantiated by several translation units; a line counts
        # as covered when at least one instantiation executed it.
        self.lines[line] = max(self.lines.get(line, 0), count)

    def add_branch(self, line: int, block: int, branch: int, taken: int) -> None:
        key = (line, block, branch)
        self.branches[key] = max(self.branches.get(key, 0), taken)

    @property
    def lines_total(self) -> int:
        return len(self.lines)

    @property
    def lines_hit(self) -> int:
        return sum(1 for count in self.lines.values() if count > 0)

    @property
    def branches_total(self) -> int:
        return len(self.branches)

    @property
    def branches_hit(self) -> int:
        return sum(1 for count in self.branches.values() if count > 0)


def parse_lcov(paths: list[str]) -> dict[str, FileCoverage]:
    files: dict[str, FileCoverage] = defaultdict(FileCoverage)
    current: FileCoverage | None = None

    for path in paths:
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            for raw in handle:
                line = raw.rstrip("\n")
                if line.startswith("SF:"):
                    current = files[line[3:]]
                elif line == "end_of_record":
                    current = None
                elif line.startswith("DA:") and current is not None:
                    fields = line[3:].split(",")
                    if len(fields) >= 2:
                        try:
                            current.add_line(int(fields[0]), int(fields[1]))
                        except ValueError:
                            continue
                elif line.startswith("BRDA:") and current is not None:
                    fields = line[5:].split(",")
                    if len(fields) >= 4:
                        try:
                            taken = 0 if fields[3] == "-" else int(fields[3])
                            current.add_branch(
                                int(fields[0]), int(fields[1]), int(fields[2]), taken
                            )
                        except ValueError:
                            continue
    return files


def normalize(path: str, root: str) -> str:
    normalized = path.replace("\\", "/")
    root_normalized = os.path.abspath(root).replace("\\", "/").rstrip("/")
    if normalized.lower().startswith(root_normalized.lower() + "/"):
        normalized = normalized[len(root_normalized) + 1 :]
    if normalized.startswith("./"):
        normalized = normalized[2:]
    return normalized


def selected(path: str, includes: list[str], excludes: list[str]) -> bool:
    if includes and not any(
        fnmatch.fnmatch(path, pattern) or path.startswith(pattern.rstrip("*"))
        for pattern in includes
    ):
        return False
    return not any(fnmatch.fnmatch(path, pattern) for pattern in excludes)


def percent(hit: int, total: int) -> float:
    return 100.0 * hit / total if total else 100.0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("lcov", nargs="+", help="lcov coverage files")
    parser.add_argument("--root", default=".", help="repository root for path names")
    parser.add_argument(
        "--include",
        action="append",
        default=None,
        help="glob of files to include (default: include/boost/virtual_serial/*)",
    )
    parser.add_argument("--exclude", action="append", default=[], help="glob to exclude")
    parser.add_argument("--fail-under-lines", type=float, default=0.0)
    parser.add_argument("--fail-under-branches", type=float, default=0.0)
    parser.add_argument(
        "--show-missing",
        type=int,
        default=0,
        metavar="N",
        help="print the N least covered files with their uncovered lines",
    )
    parser.add_argument("--json", dest="json_path", default=None)
    args = parser.parse_args()

    includes = args.include or ["include/boost/virtual_serial/*"]
    files = parse_lcov(args.lcov)

    rows = []
    total_lines = total_lines_hit = 0
    total_branches = total_branches_hit = 0
    details = {}

    for path, data in files.items():
        relative = normalize(path, args.root)
        if not selected(relative, includes, args.exclude):
            continue
        if data.lines_total == 0:
            continue
        rows.append(
            (
                relative,
                data.lines_hit,
                data.lines_total,
                data.branches_hit,
                data.branches_total,
                sorted(line for line, count in data.lines.items() if count == 0),
            )
        )
        total_lines += data.lines_total
        total_lines_hit += data.lines_hit
        total_branches += data.branches_total
        total_branches_hit += data.branches_hit
        details[relative] = {
            "lines_total": data.lines_total,
            "lines_hit": data.lines_hit,
            "branches_total": data.branches_total,
            "branches_hit": data.branches_hit,
            "uncovered_lines": sorted(
                line for line, count in data.lines.items() if count == 0
            ),
        }

    rows.sort(key=lambda row: (percent(row[1], row[2]), row[0]))

    width = max([len(row[0]) for row in rows] + [24])
    header = f"{'file'.ljust(width)}  {'lines'.rjust(15)}  {'branches'.rjust(15)}"
    print(header)
    print("-" * len(header))
    for relative, lines_hit, lines_total, branches_hit, branches_total, _ in rows:
        print(
            f"{relative.ljust(width)}  "
            f"{f'{lines_hit}/{lines_total}'.rjust(9)} {percent(lines_hit, lines_total):6.2f}%  "
            f"{f'{branches_hit}/{branches_total}'.rjust(9)} {percent(branches_hit, branches_total):6.2f}%"
        )
    print("-" * len(header))
    line_rate = percent(total_lines_hit, total_lines)
    branch_rate = percent(total_branches_hit, total_branches)
    print(
        f"{'TOTAL'.ljust(width)}  "
        f"{f'{total_lines_hit}/{total_lines}'.rjust(9)} {line_rate:6.2f}%  "
        f"{f'{total_branches_hit}/{total_branches}'.rjust(9)} {branch_rate:6.2f}%"
    )

    if args.show_missing:
        print("\nleast covered files:")
        for row in rows[: args.show_missing]:
            if not row[5]:
                continue
            lines = ", ".join(str(line) for line in row[5][:60])
            suffix = " ..." if len(row[5]) > 60 else ""
            print(f"  {row[0]}: {lines}{suffix}")

    if args.json_path:
        with open(args.json_path, "w", encoding="utf-8") as handle:
            json.dump(
                {
                    "lines": {"hit": total_lines_hit, "total": total_lines},
                    "branches": {"hit": total_branches_hit, "total": total_branches},
                    "files": details,
                },
                handle,
                indent=2,
            )

    failures = []
    if total_lines and line_rate < args.fail_under_lines:
        failures.append(
            f"line coverage {line_rate:.2f}% is below the required "
            f"{args.fail_under_lines:.2f}%"
        )
    if total_branches and branch_rate < args.fail_under_branches:
        failures.append(
            f"branch coverage {branch_rate:.2f}% is below the required "
            f"{args.fail_under_branches:.2f}%"
        )

    for failure in failures:
        print(f"FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1

    if total_lines:
        print(
            f"OK: line coverage {line_rate:.2f}% (>= {args.fail_under_lines:.2f}%), "
            f"branch coverage {branch_rate:.2f}% (>= {args.fail_under_branches:.2f}%)"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
