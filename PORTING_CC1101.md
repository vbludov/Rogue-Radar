# T-Embed CC1101 port and build validation

Rogue Radar has two pinned PlatformIO environments:

- `original` targets the original T-Embed S3 pinout and is the default.
- `t_embed_cc1101` defines `ROGUE_RADAR_BOARD_T_EMBED_CC1101` and selects the
  T-Embed CC1101 display setup.

Both environments pin PlatformIO's Espressif32 platform to `6.13.0` (Arduino
ESP32 2.x) and LVGL to `9.0.0`. Rogue Radar uses the LVGL 9 display and input
APIs. The sketch also uses Arduino ESP32 2.x LEDC APIs such as `ledcSetup()` and
`ledcAttachPin()`, plus the classic BLE API whose return types changed in
Arduino ESP32 3.x. Treat a move to Arduino ESP32 3.x as a separate migration.

## Reproducible build

Use a dedicated PlatformIO core directory so downloaded packages and build
state do not depend on a developer's global PlatformIO installation:

```powershell
$env:PLATFORMIO_CORE_DIR = "C:\path\to\pio-core"
pio pkg install
pio run -e original
pio run -e t_embed_cc1101
```

On Windows, the repository's normal absolute path can make GCC's expanded
include command exceed the CreateProcess limit. A reproducible short-path
workaround is to map temporary drive letters for the repository and PlatformIO
core, and to keep build and dependency output below that short core path:

```powershell
subst R: "C:\path\to\Rogue-Radar"
subst P: "C:\path\to\pio-core"
$env:PLATFORMIO_CORE_DIR = "P:\"
$env:PLATFORMIO_BUILD_DIR = "P:\rr-build"
$env:PLATFORMIO_LIBDEPS_DIR = "P:\rr-libdeps"
Set-Location R:\
pio run -e original
pio run -e t_embed_cc1101
```

Remove the temporary mappings after validation with `subst R: /d` and
`subst P: /d`.

The generated application binaries are:

```text
.pio/build/original/firmware.bin
.pio/build/t_embed_cc1101/firmware.bin
```

Do not treat a successful compile as hardware validation. Before release,
verify the original unit and the CC1101 unit separately using a serial boot
log and the checklist below.

## CC1101 hardware differences

### Initial USB boot test

The initial CC1101 build has been flashed to an ESP32-S3 unit with 16 MB
flash and 8 MB PSRAM. Flash writes passed esptool hash verification, and the
serial log reached `Boot complete` without a reset during a 25-second capture.
Radio scans, sustained operation, and other peripheral behavior still need
further interactive validation. Missing Preferences namespaces on the first boot
use the firmware's default settings.

The user subsequently confirmed working display, menu navigation and encoder
selection. A captured Network Scanner re-entry crash decoded to
`createNetworkScanner -> lv_obj_delete`: the shared Wi-Fi Back handler left
`wifiToolScreen` pointing to a screen deleted by the return animation. The
handler now clears that reference before scheduling deletion. Regression
check: repeatedly open Network Scanner, return with Back, and reopen it;
also switch between different Wi-Fi tools. This addresses the captured crash,
not every possible stability issue.

After flashing the fix, the user confirmed repeated Network Scanner entry
and Back navigation work. A subsequent 45-second serial capture contained
no new crash output. Other tool combinations still need longer testing.

When packaging a merged image, preserve the generated bootloader's **DIO**
header. Do not force `--flash_mode qio` in `merge_bin` or `write_flash`.
PlatformIO intentionally uses DIO for the boot image even though the build
configures QIO flash operation. Forcing the header to QIO caused ROM-loader
errors and watchdog resets on the test unit; restoring the generated DIO
bootloader resolved them. This is a packaging requirement, not a change to
the application's QIO/OPI memory configuration.

The CC1101 model is not pin-compatible with the original T-Embed. Its display,
encoder, LEDs, storage, battery measurement, audio, and power handling require
the CC1101 board adapter selected by `ROGUE_RADAR_BOARD_T_EMBED_CC1101`.

