/*
 * hb_silver_gfx.c — native bitmap blit onto a view's draw context.
 * See hb_silver_gfx.h.
 *
 */

#include "hb_silver_gfx.h"

#define A_DEVICE_CTOR   0x08074d60u
#define A_CONTEXT_CTOR  0x08417bfcu
#define A_COPY_TO_RECT  0x0841919eu

#define PIXFMT_32       0x1888   /* format value the OS uses for our 32-bit buffers */
#define DEPTH_32        32

#define FN_THUMB(a) ((uintptr_t)((a) | 1u))

typedef void (*device_ctor_t)(void *, const void *, void *, int32_t, int32_t,
                              int, void *, unsigned char, unsigned char, void *);
typedef void (*context_ctor_t)(void *, void *);
typedef void (*copy_to_rect_t)(void *, void *, const void *, const void *,
                               unsigned char, const void *);

void *hb_silver_gfx_argb_surface(void *dev_storage, void *gfx_storage,
                                 void *pixels, int w, int h)
{
    int32_t bounds[4];   /* draw-rect {top,left,bottom,right} */

    if (!dev_storage || !gfx_storage || !pixels || w <= 0 || h <= 0)
        return 0;

    bounds[0] = 0; bounds[1] = 0; bounds[2] = h; bounds[3] = w;

    ((device_ctor_t)FN_THUMB(A_DEVICE_CTOR))(dev_storage, bounds, pixels,
                                             (int32_t)(w * 4), DEPTH_32,
                                             PIXFMT_32, 0, 0, 0, 0);
    ((context_ctor_t)FN_THUMB(A_CONTEXT_CTOR))(gfx_storage, dev_storage);
    return gfx_storage;
}

void hb_silver_gfx_blit(void *dest_ctx, void *src_ctx, int w, int h,
                        const void *dest_rect, unsigned char alpha)
{
    int32_t src_rect[4];   /* {top,left,bottom,right} = whole source */

    if (!dest_ctx || !src_ctx || !dest_rect)
        return;
    src_rect[0] = 0; src_rect[1] = 0; src_rect[2] = h; src_rect[3] = w;

    ((copy_to_rect_t)FN_THUMB(A_COPY_TO_RECT))(dest_ctx, src_ctx, src_rect,
                                               dest_rect, alpha, 0);
}
