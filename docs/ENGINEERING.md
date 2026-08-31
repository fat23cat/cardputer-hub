# Cardputer Hub Engineering Guidelines

## 1. Purpose

This document defines engineering requirements for Cardputer Hub.

The project should be designed for:

* safe refactoring;
* automated verification;
* reproducible firmware builds;
* predictable releases;
* GitHub-based development;
* effective development with Codex and other coding agents.

Testing, CI/CD, and release automation are considered part of the project architecture rather than optional infrastructure to add later.

---

## 2. Core Engineering Principles

### Testability by Design

Application logic should be designed so that most behavior can be tested without physical Cardputer hardware.

Avoid combining:

```text
physical input
Bluetooth calls
UI rendering
state updates
storage
```

inside a single implementation unit.

Prefer:

```text
Input
  ↓
Application / Service Logic
  ↓
Interfaces
  ├── Bluetooth
  ├── Display
  ├── Storage
  ├── Network
  └── LED
```

Hardware-specific implementations can then be replaced with fake or mock implementations during tests.

---

## 3. Hardware Separation

Core logic and most Services should be compilable and testable on a normal development machine.

Hardware-specific calls should live in thin adapters.

Examples:

```text
M5Cardputer keyboard API
        ↓
Keyboard Adapter
        ↓
InputEvent
        ↓
Application
```

```text
IndicatorService
        ↓
ILEDAdapter
        ↓
Puzzle WS2812 implementation
```

Hardware adapters should contain as little business logic as possible.

---

## 4. Testing Strategy

Automated tests should provide enough confidence to refactor the system safely.

Testing should focus on important behavior rather than maximizing a coverage number.

Important areas include:

```text
Host Profiles
host switching
navigation
Action routing
Service state transitions
configuration
persistence
capabilities
indicator priority
error handling
serialization
```

### Test-Driven Development

Behavioral implementation should follow the red-green-refactor cycle:

```text
RED
write the smallest test that describes the next observable behavior
run it and confirm that it fails for the expected reason
      ↓
GREEN
write the minimum implementation needed to make the test pass
run the relevant test suite
      ↓
REFACTOR
improve the design without changing behavior
keep all tests passing
```

Do not write behavioral production code before its failing test unless the work
is an explicitly identified exception, such as a disposable hardware
exploration or an infrastructure change that cannot be exercised by the current
test harness. Convert exploratory findings into automated tests before merging
production behavior.

Documentation-only changes, formatting-only changes, and mechanical refactors
that intentionally preserve behavior do not require a new failing test.

---

## 5. Unit Tests

Core domain behavior should have unit tests.

Examples:

### HostService

Test scenarios such as:

```text
create Host Profile
select active host
switch host
remember active host
handle unavailable host
remove host
restore configuration
```

### Navigation

Test:

```text
open Mini App
push View
Back
return to Launcher
global Home action
```

### Action Bus

Test:

```text
route Action to appropriate handler
preserve Action parameters
reject unsupported Action
handle different Action sources consistently
```

### ConfigurationService

Test:

```text
load configuration
save configuration
default configuration
invalid configuration
migration when configuration schema changes
```

### IndicatorService

Test:

```text
priority resolution
animation state
notification overrides idle state
critical state overrides notification
restore previous state
```

---

## 6. Behavior-Oriented Tests

Tests should verify observable behavior rather than implementation details.

Prefer:

```text
Given:
Work MacBook is active

When:
Personal MacBook is selected

Then:
HostService requests connection to Personal MacBook

And:
activeHost becomes Personal MacBook after successful connection
```

Avoid tests that fail only because internal functions or classes were renamed.

---

## 7. Integration Tests

Integration tests should verify collaboration between important components.

Example:

```text
Input
  ↓
Action Bus
  ↓
HostService
  ↓
FakeBluetoothService
```

Scenario:

```text
Given:
Personal MacBook is active

When:
SELECT_HOST(host-work) is dispatched

Then:
HostService requests connection to host-work

And:
the correct Bluetooth abstraction is called
```

Another example:

```text
WeatherApp
   ↓
WeatherService
   ↓
FakeNetworkClient
```

---

## 8. Regression Tests

Bug fixes should begin with a regression test that reproduces the defect
whenever practical.

Preferred workflow:

```text
bug discovered
      ↓
write failing test
      ↓
fix implementation
      ↓
test passes
```

Important regression areas include:

```text
host switching
navigation
configuration
persistence
Actions
Bluetooth state
indicator state
```

---

## 9. Test Coverage

Coverage should be measured for host-testable code.

Coverage percentage is not the primary quality target.

