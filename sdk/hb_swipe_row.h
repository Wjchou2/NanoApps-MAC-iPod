/*
 * hb_swipe_row.h — iOS-style swipe-to-reveal list row helper.
 *
 * Creates a row container with a hidden trailing action (typically a
 * delete button) that the user can reveal by swiping the row left.
 * Tap on the body fires `on_tap`; tap on the revealed action fires
 * `on_action`. Tap on the body when revealed snaps the row back
 * closed without firing `on_tap`.
 *
 * Callbacks take a single `cookie` (whatever the caller passed in),
 * keeping the per-row data plumbing simple and detached from LVGL's
 * own user_data slots.
 *
 * Returns the BODY container — callers should add their row content
 * (icons, labels, etc.) as children of it.
 */
#ifndef HB_SWIPE_ROW_H_
#define HB_SWIPE_ROW_H_

#include "lvgl/lvgl.h"

typedef void (*hb_swipe_cb_t)(void *cookie);

/* Create a swipe-to-reveal row in `parent`.
   `row_w`/`row_h` = visible row dimensions in the list.
   `action_w` = width of the revealed action (e.g. 64 for a trash button).
   `action_label` = text/symbol shown on the action button.
   `action_bg` = hex color (0xRRGGBB) for the action button background.
   `cookie` = passed to both on_tap and on_action.

   Returns the body container that the caller fills with content. */
lv_obj_t *hb_swipe_row_create(lv_obj_t *parent,
                              int row_w, int row_h, int action_w,
                              const char *action_label, uint32_t action_bg,
                              hb_swipe_cb_t on_tap,
                              hb_swipe_cb_t on_action,
                              void *cookie);

/* Update the cookie associated with an existing swipe row's body.
   Returned-to via the on_tap / on_action callbacks. Used by hb_vlist
   bind callbacks when the same pool row is recycled to display a
   different data item. No-op if body wasn't created via this helper. */
void hb_swipe_row_set_cookie(lv_obj_t *body, void *cookie);

#endif
