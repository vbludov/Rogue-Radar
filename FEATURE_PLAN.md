# Saved Devices, Learn a Known Device, and Nearby Signals

Status: planned; these features are not implemented in the current firmware.
Implementation order: Saved Devices first, then Learn a Known Device, then
Nearby Signals. They build on the existing Track Signal feature.

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

### Storage foundation

Design saved records for multiple user-confirmed addresses from the beginning,
even though the first implementation saves one selected address per device.
Keep address type, observation metadata, and advertised details separate from
the custom name. Bound record sizes and address counts, and version the storage
format so learning can be added without discarding existing saved devices.

The first delivery covers saving/naming a selected target, listing saved
devices, reopening Track Signal, renaming/deleting entries, and persistence
across restarts. Verify waiting, signal loss/reacquisition, and Pocket Mode
before adding the learning workflow. Optional SD backup follows the core flow.

### Identity and locating limits

Wi-Fi targets use their BSSID. BLE targets use their observed address; devices
that rotate addresses may require manual relinking. An advertised name alone
must not automatically establish identity because multiple devices can share it.

Locating depends on currently detectable wireless signals. Saved Devices does
not provide GPS position or historical movement tracking. Signal strength helps
with searching but does not establish precise distance, direction, or ownership.

## Learn a Known Device

Help associate a nearby Wi-Fi access point or BLE advertiser with a named saved
device, and add further observed addresses only with the user's confirmation.
This extends Saved Devices — Find and Track Known Devices.

### Planned workflow

1. Open Learn a Known Device, place the radar near the intended device, and
   choose a candidate from a nearby scan. Proximity and signal strength help
   narrow the list but do not confirm identity.
2. Review the observed address, address type, advertised name, and available
   service/manufacturer advertisement details. Save the candidate under a new
   custom name or explicitly associate it with an existing saved device.
3. Repeat when another address is observed, confirming each association before
   it is stored. Allow reviewing and removing associated addresses.
4. Open the saved device in Track Signal using its confirmed addresses. Show
   which observed address is active; do not combine simultaneous advertisers'
   RSSI readings into one chart. Require selection if several addresses are
   present, and reset history if the tracked address changes.

Similar advertisement details may suggest **Possible matches**, but must never
automatically merge devices, relink addresses, or start tracking an unconfirmed
candidate. Names and service/manufacturer data can be shared by many devices;
they are supporting observations, not guaranteed unique fingerprints.

### Limits

A short capture may not observe any address rotation. Saving several observed
MAC addresses does not predict or resolve future random addresses. An old
address match alone also does not permanently establish device identity.
Resolving BLE resolvable private addresses requires the relevant identity
resolving key; supported pairing/bonding integration is a separate future
investigation, outside this learning feature. The ESP32-S3 implementation
supports BLE, not Bluetooth Classic.

## Nearby Signals

Discover nearby Wi-Fi access points or BLE advertisers in separate views ranked
by smoothed recent signal strength. Each row shows a name or address fallback,
current RSSI, a compact signal-history chart, and a rising/falling indicator.

Refresh rankings periodically and freeze ordering while navigating so selection
remains stable. Selecting a result offers Track Signal, Save Device, and access
to Learn a Known Device for a confirmed association. Mark
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
