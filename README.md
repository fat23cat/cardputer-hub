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

## Planned Project Structure

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
└── ENGINEERING.md

.github/
└── workflows/
```

The exact structure may evolve during implementation while preserving the documented dependency direction.

---

## Development Environment

The planned initial development environment uses:

* macOS
* Git
* Python 3.12
* uv
* PlatformIO Core

The Python 3.12 series is currently selected through `.python-version`. Its
exact patch version, along with exact versions and installation instructions
for `uv`, PlatformIO Core, and the firmware toolchain, will be added during
project bootstrap before the build is considered reproducible.

### Verify the local environment

```bash
git --version
uv --version
python3.12 --version
pio --version
```

---

## Python

The repository uses Python 3.12 for development tooling.

The expected project version is defined in:

```text
.python-version
```

With `uv` installed, install the Python version selected by the repository:

```bash
uv python install 3.12
```

Do not modify or replace the macOS system Python for this project.

---

## PlatformIO

PlatformIO is the preferred firmware build system.

Verify the installation:

```bash
pio --version
```

Project-specific ESP32 and M5Stack dependencies should be declared in `platformio.ini` rather than installed manually.

---

## Build

The exact build configuration will be added during project bootstrap.

The expected command will be:

```bash
pio run
```

---

## Tests

The project is designed so that most application and Service logic can be tested without physical Cardputer hardware.

The expected host-side test command will be:

```bash
pio test
```

Additional convenience commands may be introduced during repository bootstrap.

---

## Flashing

Once the Cardputer-Adv build environment is configured, firmware should be uploadable with:

```bash
pio run -t upload
```

The Cardputer must be connected using a USB-C cable that supports data transfer.

---

## Serial Monitor

The expected command is:

```bash
pio device monitor
```

The final baud rate and monitor configuration will be defined in `platformio.ini`.

---

## CI/CD

Pull requests are expected to run automated checks including:

```text
formatting
static analysis
unit tests
integration tests
Cardputer-Adv firmware build
```

Validated commits on `main` may be published through the separate protected
release workflow.

The two newest official firmware versions should be published through GitHub
Releases. All official Git tags should be preserved, and a manually triggered
workflow should be able to rebuild any tagged version as a temporary artifact.

See [`docs/ENGINEERING.md`](docs/ENGINEERING.md) for the complete workflow.

---

## Current Status

The project is currently in the **architecture and bootstrap phase**.

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

Product features should not be implemented before the initial development infrastructure is in place.

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
