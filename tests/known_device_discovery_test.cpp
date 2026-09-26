#include <cstdlib>
#include <cstring>
#include <iostream>

#include "radio_stubs/Arduino.h"
#include "radio_stubs/WiFi.h"
#include "radio_stubs/BLEDevice.h"

uint32_t fakeMillis;
FakeWiFiClass WiFi;
static BLEScan fakeBleScan;
bool BLEDevice::initialized = false;
BLEScan *BLEDevice::scan = &fakeBleScan;
gap_event_handler BLEDevice::m_customGapHandler = nullptr;
void fakeArduinoDelayHook(uint32_t) {}

int16_t FakeWiFiClass::scanComplete() const {
    return state == ScanState::Running ? WIFI_SCAN_RUNNING :
           state == ScanState::Done ? static_cast<int16_t>(results.size()) : WIFI_SCAN_FAILED;
}
void FakeWiFiClass::scanDelete() { ++scanDeleteCalls; results.clear(); state = ScanState::Idle; }
int16_t FakeWiFiClass::scanNetworks(bool, bool, bool, uint32_t dwell, uint8_t channel,
                                    const char *, const uint8_t *) {
    ++scanStartCalls; lastDwellMs = dwell; lastChannel = channel;
    if (failNextStart) { failNextStart = false; return WIFI_SCAN_FAILED; }
    state = ScanState::Running; driverRunning = true; return WIFI_SCAN_RUNNING;
}
int16_t FakeWiFiClass::scanNetworks(bool async, bool hidden, bool passive,
                                    uint32_t dwell, uint8_t channel) {
    return scanNetworks(async, hidden, passive, dwell, channel, nullptr, nullptr);
}
const uint8_t *FakeWiFiClass::BSSID(int16_t index) const { return results[index].bssid.data(); }
int FakeWiFiClass::RSSI(int16_t index) const { return results[index].rssi; }
int FakeWiFiClass::channel(int16_t index) const { return results[index].channel; }
String FakeWiFiClass::SSID(int16_t index) const { return String(results[index].ssid); }
void FakeWiFiClass::reset() { *this = FakeWiFiClass{}; }
void FakeWiFiClass::complete(const std::vector<FakeWifiResult> &value) {
    results = value; state = ScanState::Done; driverRunning = false;
}
void FakeWiFiClass::fail() { state = ScanState::Failed; driverRunning = false; }
void FakeWiFiClass::startExternalScan() { state = ScanState::Running; driverRunning = true; }
void FakeWiFiClass::driverStop() {
    ++driverStopCalls; driverRunning = false; stoppedEventPending = true;
    stoppedAtMs = fakeMillis; state = ScanState::Failed;
}
void FakeWiFiClass::deliverStoppedScanDone() {
    stoppedEventPending = false; results.clear(); state = ScanState::Done;
}
int esp_wifi_scan_stop() { WiFi.driverStop(); return 0; }

#include "../rogue-radar/known_device_discovery.h"

using namespace rogue_radar;

