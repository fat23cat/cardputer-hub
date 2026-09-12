# Cardputer Hub Device Guide

Normal firmware opens **Home** after a two-second segmented startup screen. The
startup screen keeps the embedded firmware version visible while its progress
bar fills; it does not pause Bluetooth, other background work, or input polling.
Input sampled as the startup screen hands off to Home is consumed. Home
shows the selected host in compact Micro 5 text and live BT status, with a slow
dotted wave below. The top line has time, Wi-Fi status, and estimated battery
percentage, with no battery icon. Time currently shows `--:--`; Wi-Fi shows `OFFLINE` until Wi-Fi
setup is implemented. Battery is sampled every five seconds (`--%` if
unavailable); the voltage-based estimate can be less accurate with USB power
connected. Long host names are shortened with an ellipsis on Home only; their
stored names remain unchanged. Press **Tab** on the main keyboard to open
the general Settings menu (**Fn+Tab** and **G0** still work), then **Enter** on Bluetooth to open the existing
Bluetooth panel. BLE is the only host-control transport. USB supplies power, firmware
installation, and fixed USB Serial/JTAG diagnostics.

## Hosts and Bluetooth

The Bluetooth panel lists Bluetooth On/Off, Add device, and saved hosts. SELECTED
marks the saved choice, even when BLE is Off or activation fails; it does not
mean that a connection exists. The upper-right status separately shows OFF, CONNECTING,
SECURING, READY, or ERROR. READY means the selected host has established the
secured HID connection and both report subscriptions.

On the first startup with no host settings, BLE is Off. Existing Cardputer bond
records are imported as `Host N` without deleting or re-pairing them. Select a
host to open its menu, then choose Connect to enable BLE for it. The same
menu provides Rename and Delete. Pressing Enter on Bluetooth
before selecting a host moves the highlight to a saved host with “Select host,
then Enter”. Choose the intended host with `;` / `.` and open its menu with Enter, then choose Connect.
If there are no saved hosts, it highlights Add device with “Add device, then
Enter”; a second Enter opens pairing. Until you confirm, BLE stays OFF and
Home does not show an error. Names are 1–24 printable
ASCII characters and cannot consist only of spaces.

## Controls

| Control | Behavior |
| --- | --- |
| Tab on Home (also Fn+Tab or G0) | Open Settings without changing BLE state |
| Enter on Bluetooth in Settings | Open the Bluetooth panel |
| Backtick/Escape in Settings | Return Home |
| Tab in the Bluetooth list (also Fn+Tab or G0) | Return to Settings |
| `;` / `.` (keys marked Up / Down, without Fn) | Move through the list; Fn+arrow combinations also work |
| Enter on Bluetooth | Turn BLE On/Off; without a selected host, highlight a host or Add device for confirmation |
| Enter on Add device | Open the two-minute pairing window |
| Enter on a saved host | Open its Connect / Rename / Delete menu without changing BLE |
| Connect in a host menu | Save that selection and enable BLE for it |
| Rename in a host menu | Edit that host’s display name |
| Delete in a host menu | Open confirmation for deleting only that host and its pairing |
| Enter on deletion confirmation | Delete the named host; Backtick/Escape cancels |
| R on a saved host | Edit its display name; Backspace deletes a character |
| Enter while editing | Save the name |
| Backtick (without Fn), or Escape (`Fn+backtick`), in the list | Return Home without changing BLE state |
| Escape while renaming | Cancel the edit |
| Backtick or Escape during pairing | Cancel pairing and restore the previous selected host/On-Off setting |
| Reset | Restart and restore saved host settings |
| G0 plus Reset | Enter firmware download mode |

In the host list, the keys marked with Up/Down arrows work without holding Fn.
While editing a host name, `;` and `.` enter punctuation normally. Pairing code
entry still accepts digits only. Moving focus updates only the changed rows;
scrolling updates the visible list without clearing the whole screen.

Back from rename/delete returns to the host menu; Back from the host menu
returns to the Bluetooth list. Back from pairing returns to the Bluetooth list.
Settings and the Bluetooth list have no bottom bar or Esc Home label.
Escape/backtick still returns Home; arrows and Enter work as before. The X shortcut and global cleanup button are removed. Backtick remains a printable character while renaming (use
Escape to cancel that edit). Home is always the default screen after reboot. Ordinary Enter and B do
nothing on Home. Tab/G0/Fn+Tab does not dismiss a host submenu, rename/delete prompt,
or pairing view; leave that view using Back first. The existing Esc Home action
in the Bluetooth list continues to return directly Home;
opening/closing settings does not change saved host selection or BLE On/Off.

