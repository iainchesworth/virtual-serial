# Changelog fragments

This directory holds one file per user-visible change. `CHANGELOG.md` is
assembled from these fragments at release time, following
[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/).

The point is merge-conflict avoidance: every pull request adds its own file, so
two branches never edit the same lines of `CHANGELOG.md`. The release manager
folds the pending files into a dated version section with one command.

## Adding a fragment

Create `changelog.d/<slug>.<category>.md`:

| Part | Values |
|---|---|
| `<slug>` | lowercase letters, digits and hyphens, e.g. `async-timeouts` or `1217` |
| `<category>` | `added`, `changed`, `deprecated`, `removed`, `fixed`, `security` |

Examples:

```
changelog.d/async-contract.added.md
changelog.d/1217.fixed.md
```

The content is Markdown. Write one entry, or several `-` bullets:

```markdown
Added `read_timeout` instrumentation that reports the elapsed wait when a read
times out.
```

```markdown
- `mock_port` can now inject an overrun independently of the FIFO capacity.
- `rfc2217_port` reports a parity error from `NOTIFY-LINESTATE`.
```

A bullet may continue over several lines; a blank line starts a new entry.
Fragments are plain text, so no front matter and no tooling beyond Python's
standard library is required.

Files whose name starts with `_` or `.`, and this `README.md`, are ignored by
the tooling, which is why `_template.added.md` can live here.

## Commands

```bash
python tools/changelog.py --check            # validate (run in CI)
python tools/changelog.py --dry-run          # preview the Unreleased section
python tools/changelog.py                    # merge into [Unreleased], delete files
python tools/changelog.py --release 1.1.0    # cut a dated release
```

`--release` requires the new version to be greater than every version already
in the changelog, writes the version heading and date, moves the entries under
it, empties `[Unreleased]`, and refreshes the link references at the bottom of
`CHANGELOG.md`. The repository URL defaults to
`https://github.com/boostorg/virtual_serial` and can be overridden with
`--repo-url`.

The same rules are enforced by the `Changelog fragments` CI job, which also
runs the tool's own unit tests (`tools/test_changelog.py`).
