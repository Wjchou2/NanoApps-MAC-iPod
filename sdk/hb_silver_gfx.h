/*
 * hb_silver_gfx.h — blit a raw ARGB-8888 pixel buffer onto a native draw
 * context (the OS graphics port a view receives in its draw routine).
 *
 * Wrap the pixels in a draw-DEVICE (base ptr + rowbytes + format),
 * build a source draw-CONTEXT over that device, then rect-copy the source into
 * the destination context. By driving it from a view's overridden draw routine,
 * our pixel buffer composites natively (clipped + layered + animated by the OS),
 * which is what lets an LVGL/GL framebuffer live inside a Silver screen.
 *
 * Addresses are for iPod nano 7 OS 1.1.2. Call on the UI task (from a draw
 * routine). The source context is built once over a fixed buffer and reused.
 */
#ifndef HB_SILVER_GFX_H
#define HB_SILVER_GFX_H

#include "hb_sdk.h"

/* Storage the caller must provide (persisting for the surface's lifetime). The
 * draw-device object carries an embedded lock + shared-data block, so it is
 * larger than it looks; these are generous upper bounds (over-allocation is
 * safe — each ctor writes only its own fields). */
#define HB_GFX_DEVICE_BYTES   512u
#define HB_GFX_CONTEXT_BYTES  360u

/*
 * Wrap a tightly-packed ARGB-8888 buffer (w x h, rowBytes = w*4) as a reusable
 * SOURCE draw context. `dev_storage` (>= HB_GFX_DEVICE_BYTES) and `gfx_storage`
 * (>= HB_GFX_CONTEXT_BYTES) must stay live as long as the surface is used (the
 * context refers to the device, the device to the pixels). Returns the source
 * context to hand to hb_silver_gfx_blit (NULL on bad args).
 */
void *hb_silver_gfx_argb_surface(void *dev_storage, void *gfx_storage,
                                 void *pixels, int w, int h);

/*
 * Copy the whole source surface (w x h) into `dest_ctx` at `dest_rect`, an OS
 * draw-rect (int32 {top,left,bottom,right}). 1:1 when the dest rect is w x h;
 * the OS scales otherwise. `alpha` 0xFF = opaque.
 */
void hb_silver_gfx_blit(void *dest_ctx, void *src_ctx, int w, int h,
                        const void *dest_rect, unsigned char alpha);

#endif /* HB_SILVER_GFX_H */
