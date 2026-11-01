#!/usr/bin/env python3
"""Render performance results as Markdown and check them against floors.

Examples
--------
    python tools/perf_report.py perf.json
    python tools/perf_report.py perf-Linux.json perf-macOS.json \
        --floors perf/floors.json
    python tools/perf_report.py perf.json --floors perf/floors.json \
        >> "$GITHUB_STEP_SUMMARY"

Exit status is 0 when the report renders and every floor passes, 1 when a
floor is missed, and 2 when an input file is invalid.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

SCHEMA_VERSION = 1


class ReportError(Exception):
    """An input file or floor definition is not usable."""


def load(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ReportError(f"{path}: {error}") from error
    except json.JSONDecodeError as error:
        raise ReportError(f"{path}: invalid JSON: {error}") from error

    if not isinstance(data, dict) or data.get("schema") != SCHEMA_VERSION:
        raise ReportError(f"{path}: expected schema {SCHEMA_VERSION}")
    results = data.get("results")
    if not isinstance(results, list) or not results:
        raise ReportError(f"{path}: no results")

    for result in results:
        for key in ("name", "unit", "value"):
            if key not in result:
                raise ReportError(f"{path}: result without '{key}'")
        try:
            result["value"] = float(result["value"])
        except (TypeError, ValueError) as error:
            raise ReportError(f"{path}: '{result['name']}' has a non-numeric value") from error
        result.setdefault("samples", [])
        result.setdefault("iterations", 0)
    return data


def load_floor_file(path: Path) -> dict:
    try:
        floors = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ReportError(f"{path}: {error}") from error
    except json.JSONDecodeError as error:
        raise ReportError(f"{path}: invalid JSON: {error}") from error
    if not isinstance(floors, dict):
        raise ReportError(f"{path}: expected an object of metric -> rule")
    for name, rule in floors.items():
        if not isinstance(rule, dict) or not ({"min", "max"} & set(rule)):
            raise ReportError(f"{path}: floor '{name}' needs 'min' or 'max'")
    return floors


def label(data: dict, path: Path) -> str:
    environment = data.get("environment", {})
    os_name = environment.get("os", path.stem)
    compiler = environment.get("compiler", "")
    return f"{os_name} ({compiler})" if compiler else str(os_name)


def format_value(value: float) -> str:
    if value >= 1000.0:
        return f"{value:,.0f}"
    if value >= 10.0:
        return f"{value:.1f}"
    return f"{value:.2f}"


def percentile(samples: list[float], fraction: float) -> float:
    ordered = sorted(float(sample) for sample in samples)
    if not ordered:
        return 0.0
    position = fraction * (len(ordered) - 1)
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def render(files: list[tuple[Path, dict]], title: str) -> list[str]:
    labels: list[str] = []
    for path, data in files:
        name = label(data, path)
        if name in labels:
            name = f"{name} [{path.stem}]"
        labels.append(name)

    lines: list[str] = [f"## {title}", ""]

    for (path, data), name in zip(files, labels):
        environment = data.get("environment", {})
        lines.append(
            f"- **{name}**: {environment.get('cxx_standard', '?')}, "
            f"Boost {environment.get('boost_version', '?')}, "
            f"{environment.get('build_type', '?')}"
        )
    lines.append("")

    order: list[str] = []
    for _, data in files:
        for result in data["results"]:
            if result["name"] not in order:
                order.append(result["name"])

    units: dict[str, str] = {}
    for _, data in files:
        for result in data["results"]:
            units[result["name"]] = result["unit"]

    header = "| Metric | " + " | ".join(labels) + " | Unit |"
    lines.append(header)
    lines.append("|" + "---|" * (len(labels) + 2))

    for name in order:
        cells = []
        for (_, data), name_label in zip(files, labels):
            result = next(
                (entry for entry in data["results"] if entry["name"] == name), None
            )
            cells.append(format_value(result["value"]) if result else "—")
        lines.append(f"| {name.replace('_', ' ')} | " + " | ".join(cells)
                     + f" | {units[name]} |")

    latencies = [
        name for name in order
        if units[name] == "us"
        and any(
            entry.get("samples")
            for _, data in files
            for entry in data["results"]
            if entry["name"] == name
        )
    ]
    if latencies:
        lines.extend(["", "### Latency percentiles", "", ])
        lines.append("| Metric | " + " | ".join(labels) + " | p50 | p90 | p99 |")
        lines.append("|" + "---|" * (len(labels) + 4))
        for name in latencies:
            cells = []
            percentiles = []
            for _, data in files:
                result = next(
                    (entry for entry in data["results"] if entry["name"] == name),
                    None,
                )
                samples = result.get("samples", []) if result else []
                cells.append(format_value(result["value"]) if result else "—")
                if samples:
                    percentiles = [
                        format_value(percentile(samples, 0.50)),
                        format_value(percentile(samples, 0.90)),
                        format_value(percentile(samples, 0.99)),
                    ]
                else:
                    percentiles = ["—", "—", "—"]
            lines.append(f"| {name.replace('_', ' ')} | " + " | ".join(cells)
                         + " | " + " | ".join(percentiles) + " |")
    return lines


def check_floors(
    files: list[tuple[Path, dict]], floors: dict, labels: list[str]
) -> list[str]:
    problems: list[str] = []
    for (path, data), name in zip(files, labels):
        by_name = {result["name"]: result for result in data["results"]}
        for metric, rule in sorted(floors.items()):
            result = by_name.get(metric)
            if result is None:
                problems.append(f"{name}: metric '{metric}' is missing")
                continue
            if "unit" in rule and rule["unit"] != result["unit"]:
                problems.append(
                    f"{name}: '{metric}' is in {result['unit']}, floor expects "
                    f"{rule['unit']}"
                )
                continue
            value = result["value"]
            if "min" in rule and value < float(rule["min"]):
                problems.append(
                    f"{name}: '{metric}' is {format_value(value)} {result['unit']}, "
                    f"below the floor of {format_value(float(rule['min']))}"
                )
            if "max" in rule and value > float(rule["max"]):
                problems.append(
                    f"{name}: '{metric}' is {format_value(value)} {result['unit']}, "
                    f"above the ceiling of {format_value(float(rule['max']))}"
                )
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+", help="performance JSON files")
    parser.add_argument("--floors", help="floor definition JSON")
    parser.add_argument("--title", default="Performance results")
    parser.add_argument("--output", help="write the Markdown report to this file")
    arguments = parser.parse_args(argv)

    loaded: list[tuple[Path, dict]] = []
    try:
        for name in arguments.files:
            path = Path(name)
            loaded.append((path, load(path)))
        floors = load_floor_file(Path(arguments.floors)) if arguments.floors else None
    except ReportError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    labels = [label(data, path) for path, data in loaded]
    lines = render(loaded, arguments.title)

    problems: list[str] = []
    if floors is not None:
        problems = check_floors(loaded, floors, labels)
        lines.extend(["", "### Floors", ""])
        if problems:
            lines.append(f"**{len(problems)} floor(s) missed**")
            lines.extend(f"- {problem}" for problem in problems)
        else:
            lines.append(f"All {len(floors)} floors passed.")

    report = "\n".join(lines) + "\n"
    if arguments.output:
        Path(arguments.output).write_text(report, encoding="utf-8")
    else:
        sys.stdout.write(report)

    if problems:
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
