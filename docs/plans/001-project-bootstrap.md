# Project Bootstrap Plan

Status: **Complete**

This plan describes the first implementation change for Cardputer Hub. It was
reviewed, approved, and implemented on `feat/001-project-bootstrap`.

## 1. Objective

Create a reproducible development foundation and a minimal compilable
Cardputer-Adv firmware skeleton.

The bootstrap should provide:

* a pinned PlatformIO firmware environment;
* the initial architecture-aligned source tree;
* native host-side tests;
* formatting and static analysis;
* simple local setup, build, test, format, lint, check, upload, and monitor commands;
* pull-request and main-branch CI with Cardputer-Adv compilation;
* embedded build and version metadata;
* protected release, two-Release retention, and tag-rebuild workflows;
* complete local development instructions in README.md.

## 2. Scope

### In scope

* PlatformIO configuration for the M5Stack Cardputer-Adv.
* Arduino as the initial firmware framework.
* A minimal composition root and Cardputer hardware bootstrap adapter.
* A small hardware-independent build-information component.
* Host-native unit-test infrastructure and an initial meaningful test.
* Compiler warnings, Cppcheck, and C/C++ formatting.
* Reproducible Python tooling through uv.
* Local command wrappers shared with CI.
* GitHub Actions validation and firmware compilation.
* Release infrastructure required by docs/ENGINEERING.md.
* Developer-facing setup, build, test, flash, and monitor documentation.

### Non-goals

Do not implement or stub domain behavior for:

* Wi-Fi;
* Bluetooth or BLE HID;
* Services;
* Mini Apps;
* Launcher, navigation, or application UI;
* host profiles or host switching;
* weather, VPS, Telegram, media, indicators, or remote control;
* product configuration or persistence.

Empty architectural layer directories may be reserved, but they must not
contain speculative interfaces or placeholder product implementations.

## 3. Architecture Boundaries

The bootstrap must preserve:

    Mini Apps
        ↓
    Services
        ↓
    Connectivity
        ↓
    Hardware Adapters

The initial firmware should use only:

    main.cpp
        ↓
    minimal System Core bootstrap
        ↓
    Cardputer hardware adapter
        ↓
    M5Cardputer / M5Unified

main.cpp is the composition root. Direct M5Stack or Arduino calls should be
confined to the Cardputer hardware adapter. Pure code used by native tests must
not include Arduino, ESP32, or M5Stack headers.

## 4. Toolchain and Pinning Decisions

The implementation should begin from M5Stack's official Cardputer-Adv
PlatformIO configuration:

    platform = espressif32@6.7.0
    board = esp32-s3-devkitc-1
    framework = arduino
    upload_speed = 1500000
    build_flags =
      -DESP32S3
      -DCORE_DEBUG_LEVEL=5
      -DARDUINO_USB_CDC_ON_BOOT=1
      -DARDUINO_USB_MODE=1

Retain the vendor-tested espressif32@6.7.0 baseline initially. A platform
upgrade should be a separate change after the baseline build is green.

Before implementation is complete, resolve and record exact compatible versions
for every item below. No placeholder, floating branch, or unbounded version may
remain:

| Component | Pinning mechanism |
| --- | --- |
| Python | Exact patch version in .python-version |
| PlatformIO Core | Exact dependency in pyproject.toml and uv.lock |
| clang-format | Exact dependency in pyproject.toml and uv.lock |
| ESP32 PlatformIO platform | Exact version in platformio.ini |
| PlatformIO native platform | Exact version in platformio.ini |
| M5Cardputer | Release version or full Git commit SHA in lib_deps |
| M5Unified and important transitive libraries | Exact version or full Git commit SHA |
| Cppcheck tool package | Exact tool-cppcheck version in platform_packages |
| GitHub Actions | Full commit SHA with the upstream version in a comment |
| CI container images, if used | Immutable image digest |

Commit uv.lock. CI must use uv sync --frozen or an equivalent locked operation
and fail rather than alter dependency resolution.

