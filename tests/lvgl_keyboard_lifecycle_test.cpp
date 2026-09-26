#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "../rogue-radar/deferred_screen_delete.h"

static_assert(LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR == 0 && LVGL_VERSION_PATCH == 0,
              "lifecycle regression must run against the firmware's pinned LVGL 9.0.0");

static lv_display_t *disp;
static lv_indev_t *encoder;
static int32_t encoderDiff;
static lv_indev_state_t encoderState = LV_INDEV_STATE_RELEASED;
static void step(uint32_t total, uint32_t quantum);

static void encoderRead(lv_indev_t *, lv_indev_data_t *data) {
    data->enc_diff = encoderDiff;
    data->state = encoderState;
    encoderDiff = 0;
}

static void driveEncoder(int32_t diff, lv_indev_state_t state) {
    encoderDiff = diff;
    encoderState = state;
    step(40, 40);
}

static void step(uint32_t total, uint32_t quantum = 1) {
    while (total) {
        uint32_t n = total < quantum ? total : quantum;
        lv_tick_inc(n);
        lv_timer_handler();
        total -= n;
    }
}

static void deleted(lv_event_t *e) {
    int *count = static_cast<int *>(lv_event_get_user_data(e));
    ++*count;
}

static void check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(2);
    }
}

static void init() {
    lv_init();
    disp = lv_display_create(320, 170);
    check(disp != nullptr, "display allocation");
    lv_display_set_default(disp);
    lv_timer_pause(lv_display_get_refr_timer(disp));
    encoder = lv_indev_create();
    lv_indev_set_type(encoder, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(encoder, encoderRead);
}

static void memory(const char *label) {
    lv_mem_monitor_t m{};
    lv_mem_monitor(&m);
    std::printf("%s: free=%u largest=%u used=%u%% frag=%u%%\n", label,
                (unsigned)m.free_size, (unsigned)m.free_biggest_size,
                m.used_pct, m.frag_pct);
    std::fflush(stdout);
}

static int helperTest() {
    init();
    int prevDeleted = 0, independentDeleted = 0;
    lv_obj_t *previous = lv_screen_active();
    lv_obj_add_event_cb(previous, deleted, LV_EVENT_DELETE, &prevDeleted);
    lv_obj_t *next = lv_obj_create(nullptr);
    lv_obj_t *independent = lv_obj_create(nullptr);
    lv_obj_add_event_cb(independent, deleted, LV_EVENT_DELETE, &independentDeleted);

    lv_screen_load_anim(next, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
    step(40);
    check(lv_display_get_screen_active(disp) == next, "new screen active");
    check(lv_display_get_screen_prev(disp) == previous, "old screen is transition previous");

    queueDeferredScreenDelete(previous, 1);
    queueDeferredScreenDelete(independent, 1);
    step(2);
    check(independentDeleted == 1, "independent queued screen deleted");
    check(prevDeleted == 0, "transition previous screen retained");

    step(300, 5);
    check(prevDeleted == 1, "transition previous screen deleted after release");
    check(lv_display_get_screen_prev(disp) == nullptr, "transition completed cleanly");

    int scheduledDeleted = 0;
    lv_obj_t *scheduled = lv_obj_create(nullptr);
    lv_obj_add_event_cb(scheduled, deleted, LV_EVENT_DELETE, &scheduledDeleted);
    lv_screen_load_anim(scheduled, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 100, false);
    queueDeferredScreenDelete(scheduled, 1);
    step(50, 5);
    check(scheduledDeleted == 0, "delayed animation target retained");
    step(200, 5);
    check(scheduledDeleted == 0 && lv_screen_active() == scheduled,
          "animation target retained while active");
    lv_obj_t *finalScreen = lv_obj_create(nullptr);
    lv_screen_load(finalScreen);
    step(60, 5);
    check(scheduledDeleted == 1, "formerly active target deleted after release");

    int duplicateDeleted = 0;
    lv_obj_t *duplicate = lv_obj_create(nullptr);
    lv_obj_add_event_cb(duplicate, deleted, LV_EVENT_DELETE, &duplicateDeleted);
    queueDeferredScreenDelete(duplicate, 100);
    queueDeferredScreenDelete(duplicate, 1);
    step(2);
    check(duplicateDeleted == 1, "duplicate request coalesced/reset");

    int externalDeleted = 0, reusedDeleted = 0;
    lv_obj_t *external = lv_obj_create(nullptr);
    void *oldAddress = external;
    lv_obj_add_event_cb(external, deleted, LV_EVENT_DELETE, &externalDeleted);
    queueDeferredScreenDelete(external, 100);
    lv_obj_delete(external);
    check(externalDeleted == 1, "external deletion observed");

    lv_obj_t *reused = nullptr;
    lv_obj_t *candidates[32]{};
    int candidateCount = 0;
    for (; candidateCount < 32; ++candidateCount) {
        candidates[candidateCount] = lv_obj_create(nullptr);
        if (candidates[candidateCount] == oldAddress) {
            reused = candidates[candidateCount];
            ++candidateCount;
            break;
        }
    }
    if (reused) {
        lv_obj_add_event_cb(reused, deleted, LV_EVENT_DELETE, &reusedDeleted);
        step(150, 5);
        check(reusedDeleted == 0, "stale external-delete timer cannot delete reused address");
        queueDeferredScreenDelete(reused, 1);
        step(2);
        check(reusedDeleted == 1, "reused address gets fresh request");
    }
    for (int i = 0; i < candidateCount; ++i) {
        if (candidates[i] && candidates[i] != reused) lv_obj_delete(candidates[i]);
    }

    queueDeferredScreenDelete(nullptr, 0);
    lv_obj_t *earlyAp = lv_obj_create(nullptr);
    lv_obj_t *earlyKeyboard = lv_obj_create(nullptr);
    lv_screen_load_anim(earlyAp, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
    step(50, 5);
    loadScreenWithoutSlide(earlyKeyboard);
    step(400, 5);
    check(lv_screen_active() == earlyKeyboard, "keyboard interrupts incoming AP screen");
    check(lv_display_get_screen_prev(disp) == nullptr,
          "early keyboard load cannot leave Back blocked by stale transition state");
    lv_obj_delete(earlyAp);
    // Idle-home cleanup must clear the active menu reference even though LVGL
    // owns its eventual deletion. Otherwise re-entry deletes a freed menu.
    lv_obj_t *home = lv_screen_active();
    lv_obj_t *ownedMenu = lv_obj_create(nullptr);
    lv_obj_t *inactiveMenu = lv_obj_create(nullptr);
    int activeRetired = 0, inactiveRetired = 0;
    lv_obj_add_event_cb(ownedMenu, deleted, LV_EVENT_DELETE, &activeRetired);
    lv_obj_add_event_cb(inactiveMenu, deleted, LV_EVENT_DELETE, &inactiveRetired);
    lv_screen_load(ownedMenu);
    lv_obj_t *activeMenu = ownedMenu;
    releaseScreenForHome(ownedMenu, activeMenu, home);
    releaseScreenForHome(inactiveMenu, activeMenu, home);
    check(!ownedMenu && !inactiveMenu, "home cleanup drops active and inactive references");
    check(activeRetired == 0, "home animation retains active screen until transition ends");
    lv_screen_load_anim(home, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, true);
    step(350, 5);
    check(activeRetired == 1 && inactiveRetired == 1, "home cleanup deletes both screens once");
    releaseScreenForHome(ownedMenu, home, home);  // Safe after prior auto-deletion.
    ownedMenu = lv_obj_create(nullptr);
    lv_screen_load(ownedMenu);
    check(lv_screen_active() == ownedMenu, "menu recreation after idle return succeeds");
    lv_obj_t *homeReference = home;
    releaseScreenForHome(homeReference, ownedMenu, home);
    check(homeReference == home, "home itself is never retired");
    std::printf("PASS helper lifecycle (address_reused=%s)\n", reused ? "yes" : "no");
    return 0;
}

static lv_obj_t *makeApList(unsigned rows) {
    lv_obj_t *screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101010), LV_PART_MAIN);
    lv_obj_t *header = lv_label_create(screen);
    lv_label_set_text(header, "WiFi  Connect to AP");
    lv_obj_t *status = lv_label_create(screen);
    lv_label_set_text(status, "Press Scan, select AP, then enter password.");
    lv_obj_set_width(status, 306);
    lv_obj_t *list = lv_list_create(screen);
    lv_obj_set_size(list, 320, 90);
    for (unsigned i = 0; i < rows; ++i) {
        char text[96];
        std::snprintf(text, sizeof(text), "Network-%02u-with-a-realistic-name   -%u dBm  WPA2", i, 30 + i);
        lv_obj_t *button = lv_list_add_button(list, nullptr, text);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x202020), LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    }
    for (const char *text : {"Back", "Disconnect", "Scan"}) {
        lv_obj_t *button = lv_button_create(screen);
        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, text);
    }
    return screen;
}

