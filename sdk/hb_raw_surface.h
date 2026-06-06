/*
 * hb_raw_surface.h — direct-draw Silver surface runtime (no LVGL).
 *
 * Same plumbing as an LVGL surface app (the resident loads the relocatable blob,
 * hands us the OS-composited framebuffer, drives a ~60 fps heartbeat and blits
 * the framebuffer to the screen each tick), but the app draws pixels STRAIGHT
 * into the framebuffer — no LVGL render pipeline. That's the fast,
 * immediate-mode path for painting / pixel games (matches the old direct-MIPI
 * speed, but composited by the OS so it's a normal home-screen app with proper
 * touch + lifecycle).
 *
 * The runtime owns: the entry point, per-launch .bss zero, the shared touch
 * source (hb_surface_input), and the per-frame pump. The app implements
 * hb_raw_init() + hb_raw_frame() and draws via the helpers below.
 *
 * Build: RAW_SURFACE := 1 in the app Makefile (HBAppKind 1 — same view as an
 * LVGL surface). The framebuffer is XRGB8888, row-major, hb_raw_w() x hb_raw_h().
 */
#ifndef HB_RAW_SURFACE_H
#define HB_RAW_SURFACE_H

#include <stdint.h>
#include "hb_surface_input.h"

/* ---- implement these in the app ---- */
void hb_raw_init(int w, int h);                /* set up state + draw the first frame */
void hb_raw_frame(const hb_spoint_t *touch);   /* called each heartbeat (~60 fps)     */

/* ---- runtime API ---- */
uint32_t *hb_raw_fb(void);    /* the framebuffer (XRGB8888, w*h, row-major) */
int       hb_raw_w(void);
int       hb_raw_h(void);

/* Direct-draw helpers. Color is 0xRRGGBB (alpha forced opaque — the compositor
 * wants XRGB). All clip to the framebuffer. */
void hb_raw_fill(uint32_t rgb);                              /* whole framebuffer    */
void hb_raw_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void hb_raw_rect_outline(int x, int y, int w, int h, int t, uint32_t rgb);
void hb_raw_disc(int cx, int cy, int r, uint32_t rgb);      /* filled circle (brush) */
void hb_raw_blit(int x, int y, int w, int h, const uint32_t *src);   /* opaque copy  */

#endif /* HB_RAW_SURFACE_H */
