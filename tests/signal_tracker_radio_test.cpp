#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "radio_stubs/Arduino.h"
#include "radio_stubs/WiFi.h"
#include "radio_stubs/BLEDevice.h"

uint32_t fakeMillis = 0;
static uint32_t fakeScanDoneDelayMs = 2;
static uint32_t fakeBleDoneDelayMs = 2;
static uint32_t fakeBleStopWaitMs = 0;
FakeWiFiClass WiFi;
bool BLEDevice::initialized = false;
static BLEScan fakeBleScan;
BLEScan *BLEDevice::scan = &fakeBleScan;
gap_event_handler BLEDevice::m_customGapHandler = nullptr;

int16_t FakeWiFiClass::scanComplete() const {
    if (state == ScanState::Running) return WIFI_SCAN_RUNNING;
    if (state == ScanState::Done) return static_cast<int16_t>(results.size());
    return WIFI_SCAN_FAILED;
}
void FakeWiFiClass::scanDelete() {
    ++scanDeleteCalls;
    results.clear();
    if (state == ScanState::Done) state = ScanState::Idle;
    // Match pinned Arduino 2.0.17: deleting results does not clear a running
    // scan's WIFI_SCANNING_BIT or _scanStarted state.
}
int16_t FakeWiFiClass::scanNetworks(bool, bool, bool, uint32_t dwellMs,
                                    uint8_t channel, const char *,
                                    const uint8_t *bssid) {
    if (state == ScanState::Running) return WIFI_SCAN_RUNNING;
    scanDelete();
    ++scanStartCalls;
    if (failNextStart) {
        failNextStart = false;
        state = ScanState::Idle;
        driverRunning = false;
        return WIFI_SCAN_FAILED;
    }
    lastDwellMs = dwellMs;
    lastChannel = channel;
    std::copy(bssid, bssid + 6, lastFilter.begin());
    state = ScanState::Running;
    driverRunning = true;
    return WIFI_SCAN_RUNNING;
}
const uint8_t *FakeWiFiClass::BSSID(int16_t index) const {
    return index >= 0 && static_cast<std::size_t>(index) < results.size()
        ? results[static_cast<std::size_t>(index)].bssid.data() : nullptr;
}
int FakeWiFiClass::RSSI(int16_t index) const {
    return results[static_cast<std::size_t>(index)].rssi;
}
int FakeWiFiClass::channel(int16_t index) const {
    return results[static_cast<std::size_t>(index)].channel;
}
void FakeWiFiClass::reset() {
    state = ScanState::Idle;
    driverRunning = false;
    stoppedEventPending = false;
    failNextStart = false;
    stoppedAtMs = 0;
    scanStartCalls = scanDeleteCalls = driverStopCalls = 0;
    lastChannel = 0;
    lastDwellMs = 0;
    lastFilter.fill(0);
    results.clear();
}
void FakeWiFiClass::complete(const std::vector<FakeWifiResult> &newResults) {
    results = newResults;
    driverRunning = false;
    stoppedEventPending = false;
    state = ScanState::Done;
}
void FakeWiFiClass::fail() {
    driverRunning = false;
    stoppedEventPending = false;
    state = ScanState::Failed;
}
void FakeWiFiClass::startExternalScan() {
    state = ScanState::Running;
    driverRunning = true;
}
void FakeWiFiClass::driverStop() {
    ++driverStopCalls;
    driverRunning = false;
    if (state == ScanState::Running || state == ScanState::Failed) {
        stoppedEventPending = true;
        stoppedAtMs = fakeMillis;
    }
}
void FakeWiFiClass::deliverStoppedScanDone() {
    if (!stoppedEventPending) return;
    stoppedEventPending = false;
    results.clear();
    state = ScanState::Done;
}
int esp_wifi_scan_stop() { WiFi.driverStop(); return 0; }
void fakeArduinoDelayHook(uint32_t delayedMs) {
    // Model a scan-done event arriving asynchronously two milliseconds after
    // esp_wifi_scan_stop(), rather than before stop() returns.
    if (WiFi.stoppedEventPending && fakeMillis - WiFi.stoppedAtMs >= fakeScanDoneDelayMs) {
        WiFi.deliverStoppedScanDone();
    }
    if (fakeBleScan.stopPending) {
        fakeBleStopWaitMs += delayedMs;
        if (fakeBleStopWaitMs >= fakeBleDoneDelayMs) {
            fakeBleScan.stopPending = false;
            BLEDevice::emitScanStopComplete();
        }
    }
}

#include "../rogue-radar/signal_tracker_radio.h"