The implementation handoff should record why each version was selected and the
command used to display the resolved PlatformIO dependency graph.

## 5. Proposed Repository Layout

    .
    ├── .clang-format
    ├── .github/
    │   ├── dependabot.yml
    │   └── workflows/
    │       ├── ci.yml
    │       ├── release.yml
    │       └── rebuild-tag.yml
    ├── .gitignore
    ├── .python-version
    ├── Makefile
    ├── platformio.ini
    ├── pyproject.toml
    ├── uv.lock
    ├── scripts/
    │   └── build_metadata.py
    ├── src/
    │   ├── apps/
    │   │   └── .gitkeep
    │   ├── connectivity/
    │   │   └── .gitkeep
    │   ├── core/
    │   │   └── lifecycle/
    │   │       ├── build_info.cpp
    │   │       └── build_info.h
    │   ├── hardware/
    │   │   └── cardputer/
    │   │       ├── cardputer_platform.cpp
    │   │       └── cardputer_platform.h
    │   ├── services/
    │   │   └── .gitkeep
    │   └── main.cpp
    └── test/
        └── test_build_info/
            └── test_main.cpp

The empty apps, connectivity, and services directories reserve the documented
top-level boundaries only. They should contain no implementation.

## 6. PlatformIO Environments

### cardputer-adv

The production environment should:

* use the official generic ESP32-S3 board configuration and required USB flags;
* use the Arduino framework;
* compile the minimal Cardputer-Adv firmware;
* pin M5Cardputer and important dependencies;
* enable strict warnings for project-owned code;
* define upload and serial-monitor settings;
* use a documented monitor speed, initially 115200;
* embed semantic version, commit SHA, and build type.

The firmware skeleton should:

1. initialize the Cardputer through the hardware adapter;
2. initialize serial logging;
3. print firmware name, version, commit, and build type once;
4. perform a minimal non-blocking update loop;
5. render no product UI and start no connectivity.

### native

The native environment should:

* pin the PlatformIO native platform;
* use PlatformIO's supported Unity test runner;
* compile only hardware-independent project sources;
* exclude main.cpp and all Cardputer or Arduino adapter sources;
* enable strict host-compiler warnings;
* run without connected hardware.

README.md should document that native testing requires Xcode Command Line Tools
on macOS and build-essential on Ubuntu or Linux.

## 7. Test-Driven Bootstrap Sequence

Infrastructure that cannot be tested before it exists is an allowed TDD
exception, but the first behavior-bearing component should demonstrate the
red-green-refactor workflow.

### Red

1. Configure the native test runner.
2. Add a test defining the observable build-information contract:
   * firmware name is non-empty and stable;
   * version is non-empty;
   * commit is non-empty;
   * build type is non-empty.
3. Run it and record the expected missing-implementation or undefined-value
   failure.

### Green

1. Implement the smallest hardware-independent build_info component.
2. Supply its values through PlatformIO build definitions.
3. Run the focused native test and then the complete native suite.

### Refactor

1. Remove duplication between native and firmware build definitions.
2. Keep metadata generation in the build script instead of application code.
3. Keep all native tests green.

Do not add fake product components or tests in this bootstrap.

## 8. Formatting and Static Analysis

### Formatting

Add one canonical C/C++ formatter configuration and expose:

* make format — modify project-owned C/C++ files;
* make format-check — verify formatting without modifying files.

File selection must be explicit and limited to project source and tests.
Generated files and .pio contents must be excluded.

### Static analysis

Use:

1. strict compiler warnings for native and firmware builds;
2. PlatformIO pio check with a pinned Cppcheck package.

Expose both through make lint. Analyze project-owned code and avoid failing on
warnings inside pinned third-party libraries. Any suppression must be narrow,
documented, and justified.

## 9. Local Commands

Use a small Makefile as the stable developer entry point. Commands should call
tools through the locked uv environment.

