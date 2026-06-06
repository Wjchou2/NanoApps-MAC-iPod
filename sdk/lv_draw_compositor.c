/*
 * lv_draw_compositor.c — LVGL draw unit that offloads drawing to the iPod's GPU hardware 2D 
 * engine via hb_compositor. Modeled on LVGL's STM32 DMA2D backend (lvgl/src/draw/dma2d): a hardware
 * fill/blit unit coexisting with the software unit.
 *
 * The unit only claims tasks whose target layer buffer is the compositor-addressable
 * GPU pixel buffer the surface runtime renders into (hb_lv_framebuffer()); the
 * software unit handles everything else (text, gradients, borders, intermediate
 * transform/opacity layers). Built only with HB_LV_COMPOSITOR.
 */
#ifdef HB_LV_COMPOSITOR

#include "lvgl/lvgl.h"
#include "draw/lv_draw_private.h"
#include "draw/lv_draw_buf_private.h"
#include "draw/lv_draw_image_private.h"
#include "draw/lv_image_decoder_private.h"
#include "misc/lv_area_private.h"
#include "hb_compositor.h"
#include "hb_lv_surface.h"

#define DRAW_UNIT_ID_COMPOSITOR 7

static void hw2d_dcache_flush(void *addr, uint32_t size)
{
    register uint32_t r12 __asm__("r12") = 7;
    register uint32_t r0  __asm__("r0")  = (uint32_t)(uintptr_t)addr;
    register uint32_t r1  __asm__("r1")  = size;
    __asm__ volatile("svc #70" : "+r"(r12), "+r"(r0), "+r"(r1) :: "memory");
}

/* ============================================================================
 * compositor draw-buffer pool — make LVGL's layer + decoded-image buffers GPU-mapped
 * pixel buffers so the compositor can target (and read) them. We override LVGL's draw-buf
 * malloc/free (lv_draw_buf_get_handlers) to allocate from a pool of GPU pixel buffers,
 * reusing them on free (no per-frame alloc churn, and no need for the buffer's
 * release entry point). Tiny buffers stay on lv_malloc. The pool doubles as the
 * "is this buffer compositor-addressable?" oracle the draw unit consults.
 * ==========================================================================*/
#define HW2D_POOL_MAX     48
#define HW2D_MIN_BYTES    16384u   /* below this: lv_malloc (not worth a GPU mapping) */

typedef struct {
    void    *pixels;   /* buffer's pixel pointer (== draw_buf->data) */
    void    *pb;       /* GPU pixel-buffer handle */
    uint32_t bytes;    /* capacity */
    uint8_t  in_use;
} gk_pool_ent_t;

static gk_pool_ent_t s_pool[HW2D_POOL_MAX];
static int           s_pool_n;

/* Find the pool entry whose buffer is `data` (NULL if not a compositor buffer). */
static gk_pool_ent_t *gk_pool_find(const void *data)
{
    int i;
    for (i = 0; i < s_pool_n; i++)
        if (s_pool[i].pixels == data) return &s_pool[i];
    return NULL;
}

/* Register an externally-created compositor buffer (the surface runtime's main display
   buffer) so the unit treats it like a pooled one. */
void hb_lv_compositor_register(void *pixels, void *pb)
{
    if (pixels && s_pool_n < HW2D_POOL_MAX) {
        s_pool[s_pool_n].pixels = pixels;
        s_pool[s_pool_n].pb     = pb;
        s_pool[s_pool_n].bytes  = 0xffffffffu;   /* never reused for alloc */
        s_pool[s_pool_n].in_use = 1;
        s_pool_n++;
    }
}

