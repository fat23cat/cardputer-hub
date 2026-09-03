# Cardputer Hub

Cardputer Hub is an extensible firmware platform for the **M5Stack Cardputer-Adv**.

The project is designed as a small personal device platform built around reusable Services and independent Mini Apps.

The long-term goal is to support functionality such as:

* Bluetooth host control
* multiple paired devices
* Wi-Fi connectivity
* weather information
* VPS monitoring
* media controls
* RGB status indication
* microSD-backed file storage
* Telegram integration
* remote control through a Web UI
* additional Mini Apps and hardware extensions

The project is intentionally not designed as a single-purpose MacBook remote.

---

## Architecture

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

The System Core provides shared infrastructure such as:

```text
Application lifecycle
Launcher
App Registry
Navigation
Input routing
Action Bus
Configuration interfaces
Record and file-storage primitives
Logging
Capabilities
```

### Example

```text
WeatherApp
    ↓
WeatherService
    ↓
WiFiService
    ↓
ESP32 Wi-Fi
```

Mini Apps should remain thin.

Reusable logic, state, integrations, and background behavior belong in Services.

---

## Documentation

Project documentation is the source of truth for architecture and engineering decisions.

Read:

* [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — system architecture, Services, Mini Apps, connectivity, host management, remote control, and planned development phases
* [`docs/ENGINEERING.md`](docs/ENGINEERING.md) — testing, CI/CD, build system, versioning, releases, and development requirements
* [`docs/manuals/`](docs/manuals/) — installation, supported features, device
  controls, and user-facing procedures
* [`AGENTS.md`](AGENTS.md) — instructions for Codex and other coding agents working in this repository

Architectural changes should update the relevant documentation in the same pull request.

---

## Project Structure

```text
src/
├── core/
├── connectivity/
├── services/
├── apps/
├── hardware/
└── main.cpp

test/

docs/
├── ARCHITECTURE.md
├── ENGINEERING.md
└── plans/

.github/
└── workflows/

scripts/
```

The empty `apps`, `connectivity`, and `services` directories reserve the
documented boundaries. No product behavior is implemented in the bootstrap.

---

## Local Setup

The locked development environment uses:

* Python 3.12.14;
* uv 0.12.7;
* ESP-IDF 5.5.5 with its recommended compiler, CMake, and Ninja tools;
* Arduino Core 3.3.11 as an exact ESP-IDF managed component;
* PlatformIO Core 6.1.19 only for native host tests and static analysis;
* clang-format 23.1.0;
* a C++17 host compiler for native tests and C++17 project-owned firmware
  components;
* exact managed-component and Git-submodule dependency locks.

On macOS, install Git and the host compiler with Xcode Command Line Tools:

```bash
xcode-select --install
```

On Ubuntu or Debian Linux, install the native build prerequisites:

```bash
sudo apt-get update
sudo apt-get install --yes build-essential git curl wget flex bison gperf \
  python3 python3-pip python3-venv ccache libffi-dev libssl-dev dfu-util \
  libusb-1.0-0
```

Install the repository's exact uv version using its versioned installer, then
let uv install Python and the locked tools. Review downloaded installation
scripts before running them when required by your environment's security
policy.

```bash
curl -LsSf https://astral.sh/uv/0.12.7/install.sh | sh
uv python install 3.12.14
bash scripts/install_esp_idf.sh \
  "$HOME/.espressif/frameworks/esp-idf-v5.5.5"
source "$HOME/.espressif/frameworks/esp-idf-v5.5.5/export.sh"
make setup
```

The locked virtual environment is local to the checkout; these commands do not
replace the macOS system Python. Verify the resolved versions with:

```bash
uv --version
uv run --frozen python --version
uv run --frozen pio --version
uv run --frozen clang-format --version
idf.py --version
```

Do not install project-specific ESP32 or M5Stack libraries globally. The
ESP-IDF component manager resolves the exact production graph from
`main/idf_component.yml` and `dependencies.lock`; Git submodules retain the
exact Arduino-only library revisions. Re-source ESP-IDF's `export.sh` in each
new terminal before running firmware commands.

---

## Install on a Cardputer-Adv from a Fresh Machine

Follow the maintained
[`docs/manuals/installing-firmware.md`](docs/manuals/installing-firmware.md)
guide for prerequisites, USB installation, verification, and troubleshooting.

---

## Build

```bash
make build
```

The production application and matching partition-table images are written to
`build/cardputer_hub.bin` and
`build/partition_table/partition-table.bin`. The version-controlled flash layout
keeps the framework's default NVS separate from the dedicated `hub_config` NVS
partition reserved for authoritative configuration records. On startup, the
firmware initializes the Cardputer once, writes structured informational
records for the product name, version, commit, and build type to serial, and
renders the product name and version as a minimal boot screen. Its update loop
refreshes the hardware and polls semantic keyboard input events. Those events
are intentionally not routed to product behavior yet, and no connectivity or
Mini Apps are started.

System Core also provides standalone navigation history, capability, and
application-metadata registries plus record- and file-storage boundaries for
later phases. The Cardputer microSD adapter compiles against the pinned board
framework but is not constructed or mounted by the firmware runtime. These
foundations do not provide Launcher, Mini App, file-browser, backup, or
configuration import/export behavior.

The first Phase 2 foundation adds a hardware-independent Wi-Fi connection state
machine and a compiled ESP32 station adapter. Neither is constructed by the
firmware runtime yet: no credentials are compiled or persisted, no connection
starts automatically, and the supported device behavior remains unchanged.

The Bluetooth lifecycle foundation similarly adds a hardware-independent
single-peer state machine and a compiled direct ESP-NimBLE peripheral
adapter. It remains unconstructed at runtime and does not yet provide pairing,
bond management, or HID reports, so normal device behavior is unchanged.

---

## Tests

Native tests run on the host and require no Cardputer hardware:

```bash
make test
```

Behavior changes follow red-green-refactor TDD and should test observable
behavior. The native suites cover stable firmware build metadata, log-level
filtering, keyboard event translation and deduplication, opaque record-storage
validation and forwarding, owned navigation history and Back traversal,
dynamic capability registration and enumeration, owned application metadata
validation and lookup, bounded logical file-storage operations, and System
Core boot and update orchestration. They also cover Wi-Fi configuration
validation, connection state, timeout and capped retry timing, connected-only
and link-loss-safe RSSI access, disconnect-error propagation, and
credential-free diagnostics.
The Bluetooth suite covers side-effect-free construction, explicit lifecycle
results, callback-event isolation, bonded single-peer policy, unbonded-peer
rejection, reconnect timing, capped advertising backoff, fatal cleanup, stale
event isolation, retained cleanup retries, and identity-free diagnostics.
Peer-rejection coverage verifies that advertising cannot resume until all
asynchronous disconnects for rejected peers have completed.

Run formatting and static analysis separately with:

```bash
make format
make format-check
make lint
```

Before opening a pull request, run the complete CI-equivalent suite:

```bash
make check
```

`make check` verifies the lock, formatting, Cppcheck analysis, native tests,
strict compiler warnings, and the Cardputer-Adv firmware build. `make clean`
removes ESP-IDF production build output. PlatformIO remains scoped to host-side
tests and analysis.

---

## Flash

Connect the Cardputer-Adv with a USB-C cable that supports data, then run:

```bash
make upload
```

For the first installation, or a one-time upgrade from the earlier flash
layout, use `make migrate-storage-layout UPLOAD_PORT=<device>` instead. The
explicit port ensures the upload and targeted erase reach the same Cardputer.
It provisions the new configuration range; routine upgrades must continue to
use `make upload` so stored configuration is preserved. See the
[`installation guide`](docs/manuals/installing-firmware.md) for the exact
migration and release-asset flashing procedure.

ESP-IDF's flash command normally resets the device automatically. If it cannot enter
download mode, hold the `G0` button, press and release reset, release `G0`, and
retry the upload. You may need to grant access to the serial device on Linux.

---

## Serial Monitor

```bash
make monitor
```

The configured baud rate is 115200. Exit the monitor with `Ctrl+]`.

---

## CI/CD

Pull requests targeting any branch, pushes to `main`, and manual CI runs execute
the `make host-check` and `make firmware-check` portions of `make check` in
parallel on Ubuntu 24.04. This includes stacked pull requests whose base is
another feature branch. A final required status succeeds only when both paths
pass. CI also uploads the compiled application and partition-table images as an
artifact retained for seven days. All third-party Actions use full commit SHA
pins, and Dependabot proposes reviewed updates.

After CI validates a merged pull request on `main`, the protected
`Release firmware` workflow uses the source branch prefix to assign the next
semantic version: `major/` or `breaking/` bumps major, `feat/` or `minor/` bumps
minor, and `fix/`, `perf/`, or `patch/` bumps patch. Other branch types do not
release firmware. The first eligible release is `v0.1.0`. The workflow repeats
full validation, embeds release metadata, creates the version tag, and
publishes the application image, its matching partition-table image, and
`SHA256SUMS` covering both files.

Only the two newest GitHub Release records and their assets are retained. All
official Git tags are preserved. To rebuild an older version, manually run
`Rebuild tagged firmware` with its existing `vMAJOR.MINOR.PATCH` tag. That
read-only workflow checks out the tag and provides the application image,
partition table, and checksums as a temporary seven-day artifact; it does not
recreate or delete Releases or tags.

The primary local commands are:

```text
make setup         resolve locked repository and ESP-IDF dependencies
make build         compile Cardputer-Adv firmware
make test          run native tests
make format        format owned C/C++ sources
make format-check  verify formatting
make lint          run static analysis
make host-check    run lock, format, lint, and native test checks
make firmware-check
                   build and verify the production firmware
make check         run all required validation
make upload        compile and flash firmware
make migrate-storage-layout UPLOAD_PORT=<device>
                   one-time migration from the earlier flash layout
make monitor       open the 115200-baud serial monitor
make clean         remove ESP-IDF production build output
```

See [`docs/ENGINEERING.md`](docs/ENGINEERING.md) for the complete workflow.

---

## Current Status

The Phase 1 System Core foundations are complete, and Phase 2 Connectivity is
in progress with its Wi-Fi and Bluetooth lifecycle foundations delivered.
These milestones establish testable contracts and hardware adapters; they do
not make Wi-Fi, Bluetooth, or other planned product features user-visible.
Authenticated Bluetooth pairing and bond handling are the next Phase 2
foundation in the documented architecture order.

The authoritative development order is defined in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md#47-initial-development-order):

```text
1. System Core
2. Connectivity
3. Core Services
4. Application Shell
5. Mini App Infrastructure
6. Device Manager
7. Host Control
8. Weather
9. RGB Indicator
10. Remote Boundary
11. Extensions
```

Product features should follow this order and the documented dependency
direction.

---

## Initial Hardware

Target device:

* M5Stack Cardputer-Adv

Planned external hardware:

* M5Stack Unit Puzzle 8×8 WS2812E RGB LED matrix

Additional sensors and modules may be added later.

---

## License

License has not been selected yet.