| Command | Behavior |
| --- | --- |
| make setup | Synchronize locked development tools |
| make build | Compile production Cardputer-Adv firmware |
| make test | Run native host tests |
| make format | Format project-owned C/C++ files |
| make format-check | Verify formatting without mutation |
| make lint | Run compiler and static-analysis checks |
| make check | Run lock, format, lint, native-test, and firmware-build checks |
| make upload | Build and upload to a connected Cardputer-Adv |
| make monitor | Open the configured serial monitor |
| make clean | Remove PlatformIO output through PlatformIO |

make check must cover the same validation categories as pull-request CI and
must not modify tracked files.

## 10. GitHub Actions

### Pull-request and main validation

Create .github/workflows/ci.yml for:

* pull requests targeting main;
* pushes to main;
* manual diagnostic dispatches.

The workflow should:

1. check out source using an Action pinned to a full commit SHA;
2. install the exact Python version;
3. install uv using an Action pinned to a full commit SHA;
4. run uv sync --frozen;
5. restore safe PlatformIO and uv caches;
6. run make check;
7. optionally upload compiled firmware as a short-lived PR artifact.

Required properties:

* default contents: read permissions;
* no tag or Release writes in PR CI;
* cancellation of superseded runs;
* no secrets exposed to untrusted pull requests;
* full Action SHAs with readable version comments;
* cache keys derived from lock and configuration files;
* failure if formatting, analysis, tests, or firmware compilation fails.

### Release workflow

Create a separate protected workflow that:

* operates only on a validated main commit;
* assigns and embeds a semantic version before the production build;
* builds, tests, packages, and checksums firmware;
* creates the permanent Git tag and GitHub Release;
* verifies uploaded assets;
* deletes GitHub Release records and assets older than the newest two;
* never deletes official Git tags.

### Historical tag rebuild

Create a manually dispatched, read-only workflow that:

* accepts only an existing vMAJOR.MINOR.PATCH tag;
* checks out that exact tag;
* installs pinned tools and dependencies;
* runs full validation;
* builds versioned firmware and checksums;
* uploads a temporary artifact with bounded retention;
* does not create or delete tags or Releases.

## 11. README Update

Replace bootstrap placeholders with verified instructions for:

* prerequisites on macOS and Ubuntu or Linux;
* exact Python, uv, PlatformIO, compiler, and formatter versions;
* setup and all local commands;
* locating the generated firmware image;
* entering Cardputer-Adv download mode;
* flashing over a USB-C data cable;
* serial monitoring and baud rate;
* CI behavior;
* two-Release retention;
* rebuilding historical firmware from a Git tag.

README commands must be copied from and verified against actual repository
entry points.

## 12. Implementation Order

1. Resolve and document exact tool and dependency pins.
2. Add ignore rules, locked Python tooling, formatter configuration, and
   PlatformIO environments.
3. Establish native testing and execute the TDD sequence for build information.
4. Add the minimal Cardputer adapter and firmware composition root.
5. Add formatting, static analysis, and stable local commands.
6. Add pull-request and main CI using the same local commands.
7. Add protected release, retention, and historical rebuild workflows.
8. Update README.md with verified commands.
9. Run the complete verification matrix from a clean dependency state.

## 13. Verification Matrix

Run and record:

    uv lock --check
    make format-check
    make lint
    make test
    make build
    make check

Also verify:

* the native test fails first for the expected reason and then passes;
* production firmware compiles and produces the documented binary;
* make check leaves the worktree unchanged;
* no source includes excluded product behavior;
* dependency resolution has no unintended floating references;
* every non-local Action uses a full commit SHA;
* CI is read-only except protected release jobs;
* release cleanup preserves all Git tags;
* tag rebuild jobs cannot mutate Releases or tags;
* README commands match the implementation.

Upload and serial monitoring should be attempted when hardware is available.
Lack of hardware does not invalidate host tests or compilation, but the
commands and configuration must still be reviewed.