### Current first milestone

| Area | Status |
| --- | --- |
| Display and rotary input | Ported; requires device validation |
| Eight WS2812 LEDs | Ported; requires device validation |
| Wi-Fi and BLE tools | Compiled for the CC1101 profile; require device validation |
| SD mount and SD Update | Ported to shared SPI; require device validation |
| SD recording | Disabled/unavailable with the microphone tool |
| Speaker alerts and menu feedback | Ported to corrected I2S pins; require device validation |
| Microphone recording/audio menu | Disabled/unavailable |
| GPS | Disabled/unavailable |
| Battery gauge/charger data | Disabled/unavailable |
| Software shutdown | BQ25896 battery shutdown with USB guard; physical off/on validation pending |
| Deep sleep | Not implemented |
| Onboard CC1101, PN532, and infrared | No Rogue Radar features yet |

The display, SD card, CC1101, and optional nRF24 share one SPI bus on GPIO
11/9/10. Their chip selects must be outputs and high before any device is
initialized. The display uses CS 41, SD uses CS 13, and CC1101 uses CS 12.
TFT_eSPI must use the CC1101 setup and its HSPI workaround; do not initialize a
second independent `SPIClass(HSPI)` for the SD card on this board.

The CC1101 model's GPIO 21 backlight drives an AW9364. Brightness uses the
AW9364 pulse-count protocol implemented in the CC1101 board support header;
ordinary LEDC PWM is not equivalent and must not be used on this profile.

The CC1101 model has 16 MB flash and 8 MB OPI PSRAM. The PlatformIO board
settings enable QIO flash and OPI PSRAM. The original environment retains the
documented Huge APP partition layout. That layout has no second OTA app slot, so the
sketch's `Update.begin(..., U_FLASH)` SD update path must be considered
unavailable on the original profile unless it is moved to an OTA-capable 16 MB
partition table. The CC1101 profile uses `default_16MB.csv`; confirm the final
application partition limit in the linker size report.

## Current milestone hardware validation

### Top-button Back shortcut (v1.1.0-cc1101.1)

LilyGO identifies the separate top button as `BOARD_USER_KEY`, GPIO6,
active-low (`examples/utilities.h` and `examples/encode_test/encode_test.ino`
in the official T-Embed-CC1101 repository). The CC1101 profile configures it
with `INPUT_PULLUP`; the original T-Embed profile does not configure this pin
as an input because it is used by its speaker.

Press and release the top button to invoke the current screen's Back action.
It cancels an open keyboard through the existing deferred Esc path, and a short
press does nothing on the main menu. A held button does not repeat. Input is debounced
on release; presses during a screen transition or simultaneous encoder click
are ignored. During a blocking scan, the interrupt records the press and the
main loop dispatches Back only after the scan has returned. It does not
interrupt a firmware update or cancel a blocking radio scan midway.

### Pocket Mode (v1.1.0-cc1101.3)

On CC1101, hold the top button for at least two seconds and release to enter
Pocket Mode from the current screen. Misc Tools > Pocket Mode also enters it.
To keep a monitor running, enter directly from that monitor using the top
button; navigating out to Misc Tools already stops the tool in the usual way.

Pocket Mode turns the backlight off, suppresses normal ring lighting, and
discards encoder rotation/clicks. Existing monitoring timers and radio activity
continue, as do enabled sound and light alerts. Master LEDs OFF still suppresses
light alerts. Auto-return-home and inactivity dimming are suspended.

Press and release the top button to wake the same screen without navigating
Back. The next short press works as Back again. An encoder button held during
wake must be released for 50 ms before input resumes. Entry/wake dispatch waits
for blocking scans to finish. Keyboard entry retains its existing top-button
cancel behavior. Pocket Mode is not persisted across reboot and is unavailable
on the original board, which lacks the separate wake key. This is a display/input
lock, not CPU sleep; it does not promise a particular battery runtime.

