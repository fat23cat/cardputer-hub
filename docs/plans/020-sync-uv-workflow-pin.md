# Workflow uv Version Consistency Plan

Status: **Implemented**

## 1. Objective

Prevent CI and release jobs from installing a different `uv` version than the
exact version required by `pyproject.toml`.

## 2. Problem

The merge of pull request 23 updated `[tool.uv].required-version` and the CI
workflow to `0.12.12`, but the protected release workflow still installed
`0.12.7`. The resulting release job failed during `uv sync --frozen`, before
release classification or firmware compilation.

## 3. Implementation

* Use `version-file: pyproject.toml` in every `setup-uv` step.
* Keep `[tool.uv].required-version` exact so builds remain reproducible.
* Add a workflow regression test that covers CI, release, and historical
  rebuild configuration.
* Document `pyproject.toml` as the single source of truth for the workflow `uv`
  version.

## 4. Verification

The focused regression test must fail against the inconsistent workflow state
and pass after the workflow changes. Repository host checks and production
firmware compilation remain the acceptance checks for the tooling change.

Completed verification:

* the focused regression test failed on the old CI and release configuration,
  then passed after both workflows used `pyproject.toml`;
* `make host-check` passed with 46 Python tests and 218 native C++ tests;
* `make firmware-check` passed with ESP-IDF 5.5.5 for ESP32-S3.
