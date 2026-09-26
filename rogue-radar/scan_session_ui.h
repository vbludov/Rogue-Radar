#pragma once
#include "scan_session_model.h"

// Included after shared UI helpers; all backend/UI work runs from loop().
struct ScanOps {
    bool (*start)();
    void (*poll)();
    bool (*stop)();  // false retains radio ownership until pending callbacks drain
    int (*count)();
};
static lv_obj_t *trackerIconSlash(lv_obj_t *button, bool bulb);

enum class ScanUiState { Idle, Starting, Running, Stopping, Suspended };
struct ScanSessionUi {
    lv_obj_t *screen = nullptr;
    lv_group_t **group = nullptr;
    lv_obj_t *back = nullptr, *start = nullptr, *status = nullptr;
    lv_obj_t *mode = nullptr, *audio = nullptr, *light = nullptr;
    lv_obj_t *audioSlash = nullptr, *lightSlash = nullptr;
    ScanOps ops{};
    ScanUiState state = ScanUiState::Idle;
    rogue_radar::ScanSessionModel clock;
    bool continuous = false, resuming = false, resumeOnReturn = false;
    void (*navigate)(int) = nullptr;
    int navIndex = 0;
    lv_event_cb_t exit = nullptr;
    uint32_t durationMs = 0, lastPaint = 0;
    char key[16] = {}, phase[20] = {}, error[64] = {};
};
static ScanSessionUi scanSession;
static rogue_radar::ScanAlertCache<128> scanAlerts;

