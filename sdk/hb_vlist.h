/*
 * hb_vlist — virtualised vertical list for LVGL.
 *
 * Why: LVGL's lv_list is a flex column whose layout cost is O(N) per
 * scroll event. Past ~50 rows the per-frame work in lv_timer_handler
 * dominates and scroll feel falls off a cliff — we measured 1–4 fps
 * for a plain 200-label flex column on the iPod nano 7g, vs 57 fps
 * for an lv_roller of the same count.
 *
 * What: a scrollable container that holds a small pool of row widgets
 * (~visible + overscan) and recycles them as the user scrolls. Cost
 * per scroll event is O(visible-rows), not O(total).
 *
 * Trade-offs:
 *   - Fixed row height (per-list, not per-row). Variable heights need
 *     extra book-keeping that we don't ship yet.
 *   - The caller's bind_cb is invoked whenever a pooled row needs to
 *     display a different index. The first time a given pool row is
 *     bound it has no children — typical usage: create children on
 *     first call, update them on subsequent calls.
 *
 * Typical use:
 *
 *   static void bind(lv_obj_t *row, int index, void *ctx)
 *   {
 *       lv_obj_t *lbl;
 *       if (lv_obj_get_child_count(row) == 0) {
 *           lbl = lv_label_create(row);
 *           lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
 *           lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 12, 0);
 *       } else {
 *           lbl = lv_obj_get_child(row, 0);
 *       }
 *       char buf[24];
 *       lv_snprintf(buf, sizeof buf, "Item %d", index);
 *       lv_label_set_text(lbl, buf);
 *   }
 *
 *   static void clicked(int index, void *ctx)
 *   {
 *       printf("tapped %d\n", index);
 *   }
 *
 *   hb_vlist_t *v = hb_vlist_create(scr, 40, bind, NULL);
 *   hb_vlist_set_click_cb(v, clicked, NULL);
 *   hb_vlist_set_count(v, 2000);
 *   lv_obj_set_size(hb_vlist_obj(v), HB_SCREEN_W, HB_SCREEN_H - 18);
 *   lv_obj_align(hb_vlist_obj(v), LV_ALIGN_BOTTOM_MID, 0, 0);
 */
#ifndef HB_VLIST_H
#define HB_VLIST_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hb_vlist hb_vlist_t;

/* Called whenever a pooled row needs to display a new index. The same
 * row object may be re-used across many indices — manage children
 * inside the row idempotently (see header comment for the pattern). */
typedef void (*hb_vlist_bind_cb)(lv_obj_t *row, int index, void *ctx);

/* Called when a row is tapped (lv_event_t LV_EVENT_CLICKED). */
typedef void (*hb_vlist_click_cb)(int index, void *ctx);

/* Canvas-mode draw callback. Invoked once per visible row each time
 * LVGL repaints the vlist. `rect` is the row's screen-space rectangle.
 * Draw into `layer` using lv_draw_label / lv_draw_image / lv_draw_fill
 * / lv_draw_rect / lv_draw_line etc. The implementation is responsible
 * for the row background — there is no per-row widget. */
typedef void (*hb_vlist_draw_cb)(lv_layer_t *layer, const lv_area_t *rect,
                                  int index, void *ctx);

/* Build the vlist as a child of `parent`. row_h must be > 0.
 * Initial count is 0 — call hb_vlist_set_count(...) after sizing.
 *
 * Two backends:
 *  - hb_vlist_create        : widget-mode. Each pooled row is a real
 *                             lv_obj children of the spacer; bind_cb
 *                             creates/updates child widgets. Supports
 *                             swipe-to-delete and other rich row
 *                             content. Cheaper than raw lv_list but
 *                             still pays for LVGL's per-widget render.
 *  - hb_vlist_create_canvas : direct-draw. No row widgets — rows are
 *                             painted into the LVGL draw layer each
 *                             repaint via the caller's draw_cb. Same
 *                             approach lv_table uses internally. Much
 *                             faster for plain text/icon rows but the
 *                             caller can't embed real widgets per row. */
hb_vlist_t *hb_vlist_create(lv_obj_t *parent, int row_h,
                            hb_vlist_bind_cb bind, void *bind_ctx);

/* Canvas-mode: see hb_vlist_create's comment. Caller's draw_cb runs
 * once per visible row each repaint. */
hb_vlist_t *hb_vlist_create_canvas(lv_obj_t *parent, int row_h,
                                    hb_vlist_draw_cb draw, void *draw_ctx);

/* Sets total row count and refreshes visible rows. Safe to call any
 * time data changes. */
void hb_vlist_set_count(hb_vlist_t *v, int count);

/* Force re-bind of all currently visible rows. Call after mutating
 * the data model when count hasn't changed (eg. row contents updated). */
void hb_vlist_invalidate(hb_vlist_t *v);

/* Install / change the click handler. */
void hb_vlist_set_click_cb(hb_vlist_t *v, hb_vlist_click_cb cb, void *ctx);

/* The underlying LVGL container — use for size, alignment, styling,
 * scroll-to-index calls via lv_obj_scroll_to_y. */
lv_obj_t *hb_vlist_obj(hb_vlist_t *v);

/* Scroll so `index` is at the top of the viewport. */
void hb_vlist_scroll_to(hb_vlist_t *v, int index, bool anim);

#ifdef __cplusplus
}
#endif

#endif /* HB_VLIST_H */
