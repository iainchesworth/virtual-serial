#!/usr/bin/env python3
"""Manage CHANGELOG.md with Keep a Changelog fragments.

Each pull request adds one small file to ``changelog.d/`` instead of editing
``CHANGELOG.md``; the resulting conflicts are limited to the files two branches
actually touch.  At release time the fragments are folded into a dated version
section.

Fragment naming
---------------
``<slug>.<category>.md`` where category is one of ``added``, ``changed``,
``deprecated``, ``removed``, ``fixed`` or ``security``::

    changelog.d/async-contract.added.md
    changelog.d/1217.fixed.md

The content is Markdown.  A fragment may contain one entry, several ``-``
bullets, or a ``-`` bullet continued over several lines.  Files whose name
starts with ``_`` or ``.`` are ignored, as is ``README.md``, so templates can
live next to the fragments.

Usage
-----
    python tools/changelog.py --check          # validate fragments (CI)
    python tools/changelog.py --dry-run        # preview the Unreleased section
    python tools/changelog.py                  # merge and delete fragments
    python tools/changelog.py --release 1.1.0  # cut a dated release
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

DEFAULT_REPO_URL = "https://github.com/iainchesworth/virtual-serial"

CATEGORIES = [
    ("added", "Added"),
    ("changed", "Changed"),
    ("deprecated", "Deprecated"),
    ("removed", "Removed"),
    ("fixed", "Fixed"),
    ("security", "Security"),
]
CATEGORY_TITLES = dict(CATEGORIES)
CATEGORY_BY_TITLE = {title: key for key, title in CATEGORIES}

FRAGMENT_RE = re.compile(
    r"^(?P<slug>[a-z0-9][a-z0-9-]*)"
    r"\.(?P<category>added|changed|deprecated|removed|fixed|security)\.md$"
)
UNRELEASED_RE = re.compile(r"^## \[Unreleased\]\s*$")
VERSION_RE = re.compile(
    r"^## \[(?P<version>[0-9]+\.[0-9]+\.[0-9]+)\]"
    r"(?:\s*-\s*(?P<date>[0-9]{4}-[0-9]{2}-[0-9]{2}))?\s*$"
)
ANY_H2_RE = re.compile(r"^## \[")
SECTION_RE = re.compile(r"^### (?P<title>.+?)\s*$")
LINK_RE = re.compile(r"^\[(?P<label>[^\]]+)\]:\s*(?P<url>\S+)\s*$")
SEMVER_RE = re.compile(r"^(?P<major>0|[1-9][0-9]*)\.(?P<minor>0|[1-9][0-9]*)"
                       r"\.(?P<patch>0|[1-9][0-9]*)$")
ISO_DATE_RE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}$")


class ChangelogError(Exception):
    """A fragment or changelog problem the user has to fix."""


@dataclass
class Fragment:
    path: Path
    slug: str
    category: str
    bullets: list[str] = field(default_factory=list)


def ignored(name: str) -> bool:
    return name == "README.md" or name.startswith("_") or name.startswith(".")


def parse_bullets(text: str) -> list[str]:
    """Turns fragment text into Markdown bullet lines."""
    lines = [line.rstrip() for line in text.strip().splitlines()]
    if not lines:
        return []

    has_bullets = any(line.startswith(("- ", "* ")) for line in lines)
    if not has_bullets:
        joined = " ".join(line.strip() for line in lines if line.strip())
        return [f"- {joined}"] if joined else []

    bullets: list[str] = []
    current: str | None = None
    for line in lines:
        if line.startswith(("- ", "* ")):
            if current:
                bullets.append(current)
            current = "- " + line[2:].strip()
        elif line.strip() and current is not None:
            current += " " + line.strip()
        elif not line.strip() and current is not None:
            # A blank line ends the current entry.
            bullets.append(current)
            current = None
    if current:
        bullets.append(current)
    return [bullet for bullet in bullets if bullet != "-"]


def read_fragment(path: Path) -> Fragment:
    match = FRAGMENT_RE.match(path.name)
    if not match:
        raise ChangelogError(
            f"{path.name}: expected <slug>.<category>.md with category one of "
            + ", ".join(key for key, _ in CATEGORIES)
        )
    bullets = parse_bullets(path.read_text(encoding="utf-8"))
    if not bullets:
        raise ChangelogError(f"{path.name}: the fragment is empty")
    return Fragment(path, match.group("slug"), match.group("category"), bullets)


def collect_fragments(directory: Path) -> list[Fragment]:
    if not directory.exists():
        return []
    fragments: list[Fragment] = []
    errors: list[str] = []
    for path in sorted(directory.iterdir()):
        if not path.is_file() or path.suffix != ".md" or ignored(path.name):
            continue
        try:
            fragments.append(read_fragment(path))
        except ChangelogError as error:
            errors.append(str(error))
    if errors:
        raise ChangelogError("\n".join(errors))
    return fragments


def find_unreleased(lines: list[str]) -> int:
    for index, line in enumerate(lines):
        if UNRELEASED_RE.match(line):
            return index
    raise ChangelogError("CHANGELOG.md has no '## [Unreleased]' section")


def split_unreleased(
    text: str,
) -> tuple[list[str], dict[str, list[str]], list[str]]:
    lines = text.splitlines()
    start = find_unreleased(lines)
    end = len(lines)
    for index in range(start + 1, len(lines)):
        if ANY_H2_RE.match(lines[index]):
            end = index
            break

    existing: dict[str, list[str]] = {}
    category: str | None = None
    for line in lines[start + 1 : end]:
        section = SECTION_RE.match(line)
        if section:
            category = CATEGORY_BY_TITLE.get(section.group("title"))
            if category is None:
                raise ChangelogError(
                    f"unknown category '### {section.group('title')}' in the "
                    "Unreleased section; expected one of "
                    + ", ".join(title for _, title in CATEGORIES)
                )
            continue
        if line.startswith("- ") and category is not None:
            existing.setdefault(category, []).append(line.rstrip())

    return lines[: start + 1], existing, lines[end:]


def merge_bullets(
    existing: dict[str, list[str]], fragments: list[Fragment]
) -> dict[str, list[str]]:
    merged = {key: list(bullets) for key, bullets in existing.items()}
    for fragment in fragments:
        bucket = merged.setdefault(fragment.category, [])
        for bullet in fragment.bullets:
            if bullet not in bucket:
                bucket.append(bullet)
    return {key: bullets for key, bullets in merged.items() if bullets}


def render_unreleased(categories: dict[str, list[str]]) -> list[str]:
    lines: list[str] = []
    for key, title in CATEGORIES:
        bullets = categories.get(key)
        if not bullets:
            continue
        if lines:
            lines.append("")
        lines.append(f"### {title}")
        lines.append("")
        lines.extend(bullets)
    if lines:
        lines.append("")
    return lines


def join_changelog(prefix: list[str], body: list[str], suffix: list[str]) -> str:
    lines = list(prefix)
    if lines and lines[-1].strip():
        lines.append("")
    lines.extend(body)
    if suffix:
        if lines and lines[-1].strip():
            lines.append("")
        lines.extend(suffix)
    text = "\n".join(lines).rstrip() + "\n"
    return re.sub(r"\n{3,}", "\n\n", text)


def load(root: Path) -> tuple[Path, str]:
    path = root / "CHANGELOG.md"
    if not path.exists():
        raise ChangelogError(f"{path} does not exist")
    return path, path.read_text(encoding="utf-8")


def check(root: Path) -> list[Fragment]:
    """Validates the changelog and every fragment; returns the fragments."""
    _, text = load(root)
    lines = text.splitlines()
    find_unreleased(lines)

    versions: list[str] = []
    for line in lines:
        match = VERSION_RE.match(line)
        if match:
            versions.append(match.group("version"))
    duplicates = {version for version in versions if versions.count(version) > 1}
    if duplicates:
        raise ChangelogError(
            "duplicate version sections: " + ", ".join(sorted(duplicates))
        )

    return collect_fragments(root / "changelog.d")


def merge(root: Path, keep: bool, dry_run: bool) -> int:
    path, text = load(root)
    fragments = collect_fragments(root / "changelog.d")
    if not fragments:
        print("no changelog fragments to merge")
        return 0

    prefix, existing, suffix = split_unreleased(text)
    merged = merge_bullets(existing, fragments)
    body = render_unreleased(merged)
    updated = join_changelog(prefix, body, suffix)

    if dry_run:
        print("## [Unreleased]\n")
        print("\n".join(body).rstrip())
        print(f"\n({len(fragments)} fragment(s); nothing was modified)")
        return 0

    path.write_text(updated, encoding="utf-8")
    if not keep:
        for fragment in fragments:
            fragment.path.unlink()
    print(
        f"merged {len(fragments)} fragment(s) into [Unreleased]"
        + ("" if keep else " and removed them")
    )
    return 0


def semver(version: str) -> tuple[int, int, int]:
    match = SEMVER_RE.match(version)
    if not match:
        raise ChangelogError(f"'{version}' is not a semantic version (X.Y.Z)")
    return (int(match.group("major")), int(match.group("minor")), int(match.group("patch")))


def release(root: Path, version: str, date: str, keep: bool, repo_url: str) -> int:
    latest = semver(version)
    path, text = load(root)
    lines = text.splitlines()
    find_unreleased(lines)

    known = [
        semver(match.group("version"))
        for match in (VERSION_RE.match(line) for line in lines)
        if match
    ]
    if known and latest <= max(known):
        raise ChangelogError(
            f"{version} must be greater than the latest released version "
            f"{'.'.join(str(part) for part in max(known))}"
        )

    fragments = collect_fragments(root / "changelog.d")
    prefix, existing, suffix = split_unreleased(text)
    merged = merge_bullets(existing, fragments)
    if not merged:
        raise ChangelogError("there is nothing to release: no fragments and an empty Unreleased section")

    heading = f"## [{version}] - {date}"
    section = [heading, ""] + render_unreleased(merged)
    updated = join_changelog(prefix, section, suffix)

    # Refresh the link references at the bottom of the file.
    updated_lines = updated.splitlines()
    link_start = len(updated_lines)
    for index, line in enumerate(updated_lines):
        if LINK_RE.match(line):
            link_start = min(link_start, index)
    head = updated_lines[:link_start]
    links = [line for line in updated_lines[link_start:] if LINK_RE.match(line)]

    named: dict[str, str] = {}
    for line in links:
        match = LINK_RE.match(line)
        if match:
            named[match.group("label")] = match.group("url")

    named["Unreleased"] = f"{repo_url}/compare/v{version}...HEAD"
    named[version] = f"{repo_url}/releases/tag/v{version}"

    ordered: list[str] = []
    for label in ["Unreleased"] + sorted(
        (key for key in named if key != "Unreleased"),
        key=lambda value: semver(value) if SEMVER_RE.match(value) else (0, 0, 0),
        reverse=True,
    ):
        ordered.append(f"[{label}]: {named[label]}")

    new_text = "\n".join(head).rstrip() + "\n\n" + "\n".join(ordered) + "\n"
    path.write_text(new_text, encoding="utf-8")

    if not keep:
        for fragment in fragments:
            fragment.path.unlink()
    print(f"released {version} ({date}) with {len(fragments)} fragment(s)")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=".", help="repository root (default: .)")
    parser.add_argument("--repo-url", default=DEFAULT_REPO_URL,
                        help="base URL used for the link references")
    parser.add_argument("--check", action="store_true",
                        help="validate CHANGELOG.md and all fragments")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the merged Unreleased section, change nothing")
    parser.add_argument("--release", metavar="VERSION",
                        help="fold the fragments into a dated VERSION section")
    parser.add_argument("--date", default=None,
                        help="release date (YYYY-MM-DD, default: today)")
    parser.add_argument("--keep", action="store_true",
                        help="keep the fragment files after merging")
    args = parser.parse_args(argv)

    root = Path(args.root).resolve()
    try:
        if args.check:
            fragments = check(root)
            print(f"changelog OK ({len(fragments)} pending fragment(s))")
            for fragment in fragments:
                print(f"  {fragment.path.name}: {fragment.bullets[0]}")
            return 0
        if args.release:
            date = args.date or dt.date.today().isoformat()
            if not ISO_DATE_RE.match(date):
                raise ChangelogError(f"'{date}' is not a YYYY-MM-DD date")
            return release(root, args.release, date, args.keep, args.repo_url)
        return merge(root, args.keep, args.dry_run)
    except ChangelogError as error:
        for line in str(error).splitlines():
            print(f"error: {line}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