namespace {
int checks;
int failures;

void check(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void resetRadioFakes() {
    fakeMillis = 100;
    WiFi.reset();
    fakeBleScan = BLEScan{};
    BLEDevice::initialized = false;
    BLEDevice::scan = &fakeBleScan;
}

FakeWifiResult wifi(uint8_t tail, int rssi, const char *ssid) {
    FakeWifiResult value{{0x02, 0, 0, 0, 0, tail}, rssi, 6, ssid};
    return value;
}

void testOwnerAndDelayedWifiDrain() {
    resetRadioFakes();
    check(knownDiscoveryStart("nearby", KnownRadio::Wifi), "first owner acquires discovery");
    check(!knownDiscoveryStart("saved", KnownRadio::Wifi), "second owner is rejected");
    knownDiscoveryPoll("saved");
    check(WiFi.scanStartCalls == 0, "non-owner cannot poll shared radio");
    knownDiscoveryPoll("nearby");
    check(WiFi.scanStartCalls == 1 && WiFi.lastDwellMs == 110,
          "owner starts asynchronous passive WiFi sweep");
    check(!knownDiscoveryStop("nearby") && WiFi.driverStopCalls == 1,
          "stop retains owner until delayed SCAN_DONE");
    check(!knownDiscoveryStart("saved", KnownRadio::Wifi),
          "new owner cannot enter during WiFi drain");
    WiFi.fail();
    check(!knownDiscoveryStop("nearby"), "negative scan status is not drain proof");
    WiFi.deliverStoppedScanDone();
    check(knownDiscoveryStop("nearby"), "nonnegative SCAN_DONE releases WiFi owner");
    check(knownDiscoveryStart("saved", KnownRadio::Wifi), "next owner starts after drain");
    check(knownDiscoveryStop("saved"), "idle WiFi owner stops synchronously");
}

void testWifiMailboxCoalescingAndBound() {
    resetRadioFakes();
    check(knownDiscoveryStart("nearby", KnownRadio::Wifi), "WiFi discovery starts");
    knownDiscoveryPoll("nearby");
    std::vector<FakeWifiResult> results;
    results.push_back(wifi(1, -80, "old"));
    results.push_back(wifi(1, -40, "latest"));
    for (uint8_t i = 2; i <= 7; ++i) results.push_back(wifi(i, -40 - i, "AP"));
    WiFi.complete(results);
    knownDiscoveryPoll("nearby");
    KnownDiscoveryObservation observation;
    unsigned count = 0, firstBatch = 0, secondBatch = 0;
    bool sawLatest = false;
    while (knownDiscoveryTake("nearby", observation)) {
        ++count; ++firstBatch;
        if (std::strcmp(observation.address.address, "02:00:00:00:00:01") == 0) {
            sawLatest = observation.address.lastRssi == -40 &&
                        std::strcmp(observation.address.advertisedName, "latest") == 0;
        }
    }
    check(firstBatch <= 6, "first WiFi result batch never exceeds six observations");
    knownDiscoveryPoll("nearby");
    while (knownDiscoveryTake("nearby", observation)) { ++count; ++secondBatch; }
    check(secondBatch <= 6, "second WiFi result batch never exceeds six observations");
    check(count == 7, "all seven unique WiFi identities arrive across bounded batches");
    check(knownDiscoveryBackend.dropped() == 0,
          "draining each WiFi batch avoids mailbox drops");
    check(sawLatest, "duplicate WiFi identity replaces queued observation");
    check(!knownDiscoveryTake("saved", observation), "non-owner cannot consume mailbox");
    check(knownDiscoveryStop("nearby"), "ready WiFi discovery stops");
}

BLEAdvertisedDevice ble(uint8_t tail, int rssi, uint8_t type) {
    return BLEAdvertisedDevice({{0xaa, 0xbb, 0xcc, 0xdd, 0xee, tail}}, rssi)
        .setAddressType(type);
}

void finishBleStop(const char *owner) {
    BLEDevice::emitScanStopComplete();
    for (int i = 0; i < 4 && !knownDiscoveryStop(owner); ++i) {}
}

void testBleGapFenceMetadataAndCoalescing() {
    resetRadioFakes();
    check(knownDiscoveryStart("nearby", KnownRadio::Ble), "BLE owner starts");
    check(fakeBleScan.wantDuplicates && fakeBleScan.shouldParse,
          "BLE discovery requests parsed duplicate advertisements");

    std::string manufacturer(40, '\x5a');
    manufacturer[0] = '\x4c'; manufacturer[1] = '\x00';
    BLEAdvertisedDevice first = ble(1, -80, 1).setName("Band 7")
        .setManufacturerData(manufacturer)
        .addServiceUuid("12345678-1234-1234-1234-1234567890ab")
        .addServiceUuid("87654321-4321-4321-4321-ba0987654321")
        .addServiceUuid("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")
        .addServiceUuid("ffffffff-1111-2222-3333-444444444444");
    fakeBleScan.callbacks->onResult(first);
    BLEAdvertisedDevice latest = first;
    latest = ble(1, -42, 1).setName("Band 7").setManufacturerData(manufacturer);
    fakeBleScan.callbacks->onResult(latest);
    fakeBleScan.callbacks->onResult(ble(1, -50, 0));
    for (uint8_t i = 2; i <= 6; ++i) fakeBleScan.callbacks->onResult(ble(i, -50, 1));

    check(knownDiscoveryBackend.dropped() == 1,
          "BLE mailbox bounds identities while coalescing exact type/address");
    KnownDiscoveryObservation observation;
    unsigned count = 0;
    bool sawLatest = false, sawOtherType = false, sawTruncated = false;
    while (knownDiscoveryTake("nearby", observation)) {
        ++count;
        if (std::strcmp(observation.address.address, "aa:bb:cc:dd:ee:01") == 0 &&
            observation.address.addressType == 1) {
            sawLatest = observation.address.lastRssi == -42;
            sawTruncated = observation.address.metadataTruncated &&
                           observation.address.manufacturerId == 0x004c &&
                           std::strlen(observation.address.manufacturerData) == 64;
        }
        if (std::strcmp(observation.address.address, "aa:bb:cc:dd:ee:01") == 0 &&
            observation.address.addressType == 0) sawOtherType = true;
    }
    check(count == 6 && sawLatest, "BLE duplicate replaces queued exact identity");
    check(sawOtherType, "same BLE address with different address type stays separate");
    check(sawTruncated, "manufacturer metadata is bounded and marked truncated");

    check(!knownDiscoveryStop("nearby"), "manual BLE stop awaits GAP acknowledgement");
    fakeBleScan.complete();
    check(!knownDiscoveryStop("nearby"),
          "inquiry-complete callback cannot release manual stop owner");
    check(!knownDiscoveryStart("saved", KnownRadio::Ble),
          "next owner remains blocked before GAP stop acknowledgement");
    finishBleStop("nearby");
    check(!knownDiscoveryBackend.active(), "GAP acknowledgement drains BLE backend");
    check(knownDiscoveryStart("saved", KnownRadio::Ble), "BLE ownership transfers after drain");
    check(!knownDiscoveryStop("saved"), "second BLE owner begins asynchronous stop");
    finishBleStop("saved");
}
}  // namespace

int main() {
    testOwnerAndDelayedWifiDrain();
    testWifiMailboxCoalescingAndBound();
    testBleGapFenceMetadataAndCoalescing();
    if (failures) {
        std::cerr << failures << " of " << checks << " discovery checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: " << checks << " known-device discovery checks\n";
    return EXIT_SUCCESS;
}
