#pragma once

// Included after the sketch's shared UI/radio helpers.
#include "signal_tracker_model.h"
#include "signal_tracker_radio.h"

static SignalTrackerRadio trackerRadio;
static rogue_radar::SignalTrackerModel trackerModel;
static lv_obj_t *trackerScreen = nullptr;
static lv_group_t *trackerGroup = nullptr;
static lv_obj_t *trackerReturnScreen = nullptr;
static lv_group_t *trackerReturnGroup = nullptr;
static lv_obj_t *trackerRssiLabel = nullptr;
static lv_obj_t *trackerStatusLabel = nullptr;
static lv_obj_t *trackerChart = nullptr;
static lv_chart_series_t *trackerRawSeries = nullptr;
static lv_chart_series_t *trackerSmoothSeries = nullptr;
static lv_obj_t *trackerAudioLabel = nullptr;
static lv_obj_t *trackerVolumeLabel = nullptr;
static lv_obj_t *trackerLightLabel = nullptr;
static lv_obj_t *trackerAudioButton = nullptr;
static lv_obj_t *trackerLightButton = nullptr;
static bool trackerIsBle = false;
static bool trackerAudio = false;
static bool trackerLight = true;
static bool trackerHasSample = false;
static bool trackerExitPending = false;
static uint8_t trackerVolume = 20;
static uint32_t trackerLastPaint = 0;
static uint32_t trackerLastChart = 0;
static uint32_t trackerLastBeep = 0;

static void saveTrackerAudio() {
#if PERSISTENT_SETTINGS_ENABLED
    settingsPrefs.begin(PREFS_NAMESPACE, false);
    settingsPrefs.putBool("trackAudio", trackerAudio);
    settingsPrefs.putBool("trackLight", trackerLight);
    settingsPrefs.putUChar("trackVol", trackerVolume);
    settingsPrefs.end();
#endif
}

