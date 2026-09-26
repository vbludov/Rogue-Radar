# Saved Devices and Nearby Signals

Status: planned; neither feature is implemented in the current firmware.
Implementation order: Saved Devices first, then Nearby Signals. Both build on
the existing Track Signal feature.

## Saved Devices — Find and Track Known Devices

Save a known Wi-Fi or BLE device with a custom name, then select it later to
find and locate it using Track Signal's live signal chart, LED ring meter,
and optional audio guidance. Examples include “My watch,” “My AirTag,” and
“Home access point.”

Finding and tracking known devices is the primary purpose of Saved Devices.
Its saved list provides a direct starting point for locating a previously
identified device, without first finding it again in a discovery list.

### Planned behavior

- Save the selected device's custom name, Wi-Fi/BLE type, observed address,
  and last-seen information in internal flash, accessible without an SD card.
- Open Saved Devices, choose a known target, and select Track Signal.
- If the target is out of range or not advertising, show “Waiting for [name]”
  and withhold guidance beeps/ring readings until fresh signals arrive.
- Once detected, provide the existing live chart, ring meter, and optional
  audio guidance. Resume waiting when the signal is lost.
- Rename or delete saved entries, and manually relink an entry to a newly
  observed device address using “Update from nearby device.”
- Provide optional SD export/import for backups.

### Identity and locating limits

Wi-Fi targets use their BSSID. BLE targets use their observed address; devices
that rotate addresses may require manual relinking. An advertised name alone
must not automatically establish identity because multiple devices can share it.

Locating depends on currently detectable wireless signals. Saved Devices does
not provide GPS position or historical movement tracking. Signal strength helps
with searching but does not establish precise distance, direction, or ownership.

## Nearby Signals

Discover nearby Wi-Fi access points or BLE advertisers in separate views ranked
by smoothed recent signal strength. Each row shows a name or address fallback,
current RSSI, a compact signal-history chart, and a rising/falling indicator.

Refresh rankings periodically and freeze ordering while navigating so selection
remains stable. Selecting a result offers Track Signal and Save Device. Mark
saved targets with a star and display their custom names. The initial chart
placement is beside the name; a faint background chart remains a visual option
if it preserves readability on the device display.

## Description and documentation wording

Use **“Saved Devices — Find and Track Known Devices”** as the full feature
heading in descriptions, release notes, help, and documentation. The compact
device menu may remain **“Saved Devices”**, with **“Find and Track Known Devices”**
as its explanatory text where space permits.

Whenever a description introduces Saved Devices, explain that the user can
reopen a named, previously saved target to find and locate it through live
signal guidance. Keep the planned/implemented status explicit until delivery.