static lv_obj_t *makeKeyboard() {
    lv_obj_t *screen = lv_obj_create(nullptr);
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Password: Network-00-with-a-realistic-name");
    lv_obj_t *text = lv_label_create(screen);
    lv_label_set_text(text, "***************");
    static const char *map[] = {"A","B","C","D","E","F","G","H","I","J","\n",
                                "K","L","M","N","O","P","Q","R","S","T","\n",
                                "U","V","W","X","Y","Z","1","2","3","4","\n",
                                "5","6","7","8","9","0","OK","Esc",""};
    lv_obj_t *matrix = lv_buttonmatrix_create(screen);
    lv_buttonmatrix_set_map(matrix, map);
    lv_obj_set_size(matrix, 312, 120);
    return screen;
}

static int memoryScenario(bool rebuild) {
    init();
    lv_obj_t *list = makeApList(30);
    lv_screen_load(list);
    memory("30-row list");
    lv_obj_t *keyboard = makeKeyboard();
    lv_screen_load(keyboard);
    memory("list + keyboard retained");
    if (rebuild) {
        // Original cancel callback called createConnectAPTool(), allocating a
        // second complete list before either retained screen was reclaimed.
        (void)makeApList(30);
        memory("original cancel rebuilt second list");
    } else {
        // Corrected cb_connectApPasswordDone(false) reuses wifiToolScreen.
        lv_screen_load_anim(list, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, false);
        step(40);
        memory("fixed cancel reused retained list");
        check(lv_display_get_screen_active(disp) == list, "fixed cancel returns to retained list");
    }
    return 0;
}

