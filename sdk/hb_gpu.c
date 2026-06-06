/*
 * hb_gpu.c
 *
 */
#include "hb_gpu.h"
#include "hb_sdk.h"

#include <string.h>

#define FN_THUMB(a)       ((uintptr_t)(a) | 1u)

/* Static accessor: the display-manager singleton getter. */
#define GET_DM_FN         0x0841a5ccu
/* Offsets within heap-allocated objects. These are positions reached by walking
   pointers out from the singleton; the role labels are what fits the access
   pattern, not a struct definition, so one or two could be mislabeled. */
#define DM_OFF_DRIVER_1   0x008u               /* manager -> display controller driver */
#define DM_OFF_POOL_1_0   0x148u               /* manager -> pixel-buffer pool    */
#define POOL_OFF_VEC      0x014u               /* pool -> buffer pointer vector   */
/* Vtable slots. */
#define PB_VT_GET_BUF_PTR 2u                   /* buffer -> aligned pixel pointer */
#define DISP_VT_SET_BUF   27u                  /* driver -> submit buffer (flip)  */
/* Offset within the display-controller driver where the currently-active buffer for layer 0
   lives. Read off the layer-buffer setter (0x08247a48): its layer-0 branch stores
   the buffer pointer at this+200. Only layer 0 matters to us — the other layers'
   branches we didn't follow. */
#define DISP_CUR_BUF_L0   200u

typedef void *(*get_dm_t)(void);
typedef void *(*get_buf_ptr_t)(void *self);
typedef int   (*set_hw_buf_t)(void *driver, uint32_t layer, void *pBuffer);

static void          *s_disp    = NULL;
static void          *s_pb      = NULL;
static uint32_t      *s_fb      = NULL;
static set_hw_buf_t   s_set_buf = NULL;
static bool           s_ready   = false;

bool hb_gpu_init(void)
{
    if (s_ready) return true;

    void *dm = ((get_dm_t)FN_THUMB(GET_DM_FN))();
    if (!dm) return false;

    s_disp = *(void **)((uint8_t *)dm + DM_OFF_DRIVER_1);
    if (!s_disp) return false;

    void *pool = *(void **)((uint8_t *)dm + DM_OFF_POOL_1_0);
    if (!pool) return false;

    /* The pool's managed-buffer pointer vector. We take entry 0 — the OS
     * pool has 3, and we monopolise the first slot. */
    void **vec = *(void ***)((uint8_t *)pool + POOL_OFF_VEC);
    if (!vec || !vec[0]) return false;
    s_pb = vec[0];

    /* Resolve the aligned-pixel-pointer getter via vtable, cache the result. */
    void **pb_vt = *(void ***)s_pb;
    get_buf_ptr_t gbp = (get_buf_ptr_t)((uintptr_t)pb_vt[PB_VT_GET_BUF_PTR]);
    s_fb = (uint32_t *)gbp(s_pb);
    if (!s_fb) return false;

    /* Cache the layer-buffer setter (the panel flip). */
    void **disp_vt = *(void ***)s_disp;
    s_set_buf = (set_hw_buf_t)((uintptr_t)disp_vt[DISP_VT_SET_BUF]);
    if (!s_set_buf) return false;

    s_ready = true;
    return true;
}

bool hb_gpu_ready(void)         { return s_ready; }
uint32_t *hb_gpu_framebuffer(void) { return s_fb; }

void hb_gpu_flip(void)
{
    if (!s_ready) return;
    /* Make sure prior CPU writes are out of the store buffer before
     * the driver picks the buffer up. The MMU buffer is DMA-coherent
     * but the CPU's write buffer can still reorder. */
    __asm__ volatile("dsb sy" ::: "memory");
    s_set_buf(s_disp, /*layer*/ 0u, s_pb);
}

bool hb_gpu_is_owned(void)
{
    if (!s_ready) return false;
    void *cur = *(void **)((uint8_t *)s_disp + DISP_CUR_BUF_L0);
    return cur == s_pb;
}

void hb_gpu_reclaim_if_lost(void)
{
    if (!s_ready) return;
    if (!hb_gpu_is_owned()) hb_gpu_flip();
}

bool hb_gpu_blit_xrgb8888(int x, int y, int w, int h,
                          const uint32_t *src, int src_stride_px)
{
    if (!s_ready || !src) return false;
    if (w <= 0 || h <= 0) return false;

    /* Clip to panel bounds. */
    if (x < 0) { src += -x; w += x; x = 0; }
    if (y < 0) { src += (-y) * src_stride_px; h += y; y = 0; }
    if (x + w > HB_SCREEN_W) w = HB_SCREEN_W - x;
    if (y + h > HB_SCREEN_H) h = HB_SCREEN_H - y;
    if (w <= 0 || h <= 0) return false;

    uint32_t       *dst = s_fb + y * HB_SCREEN_W + x;
    const uint32_t *s   = src;
    size_t          rb  = (size_t)w * 4u;
    for (int row = 0; row < h; row++) {
        memcpy(dst, s, rb);
        dst += HB_SCREEN_W;
        s   += src_stride_px;
    }
    return true;
}

bool hb_gpu_blit_rgb565(int x, int y, int w, int h,
                        const uint16_t *src, int src_stride_px)
{
    if (!s_ready || !src) return false;
    if (w <= 0 || h <= 0) return false;

    if (x < 0) { src += -x; w += x; x = 0; }
    if (y < 0) { src += (-y) * src_stride_px; h += y; y = 0; }
    if (x + w > HB_SCREEN_W) w = HB_SCREEN_W - x;
    if (y + h > HB_SCREEN_H) h = HB_SCREEN_H - y;
    if (w <= 0 || h <= 0) return false;

    uint32_t       *dst = s_fb + y * HB_SCREEN_W + x;
    const uint16_t *s   = src;
    for (int row = 0; row < h; row++) {
        /* Per-pixel RGB565 -> XRGB8888.
         *  r5 -> r8: (v >> 11) & 0x1f  -> << 3, then |= top 3 bits of r5 for rounding
         *  g6 -> g8: (v >>  5) & 0x3f  -> << 2, then |= top 2 bits
         *  b5 -> b8: (v       ) & 0x1f -> << 3, then |= top 3 bits
         * Replicate-top trick gives a smooth 0..255 range from 0..31 / 0..63. */
        for (int i = 0; i < w; i++) {
            uint16_t v = s[i];
            uint32_t r = (v >> 11) & 0x1fu;
            uint32_t g = (v >>  5) & 0x3fu;
            uint32_t b = (v      ) & 0x1fu;
            uint32_t R = (r << 3) | (r >> 2);
            uint32_t G = (g << 2) | (g >> 4);
            uint32_t B = (b << 3) | (b >> 2);
            dst[i] = 0xff000000u | (R << 16) | (G << 8) | B;
        }
        dst += HB_SCREEN_W;
        s   += src_stride_px;
    }
    return true;
}
