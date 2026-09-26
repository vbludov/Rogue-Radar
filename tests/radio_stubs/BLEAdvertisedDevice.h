#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using esp_bd_addr_t = uint8_t[6];

class BLEAddress {
public:
    BLEAddress() = default;
    explicit BLEAddress(const std::array<uint8_t, 6> &value) : value_(value) {}
    esp_bd_addr_t *getNative() {
        return reinterpret_cast<esp_bd_addr_t *>(value_.data());
    }
    std::string toString() const {
        char text[18];
        std::snprintf(text, sizeof(text), "%02x:%02x:%02x:%02x:%02x:%02x",
                      value_[0], value_[1], value_[2], value_[3], value_[4], value_[5]);
        return text;
    }
private:
    std::array<uint8_t, 6> value_{};
};

class BLEUUID {
public:
    BLEUUID() = default;
    explicit BLEUUID(const std::string &value) : value_(value) {}
    std::string toString() const { return value_; }
private:
    std::string value_;
};

class BLEAdvertisedDevice {
public:
    BLEAdvertisedDevice() = default;
    BLEAdvertisedDevice(const std::array<uint8_t, 6> &address, int rssi)
        : address_(address), rssi_(rssi) {}
    BLEAddress getAddress() const { return address_; }
    int getRSSI() const { return rssi_; }
    uint8_t getAddressType() const { return addressType_; }
    bool haveName() const { return !name_.empty(); }
    std::string getName() const { return name_; }
    bool haveManufacturerData() const { return !manufacturerData_.empty(); }
    std::string getManufacturerData() const { return manufacturerData_; }
    int getServiceUUIDCount() const { return static_cast<int>(uuids_.size()); }
    BLEUUID getServiceUUID(int index) const { return BLEUUID(uuids_[index]); }

    BLEAdvertisedDevice &setAddressType(uint8_t value) { addressType_ = value; return *this; }
    BLEAdvertisedDevice &setName(const std::string &value) { name_ = value; return *this; }
    BLEAdvertisedDevice &setManufacturerData(const std::string &value) {
        manufacturerData_ = value; return *this;
    }
    BLEAdvertisedDevice &addServiceUuid(const std::string &value) {
        uuids_.push_back(value); return *this;
    }
private:
    BLEAddress address_;
    int rssi_ = 0;
    uint8_t addressType_ = 255;
    std::string name_;
    std::string manufacturerData_;
    std::vector<std::string> uuids_;
};

class BLEAdvertisedDeviceCallbacks {
public:
    virtual ~BLEAdvertisedDeviceCallbacks() = default;
    virtual void onResult(BLEAdvertisedDevice device) = 0;
};
