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
static void memory() {
    lv_mem_monitor_t m; lv_mem_monitor(&m);
    Serial.printf("[KnownDiag] memory heap=%u psram=%u lvgl=%u\n",
        ESP.getFreeHeap(), ESP.getFreePsram(), unsigned(m.free_size));
}
static bool check(KnownStoreStatus status, const char *step) {
    Serial.printf("[KnownDiag] SD %s: %s\n", step, knownStoreStatusText(status));
    return status == KnownStoreStatus::Ok;
}
static void storageTest() {
    if (!knownDeviceStorageReady()) { Serial.println("[KnownDiag] ASSERTFAIL SD mount"); return; }
    if (!scratch) {
        void *memory = ps_malloc(sizeof(KnownDevice));
        if (!memory) { Serial.println("[KnownDiag] ASSERTFAIL scratch allocation"); return; }
        scratch = new (memory) KnownDevice();
    }
    *scratch = KnownDevice{};
    auto &d = *scratch;
    snprintf(d.name, sizeof(d.name), "Diagnostic temporary");
    d.radio = KnownRadio::Wifi; d.addressCount = 1;
    snprintf(d.addresses[0].address, sizeof(d.addresses[0].address), "02:00:00:00:00:01");
    d.addresses[0].channel = 1;
    if (!check(knownDeviceStore().create(d), "create temporary")) return;
    const uint32_t id = d.id;
    if (!check(knownDeviceStore().read(id, d), "read")) return;
    if (!check(knownDeviceStore().renameDevice(id, "Temporary renamed"), "rename")) return;
    KnownAddress a = d.addresses[0]; strcpy(a.address, "02:00:00:00:00:02");
    if (!check(knownDeviceStore().addAddress(id, a), "add second address")) return;
    if (!check(knownDeviceStore().read(id, d), "reread") || d.addressCount != 2) return;
    if (!check(knownDeviceStore().removeAddress(id, 1), "remove address")) return;
    if (!check(knownDeviceStore().deleteDevice(id), "delete temporary")) return;
    Serial.printf("[KnownDiag] SD deleted=%s\n", knownStoreStatusText(knownDeviceStore().read(id, d)));
    Serial.println("[KnownDiag] PASS physical SD CRUD");
}
static void status() {
    auto &s = nearby_ui::state;
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
}
static void execute(const char *cmd) {
    auto &s = nearby_ui::state;
    if (!strcmp(cmd, "status")) { status(); return; }
    if (!strcmp(cmd, "sd")) { knownDeviceStorageRetry(); storageTest(); return; }
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
    if (!strcmp(cmd, "save-myamazfit") && scratch && s.learning &&
        s.learning->phase() == LearningPhase::Complete) {
        if (s.learning->evaluate(millis()).outcome != LearningOutcome::ConsistentResponse) {
            Serial.println("[KnownDiag] refusing save: evidence not consistent"); return;
        }
        *scratch = KnownDevice{}; scratch->radio = KnownRadio::Ble;
        strcpy(scratch->name, "MyAmazfit"); scratch->addressCount = 1;
        scratch->addresses[0] = s.selectedObservation;
        if (check(knownDeviceStore().create(*scratch), "save MyAmazfit")) {
            savedId = scratch->id; ++known_devices_ui::libraryGeneration;
            Serial.printf("[KnownDiag] saved MyAmazfit id=%u\n", savedId);
        }
        return;
    }
    if (!strcmp(cmd, "saved") && !s.active && !knownDevicesActive()) { createSavedDevices(); return; }
    if (!strncmp(cmd, "pick ", 5) && knownDevicesActive()) {
        known_devices_ui::request(known_devices_ui::Action::Pick, atoi(cmd + 5)); return;
    }
    if (!strcmp(cmd, "saved-track") && knownDevicesActive()) {
        known_devices_ui::request(known_devices_ui::Action::Track); return;
    }
    Serial.printf("[KnownDiag] command unavailable: %s\n", cmd);
}
static void begin() { bootAt = millis(); }
static void process() {
    if (!started && millis() - bootAt > 2500 && !lv_display_get_screen_prev(lvDisp)) {
        started = true; storageTest(); memory();
        Serial.println("[KnownDiag] READY commands: sd/nearby/status/select N/learn/capture/next/back/wifi/track/save-ui/name/cancel/save-myamazfit/saved/pick N/saved-track");
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
