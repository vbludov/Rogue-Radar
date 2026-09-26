#pragma once

#include <Arduino.h>
#include <ctype.h>
#include <string>
#include <WiFi.h>
#include <esp_wifi.h>
#include <BLEAdvertisedDevice.h>
#include "continuous_ble_scan.h"
#include "known_device_types.h"

namespace rogue_radar {

struct KnownDiscoveryObservation {
    KnownRadio radio = KnownRadio::Ble;
    KnownAddress address{};
};

// One shared, asynchronous discovery-radio owner. BLE callbacks only flatten
// advertisements into a small POD mailbox; callers consume observations from
// the Arduino loop. stop() is a drain operation and must be polled until true.
class KnownDeviceDiscovery {
 public:
    bool begin(KnownRadio radio) {
        if (state_ != State::Idle) {
            status_ = "Discovery radio busy";
            return false;
        }
        resetMailbox();
        radio_ = radio;
        if (radio == KnownRadio::Ble) {
            if (!ble_.begin(onBleAdvertisement, this)) {
                status_ = ble_.status();
                return false;
            }
            state_ = State::BleScanning;
            status_ = "Listening for BLE";
            return true;
        }

        state_ = State::WifiReady;
        nextWifiScanMs_ = 0;
        status_ = "Starting WiFi scan";
        return true;
    }

    void poll() {
        if (state_ == State::BleScanning) {
            ble_.poll();
            if (!ble_.running()) {
                state_ = State::BleStopping;
                status_ = ble_.stopFailed() ? "BLE stop failed; restart required"
                                           : "BLE scan ended";
            }
            return;
        }
        if (state_ == State::BleStopping) {
            finishBleStop();
            return;
        }
        if (state_ == State::WifiStopping) {
            finishWifiStop();
            return;
        }
        if (state_ == State::WifiCollecting) {
            collectWifiResults();
            return;
        }
        if (state_ == State::WifiScanning) {
            const int16_t count = WiFi.scanComplete();
            if (count == WIFI_SCAN_RUNNING) return;
            if (count >= 0) {
                wifiScanInFlight_ = false;
                wifiResultCount_ = count;
                wifiResultIndex_ = 0;
                state_ = State::WifiCollecting;
                collectWifiResults();
            } else {
                cancelWifiScan();
                status_ = "WiFi scan cleanup pending";
            }
            return;
        }
        if (state_ == State::WifiReady && timeReached(nextWifiScanMs_)) {
            const int16_t result = WiFi.scanNetworks(true, true, true,
                                                     kWifiDwellMs, 0);
            if (result == WIFI_SCAN_RUNNING || result >= 0) {
                wifiScanInFlight_ = true;
                state_ = State::WifiScanning;
                status_ = "Listening for WiFi";
            } else {
                nextWifiScanMs_ = millis() + kWifiRetryMs;
                status_ = "WiFi scan retrying";
            }
        }
    }

    bool stop() {
        if (state_ == State::Idle) return true;
        if (state_ == State::BleScanning) {
            state_ = State::BleStopping;
            status_ = "Stopping BLE";
        }
        if (state_ == State::BleStopping) return finishBleStop();

        if (state_ == State::WifiScanning) cancelWifiScan();
        if (state_ == State::WifiCollecting) {
            WiFi.scanDelete();
            wifiResultCount_ = wifiResultIndex_ = 0;
            state_ = State::Idle;
            status_ = "Stopped";
            return true;
        }
        if (state_ == State::WifiReady) {
            state_ = State::Idle;
            status_ = "Stopped";
            return true;
        }
        if (state_ == State::WifiStopping) return finishWifiStop();
        return false;
    }

    bool takeObservation(KnownDiscoveryObservation &observation) {
        portENTER_CRITICAL(&mailboxMux_);
        if (mailboxCount_ == 0) {
            portEXIT_CRITICAL(&mailboxMux_);
            return false;
        }
        observation = mailbox_[mailboxHead_];
        mailboxHead_ = (mailboxHead_ + 1U) % kMailboxCapacity;
        --mailboxCount_;
        portEXIT_CRITICAL(&mailboxMux_);
        return true;
    }

    bool active() const { return state_ != State::Idle; }
    bool draining() const {
        return state_ == State::WifiStopping || state_ == State::BleStopping;
    }
    KnownRadio radio() const { return radio_; }
    uint32_t dropped() const { return mailboxDrops_; }
    const char *status() const { return status_; }

 private:
    enum class State : uint8_t {
        Idle, WifiReady, WifiScanning, WifiCollecting, WifiStopping,
        BleScanning, BleStopping
    };
    static constexpr uint8_t kMailboxCapacity = 6;
    static constexpr uint32_t kWifiDwellMs = 110;
    static constexpr uint32_t kWifiScanGapMs = 220;
    static constexpr uint32_t kWifiRetryMs = 600;

