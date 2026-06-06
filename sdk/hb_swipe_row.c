/*
 * hb_swipe_row.c — iOS-style swipe-to-reveal row implementation.
 *
 * Gesture state machine:
 *   - PRESSED: record start point, mode = UNDECIDED.
 *   - PRESSING: classify the gesture based on the dominant axis:
 *       * vertical motion → SCROLL: ignore (let the parent list
 *         scroll vertically); never translate the body.
 *       * horizontal motion → SWIPE: translate the body by dx,
 *         clamped to [-action_w, 0].
 *     Once classified the mode is sticky for the rest of the gesture.
 *   - RELEASED: if mode == SWIPE, snap to revealed/closed based on
 *     past-halfway. We do NOT synthesize tap-to-open here — LVGL
 *     fires CLICKED on its own when the touch was tap-like (small
 *     dx AND small dy). Otherwise we'd also fire on a vertical
 *     scroll's release.
 *   - CLICKED (LVGL native): if currently revealed, snap closed;
 *     otherwise fire the caller's on_tap.
 *
 * Only one row may be revealed at a time. Starting a gesture on a
 * different row first closes any other revealed body.
 */

#include "hb_swipe_row.h"
#include "hb_sdk.h"
#include "hb_confirm.h"

#define SWIPE_THRESH   8        /* px of motion before classifying */

typedef enum {
    MODE_UNDECIDED = 0,
    MODE_SWIPE,
    MODE_SCROLL,
} gesture_mode_t;

typedef struct {
    lv_obj_t *body;
    lv_obj_t *action_btn;       /* hidden while body fully covers it */
    int       action_w;
    int       start_x;
    int       start_y;
    int       translation_at_press;  /* body x when the press began */
    gesture_mode_t mode;
    hb_swipe_cb_t on_tap;
    hb_swipe_cb_t on_action;
    void     *cookie;
} swipe_state_t;

static lv_obj_t *s_revealed_body = NULL;

static void snap_to(swipe_state_t *st, int x)
{
    lv_obj_set_x(st->body, x);
    if (x != 0) {
        lv_obj_clear_flag(st->action_btn, LV_OBJ_FLAG_HIDDEN);
        if (s_revealed_body && s_revealed_body != st->body) {
            swipe_state_t *other = (swipe_state_t *)lv_obj_get_user_data(s_revealed_body);
            lv_obj_set_x(s_revealed_body, 0);
            if (other) lv_obj_add_flag(other->action_btn, LV_OBJ_FLAG_HIDDEN);
        }
        s_revealed_body = st->body;
    } else {
        lv_obj_add_flag(st->action_btn, LV_OBJ_FLAG_HIDDEN);
        if (s_revealed_body == st->body) s_revealed_body = NULL;
    }
}

/* Current body x (so we don't need to track translation separately). */
static int body_x(swipe_state_t *st)
{
    return lv_obj_get_x(st->body);
}

static void on_press(lv_event_t *e)
{
    swipe_state_t *st = (swipe_state_t *)lv_event_get_user_data(e);
    lv_indev_t *ind = lv_indev_active();
    if (!ind) return;
    lv_point_t p; lv_indev_get_point(ind, &p);
    st->start_x = p.x;
    st->start_y = p.y;
    st->translation_at_press = body_x(st);
    st->mode = MODE_UNDECIDED;
}

static void on_pressing(lv_event_t *e)
{
    swipe_state_t *st = (swipe_state_t *)lv_event_get_user_data(e);
    lv_indev_t *ind = lv_indev_active();
    if (!ind) return;
    lv_point_t p; lv_indev_get_point(ind, &p);
    int dx = p.x - st->start_x;
    int dy = p.y - st->start_y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;

    if (st->mode == MODE_UNDECIDED) {
        if (ady > SWIPE_THRESH && ady > adx) {
            st->mode = MODE_SCROLL;     /* let the list scroll */
            return;
        }
        if (adx > SWIPE_THRESH && adx >= ady) {
            st->mode = MODE_SWIPE;
            /* fall through to translate */
        } else {
            return;                     /* not yet classified */
        }
    }
    if (st->mode != MODE_SWIPE) return;

    int t = st->translation_at_press + dx;
    if (t > 0) t = 0;
    if (t < -st->action_w) t = -st->action_w;
    lv_obj_set_x(st->body, t);
}

