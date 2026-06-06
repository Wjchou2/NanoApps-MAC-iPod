/*
 * hb_compositor.h — hardware-accelerated 2D (fill / blit) via the OS's 2D
 * dispatcher running on the GPU.
 *
 */
#ifndef HB_COMPOSITOR_H
#define HB_COMPOSITOR_H

#include <stdint.h>
#include <stdbool.h>

/* The OS color-format enum values we use. */
enum {
    HB_HW2D_FMT_RGB565        = 11,
    HB_HW2D_FMT_RGB888        = 12,
    HB_HW2D_FMT_ARGB8888      = 13,   /* 0xAARRGGBB little-endian — our LVGL buffer */
};

/* The OS blend-mode enum values we use. */
enum {
    HB_HW2D_BLEND_COPY    = 0,    /* opaque src -> dst */
    HB_HW2D_BLEND_NOP     = 1,
    HB_HW2D_BLEND_FILL    = 2,    /* solid color fill */
    HB_HW2D_BLEND_BLEND   = 3,    /* parametric blend (alpha) */
};
#define HB_HW2D_ROP3_COPY 0xCCu

/* The OS's memory / fill / blit descriptor structs are built at runtime by the
   OS's own constructors (byte-enums + nested bases make a hand-written layout
   fragile) and patched at confirmed offsets — see hb_compositor.c. Apps only deal
   with the surface descriptor below. */

/* A destination/source surface the helpers describe to the hardware. */
typedef struct {
    void    *base;
    uint32_t w, h;
    uint32_t stride;   /* bytes per row */
    uint32_t fmt;      /* HB_HW2D_FMT_* */
} hb_hw2d_surface;

/* True if the dispatcher singleton getter returns non-NULL (the GPU 2D path is
   reachable from this thread). Cheap; safe to gate on. */
bool hb_compositor_available(void);

/* Describe a tightly-packed surface (stride = w * bytespp(fmt), size = stride*h). */
void hb_compositor_surface_init(hb_hw2d_surface *s, void *base, uint32_t w, uint32_t h, uint32_t fmt);

/* Hardware solid-color fill of dst rect (x,y,w,h) with ARGB8888 `argb`.
   Returns the OS error code (0 = ok), -1 if the dispatcher is unavailable. */
int hb_compositor_fill(const hb_hw2d_surface *dst, int x, int y, int w, int h, uint32_t argb);

/* Hardware opaque 1:1 copy of a w*h block from src(sx,sy) to dst(dx,dy). Same
   pixel format, no scaling (low-level GPU 2D blit). Drain with hb_compositor_flush().
   Returns the OS error code, -1 if unavailable. */
int hb_compositor_blit(const hb_hw2d_surface *src, const hb_hw2d_surface *dst,
                 int sx, int sy, int dx, int dy, int w, int h);

/* Hardware SCALED copy: src(sx,sy,sw,sh) -> dst(dx,dy,dw,dh) via the dispatcher
   (routes to the hardware scaler). Drain with hb_compositor_flush_dispatch(). */
int hb_compositor_blit_scaled(const hb_hw2d_surface *src, const hb_hw2d_surface *dst,
                        int sx, int sy, int sw, int sh,
                        int dx, int dy, int dw, int dh);

/* Hardware alpha-over blit: composite src (with per-pixel alpha) onto dst.
   EXPERIMENTAL — uses parametric-blend src-over params (see hb_compositor.c). */
int hb_compositor_blit_blend(const hb_hw2d_surface *src, const hb_hw2d_surface *dst,
                       int sx, int sy, int dx, int dy, int w, int h, uint8_t global_alpha);

/* Block until all submitted GPU 2D ops have completed (needed before the CPU
   reads/flips the destination buffer). */
void hb_compositor_flush(void);

/* Allocate a GPU-mapped ARGB8888 pixel buffer the 2D engine can target (an
   ordinary heap buffer can't be). `cached` = fast CPU access but you must
   hb_compositor_pixbuf_flush() around 2D ops; uncached = coherent but slow CPU access.
   Returns an opaque handle (NULL on failure) and sets *out_pixels to the w*h*4
   pixel buffer (stride w*4). Use the pixels as an hb_hw2d_surface base. */
void *hb_compositor_pixbuf_create(int w, int h, int cached, void **out_pixels);

/* Clean the CPU cache for a cached pixbuf (CPU writes -> visible to GPU, and GPU
   writes -> readable by CPU). Safe no-op concept for uncached buffers. */
void hb_compositor_pixbuf_flush(void *pb);

#endif /* HB_COMPOSITOR_H */
