#include "../rogue-radar/known_device_store.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace rogue_radar;

class MemoryFs : public KnownDeviceFileSystem {
 public:
    bool online = true;
    size_t capacity = 1024 * 1024;
    bool fullError = false;
    std::string failRenameFrom;
    std::string failRemovePath;
    std::map<std::string, std::vector<uint8_t> > files;

    bool available() override { return online; }
    bool ensureDirectory(const char *) override { return online; }
    bool exists(const char *path) override { return files.count(path) != 0; }
    bool fileSize(const char *path, uint32_t &size) override {
        if (!exists(path)) return false;
        size = static_cast<uint32_t>(files[path].size());
        return true;
    }
    bool readAt(const char *path, uint32_t offset, void *data, size_t size) override {
        if (!online || !exists(path) || offset + size > files[path].size()) return false;
        memcpy(data, files[path].data() + offset, size);
        return true;
    }
    bool beginWrite(const char *path) override {
        if (!online || writing_) return false;
        writePath_ = path;
        writeData_.clear();
        writing_ = true;
        fullError = false;
        return true;
    }
    bool appendWrite(const void *data, size_t size) override {
        if (!writing_) return false;
        if (usedWithout(writePath_) + writeData_.size() + size > capacity) {
            fullError = true;
            return false;
        }
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        writeData_.insert(writeData_.end(), bytes, bytes + size);
        return true;
    }
    bool finishWrite() override {
        if (!writing_) return false;
        files[writePath_] = writeData_;
        writing_ = false;
        return true;
    }
    void abortWrite() override { writing_ = false; writeData_.clear(); }
    bool removeFile(const char *path) override {
        if (failRemovePath == path) return false;
        files.erase(path);
        return true;
    }
    bool renameFile(const char *from, const char *to) override {
        if (!exists(from) || failRenameFrom == from) return false;
        files[to] = files[from];
        files.erase(from);
        return true;
    }
    KnownFsListResult nextFile(const char *, uint32_t &cursor, char *name,
                               size_t nameSize) override {
        if (!online) return KnownFsListResult::Error;
        std::vector<std::string> names;
        for (std::map<std::string, std::vector<uint8_t> >::const_iterator it = files.begin();
             it != files.end(); ++it) {
            if (it->first.find(std::string(KnownDeviceStore::directory()) + "/") == 0)
                names.push_back(it->first);
        }
        if (cursor >= names.size()) return KnownFsListResult::End;
        strncpy(name, names[cursor++].c_str(), nameSize - 1);
        name[nameSize - 1] = '\0';
        return KnownFsListResult::Entry;
    }
    bool lastErrorIsFull() const override { return fullError; }

 private:
    size_t usedWithout(const std::string &excluded) const {
        size_t used = 0;
        for (std::map<std::string, std::vector<uint8_t> >::const_iterator it = files.begin();
             it != files.end(); ++it)
            if (it->first != excluded) used += it->second.size();
        return used;
    }
    bool writing_ = false;
    std::string writePath_;
    std::vector<uint8_t> writeData_;
};

static std::string path(uint32_t id, const char *suffix) {
    char value[80];
    snprintf(value, sizeof(value), "%s/%08X.%s", KnownDeviceStore::directory(), id, suffix);
    return value;
}

static KnownDevice sample(const char *name, const char *address, KnownRadio radio = KnownRadio::Ble) {
    KnownDevice device{};
    snprintf(device.name, sizeof(device.name), "%s", name);
    device.radio = radio;
    device.addressCount = 1;
    snprintf(device.addresses[0].address, sizeof(device.addresses[0].address), "%s", address);
    device.addresses[0].addressType = radio == KnownRadio::Ble ? 1 : 255;
    device.addresses[0].lastRssi = -54;
    device.addresses[0].lastSeenUnix = 1234;
    return device;
}

static void testCrudAndPagination() {
    MemoryFs fs;
    fs.files["/photos/keep.jpg"] = std::vector<uint8_t>(3, 7);
    KnownDeviceStore store(fs);
    for (int i = 0; i < 8; ++i) {
        char name[16], address[18];
        snprintf(name, sizeof(name), "Device %d", i);
        snprintf(address, sizeof(address), "AA:BB:CC:DD:EE:%02X", i);
        KnownDevice device = sample(name, address);
        assert(store.create(device) == KnownStoreStatus::Ok);
        assert(device.id == static_cast<uint32_t>(i + 1));
    }
    KnownDeviceSummary page[6]; uint8_t count = 0; uint32_t next = 0;
    assert(store.listPage(0, page, count, next) == KnownStoreStatus::Ok);
    assert(count == 6 && page[0].id == 1 && page[5].id == 6 && next == 6);
    assert(store.listPage(next, page, count, next) == KnownStoreStatus::Ok);
    assert(count == 2 && page[0].id == 7 && page[1].id == 8 && next == 6);

    assert(store.renameDevice(1, "Watch") == KnownStoreStatus::Ok);
    KnownAddress second{};
    strcpy(second.address, "AB:CD:33:44:55:66"); second.addressType = 0;
    assert(store.addAddress(1, second) == KnownStoreStatus::Ok);
    KnownAddress sameDifferentCase = second;
    strcpy(sameDifferentCase.address, "ab:cd:33:44:55:66");
    sameDifferentCase.lastRssi = -31;
    assert(store.addAddress(1, sameDifferentCase) == KnownStoreStatus::Ok);
    KnownDevice loaded{};
    assert(store.read(1, loaded) == KnownStoreStatus::Ok);
    assert(strcmp(loaded.name, "Watch") == 0 && loaded.addressCount == 2);
    assert(loaded.addresses[1].lastRssi == -31);
    assert(store.removeAddress(1, 0) == KnownStoreStatus::Ok);
    assert(store.deleteDevice(1) == KnownStoreStatus::Ok);
    assert(store.read(1, loaded) == KnownStoreStatus::NotFound);
    assert(fs.files.count("/photos/keep.jpg") == 1);
}

