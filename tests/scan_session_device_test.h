#pragma once

// Include this file near the end of rogue-radar.ino only in a dedicated
// diagnostic build, then call scanSessionDeviceTestBegin() from setup() and
// scanSessionDeviceTestProcess() from loop(). It is intentionally excluded
// from normal firmware builds.
#if defined(ROGUE_RADAR_SCAN_SESSION_DEVICE_TEST) && ROGUE_RADAR_SCAN_SESSION_DEVICE_TEST

namespace scan_session_device_test {

struct Page {
    const char *name;
    void (*create)();
    bool ble;
};

static const Page kPages[] = {
    {"Network Scanner", createNetworkScanner, false},
    {"Station Scanner", createStationScanner, false},
    {"Deauth Detector", createDeauthDetector, false},
    {"Channel Analyzer", createChannelAnalyzer, false},
    {"Packet Monitor", createPacketMonitor, false},
    {"WiFi Mapper", createWiFiMapper, false},
    {"PineAP Hunter", createPineAPHunter, false},
    {"Pwnagotchi Detector", createPwnagotchiDetector, false},
    {"Flock Detector", createFlockDetector, false},
    {"Flock Hybrid", createFlockHybridScanner, false},
    {"BLE Scanner", createBLEScanner, true},
    {"AirTag Detector", createAirTagScanner, true},
    {"Flipper Detector", createFlipperScanner, true},
    {"nyanBOX Detector", createNyanBoxDetector, true},
    {"Axon Detector", createAxonDetector, true},
    {"Raven Detector", createRavenDetector, true},
    {"Smart Charger", createSmartChargerMonitor, true},
    {"Tesla Detector", createTeslaDetector, true},
    {"Skimmer Scanner", createSkimmerScanner, true},
    {"Meta Detector", createMetaDetector, true},
};
static_assert(sizeof(kPages) / sizeof(kPages[0]) == 20,
              "device diagnostic must cover all 20 shared scan pages");

enum class Stage : uint8_t {
    WaitBoot, WaitMenu, WaitBleMenu, WaitPage, WaitRunning, RunPage, WaitDetail,
    WaitTracker, RunTracker, WaitTrackerReturn, WaitResume, RunResumed,
    WaitIdle, WaitBack, BleSoakWaitPage, BleSoakWaitRunning, BleSoak,
    BleSoakWaitIdle, BleSoakWaitBack, HybridMenuWait, HybridSoakWaitPage,
    HybridSoakWaitRunning, HybridSoak, HybridSoakWaitBack,
    Done, Failed
};

static Stage stage = Stage::WaitBoot;
static uint8_t pageIndex = 0;
static uint8_t pagePass = 0;
static uint32_t stageStartedMs = 0;
static uint32_t actionAtMs = 0;
static uint32_t lastMemoryLogMs = 0;
static uint32_t minHeap = UINT32_MAX;
static uint32_t minPsram = UINT32_MAX;
static uint32_t minLvglFree = UINT32_MAX;
static lv_obj_t *detailScreen = nullptr;
static lv_group_t *detailGroup = nullptr;
static bool trackerIsBle = false;
static bool pocketChecked = false;
static bool autoHomeChecked = false;

static bool reached(uint32_t deadline) {
    return static_cast<int32_t>(millis() - deadline) >= 0;
}

static void setStage(Stage next, uint32_t delayMs = 0) {
    stage = next;
    stageStartedMs = millis();
    actionAtMs = millis() + delayMs;
}

static void requestBackendStop() {
    scanSession.resumeOnReturn = false;
    scanSession.navigate = nullptr;
    scanSession.exit = nullptr;
    if (scanSession.state != ScanUiState::Idle)
        scanSession.state = ScanUiState::Stopping;
}

static void returnFromDetail();

static void fail(const char *reason) {
    Serial.printf("[ScanDiag] ASSERTFAIL page=%u (%s) stage=%u: %s\n",
                  pageIndex,
                  pageIndex < sizeof(kPages) / sizeof(kPages[0])
                      ? kPages[pageIndex].name : "soak",
                  static_cast<unsigned>(stage), reason);
    if (signalTrackerActive) cb_trackerBack(nullptr);
    requestBackendStop();
    if (!signalTrackerActive && detailScreen && scanSession.screen &&
        scanSession.group && *scanSession.group) returnFromDetail();
    stage = Stage::Failed;
}

static bool waitTimedOut(uint32_t timeoutMs, const char *reason) {
    if (millis() - stageStartedMs <= timeoutMs) return false;
    fail(reason);
    return true;
}

static void logMemory(const char *phase) {
    lv_mem_monitor_t monitor{};
    lv_mem_monitor(&monitor);
    const uint32_t heap = ESP.getFreeHeap();
    const uint32_t psram = ESP.getFreePsram();
    if (heap < minHeap) minHeap = heap;
    if (psram < minPsram) minPsram = psram;
    if (monitor.free_size < minLvglFree) minLvglFree = monitor.free_size;
    Serial.printf("[ScanDiag] %s heap=%lu psram=%lu lvgl=%lu largest=%lu state=%u count=%d\n",
                  phase, (unsigned long)heap, (unsigned long)psram,
                  (unsigned long)monitor.free_size,
                  (unsigned long)monitor.free_biggest_size,
                  static_cast<unsigned>(scanSession.state),
                  scanSession.ops.count ? scanSession.ops.count() : 0);
}

static void openDetail(int) {
    detailScreen = lv_obj_create(nullptr);
    applyScreenStyle(detailScreen);
    createHeader(detailScreen, "Scan diagnostic detail");
    lv_obj_t *label = lv_label_create(detailScreen);
    lv_label_set_text(label, "Synthetic result: scanner is drained");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    detailGroup = lv_group_create();
    lv_obj_t *back = createBackBtn(detailScreen, [](lv_event_t *) { returnFromDetail(); });
    lv_group_add_obj(detailGroup, back);
    setGroup(detailGroup);
    lv_screen_load_anim(detailScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}

static void returnFromDetail() {
    if (!detailScreen || !scanSession.screen || !scanSession.group || !*scanSession.group) {
        fail("synthetic detail lost owner screen/group");
        return;
    }
    lv_obj_t *old = detailScreen;
    detailScreen = nullptr;
    setGroup(*scanSession.group);
    deleteGroup(&detailGroup);
    lv_screen_load_anim(scanSession.screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, true);
    (void)old;  // LVGL owns the outgoing screen until the animation completes.
}

static void click(lv_obj_t *object) {
    if (!object) {
        fail("missing shared control");
        return;
    }
    lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr);
}

// Diagnostic mode changes are deliberately transient: do not exercise the UI
// mode button because that would write the tester's preference to NVS.
static void configureMode(bool continuous, uint32_t durationMs) {
    if (scanSession.state != ScanUiState::Idle) {
        fail("attempted to configure a busy scan session");
        return;
    }
    scanSession.continuous = continuous;
    scanSession.durationMs = durationMs;
    scanPaintControls();
}

static void openPage() {
    Serial.printf("[ScanDiag] OPEN %u/20 %s pass=%u\n",
                  static_cast<unsigned>(pageIndex + 1), kPages[pageIndex].name,
                  static_cast<unsigned>(pagePass + 1));
    kPages[pageIndex].create();
    logMemory("opened");
    setStage(Stage::WaitPage);
}

static void advancePage() {
    pagePass = 0;
    ++pageIndex;
    if (pageIndex >= sizeof(kPages) / sizeof(kPages[0])) {
        // The last basic page returns to the BLE menu, ready for the BLE soak.
        createBLEScanner();
        setStage(Stage::BleSoakWaitPage);
        return;
    }
    if (pageIndex == 10) {
        cb_wifiMenuBack(nullptr);
        setStage(Stage::WaitBleMenu, 350);
        return;
    }
    openPage();
}

static void begin() {
    Serial.println("[ScanDiag] BEGIN 20-page lifecycle + BLE/Hybrid soak");
    pageIndex = pagePass = 0;
    minHeap = minPsram = minLvglFree = UINT32_MAX;
    pocketChecked = autoHomeChecked = false;
    setStage(Stage::WaitBoot, 1500);
}

static void process() {
    if (stage == Stage::Done || stage == Stage::Failed) return;

    switch (stage) {
        case Stage::WaitBoot:
            if (!reached(actionAtMs)) return;
            createWiFiMenu();
            setStage(Stage::WaitMenu, 350);
            return;

        case Stage::WaitMenu:
            if (!reached(actionAtMs) || lv_display_get_screen_prev(lvDisp)) return;
            openPage();
            return;

        case Stage::WaitBleMenu:
            if (!reached(actionAtMs) || lv_display_get_screen_prev(lvDisp)) return;
            createBLEMenu();
            setStage(Stage::WaitMenu, 350);
            return;

        case Stage::WaitPage:
            if (waitTimedOut(5000, "page did not become active")) return;
            if (lv_display_get_screen_prev(lvDisp) || lv_screen_active() != scanSession.screen) return;
            // The first pass must remain live through navigation regardless of
            // the user's saved short duration. The second pass proves that the
            // shared controller stops at its deadline without a Stop click.
            configureMode(pagePass == 0, pagePass == 0 ? 20000U : 1500U);
            if (stage == Stage::Failed) return;
            click(scanSession.start);
            setStage(Stage::WaitRunning);
            return;

        case Stage::WaitRunning:
            if (waitTimedOut(8000, "backend did not enter Running")) return;
            if (scanSession.state == ScanUiState::Stopping && scanSession.error[0]) {
                fail(scanSession.error); return;
            }
            if (scanSession.state != ScanUiState::Running) return;
            logMemory("running");
            setStage(Stage::RunPage, pagePass == 0 ? 3000 : 2500);
            return;

        case Stage::RunPage:
            if (pagePass == 0 && scanSession.state != ScanUiState::Running) {
                fail("continuous scan stopped unexpectedly"); return;
            }
            if (!reached(actionAtMs)) return;
            if (pagePass != 0) {
                if (scanSession.state == ScanUiState::Running) {
                    fail("timed scan did not expire automatically"); return;
                }
                setStage(Stage::WaitIdle);
                return;
            }
            scanRequestNavigation(openDetail, pageIndex);
            setStage(Stage::WaitDetail);
            return;

        case Stage::WaitDetail:
            if (waitTimedOut(8000, "detail navigation did not drain scanner")) return;
            if (!detailScreen || lv_screen_active() != detailScreen ||
                lv_display_get_screen_prev(lvDisp)) return;
            if (pageIndex == 0 || pageIndex == 10) {
                trackerIsBle = pageIndex == 10;
                createSignalTracker(trackerIsBle, "Diagnostic target",
                                    "02:00:00:00:00:01", trackerIsBle ? 0 : 6);
                setStage(Stage::WaitTracker);
            } else {
                returnFromDetail();
                setStage(Stage::WaitResume);
            }
            return;

        case Stage::WaitTracker:
            if (waitTimedOut(8000, "signal tracker did not open")) return;
            if (!signalTrackerActive || lv_screen_active() != trackerScreen ||
                lv_display_get_screen_prev(lvDisp)) return;
            setStage(Stage::RunTracker, 3000);
            return;

        case Stage::RunTracker:
            if (!reached(actionAtMs)) return;
            cb_trackerBack(nullptr);
            setStage(Stage::WaitTrackerReturn);
            return;

        case Stage::WaitTrackerReturn:
            if (waitTimedOut(10000, "signal tracker did not drain/return")) return;
            if (signalTrackerActive || !detailScreen || lv_screen_active() != detailScreen ||
                lv_display_get_screen_prev(lvDisp)) return;
            returnFromDetail();
            setStage(Stage::WaitResume);
            return;

        case Stage::WaitResume:
            if (waitTimedOut(10000, "owner backend did not resume")) return;
            if (lv_screen_active() != scanSession.screen || lv_display_get_screen_prev(lvDisp) ||
                scanSession.state != ScanUiState::Running) return;
            setStage(Stage::RunResumed, 1000);
            return;

        case Stage::RunResumed:
            if (scanSession.state != ScanUiState::Running) {
                fail("resumed scan stopped unexpectedly"); return;
            }
            if (!reached(actionAtMs)) return;
            // Exercise the real deferred Back gate while a backend owns its
            // radio. Other pages use explicit Stop followed by Back.
            if (pageIndex == 1 || pageIndex == 9 || pageIndex == 11) {
                click(scanSession.back);
                setStage(Stage::WaitBack);
                return;
            }
            click(scanSession.start);
            setStage(Stage::WaitIdle);
            return;

        case Stage::WaitIdle:
            if (waitTimedOut(10000, "backend did not stop/release")) return;
            if (scanSession.state != ScanUiState::Idle) return;
            click(scanSession.back);
            setStage(Stage::WaitBack);
            return;

        case Stage::WaitBack:
            if (waitTimedOut(5000, "Back did not return to family menu")) return;
            // prev_scr is also null BEFORE a queued animation starts. Wait for
            // the destination itself, not just absence of an outgoing screen.
            if (scanSessionAttached() || lv_display_get_screen_prev(lvDisp) ||
                lv_screen_active() != (kPages[pageIndex].ble ? bleMenuScreen : wifiMenuScreen)) return;
            if (pagePass == 0) {
                pagePass = 1;
                openPage();
            } else {
                Serial.printf("[ScanDiag] PASS page %s\n", kPages[pageIndex].name);
                advancePage();
            }
            return;

        case Stage::BleSoakWaitPage:
            if (waitTimedOut(5000, "BLE soak page did not open")) return;
            if (lv_display_get_screen_prev(lvDisp) || lv_screen_active() != scanSession.screen) return;
            configureMode(true, scanSession.durationMs);
            if (stage == Stage::Failed) return;
            click(scanSession.start);
            setStage(Stage::BleSoakWaitRunning);
            return;

        case Stage::BleSoakWaitRunning:
            if (waitTimedOut(8000, "BLE soak did not start")) return;
            if (scanSession.state != ScanUiState::Running) return;
            lastMemoryLogMs = millis();
            setStage(Stage::BleSoak, 90000);
            return;

        case Stage::BleSoak:
            if (scanSession.state != ScanUiState::Running) {
                fail("BLE soak stopped unexpectedly"); return;
            }
            if (!pocketChecked && millis() - stageStartedMs >= 10000) {
#if RR_BACK_BUTTON_PIN >= 0
                lv_obj_t *owner = scanSession.screen;
                enterPocketMode();
                if (!pocketModeActive || lv_screen_active() != owner) {
                    fail("Pocket Mode did not preserve continuous owner"); return;
                }
                exitPocketMode();
#endif
                pocketChecked = true;
            }
            if (!autoHomeChecked && millis() - stageStartedMs >= 15000) {
#if AUTO_RETURN_HOME_TIMEOUT_MS > 0
                lv_obj_t *owner = scanSession.screen;
                lastActivityMs = millis() - AUTO_RETURN_HOME_TIMEOUT_MS - 1U;
                updateAutoReturnHome();
                if (lv_screen_active() != owner || scanSession.screen != owner) {
                    fail("auto-home retired continuous owner"); return;
                }
                resetInactivityTimer();
#endif
                autoHomeChecked = true;
            }
            if (millis() - lastMemoryLogMs >= 5000) {
                lastMemoryLogMs = millis(); logMemory("BLE soak");
            }
            if (!reached(actionAtMs)) return;
            click(scanSession.start);
            setStage(Stage::BleSoakWaitIdle);
            return;

        case Stage::BleSoakWaitIdle:
            if (waitTimedOut(10000, "BLE soak did not stop")) return;
            if (scanSession.state != ScanUiState::Idle) return;
            click(scanSession.back);
            setStage(Stage::BleSoakWaitBack);
            return;

        case Stage::BleSoakWaitBack:
            if (waitTimedOut(5000, "BLE soak Back did not finish")) return;
            if (scanSessionAttached() || lv_display_get_screen_prev(lvDisp) ||
                lv_screen_active() != bleMenuScreen) return;
            cb_bleMenuBack(nullptr);
            setStage(Stage::HybridMenuWait, 350);
            return;

        case Stage::HybridMenuWait:
            if (!reached(actionAtMs) || lv_display_get_screen_prev(lvDisp)) return;
            createWiFiMenu();
            actionAtMs = millis() + 350;
            setStage(Stage::HybridSoakWaitPage, 350);
            return;

        case Stage::HybridSoakWaitPage:
            if (!reached(actionAtMs) || lv_display_get_screen_prev(lvDisp)) return;
            createFlockHybridScanner();
            setStage(Stage::HybridSoakWaitRunning, 350);
            return;

        case Stage::HybridSoakWaitRunning:
            if (waitTimedOut(10000, "Hybrid soak did not start")) return;
            if (!reached(actionAtMs) || lv_display_get_screen_prev(lvDisp) ||
                lv_screen_active() != scanSession.screen) return;
            if (scanSession.state == ScanUiState::Idle) {
                configureMode(true, scanSession.durationMs);
                if (stage == Stage::Failed) return;
                click(scanSession.start);
                return;
            }
            if (scanSession.state != ScanUiState::Running) return;
            lastMemoryLogMs = millis();
            setStage(Stage::HybridSoak, 90000);
            return;

        case Stage::HybridSoak:
            if (scanSession.state != ScanUiState::Running) {
                fail("Hybrid soak stopped unexpectedly"); return;
            }
            if (millis() - lastMemoryLogMs >= 5000) {
                lastMemoryLogMs = millis(); logMemory("Hybrid soak");
            }
            if (!reached(actionAtMs)) return;
            Serial.printf("[ScanDiag] Hybrid active Back phase=%s\n", scanSession.phase);
            click(scanSession.back);
            setStage(Stage::HybridSoakWaitBack);
            return;

        case Stage::HybridSoakWaitBack:
            if (waitTimedOut(15000, "Hybrid soak active Back did not drain")) return;
            if (scanSessionAttached() || lv_display_get_screen_prev(lvDisp) ||
                lv_screen_active() != wifiMenuScreen) return;
            logMemory("final");
            Serial.printf("[ScanDiag] PASS all pages; minima heap=%lu psram=%lu lvgl=%lu\n",
                          (unsigned long)minHeap, (unsigned long)minPsram,
                          (unsigned long)minLvglFree);
            stage = Stage::Done;
            return;

        default: return;
    }
}

}  // namespace scan_session_device_test

static void scanSessionDeviceTestBegin() { scan_session_device_test::begin(); }
static void scanSessionDeviceTestProcess() { scan_session_device_test::process(); }

#endif  // ROGUE_RADAR_SCAN_SESSION_DEVICE_TEST
