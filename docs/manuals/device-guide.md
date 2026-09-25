# Cardputer Hub Device Guide

Normal firmware opens **Home** after a two-second segmented startup screen. The
startup screen keeps the embedded firmware version visible while its progress
bar fills; it does not pause Bluetooth, other background work, or input polling.
Input sampled as the startup screen hands off to Home is consumed. Home
shows a compact device-status bar above a gently moving 40-particle Living Orb.
The fixed WiFi and BT groups use a dot and label. WiFi is hollow when no network
is saved, pale when off, blue while connecting, green when connected, and red
on error. BT is hollow when off, blue while connecting or pairing, green when
ready, and red on error. The right-aligned
battery percentage has no icon; it shows `--%` when unavailable. Battery is
sampled every five seconds, and the voltage-based estimate can be less
accurate with USB power connected. A row below the Orb shows a round green dot
and the active Bluetooth host name only while the host is ready and Cardputer
Companion is connected. The row stays empty otherwise. Home shows no clock or
connection-state text. The orb pauses while Home is hidden, the display is Off,
or a page is sliding.

Home has **APPS** and **SETTINGS** actions at the bottom. **APPS** is selected
each time Home opens. Press **Left** or **Right** to select an action, then
**Enter** to open it. Plain **Tab** opens Settings directly. The
list currently contains **SYSTEM**, a read-only status screen for battery,
Bluetooth, the selected host, Wi-Fi, and firmware version; **POMODORO**, a
background focus timer; **LED GALLERY**, an 8×8 matrix animation app; and **MAC CONTROL** when a live Cardputer Companion
session is ready. **MAC STATUS** appears when that Companion supports system
metrics. MAC CONTROL is a full-screen
3×2 grid. Press the matching number to launch or focus that Mac app; production
firmware binds **1** to Telegram. Empty numbered tiles do nothing. Left and
Right move between pages when more than one page exists. A bound press expands
that tile in blue while the Mac opens the app, then flashes green on success or
red if the app is not found, and returns to the grid. Escape returns from
SYSTEM, POMODORO, LED GALLERY, MAC CONTROL, or MAC STATUS to Apps and from Apps to Home. If Companion disappears
while MAC CONTROL is open, the app closes and Apps returns; reconnect does not
reopen it or repeat the last launch. Press plain **Tab** on the main keyboard
to open the general Settings menu. Fn+Tab is inactive, and a normal G0 press
has no application action. Settings lists Bluetooth, Wi-Fi, Sound volume, Screen
timeout, Screen brightness, and LED brightness.
Selection changes immediately between rows in Apps, Settings, Bluetooth, and
Wi-Fi. Host-list scrolling keeps the selected row visible.
BLE is the only host-control transport. USB supplies power, firmware
installation, and fixed USB Serial/JTAG diagnostics.

Each recognized key press after startup has a short, soft synthesized click.
The click cycles through subtle deterministic variants rather than playing a
recorded sound. The default volume is 60%; 0% mutes it completely, and the
setting survives Reset and power cycles. Rapid presses are coalesced into one
uninterrupted cue instead of building up an audio queue.

Screen timeout defaults to Normal: after 15 seconds without a key press the
backlight dims to 10% of the selected Screen brightness, then turns off after
two minutes at the dim level. Long waits 60 seconds before dimming and five
minutes before turning off. Never keeps the display awake. Screen brightness
can be set from 20% to 100% in 10% steps; the default is 100%. A key press
while dimmed or off wakes the display and is not
delivered as a command. Background work keeps running while the screen is
dark. A Pomodoro phase change also wakes the display without switching the
active app; the normal idle dim/off cycle then starts again.

## Pomodoro

**POMODORO** is always available in Apps. Space starts, pauses, and resumes.
`R` resets. Right, `S`, or `/` skip to the next phase. The timer continues
after you leave the app: 25-minute focus, 5-minute short breaks, and a
15-minute long break after every fourth focus. Phase changes play a sound.
If a Unit Puzzle LED matrix is attached, it shows a soft steel-blue glow
for focus and a muted sage for breaks, at a low brightness. When a phase finishes, the display wakes so
the current screen is visible again; Pomodoro does not steal focus from
another open app.

