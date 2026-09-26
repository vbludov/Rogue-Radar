#pragma once

#include "known_device_types.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace rogue_radar {

enum class KnownFsListResult : uint8_t { Entry, End, Error };

class KnownDeviceFileSystem {
 public:
    virtual ~KnownDeviceFileSystem() {}
    virtual bool available() = 0;
    virtual bool ensureDirectory(const char *path) = 0;
    virtual bool exists(const char *path) = 0;
    virtual bool fileSize(const char *path, uint32_t &size) = 0;
    virtual bool readAt(const char *path, uint32_t offset, void *data, size_t size) = 0;
    virtual bool beginWrite(const char *path) = 0;
    virtual bool appendWrite(const void *data, size_t size) = 0;
    virtual bool finishWrite() = 0;
    virtual void abortWrite() = 0;
    virtual bool removeFile(const char *path) = 0;
    virtual bool renameFile(const char *from, const char *to) = 0;
    virtual KnownFsListResult nextFile(const char *directory, uint32_t &cursor,
                                       char *name, size_t nameSize) = 0;
    virtual bool lastErrorIsFull() const { return false; }
};

class KnownDeviceStore {
 public:
    static const uint8_t kMaxPageSize = 6;
    static const uint16_t kSchemaVersion = 1;
    static const char *directory() { return "/rogue-radar/known-devices"; }

    explicit KnownDeviceStore(KnownDeviceFileSystem &fs) : fs_(fs) {}

    KnownStoreStatus create(KnownDevice &device) {
        if (!ready()) return KnownStoreStatus::Unavailable;
        if (device.id != 0 || !validDevice(device, false)) return KnownStoreStatus::InvalidArgument;
        uint32_t maxId = 0, cursor = 0;
        char name[80];
        for (;;) {
            KnownFsListResult result = fs_.nextFile(directory(), cursor, name, sizeof(name));
            if (result == KnownFsListResult::End) break;
            if (result == KnownFsListResult::Error) return KnownStoreStatus::IoError;
            uint32_t id = 0;
            if (parseRecordName(name, id) && id > maxId) maxId = id;
        }
        if (maxId == 0xFFFFFFFFUL) return KnownStoreStatus::Full;
        device.id = maxId + 1;
        KnownStoreStatus status = writeAtomic(device);
        if (status != KnownStoreStatus::Ok) device.id = 0;
        return status;
    }

    KnownStoreStatus read(uint32_t id, KnownDevice &device) {
        if (!ready()) return KnownStoreStatus::Unavailable;
        return readMounted(id, device);
    }

    // The cursor is the last returned record id, not a directory position.
    // Call with zero for the first page. nextCursor equals the input cursor at
    // end of list; otherwise pass it to the next call.
    KnownStoreStatus listPage(uint32_t cursor, KnownDeviceSummary out[kMaxPageSize],
                              uint8_t &count, uint32_t &nextCursor) {
        count = 0;
        nextCursor = cursor;
        if (!out || !ready()) return out ? KnownStoreStatus::Unavailable
                                         : KnownStoreStatus::InvalidArgument;
        while (count < kMaxPageSize) {
            uint32_t id = 0;
            KnownStoreStatus findStatus = nextRecordId(nextCursor, id);
            if (findStatus == KnownStoreStatus::NotFound) {
                nextCursor = cursor;
                return KnownStoreStatus::Ok;
            }
            if (findStatus != KnownStoreStatus::Ok) return findStatus;
            nextCursor = id;
            KnownDevice &device = scratchDevice();
            if (readMounted(id, device) != KnownStoreStatus::Ok) continue;
            KnownDeviceSummary &summary = out[count++];
            summary = KnownDeviceSummary{};
            summary.id = device.id;
            snprintf(summary.name, sizeof(summary.name), "%s", device.name);
            summary.radio = device.radio;
            summary.addressCount = device.addressCount;
            for (uint8_t i = 0; i < device.addressCount; ++i) {
                if (device.addresses[i].lastSeenUnix >= summary.lastSeenUnix) {
                    summary.lastSeenUnix = device.addresses[i].lastSeenUnix;
                    summary.lastRssi = device.addresses[i].lastRssi;
                }
            }
        }
        uint32_t probeAfter = nextCursor;
        for (;;) {
            uint32_t probeId = 0;
            KnownStoreStatus probe = nextRecordId(probeAfter, probeId);
            if (probe == KnownStoreStatus::NotFound) {
                nextCursor = cursor;
                return KnownStoreStatus::Ok;
            }
            if (probe != KnownStoreStatus::Ok) return probe;
            if (readMounted(probeId, scratchDevice()) == KnownStoreStatus::Ok)
                return KnownStoreStatus::Ok;
            probeAfter = probeId;
        }
    }

