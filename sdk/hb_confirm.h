/*
 * hb_confirm — shared "are you sure?" modal for destructive UI.
 *
 * Pops a centered card on the LVGL top layer with a title, body,
 * a Cancel button, and a destructive (red) confirm button. The
 * callback runs on confirm only.
 *
 * Usage:
 *   hb_confirm_show("Delete note?", "This can't be undone.", "Delete",
 *                   on_user_confirmed, my_cookie);
 *
 * The modal owns its own teardown; the caller just provides the
 * callback. Only one modal at a time.
 */
#ifndef HB_CONFIRM_H_
#define HB_CONFIRM_H_

#include "lvgl/lvgl.h"

typedef void (*hb_confirm_cb_t)(void *cookie);
void hb_confirm_show(const char *title, const char *body,
                     const char *confirm_label,
                     hb_confirm_cb_t cb, void *cookie);

#endif
