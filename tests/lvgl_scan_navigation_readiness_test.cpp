#include <lvgl.h>

#include <cstdio>
#include <cstdlib>

static_assert(LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR == 0 &&
                  LVGL_VERSION_PATCH == 0,
              "navigation regression must use firmware-pinned LVGL 9.0.0");

namespace {
int checks;
int failures;
lv_display_t *display;
lv_obj_t *familyMenu;

void check(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

void tick(uint32_t milliseconds) {
    for (uint32_t elapsed = 0; elapsed < milliseconds; elapsed += 5) {
        lv_tick_inc(5);
        lv_timer_handler();
    }
}

bool transitionReady(bool sessionAttached, lv_obj_t *expectedFamily) {
    return !sessionAttached && expectedFamily != nullptr &&
           lv_screen_active() == expectedFamily &&
           lv_display_get_screen_prev(display) == nullptr;
}

void createFamilyAfterBack(lv_timer_t *timer) {
    lv_timer_delete(timer);
    familyMenu = lv_obj_create(nullptr);
    lv_obj_t *title = lv_label_create(familyMenu);
    lv_label_set_text(title, "WiFi Tools");
    lv_screen_load_anim(familyMenu, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 250, 0, true);
}

void runDelayedBackLifecycle() {
    lv_init();
    display = lv_display_create(320, 170);
    check(display != nullptr, "display created");
    lv_display_set_default(display);
    lv_timer_pause(lv_display_get_refr_timer(display));

    lv_obj_t *home = lv_screen_active();
    lv_obj_t *tool = lv_obj_create(nullptr);
    lv_obj_t *title = lv_label_create(tool);
    lv_label_set_text(title, "Flock Hybrid");
    lv_screen_load(tool);

    // cb_wifiToolBack detaches the scan session first, but rebuilds and loads
    // the family menu from a 75 ms timer after the LVGL event unwinds.
    bool sessionAttached = false;
    familyMenu = nullptr;
    lv_timer_create(createFamilyAfterBack, 75, nullptr);

    // This was the old diagnostic predicate. It incorrectly allowed the next
    // page to delete/reuse the still-active tool before the timer ran.
    const bool oldPredicate = !sessionAttached &&
                              lv_display_get_screen_prev(display) == nullptr;
    check(oldPredicate, "old predicate reproduces premature readiness");
    check(lv_screen_active() == tool, "old tool remains active before deferred callback");
    check(!transitionReady(sessionAttached, familyMenu),
          "expected-family predicate blocks before timer callback");

    tick(70);
    check(familyMenu == nullptr && lv_screen_active() == tool,
          "deferred family transition has not started early");
    check(!transitionReady(sessionAttached, familyMenu),
          "readiness remains false before timer deadline");

    tick(10);
    check(familyMenu != nullptr && lv_screen_active() == familyMenu,
          "timer creates and activates expected family menu");
    check(lv_display_get_screen_prev(display) == tool,
          "outgoing tool remains previous during animation");
    check(!transitionReady(sessionAttached, familyMenu),
          "readiness remains false during animation");

    tick(400);
    check(lv_screen_active() == familyMenu,
          "expected family remains active after animation");
    check(lv_display_get_screen_prev(display) == nullptr,
          "animation releases previous-screen slot");
    check(transitionReady(sessionAttached, familyMenu),
          "readiness becomes true only after expected family settles");

    // Opening the next page at this boundary must preserve the family menu and
    // make the newly-created page active.
    lv_obj_t *nextTool = lv_obj_create(nullptr);
    lv_screen_load_anim(nextTool, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
    tick(400);
    check(lv_screen_active() == nextTool &&
              lv_display_get_screen_prev(display) == nullptr,
          "next page opens after the safe readiness boundary");

    lv_screen_load(home);
    lv_obj_delete(nextTool);
    lv_obj_delete(familyMenu);
    familyMenu = nullptr;
}
}  // namespace

int main() {
    runDelayedBackLifecycle();
    if (failures) {
        std::fprintf(stderr, "%d of %d scan-navigation checks failed\n",
                     failures, checks);
        return EXIT_FAILURE;
    }
    std::printf("PASS: %d delayed scan-navigation readiness checks\n", checks);
    return EXIT_SUCCESS;
}