static void testRecoveryAndMalformedData() {
    MemoryFs fs; KnownDeviceStore store(fs);
    KnownDevice device = sample("Tag", "AA:AA:AA:AA:AA:AA");
    assert(store.create(device) == KnownStoreStatus::Ok);
    const std::string main = path(device.id, "kdev");
    const std::string tmp = path(device.id, "tmp");
    fs.files[tmp] = fs.files[main];
    fs.files[main][20] ^= 0x55;
    KnownDevice loaded{};
    assert(store.read(device.id, loaded) == KnownStoreStatus::Ok);
    assert(strcmp(loaded.name, "Tag") == 0 && fs.files.count(tmp) == 0);

    strcpy(device.name, "Tag Updated");
    assert(store.update(device) == KnownStoreStatus::Ok);
    const std::string lkg = path(device.id, "lkg");
    assert(fs.files.count(lkg) == 1);
    fs.files[main][30] ^= 0x44;
    assert(store.read(device.id, loaded) == KnownStoreStatus::Ok);
    assert(strcmp(loaded.name, "Tag") == 0);

    fs.files[main][16] ^= 1;
    fs.files.erase(lkg);
    assert(store.read(device.id, loaded) == KnownStoreStatus::InvalidRecord);
}

static void testFailuresAndTombstones() {
    MemoryFs fs; KnownDeviceStore store(fs);
    KnownDevice device = sample("Phone", "01:02:03:04:05:06");
    assert(store.create(device) == KnownStoreStatus::Ok);
    const std::string main = path(device.id, "kdev");
    const std::string tmp = path(device.id, "tmp");
    fs.failRenameFrom = tmp;
    strcpy(device.name, "Never committed");
    assert(store.update(device) == KnownStoreStatus::RecoveryFailed);
    fs.failRenameFrom.clear();
    KnownDevice loaded{};
    assert(store.read(device.id, loaded) == KnownStoreStatus::Ok);
    assert(strcmp(loaded.name, "Phone") == 0);

    fs.capacity = 100;
    KnownDevice another = sample("No room", "11:11:11:11:11:11");
    assert(store.create(another) == KnownStoreStatus::Full && another.id == 0);
    fs.capacity = 1024 * 1024;

    fs.failRemovePath = main;
    assert(store.deleteDevice(device.id) == KnownStoreStatus::RecoveryFailed);
    assert(store.read(device.id, loaded) == KnownStoreStatus::NotFound);
    fs.files[path(device.id, "del")][19] ^= 1;
    assert(store.read(device.id, loaded) == KnownStoreStatus::Ok);

    fs.online = false;
    assert(store.read(device.id, loaded) == KnownStoreStatus::Unavailable);
}

static void testValidation() {
    MemoryFs fs; KnownDeviceStore store(fs);
    KnownDevice device = sample("Bad", "AA:BB:CC:DD:EE:FF");
    device.radio = static_cast<KnownRadio>(9);
    assert(store.create(device) == KnownStoreStatus::InvalidArgument);
    device = sample("Bad address type", "AA:BB:CC:DD:EE:FF");
    device.addresses[0].addressType = 4;
    assert(store.create(device) == KnownStoreStatus::InvalidArgument);

    device = sample("Resolvable private", "AA:BB:CC:DD:EE:02");
    device.addresses[0].addressType = 2;
    device.addressCount = 2;
    strcpy(device.addresses[1].address, "AA:BB:CC:DD:EE:03");
    device.addresses[1].addressType = 3;
    assert(store.create(device) == KnownStoreStatus::Ok);
    KnownDevice loaded{};
    assert(store.read(device.id, loaded) == KnownStoreStatus::Ok);
    assert(loaded.addresses[0].addressType == 2 && loaded.addresses[1].addressType == 3);
}

int main() {
    testCrudAndPagination();
    testRecoveryAndMalformedData();
    testFailuresAndTombstones();
    testValidation();
    std::cout << "known_device_store_test passed\n";
}
