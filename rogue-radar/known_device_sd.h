#pragma once

#include <Arduino.h>
#include <SD.h>
#include <string.h>

#include "known_device_store.h"

// Implemented by the board layer in rogue-radar.ino. It selects the shared SPI
// bus safely and mounts the card at the board's configured frequency.
static bool boardMountSd();

namespace rogue_radar {

class KnownDeviceSdFileSystem : public KnownDeviceFileSystem {
 public:
    typedef bool (*MountCallback)();
    static const uint32_t kMountRetryMs = 5000;

    explicit KnownDeviceSdFileSystem(MountCallback mount) : mount_(mount) {}

    bool available() override { return mount(false); }
    bool retryAvailable() {
        // Explicit Saved Devices Retry owns this synchronous SD operation.
        // Drop cached Arduino driver state so physical removal/reinsertion is
        // not mistaken for a still-mounted card by SD.cardType().
        abortWrite();
        closeCachedHandles();
        SD.end();
        mounted_ = false;
        return mount(true);
    }

    bool mount(bool force) {
        closeCachedHandles();
        if (!force && mounted_ && SD.cardType() != CARD_NONE) return true;
        mounted_ = false;
        const uint32_t now = millis();
        if (!force && mountAttempted_ && now - lastMountAttemptMs_ < kMountRetryMs)
            return false;
        mountAttempted_ = true;
        lastMountAttemptMs_ = now;
        mounted_ = mount_ && mount_() && SD.cardType() != CARD_NONE;
        return mounted_;
    }

    bool ensureDirectory(const char *path) override {
        if (!path || path[0] != '/') return false;
        if (!SD.exists("/rogue-radar") && !SD.mkdir("/rogue-radar")) return false;
        return SD.exists(path) || SD.mkdir(path);
    }

    bool exists(const char *path) override { return path && SD.exists(path); }

    bool fileSize(const char *path, uint32_t &size) override {
        closeRead();
        File file = SD.open(path, FILE_READ);
        if (!file || file.isDirectory()) { if (file) file.close(); return false; }
        size = static_cast<uint32_t>(file.size());
        file.close();
        return true;
    }

    bool readAt(const char *path, uint32_t offset, void *data, size_t size) override {
        if (!path || !data || !openRead(path) || !readFile_.seek(offset)) return false;
        return readFile_.read(static_cast<uint8_t *>(data), size) == size;
    }

    bool beginWrite(const char *path) override {
        abortWrite();
        closeRead();
        closeList();
        fullError_ = false;
        capacityKnown_ = false;
        freeBytes_ = 0;
        if (!path || (SD.exists(path) && !SD.remove(path))) return false;
        writeFile_ = SD.open(path, FILE_WRITE);
        if (!writeFile_ || writeFile_.isDirectory()) {
            if (writeFile_) writeFile_.close();
            const uint64_t total = SD.totalBytes();
            fullError_ = total != 0 && SD.usedBytes() >= total;
            return false;
        }
        copyPath(writePath_, sizeof(writePath_), path);
        writeFailed_ = false;
        const uint64_t total = SD.totalBytes();
        const uint64_t used = SD.usedBytes();
        capacityKnown_ = total != 0 && used <= total;
        freeBytes_ = capacityKnown_ ? total - used : 0;
        return true;
    }

    bool appendWrite(const void *data, size_t size) override {
        if (!writeFile_ || writeFailed_) return false;
        if (capacityKnown_ && size > freeBytes_) {
            writeFailed_ = true;
            fullError_ = true;
            return false;
        }
        if (writeFile_.write(static_cast<const uint8_t *>(data), size) != size) {
            writeFailed_ = true;
            const uint64_t total = SD.totalBytes();
            fullError_ = capacityKnown_ && freeBytes_ < size;
            if (!fullError_ && total != 0) fullError_ = SD.usedBytes() >= total;
            return false;
        }
        if (capacityKnown_) freeBytes_ -= size;
        return true;
    }

    bool finishWrite() override {
        if (!writeFile_) return false;
        writeFile_.flush();
        writeFile_.close();
        const bool ok = !writeFailed_;
        writeFailed_ = false;
        writePath_[0] = '\0';
        return ok;
    }

