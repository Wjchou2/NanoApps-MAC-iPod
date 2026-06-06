/*
 * hb_gl_surface.h — GL surface app contract (the GL analogue of hb_lv_surface).
 *
 * A GL_SURFACE app is a relocatable .hbapp the resident loads into an operator-new
 * arena (like an LVGL surface app). Instead of an LVGL UI it provides hardware-GL
 * draw code: the app implements gl_app_init() (one-time setup) and gl_app_frame()
 * (per-frame draws). The resident injects a fullscreen GL view and, between its
 * its begin/end scene, calls the app's gl_app_frame — so gl_app_frame just issues
 * GLES 1.1 calls (sdk/gl.h) into the current context; the OS composites the result.
 */
#ifndef HB_GL_SURFACE_H
#define HB_GL_SURFACE_H

#include <stdint.h>

/* One-time setup, run when the app loads (before any GL context is current — do
 * NOT issue GL calls here; build geometry lazily in the first gl_app_frame). */
void gl_app_init(void);

/* Per-frame GL render into the CURRENT context. w/h = surface size (the full panel),
 * frame = monotonically increasing frame counter (use for animation). */
void gl_app_frame(int w, int h, uint32_t frame);

#endif /* HB_GL_SURFACE_H */
