#pragma once
// Dedicated physical-board diagnostic. No commands are compiled into releases.
#if defined(ROGUE_RADAR_KNOWN_DEVICE_TEST) && ROGUE_RADAR_KNOWN_DEVICE_TEST
namespace known_device_test {
using namespace rogue_radar;
static uint32_t bootAt = 0, lastLog = 0, savedId = 0;
static bool started = false;
static char command[48]{};
static uint8_t commandLength = 0;
static KnownDevice *scratch = nullptr;
static KnownAddress addressScratch{};
static KnownDeviceSummary pageScratch[KnownDeviceStore::kMaxPageSize]{};
static void stack(const char *where) {
    const UBaseType_t words = uxTaskGetStackHighWaterMark(nullptr);
    Serial.printf("[KnownDiag] stack %s highwater=%u bytes\n", where,
                  unsigned(words * sizeof(StackType_t)));
}
static void memory() {
    lv_mem_monitor_t m; lv_mem_monitor(&m);
    Serial.printf("[KnownDiag] memory heap=%u psram=%u lvgl=%u\n",
        ESP.getFreeHeap(), ESP.getFreePsram(), unsigned(m.free_size));
}
static void resetScratch() {
    resetKnownDevice(*scratch);
}
static bool ensureScratch() {
    if (scratch) return true;
    void *memory = ps_malloc(sizeof(KnownDevice));
    if (!memory) { Serial.println("[KnownDiag] ASSERTFAIL scratch allocation"); return false; }
    scratch = new (memory) KnownDevice;
    resetScratch();
    return true;
}
static bool check(KnownStoreStatus status, const char *step) {
    Serial.printf("[KnownDiag] SD %s: %s\n", step, knownStoreStatusText(status));
    return status == KnownStoreStatus::Ok;
}
static void storageTest() {
    stack("storage begin");
    if (!knownDeviceStorageReady()) { Serial.println("[KnownDiag] ASSERTFAIL SD mount"); return; }
    if (!ensureScratch()) return;
    resetScratch();
    auto &d = *scratch;
    snprintf(d.name, sizeof(d.name), "Diagnostic temporary");
    d.radio = KnownRadio::Wifi; d.addressCount = 1;
    snprintf(d.addresses[0].address, sizeof(d.addresses[0].address), "02:00:00:00:00:01");
    d.addresses[0].channel = 1;
    if (!check(knownDeviceStore().create(d), "create temporary")) return;
    const uint32_t id = d.id;
    if (!check(knownDeviceStore().read(id, d), "read")) return;
    if (!check(knownDeviceStore().renameDevice(id, "Temporary renamed"), "rename")) return;
    addressScratch = d.addresses[0];
    strcpy(addressScratch.address, "02:00:00:00:00:02");
    if (!check(knownDeviceStore().addAddress(id, addressScratch), "add second address")) return;
    if (!check(knownDeviceStore().read(id, d), "reread") || d.addressCount != 2) return;
    if (!check(knownDeviceStore().removeAddress(id, 1), "remove address")) return;
    if (!check(knownDeviceStore().deleteDevice(id), "delete temporary")) return;
    Serial.printf("[KnownDiag] SD deleted=%s\n", knownStoreStatusText(knownDeviceStore().read(id, d)));
    Serial.println("[KnownDiag] PASS physical SD CRUD");
    stack("storage end");
}

enum class SavedLookup : uint8_t { Found, NotFound, Error };
static SavedLookup findSavedIdentity(KnownRadio radio, const char *addressValue,
                                     uint8_t addressType) {
    if (!knownDeviceStorageReady() || !ensureScratch()) return SavedLookup::Error;
    uint32_t cursor = 0, next = 0;
    uint8_t count = 0;
    do {
        if (knownDeviceStore().listPage(cursor, pageScratch, count, next) != KnownStoreStatus::Ok)
            return SavedLookup::Error;
        for (uint8_t i = 0; i < count; ++i) {
            if (pageScratch[i].radio != radio) continue;
            if (knownDeviceStore().read(pageScratch[i].id, *scratch) != KnownStoreStatus::Ok)
                return SavedLookup::Error;
            for (uint8_t j = 0; j < scratch->addressCount; ++j) {
                const auto &address = scratch->addresses[j];
                if (address.addressType == addressType &&
                    strcasecmp(address.address, addressValue) == 0) {
                    savedId = scratch->id;
                    return SavedLookup::Found;
                }
            }
        }
        if (next == cursor) break;
        cursor = next;
    } while (count == KnownDeviceStore::kMaxPageSize);
    return SavedLookup::NotFound;
}

static void listSaved() {
    stack("list saved begin");
    if (!knownDeviceStorageReady() || !ensureScratch()) {
        Serial.println("[KnownDiag] ASSERTFAIL saved list unavailable"); return;
    }
    uint32_t cursor = 0, next = 0;
    uint8_t count = 0;
    unsigned records = 0;
    do {
        KnownStoreStatus result = knownDeviceStore().listPage(cursor, pageScratch, count, next);
        if (!check(result, "list saved")) return;
        for (uint8_t i = 0; i < count; ++i) {
            if (!check(knownDeviceStore().read(pageScratch[i].id, *scratch), "read saved")) continue;
            ++records;
            Serial.printf("[KnownDiag] saved id=%u name=%.32s radio=%u addresses=%u\n",
                          scratch->id, scratch->name, unsigned(scratch->radio),
                          scratch->addressCount);
            for (uint8_t j = 0; j < scratch->addressCount; ++j)
                Serial.printf("[KnownDiag]   address=%s type=%u rssi=%d\n",
                              scratch->addresses[j].address,
                              scratch->addresses[j].addressType,
                              scratch->addresses[j].lastRssi);
        }
        if (next == cursor) break;
        cursor = next;
    } while (count == KnownDeviceStore::kMaxPageSize);
    Serial.printf("[KnownDiag] PASS persistence list records=%u\n", records);
    stack("list saved end");
}
static bool isSyntheticCrudRecord(const KnownDevice &device) {
    return device.radio == KnownRadio::Wifi && device.addressCount > 0 &&
           (!strcmp(device.name, "Diagnostic temporary") ||
            !strcmp(device.name, "Temporary renamed")) &&
           !strcmp(device.addresses[0].address, "02:00:00:00:00:01");
}
static void cleanupSyntheticRecords() {
    stack("cleanup synthetic begin");
    if (!knownDeviceStorageReady() || !ensureScratch()) {
        Serial.println("[KnownDiag] ASSERTFAIL synthetic cleanup unavailable"); return;
    }
    unsigned removed = 0;
    bool removedOne = false;
    do {
        removedOne = false;
        uint32_t cursor = 0, next = 0;
        uint8_t count = 0;
        do {
            if (!check(knownDeviceStore().listPage(cursor, pageScratch, count, next),
                       "list synthetic cleanup")) return;
            for (uint8_t i = 0; i < count; ++i) {
                if (knownDeviceStore().read(pageScratch[i].id, *scratch) != KnownStoreStatus::Ok ||
                    !isSyntheticCrudRecord(*scratch))
                    continue;
                if (!check(knownDeviceStore().deleteDevice(scratch->id), "delete synthetic")) return;
                ++removed;
                removedOne = true;
                break;
            }
            if (removedOne || next == cursor) break;
            cursor = next;
        } while (count == KnownDeviceStore::kMaxPageSize);
    } while (removedOne);
    Serial.printf("[KnownDiag] PASS synthetic cleanup removed=%u\n", removed);
    stack("cleanup synthetic end");
}
static void status() {
    auto &s = nearby_ui::state;
    int16_t trackerRaw = 0;
    float trackerSmoothed = 0.0f;
    const bool trackerFresh = signalTrackerActive &&
                              trackerModel.current(millis(), trackerRaw, trackerSmoothed);
    int trackerAgeBucketMs = -1;
    if (signalTrackerActive && trackerHasSample) {
        for (int i = int(SignalTrackerModel::kBucketCount) - 1; i >= 0; --i) {
            if (trackerModel.bucket(size_t(i)).present()) {
                trackerAgeBucketMs =
                    (int(SignalTrackerModel::kBucketCount) - 1 - i) *
                    int(SignalTrackerModel::kBucketMs);
                break;
            }
        }
    }
    Serial.printf("[KnownDiag] tracker active=%u fresh=%u raw=%d smooth=%.1f ageBucketMs=%d radio=%s\n",
                  signalTrackerActive ? 1U : 0U, trackerFresh ? 1U : 0U,
                  trackerRaw, trackerSmoothed, trackerAgeBucketMs,
                  signalTrackerActive ? trackerRadio.status() : "idle");
    auto *saved = known_devices_ui::current();
    Serial.printf("[KnownDiag] savedUI active=%u depth=%d view=%u recordId=%u addressIndex=%u searching=%u draining=%u\n",
                  saved ? 1U : 0U, known_devices_ui::depth,
                  saved ? unsigned(saved->page) : 0U,
                  saved ? saved->record.id : 0U,
                  saved ? saved->addressIndex : 0U,
                  saved && saved->searching ? 1U : 0U,
                  saved && saved->draining ? 1U : 0U);
    if (s.table) {
        Serial.printf("[KnownDiag] nearby count=%u radio=%u view=%u backend=%s\n",
            unsigned(s.table->count()), unsigned(s.radio), unsigned(s.view), knownDiscoveryStatus());
        for (unsigned i = 0; i < s.table->count(); ++i) {
            auto *c = s.table->candidateAtRank(i);
            Serial.printf("[KnownDiag] candidate %u name=%.32s addr=%s type=%u rssi=%d stale=%u\n",
                i, c->observation.advertisedName, c->identity.address,
                c->identity.addressType, c->smoothedRssi, unsigned(c->stale));
        }
    }
    if (s.learning && s.view == nearby_ui::View::Learn) {
        auto m = s.learning->evaluate(millis());
        Serial.printf("[KnownDiag] learn phase=%u ready=%u outcome=%u samples=%u/%u/%u means=%d/%d/%d\n",
            unsigned(s.learning->phase()), s.learning->currentPhaseReady(millis()), unsigned(m.outcome),
            m.selected[0].samples, m.selected[1].samples, m.selected[2].samples,
            m.selected[0].meanRssi, m.selected[1].meanRssi, m.selected[2].meanRssi);
    }
    memory();
    stack("status");
}
static void execute(const char *cmd) {
    auto &s = nearby_ui::state;
    if (!strcmp(cmd, "status")) { status(); return; }
    if (!strcmp(cmd, "sd")) { knownDeviceStorageRetry(); storageTest(); return; }
    if (!strcmp(cmd, "cleanup-synthetic")) { cleanupSyntheticRecords(); return; }
    if (!strcmp(cmd, "name") && knownDevicesActive()) {
        known_devices_ui::request(known_devices_ui::Action::NameNew); return;
    }
    if (!strcmp(cmd, "cancel") && keyboardActive) { keyboardFinish(false); return; }
    if (!strcmp(cmd, "nearby") && !s.active && !knownDevicesActive()) { createNearbySignals(); return; }
    if (!strncmp(cmd, "select ", 7) && s.table) { nearby_ui::selectRank(atoi(cmd + 7)); return; }
    if (!strcmp(cmd, "learn") && s.view == nearby_ui::View::Detail) { nearby_ui::cbStartLearn(nullptr); return; }
    if (!strcmp(cmd, "capture") && s.view == nearby_ui::View::Learn) { nearby_ui::cbCaptureLearn(nullptr); return; }
    if (!strcmp(cmd, "next") && s.learning && s.view == nearby_ui::View::Learn) {
        if (!s.learning->currentPhaseReady(millis())) { Serial.println("[KnownDiag] phase not ready"); return; }
        nearby_ui::cbAdvanceLearn(nullptr); return;
    }
    if (!strcmp(cmd, "back")) {
        if (signalTrackerActive) cb_trackerBack(nullptr);
        else if (knownDevicesActive() && lv_screen_active() == known_devices_ui::current()->screen)
            known_devices_ui::request(known_devices_ui::Action::Back);
        else if (s.active) nearby_ui::cbBack(nullptr);
        return;
    }
    if (!strcmp(cmd, "wifi") && s.active) { nearby_ui::cbSwitchRadio(nullptr); return; }
    if (!strcmp(cmd, "track") && s.active) { nearby_ui::cbTrack(nullptr); return; }
    if (!strcmp(cmd, "save-ui") && s.active) { nearby_ui::cbSave(nullptr); return; }
    if (!strncmp(cmd, "save-verified ", 14)) {
        char requestedAddress[18]{};
        unsigned requestedType = 0;
        char trailing = 0;
        if (sscanf(cmd + 14, "%17s %u %c", requestedAddress, &requestedType, &trailing) != 2 ||
            requestedType > 255U) {
            Serial.println("[KnownDiag] usage: save-verified <address> <type>"); return;
        }
        if (!s.active || s.radio != KnownRadio::Ble ||
            (s.view != nearby_ui::View::Detail && s.view != nearby_ui::View::Learn) ||
            strcasecmp(s.selectedObservation.address, requestedAddress) != 0 ||
            s.selectedObservation.addressType != requestedType) {
            Serial.println("[KnownDiag] refusing save: command identity is not selected Nearby BLE identity");
            return;
        }
        stack("save verified begin");
        const SavedLookup lookup =
            findSavedIdentity(KnownRadio::Ble, requestedAddress, uint8_t(requestedType));
        if (lookup == SavedLookup::Error) {
            Serial.println("[KnownDiag] refusing save: identity lookup failed");
            stack("save verified lookup error");
            return;
        }
        if (lookup == SavedLookup::Found) {
            Serial.printf("[KnownDiag] PASS verified identity already saved id=%u (no duplicate)\n", savedId);
            stack("save verified existing");
            return;
        }
        if (!ensureScratch()) return;
        resetScratch();
        strcpy(scratch->name, "MyAmazfit");
        scratch->addressCount = 1;
        scratch->addresses[0] = s.selectedObservation;
        if (check(knownDeviceStore().create(*scratch), "save verified MyAmazfit")) {
            savedId = scratch->id;
            ++known_devices_ui::libraryGeneration;
            Serial.printf("[KnownDiag] PASS saved MyAmazfit id=%u\n", savedId);
        }
        stack("save verified end");
        return;
    }
    if (!strcmp(cmd, "list-saved")) { listSaved(); return; }
    if (!strcmp(cmd, "saved") && !s.active && !knownDevicesActive()) { createSavedDevices(); return; }
    if (!strncmp(cmd, "pick ", 5) && knownDevicesActive()) {
        known_devices_ui::request(known_devices_ui::Action::Pick, atoi(cmd + 5)); return;
    }
    if (!strcmp(cmd, "saved-track") && knownDevicesActive()) {
        known_devices_ui::request(known_devices_ui::Action::Track); return;
    }
    if (!strncmp(cmd, "track-saved ", 12)) {
        unsigned id = 0, addressIndex = 0;
        char trailing = 0;
        if (sscanf(cmd + 12, "%u %u %c", &id, &addressIndex, &trailing) != 2) {
            Serial.println("[KnownDiag] usage: track-saved <id> <address-index>"); return;
        }
        if (s.active || knownDevicesActive() || signalTrackerActive || !ensureScratch()) {
            Serial.println("[KnownDiag] saved tracker unavailable while another workflow is active"); return;
        }
        if (!check(knownDeviceStore().read(id, *scratch), "read tracker target") ||
            addressIndex >= scratch->addressCount) {
            Serial.println("[KnownDiag] invalid saved tracker identity"); return;
        }
        const auto &address = scratch->addresses[addressIndex];
        stack("saved tracker begin");
        createSignalTracker(scratch->radio == KnownRadio::Ble, scratch->name, address.address,
                            address.channel, address.addressType);
        return;
    }
    Serial.printf("[KnownDiag] command unavailable: %s\n", cmd);
}
static void begin() { bootAt = millis(); }
static void process() {
    if (!started && millis() - bootAt > 2500 && !lv_display_get_screen_prev(lvDisp)) {
        started = true; storageTest(); memory();
        Serial.println("[KnownDiag] READY commands: sd/cleanup-synthetic/list-saved/nearby/status/select N/learn/capture/next/back/wifi/track/save-ui/name/cancel/save-verified ADDRESS TYPE/saved/pick N/saved-track/track-saved ID ADDRESS_INDEX");
    }
    if (!started) return;
    while (Serial.available()) {
        const char ch = Serial.read();
        if (ch == '\n' || ch == '\r') {
            if (commandLength) { command[commandLength] = 0; execute(command); commandLength = 0; }
        } else if (commandLength < sizeof(command) - 1) command[commandLength++] = ch;
    }
    if (millis() - lastLog > 10000) { lastLog = millis(); memory(); }
}
}
#endif