    void abortWrite() override {
        if (writeFile_) writeFile_.close();
        if (writePath_[0]) SD.remove(writePath_);
        writePath_[0] = '\0';
        writeFailed_ = false;
    }

    bool removeFile(const char *path) override {
        closeCachedHandles();
        return path && (!SD.exists(path) || SD.remove(path));
    }

    bool renameFile(const char *from, const char *to) override {
        closeCachedHandles();
        if (!from || !to || !SD.exists(from)) return false;
        if (SD.exists(to) && !SD.remove(to)) return false;
        return SD.rename(from, to);
    }

    bool lastErrorIsFull() const override {
        const uint64_t total = SD.totalBytes();
        return fullError_ || (total != 0 && SD.usedBytes() >= total);
    }

    KnownFsListResult nextFile(const char *directory, uint32_t &cursor,
                               char *name, size_t nameSize) override {
        if (!directory || !name || nameSize == 0) return KnownFsListResult::Error;
        if (!listDir_ || strcmp(listPath_, directory) != 0 || cursor != listCursor_) {
            closeList();
            listDir_ = SD.open(directory, FILE_READ);
            if (!listDir_ || !listDir_.isDirectory()) {
                closeList();
                return KnownFsListResult::Error;
            }
            copyPath(listPath_, sizeof(listPath_), directory);
            listCursor_ = 0;
            while (listCursor_ < cursor) {
                File skipped = listDir_.openNextFile();
                if (!skipped) { closeList(); return KnownFsListResult::End; }
                skipped.close();
                ++listCursor_;
            }
        }
        File entry = listDir_.openNextFile();
        if (!entry) { closeList(); return KnownFsListResult::End; }
        ++listCursor_;
        cursor = listCursor_;
        const bool regular = !entry.isDirectory();
        copyPath(name, nameSize, entry.name());
        entry.close();
        if (!regular) return nextFile(directory, cursor, name, nameSize);
        return KnownFsListResult::Entry;
    }

 private:
    static void copyPath(char *dst, size_t capacity, const char *src) {
        if (!capacity) return;
        if (!src) { dst[0] = '\0'; return; }
        strncpy(dst, src, capacity - 1);
        dst[capacity - 1] = '\0';
    }

    bool openRead(const char *path) {
        if (readFile_ && strcmp(readPath_, path) == 0) return true;
        closeRead();
        readFile_ = SD.open(path, FILE_READ);
        if (!readFile_ || readFile_.isDirectory()) {
            closeRead();
            return false;
        }
        copyPath(readPath_, sizeof(readPath_), path);
        return true;
    }

    void closeRead() {
        if (readFile_) readFile_.close();
        readPath_[0] = '\0';
    }
    void closeList() {
        if (listDir_) listDir_.close();
        listPath_[0] = '\0';
        listCursor_ = 0;
    }
    void closeCachedHandles() { closeRead(); closeList(); }

    MountCallback mount_ = nullptr;
    File readFile_{};
    File writeFile_{};
    File listDir_{};
    char readPath_[64]{};
    char writePath_[64]{};
    char listPath_[64]{};
    uint32_t listCursor_ = 0;
    bool writeFailed_ = false;
    bool fullError_ = false;
    bool capacityKnown_ = false;
    uint64_t freeBytes_ = 0;
    bool mounted_ = false;
    bool mountAttempted_ = false;
    uint32_t lastMountAttemptMs_ = 0;
};

}  // namespace rogue_radar

static rogue_radar::KnownDeviceSdFileSystem &knownDeviceFileSystem() {
    static rogue_radar::KnownDeviceSdFileSystem fs(boardMountSd);
    return fs;
}

static rogue_radar::KnownDeviceStore &knownDeviceStore() {
    static rogue_radar::KnownDeviceStore store(knownDeviceFileSystem());
    return store;
}

static bool knownDeviceStorageReady(bool forceRetry = false) {
    rogue_radar::KnownDeviceSdFileSystem &fs = knownDeviceFileSystem();
    const bool available = forceRetry ? fs.retryAvailable() : fs.available();
    return available && fs.ensureDirectory(rogue_radar::KnownDeviceStore::directory());
}

static bool knownDeviceStorageRetry() { return knownDeviceStorageReady(true); }