static void *gk_buf_malloc(size_t size, lv_color_format_t cf)
{
    int i, best = -1;
    LV_UNUSED(cf);
    if (size < HW2D_MIN_BYTES)
        return lv_malloc(size);

    /* reuse a free pooled buffer that's big enough (smallest such — least waste) */
    for (i = 0; i < s_pool_n; i++) {
        if (!s_pool[i].in_use && s_pool[i].bytes >= size &&
            (best < 0 || s_pool[i].bytes < s_pool[best].bytes))
            best = i;
    }
    if (best >= 0) { s_pool[best].in_use = 1; return s_pool[best].pixels; }

    /* allocate a new GPU pixel buffer of >= size bytes (as 512 x ceil(size/2048)) */
    if (s_pool_n < HW2D_POOL_MAX) {
        void *px = 0, *pb;
        int h = (int)((size + 2047u) / 2048u);
        pb = hb_compositor_pixbuf_create(512, h, 1 /*cached; compositor ops cache-flush their
                                              region, so SW rendering stays fast*/, &px);
        if (px) {
            s_pool[s_pool_n].pixels = px;
            s_pool[s_pool_n].pb     = pb;
            s_pool[s_pool_n].bytes  = (uint32_t)(512u * 4u * (uint32_t)h);
            s_pool[s_pool_n].in_use = 1;
            s_pool_n++;
            return px;
        }
    }
    /* pool full or alloc failed: fall back to heap (renders fine, just not compositor) */
    return lv_malloc(size);
}

static void gk_buf_free(void *buf)
{
    gk_pool_ent_t *e = gk_pool_find(buf);
    if (e) { if (e->bytes != 0xffffffffu) e->in_use = 0; }   /* recycle (keep main pinned) */
    else lv_free(buf);
}

/* Override LVGL's draw-buf allocators (default/font/image-cache) to use the pool.
   Call once after lv_init, before any draw-buf is allocated. */
void hb_lv_compositor_drawbuf_init(void)
{
    lv_draw_buf_handlers_t *h[3];
    int i;
    h[0] = lv_draw_buf_get_handlers();
    h[1] = lv_draw_buf_get_font_handlers();
    h[2] = lv_draw_buf_get_image_handlers();
    for (i = 0; i < 3; i++) {
        if (!h[i]) continue;
        h[i]->buf_malloc_cb = gk_buf_malloc;
        h[i]->buf_free_cb   = gk_buf_free;
    }
}

typedef struct {
    lv_draw_unit_t   base_unit;
    lv_draw_task_t  *task_act;
} lv_draw_compositor_unit_t;

/* True if `layer` renders into a compositor-addressable buffer (the main display buffer
   or any pooled GPU pixel buffer). Heap-fallback layers aren't in the pool. */
static bool layer_is_compositor(lv_layer_t *layer)
{
    return layer && layer->draw_buf && gk_pool_find(layer->draw_buf->data) != NULL;
}

/* IMAGE render core (called by lv_draw_image_normal_helper after it decodes the
   image). Hardware-blits a pooled compositor source onto the (pooled) layer. evaluate_cb
   has already guaranteed: source is a pooled GPU pixel buffer, un-transformed, ARGB/
   XRGB on both sides — so no software fallback is needed here. */
