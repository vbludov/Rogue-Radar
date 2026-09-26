#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <lvgl.h>

#define SCREEN_W 320
#define SCREEN_H 170
#define ENCODER_BTN 0
#define LOW 0
#define TC(value) lv_color_hex(0x101820)

#include "../rogue-radar/known_device_types.h"

using namespace rogue_radar;

static uint32_t nowMs;
static uint32_t millis() { return nowMs; }
static int digitalRead(int) { return 1; }
static void resetInactivityTimer() {}
static void *ps_malloc(size_t size) { return std::malloc(size); }
static void *ps_calloc(size_t count, size_t size) { return std::calloc(count, size); }

struct EspStub { size_t getFreeHeap() const { return 256 * 1024; } } ESP;
struct SerialStub { void println(const char *) {} } Serial;

static lv_display_t *lvDisp;
static lv_indev_t *lvIndev;
static bool keyboardActive;
static bool keyboardFinishPending;
static bool signalTrackerActive;

static void applyScreenStyle(lv_obj_t *screen) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
}
static void styleListBtn(lv_obj_t *) {}
static void createHeader(lv_obj_t *screen, const char *title) {
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, title);
}
static lv_obj_t *createBackBtn(lv_obj_t *screen, lv_event_cb_t callback) {
    lv_obj_t *button = lv_button_create(screen);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    return button;
}
static lv_obj_t *createActionBtn(lv_obj_t *screen, const char *text,
                                 lv_event_cb_t callback, void *data = nullptr) {
    lv_obj_t *button = lv_button_create(screen);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, data);
    return button;
}
static void deleteGroup(lv_group_t **group) {
    if (group && *group) { lv_group_delete(*group); *group = nullptr; }
}
static void setGroup(lv_group_t *group) { lv_indev_set_group(lvIndev, group); }
static void loadScreenWithoutSlide(lv_obj_t *screen) { lv_screen_load(screen); }

class FakeKnownDeviceStore {
 public:
    KnownDevice record{};

    KnownStoreStatus listPage(uint32_t cursor, KnownDeviceSummary out[6],
                              uint8_t &count, uint32_t &next) {
        count = 0; next = cursor;
        if (cursor || !record.id) return KnownStoreStatus::Ok;
        out[0].id = record.id;
        std::snprintf(out[0].name, sizeof(out[0].name), "%s", record.name);
        out[0].radio = record.radio;
        out[0].addressCount = record.addressCount;
        count = 1;
        return KnownStoreStatus::Ok;
    }
    KnownStoreStatus read(uint32_t id, KnownDevice &out) {
        if (!record.id || id != record.id) return KnownStoreStatus::NotFound;
        out = record; return KnownStoreStatus::Ok;
    }
    KnownStoreStatus create(KnownDevice &device) {
        device.id = 1; record = device; return KnownStoreStatus::Ok;
    }
    KnownStoreStatus update(const KnownDevice &device) {
        if (device.id != record.id) return KnownStoreStatus::NotFound;
        record = device; return KnownStoreStatus::Ok;
    }
    KnownStoreStatus renameDevice(uint32_t id, const char *name) {
        if (id != record.id) return KnownStoreStatus::NotFound;
        std::snprintf(record.name, sizeof(record.name), "%s", name);
        return KnownStoreStatus::Ok;
    }
    KnownStoreStatus deleteDevice(uint32_t id) {
        if (id != record.id) return KnownStoreStatus::NotFound;
        record = KnownDevice{}; return KnownStoreStatus::Ok;
    }
    KnownStoreStatus addAddress(uint32_t id, const KnownAddress &address) {
        if (id != record.id || record.addressCount >= 8) return KnownStoreStatus::InvalidArgument;
        record.addresses[record.addressCount++] = address;
        return KnownStoreStatus::Ok;
    }
    KnownStoreStatus removeAddress(uint32_t id, uint8_t index) {
        if (id != record.id || index >= record.addressCount || record.addressCount <= 1)
            return KnownStoreStatus::InvalidArgument;
        for (uint8_t i = index + 1; i < record.addressCount; ++i)
            record.addresses[i - 1] = record.addresses[i];
        --record.addressCount;
        return KnownStoreStatus::Ok;
    }
};

