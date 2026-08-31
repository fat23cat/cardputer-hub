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
Storage primitives
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

## Install on a Cardputer-Adv from a Fresh Machine

These instructions build the firmware from source and install it over USB. No
global PlatformIO or M5Stack library installation is required.

### What you need

* an M5Stack Cardputer-Adv;
* a USB-C cable that supports data, not a charge-only cable;
* a macOS or Ubuntu/Debian computer with Internet access;
* permission to install command-line development tools.

### 1. Install system prerequisites

On macOS, install Git and the host compiler with Xcode Command Line Tools:

```bash
xcode-select --install
```

On Ubuntu or Debian Linux, install the native build prerequisites and grant
your user access to USB serial devices:

```bash
sudo apt-get update
sudo apt-get install --yes build-essential git curl
sudo usermod --append --groups dialout "$USER"
```

Log out and back in after changing the `dialout` group. This is not normally
required on macOS.

### 2. Download Cardputer Hub

```bash
git clone https://github.com/fat23cat/cardputer-hub.git
cd cardputer-hub
```

### 3. Install the locked development tools

Install the repository's exact uv version using its versioned installer. Review
downloaded installation scripts first when required by your environment's
security policy.

```bash
curl -LsSf https://astral.sh/uv/0.12.7/install.sh | sh
```

Restart the terminal if the `uv` command is not immediately available. Then
install the pinned Python version and repository tools:

```bash
uv python install 3.12.14
make setup
```

The project environment uses Python 3.12.14, uv 0.12.7, PlatformIO Core
6.1.19, clang-format 23.1.0, and the exact ESP32 and M5Stack dependencies from
`platformio.ini`. The virtual environment is local to this checkout and does
not replace the macOS system Python.

### 4. Connect and detect the device

Connect the powered Cardputer-Adv directly to the computer with the USB-C data
cable. Close any other serial monitor that may already have the port open, then
check that PlatformIO can see a serial device:

```bash
uv run --frozen pio device list
```

### 5. Build and install the firmware

```bash
make upload
```

The first run may take several minutes while PlatformIO downloads the pinned
toolchain and libraries. It then builds the production firmware, selects the
connected serial port, flashes the image, and resets the device.

If PlatformIO cannot enter download mode:

1. hold the `G0` button on the Cardputer-Adv;
2. press and release the reset button;
3. release `G0`;
4. run `make upload` again.

### 6. Verify the installation

The display should show `Cardputer Hub` and the embedded firmware version on a
black boot screen. To inspect serial output, run:

```bash
make monitor
```

Press reset once after the monitor opens if the startup records have already
scrolled past. A successful boot prints records similar to:

```text
[INFO] firmware.name: Cardputer Hub
[INFO] firmware.version: 0.1.0-dev
[INFO] firmware.commit: <commit>
[INFO] firmware.build_type: cardputer-adv
```

Exit the serial monitor with `Ctrl+]`. The firmware currently polls keyboard
events but does not yet route them to visible product behavior.

### Troubleshooting installation

* If no serial device appears, try a known data-capable cable and another USB
  port, then reconnect the Cardputer-Adv.
* If Linux reports permission denied, confirm membership in the `dialout`
  group with `groups`, then log out and back in.
* If upload waits for a connection or times out, use the `G0` download-mode
  sequence above and retry.
* If the monitor is blank, confirm the device reset and that no other program
  has the serial port open. The configured monitor speed is 115200 baud.
* To discard local build output and rebuild from scratch, run `make clean`,
  followed by `make upload`.

### Verify the tool installation

Developers can confirm the locked tool versions with:

```bash
uv --version
uv run --frozen python --version
uv run --frozen pio --version
uv run --frozen clang-format --version
```

Do not install project-specific ESP32 or M5Stack libraries globally.
PlatformIO resolves them from `platformio.ini`.

---

## Build

```bash
make build
```

The production image is written to
`.pio/build/cardputer-adv/firmware.bin`. On startup, the firmware initializes
the Cardputer once, writes structured informational records for the product
name, version, commit, and build type to serial, and renders the product name
and version as a minimal boot screen. Its update loop refreshes the hardware
and polls semantic keyboard input events. Those events are intentionally not
routed to product behavior yet, and no connectivity or Mini Apps are started.

---

## Tests

Native tests run on the host and require no Cardputer hardware:

```bash
make test
```

Behavior changes follow red-green-refactor TDD and should test observable
behavior. The native suites cover stable firmware build metadata, log-level
filtering, keyboard event translation and deduplication, and System Core boot
and update orchestration.

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
removes PlatformIO build output.

---

## Flash

Connect the Cardputer-Adv with a USB-C cable that supports data, then run:

```bash
make upload
```

PlatformIO normally resets the device automatically. If it cannot enter
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

Pull requests to `main`, pushes to `main`, and manual CI runs execute
`make check` on Ubuntu 24.04. CI also uploads the compiled firmware as an
artifact retained for seven days. All third-party Actions use full commit SHA
pins, and Dependabot proposes reviewed updates.

Official releases are created only by manually dispatching the protected
`Release firmware` workflow from `main` with a `MAJOR.MINOR.PATCH` version. The
workflow repeats full validation, embeds release metadata, creates a
`vMAJOR.MINOR.PATCH` tag, and publishes the firmware plus `SHA256SUMS`.

Only the two newest GitHub Release records and their assets are retained. All
official Git tags are preserved. To rebuild an older version, manually run
`Rebuild tagged firmware` with its existing `vMAJOR.MINOR.PATCH` tag. That
read-only workflow checks out the tag and provides firmware and checksums as a
temporary seven-day artifact; it does not recreate or delete Releases or tags.

The primary local commands are:

```text
make setup         install locked tools
make build         compile Cardputer-Adv firmware
make test          run native tests
make format        format owned C/C++ sources
make format-check  verify formatting
make lint          run static analysis
make check         run all required validation
make upload        compile and flash firmware
make monitor       open the 115200-baud serial monitor
make clean         remove PlatformIO build output
```

See [`docs/ENGINEERING.md`](docs/ENGINEERING.md) for the complete workflow.

---

## Current Status

The initial development infrastructure is complete. Product development should
now proceed through the documented architecture phases.

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