static void gk_image_core(lv_draw_task_t *t, const lv_draw_image_dsc_t *dsc,
                          const lv_image_decoder_dsc_t *ddsc, lv_draw_image_sup_t *sup,
                          const lv_area_t *img_coords, const lv_area_t *clip)
{
    lv_layer_t *layer = t->target_layer;
    const lv_draw_buf_t *decoded = ddsc->decoded;
    const uint8_t *src_buf = decoded->data;
    uint32_t sstride = decoded->header.stride;
    lv_color_format_t scf = decoded->header.cf;
    uint32_t sbpp = lv_color_format_get_size(scf);
    int32_t w = lv_area_get_width(clip), h = lv_area_get_height(clip);
    void *dest = lv_draw_layer_go_to_xy(layer, clip->x1 - layer->buf_area.x1,
                                        clip->y1 - layer->buf_area.y1);
    int32_t dstride = lv_draw_buf_width_to_stride(lv_area_get_width(&layer->buf_area),
                                                  layer->color_format);
    const uint8_t *sfirst;
    hb_hw2d_surface ss, ds;
    LV_UNUSED(sup); LV_UNUSED(img_coords);

    if (sstride == 0) sstride = sbpp * decoded->header.w;
    if (!gk_pool_find(src_buf)) return;   /* safety; evaluate guaranteed pooled */

    sfirst = src_buf + sstride * (uint32_t)(clip->y1 - dsc->image_area.y1)
                     + sbpp    * (uint32_t)(clip->x1 - dsc->image_area.x1);

    /* clean src + dst to memory (the GPU reads both for a blend) and invalidate, so
       the CPU re-reads the result afterward. Done BEFORE the op: dst may hold this
       frame's SW-drawn background that the blend must composite over. */
    hw2d_dcache_flush((void *)sfirst, (uint32_t)((h - 1) * (int32_t)sstride + w * (int32_t)sbpp));
    hw2d_dcache_flush(dest, (uint32_t)((h - 1) * dstride + w * 4));

    ss.base = (void *)sfirst; ss.w = (uint32_t)w; ss.h = (uint32_t)h;
    ss.stride = sstride; ss.fmt = HB_HW2D_FMT_ARGB8888;
    ds.base = dest; ds.w = (uint32_t)w; ds.h = (uint32_t)h;
    ds.stride = (uint32_t)dstride; ds.fmt = HB_HW2D_FMT_ARGB8888;

    if (dsc->opa >= LV_OPA_COVER && scf == LV_COLOR_FORMAT_XRGB8888)
        hb_compositor_blit(&ss, &ds, 0, 0, 0, 0, w, h);            /* opaque copy */
    else
        hb_compositor_blit_blend(&ss, &ds, 0, 0, 0, 0, w, h, dsc->opa);  /* src-over (+global opa) */
    hb_compositor_flush();
}

static int32_t evaluate_cb(lv_draw_unit_t *draw_unit, lv_draw_task_t *task)
{
    LV_UNUSED(draw_unit);
    if (task->type == LV_DRAW_TASK_TYPE_FILL) {
        lv_draw_fill_dsc_t *dsc = task->draw_dsc;
        lv_color_format_t cf = dsc->base.layer->color_format;
        /* solid, square, opaque, into an XRGB/ARGB compositor buffer */
        if (dsc->radius != 0) return 0;
        if (dsc->grad.dir != LV_GRAD_DIR_NONE) return 0;
        if (dsc->opa < LV_OPA_MAX) return 0;
        if (cf != LV_COLOR_FORMAT_ARGB8888 && cf != LV_COLOR_FORMAT_XRGB8888) return 0;
        if (!layer_is_compositor(dsc->base.layer)) return 0;
    }
    else if (task->type == LV_DRAW_TASK_TYPE_IMAGE) {
        lv_draw_image_dsc_t *dsc = task->draw_dsc;
        lv_color_format_t scf = dsc->header.cf;
        lv_color_format_t dcf = dsc->base.layer->color_format;
        /* un-transformed 1:1 ARGB/XRGB blit from a POOLED source onto a compositor layer */
        if (dsc->clip_radius != 0) return 0;
        if (dsc->bitmap_mask_src != NULL) return 0;
        if (dsc->tile) return 0;
        if (dsc->blend_mode != LV_BLEND_MODE_NORMAL) return 0;
        if (dsc->recolor_opa > LV_OPA_MIN) return 0;
        if (dsc->skew_x != 0 || dsc->skew_y != 0) return 0;
        if (dsc->scale_x != LV_SCALE_NONE || dsc->scale_y != LV_SCALE_NONE) return 0;
        if (dsc->rotation != 0) return 0;
        if (scf != LV_COLOR_FORMAT_ARGB8888 && scf != LV_COLOR_FORMAT_XRGB8888) return 0;
        if (dcf != LV_COLOR_FORMAT_ARGB8888 && dcf != LV_COLOR_FORMAT_XRGB8888) return 0;
        if (lv_image_src_get_type(dsc->src) != LV_IMAGE_SRC_VARIABLE) return 0;
        if (gk_pool_find(((const lv_image_dsc_t *)dsc->src)->data) == NULL) return 0;
        if (!layer_is_compositor(dsc->base.layer)) return 0;
    }
    else {
        return 0;
    }
    task->preferred_draw_unit_id = DRAW_UNIT_ID_COMPOSITOR;
    task->preference_score = 0;
    return 0;
}

