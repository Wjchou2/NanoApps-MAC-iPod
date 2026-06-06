/*
 * hb_gpu.h — direct CPU writes into the OS-owned display framebuffer, then a
 * panel flip via the display driver's layer-buffer setter (vtable slot 27).
 */
#ifndef HB_GPU_H
#define HB_GPU_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One-time setup. Looks up the manager, picks a pool buffer to render
 * into, and caches the CPU-side framebuffer pointer + driver vtable.
 * Returns true on success. Safe to call multiple times — re-init is
 * a no-op. */
bool hb_gpu_init(void);

/* True once init succeeded. */
bool hb_gpu_ready(void);

/* CPU-writable framebuffer base. NULL before init succeeds. The
 * layout is XRGB8888, width=240, height=432, stride=240*4=960 bytes. */
uint32_t *hb_gpu_framebuffer(void);

/* Submit the cached buffer to the panel driver — makes the current
 * framebuffer contents visible. */
void hb_gpu_flip(void);

/* True if the panel driver's currently-active layer-0 buffer is
 * still our pb. False means the OS compositor (status bar minute
 * tick, charging icon, etc) has stolen the layer. Read-only — does
 * not flip. */
bool hb_gpu_is_owned(void);

/* Equivalent to: if (!hb_gpu_is_owned()) hb_gpu_flip();
 * Safe to call every main-loop iteration — the read is a single
 * load and the flip only runs when the OS has stolen the layer. */
void hb_gpu_reclaim_if_lost(void);

/* Convenience: blit a rect (in XRGB8888 source) into the framebuffer
 * at (x, y). The source has its own stride (src_stride_px in pixels).
 * No-op + returns false if not initialised. */
bool hb_gpu_blit_xrgb8888(int x, int y, int w, int h,
                          const uint32_t *src, int src_stride_px);

/* Same as above but the source is RGB565. Each source pixel is
 * expanded to XRGB8888 inline as it's written into the framebuffer.
 * Used by the LVGL flush path when LV_COLOR_DEPTH=16. */
bool hb_gpu_blit_rgb565(int x, int y, int w, int h,
                        const uint16_t *src, int src_stride_px);

#ifdef __cplusplus
}
#endif

#endif /* HB_GPU_H */
