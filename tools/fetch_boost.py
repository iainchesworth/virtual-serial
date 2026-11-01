#!/usr/bin/env python3
"""Fetch and unpack a Boost release's merged header tree into .cache/boost-root.

This keeps the monorepo self-contained for offline/CI builds without requiring a
system-wide Boost installation.  Only the ``boost/`` header tree is extracted.

Usage:
    python tools/fetch_boost.py [--version 1.92.0] [--dest .cache/boost-root]
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import sys
import tarfile
import time
import urllib.error
import urllib.request

DEFAULT_VERSION = "1.92.0"
UPSTREAM = "https://archives.boost.io/release/{version}/source/boost_{underscored}.tar.bz2"


def human(n: int) -> str:
    for unit in ("B", "KiB", "MiB", "GiB"):
        if n < 1024:
            return f"{n:.1f} {unit}"
        n /= 1024.0
    return f"{n:.1f} TiB"


def download(url: str, target: pathlib.Path, attempts: int = 6) -> None:
    """Download `url` to `target`, resuming partial transfers.

    Long downloads over flaky links are the norm in CI, so the transfer is
    resumed with a Range request when a partial archive is already present,
    and retried a few times before giving up.
    """
    target.parent.mkdir(parents=True, exist_ok=True)
    expected = 0
    started = time.monotonic()

    for attempt in range(1, attempts + 1):
        existing = target.stat().st_size if target.exists() else 0
        headers = {"Range": f"bytes={existing}-"} if existing else {}
        request = urllib.request.Request(url, headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                resumed = response.status == 206 and existing > 0
                mode = "ab" if resumed else "wb"
                total = int(response.headers.get("Content-Length") or 0)
                if resumed:
                    total += existing
                else:
                    existing = 0
                expected = total
                if attempt == 1:
                    print(f"downloading {url} ({human(total)})", flush=True)
                elif resumed:
                    print(
                        f"resuming at {human(existing)} ({attempt}/{attempts})",
                        flush=True,
                    )
                with target.open(mode) as handle:
                    read = existing
                    while True:
                        chunk = response.read(1 << 20)
                        if not chunk:
                            break
                        handle.write(chunk)
                        read += len(chunk)
                        if total:
                            pct = 100.0 * read / total
                            sys.stdout.write(
                                f"\r  {human(read)} / {human(total)}  ({pct:5.1f}%)"
                            )
                            sys.stdout.flush()
            if expected == 0 or target.stat().st_size >= expected:
                print(
                    f"\n  done in {time.monotonic() - started:.1f}s", flush=True
                )
                return
        except (urllib.error.URLError, OSError, TimeoutError) as error:
            print(f"\n  transfer interrupted: {error}", flush=True)

        if attempt < attempts:
            time.sleep(min(2 * attempt, 10))

    raise RuntimeError(
        f"could not download {url}: {target.stat().st_size if target.exists() else 0}"
        f" of {expected or 'unknown'} bytes"
    )


def extract(archive: pathlib.Path, prefix: str, dest: pathlib.Path) -> None:
    print(f"extracting {prefix} -> {dest}", flush=True)
    dest.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    count = 0
    with tarfile.open(archive, "r:bz2") as tar:
        members = [m for m in tar if m.name.startswith(prefix)]
        print(f"  {len(members)} members", flush=True)
        for member in members:
            relative = member.name[len(prefix):]
            if not relative:
                continue
            member.name = relative
            tar.extract(member, path=dest, set_attrs=False)
            count += 1
            if count % 2000 == 0:
                sys.stdout.write(f"\r  {count} files")
                sys.stdout.flush()
    print(f"\r  {count} files in {time.monotonic() - started:.1f}s", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default=DEFAULT_VERSION)
    parser.add_argument("--dest", default=".cache/boost-root")
    parser.add_argument("--keep-archive", action="store_true")
    args = parser.parse_args()

    version = args.version
    underscored = version.replace(".", "_")
    url = UPSTREAM.format(version=version, underscored=underscored)
    root = pathlib.Path(args.dest).resolve()
    header = root / "boost" / "asio.hpp"
    if header.exists():
        print(f"already present: {header}")
        return 0

    archive = pathlib.Path(".cache") / f"boost_{underscored}.tar.bz2"
    download(url, archive)
    extract(archive, f"boost_{underscored}/boost/", root / "boost")
    if not args.keep_archive:
        archive.unlink()
    if not header.exists():
        print(f"error: {header} missing after extraction", file=sys.stderr)
        return 1
    print(f"Boost headers ready at {root}  (set -DBOOST_ROOT='{root}')")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
