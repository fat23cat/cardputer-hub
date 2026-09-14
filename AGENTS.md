# Cardputer Hub — Agent Instructions

Cardputer Hub is firmware for the M5Stack Cardputer-Adv.

## Load Context on Demand

Start with the request, `git diff`, affected code, and nearby tests. Find
headings before reading long documents; never read them cover to cover by
default.

* Architecture, ownership, contracts, persistence: relevant sections of
  `docs/ARCHITECTURE.md`.
* Tests, builds, CI, versions, releases: `docs/ENGINEERING.md`.
* Visible UI, input, motion, sound, display power: `docs/UI_REQUIREMENTS.md`.
* Current features, controls, installation, operation: `docs/manuals/`.

Read `docs/plans/` only when the task names or continues a plan, starting with
its current-status section. Historical notes are not current instructions.
These sources are authoritative; update the owning document when its contract
changes and do not create a parallel architecture.

## Architecture Guardrails

The primary dependency direction is:

```text
Mini Apps -> Services -> Connectivity -> Hardware Adapters
```

System Core provides cross-cutting infrastructure. Dependencies must not point
upward through the application layers.

* Mini Apps own UI/interaction; Services own reusable state and behavior.
* Connectivity owns Wi-Fi/Bluetooth; adapters hide hardware specifics.
* Route logical user Actions through shared Action infrastructure.
* Keep host details and bond references in dynamic `HostProfile` data; never
  special-case personal devices.
* Keep core logic host-testable.

## Efficient Implementation and Verification

For behavioral work, use focused TDD:

```text
RED -> one relevant failing test
GREEN -> minimum implementation and the same focused test
REFACTOR -> focused tests stay green
```

During implementation, run the narrowest affected test. Preserve `.pio/`,
`build/`, and compiler caches; do not routinely clean, reconfigure, or run full
gates after each edit.

Before reporting completion, run the applicable final gate once after the last
material change:

* documentation-only: available documentation checks, no firmware build;
* firmware-affecting implementation: `make check` (or its two component gates);
* build/dependency/toolchain work: `make check` plus focused workflow checks.

Re-run a passing gate only after relevant later edits, to confirm a failure, or
to resolve a concrete concern. Never claim an unrun check or finish with a
required failure. See `docs/ENGINEERING.md` sections 32, 40, and 41.

Keep manuals aligned with delivered firmware. Update documentation for changed
architecture, contracts, configuration, workflow, features, controls, or user
procedures; never present planned behavior as supported.

## GitHub CLI

Run every `gh` CLI command outside the sandbox on its first attempt, including
read-only GitHub queries. Use the execution tool's escalated-permission mode
with a narrowly scoped approval request; do not first run `gh` in the sandbox
and wait for its network or authentication access to fail.

## CRUB Distribution

The sibling `cardputer-firmware-manager` repository owns the deployable shared
CRUB partition layout and SD staging contract. Use its `doctor`,
`local --app hub`, and `release --app hub` commands to validate and prepare
multiboot firmware. Do not duplicate that layout here or tell users to bypass
the manager before running CRUB's `uphub` command.
