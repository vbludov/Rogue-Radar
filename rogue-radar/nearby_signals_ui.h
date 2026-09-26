#pragma once

#include <Arduino.h>
#include <new>
#include "known_device_discovery.h"
#include "known_signal_model.h"
#include "known_devices_api.h"

namespace rogue_radar {
namespace nearby_ui {

static constexpr uint8_t kRowsPerPage = 3;
static constexpr const char *kDiscoveryOwner = "nearby";
static constexpr uint32_t kMaxLearningCaptureMs = 30000UL;

enum class View : uint8_t { List, Detail, Learn, Exclusions, Suggestions };
enum class Pending : uint8_t { None, Return, SwitchRadio, Track, Save, Associate };

struct State {
    lv_obj_t *screen = nullptr;
    lv_group_t *group = nullptr;
    lv_obj_t *returnScreen = nullptr;
    lv_group_t *returnGroup = nullptr;
    lv_obj_t *status = nullptr;
    lv_obj_t *captureButton = nullptr;
    lv_obj_t *nextPhaseButton = nullptr;
    lv_obj_t *rows[kRowsPerPage]{};
    lv_obj_t *rowLabels[kRowsPerPage]{};
    lv_obj_t *rowCharts[kRowsPerPage]{};
    lv_chart_series_t *rowChartSeries[kRowsPerPage]{};
    KnownSignalTable *table = nullptr;
    KnownLearningSession *learning = nullptr;
    KnownRadio radio = KnownRadio::Ble;
    KnownRadio pendingRadio = KnownRadio::Ble;
    KnownSignalIdentity selected{};
    KnownSignalIdentity learnedTarget{};
    KnownAddress selectedObservation{};
    KnownAddress learnedObservation{};
    KnownSignalIdentity exclusions[KnownSignalTable::Capacity]{};
    uint8_t exclusionCount = 0;
    uint8_t page = 0;
    uint8_t reviewPage = 0;
    View view = View::List;
    View detailReturnView = View::List;
    Pending pending = Pending::None;
    bool active = false;
    bool suspended = false;
    bool childSeen = false;
    bool retryAllocation = false;
    bool scannerStarted = false;
    bool learningCapturing = false;
    bool learningCaptureComplete = false;
    bool learningCaptureSufficient = false;
    bool associationMode = false;
    bool learnSelectionMode = false;
    bool viewPending = false;
    View requestedView = View::List;
    uint32_t lastPaintMs = 0;
    uint32_t orderHoldUntilMs = 0;
    uint32_t suspendedAtMs = 0;
    uint32_t learningCaptureStartedMs = 0;
    uint32_t learningCaptureCompletedMs = 0;
};

static State state;

static const char *radioLabel(KnownRadio radio) {
    return radio == KnownRadio::Wifi ? "WiFi" : "BLE";
}

static const char *trendLabel(KnownSignalTrend trend) {
    switch (trend) {
        case KnownSignalTrend::Rising: return "+";
        case KnownSignalTrend::Falling: return "-";
        case KnownSignalTrend::Steady: return "=";
        default: return "?";
    }
}

static bool isExcluded(const KnownSignalIdentity &identity) {
    for (uint8_t i = 0; i < state.exclusionCount; ++i)
        if (sameKnownSignalIdentity(state.exclusions[i], identity)) return true;
    return false;
}

static void setExcluded(const KnownSignalIdentity &identity, bool excluded) {
    for (uint8_t i = 0; i < state.exclusionCount; ++i) {
        if (!sameKnownSignalIdentity(state.exclusions[i], identity)) continue;
        if (!excluded) {
            for (uint8_t j = i + 1; j < state.exclusionCount; ++j)
                state.exclusions[j - 1] = state.exclusions[j];
            --state.exclusionCount;
        }
        if (state.learning) state.learning->exclude(identity, excluded);
        return;
    }
    if (excluded && state.exclusionCount < KnownSignalTable::Capacity) {
        state.exclusions[state.exclusionCount++] = identity;
        if (state.learning) state.learning->exclude(identity, true);
    }
}

static void freeModels() {
    if (state.learning) {
        state.learning->~KnownLearningSession();
        free(state.learning);
        state.learning = nullptr;
    }
    if (state.table) {
        state.table->~KnownSignalTable();
        free(state.table);
        state.table = nullptr;
    }
}

static bool allocateModels() {
    if (!state.table) {
        void *memory = ps_malloc(sizeof(KnownSignalTable));
        if (!memory) return false;
        state.table = new (memory) KnownSignalTable();
    }
    if (!state.learning) {
        void *memory = ps_malloc(sizeof(KnownLearningSession));
        if (!memory) { freeModels(); return false; }
        state.learning = new (memory) KnownLearningSession();
    }
    return true;
}

static void deleteGroup() {
    if (state.group) { lv_group_delete(state.group); state.group = nullptr; }
}

static void prepareScreen(const char *title) {
    deleteGroup();
    lv_obj_clean(state.screen);
    applyScreenStyle(state.screen);
    createHeader(state.screen, title);
    state.group = lv_group_create();
    memset(state.rows, 0, sizeof(state.rows));
    memset(state.rowLabels, 0, sizeof(state.rowLabels));
    memset(state.rowCharts, 0, sizeof(state.rowCharts));
    memset(state.rowChartSeries, 0, sizeof(state.rowChartSeries));
}

static void addToGroup(lv_obj_t *object) {
    if (object && state.group) lv_group_add_obj(state.group, object);
}

static void cbBack(lv_event_t *);
static lv_obj_t *smallButton(const char *text, int x, int width,
                             lv_event_cb_t callback, void *data = nullptr) {
    lv_obj_t *button = createActionBtn(state.screen, text, callback, data);
    if (callback == cbBack) lv_obj_add_flag(button, LV_OBJ_FLAG_USER_1);
    lv_obj_set_size(button, width, 26);
    lv_obj_align(button, LV_ALIGN_BOTTOM_LEFT, x, -3);
    addToGroup(button);
    return button;
}

static void showList();
static void showDetail();
static void showLearn();
static void showExclusions();
static void showSuggestions();

static void requestView(View view) {
    state.requestedView = view;
    state.viewPending = true;
}

static void requestStop(Pending pending) {
    if (state.pending != Pending::None) return;
    state.pending = pending;
    if (state.status) lv_label_set_text(state.status, "Stopping radio...");
}

static void cbBack(lv_event_t *) {
    resetInactivityTimer();
    if (state.view == View::Detail) {
        if (state.detailReturnView == View::Suggestions) {
            state.selected = state.learnedTarget;
            state.selectedObservation = state.learnedObservation;
        }
        requestView(state.detailReturnView);
        return;
    }
    if (state.view == View::Learn || state.view == View::Exclusions ||
        state.view == View::Suggestions) {
        requestView(state.view == View::Learn ? View::Detail : View::Learn);
        return;
    }
    requestStop(Pending::Return);
}

static void cbSwitchRadio(lv_event_t *) {
    state.pendingRadio = state.radio == KnownRadio::Wifi ? KnownRadio::Ble
                                                         : KnownRadio::Wifi;
    requestStop(Pending::SwitchRadio);
}

static void cbPrev(lv_event_t *) {
    if (state.page) --state.page;
    state.lastPaintMs = 0;
}

static void cbNext(lv_event_t *) {
    if (state.table && (state.page + 1U) * kRowsPerPage < state.table->count()) ++state.page;
    state.lastPaintMs = 0;
}

static void selectRank(size_t rank) {
    if (!state.table) return;
    const KnownSignalCandidate *candidate = state.table->candidateAtRank(rank);
    if (!candidate) return;
    state.selected = candidate->identity;
    state.selectedObservation = candidate->observation;
    state.detailReturnView = View::List;
    state.table->setOrderFrozen(true);
    requestView(View::Detail);
}

static void cbRow(lv_event_t *event) {
    const size_t slot = static_cast<size_t>(reinterpret_cast<uintptr_t>(
        lv_event_get_user_data(event)));
    selectRank(state.page * kRowsPerPage + slot);
}

static void cbRowFocused(lv_event_t *) {
    state.orderHoldUntilMs = millis() + 2000UL;
}

static void cbRetryAllocation(lv_event_t *) {
    state.retryAllocation = true;
}

static void cbTrack(lv_event_t *) { requestStop(Pending::Track); }
static void cbSave(lv_event_t *) { requestStop(Pending::Save); }
static void cbAssociate(lv_event_t *) { requestStop(Pending::Associate); }

static void cbStartLearn(lv_event_t *) {
    if (!state.learning) return;
    state.exclusionCount = 0;
    state.learnedTarget = state.selected;
    state.learnedObservation = state.selectedObservation;
    state.learning->begin(state.radio, state.selectedObservation, millis());
    state.learningCapturing = false;
    state.learningCaptureComplete = false;
    state.learningCaptureSufficient = false;
    state.learningCaptureStartedMs = 0;
    state.learningCaptureCompletedMs = 0;
    requestView(View::Learn);
}

static void cbCaptureLearn(lv_event_t *) {
    if (!state.learning || state.learning->phase() == LearningPhase::Complete ||
        state.learningCapturing || state.learningCaptureComplete) return;
    state.learningCaptureStartedMs = millis();
    state.learningCaptureCompletedMs = 0;
    state.learning->restartCurrentPhase(state.learningCaptureStartedMs);
    state.learningCapturing = true;
    state.learningCaptureComplete = false;
    state.learningCaptureSufficient = false;
    state.lastPaintMs = 0;
}

static void cbAdvanceLearn(lv_event_t *) {
    if (!state.learning || !state.learningCaptureComplete) return;
    state.learning->advance(state.learningCaptureCompletedMs);
    state.learningCapturing = false;
    state.learningCaptureComplete = false;
    state.learningCaptureSufficient = false;
    state.learningCaptureStartedMs = 0;
    state.learningCaptureCompletedMs = 0;
    requestView(View::Learn);
}

static void cbOpenExclusions(lv_event_t *) {
    state.reviewPage = 0;
    requestView(View::Exclusions);
}
static void cbOpenSuggestions(lv_event_t *) {
    state.reviewPage = 0;
    requestView(View::Suggestions);
}
static void cbReviewPrev(lv_event_t *) {
    if (state.reviewPage) --state.reviewPage;
    requestView(state.view);
}
static void cbReviewNext(lv_event_t *) {
    ++state.reviewPage;
    requestView(state.view);
}

static void cbExcludeRow(lv_event_t *event) {
    const size_t rank = static_cast<size_t>(reinterpret_cast<uintptr_t>(
        lv_event_get_user_data(event)));
    if (!state.table) return;
    const KnownSignalCandidate *candidate = state.table->candidateAtRank(rank);
    if (!candidate || sameKnownSignalIdentity(candidate->identity, state.learnedTarget)) return;
    setExcluded(candidate->identity, !isExcluded(candidate->identity));
    requestView(View::Exclusions);
}

static void cbSuggestionRow(lv_event_t *event) {
    if (!state.learning || !state.table) return;
    PossibleKnownMatch matches[KnownLearningSession::SuggestionCapacity]{};
    const size_t count = state.learning->possibleMatches(
        matches, KnownLearningSession::SuggestionCapacity);
    const size_t index = static_cast<size_t>(reinterpret_cast<uintptr_t>(
        lv_event_get_user_data(event)));
    if (index >= count) return;
    const KnownSignalCandidate *candidate = state.table->find(matches[index].identity);
    if (!candidate) return;
    state.selected = candidate->identity;
    state.selectedObservation = candidate->observation;
    state.detailReturnView = View::Suggestions;
    requestView(View::Detail);
}

static void showList() {
    state.view = View::List;
    if (state.table) state.table->setOrderFrozen(false);
    prepareScreen(state.learnSelectionMode ? "Learn: choose signal" :
                  state.associationMode ? "Update from nearby" : "Nearby Signals");
    state.status = lv_label_create(state.screen);
    lv_obj_set_pos(state.status, 6, 27);
    lv_obj_set_width(state.status, SCREEN_W - 12);
    lv_label_set_long_mode(state.status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(state.status, lv_color_hex(TH.textDim), 0);

    for (uint8_t i = 0; i < kRowsPerPage; ++i) {
        lv_obj_t *button = lv_btn_create(state.screen);
        lv_obj_set_size(button, SCREEN_W - 8, 31);
        lv_obj_set_pos(button, 4, 42 + i * 32);
        styleListBtn(button);
        lv_obj_set_height(button, 31);
        lv_obj_set_style_pad_all(button, 1, 0);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, "");
        lv_obj_set_size(label, 202, 28);
        lv_obj_set_pos(label, 2, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_t *chart = lv_chart_create(button);
        lv_obj_set_size(chart, 95, 15);
        lv_obj_set_pos(chart, 210, 13);
        lv_obj_remove_flag(chart, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(chart, 0, 0);
        lv_obj_set_style_radius(chart, 0, 0);
        lv_obj_set_style_border_width(chart, 0, 0);
        lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart, KnownSignalTable::HistoryBuckets);
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -110, -20);
        lv_chart_set_div_line_count(chart, 0, 0);
        lv_chart_series_t *series = lv_chart_add_series(
            chart, lv_color_hex(TH.accent), LV_CHART_AXIS_PRIMARY_Y);
        lv_obj_set_style_line_width(chart, 1, LV_PART_ITEMS);
        lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
        lv_chart_set_all_value(chart, series, LV_CHART_POINT_NONE);
        state.rows[i] = button;
        state.rowLabels[i] = label;
        state.rowCharts[i] = chart;
        state.rowChartSeries[i] = series;
        lv_obj_add_event_cb(button, cbRow, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(button, cbRowFocused, LV_EVENT_FOCUSED, nullptr);
        addToGroup(button);
    }

    smallButton("Back", 4, 48, cbBack);
    smallButton(radioLabel(state.radio), 56, 58, cbSwitchRadio);
    smallButton("<", 118, 44, cbPrev);
    smallButton(">", 166, 44, cbNext);
    setGroup(state.group);
    state.lastPaintMs = 0;
}

static void showDetail() {
    state.view = View::Detail;
    if (state.table) state.table->setOrderFrozen(true);
    prepareScreen(state.learnSelectionMode ? "Learn candidate" : "Signal details");

    char savedName[33] = {};
    const bool saved = knownUiIsSaved(state.radio, state.selectedObservation,
                                      savedName, sizeof(savedName));
    char info[430];
    snprintf(info, sizeof(info),
             "%s%s%s\nType: %s%s\nAddress: %s\nRSSI: %d dBm  Channel: %u\n"
             "Address type: %u\nManufacturer: %04X\nData: %.64s%s\nServices: %.128s%s",
             saved ? "* " : "", saved ? savedName : state.selectedObservation.advertisedName,
             (!saved && !state.selectedObservation.advertisedName[0]) ? "<unnamed>" : "",
             radioLabel(state.radio), state.radio == KnownRadio::Ble ? " advertiser" : " AP",
             state.selectedObservation.address, state.selectedObservation.lastRssi,
             state.selectedObservation.channel, state.selectedObservation.addressType,
             state.selectedObservation.manufacturerId,
             state.selectedObservation.manufacturerData[0] ?
                 state.selectedObservation.manufacturerData : "none",
             state.selectedObservation.metadataTruncated ? "..." : "",
             state.selectedObservation.serviceUuids[0] ?
                 state.selectedObservation.serviceUuids : "none",
             state.selectedObservation.metadataTruncated ? "..." : "");
    lv_obj_t *card = lv_obj_create(state.screen);
    lv_obj_set_pos(card, 4, 28);
    lv_obj_set_size(card, SCREEN_W - 8, 103);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(TH.card), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_scroll_dir(card, LV_DIR_VER);
    addToGroup(card);
    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, info);
    lv_obj_set_width(label, SCREEN_W - 20);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(TH.text), 0);

