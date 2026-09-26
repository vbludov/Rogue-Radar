#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <lvgl.h>
#define PERSISTENT_SETTINGS_ENABLED 0
#define SCREEN_W 320
#define ENCODER_BTN 0
#define LOW 0
#define TC(x) lv_color_hex(0x55aaff)
static uint32_t nowMs;
static uint32_t millis() { return nowMs; }
static int keyLevel = 1;
static int digitalRead(int) { return keyLevel; }
static bool soundEnabled = true, lightAlertEnabled = false, soundReady = false;
static bool signalTrackerActive = false;
static lv_display_t *lvDisp;
static void resetInactivityTimer() {}
static void stopSoundDriverAfterChirp() {}
static void savePersistentAlertSoundSetting() {}
static void savePersistentLightAlertSetting() {}
struct Lights { bool setAlertEnabled(bool value) { return value; } } ledStrip;
static lv_obj_t *createActionBtn(lv_obj_t *parent, const char *text, lv_event_cb_t cb) {
    auto *button = lv_button_create(parent);
    lv_obj_set_size(button, 110, 26);
    auto *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, nullptr);
    return button;
}
#include "../rogue-radar/scan_session_ui.h"
static lv_obj_t *trackerIconSlash(lv_obj_t *button, bool) { return lv_label_create(button); }

static int starts, polls, stopCalls, legacyCalls, exits;
static int drainSteps;
static bool resumeObserved, startSucceeds = true;
static lv_obj_t *home, *owner, *detail;
static lv_group_t *group;
static bool backendStart() { ++starts; resumeObserved = scanSessionIsResume(); return startSucceeds; }
static void backendPoll() { ++polls; }
static bool backendStop() { ++stopCalls; return drainSteps-- <= 0; }
static int backendCount() { return 3; }
static void oldStart(lv_event_t *) { ++legacyCalls; }
static void returnBack(lv_event_t *) {
    if (scanDeferBack(returnBack)) return;
    ++exits;
    lv_screen_load(home);
    lv_obj_delete(owner); owner = nullptr;
    lv_group_delete(group); group = nullptr;
}
static void openDetail(int index) {
    assert(index == 7);
    detail = lv_obj_create(nullptr);
    lv_screen_load(detail);
}
static void setupPage(bool continuous) {
    owner = lv_obj_create(nullptr);
    auto *back = createActionBtn(owner, "Back", returnBack);
    auto *start = createActionBtn(owner, "Scan", oldStart);
    auto *status = lv_label_create(owner);
    group = lv_group_create();
    lv_group_add_obj(group, back); lv_group_add_obj(group, start);
    attachScanSession(owner, &group, back, start, status, "test", 1000,
                      ScanOps{backendStart, backendPoll, backendStop, backendCount}, continuous);
    lv_screen_load(owner);
    lv_obj_update_layout(owner);
}
static void tick(uint32_t n) { nowMs += n; lv_tick_inc(n); processScanSession(); }
static void clickStart() { lv_obj_send_event(scanSession.start, LV_EVENT_CLICKED, nullptr); }
int main() {
    lv_init();
    lvDisp = lv_display_create(320,170);
    lv_timer_pause(lv_display_get_refr_timer(lvDisp));
    home = lv_screen_active();
    setupPage(false);
    assert(lv_group_get_obj_count(group) == 5);
    clickStart(); assert(legacyCalls == 0 && starts == 0);
    tick(0); assert(starts == 1 && scanSession.state == ScanUiState::Running);
    tick(999); assert(scanSession.state == ScanUiState::Running);
    tick(1); assert(scanSession.state == ScanUiState::Stopping);
    tick(0); assert(scanSession.state == ScanUiState::Idle);
    returnBack(nullptr); assert(exits == 1);

    setupPage(false); clickStart(); tick(0); tick(400);
    scanRequestNavigation(openDetail, 7); tick(0);
    tick(5000); assert(scanSession.clock.remaining(nowMs) == 600);
    lv_screen_load(owner); lv_obj_delete(detail); detail = nullptr;
    tick(0); tick(599); assert(scanSession.state == ScanUiState::Running);
    tick(1); tick(0); assert(scanSession.state == ScanUiState::Idle);
    returnBack(nullptr); assert(exits == 2);

    setupPage(true); clickStart(); tick(0);
    assert(scanSessionContinuous() && scanShouldAlert("aa") && !scanShouldAlert("aa"));
    tick(120000); assert(scanSession.state == ScanUiState::Running);
    assert(scanShouldAlert("before-detail"));
    drainSteps = 2;
    scanRequestNavigation(openDetail, 7);
    tick(0); assert(lv_screen_active() == owner);
    tick(0); assert(lv_screen_active() == owner);
    keyLevel = LOW; tick(0); assert(lv_screen_active() == owner);
    keyLevel = 1; tick(0); assert(lv_screen_active() == detail);
    assert(scanSession.state == ScanUiState::Suspended);
    tick(40000); assert(scanSession.state == ScanUiState::Suspended);
    lv_screen_load(owner); lv_obj_delete(detail); detail = nullptr;
    tick(0); assert(resumeObserved && scanSession.state == ScanUiState::Running);
    assert(scanSession.clock.elapsed(nowMs) == 120000);
    assert(!scanShouldAlert("before-detail"));

    // Rebuilt result groups must regain controls without duplicates.
    lv_group_delete(group); group = lv_group_create();
    scanRestoreControls(); scanRestoreControls();
    assert(lv_group_get_obj_count(group) == 3);
    drainSteps = 1; returnBack(nullptr);
    tick(0); assert(owner != nullptr && exits == 2);
    tick(0); assert(owner == nullptr && exits == 3);

    // A failed backend start must drain, then permit retry/Back.
    setupPage(false); startSucceeds = false;
    clickStart(); tick(0); assert(scanSession.state == ScanUiState::Stopping);
    tick(0); assert(scanSession.state == ScanUiState::Idle && scanSession.error[0]);
    startSucceeds = true; clickStart(); tick(0);
    assert(scanSession.state == ScanUiState::Running && !scanSession.error[0]);
    clickStart(); tick(0); assert(scanSession.state == ScanUiState::Idle);
    returnBack(nullptr);

    // Unexpected owner deletion retains backend cleanup without dereferencing UI.
    setupPage(true); clickStart(); tick(0); drainSteps = 1;
    lv_screen_load(home); lv_obj_delete(owner); owner = nullptr;
    lv_group_delete(group); group = nullptr;
    tick(0); assert(scanSession.state == ScanUiState::Stopping);
    tick(0); assert(scanSession.state == ScanUiState::Idle);
    (void)scanSessionPhase; (void)scanSessionAttached;
    std::puts("PASS: scan UI timing, deferred navigation, resume, Back, errors and deletion");
}
