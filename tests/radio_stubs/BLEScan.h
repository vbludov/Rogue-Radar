#pragma once
#include "BLEAdvertisedDevice.h"
#include <cstdint>

class BLEScanResults {};

class BLEScan {
public:
    void clearResults() { ++clearCalls; }
    void setAdvertisedDeviceCallbacks(BLEAdvertisedDeviceCallbacks *callback,
                                      bool duplicates, bool parse) {
        callbacks = callback;
        wantDuplicates = duplicates;
        shouldParse = parse;
    }
    void setActiveScan(bool active) { activeScan = active; }
    void setInterval(uint16_t value) { interval = value; }
    void setWindow(uint16_t value) { window = value; }
    bool start(uint32_t duration, void (*complete)(BLEScanResults), bool) {
        ++startCalls;
        lastDuration = duration;
        completionCallback = complete;
        stopPending = false;
        running = startSucceeds;
        return startSucceeds;
    }
    void stop() { ++stopCalls; running = false; stopPending = true; }
    void complete() {
        running = false;
        stopPending = false;
        if (completionCallback) completionCallback(BLEScanResults{});
    }

    BLEAdvertisedDeviceCallbacks *callbacks = nullptr;
    bool wantDuplicates = false;
    bool shouldParse = true;
    bool activeScan = true;
    bool startSucceeds = true;
    bool running = false;
    bool stopPending = false;
    void (*completionCallback)(BLEScanResults) = nullptr;
    uint16_t interval = 0;
    uint16_t window = 0;
    uint32_t lastDuration = 0;
    int startCalls = 0;
    int stopCalls = 0;
    int clearCalls = 0;
};
