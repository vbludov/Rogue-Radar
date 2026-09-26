# Rogue Radar for LilyGO T-Embed CC1101

Handheld **Wi-Fi and Bluetooth Low Energy scanning, signal tracking, and known-device finding** for the LilyGO T-Embed CC1101. Navigate with the rotary encoder, follow signals on the display, and use the eight-light ring and speaker for visual and audio guidance.

**Branch:** `t-embed-cc1101` · **Current firmware:** `v1.1.0-cc1101.13` · **Status:** test build

This branch builds on [ATOMNFT/Rogue-Radar](https://github.com/ATOMNFT/Rogue-Radar), with board support and features developed for the T-Embed CC1101. The tools described below use the ESP32-S3's 2.4 GHz Wi-Fi and BLE radios. Sub-GHz scanning/transmitting through the CC1101 radio is not implemented.

## Overview

- **Discover nearby signals:** scan Wi-Fi access points and BLE advertisers, inspect detector matches, and compare recent signal strength.
- **Track a selected target:** follow a live signal chart with a strength meter, ring lighting, and optional audio guidance.
- **Find and Track Known Devices:** save named Wi-Fi or BLE devices on a microSD card and reopen them later in Track Signal.
- **Learn a Known Device:** compare near/away/near measurements and review neighboring signals before confirming a saved identity.
- **Keep monitoring:** choose timed or continuous scanning, independently toggle sound and light alerts, and use Pocket Mode while carrying the device.

[Controls](#controls-and-power) · [Build and flash](#build-and-flash) · [Version tracker](#version-tracker) · [Feature plan and validation](FEATURE_PLAN.md)

## Signal discovery and tracking

### Nearby Signals

Separate **Wi-Fi** and **BLE** views rank access points or advertisers by smoothed recent signal strength. Each row shows the SSID or advertised name, falling back to an address, alongside RSSI, a small history chart, a trend indicator, and saved-device status. Ranking pauses while navigating so the selected row stays stable.

Select a result to **Track**, **Save**, or associate its address with an existing saved device. The charts cover **−130 to 0 dBm**, keeping very strong nearby signals visible.

### Track Signal

Select **Track Signal** from a Network Scanner result, any BLE scanner/detector result, Nearby Signals, or a saved device. Tracking follows the selected Wi-Fi BSSID or BLE address.

- Large **30-second live chart** with raw and smoothed readings, current RSSI, and a strengthening/weakening indicator.
- **Encoder-ring strength indication** that fills as the signal gets stronger.
- **Optional guidance audio** with faster, higher-pitched beeps as signal strength increases.
- Compact **speaker** and **lightbulb** toggles; crossed icons indicate disabled guidance.
- Missing readings appear as gaps. Waiting/lost status replaces live guidance when the target is absent.
- Back returns to the originating result or saved-device page.

Signal strength helps you explore where a signal becomes stronger; it does not provide reliable distance or direction. Tracking guidance settings are separate from scanner detection-alert settings.

### Saved Devices — Find and Track Known Devices

Save a Wi-Fi or BLE target with a recognizable name, such as **My Watch** or **My AirTag**, then open **Saved Devices → your device → Track Signal** to find and locate it when it is advertising within range.

The SD library supports custom names, rename/delete, advertised details, and up to **eight explicitly confirmed addresses per device**. You can review or remove confirmed addresses and associate a newly observed address. If several saved addresses are present, choose the one to track; their signal readings are not combined.

Saved devices live on the **microSD card**, while ordinary preferences are saved internally. An unavailable card disables the saved library; live scanning and unsaved tracking remain available. Records include corruption checks and recovery files for interrupted updates.

### Learn a Known Device

A guided **near → away → near** exercise helps distinguish a chosen Wi-Fi or BLE signal from nearby stationary devices:

1. Select the target in Nearby Signals or use Learn a Known Device.
2. Position the target near the radar, press **Capture**, and keep it still until capture finishes.
3. Press **Next**, move only the target away, then capture again.
4. Return it to the same near position and orientation for the final capture.
5. Review the signal response, excluded neighbors, and possible matches before saving or associating an address.

The result can report a consistent response, inconsistent readings, or insufficient evidence. Similar advertisement details are suggestions for review. Learning never automatically merges devices or confirms a new address.

BLE addresses can rotate, and names or advertisement contents can be shared by unrelated devices. Saving several addresses does not guarantee recognition of a future private address. This feature uses BLE advertisements; it does not support Bluetooth Classic devices.

## Wi-Fi tools

| Tool | What it does |
|---|---|
| **Network Scanner** | Lists nearby access points with SSID, BSSID, RSSI, channel, and security details; select a result to track or save it. |
| **Connect to AP** | Select an access point, enter its password, and connect for the connected-network tools. |
| **LAN Host Discovery** | Checks hosts on the connected local subnet using lightweight TCP probes. |
| **Gateway Info** | Shows connection, addressing, gateway, DNS, signal, and connectivity information. |
| **Station Scanner** | Observes nearby Wi-Fi client activity passively. |
| **Deauth Detector** | Monitors deauthentication/disassociation activity and provides event statistics. |
| **Channel Analyzer** | Surveys Wi-Fi channel activity and signal strength. |
| **Packet Monitor** | Shows packet rates, packet types, average RSSI, channel controls, and a live graph. |
| **WiFi Mapper** | Displays nearby access-point signals with RSSI scaling and speed presets. |
| **PineAP Hunter** | Watches for BSSIDs cycling through many SSIDs across scans. |
| **Pwnagotchi Watch** | Looks for matching beacon behavior and displays advertised status and device details. |
| **Flock Detector** | Flags Wi-Fi patterns associated with Flock-style devices and shows match details. |
| **Flock Hybrid** | Alternates BLE and Wi-Fi detection phases and combines their results in one view. |

## BLE tools

Every BLE scanner/detector below provides **Track Signal** access from a selected result. Detector matches are based on advertisement patterns and are indications to inspect, not proof of a device's identity or purpose.

| Tool | What it does |
|---|---|
| **BLE Scanner** | Discovers BLE advertisers and shows their names, addresses, and signal details. |
| **AirTag Detector** | Looks for AirTag-like / Apple Find My advertisement patterns. |
| **Flipper Detector** | Looks for Flipper-style names, address prefixes, and service identifiers. |
| **nyanBOX Detector** | Shows matching badge advertisements, including available level, version, and age details. |
| **Axon Detector** | Looks for configured Axon-style address prefixes and shows matching advertisements. |
| **Raven Detector** | Looks for Raven / SoundThinking-style BLE service patterns. |
| **Smart Charger** | Passively looks for Smart Charger / FFF0 advertisements and shows available details without connecting or controlling the charger. |
| **Tesla Detector** | Looks for Tesla-style BLE names and shows signal and advertisement details. |
| **Skimmer Detector** | Flags suspicious BLE serial/module names and related advertised patterns for inspection. |
| **Meta Detector** | Looks for Meta / Ray-Ban-style smart-glasses advertisements. |

## Timed and continuous scanning

The 20 scanner/detector pages offer **Timed / Continuous**, **Start / Stop**, and compact speaker/lightbulb alert toggles. The mode is remembered per tool. Open a page, select the mode, and press **Start**.

**Timed** uses the configured duration. **Continuous** runs until Stop or Back. Stop keeps the results; Back releases the radio before leaving. Opening a result or Track Signal pauses its parent scan, which resumes on return with the remaining timed duration preserved.

This applies to all ten BLE pages and the ten Wi-Fi monitoring pages. **Connect to AP, LAN Host Discovery, and Gateway Info** retain their separate workflows. Flock Hybrid alternates radios; it does not listen on Wi-Fi and BLE simultaneously. Scan results are bounded in memory rather than stored as a permanent encounter log.

## Alerts, Pocket Mode, and settings

| Feature | Behavior |
|---|---|
| **Alert Sound** | Enables detection chirps independently of menu feedback and tracking guidance audio. |
| **Light Alert** | Uses the same detection events as sound alerts: the ring flashes red, then runs a red chase effect. Sound and light can be enabled independently. |
| **Scanner alert toggles** | Speaker and lightbulb buttons control the saved Alert Sound / Light Alert preferences. The master LED switch still applies. |
| **Pocket Mode** | Turns off the display and suppresses normal ring lighting, locks encoder input, and keeps monitoring and enabled alerts running. The top button wakes the same screen. |
| **Brightness and dimming** | Adjust display brightness and enable inactivity dimming. |
| **Themes and rotation** | Choose a built-in color theme or flip the landscape display. |
| **LEDs** | Enable or disable the eight-light WS2812 ring, including its menu and scanning effects. |
| **Sound controls** | Adjust alert/menu volumes and toggle menu feedback separately. |
| **Scan Defaults** | Configure scan durations, result limits, channel-hop timing, and scan presets. |
| **Device Info** | View firmware, board, chip, memory, and address information. |
| **SD Update** | Provides an on-device application update workflow from microSD; place the matching CC1101 application binary at `/update.bin` in the card root. USB flashing is the validated installation path below. |
| **Reset Settings** | Restore default saved runtime preferences. |
| **Power On/Off** | Open the shutdown confirmation from the main menu or Misc Tools. |

Repeat sightings are filtered to reduce repeated alerts. Active scanning and tracking are protected from automatic return-home. Pocket Mode is a display/input lock, so it does not put the processor to sleep.

## Controls and power

| Control | Action |
|---|---|
| **Rotate encoder** | Move through menu items and results. |
| **Press encoder** | Select the focused item. |
| **Top button: press and release** | Go Back one level or cancel keyboard entry. On the main menu it has no Back action. |
| **Top button: hold at least two seconds, then release** | Enter Pocket Mode directly from the active screen. Keyboard entry retains its cancel behavior. |
| **Top button while in Pocket Mode** | Wake the current screen; the next short press works as Back. |

For continued monitoring in a pocket, enter Pocket Mode directly from the running tool. Navigation to Misc Tools first leaves that tool. Back/wake actions can wait for a blocking scan to return.

To shut down, disconnect USB and choose **Power On/Off → Power Off**. Allow up to 15 seconds. To turn the device on, use the hardware **PWR/QON** button or reconnect USB. The top Back button is separate from the hardware power control.

## Hardware and storage

This build targets the **LilyGO T-Embed CC1101** with an ESP32-S3, a **320 × 170 ST7789 display**, rotary encoder, separate top button, **eight WS2812 LEDs**, onboard speaker, and microSD slot. The display and card share the board's SPI bus.

A **2 GB FAT32 card** has passed physical saved-device create/read/rename/address-edit/delete tests and restart persistence. Card seating matters; insert it fully with the contacts correctly engaged. Compatibility with other cards has not been established by that test. The firmware does not automatically format a card.

Board pin assignments and build details are maintained in [board_config.h](rogue-radar/board_config.h) and the [CC1101 porting notes](PORTING_CC1101.md).

## Build and flash

Use [PlatformIO](https://platformio.org/) and explicitly select the CC1101 environment:

```sh
git clone --branch t-embed-cc1101 https://github.com/vbludov/Rogue-Radar.git
cd Rogue-Radar
pio run -e t_embed_cc1101
pio run -e t_embed_cc1101 -t upload
pio device monitor -b 115200
```

If port selection is needed, add `--upload-port COM5` to the upload command, replacing `COM5` with your device's port. The project pins its platform and library versions and selects the CC1101 display configuration automatically. No manual TFT library-file replacement is required.

The application binary is `.pio/build/t_embed_cc1101/firmware.bin` with default build paths. PlatformIO's upload target handles the required flash images and offsets. See the [Windows short-path build instructions](PORTING_CC1101.md#reproducible-build) if your checkout path exceeds the compiler's command-length limit.

Always specify `-e t_embed_cc1101`: the repository retains an `original` compatibility environment as its default, which is not the CC1101 target.

## Validation status

Both firmware profiles compile. Host checks cover signal models, radio/session lifecycles, storage recovery, and UI navigation. Physical CC1101 checks include display/controls, scanner lifecycle handoffs, SD operations, and restart persistence. Controlled Amazfit Band 7 learning produced a consistent near/away/near response; the named saved device subsequently reopened into Track Signal with fresh BLE readings.

The `.13` chart-range update builds and boots on the device; close-range visual confirmation remains pending. Additional cards and device types, saved-target loss/reacquisition, saved-entry Pocket Mode, and the SD firmware-update flow still need broader hardware validation. A detector appearing in the menu does not mean its accuracy has been validated against every matching device.

See [FEATURE_PLAN.md](FEATURE_PLAN.md#validation-status) for the detailed feature scope and current validation record.

## Version tracker

| Version | Status | Changes |
|---|---|---|
| v1.1.0-cc1101.13 | CC1101 test build | Keeps strong and weak RSSI history visible in Nearby Signals and Track Signal with full-range chart scales and bounded plotting |
| v1.1.0-cc1101.12 | CC1101 test build | Fixes excessive stack use in saved-device storage and screen resets; 2 GB FAT32 CRUD, restart persistence, and saved Amazfit tracking verified on hardware |
| v1.1.0-cc1101.11 | CC1101 test build | Adds SD-backed Saved Devices, guided Learn a Known Device capture, Nearby Signals, and reopening named targets in Track Signal; live discovery and Amazfit learning tested; SD validation completed in .12 |
| v1.1.0-cc1101.10 | CC1101 test build | Adds Timed/Continuous scan sessions, Start/Stop, compact alert toggles, safe detail/tracker handoffs, and protection from inactivity timeout |
| v1.1.0-cc1101.9 | CC1101 test build | Adds main-menu Power On/Off with confirmation, BQ25896 battery shutdown, USB guard, and power-on instructions |
| v1.1.0-cc1101.8 | CC1101 test build | Protects keyboard editing from idle home cleanup, clears retired menu references, and fixes stale transition state when opening the keyboard |
| v1.1.0-cc1101.7 | CC1101 test build | Fixes Connect to AP keyboard cancellation by reusing the AP list; protects deferred screen cleanup during transitions |
| v1.1.0-cc1101.6 | CC1101 test build | Adds direct Track Signal access to nyanBOX, Axon, Raven, Smart Charger, Tesla, Skimmer, and Meta detector results |
| v1.1.0-cc1101.5 | CC1101 test build | Enlarges Track Signal history chart by 39%; places RSSI beside history and removes the separate status row |
| v1.1.0-cc1101.4 | CC1101 test build | Adds Wi-Fi/BLE Track Signal with 30-second live chart, strength ring, optional guidance audio, compact mute/light toggles, and selectable AirTag results |
| v1.1.0-cc1101.3 | CC1101 test build | Adds Pocket Mode: display off, encoder locked, monitoring and selected alerts continue; top-button wake |
| v1.1.0-cc1101.2 | CC1101 test build | Adds saved Light Alert toggle: red ring flash and chase on detection alerts, independent of sound; restores existing lighting afterward |
| v1.1.0-cc1101.1 | CC1101 test build | Adds T-Embed CC1101 board support and top-button Back shortcut (GPIO6), including keyboard cancel; fixes Wi-Fi tool re-entry crash; preserves the original T-Embed build |

## Credits

- [ATOMNFT / Rogue-Radar](https://github.com/ATOMNFT/Rogue-Radar) — original firmware and project foundation.
- [JustCallMeKoKo / ESP32Marauder](https://github.com/justcallmekoko/ESP32Marauder) — Wi-Fi/BLE research tools and inspiration.
- [jbohack / nyanBOX](https://github.com/jbohack/nyanBOX) — badge hardware, firmware, and BLE ideas.
- [spacehuhn / PacketMonitor32](https://github.com/spacehuhn/PacketMonitor32) — packet monitoring and graph inspiration.
- [GhostESP Revival](https://github.com/GhostESP-Revival/GhostESP) — BLE detector ideas.
- [Esp32vsEvil / TeslaScanner](https://github.com/Esp32vsEvil/TeslaScanner) — Tesla scanner inspiration.
- [0xXyc / flock-you-wifi-recon](https://github.com/0xXyc/flock-you-wifi-recon) — Flock-related matching and detection research.

Use wireless analysis tools responsibly and only where authorized.