## LED Gallery

**LED GALLERY** is always available in Apps, including without a Unit Puzzle
matrix. With the matrix attached, it runs one of twenty animations continuously:
Plasma, Lava, Kaleidoscope, Aurora, Warp, Comets, Fireflies, Vortex,
Ripple, Particle Storm, Game of Life, Reaction Diffusion, Fire, Gravity Well,
Swarm, Falling Sand, Langton's Ant, Tetris Dream, Rule Machine, and Electric Storm.
Falling Sand fills the matrix, fades when it is full, then begins again.
Right selects the next effect; Left selects the previous one, wrapping at the
ends. `1`–`9` select effects 1–9, `0` selects effect 10, and Fn with the same
digits selects effects 11–20. The LCD shows `NN/20`, the global navigation row
at the bottom, and effect-specific controls above it only when available. Space
triggers the shown primary action. WASD and G have effect-specific meanings shown on the LCD;
other printable keys, including R/r, add smaller bursts in Particle Storm.
Escape returns
to Apps. The last selected effect is restored when reopening the app during this
firmware session; after reboot it starts with Plasma. Effects never switch on
their own. The LCD can dim and turn off while the LED animation continues. The
matrix uses the global LED brightness setting, 1% to 10% in 1% steps (default
3%). The shared output path limits every app to 10%. If Pomodoro is active, its
LED progress returns when you close Gallery.

## Hosts and Bluetooth

The Bluetooth list and saved-host action menu have no navigation footer.
Rename, Delete, and pairing show a quiet `ESC CANCEL` hint; confirm actions
appear only when they are valid (`ENTER APPLY`, `ENTER YES`, or `ENTER DELETE`).
Escape still goes back or cancels; Enter still confirms.

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

## Cardputer Companion

The macOS **Cardputer Companion.app** is optional. Build and launch it from
`companion/macos` as described in that directory's README. Grant Bluetooth
permission on first launch. The
Companion attaches to the already-paired Cardputer; it does not scan or create
a second pairing. The menu-bar popover shows connection status, protocol, and
last valid message; it also provides Reconnect, Start at Login, Diagnostics,
About, and Quit. Start at Login can be enabled or disabled in the popover.
Closing the Mac lid, sleep,
or a BLE drop invalidates the session; after wake and HID reconnect it attaches
again without relaunching Companion or re-pairing.
MAC CONTROL becomes available in Apps only while that session is live. Press **1** to focus Telegram if it is already running, or to launch it
if it is closed. Keyboard and consumer HID keep working if the Companion is
missing, crashed, or disconnected.
After Companion closes, its host-name row on Home disappears when the session
is detected as unavailable, even if Bluetooth HID remains connected.

MAC STATUS shows CPU, physical memory used/total, memory pressure, root-volume
storage usage, Mac battery, download/upload rates, and thermal state. The
memory value is an estimate that counts compressed and inactive app memory but
excludes free memory and file-backed cache.
The dashboard occupies the full screen without a title or connection indicator.
It polls roughly once per second only while open. Individual unavailable
metrics and values older than three seconds show `--`. CPU and network rates
may initially show `--` while the Companion establishes counter baselines.
Press Escape to leave; the other keys do not control the dashboard. A v1
Companion still supports MAC CONTROL but does not expose MAC STATUS. Companion
loss closes MAC STATUS and reconnect does not reopen it automatically.

## Wi-Fi

Open **Wi-Fi** from Settings to see the current status, turn the saved network
on or off, and enter credentials by hand. Scanning nearby networks is not
included.

If no network is saved, **Configure network** is already focused. Press Enter,
type the network name, press Enter, then type the password. The last typed
character is shown for about two seconds, then becomes an asterisk. An
empty password is allowed for an open network. The unmarked Escape key
(backtick) or Fn+backtick cancels without saving.
After a successful save, drafts are cleared. If Wi-Fi was already off, it stays
off until you turn it on.