Validation: both board profiles compile; host tests for 7 and 8 LEDs pass normal
light suppression, visible/capped alerts, and restoration, plus existing alert
regressions. CC1101 flash hash verification and v1.1.0-cc1101.3 boot passed.
Physical button, monitoring continuity, and pocket behavior await hardware checks.

Hardware checks: enter from an active monitor, confirm display and normal ring
go dark, turn/click the encoder without waking, and trigger an enabled alert.
After more than the configured auto-home timeout, wake and confirm the monitor
is still active. Confirm wake does not also go Back, encoder-held wake cannot
select an item, and the next short top-button press navigates normally.

Validation: try Wi-Fi and BLE submenus, Network Scanner, a device detail
screen, WiFi Mapper, and a settings page. Confirm one level per press, no
repeat while held, no action on the home screen, and keyboard cancellation
without applying text. Also check Back after a scan and the encoder's normal
operation. Build validation alone does not establish these hardware results.

### Light Alert (v1.1.0-cc1101.2)

Misc Tools > Light Alert is a saved ON/OFF setting, OFF by default. It shares
the detection chirps' event gates and cooldowns, independently of Alert Sound.
The ring fills red for 240 ms, then runs two red chase rotations before
restoring the latest menu/status/scan lighting. Further hits during an active
animation are coalesced. The master LEDs switch and inactivity dimming apply.
Menu sounds, connection tones, and volume previews do not trigger it.

Validation: both PlatformIO board profiles compile. Deterministic host tests
with 7 and 8 LEDs passed flash/chase timing, coalescing, background restoration,
disable, and brightness limits. The CC1101 app was flashed with hash verification
and reached Boot complete as v1.1.0-cc1101.2.

Hardware validation remains required: trigger a detector alert with sound
OFF and Light Alert ON, repeat with sound ON, check OFF suppresses the effect,
and verify persistence after reboot and normal lighting restoration. Check
the master LEDs toggle and dimmed brightness during an alert as well.

### Connect to AP keyboard exit (v1.1.0-cc1101.7)

Cancelling the Wi-Fi password keyboard with Esc or the top Back button returns
to the retained AP list, preserving its selection and scroll position. Previously
cancel rebuilt the entire list while the old list and keyboard were still in
LVGL's fixed memory pool, risking an allocation assertion and frozen UI.

Deferred screen cleanup now tracks each screen independently and waits until
LVGL has finished using it in a transition. This also protects the password OK
path's connecting/status screens. LVGL warnings and errors go to the USB serial
log to make remaining failures diagnosable.

Both PlatformIO board profiles compiled successfully. The CC1101 application
was flashed with hash verification and reached Boot complete as v1.1.0-cc1101.7.
With pinned LVGL 9.0.0 and a 64 KiB pool, a representative host scenario with
30 AP rows reproduces allocation failure when cancel rebuilds the list; reusing
the retained list passes. This models the allocation pattern rather than the
entire firmware. The real-LVGL cleanup regression also covers independent and
duplicate requests, active/previous/delayed animation targets, external deletion,
and reuse of a freed object's address.

Hardware checks: scan a full AP list, open a secured AP, cancel with Esc and with
the top button, and repeat. Check the retained selection, Wi-Fi Tools Back and
re-entry, and both successful and failed password submission to an authorized
AP. Device interaction testing remains pending until confirmed by the user.

### Keyboard and idle screen lifecycle (v1.1.0-cc1101.8)

Follow-up to the remaining crash report after v1.1.0-cc1101.7: a captured device
LoadProhibited backtrace resolves to `createWiFiMenu()` deleting a retired menu.
Idle-home cleanup previously kept the active screen's pointer even though LVGL
automatically deleted that screen. It could also tear down the password keyboard's
return screen and group while editing was still active.

Idle return now waits while a keyboard is open/closing or a screen transition is
active. Closing the keyboard refreshes the activity timer. Home cleanup drops
both active and inactive screen references before scheduling their deletion,
so reopening a menu cannot delete its old allocation again.

