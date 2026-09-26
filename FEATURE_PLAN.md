# Saved Devices, Learn a Known Device, and Nearby Signals

Status: implemented in `v1.1.0-cc1101.11`; host tests cover storage, signal
models, and Saved UI lifecycle. Live discovery and controlled Amazfit learning
have passed on CC1101. Physical SD persistence remains blocked by card detection. These features
build on the existing Track Signal feature.

## Saved Devices — Find and Track Known Devices

Save a known Wi-Fi or BLE device with a custom name, then select it later to
find and locate it using Track Signal's live signal chart, LED ring meter,
and optional audio guidance. Examples include “My watch,” “My AirTag,” and
“Home access point.”

Finding and tracking known devices is the primary purpose of Saved Devices.
Its saved list provides a direct starting point for locating a previously
identified device, without first finding it again in a discovery list.

### Implemented behavior

- Save the selected device's custom name, Wi-Fi/BLE type, observed address,
  and last-seen information on the SD card as the primary saved-device library.
- Open Saved Devices, choose a known target, and select Track Signal.
- If the target is out of range or not advertising, show “Waiting for [name]”
  and withhold guidance beeps/ring readings until fresh signals arrive.
- Once detected, provide the existing live chart, ring meter, and optional
  audio guidance. Resume waiting when the signal is lost.
- Rename or delete saved entries, and manually relink an entry to a newly
  observed device address using “Update from nearby device.”
- Allow backing up the library by copying its files from the SD card.
- If the card is missing or unreadable, show that the saved library is
  unavailable; keep live scanning and tracking available. Report failed saves
  clearly rather than silently creating a second library in internal flash.

### Storage foundation

Use SD storage for device records and learned advertisement details; internal
flash remains suitable for small application preferences. SD capacity provides
room to grow, while the record format provides support for multiple addresses.
Read records as needed with bounded memory use rather than loading the entire
library into RAM. Keep files in a dedicated application directory.

Design saved records for multiple user-confirmed addresses from the beginning,
even though the first implementation saves one selected address per device.
Keep address type, observation metadata, and advertised details separate from
the custom name. Bound record sizes and address counts, and version the storage
format so learning can be added without discarding existing saved devices.

Use recoverable writes with a temporary file and a last-known-good copy; validate
records before accepting them. Test interrupted writes, card removal, malformed
files, and a full card. Batch observation updates instead of writing on every
scan result. Validate SD access alongside display updates on the shared SPI bus.

The implementation covers saving and naming a selected target, paginated
listing, reopening Track Signal, renaming and deleting entries, and recoverable
SD persistence. Records support up to eight explicitly confirmed addresses.
A dedicated backup/restore menu can follow the core flow; copying the library
files provides the initial backup method.

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

### Implemented workflow

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

The guided capture uses three explicit phases: near the selected device, away
from it, then near it again. Position the target first, then press **Capture**
for each phase. Sampling freezes once the minimum observation period and fresh
sample count are met, so moving or waiting between phases does not contaminate
the readings. An insufficient capture stops after 30 seconds and can still be
reviewed; it is never reported as a successful identification. Use **Next** to
advance. The result describes only whether the selected
signal responded consistently to that movement. Nearby signals can be marked
as exclusions during the exercise. Neither a consistent response nor an
excluded neighbor proves identity; Save and Associate remain explicit actions.

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

Rankings refresh periodically and freeze while navigating so selection remains
stable. Selecting a result offers Track Signal, Save Device, and access to
Learn a Known Device for a confirmed association. Saved targets use a star and
their custom name. Rows include a compact recent-history trace and trend.

## Validation status

Host tests cover fixed-capacity signal history and ranking, the guided learning
model, storage CRUD, pagination, malformed records, interrupted-write recovery,
card unavailability, full media, and delete tombstones. The SD library uses
versioned, CRC-validated, file-per-record storage with temporary and
last-known-good recovery files; it does not fall back to internal flash.

On 2026-09-26, both firmware profiles built and all host suites passed. CC1101
live tests exercised Wi-Fi/BLE discovery, Nearby/Track/Back, unavailable-card
handling, and controlled learning with an Amazfit Band 7. Its near/away/near
means were -37/-76/-41 dBm (8/8/9 fresh samples), yielding a consistent response
without a qualifying ambiguous neighbor. The earlier capture, interrupted by
card handling, correctly returned inconsistent near readings. A single device
test does not establish a general identification success rate.

Physical SD persistence is not verified: three tested cards failed initial
SPI communication, including an isolated test without display or radios on
both SPI controllers. No device was saved and no card was formatted by the
firmware. A working/seated card is needed for SD/display sharing, restart
persistence, and saved-entry tracking/reacquisition/Pocket Mode checks.
Original T-Embed runtime checks also remain pending. `v1.1.0-cc1101.11`
therefore remains a test build.

## Description and documentation wording

Use **“Saved Devices — Find and Track Known Devices”** as the full feature
heading in descriptions, release notes, help, and documentation. The compact
device menu may remain **“Saved Devices”**, with **“Find and Track Known Devices”**
as its explanatory text where space permits.

Whenever a description introduces Saved Devices, explain that the user can
reopen a named, previously saved target to find and locate it through live
signal guidance. Keep the pending physical-validation status explicit until
that work is complete.
