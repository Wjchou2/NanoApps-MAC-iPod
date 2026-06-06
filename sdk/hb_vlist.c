/*
 * hb_vlist — see hb_vlist.h for the why.
 *
 * Layout strategy:
 *   - viewport: scrollable lv_obj (the public handle).
 *   - spacer: a tall transparent child whose height equals
 *     row_h * count. This is what gives the scroll bar / momentum
 *     its full range without our needing to actually emit N widgets.
 *   - row pool: ceil(viewport_h / row_h) + 2 row objects, parented to
 *     the spacer. We reposition them via lv_obj_set_y to wherever the
 *     scroll says they should live.
 *
 * Scroll handling:
 *   - On LV_EVENT_SCROLL_END / LV_EVENT_SCROLL we recompute
 *     first_visible = clamp(scroll_y / row_h, 0, count - pool_n) and
 *     assign each pool row to (first_visible + pool_slot).
 *   - bind_cb is only called when the index actually changes for a
 *     given pool slot — cheap re-renders.
 *
 * Pool sizing:
 *   We compute pool size lazily once the viewport height is known —
 *   either after the first layout pass or on first set_count. If the
 *   viewport resizes later, we re-pool. The pool is allocated via
 *   lv_malloc so it lives in LVGL's TLSF heap.
 */
#include "hb_vlist.h"
#include "lvgl/lvgl.h"

#include <stddef.h>

#define HB_VLIST_OVERSCAN 1     /* extra rows on each side */

struct hb_vlist {
    lv_obj_t          *viewport;
    lv_obj_t          *spacer;
    lv_obj_t         **rows;        /* pool, pool_n entries (widget mode only) */
    int               *row_index;   /* index currently bound at each slot, -1 if unbound */
    int                pool_n;
    int                row_h;
    int                count;
    int                first_visible;
    bool               canvas_mode; /* false → widget pool, true → direct-draw */
    hb_vlist_bind_cb   bind;
    void              *bind_ctx;
    hb_vlist_draw_cb   draw;
    void              *draw_ctx;
    hb_vlist_click_cb  click;
    void              *click_ctx;
};

/* Each pool row stores back-pointers via user_data so the event handler
 * can recover (vlist, pool_slot). pool_slot lets us re-compute the
 * row's current index from first_visible — robust across recycles. */
typedef struct {
    hb_vlist_t *v;
    int         pool_slot;
} hb_vlist_row_ud_t;

static void row_clicked_cb(lv_event_t *e)
{
    hb_vlist_row_ud_t *ud = (hb_vlist_row_ud_t *)lv_event_get_user_data(e);
    if (!ud || !ud->v || !ud->v->click) return;
    int idx = ud->v->row_index[ud->pool_slot];
    if (idx < 0 || idx >= ud->v->count) return;
    ud->v->click(idx, ud->v->click_ctx);
}

