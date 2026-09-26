#pragma once
#include "BLEScan.h"

class BLEDevice {
public:
    static bool getInitialized() { return initialized; }
    static void init(const char *) { initialized = true; }
    static BLEScan *getScan() { return scan; }

    static bool initialized;
    static BLEScan *scan;
};