## 14. Acceptance Criteria

The bootstrap is complete only when:

* a clean checkout can install the documented locked tools;
* native tests pass without physical hardware;
* formatting and static analysis pass;
* minimal production firmware compiles for Cardputer-Adv;
* make check passes and matches CI's major validation categories;
* PR CI includes firmware compilation and main repeats full validation;
* release and tag-rebuild workflows satisfy retention and permission rules;
* important tools, platforms, libraries, Actions, and images are pinned where
  practical;
* firmware exposes version, commit, and build type;
* README setup, build, test, flash, and monitor instructions are verified;
* no excluded product feature is implemented;
* all applicable repository checks pass.

## 15. Risks and Review Points

Reviewers should explicitly confirm:

1. whether to retain M5Stack's official espressif32@6.7.0 baseline or validate a
   newer pinned platform in this bootstrap;
2. whether serial-only firmware behavior is sufficient;
3. whether .gitkeep files are acceptable for empty architecture layers;
4. whether short-lived PR firmware artifacts should be enabled initially;
5. whether release automation belongs in this bootstrap, as required by
   docs/ENGINEERING.md, or should receive a separate infrastructure plan before
   product work begins.

Implementation began after these review points were resolved as recorded below.

## 16. Completion Record

Completed on 2026-08-31. The review points were resolved by retaining the
official `espressif32@6.7.0` baseline, using serial-only skeleton behavior,
reserving empty layers with `.gitkeep`, enabling seven-day CI artifacts, and
including the documented release infrastructure in this bootstrap.

The final important pins are:

| Component | Selected version |
| --- | --- |
| Python | 3.12.14 |
| uv | 0.12.7 |
| PlatformIO Core | 6.1.19 |
| clang-format | 23.1.0 |
| ESP32 platform | 6.7.0 |
| Native platform | 1.2.1 |
| Cppcheck tool package | 1.21100.230717 (Cppcheck 2.11.0) |
| Unity | 2.6.1 |
| M5Cardputer | upstream 1.2.0 commit `2d4fa6646e4e5b47e0af96214b003aa7b15b8d81` |
| M5Unified | 0.2.21 |
| M5GFX | 0.2.28 |
| IRremote | 4.7.1 |
| actions/checkout | v7.0.1 commit `3d3c42e5aac5ba805825da76410c181273ba90b1` |
| astral-sh/setup-uv | v10.0.1 commit `20cfd1bf945f4377ade1205e4dbc17946fc9a30d` |
| actions/cache | v6.1.0 commit `55cc8345863c7cc4c66a329aec7e433d2d1c52a9` |
| actions/upload-artifact | v7.0.1 commit `043fb46d1a93c77aae656e7c1c64a875d1fc6a0a` |

The vendor-tested ESP32 platform was retained as required by the approved
plan. M5Cardputer uses its release commit because the package manifest reports
an older version; its compatible important dependencies were then pinned to
the exact versions resolved by the successful firmware build. Cppcheck uses
the newest 2.11.0 package build published for both macOS and Linux (the newer
package identifier is macOS ARM-only). Python tooling is locked in `uv.lock`,
and GitHub Actions use immutable commits with readable upstream release
comments.

The resolved PlatformIO graph was recorded with:

    uv run --frozen pio pkg list -e cardputer-adv
    uv run --frozen pio pkg list -e native

TDD evidence: the focused native test first failed because
`core/lifecycle/build_info.h` did not exist. The minimal build-information
implementation was then added, after which the focused test and full native
suite passed. No integration-test target was added because this skeleton
introduces no component integration or product behavior.

Final verification passed:

    uv lock --check
    make format-check
    make lint
    make test
    make build
    make check

The firmware image was produced at
`.pio/build/cardputer-adv/firmware.bin`. Upload and serial monitoring were not
run because physical Cardputer hardware was not available; their PlatformIO
configuration and local command wrappers were reviewed.
