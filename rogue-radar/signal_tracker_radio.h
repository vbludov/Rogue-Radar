#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <ctype.h>
#include <string.h>
#include "ble_scan_stop_fence.h"

// Owns the Arduino Wi-Fi or BLE scanner while signal tracking is active.
// Call poll() regularly from the Arduino loop and stop() before leaving the
// tracker. Scanner callbacks only update the small, synchronized RSSI mailbox.
class SignalTrackerRadio {
 public:
  SignalTrackerRadio() : bleCallbacks_(this) {}
  ~SignalTrackerRadio() { stop(); }

  SignalTrackerRadio(const SignalTrackerRadio &) = delete;
  SignalTrackerRadio &operator=(const SignalTrackerRadio &) = delete;

  bool beginWifi(const char *mac, uint8_t channel) {
    stop();
    if (!parseMac(mac, targetMac_)) {
      status_ = "Invalid WiFi BSSID";
      return false;
    }

    if (!wifiDrainPending_ && WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
      status_ = "WiFi scanner busy";
      return false;
    }

    preferredChannel_ = validWifiChannel(channel) ? channel : 0;
    focusedScans_ = 0;
    mode_ = Mode::Wifi;
    nextWifiScanMs_ = 0;
    clearSample();
    if (wifiDrainPending_) {
      // Keep the validated request queued. poll() will start it as soon as the
      // canceled scan's delayed SCAN_DONE event has been consumed.
      status_ = "WiFi scan cleanup pending";
      return true;
    }
    if (!startWifiScan()) {
      status_ = "WiFi scan retrying";
      nextWifiScanMs_ = millis() + kWifiRetryMs;
    }
    return true;
  }

  bool beginBle(const char *mac) {
    stop();
    if (bleDrainPending_ && !drainBleScan(0)) {
      status_ = "BLE scan cleanup pending";
      return false;
    }
    if (!parseMac(mac, targetMac_)) {
      status_ = "Invalid BLE address";
      return false;
    }

    // The tracker UI normally initializes BLE first. BLEDevice::init() is
    // idempotent in the pinned Arduino BLE library, so this also makes the
    // adapter safe when it is used independently.
    if (!BLEDevice::getInitialized()) BLEDevice::init("");
    bleScan_ = BLEDevice::getScan();
    if (bleScan_ == nullptr) {
      status_ = "BLE scanner unavailable";
      return false;
    }
    if (!BleScanStopFence::acquire(this)) {
      bleScan_ = nullptr;
      status_ = "BLE scanner busy";
      return false;
    }

    clearSample();
    mode_ = Mode::Ble;
    bleIdlePasses_ = 0;
    setAcceptingBle(true);
    bleScan_->clearResults();
    bleScan_->setAdvertisedDeviceCallbacks(&bleCallbacks_, true, false);
    bleScan_->setActiveScan(false);
    bleScan_->setInterval(120);
    bleScan_->setWindow(100);

    if (!bleScan_->start(0, nullptr, false)) {
      setAcceptingBle(false);
      restoreBleScanner();
      BleScanStopFence::release(this);
      mode_ = Mode::Idle;
      status_ = "BLE scan failed";
      return false;
    }

    status_ = "Tracking BLE";
    return true;
  }

  void poll() {
    if (mode_ != Mode::Wifi) return;

    if (wifiDrainPending_) {
      if (!drainWifiScan(0)) return;
      status_ = "Starting WiFi scan";
      nextWifiScanMs_ = millis();
    }

    if (wifiScanInFlight_) {
      const int16_t resultCount = WiFi.scanComplete();
      if (resultCount == WIFI_SCAN_RUNNING) return;

      bool found = false;
      if (resultCount >= 0) {
        wifiScanInFlight_ = false;
        for (int16_t i = 0; i < resultCount; ++i) {
          const uint8_t *bssid = WiFi.BSSID(i);
          if (bssid != nullptr && memcmp(bssid, targetMac_, 6) == 0) {
            publishSample(WiFi.RSSI(i));
            const int32_t foundChannel = WiFi.channel(i);
            if (validWifiChannel(foundChannel)) {
              preferredChannel_ = static_cast<uint8_t>(foundChannel);
            }
            found = true;
          }
        }
        WiFi.scanDelete();
        status_ = found ? "Tracking WiFi" : "WiFi target not seen";
        nextWifiScanMs_ = millis() + kWifiScanGapMs;
      } else {
        // The pinned WiFi library can report a timeout before its delayed
        // SCAN_DONE event clears _scanStarted. Keep ownership until that event
        // is processed so a stale completion cannot corrupt the next scan.
        cancelWifiScan();
        status_ = "WiFi scan retrying";
      }
    }

    if (!wifiScanInFlight_ && !wifiDrainPending_ && timeReached(nextWifiScanMs_)) {
      if (!startWifiScan()) {
        status_ = "WiFi scan retrying";
        nextWifiScanMs_ = millis() + kWifiRetryMs;
      }
    }
  }

