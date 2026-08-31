# Install Cardputer Hub

These instructions build the firmware from source and install it on an M5Stack
Cardputer-Adv over USB. No global PlatformIO or M5Stack library installation is
required.

## What You Need

* an M5Stack Cardputer-Adv;
* a USB-C cable that supports data, not a charge-only cable;
* a macOS or Ubuntu/Debian computer with Internet access;
* permission to install command-line development tools.

## 1. Install System Prerequisites

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

## 2. Download Cardputer Hub

```bash
git clone https://github.com/fat23cat/cardputer-hub.git
cd cardputer-hub
```

## 3. Install the Locked Development Tools

Install the repository's exact uv version using its versioned installer. Review
downloaded installation scripts first when required by your environment's
security policy.

```bash
curl -LsSf https://astral.sh/uv/0.12.7/install.sh | sh
```

Restart the terminal if `uv` is not immediately available. Then install the
pinned Python version and repository tools:

```bash
uv python install 3.12.14
make setup
```

The virtual environment is local to the checkout and does not replace the
system Python. PlatformIO resolves the exact ESP32 and M5Stack dependencies
from `platformio.ini`.

## 4. Connect and Detect the Device

Connect the powered Cardputer-Adv directly to the computer with the USB-C data
cable. Close any other serial monitor that may already have the port open, then
check that PlatformIO can see a serial device:

```bash
uv run --frozen pio device list
```

## 5. Build and Install the Firmware

```bash
make upload
```

The first run may take several minutes while PlatformIO downloads the pinned
toolchain and libraries. It then builds the production firmware, selects the
connected serial port, flashes the image, and resets the device.

If PlatformIO cannot enter download mode:

1. Hold the `G0` button on the Cardputer-Adv.
2. Press and release the reset button.
3. Release `G0`.
4. Run `make upload` again.

## 6. Verify the Installation

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

Exit the serial monitor with `Ctrl+]`. Continue with the
[Device Guide](device-guide.md) for supported controls and current limitations.

## Troubleshooting

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
