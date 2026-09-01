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

For the first Cardputer Hub installation on a device, or when upgrading a
device that previously ran a Cardputer Hub release with no dedicated
`hub_config` partition, run the one-time storage-layout migration:

```bash
make migrate-storage-layout UPLOAD_PORT=/dev/ttyACM0
```

Replace `/dev/ttyACM0` with the exact device path reported in step 4. The port
is mandatory so both the upload and destructive erase target the same physical
Cardputer; the migration refuses to auto-detect a port.

This builds and uploads the firmware and partition table, then erases only the
new configuration partition's range (`0x7e0000-0x7effff`). That range belonged
to SPIFFS in the previous layout, so it must be provisioned before NVS can use
it. No Cardputer Hub configuration existed there under the previous layout.

Do not use this migration target for routine upgrades. It intentionally clears
`hub_config` and would remove configuration already stored there. Once the
device has the current layout, use the non-destructive upload path:

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
4. Run the selected installation command again.

### Updating with Published Release Assets

Each GitHub Release contains a versioned application image, its matching
partition-table image, and `SHA256SUMS`:

```text
cardputer-hub-v0.2.0.bin
cardputer-hub-v0.2.0-partitions.bin
SHA256SUMS
```

Verify both downloads against `SHA256SUMS`. For a device that already has the
`hub_config` layout, flash both images without erasing data partitions (replace
the example port and version with the downloaded release):

```bash
uv run --frozen pio pkg exec --package tool-esptoolpy -- \
  esptool.py --chip esp32s3 --port /dev/ttyACM0 write_flash \
  0x8000 cardputer-hub-v0.2.0-partitions.bin \
  0x10000 cardputer-hub-v0.2.0.bin
```

When upgrading from the earlier layout, provision the range exactly once
before flashing those two assets:

```bash
uv run --frozen pio pkg exec --package tool-esptoolpy -- \
  esptool.py --chip esp32s3 --port /dev/ttyACM0 erase_region \
  0x7e0000 0x10000
```

The release images are a matched pair. Never install a new application image
while leaving an older partition table on the device.

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