static bool scanSessionIsResume() { return scanSession.resuming; }
static bool scanSessionAttached() {
    return scanSession.screen != nullptr || scanSession.state != ScanUiState::Idle;
}
static bool scanSessionContinuous() { return scanSession.continuous; }
static void scanSessionPhase(const char *phase) {
    snprintf(scanSession.phase, sizeof(scanSession.phase), "%s", phase ? phase : "");
}
static bool scanShouldAlert(const char *key) {
    uint64_t hash = 14695981039346656037ULL;
    for (const char *p = key; p && *p; ++p) {
        hash ^= static_cast<uint8_t>(*p);
        hash *= 1099511628211ULL;
    }
    // Time spent in a detail/tracker page is not evidence of radio absence.
    return scanAlerts.observe(hash, scanSession.clock.elapsed(millis()));
}
static void scanSessionError(const char *reason) {
    snprintf(scanSession.error, sizeof(scanSession.error), "%s", reason);
    scanSession.resumeOnReturn = false;
    scanSession.state = ScanUiState::Stopping;
}
static void scanRestoreControls() {
    auto &s = scanSession;
    if (!s.screen || !s.group || !*s.group) return;
    for (lv_obj_t *obj : {s.mode, s.audio, s.light}) {
        if (obj && lv_obj_get_group(obj) != *s.group) lv_group_add_obj(*s.group, obj);
    }
}
static void scanPaintControls() {
    auto &s = scanSession;
    if (!s.screen) return;
    const bool busy = s.state == ScanUiState::Starting || s.state == ScanUiState::Running ||
                      s.state == ScanUiState::Stopping;
    lv_label_set_text(lv_obj_get_child(s.start, 0),
        s.state == ScanUiState::Stopping ? "Wait..." : busy ? "Stop" : "Start");
    if (busy) lv_obj_add_state(s.mode, LV_STATE_DISABLED);
    else lv_obj_remove_state(s.mode, LV_STATE_DISABLED);
    if (s.continuous) lv_label_set_text(lv_obj_get_child(s.mode, 0), "Continuous");
    else lv_label_set_text_fmt(lv_obj_get_child(s.mode, 0), "Timed %lus", (unsigned long)(s.durationMs / 1000));
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *button = i ? s.light : s.audio;
        lv_obj_t *slash = i ? s.lightSlash : s.audioSlash;
        const bool enabled = i ? lightAlertEnabled : soundEnabled;
        if (enabled) {
            lv_obj_add_flag(slash, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_state(button, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_flag(slash, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_state(button, LV_STATE_CHECKED);
        }
    }
}
static void cb_scanStartStop(lv_event_t *) {
    auto &s = scanSession;
    resetInactivityTimer();
    if (s.state == ScanUiState::Stopping) return;
    if (s.state == ScanUiState::Running || s.state == ScanUiState::Starting) {
        s.resumeOnReturn = false;
        s.state = ScanUiState::Stopping;
    } else {
        s.error[0] = s.phase[0] = '\0';
        s.resuming = false;
        s.resumeOnReturn = false;
        s.state = ScanUiState::Starting;
        scanAlerts.reset();
    }
    scanPaintControls();
}
static void cb_scanMode(lv_event_t *) {
    auto &s = scanSession;
    if (s.state != ScanUiState::Idle) return;
    s.continuous = !s.continuous;
#if PERSISTENT_SETTINGS_ENABLED
    settingsPrefs.begin(PREFS_NAMESPACE, false);
    settingsPrefs.putBool(s.key, s.continuous);
    settingsPrefs.end();
#endif
    scanPaintControls();
    resetInactivityTimer();
}
static void cb_scanAudio(lv_event_t *) {
    soundEnabled = !soundEnabled;
    if (!soundEnabled && soundReady) stopSoundDriverAfterChirp();
    savePersistentAlertSoundSetting();
    scanPaintControls();
    resetInactivityTimer();
}
static void cb_scanLight(lv_event_t *) {
    lightAlertEnabled = ledStrip.setAlertEnabled(!lightAlertEnabled);
    savePersistentLightAlertSetting();
    scanPaintControls();
    resetInactivityTimer();
}
static void attachScanSession(lv_obj_t *screen, lv_group_t **group,
                              lv_obj_t *back, lv_obj_t *startButton,
                              lv_obj_t *statusLabel, const char *prefsKey,
                              uint32_t durationMs, ScanOps ops,
                              bool defaultContinuous = false) {
    // Callers reach a new tool only through the deferred Back gate. Never
    // overwrite an outstanding backend if an external caller violates that.
    if (scanSession.state != ScanUiState::Idle) return;
    scanSession = ScanSessionUi{};
    auto &s = scanSession;
    s.screen = screen; s.group = group; s.back = back;
    s.start = startButton; s.status = statusLabel; s.ops = ops;
    s.durationMs = durationMs; s.continuous = defaultContinuous;
    snprintf(s.key, sizeof(s.key), "%s", prefsKey);
#if PERSISTENT_SETTINGS_ENABLED
    settingsPrefs.begin(PREFS_NAMESPACE, true);
    s.continuous = settingsPrefs.getBool(s.key, defaultContinuous);
    settingsPrefs.end();
#endif
    // Replace the old Start callback without retaining a blocking scanner path.
    for (int32_t i = (int32_t)lv_obj_get_event_count(startButton) - 1; i >= 0; --i) {
        auto *d = lv_obj_get_event_dsc(startButton, i);
        if (d->filter == LV_EVENT_CLICKED) lv_obj_remove_event(startButton, i);
    }
    lv_obj_add_event_cb(startButton, cb_scanStartStop, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_width(back, 50);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    lv_obj_set_style_pad_all(back, 0, 0);
    lv_label_set_text(lv_obj_get_child(back, 0), "< Back");
    lv_obj_set_width(startButton, 70);
    lv_obj_align(startButton, LV_ALIGN_BOTTOM_LEFT, 160, -4);
    lv_obj_set_style_pad_all(startButton, 0, 0);
    s.mode = createActionBtn(screen, "", cb_scanMode);
    lv_obj_set_width(s.mode, 98); lv_obj_align(s.mode, LV_ALIGN_BOTTOM_LEFT, 58, -4);
    lv_obj_set_style_pad_all(s.mode, 0, 0);
    lv_obj_set_style_text_font(s.mode, &lv_font_montserrat_12, 0);
    s.audio = createActionBtn(screen, "", cb_scanAudio);
    lv_obj_set_width(s.audio, 36); lv_obj_align(s.audio, LV_ALIGN_BOTTOM_LEFT, 234, -4);
    s.audioSlash = trackerIconSlash(s.audio, false);
    s.light = createActionBtn(screen, "", cb_scanLight);
    lv_obj_set_width(s.light, 36); lv_obj_align(s.light, LV_ALIGN_BOTTOM_LEFT, 274, -4);
    s.lightSlash = trackerIconSlash(s.light, true);
    lv_obj_add_event_cb(screen, [](lv_event_t *e) {
        if (lv_event_get_target(e) != scanSession.screen) return;
        // Normal exits drain first. Retain a detached backend if some external
        // deletion happens unexpectedly, so loop can still finish its cleanup.
        scanSession.screen = nullptr;
        scanSession.navigate = nullptr; scanSession.exit = nullptr;
        scanSession.resumeOnReturn = false;
        if (scanSession.state != ScanUiState::Idle)
            scanSession.state = ScanUiState::Stopping;
    }, LV_EVENT_DELETE, nullptr);
    scanRestoreControls();
    scanPaintControls();
    if (statusLabel) {
        lv_obj_set_height(statusLabel, 18);
        lv_obj_set_width(statusLabel, SCREEN_W - 16);
        lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_DOT);
        lv_label_set_text(statusLabel, "Select mode, then Start");
    }
}

// Navigation is deferred until a backend relinquishes its radio. The result
// arrays remain stable while the selected index is passed to the detail page.
static void scanRequestNavigation(void (*fn)(int), int index) {
    auto &s = scanSession;
    if (!s.screen) { fn(index); return; }
    if (s.navigate || s.exit) return;
    s.navigate = fn; s.navIndex = index;
    s.resumeOnReturn = s.state == ScanUiState::Running;
    if (s.resumeOnReturn) s.clock.suspend(millis());
    s.state = ScanUiState::Stopping;
}
static bool scanDeferBack(lv_event_cb_t callback) {
    auto &s = scanSession;
    if (!s.screen || lv_screen_active() != s.screen) return false;
    if (s.state == ScanUiState::Idle) {
        s.screen = nullptr;
        return false;
    }
    s.resumeOnReturn = false;
    s.navigate = nullptr;
    s.exit = callback;
    s.state = ScanUiState::Stopping;
    return true;
}
static void processScanSession() {
    auto &s = scanSession;
    if (s.state == ScanUiState::Stopping) {
        if (s.screen && s.status)
            lv_label_set_text(s.status, s.error[0] ? s.error : "Stopping scan...");
        if (s.ops.stop && !s.ops.stop()) { scanPaintControls(); return; }
        if (s.navigate || s.exit) {
            if (lv_display_get_screen_prev(lvDisp) || digitalRead(ENCODER_BTN) == LOW) return;
        }
        if (s.exit) {
            auto callback = s.exit;
            s.exit = nullptr; s.screen = nullptr; s.state = ScanUiState::Idle;
            s.clock.stop(); resetInactivityTimer();
            callback(nullptr);
            return;
        }
        s.state = s.resumeOnReturn ? ScanUiState::Suspended : ScanUiState::Idle;
        if (!s.resumeOnReturn) s.clock.stop();
        scanPaintControls();
        if (s.screen && s.status) {
            if (s.error[0]) lv_label_set_text(s.status, s.error);
            else lv_label_set_text_fmt(s.status, "Stopped - %d found", s.ops.count ? s.ops.count() : 0);
        }
        resetInactivityTimer();
        if (s.navigate) {
            auto fn = s.navigate; const int idx = s.navIndex;
            s.navigate = nullptr;
            fn(idx);
            return;
        }
    }
    if (!s.screen || signalTrackerActive) return;
    if (s.state == ScanUiState::Suspended && lv_screen_active() == s.screen &&
        !lv_display_get_screen_prev(lvDisp)) {
        s.resuming = true;
        s.state = ScanUiState::Starting;
    }
    if (s.state == ScanUiState::Starting) {
        if (lv_display_get_screen_prev(lvDisp)) return;
        if (!s.resuming) {
            s.clock.configure(s.durationMs, s.continuous);
            s.clock.start(millis());
        } else s.clock.resume(millis());
        s.state = ScanUiState::Running;
        if (!s.ops.start || !s.ops.start()) scanSessionError("Scan start failed; retry Start");
        s.resuming = false;
        scanPaintControls();
    }
    if (s.state != ScanUiState::Running) return;
    if (!s.continuous && s.clock.expired(millis())) {
        s.resumeOnReturn = false;
        s.state = ScanUiState::Stopping;
        return;
    }
    if (s.ops.poll) s.ops.poll();
    if (s.state != ScanUiState::Running) return;
    const uint32_t now = millis();
    if (s.status && now - s.lastPaint >= 500) {
        s.lastPaint = now;
        const uint32_t seconds = s.clock.elapsed(now) / 1000;
        lv_label_set_text_fmt(s.status, "%s %02lu:%02lu - %d %s",
            s.continuous ? "Continuous" : "Timed", (unsigned long)(seconds / 60),
            (unsigned long)(seconds % 60), s.ops.count ? s.ops.count() : 0, s.phase);
    }
}