A separate pinned-LVGL regression reproduced stale previous-screen state when a
direct keyboard load interrupts an animation. The keyboard now uses a one-ms
transition without sliding so LVGL runs its completion cleanup; stale transition
state no longer blocks input and the top Back shortcut in that scenario.

Host checks cover actual encoder events opening the keyboard, Esc and shortcut
cancel boundaries, interrupted transitions, and menu retirement/recreation.
An instrumented on-device run passed six cancellations with 30 synthetic AP
entries, alternating encoder Esc and the top-button release handler, including
an intentionally interrupted incoming screen animation. Forced idle expiry
left the keyboard's return state intact in every cycle. Idle-home cleanup and
subsequent Wi-Fi menu recreation also completed. The automatic diagnostic
sequence is excluded from the normal firmware.
Both normal board profiles compiled successfully, and the normal CC1101 app
was flashed with hash verification and booted as v1.1.0-cc1101.8.
The user subsequently confirmed the original keyboard-exit sequence works.

### Track Signal (v1.1.0-cc1101.4)

Open a Network Scanner or BLE Scanner result, then select **Track Signal**.
AirTag Detector results are now selectable and lead to the same BLE detail and
tracking view; Flipper results already use that detail view. Tracking follows
the chosen Wi-Fi BSSID or BLE address, never a shared SSID/name. A BLE device
that rotates its address must be scanned and selected again.

As of v1.1.0-cc1101.6, all BLE menu modes expose Track Signal from a selected
result's detail screen: BLE Scanner, AirTag, Flipper, nyanBOX, Axon, Raven,
Smart Charger, Tesla, Skimmer, and Meta. nyanBOX/Axon retain their separate
legacy Locate actions. Skimmer/Meta results are now selectable; rescanning rebuilds
their encoder focus groups before replacing the old results. Tracker Back returns
to the same detector detail, then its existing Back action returns to its list.
Target-specific RF behavior still requires testing with matching advertisers.
Both board builds and CC1101 flash/boot verification passed for v1.1.0-cc1101.6.

The screen shows the target identity, live RSSI, recent strength trend, and a
30-second chart: muted raw readings and a brighter smoothed average, with a
fixed -100 to -30 dBm scale. Missing 500 ms buckets are gaps. After three seconds
without a reading, it shows signal lost and stops the meter/beeps; reacquisition
resets smoothing and builds a fresh trend. Signal strength is not distance or
direction, and reflections/antenna orientation can affect it.

In v1.1.0-cc1101.5, RSSI shares the identity/history row and the live chart grows
from 62 to 86 pixels high. The separate tracking-status row is removed. RSSI
uses the theme's success color for a strengthening signal and warning color for
a weakening signal. Waiting/lost/stopping messages appear inside the chart only
when needed; a live signal leaves the chart unobstructed.
Both board builds and CC1101 flash/boot verification passed for this layout.

The encoder ring fills blue through yellow to green as the smoothed signal
strengthens. Compact speaker and lightbulb buttons toggle guidance audio and
the tracking ring independently; a diagonal slash means disabled. Their enabled
state and audio volume are saved. Audio starts muted and light starts enabled.
Volume cycles 10–50% in 10% steps. Stronger signals beep faster and at a higher
pitch. These settings do not change detection alert or menu-sound preferences.
The master LEDs switch/dimming still apply; red detection overlays retain priority.

Radio sampling is asynchronous and passive. Wi-Fi uses the target channel plus
periodic all-channel sweeps; it does not intentionally disconnect an existing
connection. BLE receives advertisements continuously without connecting. Tracking
owns the selected scanner until Back stops it and returns to the target's detail
screen. Back waits on "Stopping scan..." if a canceled Wi-Fi scan still has a
completion event pending, so a subsequent regular scan cannot inherit its results.
The idle return-home timer is suspended while tracking. In Pocket Mode,
sampling/chart history and enabled audio continue, normal tracking LEDs stay
dark, and the top button wakes the same view.

