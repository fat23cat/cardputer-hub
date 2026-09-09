# Cardputer Hub Device Guide

This guide describes the currently implemented device behavior. Cardputer Hub
has entered its Phase 2 Connectivity work, but those foundations are not yet
composed into user-visible features.

## Supported Features

On startup, the firmware:

1. initializes the Cardputer-Adv once;
2. starts one USB composite device with serial diagnostics and an inactive HID
   interface;
3. writes the firmware name, version, commit, and build type to the serial log;
4. clears the display to black;
5. shows `Cardputer Hub` and the firmware version in white;
6. enters a non-blocking update loop and polls the keyboard.

The firmware recognizes printable key presses, Tab, Enter, Backspace, Delete,
Escape, arrow keys, F1 through F12, and the active Shift, Ctrl, Alt, Option, and
Fn modifiers. These events are internal System Core input data at this stage.

## Controls and Key Combinations

No keyboard shortcut or key combination currently triggers a user-visible
Cardputer Hub action. Pressed keys are not shown or written to the serial log.

| Control | Current behavior |
| --- | --- |
| Keyboard keys and modifiers | Detected internally; no visible action is assigned |
| Reset button | Restarts the firmware and redraws the boot screen |
| `G0` plus reset | Enters download mode for firmware installation |

This table must be updated whenever a shortcut, global control, navigation key,
or Mini App control becomes available.

## View Startup Diagnostics

Connect the Cardputer-Adv over USB and run:

```bash
make monitor
```

If more than one serial device is present, pass the application CDC path:

```bash
make monitor UPLOAD_PORT=/dev/ttyACM1
```

The application CDC path can differ from the ROM download-mode path used to
flash firmware. List ports again after the application starts when selection
is ambiguous.

The serial monitor uses 115200 baud. Press reset if startup records have already
scrolled past. Exit with `Ctrl+]`.

The firmware logs build metadata but does not log keyboard characters,
credentials, or other user data.

The HID interface is transport infrastructure only. Normal keyboard input and
Actions are not routed to the connected host yet.

## Current Limitations

The current firmware does not yet provide:

* Launcher, navigation, or Mini Apps;
* user-facing Actions or keyboard mappings;
* user-facing Wi-Fi or Bluetooth connectivity;
* configuration or persistence;
* host profiles or host switching;
* weather, VPS, Telegram, media, or RGB indicator features.

The internal Action model and Action Bus are infrastructure for later features;
they do not change the current device controls.
