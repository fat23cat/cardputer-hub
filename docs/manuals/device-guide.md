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
Bluetooth, the selected host, Wi-Fi, and the firmware build (date and
commit, as `BUILD`); **POMODORO**, a
background focus timer; **LED GALLERY**, an 8×8 matrix animation app; **NFC**, an inventory of NFC-tagged boxes and bags, when an
M5Stack Unit NFC is connected; and **MAC CONTROL** when a live Cardputer Companion
session is ready. **MAC STATUS** and **AI** appear with that session too. AI's
**USAGE** page shows automatically discovered Codex, Cursor and Claude account
quota; **STATUS** shows whether their desktop agents are working. MAC CONTROL is a full-screen
3×2 grid. Press the matching number to launch or focus that Mac app; production
firmware binds **1** to Telegram. Empty numbered tiles do nothing. Left and
Right move between pages when more than one page exists. A bound press expands
that tile in blue while the Mac opens the app, then flashes green on success or
red if the app is not found, and returns to the grid. Escape returns from
SYSTEM, POMODORO, LED GALLERY, NFC, MAC CONTROL, MAC STATUS, or either main page
of AI to Apps, and from Apps to Home. In AI's usage details, Escape first returns
to USAGE. If Companion disappears
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

## NFC

**NFC** keeps an inventory of boxes, bags and suitcases. A cheap writable NFC
sticker on each container carries only a random ID; the container's name and
description (what is inside) are stored on the Cardputer's **microSD card** and
edited in the
**Inventory** window of Cardputer Companion on the Mac. Reading a container
needs neither the Mac, Wi-Fi nor the Internet.

It needs the **M5Stack Unit NFC** (ST25R3916, SKU U216) plugged into the
Cardputer-Adv **Grove port A** with its cable **before the device is powered on
or reset**. Without the unit the app is listed as unavailable
(`REQUIRES NFC_READER`) and nothing changes elsewhere. If the unit is pulled out
while NFC is open, the app closes and Apps returns; plugging it back in makes
NFC available again without a reset, but a unit that was not connected at
power-on is not looked for later. The Unit NFC and the **Unit Puzzle** LED
matrix use the same Grove port, so connect only one of them. When a Unit NFC
answers at startup, the Puzzle output is switched off for that session.

**Tags.** Use writable, unlocked **NTAG213**, NTAG215 or NTAG216 stickers
(NFC Forum Type 2). Other cards and tags, including MIFARE Classic transport
cards, are shown as `UNSUPPORTED TAG` and never read or written. A sticker that
already holds other data (for example a web link) shows `TAG HOLDS OTHER DATA`
and is not overwritten during registration; erasing it requires Fn+Del and
confirmation. A locked sticker shows `TAG IS LOCKED`.

**microSD.** Insert a FAT-formatted card. The app mounts it when NFC opens and
shows `NO SD` in the header while no card is mounted. Records are kept under
`cardputer-hub/inventory/records/` on the card, one file per container named by
its ID. The firmware never formats or repairs the card. Do not edit these files
by hand: the Cardputer refuses a damaged record (`RECORD IS DAMAGED`) and leaves
it unchanged.

**Register a container.** Open **NFC** and press **Enter** on `TAP A TAG`
(`ENTER  NEW`), or hold a blank sticker to the antenna and press **Enter** on
`BLANK TAG`. Type a short name of up to 32 letters, digits or symbols (the
Cardputer keyboard types Latin text; a Cyrillic name and the description are
written on the Mac) and press **Enter**, or Escape to cancel. You may put the
sticker away while typing. `TAP TAG TO WRITE` then waits for any blank sticker:
hold one to the reader and keep it there while the app writes the ID
(`WRITING TAG`), reads it back (`CHECKING TAG`) and only then saves the record
(`SAVING RECORD`); the container opens with `SAVED`. A sticker that is not blank
is refused (`THIS TAG IS NOT BLANK`) and nothing is written to it; present
another or press Escape to stop waiting. Registration needs a mounted microSD
card (`INSERT MICROSD TO SAVE` otherwise); at most 256 containers fit
(`INVENTORY IS FULL`). If the sticker leaves during the write,
`TAG NOT WRITTEN, TAP AGAIN` keeps the name waiting and nothing is saved; tap
the sticker again. If the ID was written but the record could not be saved,
`RECORD NOT SAVED` offers **Enter** to save it again, and a later tap of that
sticker shows `NO RECORD FOR THIS TAG` with **Enter** to create the record for
the same ID; the sticker need not stay on the reader while you type that name.