Validation: original and CC1101 builds pass, as do 224 signal-model and 47 radio
lifecycle host checks. CI runs these tests. The flashed CC1101 application passed
hash verification and booted as v1.1.0-cc1101.4. Live RF behavior, icon readability,
audio, and the chart still require device testing; host radio tests use stubs.

Hardware validation: select a result other than the first and verify its address;
walk closer/farther; test both icon toggles, saved volume, loss/reacquisition,
Pocket Mode, and repeated Back/re-entry followed by a normal rescan for Wi-Fi,
BLE and AirTag lists.

The next planned feature is **Saved Devices — Find and Track Known Devices**:
name and save known Wi-Fi/BLE targets on the SD card, then reopen them to locate
them using the existing tracker. **Learn a Known Device** follows, adding
advertised details and multiple user-confirmed addresses to saved records.
**Nearby Signals** comes next, adding a ranked discovery list with history
charts, trend indicators, and save/learn/track actions. These features are
planned, not yet implemented; see [FEATURE_PLAN.md](FEATURE_PLAN.md).

### General hardware checks

1. Confirm the boot log reports the intended board profile, 16 MB flash,
   external PSRAM, and adequate free heap.
2. Confirm the splash and LVGL menus render at 320x170 with correct rotation,
   color order, and encoder direction.
3. Exercise backlight on/off and dimming across several levels, including wake
   from the dim state, to validate the AW9364 pulse-count sequence on GPIO 21.
4. Scan Wi-Fi and BLE, then leave and re-enter each tool to catch radio and
   heap-lifetime failures.
5. Verify the eight WS2812 LEDs. The original unit uses seven APA102 LEDs and
   requires a different driver.
6. Mount and read the SD card while the display is updating, then validate the
   SD Update file-open path without applying an update during basic bring-up.
7. Verify menu ticks and alert chirps through the speaker on the corrected
   I2S pins.

Future milestones must validate shared-SPI transitions among display, SD, and
CC1101; the GPIO 39/42 digital microphone and GPIO 46/40/7 speaker; the
BQ27220/BQ25896 battery path on I2C GPIO 8/18; and sleep/wake using the user
key. GPIO 4 is an encoder input and must not be sampled as a battery ADC.
GPIO 15 enables a switched peripheral rail and is not the original T-Embed
power latch.

The CC1101 board also assigns GPIO 43/44 to its exposed UART and optionally to
nRF24 CE/CS. A GPS attached there and nRF24 support cannot be active at the
same time without a deliberate mux policy.

### Power On/Off (v1.1.0-cc1101.9)

Main menu **Power On/Off** opens a confirmation page with Back selected by
default. The existing Misc Tools shortcut opens the same page and returns to
its own menu. Both the encoder Back and top-button Back cancel normally.

The CC1101 uses BQ25896 ship mode, not GPIO15 (a peripheral rail). Unplug USB
before selecting **Power Off**; USB power cannot be removed by cutting the
battery path. Turn on using the hardware **PWR/QON** button or reconnect USB,
not the GPIO6 top Back button. Shutdown takes up to 15 seconds because
BATFET_DLY is enabled so the I2C transaction completes before battery cutoff.

The implementation checks PMU identity and VBUS_GD, preserves unrelated
register settings, and reports I2C errors without stranding the UI. If power
remains after 16 seconds, it attempts to restore the battery path and returns
a retry message. No charger voltage/current settings are changed. The original
board retains its GPIO power-latch path.

