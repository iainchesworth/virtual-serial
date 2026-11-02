#!/usr/bin/env python3
"""Check that the project version is consistent across every surface.

The version lives in ``version.txt``.  CMake reads it, the public header
exposes it at compile time, vcpkg records it and the changelog names the
release.  This script fails when one of those slips out of step.

Usage
-----
    python tools/check_version.py
    python tools/check_version.py --print
    python tools/check_version.py --require-changelog
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

SEMVER = re.compile(
    r"^(?P<major>0|[1-9][0-9]*)\."
    r"(?P<minor>0|[1-9][0-9]*)\."
    r"(?P<patch>0|[1-9][0-9]*)"
    r"(?:-(?P<pre>[0-9A-Za-z.-]+))?$"
)
CONFIG_STRING = re.compile(r'#\s*define\s+BOOST_VIRTUAL_SERIAL_VERSION_STRING\s+"([^"]+)"')
CONFIG_NUMBER = re.compile(r"#\s*define\s+BOOST_VIRTUAL_SERIAL_VERSION\s+([0-9]+)")


class VersionError(Exception):
    pass


def read_version(root: Path) -> str:
    path = root / "version.txt"
    if not path.exists():
        raise VersionError(f"{path} does not exist")
    version = path.read_text(encoding="utf-8").strip()
    if not SEMVER.match(version):
        raise VersionError(
            f"{path} contains '{version}', which is not a semantic version")
    return version


def expected_number(version: str) -> int:
    match = SEMVER.match(version)
    assert match is not None
    return (int(match.group("major")) * 100000
            + int(match.group("minor")) * 100
            + int(match.group("patch")))


def check(root: Path, require_changelog: bool = False) -> str:
    version = read_version(root)
    errors: list[str] = []

    config_path = root / "include/boost/virtual_serial/config.hpp"
    if not config_path.exists():
        errors.append(f"{config_path} does not exist")
    else:
        config = config_path.read_text(encoding="utf-8")
        declared = CONFIG_STRING.search(config)
        if declared is None:
            errors.append(f"{config_path} has no BOOST_VIRTUAL_SERIAL_VERSION_STRING")
        elif declared.group(1) != version:
            errors.append(
                f"config.hpp version string is '{declared.group(1)}', "
                f"version.txt says '{version}'")
        number = CONFIG_NUMBER.search(config)
        if number is None:
            errors.append(f"{config_path} has no BOOST_VIRTUAL_SERIAL_VERSION")
        elif int(number.group(1)) != expected_number(version):
            errors.append(
                f"config.hpp version number is {number.group(1)}, expected "
                f"{expected_number(version)}")

    vcpkg_path = root / "vcpkg.json"
    if not vcpkg_path.exists():
        errors.append(f"{vcpkg_path} does not exist")
    else:
        try:
            manifest = json.loads(vcpkg_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as error:
            errors.append(f"{vcpkg_path} is not valid JSON: {error}")
        else:
            if manifest.get("version") != version:
                errors.append(
                    f"vcpkg.json version is '{manifest.get('version')}', "
                    f"version.txt says '{version}'")

    if require_changelog:
        changelog = root / "CHANGELOG.md"
        if not changelog.exists():
            errors.append(f"{changelog} does not exist")
        elif f"## [{version}]" not in changelog.read_text(encoding="utf-8"):
            errors.append(f"CHANGELOG.md has no '## [{version}]' section")

    if errors:
        raise VersionError("\n".join(errors))
    return version


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=".", help="repository root (default: .)")
    parser.add_argument("--print", action="store_true",
                        help="print the version and do not run the checks")
    parser.add_argument("--require-changelog", action="store_true",
                        help="also require a changelog section for the version")
    arguments = parser.parse_args(argv)

    root = Path(arguments.root).resolve()
    try:
        version = read_version(root)
        if arguments.print:
            print(version)
            return 0
        version = check(root, arguments.require_changelog)
    except VersionError as error:
        for line in str(error).splitlines():
            print(f"error: {line}", file=sys.stderr)
        return 1

    print(f"version OK: {version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
