#!/usr/bin/env python3
"""Tests for tools/check_version.py."""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import unittest
import uuid
from pathlib import Path

TOOL = Path(__file__).resolve().with_name("check_version.py")
TEMP_ROOT = TOOL.parent.parent / ".cache" / "version-tests"


class VersionTests(unittest.TestCase):
    def setUp(self) -> None:
        TEMP_ROOT.mkdir(parents=True, exist_ok=True)
        self.root = TEMP_ROOT / uuid.uuid4().hex
        self.root.mkdir()
        self.write_version("0.1.0-beta.1")

    def tearDown(self) -> None:
        shutil.rmtree(self.root, ignore_errors=True)

    def write_version(self, version: str) -> None:
        (self.root / "version.txt").write_text(version + "\n", encoding="utf-8")
        self.write_config(version)
        self.write_vcpkg(version)
        (self.root / "CHANGELOG.md").write_text(
            f"# Changelog\n\n## [Unreleased]\n\n## [{version}] - 2026-11-02\n\n- Something.\n",
            encoding="utf-8",
        )

    def write_config(self, version: str, number: int | None = None) -> None:
        match = version.split("-")[0].split(".")
        expected = (int(match[0]) * 100000 + int(match[1]) * 100 + int(match[2])
                    if number is None else number)
        path = self.root / "include/boost/virtual_serial/config.hpp"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(
            f"#define BOOST_VIRTUAL_SERIAL_VERSION {expected}\n"
            f'#define BOOST_VIRTUAL_SERIAL_VERSION_STRING "{version}"\n',
            encoding="utf-8",
        )

    def write_vcpkg(self, version: str) -> None:
        (self.root / "vcpkg.json").write_text(
            json.dumps({"name": "boost-virtual-serial", "version": version}),
            encoding="utf-8",
        )

    def run_tool(self, *arguments: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(TOOL), "--root", str(self.root), *arguments],
            capture_output=True,
            text=True,
        )

    def test_consistent_version_passes(self) -> None:
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("version OK: 0.1.0-beta.1", result.stdout)

    def test_print_outputs_only_the_version(self) -> None:
        result = self.run_tool("--print")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "0.1.0-beta.1")

    def test_config_string_mismatch_fails(self) -> None:
        self.write_config("0.1.0-beta.2")
        result = self.run_tool()
        self.assertEqual(result.returncode, 1)
        self.assertIn("config.hpp version string", result.stderr)

    def test_config_number_mismatch_fails(self) -> None:
        self.write_config("0.1.0-beta.1", number=999)
        result = self.run_tool()
        self.assertEqual(result.returncode, 1)
        self.assertIn("config.hpp version number", result.stderr)

    def test_vcpkg_mismatch_fails(self) -> None:
        self.write_vcpkg("0.2.0")
        result = self.run_tool()
        self.assertEqual(result.returncode, 1)
        self.assertIn("vcpkg.json version", result.stderr)

    def test_invalid_version_fails(self) -> None:
        (self.root / "version.txt").write_text("0.1.0beta\n", encoding="utf-8")
        result = self.run_tool()
        self.assertEqual(result.returncode, 1)
        self.assertIn("not a semantic version", result.stderr)

    def test_changelog_required(self) -> None:
        result = self.run_tool("--require-changelog")
        self.assertEqual(result.returncode, 0, result.stderr)

        (self.root / "CHANGELOG.md").write_text("# Changelog\n", encoding="utf-8")
        result = self.run_tool("--require-changelog")
        self.assertEqual(result.returncode, 1)
        self.assertIn("has no '## [0.1.0-beta.1]' section", result.stderr)

    def test_numeric_encoding_for_prerelease(self) -> None:
        # 0.1.0-beta.1 shares the numeric version 0.1.0 -> 100.
        config = (self.root / "include/boost/virtual_serial/config.hpp").read_text(
            encoding="utf-8")
        self.assertIn("BOOST_VIRTUAL_SERIAL_VERSION 100", config)


if __name__ == "__main__":
    unittest.main()