    KnownStoreStatus update(const KnownDevice &device) {
        if (!validDevice(device, true)) return KnownStoreStatus::InvalidArgument;
        KnownDevice &existing = scratchDevice();
        KnownStoreStatus status = read(device.id, existing);
        if (status != KnownStoreStatus::Ok) return status;
        return writeAtomic(device);
    }

    KnownStoreStatus renameDevice(uint32_t id, const char *name) {
        if (!name || !name[0] || boundedLength(name, 33) >= 33) return KnownStoreStatus::InvalidArgument;
        KnownDevice &device = scratchDevice();
        KnownStoreStatus status = read(id, device);
        if (status != KnownStoreStatus::Ok) return status;
        snprintf(device.name, sizeof(device.name), "%s", name);
        return writeAtomic(device);
    }

    KnownStoreStatus deleteDevice(uint32_t id) {
        KnownDevice &device = scratchDevice();
        KnownStoreStatus status = read(id, device);
        if (status != KnownStoreStatus::Ok) return status;
        char mainPath[64], tmpPath[64], goodPath[64], delPath[64], deleteTmp[64];
        paths(id, mainPath, tmpPath, goodPath, delPath);
        snprintf(deleteTmp, sizeof(deleteTmp), "%s/%08lX.dtmp", directory(), (unsigned long)id);
        uint8_t marker[20] = {'R','R','K','D','D','E','L',0,1,0,0,0,0,0,0,0,0,0,0,0};
        put32(marker + 12, id);
        put32(marker + 16, crcUpdate(0xFFFFFFFFUL, marker, 16) ^ 0xFFFFFFFFUL);
        fs_.removeFile(deleteTmp);
        if (!writeBytes(deleteTmp, marker, sizeof(marker)))
            return fs_.lastErrorIsFull() ? KnownStoreStatus::Full : KnownStoreStatus::IoError;
        fs_.removeFile(delPath);
        if (!fs_.renameFile(deleteTmp, delPath)) return KnownStoreStatus::IoError;
        const bool mainOk = !fs_.exists(mainPath) || fs_.removeFile(mainPath);
        const bool tmpOk = !fs_.exists(tmpPath) || fs_.removeFile(tmpPath);
        const bool goodOk = !fs_.exists(goodPath) || fs_.removeFile(goodPath);
        return (mainOk && tmpOk && goodOk) ? KnownStoreStatus::Ok : KnownStoreStatus::RecoveryFailed;
    }

    KnownStoreStatus addAddress(uint32_t id, const KnownAddress &address) {
        if (!validAddress(address)) return KnownStoreStatus::InvalidArgument;
        KnownDevice &device = scratchDevice();
        KnownStoreStatus status = read(id, device);
        if (status != KnownStoreStatus::Ok) return status;
        for (uint8_t i = 0; i < device.addressCount; ++i) {
            if (strcasecmp(device.addresses[i].address, address.address) == 0 &&
                device.addresses[i].addressType == address.addressType) {
                device.addresses[i] = address;
                return writeAtomic(device);
            }
        }
        if (device.addressCount >= 8) return KnownStoreStatus::Full;
        device.addresses[device.addressCount++] = address;
        return writeAtomic(device);
    }

    KnownStoreStatus removeAddress(uint32_t id, uint8_t index) {
        KnownDevice &device = scratchDevice();
        KnownStoreStatus status = read(id, device);
        if (status != KnownStoreStatus::Ok) return status;
        if (index >= device.addressCount || device.addressCount <= 1)
            return KnownStoreStatus::InvalidArgument;
        for (uint8_t i = index + 1; i < device.addressCount; ++i)
            device.addresses[i - 1] = device.addresses[i];
        device.addresses[--device.addressCount] = KnownAddress{};
        return writeAtomic(device);
    }

