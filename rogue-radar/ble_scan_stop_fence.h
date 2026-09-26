#pragma once

#include <Arduino.h>
#include <BLEDevice.h>

// Arduino-ESP32 2.0.x BLEScan::stop() returns before Bluedroid confirms that
// scanning stopped. Its scan-complete callback is not reliably invoked for a
// manual stop, so radio ownership must instead be fenced by GAP's explicit
// SCAN_STOP_COMPLETE event.
//
// BLEDevice exposes one process-wide custom GAP hook. This dispatcher keeps a
// single scan owner and chains the handler that was installed before it. It is
// intentionally left installed after release so alternating scanner adapters
// cannot create a handler-chaining cycle.
class BleScanStopFence {
 public:
  enum class StopResult : uint8_t { NotRequested, Pending, Succeeded, Failed };

  static bool acquire(void *owner) {
    if (owner == nullptr) return false;
    install();
    bool acquired = false;
    portENTER_CRITICAL(&mux());
    if (activeOwner() == nullptr || activeOwner() == owner) {
      activeOwner() = owner;
      result() = StopResult::NotRequested;
      acquired = true;
    }
    portEXIT_CRITICAL(&mux());
    return acquired;
  }

  // Arm before BLEScan::stop(), because the GAP acknowledgement may arrive on
  // the Bluetooth task immediately after esp_ble_gap_stop_scanning().
  static void requestStop(void *owner) {
    portENTER_CRITICAL(&mux());
    if (activeOwner() == owner) result() = StopResult::Pending;
    portEXIT_CRITICAL(&mux());
  }

  static StopResult stopResult(void *owner) {
    portENTER_CRITICAL(&mux());
    const StopResult value = activeOwner() == owner
        ? result() : StopResult::NotRequested;
    portEXIT_CRITICAL(&mux());
    return value;
  }

  static void release(void *owner) {
    portENTER_CRITICAL(&mux());
    if (activeOwner() == owner) {
      activeOwner() = nullptr;
      result() = StopResult::NotRequested;
    }
    portEXIT_CRITICAL(&mux());
  }

 private:
  static void install() {
    if (BLEDevice::m_customGapHandler == gapHandler) return;
    previousHandler() = BLEDevice::m_customGapHandler;
    BLEDevice::setCustomGapHandler(gapHandler);
  }

  static void gapHandler(esp_gap_ble_cb_event_t event,
                         esp_ble_gap_cb_param_t *param) {
    if (event == ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT) {
      portENTER_CRITICAL(&mux());
      if (activeOwner() != nullptr && result() == StopResult::Pending) {
        result() = param != nullptr &&
                          param->scan_stop_cmpl.status == ESP_BT_STATUS_SUCCESS
                      ? StopResult::Succeeded : StopResult::Failed;
      }
      portEXIT_CRITICAL(&mux());
    }
    gap_event_handler previous = previousHandler();
    if (previous != nullptr) previous(event, param);
  }

  static portMUX_TYPE &mux() {
    static portMUX_TYPE value = portMUX_INITIALIZER_UNLOCKED;
    return value;
  }
  static void *&activeOwner() {
    static void *value = nullptr;
    return value;
  }
  static StopResult &result() {
    static StopResult value = StopResult::NotRequested;
    return value;
  }
  static gap_event_handler &previousHandler() {
    static gap_event_handler value = nullptr;
    return value;
  }
};