As a guideline, core application and Service logic should aim for approximately:

```text
80%+ line coverage
```

when practical.

Critical behavioral paths matter more than the raw percentage.

Hardware adapters may have lower automated coverage.

---

## 10. Static Analysis

CI should run static analysis appropriate for the chosen C++ toolchain.

Compiler warnings should be enabled at a reasonably strict level.

New warnings should not be ignored without justification.

Warnings introduced by a pull request should normally fail CI where practical.

---

## 11. Formatting

The repository should define one canonical formatter for each supported file
type and expose them through one repository formatting command.

Formatting must not depend on developer IDE configuration.

The repository should contain its formatter configuration.

CI should verify formatting.

---

## 12. Dependency Management

Every toolchain component and dependency should use an exact, reproducible
version wherever its ecosystem permits pinning.

Examples:

```text
ESP32 framework/core
M5Cardputer
M5Unified
BLE HID library
LED library
test framework
Python
uv
PlatformIO Core
GitHub Actions
CI container images
```

Use version-controlled manifests and lockfiles where available. Production
builds must not implicitly depend on floating tags, broad version ranges, or
whatever version happens to be latest.

Dependency upgrades should be explicit, reviewable changes. Automated update
tools such as Dependabot or Renovate may prepare these changes, but the normal
validation pipeline must pass before they are merged.

---

## 13. Build System

Use a declarative, reproducible firmware build.

PlatformIO is the preferred initial build system unless implementation proves another option significantly better.

The repository should define:

```text
target board
platform/framework version
dependencies
build flags
test environments
```

in version-controlled configuration.

A new developer should not need to manually install an undocumented collection of Arduino libraries.

---

## 14. Repository Workflow

The source repository is hosted on GitHub.

Primary branch:

```text
main
```

Normal development flow:

```text
feature branch
      ↓
Pull Request
      ↓
CI
      ↓
review
      ↓
merge
      ↓
main branch validation
```

Direct changes to protected `main` should be avoided.

Official releases use a separate protected release workflow for a validated
commit on `main`. A merge does not create a release unless that workflow is
explicitly triggered by the selected release policy.

---

## 15. Pull Request CI

Opening or updating a pull request targeting `main` must trigger CI.

Implementation, build, dependency, and toolchain changes should include at
least:

```text
checkout
      ↓
install pinned toolchain
      ↓
dependency cache
      ↓
format check
      ↓
static analysis
      ↓
unit tests
      ↓
integration tests
      ↓
Cardputer-Adv firmware build
```

A pull request should not be mergeable while required checks are failing.

---

## 16. Firmware Compilation on Applicable PRs

Every pull request that changes firmware, build configuration, dependencies, or
tooling that can affect the production image must compile the production
Cardputer-Adv firmware.

Documentation-only changes may skip firmware compilation when CI can classify
them reliably using reviewed path filters.

Passing host tests alone is insufficient.

CI must protect against:

```text
tests pass
but
firmware does not compile
```

---

## 17. PR Firmware Artifact

PR builds may optionally publish a temporary firmware artifact.

Example:

```text
cardputer-hub-pr-142.bin
```

This is useful for manual hardware validation before merge.

A PR artifact is not an official release.

---

## 18. Main Branch Validation

Every merge to `main` should execute the complete validation pipeline again.

Do not assume that successful PR CI guarantees the merge commit is valid.

Pipeline:

```text
main updated
      ↓
format/static checks
      ↓
tests
      ↓
production firmware build
```

This validation workflow must not create a Git tag or GitHub Release. Release
creation belongs to the protected release workflow.

---

## 19. Versioning

Firmware must have an explicit version.

Use semantic versioning:

```text
MAJOR.MINOR.PATCH
```

Examples:

```text
0.1.0
0.1.1
0.2.0
1.0.0
```

Development before the first stable release may remain in:

```text
0.x.y
```

---

## 20. Automatic Versioning

Production versions should be generated by CI/CD rather than manually editing version constants in multiple locations.

The final mechanism may use:

```text
PR labels
Conventional Commits
release metadata
another deterministic release strategy
```

The exact mechanism can be selected during implementation.

Requirement:

> A normal release must not require manually synchronizing the same version number across several files.

---

## 21. Version Inside Firmware

The firmware should expose build information.

At minimum:

```text
semantic version
Git commit SHA
build type
```

A future About screen may display:

```text
CARDPUTER HUB

Version
0.4.12

Commit
a84cf21
```

This should make deployed firmware traceable back to source.

---

## 22. Firmware Filenames

Official release filenames should include the version.

Example:

```text
cardputer-hub-v0.4.12.bin
```

If multiple images become necessary:

```text
cardputer-hub-v0.4.12.bin
cardputer-hub-v0.4.12-factory.bin
cardputer-hub-v0.4.12-ota.bin
```

Official release assets should not rely on an ambiguous filename such as:

```text
firmware.bin
```

---

## 23. Git Tags

Every official release must have a Git tag.

Example:

```text
v0.4.12
```

The tag must refer to the exact source revision used to build the release firmware.

Release tags are permanent rebuild references and must not be deleted by release
retention automation.

---

## 24. GitHub Releases

Successful release builds should automatically create a GitHub Release.

Example:

```text
v0.4.12
```

The Release should contain downloadable firmware assets.

Example:

```text
cardputer-hub-v0.4.12.bin
```

GitHub should retain only the two most recently published official Releases and
their downloadable assets. Draft Releases and temporary rebuild artifacts do
not count toward this limit.

A user should be able to navigate:

```text
GitHub
  ↓
Releases
  ↓
v0.4.12
  ↓
Assets
  ↓
cardputer-hub-v0.4.12.bin
```

without locally building the project.

After a new Release and its assets have been verified, the protected release
workflow should delete GitHub Release records and assets older than the newest
two. It must preserve every Git tag so older versions remain reproducible.

---

## 25. GitHub Actions Artifacts

CI should also store useful build output as GitHub Actions artifacts.

Use cases:

```text
PR testing
debugging
workflow inspection
temporary builds
```

GitHub Release assets are the user-facing storage for the two newest versions.
Historical tag rebuilds should use temporary GitHub Actions artifacts with an
explicit, bounded retention period.

---

## 26. Checksums

Official releases should preferably include SHA-256 checksums.

Example file:

```text
SHA256SUMS
```

Example entry:

```text
<sha256>  cardputer-hub-v0.4.12.bin
```

This allows downloaded firmware integrity to be verified.

---

## 27. Release Metadata

Each release should remain traceable to:

```text
version
Git commit
target board
toolchain
build configuration
```

Relevant metadata should exist in the release, firmware, build output, or an appropriate combination.

---

## 28. Release Pipeline

The intended release process is:

```text
release requested for a validated commit on main
    ↓
full validation
    ↓
version assigned
    ↓
version metadata generated and embedded
    ↓
production firmware build
    ↓
firmware packaged
    ↓
Git tag
    ↓
GitHub Release
    ↓
firmware assets attached
    ↓
release and assets verified
    ↓
GitHub Releases older than the newest two deleted
while all Git tags are preserved
```

A failed required step means the release is unsuccessful.

### Rebuilding a Historical Release

A manually triggered workflow should rebuild any official version from its Git
tag without requiring the corresponding GitHub Release to be retained.

The workflow should:

```text
accept a required tag matching vMAJOR.MINOR.PATCH
      ↓
verify that the tag exists
      ↓
check out the exact tagged commit
      ↓
install the pinned toolchain and locked dependencies
      ↓
run full validation
      ↓
derive and verify the firmware version from the tag
      ↓
build firmware and generate checksums
      ↓
upload a temporary workflow artifact
```

The rebuild workflow should be read-only with respect to tags and GitHub
Releases. Recreating a deleted Release is a separate, explicitly authorized
operation and must still respect the two-Release retention policy.

---

## 29. Release Notes

Release notes should preferably be generated automatically from merged changes.

Manual additions may be used when necessary.

The release process should not require manually reconstructing the list of merged changes.

---

## 30. Branch Protection

Recommended `main` protection:

```text
require pull requests
require CI checks
prevent force pushes
prevent deletion
```

Optionally:

```text
require review
require branch to be up to date
```

depending on project workflow.

---

## 31. CI Security

GitHub Actions workflows should use the minimum required permissions.

PR workflows should not have permission to create:

```text
Git tags
GitHub Releases
```

unless explicitly necessary.

Release permissions should belong only to the appropriate protected release workflow.

Every non-local GitHub Action, including actions published by GitHub, must be
pinned to a full-length commit SHA wherever the workflow syntax supports it.
Floating branches and version tags such as `main` or `v4` are not sufficient.
Keep a comment beside each SHA recording the corresponding upstream release
version, and use an automated dependency updater to propose reviewed SHA
updates.

Pin CI container images by immutable digest wherever supported.

---

## 32. Local Development Commands

Developers should be able to execute CI-equivalent checks locally.

The repository should expose simple commands or scripts for:

```text
format
lint
test
build
check
```

Exact implementation may use:

```text
Makefile
scripts/
PlatformIO commands
task runner
```