 private:
    KnownStoreStatus readMounted(uint32_t id, KnownDevice &device) {
        if (id == 0) return KnownStoreStatus::InvalidArgument;
        char mainPath[64], tmpPath[64], goodPath[64], delPath[64];
        paths(id, mainPath, tmpPath, goodPath, delPath);
        if (validTombstone(delPath, id)) return KnownStoreStatus::NotFound;
        if (decodeValid(mainPath, device) && device.id == id) return KnownStoreStatus::Ok;
        if (decodeValid(tmpPath, device) && device.id == id) {
            if (fs_.exists(mainPath)) fs_.removeFile(mainPath);
            fs_.renameFile(tmpPath, mainPath);
            return KnownStoreStatus::Ok;
        }
        if (decodeValid(goodPath, device) && device.id == id) {
            if (fs_.exists(mainPath)) fs_.removeFile(mainPath);
            fs_.renameFile(goodPath, mainPath);
            return KnownStoreStatus::Ok;
        }
        if (fs_.exists(mainPath) || fs_.exists(tmpPath) || fs_.exists(goodPath))
            return KnownStoreStatus::InvalidRecord;
        return KnownStoreStatus::NotFound;
    }
    static const uint32_t kAddressSize = 18 + 33 + 1 + 1 + 2 + 65 + 129 + 1 + 4 + 4 + 1;
    static const uint32_t kPayloadSize = 4 + 33 + 1 + 1 + 8 * kAddressSize;
    static const uint32_t kFileSize = 16 + kPayloadSize;
    KnownDeviceFileSystem &fs_;

    static KnownDevice &scratchDevice() {
        // Store calls are synchronous. Keeping this record in static storage avoids
        // consuming roughly 2.1 KB of the Arduino loop task stack per operation.
        static KnownDevice scratch;
        return scratch;
    }

    bool ready() { return fs_.available() && fs_.ensureDirectory(directory()); }

