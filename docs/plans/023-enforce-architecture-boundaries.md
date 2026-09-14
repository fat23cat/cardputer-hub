# 023 — Enforce Architecture Boundaries

## 1. Goal

Make the documented architectural dependency rules mechanically enforceable.

The repository currently documents the primary dependency direction as:

```text
Apps
  ↓
Services
  ↓
Connectivity
  ↓
Hardware Adapters
```

with System Core providing shared infrastructure.

However, production sources currently compile as one broad ESP-IDF component with the full `src` directory available as an include root.

As a result, architectural boundaries are currently conventions rather than build-time constraints.

This plan introduces automated validation that prevents forbidden cross-layer dependencies.

---

## 2. Problem

Today the following would be technically possible:

```cpp
// src/apps/foo.cpp
#include "hardware/esp32/..."
```

or:

```cpp
// src/core/foo.cpp
#include "services/..."
```

even though these dependencies violate the intended architecture.

The compiler and linker would accept them because all project-owned sources are part of the same broad component.

As the number of Services and Mini Apps grows, architectural drift becomes increasingly likely.

---

## 3. Scope

Included:

* formal dependency rules;
* automated source-level dependency validation;
* CI integration;
* tests for the validator;
* documentation of allowed dependencies.

Optional, but not required in this plan:

* splitting production code into multiple CMake libraries/components.

Not included:

* moving source directories;
* rewriting existing architecture;
* Launcher implementation;
* dependency injection framework;
* generic module system.

---

## 4. Dependency Rules

Define an explicit dependency matrix.

Initial recommended rules:

```text
core
  → C++ standard library only
  → selected ESP-IDF-independent interfaces where explicitly approved

hardware
  → core interfaces
  → connectivity interfaces/types when implementing connectivity adapters
  → platform libraries / ESP-IDF / M5Stack

connectivity
  → core
  → connectivity

services
  → core
  → connectivity
  → services

apps
  → core
  → services
  → apps

validation
  → any production layer required by hardware validation

main.cpp
  → all layers
```

Forbidden examples:

```text
core → services
core → apps
core → hardware

connectivity → services
connectivity → apps

services → apps
services → hardware

apps → hardware
```

If exceptions exist, document them explicitly instead of weakening the whole rule.

---

## 5. First Implementation: Dependency Checker

Start with a repository script rather than immediately restructuring CMake.

Suggested file:

```text
scripts/check_architecture.py
```

The script should:

1. scan project-owned `.h/.hpp/.cpp` files;
2. parse project-local `#include "..."` directives;
3. identify source and target layers;
4. validate each edge against the dependency matrix;
5. report violations with filename, line number, and dependency;
6. exit non-zero on any violation.

Example output:

```text
Architecture violation:

src/apps/weather/weather_app.cpp:8
apps -> hardware is forbidden

#include "hardware/esp32/wifi/esp32_wifi_adapter.h"

Allowed dependency:
apps -> services
```

---

## 6. Do Not Build a C++ Parser

The first implementation should intentionally remain simple.

Only project-local includes need to be inspected:

```cpp
#include "core/..."
#include "services/..."
```

System includes can be ignored:

```cpp
#include <string>
#include <vector>
```

The repository has predictable include paths, so a lightweight line parser is sufficient.

Do not introduce Clang tooling solely for this check.

---

## 7. Configuration

Keep the dependency matrix explicit and readable.

Example:

```python
ALLOWED = {
    "core": {"core"},
    "connectivity": {"core", "connectivity"},
    "services": {"core", "connectivity", "services"},
    "apps": {"core", "services", "apps"},
    "hardware": {"core", "connectivity", "hardware"},
    "validation": {
        "core",
        "connectivity",
        "services",
        "apps",
        "hardware",
        "validation",
    },
}
```

`src/main.cpp` should be treated as the composition root and allowed to depend on all layers.

Avoid per-file exception lists unless a real architectural requirement exists.

---

## 8. CI Integration

Add a Make target:

```make
architecture-check:
    $(RUN) python scripts/check_architecture.py
```

Then include it in:

```make
host-check
```

Recommended ordering:

```text
lock-check
architecture-check
format-check
lint
test
```

This ensures architectural violations fail PRs before firmware build.

---

## 9. Tests

Add tests for the checker itself.

Suggested:

```text
test_python/test_architecture_dependencies.py
```

Test scenarios:

* `apps → services` accepted;
* `apps → hardware` rejected;
* `services → connectivity` accepted;
* `services → apps` rejected;
* `connectivity → hardware` rejected;
* `core → services` rejected;
* `hardware → connectivity` accepted;
* validation harness may access hardware;
* `main.cpp` may compose all layers;
* system includes are ignored;
* comments containing `#include` are ignored if necessary.

Tests should use temporary fixture files rather than modifying repository sources.

---

## 10. Existing Code Validation

Run the checker against the existing project before enabling CI.

Any current violations should be handled explicitly:

```text
1. determine whether the dependency is accidental;
2. move the dependency behind an interface if needed;
3. only add an exception if the dependency is intentionally architectural.
```

Do not silently whitelist existing violations simply to make the check pass.

---

## 11. Optional Second Phase: CMake Enforcement

After the source-level checker is stable, consider splitting project code into logical CMake targets.

Possible shape:

```text
hub_core
hub_connectivity
hub_services
hub_apps
hub_hardware
```

Dependency graph:

```text
hub_apps
  ↓
hub_services
  ↓
hub_connectivity
  ↓
hub_core

hub_hardware
  ↓
hub_connectivity
  ↓
hub_core
```

The final application target would link them together.

This gives compiler/linker-level boundary enforcement.

However, this should be a separate follow-up unless it remains trivial.

The dependency checker delivers most of the immediate value with much less build-system risk.

---

## 12. Architectural Documentation

Update `docs/ARCHITECTURE.md` with the explicit allowed-dependency matrix.

Clarify that:

```text
main.cpp is the composition root and is intentionally exempt from normal
downward dependency restrictions.
```

Also clarify that hardware implementations may implement interfaces declared in Core or Connectivity but business/application code must never reach hardware implementations directly.

---

## 13. Acceptance Criteria

The plan is complete when:

* architecture dependency rules are explicitly documented;
* `scripts/check_architecture.py` exists;
* forbidden project-local includes fail the check;
* allowed dependencies pass;
* the check runs as part of `make host-check`;
* CI rejects an intentionally introduced `apps → hardware` dependency;
* current production code passes without unjustified exceptions;
* firmware output and runtime behavior remain unchanged.

---

## 14. Follow-Up

Once Mini Apps start growing, consider converting the logical layers into separate CMake targets/components.

Do not perform that migration solely for aesthetic reasons.

The important goal is that architectural violations become impossible to merge unnoticed.

---

## 15. Completion Record

Implemented on 2026-09-14.

The RED steps were observed before implementation: the focused suite first
failed because the checker module did not exist, the CI-contract scenario
failed because `host-check` did not run the new target, and the relative-include
scenario demonstrated that `../../hardware/...` initially bypassed the layer
classifier. GREEN added the lightweight include checker, explicit dependency
matrix, comment-aware parsing, root-relative and relative include
classification, actionable diagnostics, Make/CI wiring, and architecture and
engineering documentation. Review hardening then made classification normalize
the complete path before selecting its layer and made unknown source and
resolved target layers fail closed.

Verification results:

* focused architecture suite: 18/18 passed;
* current production tree: 87 source files checked with no violations;
* host gate: lock, architecture, formatting, and static-analysis checks passed;
  all 66 Python and 253 native C++ tests passed;
* static analysis retained three pre-existing low-severity style findings and
  no high- or medium-severity findings;
* ESP-IDF 5.5.5 production firmware gate passed; `cardputer_hub.bin` is 842,672
  bytes with 75% of the smallest application partition free.

The optional CMake target split remains deferred as described in section 11.