static void updateTrackerAudioLabels() {
    // These objects are the diagonal slash overlays on the two icon buttons.
    if (trackerAudio) {
        lv_obj_add_flag(trackerAudioLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(trackerAudioButton, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_flag(trackerAudioLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(trackerAudioButton, LV_STATE_CHECKED);
    }
    if (trackerLight) {
        lv_obj_add_flag(trackerLightLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(trackerLightButton, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_flag(trackerLightLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(trackerLightButton, LV_STATE_CHECKED);
    }
    lv_label_set_text_fmt(trackerVolumeLabel, "Vol:%u%%", trackerVolume);
}

static void stopSignalTracker() {
    signalTrackerActive = false;
    trackerRadio.stop();
    deleteGroup(&trackerGroup);
    trackerRssiLabel = trackerStatusLabel = trackerChart = nullptr;
    trackerRawSeries = trackerSmoothSeries = nullptr;
    trackerAudioLabel = trackerVolumeLabel = nullptr;
    trackerLightLabel = nullptr;
    trackerAudioButton = trackerLightButton = nullptr;
    setAllLEDs(MENU_COLORS[trackerIsBle ? 1 : 0].r,
               MENU_COLORS[trackerIsBle ? 1 : 0].g,
               MENU_COLORS[trackerIsBle ? 1 : 0].b, 3);
}

static void finishTrackerBack() {
    stopSignalTracker();
    setGroup(trackerReturnGroup);
    trackerScreen = nullptr;  // LVGL's outgoing animation owns deletion.
    lv_screen_load_anim(trackerReturnScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, true);
    trackerReturnScreen = nullptr;
    trackerReturnGroup = nullptr;
    trackerExitPending = false;
}

static void cb_trackerBack(lv_event_t *) {
    if (trackerExitPending || lv_display_get_screen_prev(lvDisp)) return;
    trackerExitPending = true;
    trackerRadio.stop();
    lv_label_set_text(trackerStatusLabel, "Stopping scan...");
    lv_obj_clear_flag(trackerStatusLabel, LV_OBJ_FLAG_HIDDEN);
    // Do not expose the legacy scanner until its canceled predecessor's
    // asynchronous completion has been consumed. The display stays responsive.
    if (trackerRadio.readyToRelease()) finishTrackerBack();
}

static void processSignalTracker() {
    if (!signalTrackerActive) return;
    if (trackerExitPending) {
        if (trackerRadio.readyToRelease()) finishTrackerBack();
        else lv_label_set_text(trackerStatusLabel, trackerRadio.status());
        return;
    }
    trackerRadio.poll();
    const uint32_t now = millis();
    // Consume the latest radio mailbox at a consistent cadence for smoothing.
    if (now - trackerLastPaint < 100) return;
    trackerLastPaint = now;
    int sample;
    if (trackerRadio.takeSample(sample)) {
        trackerModel.addSample(sample, now);
        trackerHasSample = true;
    }
    trackerModel.advance(now);

    int16_t raw = 0;
    float smooth = 0;
    const bool fresh = trackerModel.current(now, raw, smooth);
    const uint8_t strength = trackerModel.strengthPercent(now);
    if (fresh) {
        lv_label_set_text_fmt(trackerRssiLabel, "%d dBm", raw);
        lv_color_t readingColor = TC(text);
        switch (trackerModel.trend(now)) {
            case rogue_radar::SignalTrend::Stronger: readingColor = TC(success); break;
            case rogue_radar::SignalTrend::Weaker: readingColor = TC(warn); break;
            default: break;
        }
        lv_obj_set_style_text_color(trackerRssiLabel, readingColor, 0);
        lv_obj_add_flag(trackerStatusLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(trackerRssiLabel, "-- dBm");
        lv_obj_set_style_text_color(trackerRssiLabel, TC(textDim), 0);
        const char *status = trackerRadio.status();
        if (strncmp(status, "Tracking", 8) == 0 || strncmp(status, "Sweeping", 8) == 0)
            status = "Waiting for signal";
        lv_label_set_text(trackerStatusLabel, trackerHasSample ? "Signal lost / searching" : status);
        lv_obj_clear_flag(trackerStatusLabel, LV_OBJ_FLAG_HIDDEN);
    }

    rgb_color frame[NUM_LEDS] = {};
    if (fresh && trackerLight) {
        // Blue -> yellow -> green. At least one LED represents a fresh weak hit.
        const uint8_t count = 1 + (strength * (NUM_LEDS - 1) / 100);
        rgb_color color;
        if (strength < 50) {
            const uint8_t v = strength * 255 / 50;
            color = {v, v, (uint8_t)(255 - v)};
        } else {
            color = {(uint8_t)(255 - (strength - 50) * 255 / 50), 255, 0};
        }
        for (uint8_t i = 0; i < count; ++i) frame[i] = color;
    }
    ledStrip.write(frame, NUM_LEDS, activeLedBrightness(LED_BRIGHTNESS));

    if (now - trackerLastChart >= 500) {
        trackerLastChart = now;
        for (uint32_t i = 0; i < rogue_radar::SignalTrackerModel::kBucketCount; ++i) {
            const auto b = trackerModel.bucket(i);
            lv_chart_set_value_by_id(trackerChart, trackerRawSeries, i,
                b.raw == rogue_radar::SignalTrackerModel::kAbsent ? LV_CHART_POINT_NONE : b.raw);
            lv_chart_set_value_by_id(trackerChart, trackerSmoothSeries, i,
                b.smoothed == rogue_radar::SignalTrackerModel::kAbsent ? LV_CHART_POINT_NONE : b.smoothed);
        }
        lv_chart_refresh(trackerChart);
    }
    // Short guidance chirps use their own preference, separate from alerts/menu ticks.
    const uint32_t interval = 1600 - strength * 14;
    if (fresh && trackerAudio && now - trackerLastBeep >= interval) {
        trackerLastBeep = now;
        soundTone(1000 + strength * 10, 18, trackerVolume, false);
        stopSoundDriverAfterChirp();
    }
}

static lv_obj_t *trackerIconSlash(lv_obj_t *button, bool bulb) {
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_border_color(button, TC(accent), LV_STATE_CHECKED);
    lv_obj_set_style_border_width(button, 2, LV_STATE_CHECKED);
    if (!bulb) {
        lv_obj_t *icon = lv_label_create(button);
        lv_label_set_text(icon, LV_SYMBOL_VOLUME_MAX);
        lv_obj_set_style_text_color(icon, TC(text), 0);
        lv_obj_center(icon);
    } else {
        // Small vector bulb: outlined globe, neck, and base.
        lv_obj_t *globe = lv_obj_create(button);
        lv_obj_set_size(globe, 12, 12);
        lv_obj_set_pos(globe, 9, 2);
        lv_obj_set_style_radius(globe, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(globe, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(globe, 2, 0);
        lv_obj_set_style_border_color(globe, TC(text), 0);
        lv_obj_clear_flag(globe, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *neck = lv_obj_create(button);
        lv_obj_set_size(neck, 6, 6);
        lv_obj_set_pos(neck, 12, 12);
        lv_obj_set_style_pad_all(neck, 0, 0);
        lv_obj_set_style_radius(neck, 0, 0);
        lv_obj_set_style_bg_color(neck, TC(text), 0);
        lv_obj_set_style_border_width(neck, 0, 0);
        lv_obj_clear_flag(neck, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *base = lv_obj_create(button);
        lv_obj_set_size(base, 8, 2);
        lv_obj_set_pos(base, 11, 20);
        lv_obj_set_style_bg_color(base, TC(text), 0);
        lv_obj_set_style_border_width(base, 0, 0);
        lv_obj_clear_flag(base, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
    static const lv_point_precise_t slashPoints[] = {{5, 3}, {25, 23}};
    lv_obj_t *slash = lv_line_create(button);
    lv_line_set_points(slash, slashPoints, 2);
    lv_obj_set_style_line_width(slash, 2, 0);
    lv_obj_set_style_line_color(slash, TC(alert), 0);
    lv_obj_clear_flag(slash, LV_OBJ_FLAG_CLICKABLE);
    return slash;
}

static void createSignalTracker(bool isBle, const char *name, const char *mac, uint8_t channel) {
    if (signalTrackerActive || lv_display_get_screen_prev(lvDisp)) return;
    trackerIsBle = isBle;
    trackerReturnScreen = lv_screen_active();
    trackerReturnGroup = lv_indev_get_group(lvIndev);
    trackerModel.reset(millis());
    trackerHasSample = false;
    trackerExitPending = false;
    trackerLastPaint = trackerLastChart = trackerLastBeep = millis();
#if PERSISTENT_SETTINGS_ENABLED
    settingsPrefs.begin(PREFS_NAMESPACE, true);
    trackerAudio = settingsPrefs.getBool("trackAudio", false);
    trackerLight = settingsPrefs.getBool("trackLight", true);
    trackerVolume = settingsPrefs.getUChar("trackVol", 20);
    settingsPrefs.end();
    if (trackerVolume < 10 || trackerVolume > 50) trackerVolume = 20;
#endif
    if (isBle && !bleInitialized) {
        BLEDevice::init("");
        bleInitialized = true;
    }
    if (isBle) trackerRadio.beginBle(mac);
    else trackerRadio.beginWifi(mac, channel);

    trackerScreen = lv_obj_create(nullptr);
    applyScreenStyle(trackerScreen);
    char heading[48];
    snprintf(heading, sizeof(heading), "%s: %.22s", isBle ? "BLE" : "WiFi",
             name && name[0] ? name : "Unnamed target");
    lv_obj_t *header = createHeader(trackerScreen, heading);
    lv_obj_t *title = lv_obj_get_child(header, 0);
    lv_obj_set_width(title, SCREEN_W - (BATTERY_METER_ENABLED ? 132 : 64));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_t *identity = lv_label_create(trackerScreen);
    lv_label_set_text_fmt(identity, "%s  |  30s history", mac);
    lv_obj_set_pos(identity, 6, 28);
    lv_obj_set_width(identity, 212);
    lv_label_set_long_mode(identity, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(identity, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(identity, TC(textDim), 0);
    trackerRssiLabel = lv_label_create(trackerScreen);
    lv_obj_set_pos(trackerRssiLabel, 222, 27);
    lv_obj_set_width(trackerRssiLabel, SCREEN_W - 228);
    lv_obj_set_style_text_align(trackerRssiLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(trackerRssiLabel, TC(text), 0);
    lv_label_set_text(trackerRssiLabel, "-- dBm");
    trackerChart = lv_chart_create(trackerScreen);
    lv_obj_set_pos(trackerChart, 36, 48);
    lv_obj_set_size(trackerChart, SCREEN_W - 43, 86);
    lv_obj_clear_flag(trackerChart, LV_OBJ_FLAG_SCROLLABLE);
    lv_chart_set_type(trackerChart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(trackerChart, rogue_radar::SignalTrackerModel::kBucketCount);
    lv_chart_set_range(trackerChart, LV_CHART_AXIS_PRIMARY_Y, -100, -30);
    lv_chart_set_div_line_count(trackerChart, 3, 5);
    lv_obj_set_style_bg_color(trackerChart, TC(card), 0);
    lv_obj_set_style_border_width(trackerChart, 0, 0);
    lv_obj_set_style_pad_all(trackerChart, 0, 0);
    lv_obj_set_style_line_color(trackerChart, TC(border), LV_PART_MAIN);
    lv_obj_set_style_size(trackerChart, 0, 0, LV_PART_INDICATOR);
    trackerRawSeries = lv_chart_add_series(trackerChart, TC(textDim), LV_CHART_AXIS_PRIMARY_Y);
    trackerSmoothSeries = lv_chart_add_series(trackerChart, TC(accent), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(trackerChart, trackerRawSeries, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(trackerChart, trackerSmoothSeries, LV_CHART_POINT_NONE);
    // Status uses the chart only while waiting/lost/stopping, never a dedicated row.
    trackerStatusLabel = lv_label_create(trackerChart);
    lv_obj_set_width(trackerStatusLabel, SCREEN_W - 55);
    lv_label_set_long_mode(trackerStatusLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(trackerStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(trackerStatusLabel, TC(textDim), 0);
    lv_label_set_text(trackerStatusLabel, "Waiting for signal");
    lv_obj_center(trackerStatusLabel);
    lv_obj_t *high = lv_label_create(trackerScreen);
    lv_label_set_text(high, "-30"); lv_obj_set_pos(high, 3, 47);
    lv_obj_set_style_text_color(high, TC(textDim), 0);
    lv_obj_t *low = lv_label_create(trackerScreen);
    lv_label_set_text(low, "-100"); lv_obj_set_pos(low, 0, 119);
    lv_obj_set_style_text_color(low, TC(textDim), 0);

    trackerGroup = lv_group_create();
    lv_obj_t *back = createBackBtn(trackerScreen, cb_trackerBack);
    lv_obj_set_width(back, 64);
    lv_obj_set_style_pad_all(back, 2, 0);
    lv_group_add_obj(trackerGroup, back);
    lv_obj_t *audio = createActionBtn(trackerScreen, "", [](lv_event_t *) {
        trackerAudio = !trackerAudio;
        saveTrackerAudio(); updateTrackerAudioLabels();
    });
    lv_obj_set_width(audio, 32);
    lv_obj_align(audio, LV_ALIGN_BOTTOM_LEFT, 158, -4);
    trackerAudioButton = audio;
    trackerAudioLabel = trackerIconSlash(audio, false);
    lv_group_add_obj(trackerGroup, audio);
    lv_obj_t *light = createActionBtn(trackerScreen, "", [](lv_event_t *) {
        trackerLight = !trackerLight;
        saveTrackerAudio(); updateTrackerAudioLabels();
    });
    lv_obj_set_width(light, 32);
    lv_obj_align(light, LV_ALIGN_BOTTOM_LEFT, 196, -4);
    trackerLightButton = light;
    trackerLightLabel = trackerIconSlash(light, true);
    lv_group_add_obj(trackerGroup, light);
    lv_obj_t *volume = createActionBtn(trackerScreen, "", [](lv_event_t *) {
        trackerVolume = trackerVolume >= 50 ? 10 : trackerVolume + 10;
        saveTrackerAudio(); updateTrackerAudioLabels();
    });
    lv_obj_set_width(volume, 72);
    lv_obj_set_style_pad_all(volume, 2, 0);
    trackerVolumeLabel = lv_obj_get_child(volume, 0);
    lv_group_add_obj(trackerGroup, volume);
    lv_obj_t *rawLegend = lv_label_create(trackerScreen);
    lv_label_set_text(rawLegend, "Raw"); lv_obj_set_pos(rawLegend, 77, SCREEN_H - 24);
    lv_obj_set_style_text_color(rawLegend, TC(textDim), 0);
    lv_obj_t *avgLegend = lv_label_create(trackerScreen);
    lv_label_set_text(avgLegend, "Avg"); lv_obj_set_pos(avgLegend, 112, SCREEN_H - 24);
    lv_obj_set_style_text_color(avgLegend, TC(accent), 0);
    updateTrackerAudioLabels();
    setGroup(trackerGroup);
    signalTrackerActive = true;
    lv_screen_load_anim(trackerScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}
