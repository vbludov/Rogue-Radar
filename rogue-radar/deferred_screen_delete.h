#ifndef ROGUE_RADAR_DEFERRED_SCREEN_DELETE_H
#define ROGUE_RADAR_DEFERRED_SCREEN_DELETE_H

#include <lvgl.h>

#include <new>
#include <stdint.h>

namespace deferred_screen_delete_detail {

struct Request {
    lv_obj_t *screen;
    lv_timer_t *timer;
    Request *next;
};

static Request *requests = nullptr;
static const uint32_t kProtectedRetryMs = 50;

static void unlinkRequest(Request *request) {
    Request **link = &requests;
    while (*link && *link != request) {
        link = &(*link)->next;
    }
    if (*link == request) {
        *link = request->next;
    }
}

static void screenDeleted(lv_event_t *event) {
    Request *request = static_cast<Request *>(lv_event_get_user_data(event));
    if (!request) return;

    unlinkRequest(request);
    if (request->timer) {
        lv_timer_delete(request->timer);
        request->timer = nullptr;
    }
    delete request;
}

static bool screenIsProtected(lv_obj_t *screen) {
    lv_display_t *display = lv_obj_get_display(screen);
    if (!display) return false;

    if (screen == lv_display_get_screen_active(display)) return true;
    if (screen == lv_display_get_screen_prev(display)) return true;
    if (lv_anim_get(screen, nullptr)) return true;

    // LVGL 9.0 has no public accessor for the pending screen-to-load pointer.
    // The active/previous checks are repeated on every timer attempt, so a
    // screen remains queued until an animated transition releases it.
    return false;
}

static void deleteTimer(lv_timer_t *timer) {
    Request *request = static_cast<Request *>(lv_timer_get_user_data(timer));
    if (!request || request->timer != timer || !request->screen) return;

    if (screenIsProtected(request->screen)) {
        lv_timer_set_period(timer, kProtectedRetryMs);
        return;
    }

    // LV_EVENT_DELETE synchronously unlinks the request, cancels this timer,
    // and frees the request. Do not access request after this call.
    lv_obj_delete(request->screen);
}

}  // namespace deferred_screen_delete_detail

// Queue a screen for deletion after delayMs. Requests for different screens
// remain independent; queuing the same screen again restarts its existing
// timer. Active and transition-previous screens are retried instead of freed.
static void queueDeferredScreenDelete(lv_obj_t *screen, uint32_t delayMs) {
    if (!screen) return;

    using namespace deferred_screen_delete_detail;
    const uint32_t period = delayMs ? delayMs : 1;

    for (Request *request = requests; request; request = request->next) {
        if (request->screen == screen) {
            lv_timer_set_period(request->timer, period);
            lv_timer_reset(request->timer);
            return;
        }
    }

    Request *request = new (std::nothrow) Request{screen, nullptr, requests};
    if (!request) return;

    request->timer = lv_timer_create(deleteTimer, period, request);
    if (!request->timer) {
        delete request;
        return;
    }

    lv_obj_add_event_cb(screen, screenDeleted, LV_EVENT_DELETE, request);
    requests = request;
}

#endif  // ROGUE_RADAR_DEFERRED_SCREEN_DELETE_H