static int32_t dispatch_cb(lv_draw_unit_t *draw_unit, lv_layer_t *layer)
{
    lv_draw_compositor_unit_t *u = (lv_draw_compositor_unit_t *)draw_unit;
    lv_draw_task_t *t;
    lv_area_t clip;

    if (u->task_act)
        return LV_DRAW_UNIT_IDLE;

    t = lv_draw_get_available_task(layer, NULL, DRAW_UNIT_ID_COMPOSITOR);
    if (t == NULL)
        return LV_DRAW_UNIT_IDLE;

    if (lv_draw_layer_alloc_buf(layer) == NULL)
        return LV_DRAW_UNIT_IDLE;

    t->state = LV_DRAW_TASK_STATE_IN_PROGRESS;
    t->draw_unit = draw_unit;
    u->task_act = t;

    if (lv_area_intersect(&clip, &t->area, &t->clip_area) &&
        t->type == LV_DRAW_TASK_TYPE_IMAGE) {
        /* decode + clip handled by the helper; gk_image_core does the hw blit */
        lv_draw_image_normal_helper(t, t->draw_dsc, &t->area, gk_image_core, NULL);
    }
    else if (lv_area_intersect(&clip, &t->area, &t->clip_area) &&
        t->type == LV_DRAW_TASK_TYPE_FILL) {
        lv_draw_fill_dsc_t *dsc = t->draw_dsc;
        int32_t w = lv_area_get_width(&clip);
        int32_t h = lv_area_get_height(&clip);
        void *dest = lv_draw_layer_go_to_xy(layer,
                                            clip.x1 - layer->buf_area.x1,
                                            clip.y1 - layer->buf_area.y1);
        int32_t stride = lv_draw_buf_width_to_stride(lv_area_get_width(&layer->buf_area),
                                                     dsc->base.layer->color_format);
        hb_hw2d_surface s;
        s.base = dest; s.w = (uint32_t)w; s.h = (uint32_t)h;
        s.stride = (uint32_t)stride; s.fmt = HB_HW2D_FMT_ARGB8888;
        /* dest is the first pixel of the clipped rect; fill {0,0,w,h} at full stride */
        hb_compositor_fill(&s, 0, 0, w, h, 0xff000000u | lv_color_to_u32(dsc->color));
        hb_compositor_flush();   /* sync: GPU op done before the task is marked finished */
        /* clean+invalidate the filled region (cached buffer) so later CPU access
           sees the GPU result. Flush the rect's bounding byte range. */
        hw2d_dcache_flush(dest, (uint32_t)((h - 1) * stride + w * 4));
    }

    t->state = LV_DRAW_TASK_STATE_FINISHED;
    u->task_act = NULL;
    lv_draw_dispatch_request();
    return 1;
}

static int32_t delete_cb(lv_draw_unit_t *draw_unit)
{
    LV_UNUSED(draw_unit);
    return 0;
}

void lv_draw_compositor_init(void)
{
    lv_draw_compositor_unit_t *u = lv_draw_create_unit(sizeof(lv_draw_compositor_unit_t));
    u->base_unit.evaluate_cb = evaluate_cb;
    u->base_unit.dispatch_cb = dispatch_cb;
    u->base_unit.delete_cb   = delete_cb;
    u->base_unit.name        = "compositor";
}

#endif /* HB_LV_COMPOSITOR */