    smallButton("Back", 4, 48, cbBack);
    if (state.associationMode) {
        smallButton("Associate", 58, 94, cbAssociate);
    } else if (state.learnSelectionMode) {
        smallButton("Start Learn", 58, 104, cbStartLearn);
    } else {
        smallButton("Track", 52, 48, cbTrack);
        smallButton(saved ? "Saved" : "Save", 104, 48, cbSave);
        smallButton("Learn", 156, 48, cbStartLearn);
        smallButton("Associate", 208, 104, cbAssociate);
    }
    setGroup(state.group);
}

static const char *phaseInstruction(LearningPhase phase) {
    switch (phase) {
        case LearningPhase::NearFirst: return "1/3 Place target 5-10cm beside radar";
        case LearningPhase::Away: return "2/3 Move only target 3-5m away (move radar for fixed AP)";
        case LearningPhase::NearSecond: return "3/3 Return to SAME spot/orientation";
        default: return "Capture complete - review evidence";
    }
}

static const char *outcomeLabel(LearningOutcome outcome) {
    switch (outcome) {
        case LearningOutcome::InsufficientEvidence: return "Insufficient evidence";
        case LearningOutcome::WeakResponse: return "Weak distance response";
        case LearningOutcome::InconsistentResponse: return "Near readings inconsistent";
        case LearningOutcome::AmbiguousResponse: return "Ambiguous: neighbor also matched";
        case LearningOutcome::ConsistentResponse: return "Consistent candidate response";
        default: return "Capture in progress";
    }
}

static void paintLearnStatus() {
    if (!state.learning || !state.status) return;
    const LearningMetrics metrics = state.learning->evaluate(millis());
    const uint32_t now = millis();
    const LearningPhaseEvidence current = state.learning->selectedPhaseEvidence(now);
    PossibleKnownMatch suggestions[KnownLearningSession::SuggestionCapacity]{};
    const size_t suggestionCount = state.learning->possibleMatches(
        suggestions, KnownLearningSession::SuggestionCapacity);
    char text[340];
    const bool complete = state.learning->phase() == LearningPhase::Complete;
    const uint32_t captureElapsedMs = state.learningCapturing
        ? now - state.learningCaptureStartedMs
        : state.learningCaptureComplete
            ? state.learningCaptureCompletedMs - state.learningCaptureStartedMs : 0;
    const char *captureStatus = complete ? outcomeLabel(metrics.outcome) :
        state.learningCaptureComplete
            ? (state.learningCaptureSufficient
                ? "Capture complete - press Next"
                : "Not enough samples; Next to review") :
        state.learningCapturing ? "Collecting - hold position" :
                                  "Position target; press Capture";
    snprintf(text, sizeof(text),
             "%s\nTarget: %.24s\nSamples N/A/N: %u / %u / %u\n"
             "Means: %d / %d / %d dBm\n%s\n"
             "Current: %lus, %u samples\n"
             "Possible matches: %u (review; never auto-linked)",
             phaseInstruction(state.learning->phase()), state.learnedObservation.address,
             metrics.selected[0].samples, metrics.selected[1].samples,
             metrics.selected[2].samples, metrics.selected[0].meanRssi,
             metrics.selected[1].meanRssi, metrics.selected[2].meanRssi,
             captureStatus,
             static_cast<unsigned long>(captureElapsedMs / 1000UL), current.samples,
             static_cast<unsigned>(suggestionCount));
    lv_label_set_text(state.status, text);
    if (state.captureButton) {
        if (!state.learningCapturing && !state.learningCaptureComplete)
            lv_obj_remove_state(state.captureButton, LV_STATE_DISABLED);
        else lv_obj_add_state(state.captureButton, LV_STATE_DISABLED);
    }
    if (state.nextPhaseButton) {
        if (state.learningCaptureComplete)
            lv_obj_remove_state(state.nextPhaseButton, LV_STATE_DISABLED);
        else lv_obj_add_state(state.nextPhaseButton, LV_STATE_DISABLED);
    }
}

static void showLearn() {
    state.view = View::Learn;
    if (state.table) state.table->setOrderFrozen(true);
    prepareScreen("Learn Known Device");
    state.captureButton = nullptr;
    state.nextPhaseButton = nullptr;
    state.status = lv_label_create(state.screen);
    lv_obj_set_pos(state.status, 7, 30);
    lv_obj_set_size(state.status, SCREEN_W - 14, 105);
    lv_label_set_long_mode(state.status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(state.status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(state.status, lv_color_hex(TH.text), 0);

    smallButton("Back", 4, 48, cbBack);
    smallButton("Exclude", 56, 72, cbOpenExclusions);
    if (state.learning->phase() != LearningPhase::Complete) {
        state.captureButton = smallButton("Capture", 132, 70, cbCaptureLearn);
        state.nextPhaseButton = smallButton("Next", 206, 56, cbAdvanceLearn);
    }
    else {
        smallButton("Matches", 132, 70, cbOpenSuggestions);
        smallButton("Save", 206, 48, cbSave);
        smallButton("Add to", 258, 58, cbAssociate);
    }
    paintLearnStatus();
    setGroup(state.group);
}

static void showExclusions() {
    state.view = View::Exclusions;
    if (state.table) state.table->setOrderFrozen(true);
    prepareScreen("Exclude neighbors");
    state.status = lv_label_create(state.screen);
    lv_label_set_text(state.status, "Select signals that are not the target");
    lv_obj_set_pos(state.status, 6, 28);
    lv_obj_set_style_text_color(state.status, lv_color_hex(TH.textDim), 0);

    const size_t first = state.reviewPage * kRowsPerPage;
    size_t ordinal = 0;
    uint8_t shown = 0;
    for (size_t rank = 0; state.table && rank < state.table->count() && shown < kRowsPerPage; ++rank) {
        const KnownSignalCandidate *candidate = state.table->candidateAtRank(rank);
        if (!candidate || sameKnownSignalIdentity(candidate->identity, state.learnedTarget)) continue;
        if (ordinal++ < first) continue;
        char text[72];
        snprintf(text, sizeof(text), "%s %.18s %ddBm",
                 isExcluded(candidate->identity) ? "[x]" : "[ ]",
                 candidate->observation.advertisedName[0] ?
                     candidate->observation.advertisedName : candidate->identity.address,
                 candidate->observation.lastRssi);
        lv_obj_t *button = lv_btn_create(state.screen);
        lv_obj_set_size(button, SCREEN_W - 8, 31);
        lv_obj_set_pos(button, 4, 42 + shown * 32);
        styleListBtn(button);
        lv_obj_set_height(button, 31);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_center(label);
        lv_obj_add_event_cb(button, cbExcludeRow, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(rank));
        addToGroup(button);
        ++shown;
    }
    smallButton("Done", 4, 56, cbBack);
    if (state.reviewPage) smallButton("<", 64, 44, cbReviewPrev);
    const size_t totalNeighbors = state.table && state.table->find(state.learnedTarget)
        ? state.table->count() - 1 : state.table ? state.table->count() : 0;
    if (first + shown < totalNeighbors)
        smallButton(">", 112, 44, cbReviewNext);
    setGroup(state.group);
}

static void showSuggestions() {
    state.view = View::Suggestions;
    if (state.table) state.table->setOrderFrozen(true);
    prepareScreen("Possible matches");
    state.status = lv_label_create(state.screen);
    lv_label_set_text(state.status, "Shared metadata only - select to review");
    lv_obj_set_pos(state.status, 6, 28);
    lv_obj_set_style_text_color(state.status, lv_color_hex(TH.textDim), 0);

    PossibleKnownMatch matches[KnownLearningSession::SuggestionCapacity]{};
    const size_t count = state.learning ? state.learning->possibleMatches(
        matches, KnownLearningSession::SuggestionCapacity) : 0;
    const size_t first = state.reviewPage * kRowsPerPage;
    for (uint8_t slot = 0; slot < kRowsPerPage && first + slot < count; ++slot) {
        const size_t index = first + slot;
        const KnownSignalCandidate *candidate = state.table->find(matches[index].identity);
        char text[90];
        snprintf(text, sizeof(text), "score %u  %.24s\n%s",
                 matches[index].similarityScore,
                 candidate && candidate->observation.advertisedName[0]
                     ? candidate->observation.advertisedName : matches[index].identity.address,
                 matches[index].identity.address);
        lv_obj_t *button = lv_btn_create(state.screen);
        lv_obj_set_size(button, SCREEN_W - 8, 31);
        lv_obj_set_pos(button, 4, 42 + slot * 32);
        styleListBtn(button);
        lv_obj_set_height(button, 31);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
        lv_obj_center(label);
        lv_obj_add_event_cb(button, cbSuggestionRow, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(index));
        addToGroup(button);
    }
    if (count == 0) {
        lv_obj_t *empty = lv_label_create(state.screen);
        lv_label_set_text(empty, "No similar advertisement metadata observed.");
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
    }
    smallButton("Done", 4, 56, cbBack);
    if (state.reviewPage) smallButton("<", 64, 44, cbReviewPrev);
    if (first + kRowsPerPage < count) smallButton(">", 112, 44, cbReviewNext);
    setGroup(state.group);
}

static void paintList() {
    if (state.view != View::List || !state.table) return;
    const size_t first = state.page * kRowsPerPage;
    for (uint8_t slot = 0; slot < kRowsPerPage; ++slot) {
        const size_t rank = first + slot;
        const KnownSignalCandidate *candidate = state.table->candidateAtRank(rank);
        if (!candidate) {
            lv_obj_add_flag(state.rows[slot], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(state.rows[slot], LV_OBJ_FLAG_HIDDEN);
        char savedName[33] = {};
        const bool saved = knownUiIsSaved(state.radio, candidate->observation,
                                          savedName, sizeof(savedName));
        char text[92];
        const char *name = saved ? savedName : candidate->observation.advertisedName;
        if (!name[0]) name = candidate->identity.address;
        snprintf(text, sizeof(text), "%s%.18s  %ddBm %s\n%.17s%s",
                 saved ? "*" : "", name, candidate->smoothedRssi,
                 trendLabel(candidate->trend), candidate->identity.address,
                 candidate->stale ? " stale" : "");
        lv_label_set_text(state.rowLabels[slot], text);
        lv_chart_set_series_color(state.rowCharts[slot], state.rowChartSeries[slot],
                                  bleRssiColor(candidate->smoothedRssi));
        for (size_t point = 0; point < KnownSignalTable::HistoryBuckets; ++point) {
            const int16_t rssi = candidate->history[
                KnownSignalTable::HistoryBuckets - 1 - point];
            lv_chart_set_value_by_id(
                state.rowCharts[slot], state.rowChartSeries[slot], point,
                rssi == KnownSignalCandidate::MissingRssi
                    ? LV_CHART_POINT_NONE : rssi);
        }
        lv_chart_refresh(state.rowCharts[slot]);
        lv_obj_set_style_text_color(state.rows[slot],
                                    bleRssiColor(candidate->smoothedRssi),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    char status[80];
    const unsigned pages = static_cast<unsigned>(
        (state.table->count() + kRowsPerPage - 1) / kRowsPerPage);
    snprintf(status, sizeof(status), "%s  %u signals  page %u/%u%s",
             knownDiscoveryStatus(), static_cast<unsigned>(state.table->count()),
             state.page + 1, pages ? pages : 1,
             knownDiscoveryBackend.dropped() ? "  drops" : "");
    lv_label_set_text(state.status, status);
}

static bool startScanner(KnownRadio radio, bool resetTable) {
    if (!state.table) return false;
    if (resetTable) {
        state.table->reset(radio);
        state.page = 0;
    }
    state.radio = radio;
    state.scannerStarted = knownDiscoveryStart(kDiscoveryOwner, radio);
    return state.scannerStarted;
}

static void finishReturn() {
    lv_obj_t *screen = state.screen;
    lv_obj_t *returnScreen = state.returnScreen ? state.returnScreen : mainScreen;
    lv_group_t *returnGroup = state.returnGroup ? state.returnGroup : navGroup;
    state.screen = nullptr;
    state.active = false;
    deleteGroup();
    setGroup(returnGroup);
    freeModels();
    lv_screen_load_anim(returnScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, true);
    (void)screen;
}

static void dispatchPending() {
    const Pending pending = state.pending;
    state.pending = Pending::None;
    state.scannerStarted = false;
    if (pending == Pending::Return) { finishReturn(); return; }
    if (pending == Pending::SwitchRadio) {
        startScanner(state.pendingRadio, true);
        showList();
        return;
    }
    state.suspended = true;
    state.childSeen = false;
    state.suspendedAtMs = millis();
    if (pending == Pending::Track)
        knownUiTrackCandidate(state.selectedObservation, state.radio);
    else if (pending == Pending::Save)
        knownUiSaveCandidate(state.selectedObservation, state.radio);
    else if (pending == Pending::Associate)
        knownUiAssociateCandidate(state.selectedObservation, state.radio);
}

static void create(bool learnSelection, bool association, KnownRadio initialRadio) {
    if (state.active) return;
    state = State{};
    state.active = true;
    state.learnSelectionMode = learnSelection;
    state.associationMode = association;
    state.radio = initialRadio;
    state.returnScreen = lv_screen_active();
    state.returnGroup = lv_indev_get_group(lvIndev);
    if (!allocateModels()) {
        state.screen = lv_obj_create(nullptr);
        prepareScreen("Nearby Signals");
        state.status = lv_label_create(state.screen);
        lv_label_set_text(state.status, "PSRAM unavailable\nDiscovery cannot start");
        lv_obj_center(state.status);
        smallButton("Back", 4, 48, cbBack);
        smallButton("Retry", 56, 60, cbRetryAllocation);
        setGroup(state.group);
        lv_screen_load_anim(state.screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
        return;
    }
    state.table->reset(initialRadio);
    state.screen = lv_obj_create(nullptr);
    showList();
    if (!startScanner(initialRadio, false))
        lv_label_set_text(state.status, knownDiscoveryStatus());
    lv_screen_load_anim(state.screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}

static void process() {
    if (!state.active) return;
    const bool uiMutationSafe = lv_screen_active() == state.screen &&
        !lv_display_get_screen_prev(lvDisp) && digitalRead(ENCODER_BTN) != LOW;
    if (state.retryAllocation && uiMutationSafe) {
        state.retryAllocation = false;
        if (allocateModels()) {
            state.table->reset(state.radio);
            showList();
            if (!startScanner(state.radio, false) && state.status)
                lv_label_set_text(state.status, knownDiscoveryStatus());
        } else if (state.status) {
            lv_label_set_text(state.status, "PSRAM still unavailable");
        }
        return;
    }
    if (state.suspended) {
        if (!state.childSeen) {
            if (lv_screen_active() != state.screen || lv_display_get_screen_prev(lvDisp))
                state.childSeen = true;
            else if (millis() - state.suspendedAtMs >= 750) {
                state.suspended = false;
                if (!startScanner(state.radio, false) && state.status)
                    lv_label_set_text(state.status, "Action unavailable; retry");
            }
            return;
        }
        if (lv_screen_active() != state.screen || lv_display_get_screen_prev(lvDisp)) return;
        state.suspended = false;
        state.childSeen = false;
        if (!startScanner(state.radio, false) && state.status)
            lv_label_set_text(state.status, knownDiscoveryStatus());
        return;
    }
    if (state.viewPending && uiMutationSafe) {
        const View requested = state.requestedView;
        state.viewPending = false;
        if (requested == View::List) showList();
        else if (requested == View::Detail) showDetail();
        else if (requested == View::Learn) showLearn();
        else if (requested == View::Exclusions) showExclusions();
        else showSuggestions();
    }
    if (state.pending != Pending::None) {
        knownDiscoveryPoll(kDiscoveryOwner);
        if (!knownDiscoveryStop(kDiscoveryOwner)) return;
        if (!uiMutationSafe) return;
        dispatchPending();
        return;
    }
    if (!state.scannerStarted) return;

    knownDiscoveryPoll(kDiscoveryOwner);
    KnownDiscoveryObservation observation;
    while (knownDiscoveryTake(kDiscoveryOwner, observation)) {
        state.table->observe(observation.radio, observation.address, millis());
        if (state.learning && state.learningCapturing &&
            (state.view == View::Learn || state.view == View::Exclusions)) {
            const uint32_t sampleNow = millis();
            state.learning->observe(observation.radio, observation.address, sampleNow);
            if (state.learning->currentPhaseReady(sampleNow)) {
                state.learningCapturing = false;
                state.learningCaptureComplete = true;
                state.learningCaptureSufficient = true;
                state.learningCaptureCompletedMs = sampleNow;
                state.lastPaintMs = 0;
            }
        }
        const KnownSignalIdentity observedIdentity = knownSignalIdentity(
            observation.radio, observation.address);
        if (sameKnownSignalIdentity(state.selected, observedIdentity)) {
            const KnownSignalCandidate *candidate = state.table->find(observedIdentity);
            if (candidate) state.selectedObservation = candidate->observation;
        }
    }
    const uint32_t now = millis();
    if (state.learning && state.learningCapturing) {
        const bool sufficient = state.learning->currentPhaseReady(now);
        const bool timedOut = now - state.learningCaptureStartedMs >=
                              kMaxLearningCaptureMs;
        if (sufficient || timedOut) {
            state.learningCapturing = false;
            state.learningCaptureComplete = true;
            state.learningCaptureSufficient = sufficient;
            state.learningCaptureCompletedMs = now;
            state.lastPaintMs = 0;
        }
    }
    if (state.view == View::List) {
        const bool holdingOrder = static_cast<int32_t>(state.orderHoldUntilMs - now) > 0;
        state.table->setOrderFrozen(holdingOrder);
    }
    // Retire vanished advertisers promptly so an old strong sample cannot
    // dominate the walking survey. Frozen selection remains explicitly stale.
    state.table->expireStale(now, 10000);
    state.table->refreshRanking(now, 1000);
    if (state.view == View::List && now - state.lastPaintMs >= 500) {
        state.lastPaintMs = now;
        paintList();
    } else if (state.view == View::Learn && now - state.lastPaintMs >= 1000) {
        state.lastPaintMs = now;
        paintLearnStatus();
    }
}

}  // namespace nearby_ui
}  // namespace rogue_radar

static void createNearbySignals() {
    rogue_radar::nearby_ui::create(false, false, rogue_radar::KnownRadio::Ble);
}

static void createLearnKnownDevice() {
    rogue_radar::nearby_ui::create(true, false, rogue_radar::KnownRadio::Ble);
}

static void createNearbySignalsForAssociation(rogue_radar::KnownRadio radio) {
    rogue_radar::nearby_ui::create(false, true, radio);
}

static void processNearbySignals() {
    rogue_radar::nearby_ui::process();
}

static bool nearbySignalsActive() {
    return rogue_radar::nearby_ui::state.active;
}

static bool nearbySignalsSuspended() {
    return rogue_radar::nearby_ui::state.active &&
           rogue_radar::nearby_ui::state.suspended;
}