static FakeKnownDeviceStore fakeStore;
static FakeKnownDeviceStore &knownDeviceStore() { return fakeStore; }
static bool knownDeviceStorageReady() { return true; }
static bool knownDeviceStorageRetry() { return true; }
static bool knownUiIsSaved(KnownRadio, const KnownAddress &, char *, size_t);

struct KnownDiscoveryObservation { KnownRadio radio; KnownAddress address; };
static bool knownDiscoveryStart(const char *, KnownRadio) { return true; }
static void knownDiscoveryPoll(const char *) {}
static bool knownDiscoveryStop(const char *) { return true; }
static bool knownDiscoveryTake(const char *, KnownDiscoveryObservation &) { return false; }
static const char *knownDiscoveryStatus() { return "Idle"; }

struct TrackerModelStub {
    bool current(uint32_t, int16_t &, float &) const { return false; }
} trackerModel;
static void createSignalTracker(bool, const char *, const char *, uint8_t, uint8_t) {
    signalTrackerActive = true;
}

typedef void (*KeyboardDone)(const char *, bool);
static KeyboardDone keyboardCallback;
static lv_obj_t *keyboardScreen;
static void createKeyboardScreen(const char *title, const char *, size_t, KeyboardDone done) {
    keyboardCallback = done;
    keyboardActive = true;
    keyboardScreen = lv_obj_create(nullptr);
    lv_obj_t *label = lv_label_create(keyboardScreen);
    lv_label_set_text(label, title);
    lv_screen_load(keyboardScreen);
}

static lv_obj_t *nearbyScreen;
static lv_group_t *nearbyGroup;
static bool nearbySignalsActive() { return nearbyScreen != nullptr; }
static void createNearbySignalsForAssociation(KnownRadio) {
    nearbyScreen = lv_obj_create(nullptr);
    nearbyGroup = lv_group_create();
    lv_screen_load_anim(nearbyScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 0, false);
}

#include "../rogue-radar/known_devices_ui.h"

static void step(uint32_t duration) {
    while (duration) {
        const uint32_t amount = duration > 5 ? 5 : duration;
        nowMs += amount;
        lv_tick_inc(amount);
        lv_timer_handler();
        duration -= amount;
    }
}
static void settleAndProcess() { step(250); processKnownDevices(); }

static void finishKeyboardTo(lv_obj_t *returnScreen, const char *text, bool accepted) {
    assert(keyboardActive && keyboardCallback && keyboardScreen);
    keyboardCallback(text, accepted);
    keyboardActive = false;
    keyboardFinishPending = false;
    assert(lv_screen_active() == returnScreen);
    lv_obj_delete(keyboardScreen);
    keyboardScreen = nullptr;
    step(150);
}