**Read a container.** Hold its sticker to the reader. The name appears at the
top and the description below, line by line with long lines wrapped; Cyrillic
text is shown. With more than seven lines, Up/Down (`;` / `.`) or Left/Right
(`,` / `/`) change pages, and the page counter shows where you are. Holding
the sticker never writes anything. Removing it leaves the container visible so
you can read and page through its contents. Press **Escape** to close it, or
present another sticker to replace it after that sticker is read. Erasing needs
the original sticker to be on the reader. `MICROSD UNAVAILABLE` means the card is missing or failed:
insert it and press **Enter** to retry. The RF field is on only while NFC is
open.

**Erase a sticker.** With an inventory sticker on the reader (a known
container, `NO RECORD FOR THIS TAG` or `RECORD IS DAMAGED`), press **Fn+Del**.
`ERASE THIS TAG?` asks first; **Enter** erases, Escape cancels. The app empties
the sticker and checks it, then deletes the container's record from the
microSD card for good; the sticker is blank again and can be registered for
another box. If the sticker was removed, **Enter** waits for that same sticker
to return. If a different sticker is present, remove it and bring back the
original before pressing **Enter**; the warning disables confirmation. While a
returned sticker is being read, wait for the check to finish before pressing
**Enter**. `TAG DATA CHANGED` means the same sticker's contents no longer match
what was selected; cancel and inspect it again before starting a new erase. If
erasure cannot be verified, `ERASE UNCONFIRMED` keeps the record on microSD.
If the sticker stays on the reader and is then read as blank, its record is
deleted. After removing the sticker, check whether it is blank; if so, delete
the orphaned record in the Companion. A later tap does not delete the record
automatically. If the record could not be deleted after a verified erase,
`RECORD NOT DELETED` says so; delete it in the Companion. Writable stickers
with unrelated NDEF data can also be erased after confirmation, without
deleting any inventory record. Locked or reserved areas are not erased.