  bool takeSample(int &rssi) {
    bool ready;
    portENTER_CRITICAL(&mux_);
    ready = sampleReady_;
    if (ready) {
      rssi = sampleRssi_;
      sampleReady_ = false;
    }
    portEXIT_CRITICAL(&mux_);
    return ready;
  }

  void stop() {
    const Mode stoppingMode = mode_;
    if (stoppingMode == Mode::Wifi && wifiScanInFlight_) {
      cancelWifiScan();
    } else if (stoppingMode == Mode::Ble && bleScan_ != nullptr) {
      setAcceptingBle(false);
      bleDrainPending_ = true;
      bleIdlePasses_ = 0;
      BleScanStopFence::requestStop(this);
      bleScan_->stop();
      // Detach the callback before waiting so no new callback can retain this
      // object's address. Existing callbacks only touch the RSSI mailbox.
      bleScan_->setAdvertisedDeviceCallbacks(nullptr, false, true);
      waitForBleCallbacks();
    }

    // esp_wifi_scan_stop() returns before Arduino's queued SCAN_DONE handler
    // clears WIFI_SCANNING_BIT/_scanStarted. Give that handler a short bounded
    // drain, but retain pending ownership if it has not arrived yet.
    if (wifiDrainPending_) drainWifiScan(kWifiStopDrainMs);
    if (bleDrainPending_) drainBleScan(kBleDrainTimeoutMs);

    mode_ = Mode::Idle;
    preferredChannel_ = 0;
    focusedScans_ = 0;
    nextWifiScanMs_ = 0;
    clearSample();
    status_ = wifiDrainPending_ ? "WiFi scan cleanup pending" :
              bleDrainPending_ &&
                      BleScanStopFence::stopResult(this) ==
                          BleScanStopFence::StopResult::Failed
                  ? "BLE stop failed; restart required" :
              bleDrainPending_ ? "BLE scan cleanup pending" : "Stopped";
  }

  const char *status() { return status_; }

  // The UI must retain scanner ownership after stop until Arduino consumes
  // a late SCAN_DONE event. Legacy tools may then safely start their own scan.
  bool readyToRelease() { return drainWifiScan(0) && drainBleScan(0); }

 private:
  enum class Mode : uint8_t { Idle, Wifi, Ble };

  class TrackerBleCallbacks : public BLEAdvertisedDeviceCallbacks {
   public:
    explicit TrackerBleCallbacks(SignalTrackerRadio *owner) : owner_(owner) {}

    void onResult(BLEAdvertisedDevice device) override {
      owner_->onBleResult(device);
    }

   private:
    SignalTrackerRadio *owner_;
  };

  static constexpr uint8_t kFocusedScansBeforeSweep = 5;
  static constexpr uint32_t kWifiScanGapMs = 220;
  static constexpr uint32_t kWifiRetryMs = 600;
  static constexpr uint32_t kWifiStopDrainMs = 120;
  static constexpr uint32_t kBleDrainTimeoutMs = 100;

  static bool validWifiChannel(int32_t channel) {
    return channel >= 1 && channel <= 14;
  }

  static bool timeReached(uint32_t deadline) {
    return static_cast<int32_t>(millis() - deadline) >= 0;
  }

  static int8_t hexValue(char c) {
    if (c >= '0' && c <= '9') return static_cast<int8_t>(c - '0');
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') return static_cast<int8_t>(c - 'a' + 10);
    return -1;
  }

  static bool parseMac(const char *text, uint8_t out[6]) {
    if (text == nullptr || strlen(text) != 17) return false;
    for (uint8_t i = 0; i < 6; ++i) {
      const int8_t high = hexValue(text[i * 3]);
      const int8_t low = hexValue(text[i * 3 + 1]);
      if (high < 0 || low < 0) return false;
      out[i] = static_cast<uint8_t>((high << 4) | low);
      if (i < 5 && text[i * 3 + 2] != ':' && text[i * 3 + 2] != '-') return false;
    }
    return text[17] == '\0';
  }

  bool startWifiScan() {
    if (mode_ != Mode::Wifi || wifiDrainPending_) return false;

    uint8_t scanChannel = 0;
    if (preferredChannel_ != 0 && focusedScans_ < kFocusedScansBeforeSweep) {
      scanChannel = preferredChannel_;
      ++focusedScans_;
    } else {
      focusedScans_ = 0;
    }

    const uint32_t dwellMs = scanChannel == 0 ? 100 : 180;
    const int16_t result = WiFi.scanNetworks(true, true, true, dwellMs,
                                             scanChannel, nullptr, targetMac_);
    if (result == WIFI_SCAN_RUNNING || result >= 0) {
      wifiScanInFlight_ = true;
      status_ = scanChannel == 0 ? "Sweeping WiFi channels" : "Tracking WiFi";
      return true;
    }

    wifiScanInFlight_ = false;
    status_ = "WiFi scan failed";
    nextWifiScanMs_ = millis() + kWifiRetryMs;
    return false;
  }

  void cancelWifiScan() {
    if (!wifiScanInFlight_) return;
    // Do not call scanDelete() here. The Arduino library still owns an active
    // scan until its asynchronous SCAN_DONE event runs _scanDone().
    esp_wifi_scan_stop();
    wifiScanInFlight_ = false;
    wifiDrainPending_ = true;
  }

