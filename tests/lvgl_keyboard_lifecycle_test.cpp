#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "../rogue-radar/deferred_screen_delete.h"

static_assert(LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR == 0 && LVGL_VERSION_PATCH == 0,
              "lifecycle regression must run against the firmware's pinned LVGL 9.0.0");

static lv_display_t *disp;

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

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 2) return 64;
    if (std::strcmp(argv[1], "helper") == 0) return helperTest();
    if (std::strcmp(argv[1], "old") == 0) return memoryScenario(true);
    if (std::strcmp(argv[1], "fixed") == 0) return memoryScenario(false);
    return 64;
}