    static bool timeReached(uint32_t deadline) {
        return static_cast<int32_t>(millis() - deadline) >= 0;
    }

    void resetMailbox() {
        portENTER_CRITICAL(&mailboxMux_);
        mailboxHead_ = mailboxCount_ = 0;
        mailboxDrops_ = 0;
        portEXIT_CRITICAL(&mailboxMux_);
    }

    void pushObservation(const KnownDiscoveryObservation &observation) {
        portENTER_CRITICAL(&mailboxMux_);
        for (uint8_t offset = 0; offset < mailboxCount_; ++offset) {
            const uint8_t index = (mailboxHead_ + offset) % kMailboxCapacity;
            if (mailbox_[index].radio == observation.radio &&
                mailbox_[index].address.addressType == observation.address.addressType &&
                strcmp(mailbox_[index].address.address,
                       observation.address.address) == 0) {
                mailbox_[index] = observation;
                portEXIT_CRITICAL(&mailboxMux_);
                return;
            }
        }
        if (mailboxCount_ == kMailboxCapacity) {
            ++mailboxDrops_;
            portEXIT_CRITICAL(&mailboxMux_);
            return;
        }
        const uint8_t tail = (mailboxHead_ + mailboxCount_) % kMailboxCapacity;
        mailbox_[tail] = observation;
        ++mailboxCount_;
        portEXIT_CRITICAL(&mailboxMux_);
    }

    void publishWifiResult(int16_t index) {
        const uint8_t *bssid = WiFi.BSSID(index);
        if (!bssid) return;
        KnownDiscoveryObservation observation{};
        observation.radio = KnownRadio::Wifi;
        KnownAddress &address = observation.address;
        snprintf(address.address, sizeof(address.address),
                 "%02x:%02x:%02x:%02x:%02x:%02x", bssid[0], bssid[1], bssid[2],
                 bssid[3], bssid[4], bssid[5]);
        String ssid = WiFi.SSID(index);
        snprintf(address.advertisedName, sizeof(address.advertisedName), "%s",
                 ssid.length() ? ssid.c_str() : "<hidden>");
        address.channel = static_cast<uint8_t>(WiFi.channel(index));
        address.lastRssi = static_cast<int8_t>(WiFi.RSSI(index));
        address.lastSeenUptimeMs = millis();
        pushObservation(observation);
    }

    void collectWifiResults() {
        uint8_t published = 0;
        while (wifiResultIndex_ < wifiResultCount_ && published < kMailboxCapacity) {
            publishWifiResult(wifiResultIndex_++);
            ++published;
        }
        if (wifiResultIndex_ < wifiResultCount_) return;
        WiFi.scanDelete();
        wifiResultCount_ = wifiResultIndex_ = 0;
        state_ = State::WifiReady;
        nextWifiScanMs_ = millis() + kWifiScanGapMs;
        status_ = "Listening for WiFi";
    }

    static void appendHex(char *output, size_t capacity, size_t &used,
                          const uint8_t *bytes, size_t length, bool &truncated) {
        static const char hex[] = "0123456789ABCDEF";
        for (size_t i = 0; i < length; ++i) {
            if (used + 2 >= capacity) { truncated = true; return; }
            output[used++] = hex[bytes[i] >> 4];
            output[used++] = hex[bytes[i] & 0x0f];
        }
        output[used] = '\0';
    }

    static void onBleAdvertisement(BLEAdvertisedDevice &device, void *context) {
        static_cast<KnownDeviceDiscovery *>(context)->publishBleResult(device);
    }

    void publishBleResult(BLEAdvertisedDevice &device) {
        KnownDiscoveryObservation observation{};
        observation.radio = KnownRadio::Ble;
        KnownAddress &address = observation.address;
        String mac = device.getAddress().toString().c_str();
        snprintf(address.address, sizeof(address.address), "%s", mac.c_str());
        for (char *p = address.address; *p; ++p)
            *p = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
        address.addressType = device.getAddressType();
        if (device.haveName()) {
            String name = device.getName().c_str();
            snprintf(address.advertisedName, sizeof(address.advertisedName), "%s", name.c_str());
        }
        address.lastRssi = static_cast<int8_t>(device.getRSSI());
        address.lastSeenUptimeMs = millis();

        if (device.haveManufacturerData()) {
            std::string data = device.getManufacturerData();
            const size_t length = data.length();
            const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data.data());
            if (length >= 2)
                address.manufacturerId = static_cast<uint16_t>(bytes[0]) |
                                         (static_cast<uint16_t>(bytes[1]) << 8);
            size_t used = 0;
            appendHex(address.manufacturerData, sizeof(address.manufacturerData), used,
                      bytes, length, address.metadataTruncated);
        }