namespace {
int checks = 0;
int failures = 0;
void check(bool condition, const std::string &message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
void resetFakes() {
    fakeMillis = 100;
    fakeScanDoneDelayMs = 2;
    fakeBleDoneDelayMs = 2;
    fakeBleStopWaitMs = 0;
    WiFi.reset();
    fakeBleScan = BLEScan();
    BLEDevice::initialized = false;
    BLEDevice::scan = &fakeBleScan;
}
const char *kTarget = "10:20:30:40:50:60";
std::array<uint8_t, 6> targetBytes() {
    return {{0x10, 0x20, 0x30, 0x40, 0x50, 0x60}};
}

void testAsyncWifiResultAndSample() {
    resetFakes();
    SignalTrackerRadio radio;
    check(radio.beginWifi(kTarget, 6), "WiFi async tracking starts");
    check(WiFi.scanStartCalls == 1, "one scan is started");
    check(WiFi.lastChannel == 6 && WiFi.lastDwellMs == 180,
          "preferred channel uses focused dwell");
    check(WiFi.lastFilter == targetBytes(), "BSSID filter reaches scan API");

    const std::array<uint8_t, 6> other = {{1, 2, 3, 4, 5, 6}};
    WiFi.complete({{other, -20, 1}, {targetBytes(), -61, 11}});
    radio.poll();
    int sample = 0;
    check(radio.takeSample(sample) && sample == -61,
          "completed async scan publishes matching BSSID RSSI");
    check(!radio.takeSample(sample), "sample mailbox is consume-once");
    check(std::strcmp(radio.status(), "Tracking WiFi") == 0,
          "successful scan updates status");
}

void testBusyExternalScanFailsWithoutOwnership() {
    resetFakes();
    WiFi.startExternalScan();
    SignalTrackerRadio radio;
    check(!radio.beginWifi(kTarget, 1), "foreign running scan reports busy");
    check(WiFi.driverStopCalls == 0,
          "begin does not cancel a scan it does not own");
    check(std::strcmp(radio.status(), "WiFi scanner busy") == 0,
          "busy failure has actionable status");
}

void testStopDrainsDelayedDoneBeforeReentry() {
    resetFakes();
    SignalTrackerRadio radio;
    check(radio.beginWifi(kTarget, 6), "first WiFi tracking starts");
    radio.stop();
    check(!WiFi.stoppedEventPending,
          "stop drains delayed scan-done event before returning");
    check(WiFi.scanComplete() != WIFI_SCAN_RUNNING,
          "stop leaves Arduino scanner out of running state");
    check(radio.beginWifi(kTarget, 11),
          "immediate WiFi reentry starts a new owned scan");
    check(WiFi.scanStartCalls == 2,
          "reentry starts a second scan rather than adopting old RUNNING state");
    check(WiFi.lastChannel == 11, "reentry uses new requested channel");
}

void testInitialStartFailureRetriesAfterDelay() {
    resetFakes();
    WiFi.failNextStart = true;
    SignalTrackerRadio radio;
    check(radio.beginWifi(kTarget, 6),
          "valid target survives transient initial scan start failure");
    check(std::strcmp(radio.status(), "WiFi scan retrying") == 0,
          "initial failure reports retrying");
    check(WiFi.scanStartCalls == 1, "initial failed start is attempted once");
    delay(599);
    radio.poll();
    check(WiFi.scanStartCalls == 1,
          "initial start failure waits full retry interval");
    delay(1);
    radio.poll();
    check(WiFi.scanStartCalls == 2 && WiFi.state == FakeWiFiClass::ScanState::Running,
          "initial start failure retries with a fresh async scan");
}
void testFailedScanWaitsForDelayedCompletion() {
    resetFakes();
    SignalTrackerRadio radio;
    check(radio.beginWifi(kTarget, 6), "WiFi tracking starts before failure");
    WiFi.fail();
    radio.poll();
    const int startsBeforeRetry = WiFi.scanStartCalls;
    delay(1);
    radio.poll();
    check(WiFi.scanStartCalls == startsBeforeRetry,
          "failed scan never restarts while canceled completion is pending");
    delay(1);
    radio.poll();
    check(WiFi.scanStartCalls == startsBeforeRetry + 1,
          "nonnegative delayed completion permits one fresh retry");
}

void testReadyToReleaseGatesLegacyScanner() {
    resetFakes();
    fakeScanDoneDelayMs = 1000;
    SignalTrackerRadio radio;
    check(radio.readyToRelease(),
          "readyToRelease is true when no canceled scan is owned");
    check(radio.beginWifi(kTarget, 6), "WiFi starts before release gate test");
    radio.stop();
    const int deletesBeforeReady = WiFi.scanDeleteCalls;
    check(!radio.readyToRelease(),
          "readyToRelease stays false while completion is absent");
    check(WiFi.scanDeleteCalls == deletesBeforeReady,
          "false release check does not delete driver-owned scan state");

    WiFi.deliverStoppedScanDone();
    check(radio.readyToRelease(),
          "readyToRelease becomes true only after nonnegative completion");
    check(WiFi.scanDeleteCalls == deletesBeforeReady + 1,
          "successful release check consumes canceled results exactly once");
    const int startsBeforeLegacy = WiFi.scanStartCalls;
    const std::array<uint8_t, 6> filter = targetBytes();
    check(WiFi.scanNetworks(true, true, true, 100, 0, nullptr,
                            filter.data()) == WIFI_SCAN_RUNNING,
          "legacy scanner can start after release is granted");
    check(WiFi.scanStartCalls == startsBeforeLegacy + 1,
          "legacy scan is new work rather than stale completion");
}
void testLongDelayedCompletionQueuesReentry() {
    resetFakes();
    fakeScanDoneDelayMs = 1000;
    SignalTrackerRadio radio;
    check(radio.beginWifi(kTarget, 6), "WiFi starts before long delayed stop");
    radio.stop();
    check(WiFi.stoppedEventPending,
          "bounded stop retains ownership when completion is delayed");
    const int startsBeforeReentry = WiFi.scanStartCalls;
    check(radio.beginWifi(kTarget, 11),
          "valid reentry request queues while owned cleanup is pending");
    check(WiFi.scanStartCalls == startsBeforeReentry,
          "queued reentry does not call scanNetworks while pending");
    check(std::strcmp(radio.status(), "WiFi scan cleanup pending") == 0,
          "queued reentry exposes cleanup status");
    radio.poll();
    check(WiFi.scanStartCalls == startsBeforeReentry,
          "poll still does not adopt stale RUNNING state");
    WiFi.deliverStoppedScanDone();
    radio.poll();
    check(WiFi.scanStartCalls == startsBeforeReentry + 1,
          "delayed nonnegative completion starts queued target");
    check(WiFi.lastChannel == 11,
          "queued target preserves requested channel");
}

void testBleLifecycleUsesCallbackWithoutStoredResults() {
    resetFakes();
    SignalTrackerRadio radio;
    check(radio.beginBle(kTarget), "BLE tracking starts");
    check(fakeBleScan.running && fakeBleScan.lastDuration == 0,
          "BLE scan is infinite");
    check(!fakeBleScan.activeScan, "BLE tracking is passive");
    check(fakeBleScan.wantDuplicates && !fakeBleScan.shouldParse,
          "BLE callback receives duplicates without payload parsing/storage");
    check(fakeBleScan.callbacks != nullptr, "BLE callback is attached");
    radio.stop();
    check(!fakeBleScan.running && fakeBleScan.callbacks == nullptr,
          "BLE stop halts scan and detaches callback");
    check(fakeBleScan.activeScan && fakeBleScan.interval == 150 &&
              fakeBleScan.window == 140,
          "BLE stop restores normal scanner settings");
}

void testBleReleaseWaitsForInquiryCompletion() {
    resetFakes();
    fakeBleDoneDelayMs = 1000;
    SignalTrackerRadio radio;
    check(radio.beginBle(kTarget), "BLE starts before delayed release test");
    radio.stop();
    check(!radio.readyToRelease(),
          "BLE release remains pending before GAP stop acknowledgement");
    check(std::strcmp(radio.status(), "BLE scan cleanup pending") == 0,
          "pending BLE completion has actionable status");
    check(!radio.beginBle(kTarget),
          "BLE reentry cannot overwrite pending completion callback");
    BLEDevice::emitScanStopComplete();
    check(!radio.readyToRelease(), "first completed idle pass retains owner");
    check(radio.readyToRelease(), "second completed idle pass releases owner");
    check(radio.beginBle(kTarget), "BLE can restart after completion is consumed");
    fakeBleDoneDelayMs = 2;
    fakeBleStopWaitMs = 0;
    radio.stop();
}

void testBleStopFailureRetainsOwnership() {
    resetFakes();
    fakeBleDoneDelayMs = 1000;
    SignalTrackerRadio radio;
    check(radio.beginBle(kTarget), "BLE starts before stop failure test");
    radio.stop();
    BLEDevice::emitScanStopComplete(1);
    check(!radio.readyToRelease(), "failed BLE stop cannot release scanner");
    check(std::strcmp(radio.status(), "BLE stop failed; restart required") == 0,
          "failed BLE stop exposes restart-required status");
    SignalTrackerRadio other;
    check(!other.beginBle(kTarget),
          "failed BLE stop prevents a new tracker owner");
}
}  // namespace

int main() {
    testAsyncWifiResultAndSample();
    testBusyExternalScanFailsWithoutOwnership();
    testStopDrainsDelayedDoneBeforeReentry();
    testInitialStartFailureRetriesAfterDelay();
    testFailedScanWaitsForDelayedCompletion();
    testReadyToReleaseGatesLegacyScanner();
    testLongDelayedCompletionQueuesReentry();
    testBleLifecycleUsesCallbackWithoutStoredResults();
    testBleReleaseWaitsForInquiryCompletion();
    testBleStopFailureRetainsOwnership();
    if (failures != 0) {
        std::cerr << failures << " of " << checks << " radio checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: " << checks << " SignalTrackerRadio checks\n";
    return EXIT_SUCCESS;
}
