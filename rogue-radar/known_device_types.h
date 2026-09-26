#pragma once

#include <stddef.h>
#include <stdint.h>

namespace rogue_radar {

enum class KnownRadio : uint8_t { Wifi = 1, Ble = 2 };

struct KnownAddress {
    char address[18] = {};
    char advertisedName[33] = {};
    uint8_t addressType = 255;
    uint8_t channel = 0;
    uint16_t manufacturerId = 0;
    char manufacturerData[65] = {};
    char serviceUuids[129] = {};
    bool metadataTruncated = false;
    uint32_t lastSeenUptimeMs = 0;
    uint32_t lastSeenUnix = 0;
    int8_t lastRssi = -127;
};

struct KnownDevice {
    uint32_t id = 0;
    char name[33] = {};
    KnownRadio radio = KnownRadio::Ble;
    uint8_t addressCount = 0;
    KnownAddress addresses[8] = {};
};

struct KnownDeviceSummary {
    uint32_t id = 0;
    char name[33] = {};
    KnownRadio radio = KnownRadio::Ble;
    uint8_t addressCount = 0;
    uint32_t lastSeenUnix = 0;
    int8_t lastRssi = -127;
};

enum class KnownStoreStatus : uint8_t {
    Ok = 0,
    NotFound,
    Unavailable,
    InvalidArgument,
    InvalidRecord,
    Full,
    IoError,
    RecoveryFailed
};

inline const char *knownStoreStatusText(KnownStoreStatus status) {
    switch (status) {
        case KnownStoreStatus::Ok: return "OK";
        case KnownStoreStatus::NotFound: return "Not found";
        case KnownStoreStatus::Unavailable: return "SD unavailable";
        case KnownStoreStatus::InvalidArgument: return "Invalid data";
        case KnownStoreStatus::InvalidRecord: return "Invalid record";
        case KnownStoreStatus::Full: return "Storage full";
        case KnownStoreStatus::IoError: return "SD I/O error";
        case KnownStoreStatus::RecoveryFailed: return "Recovery failed";
    }
    return "Unknown error";
}

}  // namespace rogue_radar