**Edit on the Mac.** With Cardputer Companion connected, choose **Inventory**
in its menu (see [Inventory on the Mac](#inventory-on-the-mac)). Saved edits appear
on the Cardputer the next time the sticker is tapped, or at once if it is on
the reader.

The ID on a sticker is a locator, not a secret: copying it to another sticker
makes both open the same record. Physical acceptance of NFC writing, microSD
recovery and the Cyrillic display on a Unit NFC is pending
([plan 045](../plans/045-nfc-inventory.md#current-status)).

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
a second pairing. The menu-bar menu shows connection status, the Cardputer
firmware build and the last valid message; it also provides Reconnect, Start at
Login, Inventory, Diagnostics, About, and Quit. Start at Login can be enabled or disabled in the menu.
Closing the Mac lid, sleep,
or a BLE drop invalidates the session; after wake and HID reconnect it attaches
again without relaunching Companion or re-pairing.
MAC CONTROL becomes available in Apps only while that session is live. Press **1** to focus Telegram if it is already running, or to launch it
if it is closed. Keyboard and consumer HID keep working if the Companion is
missing, crashed, or disconnected.
After Companion closes, its host-name row on Home disappears when the session
is detected as unavailable, even if Bluetooth HID remains connected.

Update the firmware and the Companion together, from the same checkout. Both
carry a build ID — the build date and commit, for example
`2026-09-29 abc1234` (`+` after the commit means uncommitted changes) — shown
as `BUILD` in SYSTEM and under **Builds** in the Companion menu. When the two
were built from different protocol definitions, they do not connect: Home
shows a red dot with `UPDATE COMPANION`, `UPDATE FIRMWARE` or `REBUILD BOTH`,
and the Companion menu names the side to update. The side with the older build
date is the one to update; with equal dates, rebuild both. A Companion from
before this change always shows `UPDATE COMPANION`. After updating, the
Companion reconnects on its own after a relaunch; use **Reconnect** if it was
already running. If the Cardputer does not answer at all, the menu says so and
keeps retrying.

### Inventory on the Mac

Choose **Inventory** (⌘I while the menu is open) in the Companion menu to open
the Inventory window. The left column lists the containers registered on the
connected Cardputer, read from its microSD card; `Damaged record` marks a file
the Cardputer refuses. Select a container to load it, then edit its **name**
(up to 32 characters) and its **description** — free text with line breaks of
up to 900 characters, for example one thing per line or a comma-separated list
— with normal macOS text input, including Cyrillic. The window counts the
characters as you type. Press **Save** (⌘S) to send the record; the Cardputer
checks it and saves it as the next revision, and the window shows `Saved`.
Spaces at the ends of lines and blank lines at the start or end are removed
when saving. **Reload** fetches the current record and discards unsaved edits.
Switching containers or closing the window with unsaved edits asks first.

**Delete…** removes the selected container's record from the Cardputer for
good, after a confirmation; a damaged record can be deleted the same way. The
sticker keeps its ID: tapped later, it shows `NO RECORD FOR THIS TAG`, where it
can be erased or given a new record.

Editing needs a live Companion session and a mounted microSD card on the
Cardputer; otherwise the window says so and disables the editor. A fresh list
and record load are required before editing resumes. An unsaved draft stays in
the window during reconnection. Nothing is stored as an authoritative Mac copy: a second
Mac sees the same records once it connects to the Cardputer. Each new
connection reads the selected record again, and a record that is no longer on
the Cardputer is closed. If the record changed on the Cardputer since you
loaded it (for example from the other Mac), **Save** and **Delete…** report
the change and nothing is overwritten; **Reload** to continue. If the
connection drops while saving, the edit stays in the window unsaved and is
never sent again on its own; save it again after reconnecting or reload to see
what the Cardputer kept. Physical acceptance of transfer speed and of two Macs
is pending.

MAC STATUS shows CPU, physical memory used/total, how much of the startup disk
is used (`DISK 63% USED`), Mac battery, download/upload rates, whether memory
is sufficient (`MEMORY OK`, `TIGHT` or `CRITICAL`, from macOS memory pressure)
and how hot the Mac is (`TEMP OK`, `WARM`, `HOT` or `CRITICAL`, from the macOS
thermal state; green is fine, red needs attention). The
memory value is an estimate that counts compressed and inactive app memory but
excludes free memory and file-backed cache. Under the CPU value a line shows
the last 60 seconds; it grows from the right after you open the app and breaks
only where the Mac did not answer.
The dashboard occupies the full screen without a title or connection indicator.
It polls roughly once per second only while open. Individual unavailable
metrics and values older than three seconds show `--`. CPU and network rates
may initially show `--` while the Companion establishes counter baselines.

The battery also shows the time to
full charge while charging (with a green dot), the time left on battery, or
`AC` when plugged in and not charging. Five dots at the bottom mean four more
pages are available. Press Right (`/`) or Left (`,`), with or without Fn, to
move between them; the pages wrap around, and MAC STATUS always opens on the
overview:

* **CPU / TOP APPS** — CPU with a 60-second line; `CORES` splits the load
  between the fast performance cores (`FAST`) and the energy-efficient cores
  (`EFF`) that run background work; `GPU` is graphics-chip load; below are the
  four apps using the most CPU; `APPS IDLE` means no app uses at least 1% of the
  Mac. An app's helper processes count toward the app. The list uses each
  app's average over about ten seconds, and apps with nearly equal use keep
  their places, so the order changes only when one app is clearly busier.
  Only your own processes are listed; system processes owned by other users
  are not.
* **POWER** — battery percentage and segment meter, charging state with
  `FULL IN` or `EMPTY IN` time, `POWER USE` (what the whole Mac draws right
  now, in watts), `CHARGER` (the connected charger's rating), `BATTERY HEALTH`
  (capacity left compared with a new battery), `CHARGE CYCLES`, and the
  lowest-charged Apple mouse, keyboard or trackpad (`LOW` at 20% or less). A
  Mac without a battery shows `NO BATTERY`.
* **NETWORK** — download and upload with 60-second lines; `INTERNET PING` and
  `ROUTER PING` are round-trip times to `1.1.1.1` and to your router (a slow
  internet ping with a fast router ping points at the provider, both slow at
  Wi-Fi); `WI-FI SIGNAL` is `STRONG`, `GOOD`, `WEAK` or `VERY WEAK`;
  `WI-FI SPEED` is the current link rate in Mbit/s. The Wi-Fi network name is
  not read, so macOS does not ask for Location access.
* **MEMORY / DISK** — RAM in use and a bar split into `APPS` (memory your
  apps use), `MACOS` (memory the system keeps for itself and cannot move) and
  `COMPRESSED` (app memory macOS squeezed to make room); `SWAPPED TO DISK` is
  memory moved to the SSD because RAM was full; `DISK FREE` is free space on
  the startup disk in decimal GB as Finder shows it; `DISK` shows current read
  and write speed in MB/s.

A detail page updates about every two seconds; its values show `--` for the
first two to four seconds after it opens, and again when they are more than
six seconds old. The Mac
measures the network only while the NETWORK page is open.
Press Escape to leave. Companion
loss closes MAC STATUS and reconnect does not reopen it automatically.

## AI

Open **AI** in Apps. The top strip shows **STATUS** and **USAGE**; press
Left/Right (the `,` / `/` keys also work without Fn) to cycle between the pages
with a short slide in the direction you press. The first visit
opens STATUS, and later visits remember your last page until a restart. All
account and agent information remains visible below the strip. On USAGE, a red
`!` beside STATUS means an agent needs attention; switch to STATUS to see which
one. This mark uses fresh agent data only. Status checks continue while AI is
open on either page or in details. Unit Puzzle follows the page you are viewing.
Escape on a main page closes AI. Companion loss closes AI and clears its data;
reconnecting does not reopen it.

### USAGE

USAGE has no setup screen. The Companion checks Codex, the existing
Cursor Agent sign-in and the existing Claude Code sign-in on that Mac; an absent
provider is omitted. Plus accounts show 5-hour and weekly limits, Business shows
credits, Cursor Enterprise shows personal spend, and Claude Pro or Max shows its
5-hour and weekly limits. The first time, macOS asks whether Cardputer Companion may
use `Claude Code-credentials`; choose **Always Allow**. Until you answer,
Claude is not shown, and with no other account USAGE shows `CHECKING AI`.
Claude values update about once a minute; if the Claude service limits
requests, they are marked `STALE` until it allows them again. If you decline, Claude
stays hidden for an hour, then silent checks resume. Only the first read after
launching Companion may ask for permission; closing and opening the lid does
not trigger another authorization prompt. If Keychain access becomes unavailable,
Companion keeps using its current token until it expires, then shows the last
values as `STALE` while retrying quietly every five minutes. Relaunch Companion
to grant access again if needed. At most two providers appear,
in the order Codex, Cursor, Claude. When Claude Code has not run for several
hours, Claude values stay visible marked `STALE` until you use it again; a
stale window whose reset time has passed shows 100% left and `--`. Signing out
of Claude Code or switching accounts shows up within about five minutes. Bars and `LEFT` percentages show remaining capacity.
Business credits and Cursor Enterprise spend show **used / limit** in credits
or dollars alongside the remaining percentage, its bar and the reset time.
An unavailable reset time appears as `RESET --`.
`STALE` marks provider data that is no longer fresh; on a single-metric screen
it appears below the provider name. `CHECKING AI` appears during discovery and
checks again every two seconds until the Mac finishes;
`NO AI ACCOUNTS` appears if none can be read. The Mac samples accounts every
30 seconds and the Cardputer checks the cache every 10 seconds after discovery.
Companion's Diagnostics submenu shows whether its own Codex, Cursor and Claude
cache is fresh or stale. If the Mac shows fresh while USAGE shows `STALE`, the issue
is between Companion and the device; if both show stale, inspect the provider
refresh on the Mac. Up/Down (the `;` / `.` keys, with or without Fn) selects a visible metric for roughly three seconds
and expands its gauge on Unit Puzzle. Otherwise Puzzle shows one full 8×8 gauge,
two four-row gauges, or, with three or four metrics, four two-row gauges.
Each four-row gauge has purple dots at both ends; its other 30 dots show the
remaining limit. A two-row gauge also has purple ends and 14 limit dots; the
gauges follow the screen order from top to bottom. The dots stay visible when
the limit reaches zero.
The Puzzle shows these gauges only while USAGE or its details are visible, above Pomodoro;
switching to STATUS or closing AI gives the matrix back, and changes made while USAGE was hidden do
not replay a low-quota or reset flash when you reopen it. Companion loss clears
the gauge and account values; the next Mac supplies its own data.

With Codex Plus and Claude together, each account shows two rows, `5H` and
`WK`, with the remaining percentage under `LEFT` and the time until the window
resets under `RESET`. A stale account shows `STALE` in place of `RESET`. Select
a row with Up/Down and press Enter to open that account's LIMITS details.
Without a selected row, Enter opens the first account with 5-hour or weekly
limits. Claude details have LIMITS only.

With a current Companion and Codex Plus, the small `R×N` mark on the main
screen shows the number of available reset credits. Its absence means that
the count is unavailable; `R×0` means none remain. Press Enter for LIMITS
(two separate columns for used, left and reset timing). Press the `,` / `/`
keys marked Left / Right without Fn to switch to RESETS (count, short titles
and expiry). Expiry appears in days, hours, or minutes; `EXP NOW` means it has
elapsed, and `EXP --` means its timing is unknown. Up/Down scrolls when there
are more than two detail rows; the `;` / `.` keys work without Fn. Enter
returns to the main dashboard; Escape in details also returns to USAGE, and
Escape on USAGE closes AI. These details are
read-only; RESETS appears only for Codex Plus. If one rolling window is unavailable,
its LIMITS column shows `--` while RESETS remains accessible. Titles that the
Cardputer font cannot show appear as `RESET CREDIT`.

### STATUS

STATUS fills the area below the navigation strip with one colored plate for each app whose hooks
Companion has installed on the selected Mac, in the order CODEX, CLAUDE,
CURSOR. With no hooks installed it reads `NO AI HOOKS`; until the Mac answers
it reads `CHECKING AI`.

| Plate | Meaning |
| --- | --- |
| Blue, `WORKING` | An agent is working and nothing waits for you |
| Vermilion, `NEEDS YOU` | An agent waits for a permission or an answer, or stopped with an error |
| Green, `DONE` | The work ended or you stopped it within the last ten minutes, and nothing is active |
| Light grey, `DONE` | The same, but the last finish is more than ten minutes old |
| Pale, `--` | No live session yet, hooks not firing, or no fresh answer |

A wait or error in one chat stays visible while another chat of the same app
works. A permission request shows `NEEDS YOU` only after about 15 seconds
without an answer, so automatic approvals (such as Codex auto-review) do not
flash it; a question or an error shows at once. `DONE` means the agent stopped, not that the result is correct. Escape
closes AI.

It needs hooks in each desktop app. In the Companion menu, open **AI Agent
Hooks** and choose **Install Codex Hooks**, **Install Claude Code Hooks** or
**Install Cursor Hooks**, then restart that app. Codex asks you to review and
trust new hooks (in the CLI, `/hooks`). Companion changes only its own entries
in `~/.claude/settings.json`, `~/.codex/hooks.json` (or `$CODEX_HOME`) and
`~/.cursor/hooks.json`, keeps a `.cardputer-backup` copy, and refuses a file
that is not plain JSON. **Remove … Hooks** takes them out again. Diagnostics
lists whether each app's hooks are installed and when the last event arrived.
A row appears within a few seconds of installing. A workplace policy can
disable user hooks (Codex `hooks = false` or `allow_managed_hooks_only`, Cursor
enterprise hooks); the row then stays `--`.

Known gaps: Claude Code reports no event when you interrupt it or deny a
permission, so that chat keeps its last state until the agent process ends or
about 15 minutes pass; a Cursor agent waiting for approval shows `WORKING`.
Hooks in cloud agents do not reach the Mac.

While STATUS is visible, a connected Unit Puzzle shows the same apps as
stacked bands in the screen order Codex, Claude, Cursor: one app fills the
matrix, two take the top and bottom four rows, three take three, three and two
rows. With more than one app, each band starts and ends with a purple dot, as
in USAGE. Blue, vermilion and green match the plates,
an old `DONE` is a dimmer green and an app with `--` is a dim grey band; with no hooks installed the matrix is
left to Pomodoro or other owners. Switching to USAGE transfers the matrix to
the quota gauges; closing AI gives it back to other owners.

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
| Left/Right on AI's main pages (`,` / `/` without Fn also work) | Switch STATUS and USAGE |
| Up/Down on AI's USAGE dashboard (`;` / `.` without Fn also work) | Select a row and temporarily expand its quota metric on Unit Puzzle |
| Enter in AI's USAGE with Codex Plus or Claude | Open LIMITS for the selected row's account (or the first such account); from details return to the dashboard |
| Escape in AI's LIMITS / RESETS | Return to USAGE; another Escape closes AI |
| Left/Right in Codex Plus details | Switch LIMITS and RESETS |
| Up/Down on the RESETS page | Scroll available reset-credit details |
| Left/Right, 1–0, Fn+1–0 in LED GALLERY | Select one of twenty effects |
| Space in LED GALLERY | Trigger the current effect's primary action shown on the LCD |
| Enter in NFC on `TAP A TAG`, `BLANK TAG` or `NO RECORD FOR THIS TAG` | Enter a name for a new container, or for that sticker's record |
| Enter in the NFC name editor | Save the name; a new container then waits for a blank sticker |
| Escape in the NFC name editor or on `TAP TAG TO WRITE` | Cancel the name or the registration |
| Up/Down or Left/Right in an NFC container (`;` `.` `,` `/` also work) | Change description pages without wrapping |
| Fn+Del in NFC on an inventory sticker | Ask to erase the sticker and delete its record; Enter erases, Escape cancels |
| Enter in NFC on `RECORD NOT SAVED`, `MICROSD UNAVAILABLE` or a notice | Save again / retry the card / dismiss the notice |
| Escape in NFC on other screens | Return to Apps (ignored while a sticker is being written) |
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
The current firmware includes Apps, SYSTEM, POMODORO, LED GALLERY, NFC when a Unit NFC is
connected at startup, MAC CONTROL when
Companion is ready, and MAC STATUS and AI with that Companion
session. AI's STATUS page has not yet been checked with the real desktop apps on
both Macs or on a physical Unit Puzzle. It does not include profile-metadata editing or template
resolution, Action-to-HID mappings, a Mac companion CLI/control protocol,
Wi-Fi network scanning, or weather/VPS/Telegram features. Boot/status sound
cues from the broader UI requirements remain planned. Unit Puzzle LED Gallery
requires physical acceptance and tuning on the actual matrix. NFC inventory has
not yet been checked on a physical Unit NFC with stickers; it writes only blank
NTAG213/215/216 stickers and erases only inventory stickers, does not scan in
the background or look for a Unit
NFC connected after startup, does not take Cyrillic input on the Cardputer
keyboard, and the Unit NFC and Unit Puzzle cannot be used together.
