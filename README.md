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
* [`docs/UI_REQUIREMENTS.md`](docs/UI_REQUIREMENTS.md) — delivered UI behavior
  and the remaining visual, motion, sound, display-power and input requirements
* [`docs/plans/README.md`](docs/plans/README.md) — implementation-plan status index
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
├── UI_REQUIREMENTS.md
└── plans/

.github/
└── workflows/

scripts/
```

The `apps`, `connectivity`, and `services` directories contain the built-in
system UI, connectivity foundations, and host/configuration/battery Services.

---

## Local Setup

The locked development environment uses:

* Python 3.12.14;
* uv 0.12.12;
* ESP-IDF 5.5.5 with its recommended compiler, CMake, and Ninja tools;
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
curl -LsSf https://astral.sh/uv/0.12.12/install.sh | sh
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
exact pinned hardware-support sources. Re-source ESP-IDF's `export.sh` in each
new terminal before running firmware commands.

---

## Install on a Cardputer-Adv from a Fresh Machine

Follow the maintained
[`docs/manuals/installing-firmware.md`](docs/manuals/installing-firmware.md)
guide for prerequisites, USB installation, verification, and troubleshooting.

To keep Cardputer Hub and Codex Microputer ADV installed simultaneously, use
the separate [Cardputer Firmware Manager](https://github.com/fat23cat/cardputer-firmware-manager).
It owns the shared `crub` layout, stages all or selected local builds and GitHub
Release images on microSD, and keeps application data partitions intact during
routine updates.

For a local multiboot build, start CRUB's `usbsd` mode and validate/stage the
Hub image through the sibling manager repository:

```bash
cd ../cardputer-firmware-manager
python3 -m firmware_manager doctor
python3 -m firmware_manager local --app hub --build --sd /Volumes/CARDPUTER
```

The manager calls `scripts/build_firmware.sh`, which activates the pinned
ESP-IDF 5.5.5 installation from the documented setup path, or a repository-local
`build-tools/` installation when present, without inheriting another
application's ESP-IDF environment. Nonstandard installations may set
`CARDPUTER_HUB_IDF_PATH` and, when needed, `CARDPUTER_HUB_IDF_TOOLS_PATH`.

After safely ejecting the card and exiting `usbsd`, run `sd` so CRUB remounts
the card and reloads its aliases. Then run `uphub` and require both `app: ok`
and `flash complete` before launching `hub`. To install a published build
instead, use:

```bash
python3 -m firmware_manager release --app hub --sd /Volumes/CARDPUTER
```

Do not copy or flash an unvalidated raw image directly.

---

## Build

```bash
make build
```

External orchestrators such as Cardputer Firmware Manager use the self-contained
wrapper `scripts/build_firmware.sh`; interactive development may continue to
activate ESP-IDF and call `make build` directly.

The production application and matching partition-table images are written to
`build/cardputer_hub.bin` and
`build/partition_table/partition-table.bin`. The version-controlled flash layout
keeps the framework's default NVS separate from the dedicated `hub_config` NVS
partition reserved for authoritative configuration records. On startup, the
firmware enters through native ESP-IDF, initializes M5Unified directly, writes
structured informational records for the product name, version, commit, and
build type to serial, briefly renders the product name/version, then opens a compact Home with the selected host, BT status, an estimated battery
percentage, and a slow dotted wave. The future clock and Wi-Fi slots currently
show `--:--` and `OFFLINE`. Tab (also G0 or Fn+Tab) opens Settings; its Bluetooth entry opens the existing BLE panel.
Screen navigation uses short horizontal transitions with live input. The update loop polls semantic keyboard events, routes
local settings Actions, and advances HostService/BluetoothService. Host selection,
BLE On/Off, pairing, renaming, and per-host deletion are available; keyboard-to-HID mappings and the
full Mini App shell are not yet implemented.

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
adapter. The same boundary now supports explicit authenticated pairing,
stable opaque bond references, a 16-bond registry, selected-bond reconnection,
explicit bond removal, and a shared hardware-neutral HID report contract. The
direct ESP-NimBLE adapter exposes a secured keyboard and consumer-control HID
service, accepts reports only for the selected authenticated and subscribed
peer, and releases active reports before controlled disconnects. HostService
composes this boundary with ConfigurationService: profiles, selection, and BLE
On/Off are stored in internal `hub_config` NVS. First use defaults to Off and
imports existing pairs without advertising. Switching closes the old connection
before allowing the selected host. Tab (also G0 or Fn+Tab) opens Settings, whose Bluetooth entry exposes these
operations through ActionBus; see the [device guide](docs/manuals/device-guide.md).

---

## Tests

The host-management suites cover persistence, selected-host switching and Off,
error handling, pairing cancellation/completion, and local settings Actions
through real Services with fake hardware. Physical two-host acceptance is tracked
separately in plan 017.

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
rejection, pairing-window timing, all authenticated challenge modes, strict
security completion, stable-reference finalization, capacity, target selection,
bond deletion, reconnect timing, capped advertising backoff, fatal cleanup,
stale event isolation, retained cleanup retries, and identity-free diagnostics.
Peer-rejection coverage verifies that advertising cannot resume until all
asynchronous disconnects for rejected peers have completed.
The HID suites additionally cover neutral and six-key keyboard reports,
consumer usages, invalid and duplicate usage rejection, selected-peer and dual
subscription readiness, report ownership, retryable backpressure, neutral
release, stale callbacks, controlled target changes, and clean re-enable.

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

When multiple serial devices are connected, list the ports with
`python -m serial.tools.list_ports` and select the Cardputer's fixed USB
Serial/JTAG port explicitly:

```bash
make monitor UPLOAD_PORT=<port>
```

Replace `<port>` with the detected device path, such as `/dev/ttyACM0` on Linux
or `/dev/cu.usbmodem...` on macOS.

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

Reviewed on **2026-09-11**. The authoritative
[phase checklist](docs/ARCHITECTURE.md#47-initial-development-order) records
completed steps and remaining work; the [plan index](docs/plans/README.md)
links implementation and validation history.

| Phase | Status |
| --- | --- |
| 1 — System Core | Complete |
| 2 — Connectivity | Software complete; physical acceptance partial |
| 3 — Core Services | HostService, host configuration and battery delivered; broader Services pending |
| 4 — Application Shell | Home, Settings, Tab navigation and page transitions delivered; Launcher/power/sound pending |
| 5 — Mini App Infrastructure | Pending; registry primitives exist |
| 6 — Device Manager | Built-in host list/pair/rename/delete/select delivered; full Mini App integration pending |
| 7–11 — Host Control, Weather, RGB, Remote, Extensions | Pending |

Normal firmware provides a BLE-only host connection, saved profiles and On/Off,
Home telemetry, and local Settings. USB is for power, flashing and fixed serial
diagnostics. USB HID and arbitration are removed; IHidTransport remains the
future extension boundary. Wi-Fi is not yet composed; clock synchronization,
Action-to-HID mappings and a Mac companion are not implemented.

Physical checks confirmed fresh pairing, one local Off/On cycle, adding and
switching two computers, and reconnection to the last selected host after Reset
and power-on. The operator accepted the Home/navigation appearance.
[Plan 017](docs/plans/017-hid-transport-arbitration.md#0-current-closeout-status)
keeps longer Off/reboot-Off checks, report/interruption reruns, long-duration
and equipment-limited cases, and USB serial hotplug open. Historical harness passes are not represented
as final-image acceptance. Latest local checks passed: 44 Python tests, 214
native tests, formatting, static analysis and ESP-IDF production compilation.

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