When a network is saved, the first row toggles Wi-Fi On/Off. **Change network**
uses the same name/password editors and replaces the saved network without
forgetting it first. **Forget network** asks for confirmation; the unmarked
Escape key (backtick) or Fn+backtick keeps the saved network. Name, password,
and Forget screens show `ESC CANCEL` with `ENTER NEXT` once a name is typed,
`ENTER CONNECT`, or `ENTER FORGET`. Signal strength appears only while connected.

Home uses the WiFi label and status dot only. A red dot means the connection failed;
open Wi-Fi Settings for a short domain message such as a save or connection
failure. Passwords are never shown after entry and are not written to logs.

While the name or password editor is open, `;` and `.` type those characters
instead of moving a list.

## Controls

| Control | Behavior |
| --- | --- |
| Left / Right on Home | Select APPS / SETTINGS |
| Enter on Home | Open the selected action; APPS is selected by default |
| Escape in Apps | Return Home |
| Enter on SYSTEM | Open the read-only system status list |
| Escape in SYSTEM | Return to Apps |
| Escape in POMODORO | Return to Apps; the timer keeps running |
| Escape in LED GALLERY | Return to Apps; Pomodoro LED progress returns if active |
| Left/Right, 1–0, Fn+1–0 in LED GALLERY | Select one of twenty effects |
| Space in LED GALLERY | Trigger the current effect's primary action shown on the LCD |
| Space in POMODORO | Start, pause, or resume |
| R in POMODORO | Reset the timer |
| Right, S, or / in POMODORO | Skip to the next phase |
| Plain Tab on Home | Open Settings without changing BLE state |
| Enter on Bluetooth in Settings | Open the Bluetooth panel |
| Enter on Wi-Fi in Settings | Open Wi-Fi Settings |
| `;` / `.` or Up / Down in Settings | Move among all six rows |
| `,` / `/` (keys marked Left / Right, without Fn) on Sound volume | Decrease / increase volume by 10%, from 0% to 100%; Fn+arrow combinations also work |
| Left / Right on Screen timeout | Select Normal, Long, or Never without wrapping |
| Left / Right on Screen brightness | Change by 10% within 20%–100% |
| Left / Right on LED brightness | Change by 1% within 1%–10% |
| Backtick/Escape in Settings | Return Home |
| Plain Tab in the Bluetooth list or Wi-Fi Settings | Return to Settings |
| Backtick/Escape on the Wi-Fi page, name, password, or Forget confirmation | Return to Wi-Fi Settings or cancel without saving |
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
Escape to cancel that edit). Home is always the default screen after reboot. Enter opens
the selected Home action. Plain Tab does not dismiss a host submenu, rename/delete prompt,
or pairing view; leave that view using Back first. The existing Esc Home action
in the Bluetooth list continues to return directly Home;
opening/closing settings does not change saved host selection or BLE On/Off.

Fn+Tab is inactive, and a normal G0 press does nothing. Holding G0 while
starting or resetting the device still enters the firmware download mode.
The Living Orb pauses while Settings, Bluetooth, Wi-Fi, Apps,
or SYSTEM is open and during screen transitions. Screens slide in from the right when opening and
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
Normal firmware has no opt-in validation-harness build or diagnostic command
shell.

## Current Limitations

Fresh pairing, two-computer addition/switching, and reconnection to the last
selected host after Reset/power-on were confirmed on Cardputer-Adv. Extended
Off, report/interruption and USB hotplug acceptance remains tracked in
[plan 017](../plans/017-hid-transport-arbitration.md#0-current-closeout-status).
The current firmware includes Apps, SYSTEM, POMODORO, LED GALLERY, MAC CONTROL when
Companion is ready, and MAC STATUS when system telemetry is available. It does not include profile-metadata editing or template
resolution, Action-to-HID mappings, a Mac companion CLI/control protocol,
Wi-Fi network scanning, or weather/VPS/Telegram features. Boot/status sound
cues from the broader UI requirements remain planned. Unit Puzzle LED Gallery
requires physical acceptance and tuning on the actual matrix.