int main() {
    (void)&knownUiTrackCandidate;
    (void)&knownUiIsSaved;
    (void)&knownInstallSaveButton;
    lv_init();
    lvDisp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_default(lvDisp);
    lv_timer_pause(lv_display_get_refr_timer(lvDisp));
    lvIndev = lv_indev_create();
    lv_indev_set_type(lvIndev, LV_INDEV_TYPE_ENCODER);

    lv_obj_t *home = lv_screen_active();
    lv_group_t *homeGroup = lv_group_create();
    setGroup(homeGroup);
    fakeStore.record.id = 1;
    std::strcpy(fakeStore.record.name, "My tag");
    fakeStore.record.radio = KnownRadio::Ble;
    fakeStore.record.addressCount = 1;
    std::strcpy(fakeStore.record.addresses[0].address, "AA:BB:CC:DD:EE:01");
    fakeStore.record.addresses[0].addressType = 1;

    createSavedDevices();
    settleAndProcess();
    assert(knownDevicesActive() && known_devices_ui::depth == 0);
    assert(known_devices_ui::current()->page == known_devices_ui::Action::List);

    known_devices_ui::request(known_devices_ui::Action::Pick, 0);
    processKnownDevices();
    assert(known_devices_ui::current()->page == known_devices_ui::Action::Detail);
    assert(lv_group_get_obj_count(known_devices_ui::current()->group) == 6);

    known_devices_ui::request(known_devices_ui::Action::UpdateNearby);
    processKnownDevices();
    step(150);
    processKnownDevices();
    assert(lv_screen_active() == nearbyScreen);
    assert(known_devices_ui::current()->wasAway);

    KnownAddress candidate{};
    std::strcpy(candidate.address, "AA:BB:CC:DD:EE:02");
    std::strcpy(candidate.advertisedName, "Rotated tag");
    candidate.addressType = 1;
    candidate.lastRssi = -42;
    knownUiAssociateCandidate(candidate, KnownRadio::Ble);
    settleAndProcess();
    assert(known_devices_ui::depth == 1);
    assert(known_devices_ui::current()->page == known_devices_ui::Action::AssociateConfirm);

    known_devices_ui::request(known_devices_ui::Action::Associate);
    processKnownDevices();
    assert(fakeStore.record.addressCount == 2);
    assert(known_devices_ui::current()->page == known_devices_ui::Action::Associate);

    known_devices_ui::request(known_devices_ui::Action::Back);
    processKnownDevices();
    step(250);
    assert(known_devices_ui::depth == 0 && lv_screen_active() == nearbyScreen);

    lv_obj_t *savedScreen = known_devices_ui::current()->screen;
    lv_group_delete(nearbyGroup); nearbyGroup = nullptr;
    lv_screen_load_anim(savedScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 100, 0, true);
    nearbyScreen = nullptr;
    step(150);
    processKnownDevices();
    assert(known_devices_ui::current()->page == known_devices_ui::Action::Detail);
    assert(known_devices_ui::current()->record.addressCount == 2);

    known_devices_ui::request(known_devices_ui::Action::Rename);
    processKnownDevices();
    assert(keyboardActive && lv_screen_active() == keyboardScreen);
    finishKeyboardTo(savedScreen, "", false);
    processKnownDevices();
    assert(known_devices_ui::current()->page == known_devices_ui::Action::Detail);
    assert(std::strcmp(fakeStore.record.name, "My tag") == 0);

    known_devices_ui::request(known_devices_ui::Action::Rename);
    processKnownDevices();
    assert(keyboardActive && lv_screen_active() == keyboardScreen);
    finishKeyboardTo(savedScreen, "Renamed tag", true);
    processKnownDevices();
    assert(std::strcmp(fakeStore.record.name, "Renamed tag") == 0);
    assert(known_devices_ui::current()->page == known_devices_ui::Action::Detail);

    known_devices_ui::request(known_devices_ui::Action::Back);
    processKnownDevices();
    assert(known_devices_ui::current()->page == known_devices_ui::Action::List);
    known_devices_ui::request(known_devices_ui::Action::Back);
    processKnownDevices();
    step(250);
    assert(!knownDevicesActive() && lv_screen_active() == home);
    assert(lv_indev_get_group(lvIndev) == homeGroup);

    // A Saved detail nested under Nearby cannot open another Nearby instance.
    // It must send the user back to the already-live parent instead.
    nearbyScreen = lv_obj_create(nullptr);
    nearbyGroup = lv_group_create();
    lv_screen_load(nearbyScreen);
    known_devices_ui::Context *nested = known_devices_ui::begin();
    assert(nested);
    step(250);
    nested->record = fakeStore.record;
    known_devices_ui::detail(*nested);
    assert(lv_group_get_obj_count(nested->group) == 5);
    known_devices_ui::close(*nested);
    step(250);
    assert(lv_screen_active() == nearbyScreen && !knownDevicesActive());
    lv_group_delete(nearbyGroup); nearbyGroup = nullptr;

    lv_mem_monitor_t memory{};
    lv_mem_monitor(&memory);
    assert(memory.free_size > 8192);
    std::printf("PASS known devices nested association, keyboard cancel/OK, Back lifecycle (%u bytes free)\n",
                static_cast<unsigned>(memory.free_size));
}
