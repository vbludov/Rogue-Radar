#include <cstdlib>
#include <cstring>
#include <iostream>

#include "radio_stubs/Arduino.h"
#include "radio_stubs/BLEDevice.h"

uint32_t fakeMillis = 0;
static BLEScan fakeBleScan;
bool BLEDevice::initialized = false;
BLEScan *BLEDevice::scan = &fakeBleScan;
gap_event_handler BLEDevice::m_customGapHandler = nullptr;
void fakeArduinoDelayHook(uint32_t) {}

#include "../rogue-radar/continuous_ble_scan.h"

namespace {
int checks = 0;
int failures = 0;
int results = 0;
int priorGapEvents = 0;

void check(bool condition, const char *message) {
  ++checks;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

void resultCallback(BLEAdvertisedDevice &, void *) { ++results; }
void priorGapHandler(esp_gap_ble_cb_event_t, esp_ble_gap_cb_param_t *) {
  ++priorGapEvents;
}

void resetFake() {
  fakeMillis = 0;
  fakeBleScan = BLEScan{};
  BLEDevice::initialized = false;
  BLEDevice::scan = &fakeBleScan;
  BLEDevice::m_customGapHandler = priorGapHandler;
  results = 0;
  priorGapEvents = 0;
}

void testManualStopWaitsForStackCompletion() {
  resetFake();
  ContinuousBleScan scan;
  check(scan.begin(resultCallback, nullptr), "infinite scan begins");
  check(fakeBleScan.lastDuration == 0 && fakeBleScan.wantDuplicates &&
            fakeBleScan.shouldParse,
        "scan uses infinite parsed duplicate callbacks");
  fakeBleScan.callbacks->onResult(BLEAdvertisedDevice{});
  check(results == 1, "advertised result reaches owner");

  check(!scan.stop(), "manual stop remains pending before GAP acknowledgement");
  check(fakeBleScan.stopCalls == 1, "manual stop is issued once");
  check(!scan.stop() && fakeBleScan.stopCalls == 1,
        "repeated stop polls without issuing another driver stop");

  ContinuousBleScan other;
  check(!other.begin(resultCallback, nullptr),
        "new owner cannot replace pending completion callback");
  check(std::strcmp(other.status(), "BLE scanner busy") == 0,
        "pending owner reports scanner busy");

  fakeBleScan.complete();
  check(!scan.stop(), "inquiry completion cannot release a manual stop");
  BLEDevice::emitScanStopComplete();
  check(scan.stop(), "GAP stop acknowledgement releases owner after callback drain");
  check(priorGapEvents == 1, "pre-existing custom GAP handler is chained once");
  check(other.begin(resultCallback, nullptr), "next owner starts after release");
  other.stop();
  BLEDevice::emitScanStopComplete();
  other.stop();
  check(other.stop(), "next owner can also complete release lifecycle");
}

void testManualStopFailureIsExplicitAndRetainsOwner() {
  resetFake();
  ContinuousBleScan scan;
  check(scan.begin(resultCallback, nullptr), "scan begins before stop failure");
  check(!scan.stop(), "manual stop awaits failure acknowledgement");
  BLEDevice::emitScanStopComplete(1);
  check(!scan.stop(), "failed GAP stop cannot release scanner ownership");
  check(std::strcmp(scan.status(), "BLE stop failed; restart required") == 0,
        "failed GAP stop has explicit recovery status");
  ContinuousBleScan other;
  check(!other.begin(resultCallback, nullptr),
        "failed stop keeps a later owner from consuming stale state");
}

void testNaturalCompletionAndStartFailure() {
  resetFake();
  ContinuousBleScan scan;
  check(scan.begin(7, resultCallback, nullptr), "timed scan begins");
  check(fakeBleScan.lastDuration == 7, "timed duration reaches BLE API");
  fakeBleScan.complete();
  scan.poll();
  check(!scan.completed(), "natural completion waits for callback drain");
  scan.poll();
  check(scan.completed() && !scan.running(), "natural completion releases cleanly");

  fakeBleScan.startSucceeds = false;
  check(!scan.begin(resultCallback, nullptr), "start failure is reported");
  check(std::strcmp(scan.status(), "BLE scan failed") == 0,
        "start failure exposes status");
  ContinuousBleScan retry;
  fakeBleScan.startSucceeds = true;
  check(retry.begin(resultCallback, nullptr), "start failure releases singleton owner");
  retry.stop();
  BLEDevice::emitScanStopComplete();
  retry.stop(); retry.stop();
}
}  // namespace

int main() {
  testManualStopWaitsForStackCompletion();
  testNaturalCompletionAndStartFailure();
  testManualStopFailureIsExplicitAndRetainsOwner();
  if (failures) {
    std::cerr << failures << " of " << checks << " continuous-BLE checks failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "PASS: " << checks << " ContinuousBleScan checks\n";
  return EXIT_SUCCESS;
}
