#pragma once
#include "BLEScan.h"

enum esp_gap_ble_cb_event_t {
    ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT = 1,
    ESP_GAP_BLE_OTHER_EVT = 2
};
static const int ESP_BT_STATUS_SUCCESS = 0;
struct esp_ble_gap_cb_param_t {
    struct { int status = ESP_BT_STATUS_SUCCESS; } scan_stop_cmpl;
};
using gap_event_handler = void (*)(esp_gap_ble_cb_event_t, esp_ble_gap_cb_param_t *);

class BLEDevice {
public:
    static bool getInitialized() { return initialized; }
    static void init(const char *) { initialized = true; }
    static BLEScan *getScan() { return scan; }
    static void setCustomGapHandler(gap_event_handler handler) {
        m_customGapHandler = handler;
    }
    static void emitScanStopComplete(int status = ESP_BT_STATUS_SUCCESS) {
        esp_ble_gap_cb_param_t param;
        param.scan_stop_cmpl.status = status;
        if (m_customGapHandler)
            m_customGapHandler(ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT, &param);
    }

    static bool initialized;
    static BLEScan *scan;
    static gap_event_handler m_customGapHandler;
};