References: [LilyGO shutdown example](https://github.com/Xinyuan-LilyGO/T-Embed-CC1101/blob/master/examples/bq25896_shutdown/bq25896_shutdown.ino),
[TI BQ25896 datasheet](https://www.ti.com/lit/ds/symlink/bq25896.pdf), and
[TI delayed-shutdown guidance](https://e2e.ti.com/support/power-management-group/power-management/f/power-management-forum/1261767/bq25896-stops-outputting-vsys-and-won-t-exit-unknown-mode).

Hardware checks: cancel/reopen from each menu; select Power Off with USB
connected and verify the guard; unplug USB and confirm shutdown; restart via
PWR/QON and separately via USB. Physical shutdown/wake remains pending user
validation. Host fault-injection tests cover identity/read/write failures,
USB rejection without writes, and preservation of register settings.

Validation: both PlatformIO profiles and all host suites passed. The CC1101
application was flashed with hash verification and booted as
`v1.1.0-cc1101.9`; the board reported PMU identity `0x06` (BQ25896 with JEITA
profile) and USB connected. Battery cutoff and physical wake are not yet
confirmed.

### Shared scan sessions (v1.1.0-cc1101.10)

`scan_session_ui.h` owns the active scanner's controls, timing and navigation.
Start, poll, stop, detail navigation and backend release run from `loop()`,
outside LVGL input callbacks. Back retains the page until radio cleanup has
completed. The top Back shortcut uses the same callback. A detail/Track Signal
handoff suspends the session and preserves its remaining timed duration; the
session resumes only when its owning scan page becomes active again.

Per-tool mode preferences use the existing Preferences namespace. Opening a
scanner creates an idle session and never starts the radio automatically.
The footer's speaker and bulb controls share Alert Sound and Light Alert
preferences with Misc Tools; the LED master preference remains in effect.
Scan pages, including retained results, are exempt from idle return-home.
Pocket Mode continues scanning with the display off and encoder locked.

`scan_session_model.h` provides wrap-safe elapsed timing and a 128-entry alert
cache. A match alerts on first observation, or after at least 30 seconds of
absence and cooldown. Oldest unseen keys may be evicted when capacity is
exceeded; rotating BLE addresses remain separate observed identities. Device
lists are bounded and are not persistent logs.

BLE rows update signal strength and age in place; new devices and evictions
trigger batched structural rebuilds. Wi-Fi and BLE family menus are retired
after returning home, while a scanner retains its own family menu for Back.
These choices preserve transition headroom in LVGL's existing 64 KB pool.

`continuous_ble_scan.h` uses advertised-device callbacks without retaining
Arduino BLEScanResults. Detector callbacks copy observations into a bounded
mailbox; main-loop processing updates results and LVGL. BLE stop must consume
the stack's stop acknowledgement before permitting another radio owner.
`ble_scan_stop_fence.h` shares the custom GAP hook between scanners and Track
Signal, chains any existing hook, and keeps ownership on a failed stop while
the UI reports that a restart is required. Natural expiry uses the inquiry
completion callback; a queued manual stop still requires its own acknowledgement.
The tracker follows the same contract in both handoff directions. Wi-Fi scanning
retains ownership until Arduino's non-negative SCAN_DONE result is consumed;
a negative timeout alone does not prove cleanup has completed.

Timed scanning uses existing configured durations; continuous backends repeat
sweeps or listen indefinitely. Flock Hybrid alternates configured BLE and
Wi-Fi phases without concurrent radio ownership. Connect to AP, LAN Host
Discovery, and Gateway Info retain their existing workflows.

Validation: both PlatformIO board profiles compile. Host suites pass 224
signal-model, 58 tracker-radio, 25 continuous-BLE and 38 session-model checks,
plus the power-control tests. The pinned LVGL suite passes controller timing,
delayed Stop/Back, timed/continuous detail resume, errors, screen deletion,
keyboard regressions and 13 animation-readiness checks.

On the USB-connected CC1101, the diagnostic passed all 20 scanner pages in
timed and continuous modes, detail suspension/resume, real Wi-Fi/BLE Track
Signal handoffs, active Back, Pocket Mode and forced inactivity timeout. It
then completed 90-second BLE and Hybrid runs; BLE reached the 30-device cap,
and Hybrid exited successfully during its Wi-Fi phase. Sampled minima were
40,072 bytes of ESP heap and 18,004 bytes of LVGL free memory, with no panic,
allocator failure or active-screen deletion warning. These are lifecycle
checks using ambient signals, not detector-accuracy tests against fixtures.
The normal application was then flashed with hash verification and booted as
`RR v1.1.0-cc1101.10`; the diagnostic harness is excluded from that build.
