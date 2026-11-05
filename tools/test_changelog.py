#!/usr/bin/env python3
"""Unit tests for tools/changelog.py.

Run with:

    python -m unittest discover -s tools -p "test_*.py" -v

The tests drive the command line against temporary repositories so the merge,
release and validation behaviour is exercised exactly as CI runs it.
"""

from __future__ import annotations

import shutil
import subprocess
import sys
import unittest
import uuid
from pathlib import Path

TOOL = Path(__file__).resolve().with_name("changelog.py")
# The temporary repositories live inside the checkout rather than in the system
# temporary directory: some sandboxes deny writes below freshly created temp
# directories, and .cache/ is already git-ignored.
TEMP_ROOT = TOOL.parent.parent / ".cache" / "changelog-tests"

CHANGELOG = """# Changelog

## [Unreleased]

## [1.0.0] - 2026-01-01

### Added

- The initial release.

[Unreleased]: https://example.test/compare/v1.0.0...HEAD
[1.0.0]: https://example.test/releases/tag/v1.0.0
"""


class ChangelogToolTests(unittest.TestCase):
    def setUp(self) -> None:
        TEMP_ROOT.mkdir(parents=True, exist_ok=True)
        self.root = TEMP_ROOT / uuid.uuid4().hex
        self.root.mkdir()
        self.fragments = self.root / "changelog.d"
        self.fragments.mkdir()
        (self.root / "CHANGELOG.md").write_text(CHANGELOG, encoding="utf-8")

    def tearDown(self) -> None:
        shutil.rmtree(self.root, ignore_errors=True)

    # -- helpers ---------------------------------------------------------

    def run_tool(self, *arguments: str) -> subprocess.CompletedProcess:
        return subprocess.run(
            [sys.executable, str(TOOL), "--root", str(self.root), *arguments],
            capture_output=True,
            text=True,
        )

    def write_fragment(self, name: str, text: str = "Something happened.") -> Path:
        path = self.fragments / name
        path.write_text(text, encoding="utf-8")
        return path

    def changelog(self) -> str:
        return (self.root / "CHANGELOG.md").read_text(encoding="utf-8")

    # -- validation ------------------------------------------------------

    def test_check_accepts_valid_fragments(self) -> None:
        self.write_fragment("a-change.added.md")
        self.write_fragment("1217.fixed.md", "- One line.\n- Another line.\n")
        result = self.run_tool("--check")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("2 pending fragment(s)", result.stdout)

    def test_check_rejects_unknown_category(self) -> None:
        self.write_fragment("thing.improved.md")
        result = self.run_tool("--check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("expected <slug>.<category>.md", result.stderr)

    def test_check_rejects_bad_slug(self) -> None:
        self.write_fragment("Not_A_Slug.added.md")
        result = self.run_tool("--check")
        self.assertNotEqual(result.returncode, 0)

    def test_check_rejects_empty_fragment(self) -> None:
        self.write_fragment("empty.added.md", "\n\n")
        result = self.run_tool("--check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("empty", result.stderr)

    def test_check_requires_unreleased_section(self) -> None:
        (self.root / "CHANGELOG.md").write_text("# Changelog\n", encoding="utf-8")
        result = self.run_tool("--check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Unreleased", result.stderr)

    def test_check_rejects_duplicate_versions(self) -> None:
        text = CHANGELOG.replace(
            "## [1.0.0] - 2026-01-01",
            "## [1.0.0] - 2026-01-02\n\n## [1.0.0] - 2026-01-01",
        )
        (self.root / "CHANGELOG.md").write_text(text, encoding="utf-8")
        result = self.run_tool("--check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("duplicate", result.stderr)

    def test_check_ignores_readme_and_templates(self) -> None:
        self.write_fragment("README.md", "documentation")
        self.write_fragment("_template.added.md", "template")
        self.write_fragment(".hidden.added.md", "hidden")
        result = self.run_tool("--check")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("0 pending fragment(s)", result.stdout)

    # -- merge -----------------------------------------------------------

    def test_dry_run_changes_nothing(self) -> None:
        fragment = self.write_fragment("a-change.added.md", "A new thing.")
        result = self.run_tool("--dry-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("### Added", result.stdout)
        self.assertIn("- A new thing.", result.stdout)
        self.assertEqual(self.changelog(), CHANGELOG)
        self.assertTrue(fragment.exists())

    def test_merge_moves_entries_and_deletes_fragments(self) -> None:
        fragment = self.write_fragment("a-change.added.md", "A new thing.")
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(fragment.exists())

        text = self.changelog()
        unreleased = text.split("## [Unreleased]", 1)[1].split("## [1.0.0]", 1)[0]
        self.assertIn("### Added", unreleased)
        self.assertIn("- A new thing.", unreleased)
        self.assertNotIn("A new thing.", text.split("## [1.0.0]", 1)[1])

    def test_merge_orders_categories_and_preserves_existing(self) -> None:
        self.write_fragment("a.added.md", "Added one.")
        self.write_fragment("b.fixed.md", "Fixed one.")
        text = self.changelog().replace(
            "## [Unreleased]",
            "## [Unreleased]\n\n### Fixed\n\n- Existing fix.",
        )
        (self.root / "CHANGELOG.md").write_text(text, encoding="utf-8")

        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        unreleased = self.changelog().split("## [Unreleased]", 1)[1].split("## [1.0.0]", 1)[0]
        self.assertLess(unreleased.index("### Added"), unreleased.index("### Fixed"))
        self.assertIn("- Added one.", unreleased)
        self.assertIn("- Existing fix.", unreleased)
        self.assertIn("- Fixed one.", unreleased)

    def test_merge_deduplicates_identical_bullets(self) -> None:
        self.write_fragment("a.added.md", "Same text.")
        self.write_fragment("b.added.md", "Same text.")
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.changelog().count("- Same text."), 1)

    def test_merge_keeps_fragments_with_keep(self) -> None:
        fragment = self.write_fragment("a.added.md", "Kept entry.")
        result = self.run_tool("--keep")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(fragment.exists())
        self.assertIn("- Kept entry.", self.changelog())

    def test_merge_without_fragments_is_a_no_op(self) -> None:
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("no changelog fragments", result.stdout)
        self.assertEqual(self.changelog(), CHANGELOG)

    # -- release ---------------------------------------------------------

    def test_release_moves_entries_and_updates_links(self) -> None:
        fragment = self.write_fragment("a.added.md", "Released thing.")
        result = self.run_tool(
            "--release", "1.1.0", "--date", "2026-02-03",
            "--repo-url", "https://example.test",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(fragment.exists())

        text = self.changelog()
        self.assertIn("## [1.1.0] - 2026-02-03", text)
        self.assertIn("- Released thing.", text)
        self.assertIn("[Unreleased]: https://example.test/compare/v1.1.0...HEAD", text)
        self.assertIn("[1.1.0]: https://example.test/releases/tag/v1.1.0", text)
        self.assertIn("[1.0.0]: https://example.test/releases/tag/v1.0.0", text)

        sections = [line for line in text.splitlines() if line.startswith("## [")]
        self.assertEqual(sections[0], "## [Unreleased]")
        self.assertEqual(sections[1], "## [1.1.0] - 2026-02-03")
        self.assertEqual(sections[2], "## [1.0.0] - 2026-01-01")

    def test_release_rejects_version_that_is_not_newer(self) -> None:
        self.write_fragment("a.added.md", "Entry.")
        result = self.run_tool("--release", "1.0.0")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("greater than", result.stderr)

    def test_release_rejects_invalid_semver(self) -> None:
        self.write_fragment("a.added.md", "Entry.")
        result = self.run_tool("--release", "1.1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("semantic version", result.stderr)

    def test_semver_orders_prereleases(self) -> None:
        import changelog as module

        self.assertLess(
            module.semver("0.1.0-beta.1"), module.semver("0.1.0-beta.2"))
        self.assertLess(
            module.semver("0.1.0-beta.2"), module.semver("0.1.0-beta.10"))
        self.assertLess(module.semver("0.1.0-beta.10"), module.semver("0.1.0"))

    def test_release_accepts_prerelease_and_prints_notes(self) -> None:
        changelog = """# Changelog

## [Unreleased]

[Unreleased]: https://example.test/compare/v0.0.0...HEAD
"""
        (self.root / "CHANGELOG.md").write_text(changelog, encoding="utf-8")
        fragment = self.write_fragment("beta.added.md", "Beta feature.")

        result = self.run_tool(
            "--release", "0.1.0-beta.1", "--date", "2026-11-02",
            "--repo-url", "https://example.test",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(fragment.exists())

        text = self.changelog()
        self.assertIn("## [0.1.0-beta.1] - 2026-11-02", text)
        self.assertIn("- Beta feature.", text)
        self.assertIn(
            "[Unreleased]: https://example.test/compare/v0.1.0-beta.1...HEAD",
            text,
        )
        self.assertIn(
            "[0.1.0-beta.1]: https://example.test/releases/tag/v0.1.0-beta.1",
            text,
        )

        notes = self.run_tool("--notes", "0.1.0-beta.1")
        self.assertEqual(notes.returncode, 0, notes.stderr)
        self.assertIn("Beta feature.", notes.stdout)
        self.assertNotIn("## [0.1.0-beta.1]", notes.stdout)

    def test_release_rejects_invalid_date(self) -> None:
        self.write_fragment("a.added.md", "Entry.")
        result = self.run_tool("--release", "1.1.0", "--date", "03/02/2026")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("YYYY-MM-DD", result.stderr)


if __name__ == "__main__":
    unittest.main()
