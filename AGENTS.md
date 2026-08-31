# Cardputer Hub — Agent Instructions

Cardputer Hub is firmware for the M5Stack Cardputer-Adv.

## Source of Truth

Before making architectural or implementation changes, read the relevant
sections of:

* `docs/ARCHITECTURE.md` — product and software architecture
* `docs/ENGINEERING.md` — development, testing, CI/CD, versioning, and release requirements

These documents are authoritative. If an implementation changes an
architectural decision, update the relevant documentation in the same pull
request. Do not silently introduce an alternative architecture.

## Essential Architecture Guardrails

The primary dependency direction is:

```text
Mini Apps
    ↓
Services
    ↓
Connectivity
    ↓
Hardware Adapters
```

System Core provides cross-cutting infrastructure. Dependencies must not point
upward through the application layers.

In particular:

* keep Mini Apps focused on UI and interaction;
* put reusable state, integrations, and behavior in Services;
* keep Wi-Fi and Bluetooth infrastructure in Connectivity;
* keep hardware-specific behavior behind adapters;
* represent user intent as logical Actions routed through the shared Action infrastructure;
* treat host names, platforms, mappings, and Bluetooth bond references as dynamic `HostProfile` data;
* never hardcode personal host devices as application-logic special cases;
* keep core application logic testable without physical Cardputer hardware.

Detailed responsibilities, examples, and planned phases are defined in
`docs/ARCHITECTURE.md`.

## Testing and Verification

Use test-driven development for behavioral changes:

```text
RED      write the next behavioral test and confirm the expected failure
GREEN    implement the minimum behavior needed to pass
REFACTOR improve the design while keeping the tests green
```

Bug fixes should begin with a failing regression test whenever practical.
Tests should focus on observable behavior rather than implementation details.
Document any exceptional case where a behavioral test cannot be written first.

Run every repository check applicable to the change as defined in
`docs/ENGINEERING.md`.

Implementation changes normally require:

```text
formatting
static analysis
unit tests
integration tests
Cardputer-Adv firmware compilation
```

Documentation-only changes require the available documentation formatting,
linting, and link checks, but do not require unrelated firmware compilation or
runtime tests. Never claim a check passed unless it was actually run.

Do not consider a task complete while an applicable required check is failing.

## Documentation

Update documentation when a change affects:

* architecture;
* responsibilities between layers;
* public Service contracts;
* configuration or persistence models;
* development workflow;
* build or release processes.

Keep detailed design information in `docs/` and avoid duplicating it here.