static void position_and_bind(hb_vlist_t *v, int slot, int index)
{
    lv_obj_t *row = v->rows[slot];
    /* Always reposition — even if the index is unchanged, scroll can
     * still shift relative to the spacer's coordinate system.
     * (Actually no: rows are children of the spacer, so absolute y is
     * stable. Only need to rebind when index changes.) */
    if (v->row_index[slot] == index) return;
    lv_obj_set_y(row, index * v->row_h);
    if (index >= 0 && index < v->count) {
        lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
        if (v->bind) v->bind(row, index, v->bind_ctx);
    } else {
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
    v->row_index[slot] = index;
}

static void recompute_visible(hb_vlist_t *v)
{
    if (v->pool_n <= 0 || v->count <= 0) return;

    int scroll_y = lv_obj_get_scroll_y(v->viewport);
    if (scroll_y < 0) scroll_y = 0;

    int first = scroll_y / v->row_h - HB_VLIST_OVERSCAN;
    if (first < 0) first = 0;
    int max_first = v->count - v->pool_n;
    if (max_first < 0) max_first = 0;
    if (first > max_first) first = max_first;
    v->first_visible = first;

    for (int i = 0; i < v->pool_n; i++) {
        position_and_bind(v, i, first + i);
    }
}

static void on_scroll(lv_event_t *e)
{
    hb_vlist_t *v = (hb_vlist_t *)lv_event_get_user_data(e);
    if (v) recompute_visible(v);
}

static void destroy_pool(hb_vlist_t *v)
{
    if (!v->rows) return;
    for (int i = 0; i < v->pool_n; i++) {
        if (v->rows[i]) lv_obj_delete(v->rows[i]);
    }
    lv_free(v->rows);
    lv_free(v->row_index);
    v->rows = NULL;
    v->row_index = NULL;
    v->pool_n = 0;
}

static void build_pool(hb_vlist_t *v)
{
    int vh = lv_obj_get_height(v->viewport);
    if (vh <= 0) {
        /* Viewport not yet laid out — defer until set_count or after
         * the layout pass. */
        return;
    }
    int visible = (vh + v->row_h - 1) / v->row_h;
    int pool_n = visible + 2 * HB_VLIST_OVERSCAN;
    if (pool_n < 3) pool_n = 3;
    if (v->count > 0 && pool_n > v->count) pool_n = v->count;

    if (pool_n == v->pool_n && v->rows) return;
    destroy_pool(v);

    v->rows      = (lv_obj_t **)lv_malloc(sizeof(lv_obj_t *) * pool_n);
    v->row_index = (int *)lv_malloc(sizeof(int) * pool_n);
    if (!v->rows || !v->row_index) {
        lv_free(v->rows); lv_free(v->row_index);
        v->rows = NULL; v->row_index = NULL;
        v->pool_n = 0;
        return;
    }
    v->pool_n = pool_n;

    for (int i = 0; i < pool_n; i++) {
        v->row_index[i] = -1;
        lv_obj_t *row = lv_obj_create(v->spacer);
        v->rows[i] = row;
        lv_obj_set_size(row, lv_pct(100), v->row_h);
        lv_obj_set_x(row, 0);
        /* Pooled rows have transparent backgrounds + no border by
         * default — the caller styles them in the bind callback. */
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

        hb_vlist_row_ud_t *ud = (hb_vlist_row_ud_t *)lv_malloc(sizeof *ud);
        ud->v = v;
        ud->pool_slot = i;
        lv_obj_add_event_cb(row, row_clicked_cb, LV_EVENT_CLICKED, ud);
    }

    recompute_visible(v);
}

static void on_viewport_layout(lv_event_t *e)
{
    hb_vlist_t *v = (hb_vlist_t *)lv_event_get_user_data(e);
    if (!v) return;
    if (!v->rows) build_pool(v);
}

hb_vlist_t *hb_vlist_create(lv_obj_t *parent, int row_h,
                            hb_vlist_bind_cb bind, void *bind_ctx)
{
    if (!parent || row_h <= 0 || !bind) return NULL;
    hb_vlist_t *v = (hb_vlist_t *)lv_malloc(sizeof *v);
    if (!v) return NULL;
    v->viewport = lv_obj_create(parent);
    v->row_h = row_h;
    v->count = 0;
    v->first_visible = 0;
    v->canvas_mode = false;
    v->bind = bind;
    v->bind_ctx = bind_ctx;
    v->draw = NULL;
    v->draw_ctx = NULL;
    v->click = NULL;
    v->click_ctx = NULL;
    v->rows = NULL;
    v->row_index = NULL;
    v->pool_n = 0;

    /* viewport: scroll on Y only, clip children at edges, no border/pad. */
    lv_obj_set_scroll_dir(v->viewport, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(v->viewport, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(v->viewport, 0, 0);
    lv_obj_set_style_border_width(v->viewport, 0, 0);
    lv_obj_set_style_radius(v->viewport, 0, 0);

    /* Spacer is a child of the viewport — gives scroll its range. */
    v->spacer = lv_obj_create(v->viewport);
    lv_obj_set_width(v->spacer, lv_pct(100));
    lv_obj_set_height(v->spacer, 0);
    lv_obj_set_style_bg_opa(v->spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(v->spacer, 0, 0);
    lv_obj_set_style_pad_all(v->spacer, 0, 0);
    lv_obj_remove_flag(v->spacer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(v->viewport, on_scroll, LV_EVENT_SCROLL, v);
    lv_obj_add_event_cb(v->viewport, on_viewport_layout,
                        LV_EVENT_SIZE_CHANGED, v);
    return v;
}

/* ---- canvas mode ---- */

/* Paint the visible rows directly via the caller's draw_cb. Same
 * approach lv_table uses: skip per-row widget overhead entirely. */
static void on_canvas_draw_post(lv_event_t *e)
{
    hb_vlist_t *v = (hb_vlist_t *)lv_event_get_user_data(e);
    if (!v || !v->draw || v->count <= 0 || v->row_h <= 0) return;

    lv_layer_t *layer = lv_event_get_layer(e);
    if (!layer) return;

    /* Compute the visible row band in spacer-coordinates.
     * spacer.y = -scroll_y when the user scrolls down. */
    int scroll_y    = lv_obj_get_scroll_y(v->viewport);
    int viewport_y  = lv_obj_get_y(v->viewport);
    int viewport_h  = lv_obj_get_content_height(v->viewport);
    int viewport_x  = lv_obj_get_x(v->viewport);
    int viewport_w  = lv_obj_get_content_width(v->viewport);
    /* Translate to absolute screen coords via the viewport's parent. */
    lv_area_t coords; lv_obj_get_coords(v->viewport, &coords);

    /* Don't clamp scroll_y to >= 0 — negative means the user pulled
     * down past the top (rubber-band), and rows must be drawn shifted
     * down to follow. Same goes for past-bottom over-scroll. */
    int first = scroll_y / v->row_h;
    int last  = (scroll_y + viewport_h + v->row_h - 1) / v->row_h;
    if (first < 0) first = 0;
    if (last < 0)  last = 0;
    if (last > v->count) last = v->count;

    (void)viewport_y; (void)viewport_x; (void)viewport_w;
    for (int i = first; i < last; i++) {
        lv_area_t rect;
        rect.x1 = coords.x1;
        rect.x2 = coords.x2;
        rect.y1 = coords.y1 + i * v->row_h - scroll_y;
        rect.y2 = rect.y1 + v->row_h - 1;
        v->draw(layer, &rect, i, v->draw_ctx);
    }
}

static void on_canvas_clicked(lv_event_t *e)
{
    hb_vlist_t *v = (hb_vlist_t *)lv_event_get_user_data(e);
    if (!v || !v->click || v->row_h <= 0) return;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p; lv_indev_get_point(indev, &p);

    lv_area_t coords; lv_obj_get_coords(v->viewport, &coords);
    int rel_y = (int)p.y - coords.y1;
    int scroll_y = lv_obj_get_scroll_y(v->viewport);
    int index = (rel_y + scroll_y) / v->row_h;
    if (index < 0 || index >= v->count) return;
    v->click(index, v->click_ctx);
}

hb_vlist_t *hb_vlist_create_canvas(lv_obj_t *parent, int row_h,
                                    hb_vlist_draw_cb draw, void *draw_ctx)
{
    if (!parent || row_h <= 0 || !draw) return NULL;
    hb_vlist_t *v = (hb_vlist_t *)lv_malloc(sizeof *v);
    if (!v) return NULL;
    v->viewport = lv_obj_create(parent);
    v->row_h = row_h;
    v->count = 0;
    v->first_visible = 0;
    v->canvas_mode = true;
    v->bind = NULL;
    v->bind_ctx = NULL;
    v->draw = draw;
    v->draw_ctx = draw_ctx;
    v->click = NULL;
    v->click_ctx = NULL;
    v->rows = NULL;
    v->row_index = NULL;
    v->pool_n = 0;

    lv_obj_set_scroll_dir(v->viewport, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(v->viewport, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(v->viewport, 0, 0);
    lv_obj_set_style_border_width(v->viewport, 0, 0);
    lv_obj_set_style_radius(v->viewport, 0, 0);
    lv_obj_add_flag(v->viewport, LV_OBJ_FLAG_CLICKABLE);

    /* Spacer gives scrolling its range — same trick as widget mode,
     * just empty in canvas mode (no children to position). */
    v->spacer = lv_obj_create(v->viewport);
    lv_obj_set_width(v->spacer, lv_pct(100));
    lv_obj_set_height(v->spacer, 0);
    lv_obj_set_style_bg_opa(v->spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(v->spacer, 0, 0);
    lv_obj_set_style_pad_all(v->spacer, 0, 0);
    lv_obj_remove_flag(v->spacer, LV_OBJ_FLAG_SCROLLABLE);

    /* DRAW_POST runs after LVGL's own background/border/etc, so our
     * rows overlay anything we styled on the viewport. */
    lv_obj_add_event_cb(v->viewport, on_canvas_draw_post,
                        LV_EVENT_DRAW_POST_END, v);
    lv_obj_add_event_cb(v->viewport, on_canvas_clicked,
                        LV_EVENT_CLICKED, v);
    /* Scroll invalidates the viewport, which schedules a repaint. */
    return v;
}

void hb_vlist_set_count(hb_vlist_t *v, int count)
{
    if (!v) return;
    if (count < 0) count = 0;
    v->count = count;
    lv_obj_set_height(v->spacer, count * v->row_h);
    if (v->canvas_mode) {
        /* No widget pool to (re)build. Just trigger a repaint so the
         * new range is drawn. */
        lv_obj_invalidate(v->viewport);
        return;
    }
    if (!v->rows) build_pool(v);
    else recompute_visible(v);
}

void hb_vlist_invalidate(hb_vlist_t *v)
{
    if (!v) return;
    if (v->canvas_mode) {
        lv_obj_invalidate(v->viewport);
        return;
    }
    for (int i = 0; i < v->pool_n; i++) v->row_index[i] = -1;
    recompute_visible(v);
}

void hb_vlist_set_click_cb(hb_vlist_t *v, hb_vlist_click_cb cb, void *ctx)
{
    if (!v) return;
    v->click = cb;
    v->click_ctx = ctx;
}

lv_obj_t *hb_vlist_obj(hb_vlist_t *v)
{
    return v ? v->viewport : NULL;
}

void hb_vlist_scroll_to(hb_vlist_t *v, int index, bool anim)
{
    if (!v || v->row_h <= 0) return;
    int y = index * v->row_h;
    lv_obj_scroll_to_y(v->viewport, y, anim ? LV_ANIM_ON : LV_ANIM_OFF);
}