  bool drainWifiScan(uint32_t timeoutMs) {
    if (!wifiDrainPending_) return true;
    const uint32_t deadline = millis() + timeoutMs;
    do {
      // Only a non-negative result proves Arduino's SCAN_DONE handler ran.
      // WIFI_SCAN_FAILED is ambiguous because scanComplete() itself clears
      // WIFI_SCANNING_BIT on timeout without resetting _scanStarted.
      if (WiFi.scanComplete() >= 0) {
        WiFi.scanDelete();
        wifiDrainPending_ = false;
        return true;
      }
      if (timeoutMs == 0 || timeReached(deadline)) break;
      delay(1);
    } while (true);
    return false;
  }

  void onBleResult(BLEAdvertisedDevice &device) {
    bool accept;
    portENTER_CRITICAL(&mux_);
    ++bleCallbacksActive_;
    accept = acceptingBle_ && mode_ == Mode::Ble;
    portEXIT_CRITICAL(&mux_);

    if (accept) {
      BLEAddress address = device.getAddress();
      const uint8_t *native = *address.getNative();
      if (native != nullptr && memcmp(native, targetMac_, 6) == 0) {
        publishSample(device.getRSSI());
      }
    }

    portENTER_CRITICAL(&mux_);
    --bleCallbacksActive_;
    portEXIT_CRITICAL(&mux_);
  }

  void publishSample(int rssi) {
    portENTER_CRITICAL(&mux_);
    sampleRssi_ = rssi;
    sampleReady_ = true;
    portEXIT_CRITICAL(&mux_);
  }

  void clearSample() {
    portENTER_CRITICAL(&mux_);
    sampleReady_ = false;
    portEXIT_CRITICAL(&mux_);
  }

  void setAcceptingBle(bool accepting) {
    portENTER_CRITICAL(&mux_);
    acceptingBle_ = accepting;
    portEXIT_CRITICAL(&mux_);
  }

  void waitForBleCallbacks() {
    const uint32_t deadline = millis() + kBleDrainTimeoutMs;
    uint8_t idlePasses = 0;
    while (true) {
      uint16_t active;
      portENTER_CRITICAL(&mux_);
      active = bleCallbacksActive_;
      portEXIT_CRITICAL(&mux_);
      // Require two idle scheduler passes. A GAP event may have observed the
      // old callback pointer immediately before stop() detached it but not yet
      // entered onResult(), where the active count is incremented.
      idlePasses = active == 0 ? static_cast<uint8_t>(idlePasses + 1) : 0;
      if (idlePasses >= 2 || timeReached(deadline)) return;
      delay(1);
    }
  }

  bool drainBleScan(uint32_t timeoutMs) {
    if (!bleDrainPending_) return true;
    const uint32_t deadline = millis() + timeoutMs;
    do {
      uint16_t active;
      portENTER_CRITICAL(&mux_);
      active = bleCallbacksActive_;
      portEXIT_CRITICAL(&mux_);

      const auto stopResult = BleScanStopFence::stopResult(this);
      if (stopResult == BleScanStopFence::StopResult::Failed) {
        status_ = "BLE stop failed; restart required";
        return false;
      }
      bleIdlePasses_ = stopResult == BleScanStopFence::StopResult::Succeeded && active == 0
          ? static_cast<uint8_t>(bleIdlePasses_ + 1) : 0;
      if (bleIdlePasses_ >= 2) {
        restoreBleScanner();
        BleScanStopFence::release(this);
        bleDrainPending_ = false;
        bleIdlePasses_ = 0;
        return true;
      }
      if (timeoutMs == 0 || timeReached(deadline)) break;
      delay(1);
    } while (true);
    return false;
  }

  void restoreBleScanner() {
    if (bleScan_ == nullptr) return;
    bleScan_->clearResults();
    // These are the settings used by Rogue Radar's regular BLE scans. The
    // pinned BLE API exposes no getters for preserving arbitrary prior values.
    bleScan_->setAdvertisedDeviceCallbacks(nullptr, false, true);
    bleScan_->setActiveScan(true);
    bleScan_->setInterval(150);
    bleScan_->setWindow(140);
    bleScan_ = nullptr;
  }

  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  Mode mode_ = Mode::Idle;
  uint8_t targetMac_[6] = {};
  bool sampleReady_ = false;
  int sampleRssi_ = 0;
  const char *status_ = "Idle";

  bool wifiScanInFlight_ = false;
  bool wifiDrainPending_ = false;
  uint8_t preferredChannel_ = 0;
  uint8_t focusedScans_ = 0;
  uint32_t nextWifiScanMs_ = 0;

  BLEScan *bleScan_ = nullptr;
  bool acceptingBle_ = false;
  uint16_t bleCallbacksActive_ = 0;
  bool bleDrainPending_ = false;
  uint8_t bleIdlePasses_ = 0;
  TrackerBleCallbacks bleCallbacks_;
};
