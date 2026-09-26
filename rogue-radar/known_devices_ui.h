#pragma once
#include <new>
#include <strings.h>
#include "known_device_types.h"

// All screen mutations and SD transactions are deferred to loop(). Two frames
// allow Saved -> Nearby -> confirm association without destroying a return page.
namespace known_devices_ui {
using namespace rogue_radar;
enum class Action : uint8_t {
    None, List, Pick, Detail, Back, Next, Previous, Track, TrackAddress,
    Addresses, RemoveConfirm, Remove, DeleteConfirm, Delete, Rename, NameNew,
    SaveNew, AssociatePick, AssociateConfirm, Associate, UpdateNearby, Retry
};
struct Context {
    lv_obj_t *screen = nullptr, *list = nullptr, *status = nullptr;
    lv_group_t *group = nullptr;
    lv_obj_t *returnScreen = nullptr;
    lv_group_t *returnGroup = nullptr;
    KnownDevice record{};
    KnownAddress candidate{};
    KnownRadio candidateRadio = KnownRadio::Ble;
    KnownDeviceSummary summaries[6]{};
    uint32_t cursor = 0, nextCursor = 0, previous[16]{};
    uint8_t previousCount = 0, count = 0, addressIndex = 0;
    Action pending = Action::None, page = Action::List;
    int argument = 0;
    bool association = false, searching = false, draining = false;
    bool wasAway = false, tracked = false, keyboardNew = false;
    uint32_t searchStart = 0, lastPaint = 0;
    uint32_t seen[8]{};
    char enteredName[33]{};
};
static Context *frames[2] = {};
static int depth = -1;
static uint32_t associationTarget = 0;
static uint32_t libraryGeneration = 0;
static Context *current() { return depth >= 0 ? frames[depth] : nullptr; }
static void request(Action action, int argument = 0) {
    Context *c = current();
    if (!c) return;
    c->pending = action; c->argument = argument;
    resetInactivityTimer();
}
static void event(lv_event_t *e) {
    const uintptr_t encoded = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    request(static_cast<Action>(encoded >> 16), encoded & 0xffff);
}
static void button(Context &c, const char *text, Action action, int argument = 0) {
    lv_obj_t *b = lv_list_add_btn(c.list, nullptr, text);
    styleListBtn(b);
    lv_obj_set_height(b, 27);
    lv_obj_add_event_cb(b, event, LV_EVENT_CLICKED,
        reinterpret_cast<void *>((static_cast<uintptr_t>(action) << 16) | (argument & 0xffff)));
    lv_group_add_obj(c.group, b);
}
static void note(Context &c, const char *text) {
    lv_obj_t *label = lv_list_add_text(c.list, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}
static void clear(Context &c, const char *title) {
    deleteGroup(&c.group);
    lv_obj_clean(c.screen);
    applyScreenStyle(c.screen);
    createHeader(c.screen, title);
    c.list = lv_list_create(c.screen);
    lv_obj_set_pos(c.list, 0, 28);
    lv_obj_set_size(c.list, SCREEN_W, SCREEN_H - 61);
    lv_obj_set_style_pad_all(c.list, 2, 0);
    lv_obj_set_style_pad_row(c.list, 1, 0);
    lv_obj_set_style_border_width(c.list, 0, 0);
    lv_obj_set_style_bg_color(c.list, TC(bg), 0);
    c.group = lv_group_create();
    lv_obj_t *back = createBackBtn(c.screen, [](lv_event_t *) { request(Action::Back); });
    lv_obj_set_width(back, 64);
    lv_group_add_obj(c.group, back);
    setGroup(c.group);
    c.status = nullptr;
}
static void error(Context &c, KnownStoreStatus status) {
    clear(c, "Saved Devices");
    note(c, knownStoreStatusText(status));
    note(c, "SD library unavailable or operation failed. Live tracking remains available.");
    button(c, "Retry library", Action::Retry);
}
static bool ready(Context &c) {
    if (knownDeviceStorageReady()) return true;
    error(c, KnownStoreStatus::Unavailable); return false;
}
static Context *begin() {
    if (depth >= 1 || keyboardActive || signalTrackerActive) return nullptr;
    void *memory = ps_malloc(sizeof(Context));
    if (!memory && ESP.getFreeHeap() > sizeof(Context) + 48000) memory = malloc(sizeof(Context));
    if (!memory) { Serial.println("[Known] Insufficient memory for saved-device page"); return nullptr; }
    Context *c = new (memory) Context();
    c->returnScreen = lv_screen_active();
    c->returnGroup = lv_indev_get_group(lvIndev);
    c->screen = lv_obj_create(nullptr);
    frames[++depth] = c;
    clear(*c, "Saved Devices");
    lv_screen_load_anim(c->screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
    return c;
}
static void close(Context &c) {
    lv_obj_t *returnScreen = c.returnScreen;
    lv_group_t *returnGroup = c.returnGroup;
    deleteGroup(&c.group);
    setGroup(returnGroup);
    lv_screen_load_anim(returnScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);
    frames[depth--] = nullptr;
    c.~Context(); free(&c);
    if (depth < 0) associationTarget = 0;
}
static void showList(Context &c) {
    c.page = c.association ? Action::AssociatePick : Action::List;
    if (!ready(c)) return;
    KnownStoreStatus status = knownDeviceStore().listPage(c.cursor, c.summaries, c.count, c.nextCursor);
    if (status != KnownStoreStatus::Ok) { error(c, status); return; }
    clear(c, c.association ? "Choose known device" : "Saved Devices");
    note(c, c.association ? "Select a device, then confirm this address." : "Find and Track Known Devices");
    unsigned shown = 0;
    for (uint8_t i = 0; i < c.count; ++i) {
        const auto &entry = c.summaries[i];
        if (c.association && entry.radio != c.candidateRadio) continue;
        char label[80];
        snprintf(label, sizeof(label), "%s: %.32s (%u)", entry.radio == KnownRadio::Ble ? "BLE" : "WiFi",
                 entry.name, entry.addressCount);
        button(c, label, Action::Pick, i); ++shown;
    }
    if (!shown) note(c, c.association ? "No compatible entries on this page." : "No saved devices. Save one from a result or Nearby Signals.");
    if (c.count == 6 && c.nextCursor != c.cursor) button(c, "Next page >", Action::Next);
    if (c.previousCount) button(c, "< Previous page", Action::Previous);
}
static void detail(Context &c) {
    c.page = Action::Detail;
    if (!ready(c)) return;
    KnownStoreStatus status = knownDeviceStore().read(c.record.id, c.record);
    if (status != KnownStoreStatus::Ok) { error(c, status); return; }
    clear(c, c.record.name);
    note(c, "Find and Track Known Devices");
    button(c, "Track Signal", Action::Track);
    button(c, "Confirmed addresses", Action::Addresses);
    if (nearbySignalsActive())
        note(c, "Back to Nearby to associate another address.");
    else
        button(c, "Update from nearby device", Action::UpdateNearby);
    button(c, "Rename", Action::Rename);
    button(c, "Delete saved device", Action::DeleteConfirm);
}
static void addresses(Context &c) {
    c.page = Action::Addresses;
    clear(c, "Confirmed addresses");
    for (uint8_t i = 0; i < c.record.addressCount; ++i) {
        const auto &a = c.record.addresses[i];
        char text[100];
        snprintf(text, sizeof(text), "%s  type:%u\n%s  captured %d dBm", a.address,
                 a.addressType, a.advertisedName, a.lastRssi);
        note(c, text);
        button(c, "Track this address", Action::TrackAddress, i);
        button(c, "Remove this address...", Action::RemoveConfirm, i);
    }
    note(c, "Capture time may be unavailable after reboot. Signal is measured live when tracking.");
}
static void candidateReview(Context &c, bool associate) {
    c.page = associate ? Action::AssociateConfirm : Action::NameNew;
    clear(c, associate ? "Confirm association" : "Save selected device");
    char text[220];
    snprintf(text, sizeof(text), "%s\n%s\nAddress type: %u | %d dBm\n%s",
             c.candidate.advertisedName, c.candidate.address, c.candidate.addressType,
             c.candidate.lastRssi, associate ? c.record.name : "Save this selected identity on SD.");
    note(c, text);
    note(c, "A name or strong signal alone does not prove identity.");
    if (associate) button(c, "Confirm: add this address", Action::Associate);
    else button(c, "Name and save this device", Action::NameNew);
}
static void keyboardDone(const char *text, bool accepted) {
    Context *c = current();
    if (!c) return;
    // The keyboard retires itself only after its owner has restored a screen.
    setGroup(c->group);
    loadScreenWithoutSlide(c->screen);
    if (!accepted) return;
    snprintf(c->enteredName, sizeof(c->enteredName), "%s", text ? text : "");
    request(c->keyboardNew ? Action::SaveNew : Action::Rename, 1);
}
static bool matches(KnownRadio radio, const KnownAddress &a, const KnownAddress &b) {
    return strcasecmp(a.address, b.address) == 0 &&
           (radio == KnownRadio::Wifi || a.addressType == b.addressType);
}
static void startTracker(Context &c, uint8_t index) {
    c.addressIndex = index;
    c.tracked = true; c.wasAway = false;
    const KnownAddress &a = c.record.addresses[index];
    createSignalTracker(c.record.radio == KnownRadio::Ble, c.record.name, a.address, a.channel,
                        a.addressType);
}
static void startSearch(Context &c) {
    if (c.record.addressCount == 1) { startTracker(c, 0); return; }
    clear(c, "Find confirmed addresses");
    note(c, "Only saved, confirmed addresses are eligible. Multiple matches require selection.");
    c.status = lv_list_add_text(c.list, "Waiting for saved device...");
    memset(c.seen, 0, sizeof(c.seen));
    if (!knownDiscoveryStart("saved", c.record.radio)) {
        note(c, knownDiscoveryStatus()); return;
    }
    c.searching = true; c.searchStart = millis(); c.lastPaint = 0;
    c.page = Action::Track;
}
static void foundChoices(Context &c) {
    c.searching = c.draining = false;
    unsigned found = 0; uint8_t single = 0;
    const uint32_t now = millis();
    for (uint8_t i = 0; i < c.record.addressCount; ++i)
        if (c.seen[i] && now - c.seen[i] <= 10000) { ++found; single = i; }
    if (found == 1) { startTracker(c, single); return; }
    clear(c, "Choose active address");
    note(c, "Several confirmed addresses were seen. Select one; readings are never combined.");
    for (uint8_t i = 0; i < c.record.addressCount; ++i)
        if (c.seen[i] && now - c.seen[i] <= 10000)
            button(c, c.record.addresses[i].address, Action::TrackAddress, i);
    if (!found) { note(c, "Signal disappeared. Try again."); button(c, "Search again", Action::Track); }
}
static void processSearch(Context &c) {
    if (c.draining || c.pending != Action::None) {
        c.draining = true;
        if (!knownDiscoveryStop("saved")) return;
        c.searching = c.draining = false;
        if (c.pending == Action::None) foundChoices(c);
        return;
    }
    knownDiscoveryPoll("saved");
    KnownDiscoveryObservation observation;
    while (knownDiscoveryTake("saved", observation)) {
        if (observation.radio != c.record.radio) continue;
        for (uint8_t i = 0; i < c.record.addressCount; ++i) {
            if (!matches(c.record.radio, c.record.addresses[i], observation.address)) continue;
            c.seen[i] = observation.address.lastSeenUptimeMs;
            // Keep the confirmed identity; observed metadata is updated in RAM.
            c.record.addresses[i].lastRssi = observation.address.lastRssi;
            c.record.addresses[i].lastSeenUptimeMs = observation.address.lastSeenUptimeMs;
        }
    }
    const uint32_t now = millis();
    unsigned found = 0;
    for (uint8_t i = 0; i < c.record.addressCount; ++i)
        if (c.seen[i] && now - c.seen[i] <= 10000) ++found;
    if (now - c.lastPaint > 500 && c.status) {
        c.lastPaint = now;
        lv_label_set_text_fmt(c.status, "Waiting for %.32s\n%u confirmed addresses present", c.record.name, found);
    }
    if (found && now - c.searchStart >= 8000) c.draining = true;
}
static void run(Context &c, Action action, int argument) {
    KnownStoreStatus status = KnownStoreStatus::Ok;
    switch (action) {
    case Action::Retry:
        if (knownDeviceStorageRetry()) { ++libraryGeneration; showList(c); }
        else error(c, KnownStoreStatus::Unavailable);
        break;
    case Action::List: showList(c); break;
    case Action::Next:
        if (c.previousCount == 16) { memmove(c.previous, c.previous + 1, 15 * sizeof(uint32_t)); --c.previousCount; }
        c.previous[c.previousCount++] = c.cursor; c.cursor = c.nextCursor; showList(c); break;
    case Action::Previous:
        if (c.previousCount) c.cursor = c.previous[--c.previousCount];
        showList(c); break;
    case Action::Pick:
        if (argument < 0 || argument >= c.count) break;
        status = knownDeviceStore().read(c.summaries[argument].id, c.record);
        if (status == KnownStoreStatus::Ok) {
            if (c.association) candidateReview(c, true); else detail(c);
        }
        break;
    case Action::Detail: detail(c); break;
    case Action::Track: startSearch(c); break;
    case Action::TrackAddress:
        if (argument >= 0 && argument < c.record.addressCount) startTracker(c, argument);
        break;
    case Action::Addresses: addresses(c); break;
    case Action::RemoveConfirm:
        c.addressIndex = argument; c.page = Action::RemoveConfirm;
        clear(c, "Remove address?");
        note(c, c.record.addresses[c.addressIndex].address);
        if (c.record.addressCount > 1) button(c, "Confirm remove address", Action::Remove);
        else note(c, "Keep at least one address, or delete the saved device.");
        break;
    case Action::Remove:
        status = knownDeviceStore().removeAddress(c.record.id, c.addressIndex);
        if (status == KnownStoreStatus::Ok) detail(c);
        break;
    case Action::DeleteConfirm:
        c.page = Action::DeleteConfirm; clear(c, "Delete saved device?");
        note(c, c.record.name); button(c, "Confirm delete", Action::Delete); break;
    case Action::Delete:
        status = knownDeviceStore().deleteDevice(c.record.id);
        if (status == KnownStoreStatus::Ok) { c.cursor = 0; c.previousCount = 0; c.record = KnownDevice{}; showList(c); }
        break;
    case Action::NameNew:
        c.keyboardNew = true;
        createKeyboardScreen("Name saved device", c.candidate.advertisedName, 32, keyboardDone); break;
    case Action::SaveNew:
        c.record = KnownDevice{};
        c.record.radio = c.candidateRadio; c.record.addressCount = 1;
        c.record.addresses[0] = c.candidate;
        snprintf(c.record.name, sizeof(c.record.name), "%s", c.enteredName);
        status = knownDeviceStore().create(c.record);
        if (status == KnownStoreStatus::Ok) detail(c);
        break;
    case Action::Rename:
        if (argument == 1) {
            status = knownDeviceStore().renameDevice(c.record.id, c.enteredName);
            if (status == KnownStoreStatus::Ok) detail(c);
        } else { c.keyboardNew = false; createKeyboardScreen("Rename device", c.record.name, 32, keyboardDone); }
        break;
    case Action::Associate:
        status = knownDeviceStore().addAddress(c.record.id, c.candidate);
        if (status == KnownStoreStatus::Ok) { clear(c, "Address saved"); note(c, c.record.name); note(c, c.candidate.address); c.page = Action::Associate; }
        break;
    case Action::UpdateNearby:
        associationTarget = c.record.id; c.wasAway = false;
        createNearbySignalsForAssociation(c.record.radio); break;
    case Action::Back:
        if (c.page == Action::Detail && !c.association && !c.candidate.address[0]) { showList(c); break; }
        if (c.page == Action::Addresses || c.page == Action::DeleteConfirm || c.page == Action::RemoveConfirm || c.page == Action::Track) { detail(c); break; }
        close(c); return;
    default: break;
    }
    if (status != KnownStoreStatus::Ok) error(c, status);
    else if (action == Action::SaveNew || action == Action::Associate || action == Action::Delete ||
             action == Action::Remove || (action == Action::Rename && argument == 1)) ++libraryGeneration;
}
} // namespace known_devices_ui

static void createSavedDevices() {
    auto *c = known_devices_ui::begin();
    if (c) c->pending = known_devices_ui::Action::List;
}
static bool knownDevicesActive() { return known_devices_ui::depth >= 0; }
static void processKnownDevices() {
    using namespace known_devices_ui;
    Context *c = current(); if (!c) return;
    if (lv_screen_active() != c->screen) { c->wasAway = true; return; }
    if (lv_display_get_screen_prev(lvDisp) || keyboardActive || keyboardFinishPending || signalTrackerActive) return;
    if (c->wasAway) {
        c->wasAway = false;
        if (c->tracked) {
            c->tracked = false;
            int16_t raw; float smoothed;
            if (trackerModel.current(millis(), raw, smoothed) && c->addressIndex < c->record.addressCount) {
                c->record.addresses[c->addressIndex].lastRssi = raw;
                c->record.addresses[c->addressIndex].lastSeenUptimeMs = millis();
                // One observation write on returning from a tracking session.
                auto result = knownDeviceStore().update(c->record);
                if (result != KnownStoreStatus::Ok) { error(*c, result); return; }
            }
        } else if (c->record.id && c->page == Action::Detail) {
            // A nested Nearby/association flow may have added an address.
            detail(*c);
            if (depth == 0) associationTarget = 0;
        }
    }
    if (c->searching || c->draining) { processSearch(*c); if (c->searching || c->draining) return; }
    if (digitalRead(ENCODER_BTN) == LOW || c->pending == Action::None) return;
    const Action action = c->pending; const int argument = c->argument;
    c->pending = Action::None; run(*c, action, argument);
}
static void knownUiSaveCandidate(const rogue_radar::KnownAddress &address, rogue_radar::KnownRadio radio) {
    char savedName[33]{};
    const bool alreadySaved = knownUiIsSaved(radio, address, savedName, sizeof(savedName));
    auto *c = known_devices_ui::begin(); if (!c) return;
    if (alreadySaved) {
        known_devices_ui::clear(*c, "Already saved");
        known_devices_ui::note(*c, savedName);
        known_devices_ui::note(*c, "Open Saved Devices to find, track, or rename this device.");
        return;
    }
    c->candidate = address; c->candidateRadio = radio;
    if (known_devices_ui::ready(*c)) known_devices_ui::candidateReview(*c, false);
}
static void knownUiAssociateCandidate(const rogue_radar::KnownAddress &address, rogue_radar::KnownRadio radio) {
    using namespace known_devices_ui;
    auto *c = begin(); if (!c) return;
    c->candidate = address; c->candidateRadio = radio; c->association = true;
    if (!ready(*c)) return;
    if (associationTarget) {
        auto status = knownDeviceStore().read(associationTarget, c->record);
        if (status != KnownStoreStatus::Ok) { error(*c, status); return; }
        if (c->record.radio != radio) { error(*c, KnownStoreStatus::InvalidArgument); return; }
        candidateReview(*c, true);
    } else showList(*c);
}
static void knownUiTrackCandidate(const rogue_radar::KnownAddress &address, rogue_radar::KnownRadio radio) {
    createSignalTracker(radio == rogue_radar::KnownRadio::Ble,
                        address.advertisedName[0] ? address.advertisedName : address.address,
                        address.address, address.channel, address.addressType);
}
static bool knownUiIsSaved(rogue_radar::KnownRadio radio, const rogue_radar::KnownAddress &address,
                           char *name, size_t size) {
    using namespace rogue_radar;
    // Bounded cache keeps repeated row painting away from the SD bus. Records
    // are streamed on a miss rather than retaining the entire library in RAM.
    struct CacheEntry { KnownRadio radio; char mac[18]; uint8_t type; char name[33]; uint32_t checked, generation; bool valid; };
    static CacheEntry *cache = nullptr;
    static KnownDevice *scratch = nullptr;
    static uint8_t replace = 0;
    static bool unavailable = false;
    static uint32_t unavailableAt = 0, unavailableGeneration = 0;
    if (name && size) name[0] = '\0';
    if (!cache) { cache = static_cast<CacheEntry *>(ps_calloc(32, sizeof(CacheEntry))); }
    if (!scratch) { void *p = ps_malloc(sizeof(KnownDevice)); if (p) scratch = new (p) KnownDevice(); }
    if (!cache || !scratch) return false;
    const uint32_t now = millis();
    // A missing card must not trigger several blocking mount attempts for
    // every live-list repaint. Explicit Retry invalidates this negative cache.
    if (unavailable && unavailableGeneration == known_devices_ui::libraryGeneration &&
        now - unavailableAt < 30000) return false;
    for (unsigned i = 0; i < 32; ++i) {
        auto &entry = cache[i];
        if (entry.valid && entry.generation == known_devices_ui::libraryGeneration &&
            entry.radio == radio &&
            (radio == KnownRadio::Wifi || entry.type == address.addressType) &&
            strcasecmp(entry.mac, address.address) == 0 && now - entry.checked < 10000) {
            if (name && size) snprintf(name, size, "%s", entry.name);
            return entry.name[0] != '\0';
        }
    }
    auto &entry = cache[replace++ % 32];
    entry = CacheEntry{}; entry.valid = true; entry.radio = radio; entry.type = address.addressType;
    entry.generation = known_devices_ui::libraryGeneration;
    entry.checked = now; snprintf(entry.mac, sizeof(entry.mac), "%s", address.address);
    if (!knownDeviceStorageReady()) {
        unavailable = true; unavailableAt = now;
        unavailableGeneration = known_devices_ui::libraryGeneration;
        return false;
    }
    unavailable = false;
    uint32_t cursor = 0, next = 0;
    KnownDeviceSummary page[6]; uint8_t count = 0;
    do {
        if (knownDeviceStore().listPage(cursor, page, count, next) != KnownStoreStatus::Ok) break;
        for (uint8_t i = 0; i < count; ++i) {
            if (page[i].radio != radio || knownDeviceStore().read(page[i].id, *scratch) != KnownStoreStatus::Ok) continue;
            for (uint8_t j = 0; j < scratch->addressCount; ++j)
                if (known_devices_ui::matches(radio, scratch->addresses[j], address)) {
                    snprintf(entry.name, sizeof(entry.name), "%s", scratch->name);
                    if (name && size) snprintf(name, size, "%s", entry.name);
                    return true;
                }
        }
        if (next == cursor) break;
        cursor = next;
    } while (count == 6);
    return false;
}
static void knownInstallSaveButton(lv_obj_t *screen, lv_group_t *group, lv_obj_t *back,
                                   lv_obj_t *track, rogue_radar::KnownRadio radio,
                                   const char *name, const char *mac, int8_t rssi, uint8_t channel,
                                   uint32_t lastSeen, uint8_t addressType) {
    struct Selection { rogue_radar::KnownAddress address; rogue_radar::KnownRadio radio; };
    auto *selection = new (std::nothrow) Selection();
    if (!selection) return;
    selection->radio = radio;
    snprintf(selection->address.advertisedName, sizeof(selection->address.advertisedName), "%s", name);
    snprintf(selection->address.address, sizeof(selection->address.address), "%s", mac);
    selection->address.lastRssi = rssi; selection->address.channel = channel;
    selection->address.lastSeenUptimeMs = lastSeen;
    selection->address.addressType = addressType;
    lv_obj_set_width(back, 64);
    lv_obj_set_width(track, 116);
    lv_obj_t *save = createActionBtn(screen, "Save", [](lv_event_t *e) {
        auto *s = static_cast<Selection *>(lv_event_get_user_data(e));
        knownUiSaveCandidate(s->address, s->radio);
    }, selection);
    lv_obj_set_width(save, 82); lv_obj_align(save, LV_ALIGN_BOTTOM_LEFT, 108, -4);
    lv_group_add_obj(group, save);
    lv_obj_add_event_cb(save, [](lv_event_t *e) {
        delete static_cast<Selection *>(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, selection);
}