    static size_t boundedLength(const char *s, size_t cap) {
        size_t n = 0;
        while (n < cap && s[n]) ++n;
        return n;
    }
    static bool validString(const char *s, size_t cap) { return boundedLength(s, cap) < cap; }
    static bool validAddress(const KnownAddress &a) {
        return validString(a.address, sizeof(a.address)) && a.address[0] &&
               (a.addressType <= 3 || a.addressType == 255) &&
               validString(a.advertisedName, sizeof(a.advertisedName)) &&
               validString(a.manufacturerData, sizeof(a.manufacturerData)) &&
               validString(a.serviceUuids, sizeof(a.serviceUuids));
    }
    static bool validDevice(const KnownDevice &d, bool requireId) {
        if ((requireId && d.id == 0) || (!requireId && d.id != 0)) return false;
        if (!validString(d.name, sizeof(d.name)) || !d.name[0] || d.addressCount == 0 || d.addressCount > 8) return false;
        if (d.radio != KnownRadio::Wifi && d.radio != KnownRadio::Ble) return false;
        for (uint8_t i = 0; i < d.addressCount; ++i) if (!validAddress(d.addresses[i])) return false;
        return true;
    }
    static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
    static void put32(uint8_t *p, uint32_t v) {
        p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
    }
    static uint16_t get16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
    static uint32_t get32(const uint8_t *p) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    static uint32_t crcUpdate(uint32_t crc, const void *data, size_t size) {
        const uint8_t *p = static_cast<const uint8_t *>(data);
        while (size--) {
            crc ^= *p++;
            for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320UL & (uint32_t)-(int32_t)(crc & 1));
        }
        return crc;
    }
    bool validTombstone(const char *path, uint32_t id) {
        uint8_t marker[20]; uint32_t size = 0;
        return fs_.exists(path) && fs_.fileSize(path, size) && size == sizeof(marker) &&
               fs_.readAt(path, 0, marker, sizeof(marker)) &&
               memcmp(marker, "RRKDDEL", 7) == 0 && marker[7] == 0 &&
               get16(marker + 8) == 1 && get16(marker + 10) == 0 &&
               get32(marker + 12) == id &&
               get32(marker + 16) == (crcUpdate(0xFFFFFFFFUL, marker, 16) ^ 0xFFFFFFFFUL);
    }
    static void paths(uint32_t id, char *mainPath, char *tmpPath, char *goodPath, char *delPath) {
        snprintf(mainPath, 64, "%s/%08lX.kdev", directory(), (unsigned long)id);
        snprintf(tmpPath, 64, "%s/%08lX.tmp", directory(), (unsigned long)id);
        snprintf(goodPath, 64, "%s/%08lX.lkg", directory(), (unsigned long)id);
        snprintf(delPath, 64, "%s/%08lX.del", directory(), (unsigned long)id);
    }
    static bool parseRecordName(const char *name, uint32_t &id) {
        if (!name) return false;
        const char *base = strrchr(name, '/'); base = base ? base + 1 : name;
        if (strlen(base) != 13 || strcmp(base + 8, ".kdev") != 0) return false;
        uint32_t value = 0;
        for (uint8_t i = 0; i < 8; ++i) {
            char c = base[i]; uint8_t digit;
            if (c >= '0' && c <= '9') digit = (uint8_t)(c - '0');
            else if (c >= 'A' && c <= 'F') digit = (uint8_t)(c - 'A' + 10);
            else if (c >= 'a' && c <= 'f') digit = (uint8_t)(c - 'a' + 10);
            else return false;
            value = (value << 4) | digit;
        }
        id = value;
        return id != 0;
    }
    KnownStoreStatus nextRecordId(uint32_t afterId, uint32_t &id) {
        uint32_t cursor = 0;
        uint32_t best = 0xFFFFFFFFUL;
        char name[80];
        for (;;) {
            KnownFsListResult result = fs_.nextFile(directory(), cursor, name, sizeof(name));
            if (result == KnownFsListResult::End) break;
            if (result == KnownFsListResult::Error) return KnownStoreStatus::IoError;
            uint32_t candidate = 0;
            if (parseRecordName(name, candidate) && candidate > afterId && candidate < best)
                best = candidate;
        }
        if (best == 0xFFFFFFFFUL) return KnownStoreStatus::NotFound;
        id = best;
        return KnownStoreStatus::Ok;
    }

    template <typename Sink> static bool writeFixedString(Sink &sink, const char *value, size_t size) {
        char normalized[129] = {};
        const size_t length = boundedLength(value, size);
        if (length >= size || size > sizeof(normalized)) return false;
        memcpy(normalized, value, length);
        return sink(normalized, size);
    }
    template <typename Sink> static bool visitPayload(const KnownDevice &d, Sink &sink) {
        uint8_t n[4]; put32(n, d.id); if (!sink(n, 4)) return false;
        if (!writeFixedString(sink, d.name, sizeof(d.name))) return false;
        uint8_t radio = (uint8_t)d.radio; if (!sink(&radio, 1) || !sink(&d.addressCount, 1)) return false;
        static const KnownAddress emptyAddress{};
        for (uint8_t i = 0; i < 8; ++i) {
            const KnownAddress &a = i < d.addressCount ? d.addresses[i] : emptyAddress;
            if (!writeFixedString(sink, a.address, sizeof(a.address)) ||
                !writeFixedString(sink, a.advertisedName, sizeof(a.advertisedName))) return false;
            if (!sink(&a.addressType, 1) || !sink(&a.channel, 1)) return false;
            uint8_t u16[2]; put16(u16, a.manufacturerId); if (!sink(u16, 2)) return false;
            if (!writeFixedString(sink, a.manufacturerData, sizeof(a.manufacturerData)) ||
                !writeFixedString(sink, a.serviceUuids, sizeof(a.serviceUuids))) return false;
            uint8_t truncated = a.metadataTruncated ? 1 : 0; if (!sink(&truncated, 1)) return false;
            uint8_t u32[4]; put32(u32, a.lastSeenUptimeMs); if (!sink(u32, 4)) return false;
            put32(u32, a.lastSeenUnix); if (!sink(u32, 4) || !sink(&a.lastRssi, 1)) return false;
        }
        return true;
    }

    uint32_t payloadCrc(const KnownDevice &device) {
        struct CrcSink { uint32_t crc; bool operator()(const void *p, size_t n) { crc = KnownDeviceStore::crcUpdate(crc, p, n); return true; } } sink{0xFFFFFFFFUL};
        visitPayload(device, sink);
        return sink.crc ^ 0xFFFFFFFFUL;
    }
    bool writeBytes(const char *path, const void *data, size_t size) {
        if (!fs_.beginWrite(path)) return false;
        if (!fs_.appendWrite(data, size)) { fs_.abortWrite(); return false; }
        if (!fs_.finishWrite()) { fs_.abortWrite(); return false; }
        return true;
    }
    bool writeRecord(const char *path, const KnownDevice &device) {
        uint8_t header[16] = {'R','R','K','D',0,0,16,0,0,0,0,0,0,0,0,0};
        put16(header + 4, kSchemaVersion); put32(header + 8, kPayloadSize); put32(header + 12, payloadCrc(device));
        if (!fs_.beginWrite(path)) return false;
        if (!fs_.appendWrite(header, sizeof(header))) { fs_.abortWrite(); return false; }
        struct WriteSink { KnownDeviceFileSystem &fs; bool operator()(const void *p, size_t n) { return fs.appendWrite(p, n); } } sink{fs_};
        if (!visitPayload(device, sink) || !fs_.finishWrite()) { fs_.abortWrite(); return false; }
        return true;
    }
    bool decodeValid(const char *path, KnownDevice &device) {
        uint32_t size = 0; uint8_t header[16];
        if (!fs_.exists(path) || !fs_.fileSize(path, size) || size != kFileSize || !fs_.readAt(path, 0, header, 16)) return false;
        if (memcmp(header, "RRKD", 4) != 0 || get16(header + 4) != kSchemaVersion || get16(header + 6) != 16 || get32(header + 8) != kPayloadSize) return false;
        uint32_t crc = 0xFFFFFFFFUL; uint8_t chunk[64];
        for (uint32_t offset = 16; offset < size;) {
            size_t amount = (size - offset > sizeof(chunk)) ? sizeof(chunk) : (size_t)(size - offset);
            if (!fs_.readAt(path, offset, chunk, amount)) return false;
            crc = crcUpdate(crc, chunk, amount); offset += amount;
        }
        if ((crc ^ 0xFFFFFFFFUL) != get32(header + 12)) return false;
        device = KnownDevice{}; uint32_t offset = 16; uint8_t b4[4], b2[2], b;
#define RR_READ_FIELD(field) do { if (!fs_.readAt(path, offset, &(field), sizeof(field))) return false; offset += sizeof(field); } while (0)
        if (!fs_.readAt(path, offset, b4, 4)) return false;
        device.id = get32(b4); offset += 4;
        RR_READ_FIELD(device.name); RR_READ_FIELD(b); device.radio = (KnownRadio)b; RR_READ_FIELD(device.addressCount);
        for (uint8_t i = 0; i < 8; ++i) {
            KnownAddress &a = device.addresses[i];
            RR_READ_FIELD(a.address); RR_READ_FIELD(a.advertisedName); RR_READ_FIELD(a.addressType); RR_READ_FIELD(a.channel);
            if (!fs_.readAt(path, offset, b2, 2)) return false;
            a.manufacturerId = get16(b2); offset += 2;
            RR_READ_FIELD(a.manufacturerData); RR_READ_FIELD(a.serviceUuids); RR_READ_FIELD(b);
            if (b > 1) return false;
            a.metadataTruncated = b != 0;
            if (!fs_.readAt(path, offset, b4, 4)) return false;
            a.lastSeenUptimeMs = get32(b4); offset += 4;
            if (!fs_.readAt(path, offset, b4, 4)) return false;
            a.lastSeenUnix = get32(b4); offset += 4;
            RR_READ_FIELD(a.lastRssi);
        }
#undef RR_READ_FIELD
        return offset == size && validDevice(device, true);
    }
    KnownStoreStatus writeAtomic(const KnownDevice &device) {
        char mainPath[64], tmpPath[64], goodPath[64], delPath[64];
        paths(device.id, mainPath, tmpPath, goodPath, delPath);
        fs_.removeFile(tmpPath);
        if (!writeRecord(tmpPath, device))
            return fs_.lastErrorIsFull() ? KnownStoreStatus::Full : KnownStoreStatus::IoError;
        KnownDevice &check = scratchDevice();
        if (!decodeValid(tmpPath, check)) { fs_.removeFile(tmpPath); return KnownStoreStatus::InvalidRecord; }
        if (fs_.exists(mainPath)) {
            fs_.removeFile(goodPath);
            if (!fs_.renameFile(mainPath, goodPath)) { fs_.removeFile(tmpPath); return KnownStoreStatus::IoError; }
        }
        if (!fs_.renameFile(tmpPath, mainPath)) {
            if (!fs_.exists(mainPath) && fs_.exists(goodPath)) fs_.renameFile(goodPath, mainPath);
            return KnownStoreStatus::RecoveryFailed;
        }
        if (!decodeValid(mainPath, check)) {
            fs_.removeFile(mainPath);
            if (fs_.exists(goodPath)) fs_.renameFile(goodPath, mainPath);
            return KnownStoreStatus::RecoveryFailed;
        }
        fs_.removeFile(delPath);
        return KnownStoreStatus::Ok;
    }
};

}  // namespace rogue_radar
