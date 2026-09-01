# 004 — Automatic Semantic Releases

## Goal

Publish firmware automatically after CI validates an eligible pull request
merge on `main`, while retaining only the two newest GitHub Releases.

## Release Policy

The protected release workflow resolves the pull request associated with the
validated `main` commit and derives the semantic-version bump from the source
branch prefix:

| Prefix | Bump |
| --- | --- |
| `major/`, `breaking/` | major |
| `feat/`, `minor/` | minor |
| `fix/`, `perf/`, `patch/` | patch |
| all other prefixes | no release |

The first eligible release is `v0.1.0`. Later versions are calculated from the
highest permanent semantic-version tag. The workflow must build and publish the
exact commit validated by the successful push-triggered CI run.

## Retention

After publishing and verifying a release, delete every published GitHub Release
record and its assets except the newest two. Preserve every official Git tag so
historical firmware remains reproducible through the existing tag-rebuild
workflow.

## Verification

Behavioral tests cover initial, major, minor, patch, and no-release branch
classification; the validated-commit workflow wiring; and two-Release
retention. Run the complete repository check, including firmware compilation.