static const char *exactKeyboardMap[] = {
    "OK", "caps", "Del", "Space", "Esc", "\n",
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "[", "]", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "\\", "\n",
    "z", "x", "c", "v", "b", "n", "m", ",", ".", "/", "@", ".", ""
};

static lv_obj_t *eventApScreen;
static lv_group_t *eventApGroup;
static lv_obj_t *eventKeyboardScreen;
static lv_group_t *eventKeyboardGroup;
static lv_obj_t *eventKeyboardMatrix;
static bool eventKeyboardOpened;
static bool eventFinishPending;

static void requestEventFinish() {
    if (eventFinishPending) return;
    eventFinishPending = true;
    lv_obj_add_state(eventKeyboardMatrix, LV_STATE_DISABLED);
}

static void eventKeyboardValueChanged(lv_event_t *event) {
    lv_obj_t *matrix = static_cast<lv_obj_t *>(lv_event_get_target(event));
    uint32_t selected = lv_buttonmatrix_get_selected_button(matrix);
    const char *text = lv_buttonmatrix_get_button_text(matrix, selected);
    if (text && std::strcmp(text, "Esc") == 0) requestEventFinish();
}

static void openEventKeyboard() {
    eventKeyboardOpened = true;
    eventKeyboardScreen = lv_obj_create(nullptr);
    lv_obj_t *box = lv_obj_create(eventKeyboardScreen);
    lv_obj_set_size(box, 310, 28);
    lv_obj_set_pos(box, 5, 30);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *text = lv_label_create(box);
    lv_label_set_text(text, "_");
    lv_obj_t *count = lv_label_create(box);
    lv_label_set_text(count, "0/64");
    eventKeyboardMatrix = lv_buttonmatrix_create(eventKeyboardScreen);
    lv_buttonmatrix_set_map(eventKeyboardMatrix, exactKeyboardMap);
    lv_obj_set_size(eventKeyboardMatrix, 312, 106);
    lv_obj_set_pos(eventKeyboardMatrix, 4, 62);
    lv_obj_clear_flag(eventKeyboardMatrix, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(eventKeyboardMatrix, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_set_scrollbar_mode(eventKeyboardMatrix, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_anim_duration(eventKeyboardMatrix, 0, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(eventKeyboardMatrix, 0, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(eventKeyboardMatrix, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_row(eventKeyboardMatrix, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_column(eventKeyboardMatrix, 1, LV_PART_MAIN);
    lv_obj_set_style_border_width(eventKeyboardMatrix, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(eventKeyboardMatrix, 3, LV_PART_ITEMS);
    lv_obj_add_event_cb(eventKeyboardMatrix, eventKeyboardValueChanged,
                        LV_EVENT_VALUE_CHANGED, nullptr);

    eventKeyboardGroup = lv_group_create();
    lv_group_add_obj(eventKeyboardGroup, eventKeyboardMatrix);
    lv_indev_set_group(encoder, eventKeyboardGroup);
    lv_group_set_editing(eventKeyboardGroup, true);
    loadScreenWithoutSlide(eventKeyboardScreen);
}

static void apRowClicked(lv_event_t *) {
    // This matches connectApSelect(): keyboard construction, group handoff and
    // direct screen load all happen inside the encoder's CLICKED event.
    openEventKeyboard();
}

static void runInputEventCancel(bool topBack) {
    eventKeyboardOpened = false;
    eventFinishPending = false;
    eventApScreen = makeApList(30);
    eventApGroup = lv_group_create();
    lv_obj_t *apRow = lv_button_create(eventApScreen);
    lv_obj_add_event_cb(apRow, apRowClicked, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(eventApGroup, apRow);
    lv_indev_set_group(encoder, eventApGroup);
    lv_group_focus_obj(apRow);
    lv_screen_load(eventApScreen);

    driveEncoder(0, LV_INDEV_STATE_PRESSED);
    driveEncoder(0, LV_INDEV_STATE_RELEASED);
    check(eventKeyboardOpened, "AP row encoder click opened keyboard inside input event");
    check(lv_indev_get_group(encoder) == eventKeyboardGroup,
          "encoder switched to keyboard group inside AP click");
    check(lv_group_get_editing(eventKeyboardGroup), "keyboard group is editing");
    step(80, 5);  // Allow the one-ms load to finish on LVGL's animation tick.

    if (topBack) {
        // The physical shortcut invokes keyboardRequestFinish(false) outside
        // LVGL's input callback.
        requestEventFinish();
    } else {
        // Rotate from OK to Esc and press it through the real encoder indev.
        driveEncoder(4, LV_INDEV_STATE_RELEASED);
        check(lv_buttonmatrix_get_selected_button(eventKeyboardMatrix) == 4,
              "encoder focused Esc key");
        driveEncoder(0, LV_INDEV_STATE_PRESSED);
        driveEncoder(0, LV_INDEV_STATE_RELEASED);
    }
    check(eventFinishPending, "cancel request queued");
    check(lv_obj_has_state(eventKeyboardMatrix, LV_STATE_DISABLED),
          "cancel request disabled matrix");

    // processKeyboardDeferredFinish invokes this later from loop(), outside
    // lv_timer_handler and after the encoder release.
    lv_indev_set_group(encoder, nullptr);
    lv_group_delete(eventKeyboardGroup);
    eventKeyboardGroup = nullptr;

    // cb_connectApPasswordDone(false) restores the retained AP group and starts
    // the animated return before keyboardFinish queues the keyboard deletion.
    lv_indev_set_group(encoder, eventApGroup);
    lv_screen_load_anim(eventApScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, false);
    queueDeferredScreenDelete(eventKeyboardScreen, 450);
    step(550, 5);
    check(lv_screen_active() == eventApScreen, "cancel returned to retained AP screen");
    check(lv_display_get_screen_prev(disp) == nullptr, "cancel transition released previous screen");
    check(lv_indev_get_group(encoder) == eventApGroup, "encoder attached to restored AP group");

    lv_indev_set_group(encoder, nullptr);
    lv_group_delete(eventApGroup);
    eventApGroup = nullptr;
    lv_obj_t *blank = lv_obj_create(nullptr);
    lv_screen_load(blank);
    lv_obj_delete(eventApScreen);
    std::printf("PASS input-event %s cancel lifecycle\n", topBack ? "top-Back" : "Esc");
}

static int exactLifecycleTest() {
    init();
    runInputEventCancel(false);
    runInputEventCancel(true);
    return 0;
}

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 2) return 64;
    if (std::strcmp(argv[1], "helper") == 0) return helperTest();
    if (std::strcmp(argv[1], "exact") == 0) return exactLifecycleTest();
    if (std::strcmp(argv[1], "old") == 0) return memoryScenario(true);
    if (std::strcmp(argv[1], "fixed") == 0) return memoryScenario(false);
    return 64;
}