but developers should not have to memorize long toolchain-specific commands.

`check` should ideally execute the same major validation categories as PR CI.

---

## 33. Reproducible Local Setup

Repository documentation must explain:

```text
required tooling
tool versions
dependency installation
build command
test command
firmware flashing
serial monitoring
```

A clean checkout should be enough to reproduce the build after installing documented prerequisites.

---

## 34. Secrets

Secrets must never be committed.

Examples:

```text
Wi-Fi credentials
Telegram bot tokens
VPS credentials
API tokens
private keys
```

Provide development templates such as:

```text
secrets.example.h
.env.example
config.example.json
```

when necessary.

Actual secret files must be excluded through `.gitignore`.

CI secrets must use GitHub Secrets or an equivalent secure mechanism.

---

## 35. Configuration Separation

User configuration, runtime configuration, and source code should remain clearly separated.

Do not require recompiling firmware merely to change ordinary runtime settings when persistent configuration can reasonably handle them.

Initial development may temporarily use compile-time configuration where necessary, but the architecture should move user settings toward ConfigurationService.

---

## 36. Logging

The project should provide structured enough logging to diagnose:

```text
boot
Wi-Fi
Bluetooth
host switching
Service errors
Action routing
configuration loading
```

Logging should not expose credentials or secrets.

Log levels should be configurable where practical.

---

## 37. Documentation Structure

Repository documentation should include at minimum:

```text
README.md
AGENTS.md
docs/ARCHITECTURE.md
docs/ENGINEERING.md
```

Potential future documentation:

```text
docs/ROADMAP.md
docs/plans/
docs/decisions/
```

---

## 38. Implementation Plans

Large implementation phases may use short-lived or historical plans under:

```text
docs/plans/
```

Examples:

```text
001-project-bootstrap.md
002-connectivity.md
003-host-service.md
004-mini-app-framework.md
```

Plans should describe:

```text
scope
non-goals
implementation steps
acceptance criteria
testing requirements
```

Architecture documents describe long-lived rules.

Plans describe a specific implementation effort.

---

## 39. Architectural Decisions

Important architectural changes may later be documented as Architecture Decision Records.

Suggested location:

```text
docs/decisions/
```

This is optional initially.

Use ADRs when a decision is important enough that future contributors may reasonably ask:

> Why was this implemented this way?

---

## 40. Codex / AI Development

Repository documentation is part of the development interface for coding agents.

`AGENTS.md` provides the entry point.

Agents should be able to determine from repository documentation:

```text
what behavior is expected
where new logic belongs
what dependency direction is allowed
how code is tested
how firmware is built
what defines completion
```

Agents should not introduce parallel architectures when an existing Service or abstraction already owns the responsibility.

Architectural changes should update documentation in the same PR.

---

## 41. Definition of Done

A feature or bug fix is not complete merely because it works once on physical hardware.

For behavioral changes, completion also requires evidence that the relevant
test was written first, observed failing for the expected reason, and then made
to pass.

Minimum Definition of Done for an implementation change:

```text
implementation complete
      +
relevant tests added or updated
      +
existing tests pass
      +
format check passes
      +
static analysis passes
      +
production firmware compiles
      +
documentation updated when required
```

Documentation-only and other non-runtime changes require the checks applicable
to the files and behavior they affect; they do not require unrelated firmware
or runtime tests. All required PR CI must pass before merge, but PR creation and
remote CI completion are merge-readiness requirements rather than prerequisites
for accurately reporting locally completed work.

---

## 42. Release Definition of Done

An official release is complete only when:

```text
full validation passed

version assigned

version metadata generated and embedded

production firmware built

Git tag created

GitHub Release created

downloadable firmware attached

CI build artifacts stored

only the two newest GitHub Releases retained

all official Git tags preserved
```

If one of the required release steps fails, the pipeline should report failure rather than silently producing a partial release.

---

## 43. Initial Project Infrastructure

The following should be created at the beginning of the project rather than postponed:

```text
repository structure
reproducible firmware build
host-side test infrastructure
hardware abstraction boundaries
formatter
static analysis
PR CI
firmware compilation in CI
basic branch protection
release workflow
automatic versioning
embedded firmware version
Git tags
GitHub Releases
two-Release retention automation
tag-based historical rebuild workflow
build artifacts
developer documentation
```

Feature development should happen on top of this infrastructure.

---

## 44. Guiding Principle

> A behavior that can be automatically verified is significantly safer and cheaper to maintain.

Testability, reproducible builds, and automated releases are therefore first-class engineering requirements for Cardputer Hub.
