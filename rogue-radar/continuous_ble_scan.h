#pragma once

#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include "ble_scan_stop_fence.h"

// Owns the shared Arduino BLE scanner for the live detector pages. The
// advertised-device callback is invoked on the BLE task; callers must copy
// data into bounded storage and defer UI work to poll() on the Arduino loop.
class ContinuousBleScan {
 public:
  using ResultCallback = void (*)(BLEAdvertisedDevice &device, void *context);

  ContinuousBleScan() : callbacks_(this) {}

  ContinuousBleScan(const ContinuousBleScan &) = delete;
  ContinuousBleScan &operator=(const ContinuousBleScan &) = delete;

  bool begin(ResultCallback callback, void *context) {
    return begin(0, callback, context);
  }

  bool begin(uint32_t durationSeconds, ResultCallback callback, void *context) {
    if (state_ != State::Idle || callback == nullptr) return false;
    if (callbackOwner() != nullptr) {
      status_ = "BLE scanner busy";
      return false;
    }

    if (!BLEDevice::getInitialized()) BLEDevice::init("");
    if (!BleScanStopFence::acquire(this)) {
      status_ = "BLE scanner busy";
      return false;
    }
    scan_ = BLEDevice::getScan();
    if (scan_ == nullptr) {
      BleScanStopFence::release(this);
      status_ = "BLE scanner unavailable";
      return false;
    }

    resultCallback_ = callback;
    resultContext_ = context;
    completionSeen_ = false;
    completed_ = false;
    idlePasses_ = 0;
    callbackOwner() = this;

    scan_->clearResults();
    scan_->setAdvertisedDeviceCallbacks(&callbacks_, true, true);
    scan_->setActiveScan(true);
    scan_->setInterval(150);
    scan_->setWindow(140);

    setAccepting(true);
    if (!scan_->start(durationSeconds, scanCompleted, false)) {
      setAccepting(false);
      scan_->setAdvertisedDeviceCallbacks(nullptr, false, true);
      callbackOwner() = nullptr;
      BleScanStopFence::release(this);
      scan_ = nullptr;
      resultCallback_ = nullptr;
      resultContext_ = nullptr;
      status_ = "BLE scan failed";
      return false;
    }

    state_ = State::Scanning;
    status_ = "Scanning";
    return true;
  }

  void poll() {
    if (state_ == State::Scanning && completionWasSeen()) {
      setAccepting(false);
      scan_->setAdvertisedDeviceCallbacks(nullptr, false, true);
      state_ = State::Stopping;
      naturalCompletion_ = true;
      idlePasses_ = 0;
      status_ = "BLE scan complete";
    }
    if (state_ == State::Stopping) finishStopIfReady();
  }

  // Returns true only after the pinned BLE stack acknowledges a manual stop
  // (or reports natural completion) and entered callbacks have drained.
  bool stop() {
    if (state_ == State::Idle) return true;

    if (state_ == State::Scanning) {
      setAccepting(false);
      state_ = State::Stopping;
      naturalCompletion_ = false;
      idlePasses_ = 0;
      BleScanStopFence::requestStop(this);
      scan_->stop();
      scan_->setAdvertisedDeviceCallbacks(nullptr, false, true);
      status_ = "Stopping BLE scan";
    }

    return finishStopIfReady();
  }

  bool active() const { return state_ == State::Scanning; }
  bool running() const { return state_ == State::Scanning; }
  bool completed() const { return completed_; }
  bool stopFailed() {
    return state_ == State::Stopping &&
           BleScanStopFence::stopResult(this) == BleScanStopFence::StopResult::Failed;
  }
  const char *status() const { return status_; }

 private:
  enum class State : uint8_t { Idle, Scanning, Stopping };
  class Callbacks : public BLEAdvertisedDeviceCallbacks {
   public:
    explicit Callbacks(ContinuousBleScan *owner) : owner_(owner) {}
    void onResult(BLEAdvertisedDevice device) override { owner_->onResult(device); }

   private:
    ContinuousBleScan *owner_;
  };

  static ContinuousBleScan *&callbackOwner() {
    static ContinuousBleScan *owner = nullptr;
    return owner;
  }

  static void scanCompleted(BLEScanResults) {
    ContinuousBleScan *owner = callbackOwner();
    if (owner == nullptr) return;
    portENTER_CRITICAL(&owner->mux_);
    owner->completionSeen_ = true;
    portEXIT_CRITICAL(&owner->mux_);
  }

  void onResult(BLEAdvertisedDevice &device) {
    bool accept;
    portENTER_CRITICAL(&mux_);
    ++callbacksActive_;
    accept = accepting_ && state_ == State::Scanning;
    portEXIT_CRITICAL(&mux_);

    if (accept && resultCallback_ != nullptr) {
      resultCallback_(device, resultContext_);
    }

    portENTER_CRITICAL(&mux_);
    --callbacksActive_;
    portEXIT_CRITICAL(&mux_);
  }

  void setAccepting(bool accepting) {
    portENTER_CRITICAL(&mux_);
    accepting_ = accepting;
    portEXIT_CRITICAL(&mux_);
  }

  bool completionWasSeen() {
    portENTER_CRITICAL(&mux_);
    const bool seen = completionSeen_;
    portEXIT_CRITICAL(&mux_);
    return seen;
  }

  bool finishStopIfReady() {
    if (state_ == State::Idle) return true;
    if (state_ != State::Stopping) return false;

    uint16_t activeCallbacks;
    portENTER_CRITICAL(&mux_);
    activeCallbacks = callbacksActive_;
    portEXIT_CRITICAL(&mux_);

    idlePasses_ = activeCallbacks == 0 ? static_cast<uint8_t>(idlePasses_ + 1) : 0;
    if (naturalCompletion_) {
      if (!completionWasSeen()) return false;
    } else {
      const auto result = BleScanStopFence::stopResult(this);
      if (result == BleScanStopFence::StopResult::Failed) {
        status_ = "BLE stop failed; restart required";
        return false;
      }
      // A manual request must consume its own GAP stop acknowledgement even
      // if a natural inquiry-complete callback raced with stop().
      if (result != BleScanStopFence::StopResult::Succeeded) return false;
    }
    if (idlePasses_ < 2) return false;

    if (scan_ != nullptr) {
      scan_->clearResults();
      scan_->setAdvertisedDeviceCallbacks(nullptr, false, true);
      scan_->setActiveScan(true);
      scan_->setInterval(150);
      scan_->setWindow(140);
    }
    if (callbackOwner() == this) callbackOwner() = nullptr;
    BleScanStopFence::release(this);
    scan_ = nullptr;
    resultCallback_ = nullptr;
    resultContext_ = nullptr;
    state_ = State::Idle;
    completed_ = naturalCompletion_;
    naturalCompletion_ = false;
    status_ = "Stopped";
    return true;
  }

  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  State state_ = State::Idle;
  BLEScan *scan_ = nullptr;
  ResultCallback resultCallback_ = nullptr;
  void *resultContext_ = nullptr;
  Callbacks callbacks_;
  bool accepting_ = false;
  uint16_t callbacksActive_ = 0;
  volatile bool completionSeen_ = false;
  bool completed_ = false;
  bool naturalCompletion_ = false;
  uint8_t idlePasses_ = 0;
  const char *status_ = "Idle";
};
