#!/usr/bin/env python3
"""Tests for tools/perf_report.py.

Run with:

    python -m unittest discover -s tools -p "test_*.py" -v
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import unittest
import uuid
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
REPORTER = TOOLS / "perf_report.py"
TEMP_ROOT = TOOLS.parent / ".cache" / "perf-report-tests"


def make_data(os_name: str = "linux", compiler: str = "gcc 13.3") -> dict:
    return {
        "schema": 1,
        "generated_at": "2026-11-01T00:00:00Z",
        "environment": {
            "os": os_name,
            "compiler": compiler,
            "cxx_standard": "201703",
            "boost_version": "1.92.0",
            "build_type": "Release",
        },
        "results": [
            {
                "name": "mock_sync_write",
                "unit": "MB/s",
                "better": "higher",
                "value": 1234.5,
                "iterations": 3,
                "samples": [1200.0, 1234.5, 1250.0],
                "note": "",
            },
            {
                "name": "tcp_pingpong",
                "unit": "us",
                "better": "lower",
                "value": 50.0,
                "iterations": 5,
                "samples": [40.0, 45.0, 50.0, 60.0, 100.0],
                "note": "",
            },
        ],
    }


class PerfReportTests(unittest.TestCase):
    def setUp(self) -> None:
        TEMP_ROOT.mkdir(parents=True, exist_ok=True)
        self.root = TEMP_ROOT / uuid.uuid4().hex
        self.root.mkdir()

    def tearDown(self) -> None:
        shutil.rmtree(self.root, ignore_errors=True)

    def write(self, name: str, data: dict | str) -> Path:
        path = self.root / name
        text = data if isinstance(data, str) else json.dumps(data)
        path.write_text(text, encoding="utf-8")
        return path

    def run_reporter(self, *arguments: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(REPORTER), *arguments],
            capture_output=True,
            text=True,
        )

    def write_floors(self, floors: dict) -> Path:
        return self.write("floors.json", floors)

    def test_renders_a_table(self) -> None:
        path = self.write("perf.json", make_data())
        result = self.run_reporter(str(path))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("## Performance results", result.stdout)
        self.assertIn("| mock sync write | 1,234 | MB/s |", result.stdout)
        self.assertIn("gcc 13.3", result.stdout)

    def test_renders_several_files_side_by_side(self) -> None:
        linux = self.write("linux.json", make_data("linux", "gcc 13.3"))
        windows = self.write("windows.json", make_data("windows", "msvc 1951"))
        result = self.run_reporter(str(linux), str(windows))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("linux (gcc 13.3)", result.stdout)
        self.assertIn("windows (msvc 1951)", result.stdout)

    def test_latency_percentiles_are_rendered(self) -> None:
        path = self.write("perf.json", make_data())
        result = self.run_reporter(str(path))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Latency percentiles", result.stdout)
        self.assertIn("tcp pingpong", result.stdout)
        # p50 of [40,45,50,60,100] is 50.
        self.assertIn("| 50.0 | 50.0 | 84.0 | 98.4 |", result.stdout)

    def test_floors_pass(self) -> None:
        path = self.write("perf.json", make_data())
        floors = self.write_floors(
            {"mock_sync_write": {"min": 100.0}, "tcp_pingpong": {"max": 100.0}}
        )
        result = self.run_reporter(str(path), "--floors", str(floors))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("All 2 floors passed.", result.stdout)

    def test_floor_minimum_missed(self) -> None:
        path = self.write("perf.json", make_data())
        floors = self.write_floors({"mock_sync_write": {"min": 5000.0}})
        result = self.run_reporter(str(path), "--floors", str(floors))
        self.assertEqual(result.returncode, 1)
        self.assertIn("below the floor", result.stderr)

    def test_floor_maximum_missed(self) -> None:
        path = self.write("perf.json", make_data())
        floors = self.write_floors({"tcp_pingpong": {"max": 10.0}})
        result = self.run_reporter(str(path), "--floors", str(floors))
        self.assertEqual(result.returncode, 1)
        self.assertIn("above the ceiling", result.stderr)

    def test_missing_metric_is_an_error(self) -> None:
        path = self.write("perf.json", make_data())
        floors = self.write_floors({"never_measured": {"min": 1.0}})
        result = self.run_reporter(str(path), "--floors", str(floors))
        self.assertEqual(result.returncode, 1)
        self.assertIn("is missing", result.stderr)

    def test_unit_mismatch_is_an_error(self) -> None:
        path = self.write("perf.json", make_data())
        floors = self.write_floors({"mock_sync_write": {"min": 1.0, "unit": "ops/s"}})
        result = self.run_reporter(str(path), "--floors", str(floors))
        self.assertEqual(result.returncode, 1)
        self.assertIn("floor expects", result.stderr)

    def test_invalid_json_is_a_usage_error(self) -> None:
        path = self.write("broken.json", "{not json")
        result = self.run_reporter(str(path))
        self.assertEqual(result.returncode, 2)
        self.assertIn("invalid JSON", result.stderr)

    def test_wrong_schema_is_a_usage_error(self) -> None:
        data = make_data()
        data["schema"] = 99
        path = self.write("future.json", data)
        result = self.run_reporter(str(path))
        self.assertEqual(result.returncode, 2)
        self.assertIn("expected schema", result.stderr)


if __name__ == "__main__":
    unittest.main()