A normal G0 press opens Settings once; holding it does not repeat. Holding G0
while starting or resetting the device still enters the firmware download mode.
The wave has a 28-second cycle and pauses while Settings/Bluetooth is open and
during screen transitions. Screens slide in from the right when opening and
from the left when returning, taking about 220 ms. You can keep pressing keys
during a transition; navigation does not wait for the animation to finish.
Moving between rows and live Bluetooth status updates do not slide the screen.

These keys operate Cardputer locally. The current settings screen does not
forward typing, shortcuts, or media commands to the computer. Firmware Services
can persist bounded platform, capability, and mapping-template identifiers for
each host, but this metadata has no editing UI and does not enable a mapping or
host command by itself.

## Add a Computer

1. Choose Add device on Cardputer.
2. Open Bluetooth settings on the computer and pair with `Cardputer Hub`.
3. Follow the code prompt on Cardputer: enter its displayed code on the computer,
   or type the computer's six-digit code on Cardputer and press Enter. For a
   comparison prompt, press Enter only if both codes match; Escape cancels.
4. After authenticated pairing succeeds, the new `Host N` profile is saved and
   selected. Wait for READY, then rename the profile if desired.

If the computer disconnects before pairing completes, the old code disappears
and Cardputer returns to discovery within the original two-minute window.
Follow the fresh prompt when you connect again.

Pairing cancellation or timeout restores the previous selection and BLE setting.
Normal switching does not require pairing again. Up to 16 pairs are supported.
Delete removes the named profile and its Cardputer pairing. Deleting the
selected host turns BLE Off and clears selection without choosing another host.
Deleting another host preserves the current connection and other profiles.

## Switch or Disconnect

Open a saved host and choose Connect. Cardputer releases held reports, closes the
old connection, saves the new selection, and permits only the selected host to
connect. An unavailable host does not cause a different laptop to take over.
The selection and BLE On/Off setting survive reboot.

To keep Cardputer disconnected, select the **Bluetooth** row and press Enter
until it shows **OFF**. Back returns Home and does not turn the radio off. To reconnect, turn it On or select a saved host. macOS may automatically
reconnect after its own Disconnect command while Cardputer remains On; use
Cardputer's Off setting for a persistent disconnect.

A missing pair or settings/backend error stops BLE and displays an error instead
of silently erasing data or choosing another host. A missing pair requires
pairing repair; there is no automatic NVS reset. After a transient startup error,
retry the Bluetooth toggle, a saved host’s Connect action, or Add device. These
actions retry initialization without requiring a reboot. A continuing failure
keeps its error visible and leaves stored profiles intact.

## Repair a Pairing

Add device is for new pairs. A computer still bonded on Cardputer is rejected
in that window, even if the computer itself has forgotten Cardputer.

1. On the computer, forget Cardputer Hub if it is still listed as paired.
2. On Cardputer, cancel Add device with Backtick/Escape if it is open.
3. Open the affected saved host, choose **Delete**, and confirm with **Enter**.
   Backtick/Escape cancels. Only this host and its pairing are deleted.
4. Return to the Bluetooth list and choose **Add device**, then pair on the
   computer and follow the code prompt. Wait for READY.

Other hosts and their pairings remain intact. Failed deletion displays an error
and can be retried, including when the pairing record is already missing.

## Diagnostics and USB

With the ESP-IDF environment active, run:

```bash
make monitor
# Select the fixed USB Serial/JTAG port explicitly when several devices exist:
make monitor UPLOAD_PORT=/dev/cu.usbmodemXXXX
```

Replace the example port with the actual device port. The console uses 115200
baud; exit with `Ctrl+]`. Reset prints firmware name, version, commit, and build
type again. Logs exclude keyboard text, pairing codes, and host identity data.

Attaching/removing USB does not select a host, turn BLE On/Off, or erase pairs.
No USB HID keyboard is exposed. USB serial hotplug behavior and final BLE
hardware acceptance are tracked separately in [plan 017](../plans/017-hid-transport-arbitration.md).
The opt-in [BLE validation harness](../validation/plan-015-device-harness.md)
provides diagnostic commands; those commands are not available in normal firmware.

## Current Limitations

Fresh pairing, two-computer addition/switching, and reconnection to the last
selected host after Reset/power-on were confirmed on Cardputer-Adv. Extended
Off, report/interruption and USB hotplug acceptance remains tracked in
[plan 017](../plans/017-hid-transport-arbitration.md#0-current-closeout-status).
The current firmware does not
include the full Launcher/Mini App shell, profile-metadata editing or template
resolution, Action-to-HID mappings, a Mac companion CLI/control protocol, Wi-Fi setup, or
weather/VPS/Telegram/RGB features. Sound, idle dimming, and wake-input behavior
from the broader UI requirements remain planned.
