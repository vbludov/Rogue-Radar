#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <string>

#include "Arduino.h"

#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED  (-2)

struct FakeWifiResult {
    std::array<uint8_t, 6> bssid;
    int rssi;
    int channel;
    std::string ssid;
};

class FakeWiFiClass {
public:
    enum class ScanState { Idle, Running, Done, Failed };

    int16_t scanComplete() const;
    void scanDelete();
    int16_t scanNetworks(bool async, bool showHidden, bool passive,
                         uint32_t dwellMs, uint8_t channel,
                         const char *ssid, const uint8_t *bssid);
    int16_t scanNetworks(bool async, bool showHidden, bool passive,
                         uint32_t dwellMs, uint8_t channel);
    const uint8_t *BSSID(int16_t index) const;
    int RSSI(int16_t index) const;
    int channel(int16_t index) const;
    String SSID(int16_t index) const;

    void reset();
    void complete(const std::vector<FakeWifiResult> &newResults);
    void fail();
    void startExternalScan();
    void driverStop();
    void deliverStoppedScanDone();

    ScanState state = ScanState::Idle;
    bool driverRunning = false;
    bool stoppedEventPending = false;
    bool failNextStart = false;
    uint32_t stoppedAtMs = 0;
    int scanStartCalls = 0;
    int scanDeleteCalls = 0;
    int driverStopCalls = 0;
    uint8_t lastChannel = 0;
    uint32_t lastDwellMs = 0;
    std::array<uint8_t, 6> lastFilter{};
    std::vector<FakeWifiResult> results;
};

extern FakeWiFiClass WiFi;
