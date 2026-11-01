`tools/fetch_boost.py` now resumes interrupted downloads with HTTP range
requests and retries, so a flaky connection no longer discards a partial Boost
archive.
