> Target branch: `develop`. The `main` branch is release-only.

## Summary

<!-- What changed, and why? Link the issue if there is one. -->

## Checklist

- [ ] Tests cover the change, or the pull request explains why they cannot.
- [ ] A changelog fragment was added under `changelog.d/` as
      `<slug>.<category>.md` (`added`, `changed`, `deprecated`, `removed`,
      `fixed`, `security`), unless the change is not user visible.
- [ ] Documentation was updated for user-visible behaviour.
- [ ] `python tools/changelog.py --check` passes locally.
- [ ] `python tools/check_version.py` passes locally.
- [ ] The commits are authored by the maintainer and contain no attribution
      trailers, generated-by statements or tool attribution (see
      `CONTRIBUTING.md`).

## Notes for reviewers

<!-- Anything subtle: trade-offs, deliberate omissions, follow-up work. -->
