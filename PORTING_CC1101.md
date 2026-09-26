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
| Software shutdown/deep sleep | Disabled/unavailable |
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