static void on_release(lv_event_t *e)
{
    swipe_state_t *st = (swipe_state_t *)lv_event_get_user_data(e);
    if (st->mode != MODE_SWIPE) return;     /* let CLICKED handler decide */
    int t = body_x(st);
    /* Past-halfway → reveal, else close. */
    if (t <= -st->action_w / 2) snap_to(st, -st->action_w);
    else                        snap_to(st, 0);
}

/* LVGL fires CLICKED on its own when the touch was tap-like (small
   dx AND small dy). We use it to either open the row (on_tap) or
   close a revealed row. We do NOT fire it during a drag — LVGL's
   own click detector skips CLICKED when the touch moved past its
   click-threshold. */
static void on_body_click(lv_event_t *e)
{
    swipe_state_t *st = (swipe_state_t *)lv_event_get_user_data(e);
    if (st->mode == MODE_SWIPE) return;     /* drag, not a tap */
    if (body_x(st) != 0) {
        snap_to(st, 0);
        return;
    }
    if (st->on_tap) st->on_tap(st->cookie);
}

static void run_action_now(void *cookie)
{
    swipe_state_t *st = (swipe_state_t *)cookie;
    if (st->on_action) st->on_action(st->cookie);
    snap_to(st, 0);
}

static void on_action_click(lv_event_t *e)
{
    swipe_state_t *st = (swipe_state_t *)lv_event_get_user_data(e);
    /* Route every swipe-row action through a confirmation modal.
     * Apps using this helper are almost always doing destructive
     * deletes; one extra tap is a small price to prevent fat-finger
     * data loss. */
    hb_confirm_show("Delete?", "This can't be undone.", "Delete",
                    run_action_now, st);
}

lv_obj_t *hb_swipe_row_create(lv_obj_t *parent,
                              int row_w, int row_h, int action_w,
                              const char *action_label, uint32_t action_bg,
                              hb_swipe_cb_t on_tap,
                              hb_swipe_cb_t on_action,
                              void *cookie)
{
    swipe_state_t *st = lv_malloc(sizeof *st);
    if (!st) return NULL;
    st->action_w = action_w;
    st->start_x = 0;
    st->start_y = 0;
    st->translation_at_press = 0;
    st->mode = MODE_UNDECIDED;
    st->on_tap = on_tap;
    st->on_action = on_action;
    st->cookie = cookie;

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, row_w, row_h);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_margin_bottom(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *act = lv_button_create(row);
    lv_obj_set_size(act, action_w, row_h);
    lv_obj_set_pos(act, row_w - action_w, 0);
    lv_obj_set_style_bg_color(act, lv_color_hex(action_bg), 0);
    lv_obj_set_style_radius(act, 8, 0);
    lv_obj_set_style_pad_all(act, 0, 0);
    lv_obj_set_style_shadow_width(act, 0, 0);
    lv_obj_add_event_cb(act, on_action_click, LV_EVENT_CLICKED, st);
    /* Hidden by default so its red corner doesn't peek through the
       body's rounded corners when the row is closed. snap_to() makes
       it visible whenever the body translates left, and hides it
       again on snap-back-to-zero. */
    lv_obj_add_flag(act, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *al = lv_label_create(act);
    lv_label_set_text(al, action_label);
    lv_obj_set_style_text_color(al, lv_color_hex(0xffffff), 0);
    lv_obj_center(al);

    lv_obj_t *body = lv_obj_create(row);
    lv_obj_set_size(body, row_w, row_h);
    lv_obj_set_pos(body, 0, 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x1a1f2e), 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_radius(body, 8, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(body, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(body, on_press,      LV_EVENT_PRESSED,  st);
    lv_obj_add_event_cb(body, on_pressing,   LV_EVENT_PRESSING, st);
    lv_obj_add_event_cb(body, on_release,    LV_EVENT_RELEASED, st);
    lv_obj_add_event_cb(body, on_body_click, LV_EVENT_CLICKED,  st);
    /* Stash a back-pointer to the state on the body's user_data slot
       so the "close any other revealed row" path can find the other
       row's swipe state when a new gesture begins. */
    lv_obj_set_user_data(body, st);

    st->body = body;
    st->action_btn = act;
    return body;
}

void hb_swipe_row_set_cookie(lv_obj_t *body, void *cookie)
{
    if (!body) return;
    swipe_state_t *st = (swipe_state_t *)lv_obj_get_user_data(body);
    if (st) st->cookie = cookie;
}
