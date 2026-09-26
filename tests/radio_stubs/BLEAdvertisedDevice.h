#pragma once
#include <array>
#include <cstdint>

using esp_bd_addr_t = uint8_t[6];

class BLEAddress {
public:
    BLEAddress() = default;
    explicit BLEAddress(const std::array<uint8_t, 6> &value) : value_(value) {}
    esp_bd_addr_t *getNative() {
        return reinterpret_cast<esp_bd_addr_t *>(value_.data());
    }
private:
    std::array<uint8_t, 6> value_{};
};

class BLEAdvertisedDevice {
public:
    BLEAdvertisedDevice() = default;
    BLEAdvertisedDevice(const std::array<uint8_t, 6> &address, int rssi)
        : address_(address), rssi_(rssi) {}
    BLEAddress getAddress() const { return address_; }
    int getRSSI() const { return rssi_; }
private:
    BLEAddress address_;
    int rssi_ = 0;
};

class BLEAdvertisedDeviceCallbacks {
public:
    virtual ~BLEAdvertisedDeviceCallbacks() = default;
    virtual void onResult(BLEAdvertisedDevice device) = 0;
};