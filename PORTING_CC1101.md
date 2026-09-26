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
documented Huge APP partition layout. That layout has no OTA slot, so the
sketch's `Update.begin(..., U_FLASH)` SD update path must be considered
unavailable on the original profile unless it is moved to an OTA-capable 16 MB
partition table. The CC1101 profile uses `default_16MB.csv`; confirm the final
application partition limit in the linker size report.

## Current milestone hardware validation

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