        size_t uuidUsed = 0;
        const int uuidCount = device.getServiceUUIDCount();
        for (int i = 0; i < uuidCount; ++i) {
            std::string uuid = device.getServiceUUID(i).toString();
            const size_t needed = uuid.length() + (uuidUsed ? 1U : 0U);
            if (uuidUsed + needed >= sizeof(address.serviceUuids)) {
                address.metadataTruncated = true;
                break;
            }
            if (uuidUsed) address.serviceUuids[uuidUsed++] = ',';
            memcpy(address.serviceUuids + uuidUsed, uuid.data(), uuid.length());
            uuidUsed += uuid.length();
            address.serviceUuids[uuidUsed] = '\0';
        }
        pushObservation(observation);
    }

    void cancelWifiScan() {
        if (wifiScanInFlight_) esp_wifi_scan_stop();
        wifiScanInFlight_ = false;
        state_ = State::WifiStopping;
    }

    bool finishWifiStop() {
        // Only a non-negative result proves Arduino's delayed SCAN_DONE handler
        // ran and reset its private _scanStarted flag.
        if (WiFi.scanComplete() < 0) return false;
        WiFi.scanDelete();
        state_ = State::Idle;
        status_ = "Stopped";
        return true;
    }

    bool finishBleStop() {
        ble_.poll();
        if (!ble_.stop()) {
            if (ble_.stopFailed()) status_ = ble_.status();
            return false;
        }
        state_ = State::Idle;
        status_ = "Stopped";
        return true;
    }

    portMUX_TYPE mailboxMux_ = portMUX_INITIALIZER_UNLOCKED;
    KnownDiscoveryObservation mailbox_[kMailboxCapacity]{};
    uint8_t mailboxHead_ = 0;
    uint8_t mailboxCount_ = 0;
    uint32_t mailboxDrops_ = 0;
    State state_ = State::Idle;
    KnownRadio radio_ = KnownRadio::Ble;
    ContinuousBleScan ble_{};
    bool wifiScanInFlight_ = false;
    int16_t wifiResultCount_ = 0;
    int16_t wifiResultIndex_ = 0;
    uint32_t nextWifiScanMs_ = 0;
    const char *status_ = "Idle";
};

}  // namespace rogue_radar

// Shared radio wrappers used by Nearby/Learn and Saved Devices. Only the
// currently visible owner may call them; ownership transfer always drains via
// knownDiscoveryStop() first.
static rogue_radar::KnownDeviceDiscovery knownDiscoveryBackend;
static const char *knownDiscoveryOwner = nullptr;

static inline bool knownDiscoveryOwnedBy(const char *owner) {
    return owner && knownDiscoveryOwner && strcmp(owner, knownDiscoveryOwner) == 0;
}

static inline bool knownDiscoveryStart(const char *owner, rogue_radar::KnownRadio radio) {
    if (!owner || knownDiscoveryOwner) return false;
    knownDiscoveryOwner = owner;
    if (knownDiscoveryBackend.begin(radio)) return true;
    knownDiscoveryOwner = nullptr;
    return false;
}

static inline void knownDiscoveryPoll(const char *owner) {
    if (!knownDiscoveryOwnedBy(owner)) return;
    knownDiscoveryBackend.poll();
}

static inline bool knownDiscoveryStop(const char *owner) {
    if (!knownDiscoveryOwnedBy(owner)) return knownDiscoveryOwner == nullptr;
    if (!knownDiscoveryBackend.stop()) return false;
    knownDiscoveryOwner = nullptr;
    return true;
}

static inline bool knownDiscoveryTake(const char *owner,
                               rogue_radar::KnownDiscoveryObservation &observation) {
    if (!knownDiscoveryOwnedBy(owner)) return false;
    return knownDiscoveryBackend.takeObservation(observation);
}

static inline bool knownDiscoveryAddressMatches(rogue_radar::KnownRadio radio,
                                         const rogue_radar::KnownAddress &left,
                                         const rogue_radar::KnownAddress &right) {
    for (size_t i = 0; i < sizeof(left.address); ++i) {
        const unsigned char a = static_cast<unsigned char>(left.address[i]);
        const unsigned char b = static_cast<unsigned char>(right.address[i]);
        if (tolower(a) != tolower(b)) return false;
        if (a == '\0') break;
    }
    return radio != rogue_radar::KnownRadio::Ble ||
           left.addressType == right.addressType;
}

static inline const char *knownDiscoveryStatus() {
    return knownDiscoveryBackend.status();
}
