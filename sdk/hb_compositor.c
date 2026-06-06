/*
 * hb_compositor.c — hardware 2D (fill/blit) via the OS's 2D dispatcher on the GPU.
 *
 */
#include "hb_compositor.h"

#define FN_THUMB(addr) ((addr) | 1u)

#define ADDR_DISP_GETINST    0x08415c74u
#define ADDR_DISP_FILL       0x081ba50au
#define ADDR_DISP_BLIT       0x081ba4eeu   /* dispatcher blit (auto-routes scaler/GPU) */
#define ADDR_DISP_FLUSH      0x081ba114u
#define ADDR_MEMINFO_CTOR  0x08417e96u
#define ADDR_FILLINFO_CTOR 0x080674b8u
#define ADDR_BLITINFO_CTOR 0x0809c2fau
/* GPU-mapped pixel buffer (the 2D engine can target it; a heap buffer can't) */
#define ADDR_OPNEW         0x0842d444u   /* heap alloc (size)  */
#define ADDR_PB_CTOR       0x084158d0u   /* pixel-buffer ctor: format, h, w, stride, strategy, align, cache */
#define ADDR_PB_FLUSH      0x084117d4u   /* pixel-buffer flush — cache clean */
/* Call the low-level GPU 2D engine directly. The dispatcher fill routes a >=4px
   fill to the hardware scaler, which appears to need a physical buffer addr ->
   crash. The low-level engine maps any CPU buffer into the GPU MMU, so its fill
   works on hb_gpu_framebuffer(). */
#define ADDR_GPU2D_GETINST 0x08402074u   /* low-level 2D engine getter */
#define ADDR_GPU2D_FILL    0x08480680u   /* low-level 2D fill (0x0841c192 was a
                                            mislabeled bool getter, never a fill) */
#define ADDR_GPU2D_BLIT    0x08480604u   /* low-level 2D blit */
#define ADDR_GPU2D_FLUSH   0x0847fff4u   /* low-level 2D flush-blocking */

/* confirmed byte offsets */
#define MI_BASE     0x00u
#define MI_STRIDE   0x04u
#define MI_FORMAT   0x08u
#define MI_SIZE     0x14u
/* The fill-descriptor's color/alpha blend-mode bytes are adjacent u8 enums at the
   head of the struct. The ctor zeros both (= src-copy); a solid fill MUST set them to
   Fill, else the engine builds a source-read GPU cmd (wedges the worker) — matching
   what the OS's own buffer-clear does. */
#define FI_BLEND_COLOR 0x00u
#define FI_BLEND_ALPHA 0x01u
#define FI_FILLCOLOR 0x34u
#define FI_DST_X     0x38u
#define FI_DST_Y     0x3cu
#define FI_DST_W     0x40u
#define FI_DST_H     0x44u
/* Blit descriptor: shares blend-op@0 / rop3@0x30 with the fill descriptor, then
   filter-alpha@0x40, src-rect@0x44, dst-rect@0x54. The rects are {x,y,width,height}
   (the blit reads width/height/x/y) — NOT top/left/bottom/right. Flags by reference,
   same as fill. Scaling happens iff src w/h != dst w/h. */
#define BI_FILTERALPHA  0x40u
#define BI_SRC_X        0x44u
#define BI_SRC_Y        0x48u
#define BI_SRC_W        0x4cu
#define BI_SRC_H        0x50u
#define BI_DST_X        0x54u
#define BI_DST_Y        0x58u
#define BI_DST_W        0x5cu
#define BI_DST_H        0x60u

/* blend-op@0 (48B): color-mode@0(u8) alpha-mode@1(u8), src-param@4 + dst-param@24,
   each {color-param@0 color-op@4 alpha-param@8 alpha-op@12 global@16}. We build the
   OS's per-pixel*global src-over blend-op by hand — the OS's own builder can't be
   called from our context (its lazy static-init faults off a raw loader pthread). The
   parameter values (below) are the OS's per-pixel*global src-over coefficients:
   src color/alpha scaled by (src-alpha * global-alpha), dst by its inverse;
   global.a = layer opacity. */
#define BO_COLOR_MODE     0x00u
#define BO_ALPHA_MODE     0x01u
#define BO_SRC_COLORPARAM 0x04u
#define BO_SRC_COLOROP    0x08u
#define BO_SRC_ALPHAPARAM 0x0cu
#define BO_SRC_ALPHAOP    0x10u
#define BO_SRC_GLOBAL     0x14u
#define BO_DST_COLORPARAM 0x18u
#define BO_DST_COLOROP    0x1cu
#define BO_DST_ALPHAPARAM 0x20u
#define BO_DST_ALPHAOP    0x24u
#define BO_DST_GLOBAL     0x28u
#define COLORPARAM_COLOR        1u
#define COLOROP_SRCMULTGLOBAL   6u
#define COLOROP_INV_SRCMULTGLOB (6u | 0x80000000u)
#define ALPHAPARAM_ONE          0u
#define ALPHAPARAM_DSTALPHA     2u
#define ALPHAOP_SRCMULTGLOBAL   4u
#define ALPHAOP_INV_SRCMULTGLOB (4u | 0x80000000u)

/* The flags arg is a 4-byte struct, but the fill/blit entries take it BY REFERENCE
   (the callee derefs r3) — passing the integer 0 makes r3 a NULL pointer -> fill
   does ldr [0] -> crash. Always pass &(a zeroed flags word). */
typedef void *(*disp_getinst_t)(void);
typedef void  (*ctor_t)(void *thiz);
typedef int   (*disp_fill_t)(void *thiz, const void *dest, const void *params, const void *flags);
typedef int   (*disp_blit_t)(void *thiz, const void *src, const void *dest,
                           const void *params, const void *flags);
typedef int   (*disp_flush_t)(void *thiz);

static inline void wr32(void *p, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)((uint8_t *)p + off) = v;
}

static uint32_t fmt_bpp(uint32_t fmt)
{
    switch (fmt) {
    case HB_HW2D_FMT_RGB565: return 2;
    case HB_HW2D_FMT_RGB888: return 3;
    default:               return 4;  /* ARGB8888 */
    }
}

static void *disp_inst(void)
{
    return ((disp_getinst_t)FN_THUMB(ADDR_DISP_GETINST))();
}

/* Build a memory descriptor in `buf` (>=48 bytes) via the OS ctor, then patch the
   buffer fields. */
static void build_meminfo(void *buf, const hb_hw2d_surface *s)
{
    int i;
    for (i = 0; i < 48; i++) ((uint8_t *)buf)[i] = 0;
    ((ctor_t)FN_THUMB(ADDR_MEMINFO_CTOR))(buf);
    wr32(buf, MI_BASE,   (uint32_t)(uintptr_t)s->base);
    wr32(buf, MI_STRIDE, s->stride);
    wr32(buf, MI_FORMAT, s->fmt);
    wr32(buf, MI_SIZE,   s->stride * s->h);
}

bool hb_compositor_available(void)
{
    return disp_inst() != 0;
}

void hb_compositor_surface_init(hb_hw2d_surface *s, void *base, uint32_t w, uint32_t h, uint32_t fmt)
{
    s->base   = base;
    s->w      = w;
    s->h      = h;
    s->fmt    = fmt;
    s->stride = w * fmt_bpp(fmt);
}

/* Build a solid-fill descriptor (mi[48], fi[128]) for dst rect (x,y,w,h)/argb. */
static void build_fill(uint8_t *mi, uint8_t *fi, const hb_hw2d_surface *dst,
                       int x, int y, int w, int h, uint32_t argb)
{
    int i;
    build_meminfo(mi, dst);
    for (i = 0; i < 128; i++) fi[i] = 0;
    ((ctor_t)FN_THUMB(ADDR_FILLINFO_CTOR))(fi);   /* rop3=copy(0xCC), blend=src-copy(0) */
    fi[FI_BLEND_COLOR] = HB_HW2D_BLEND_FILL;        /* must override the ctor default */
    fi[FI_BLEND_ALPHA] = HB_HW2D_BLEND_FILL;
    wr32(fi, FI_FILLCOLOR, argb);
    wr32(fi, FI_DST_X, (uint32_t)x);
    wr32(fi, FI_DST_Y, (uint32_t)y);
    wr32(fi, FI_DST_W, (uint32_t)w);
    wr32(fi, FI_DST_H, (uint32_t)h);
}

/* All ops go through the OS's 2D dispatcher — its 2D entry, which auto-routes
   each op to the hardware scaler / GPU 2D / software and manages its own
   acquire+flush. We stay at this layer (not the lower-level engine) so routing,
   context and flush are the OS's problem, not ours. */
int hb_compositor_fill(const hb_hw2d_surface *dst, int x, int y, int w, int h, uint32_t argb)
{
    uint8_t mi[48];
    uint8_t fi[128];   /* generously sized; OS ctor inits, we patch */
    void *disp = disp_inst();
    uint32_t flags = 0;   /* flags(0); passed BY REFERENCE (see typedef) */
    if (!disp) return -1;

    build_fill(mi, fi, dst, x, y, w, h, argb);
    return ((disp_fill_t)FN_THUMB(ADDR_DISP_FILL))(disp, mi, fi, &flags);
}

/* Build a blit descriptor (bi[128]) copying src(sx,sy,sw,sh) -> dst(dx,dy,dw,dh). Rects
   are {x,y,width,height}; scaling happens iff sizes differ. blend stays src-copy. */
static void build_blit(uint8_t *bi, int sx, int sy, int sw, int sh,
                       int dx, int dy, int dw, int dh)
{
    int i;
    for (i = 0; i < 128; i++) bi[i] = 0;
    ((ctor_t)FN_THUMB(ADDR_BLITINFO_CTOR))(bi);   /* blend=src-copy, rop3=copy(0xCC) */
    bi[BI_FILTERALPHA] = 0xff;
    wr32(bi, BI_SRC_X, (uint32_t)sx);
    wr32(bi, BI_SRC_Y, (uint32_t)sy);
    wr32(bi, BI_SRC_W, (uint32_t)sw);
    wr32(bi, BI_SRC_H, (uint32_t)sh);
    wr32(bi, BI_DST_X, (uint32_t)dx);
    wr32(bi, BI_DST_Y, (uint32_t)dy);
    wr32(bi, BI_DST_W, (uint32_t)dw);
    wr32(bi, BI_DST_H, (uint32_t)dh);
}

/* Hardware opaque 1:1 copy via the dispatcher blit (it picks GPU for a same-size
   copy). Drain with hb_compositor_flush(). For a SCALED copy use
   hb_compositor_blit_scaled() (the dispatcher routes to the scaler). */
int hb_compositor_blit(const hb_hw2d_surface *src, const hb_hw2d_surface *dst,
                 int sx, int sy, int dx, int dy, int w, int h)
{
    uint8_t mis[48], mid[48], bi[128];
    void *disp = disp_inst();
    uint32_t flags = 0;   /* flags(0); BY REFERENCE */
    if (!disp) return -1;

    build_meminfo(mis, src);
    build_meminfo(mid, dst);
    build_blit(bi, sx, sy, w, h, dx, dy, w, h);   /* 1:1 (same src/dst size) */
    return ((disp_blit_t)FN_THUMB(ADDR_DISP_BLIT))(disp, mis, mid, bi, &flags);
}

/* Hardware copy with scaling (src sw*sh -> dst dw*dh) via the dispatcher blit, which
   routes to the hardware scaler when the sizes differ. Drain with hb_compositor_flush(). */
int hb_compositor_blit_scaled(const hb_hw2d_surface *src, const hb_hw2d_surface *dst,
                        int sx, int sy, int sw, int sh,
                        int dx, int dy, int dw, int dh)
{
    uint8_t mis[48], mid[48], bi[128];
    void *disp = disp_inst();
    uint32_t flags = 0;
    if (!disp) return -1;

    build_meminfo(mis, src);
    build_meminfo(mid, dst);
    build_blit(bi, sx, sy, sw, sh, dx, dy, dw, dh);
    return ((disp_blit_t)FN_THUMB(ADDR_DISP_BLIT))(disp, mis, mid, bi, &flags);
}

/* Src-over alpha blit: composite src (per-pixel alpha) onto dst, scaled by
   global_alpha (255 = per-pixel alpha only; <255 = also a layer opacity). Builds
   the OS's per-pixel*global src-over blend-op into the blit descriptor by hand. */
int hb_compositor_blit_blend(const hb_hw2d_surface *src, const hb_hw2d_surface *dst,
                       int sx, int sy, int dx, int dy, int w, int h, uint8_t global_alpha)
{
    uint8_t mis[48], mid[48], bi[128];
    void *disp = disp_inst();
    uint32_t flags = 0;
    uint32_t galpha = (uint32_t)global_alpha << 24;   /* ARGB; .byte.a = opacity */
    if (!disp) return -1;

    build_meminfo(mis, src);
    build_meminfo(mid, dst);
    build_blit(bi, sx, sy, w, h, dx, dy, w, h);   /* 1:1; rects + filterAlpha set */

    /* overwrite blend-op (was src-copy) with per-pixel*global src-over */
    bi[BO_COLOR_MODE] = HB_HW2D_BLEND_BLEND;
    bi[BO_ALPHA_MODE] = HB_HW2D_BLEND_BLEND;
    wr32(bi, BO_SRC_COLORPARAM, COLORPARAM_COLOR);
    wr32(bi, BO_SRC_COLOROP,    COLOROP_SRCMULTGLOBAL);
    wr32(bi, BO_SRC_ALPHAPARAM, ALPHAPARAM_ONE);
    wr32(bi, BO_SRC_ALPHAOP,    ALPHAOP_SRCMULTGLOBAL);
    wr32(bi, BO_SRC_GLOBAL,     galpha);
    wr32(bi, BO_DST_COLORPARAM, COLORPARAM_COLOR);
    wr32(bi, BO_DST_COLOROP,    COLOROP_INV_SRCMULTGLOB);
    wr32(bi, BO_DST_ALPHAPARAM, ALPHAPARAM_DSTALPHA);
    wr32(bi, BO_DST_ALPHAOP,    ALPHAOP_INV_SRCMULTGLOB);
    wr32(bi, BO_DST_GLOBAL,     galpha);

    return ((disp_blit_t)FN_THUMB(ADDR_DISP_BLIT))(disp, mis, mid, bi, &flags);
}

/* Block until submitted 2D ops complete (the dispatcher's flush-blocking drains
   whichever engine(s) it routed to). Needed before the CPU reads/flips the dst. */
void hb_compositor_flush(void)
{
    void *disp = disp_inst();
    if (disp) ((disp_flush_t)FN_THUMB(ADDR_DISP_FLUSH))(disp);
}

/* Allocate a GPU-mapped ARGB8888 pixel buffer the 2D engine can target (an ordinary
   heap buffer can't be: the GPU can't resolve a device address for it). `cached`
   true -> CPU writes/reads are cached (fast for a software renderer, but you MUST
   hb_compositor_pixbuf_flush() the dirty region before a 2D op and invalidate before
   a CPU read of 2D output); false -> uncached (coherent, slow CPU access). Returns
   the buffer object (opaque handle) and writes the pixel pointer to *out_pixels;
   NULL on failure. Stride is w*4. */
void *hb_compositor_pixbuf_create(int w, int h, int cached, void **out_pixels)
{
    typedef void *(*opnew_t)(uint32_t);
    typedef void  (*pbctor_t)(void *, int, uint32_t, uint32_t, uint32_t, void *, uint32_t, int);
    typedef void *(*getbuf_t)(void *);
    static uint32_t strat[3];
    void *pb;
    uint32_t *vt;
    if (out_pixels) *out_pixels = 0;
    strat[0] = 0x10;   /* general heap */
    strat[1] = 0;      /* no bitmap heap */
    strat[2] = 5;      /* client = compositor */
    pb = ((opnew_t)FN_THUMB(ADDR_OPNEW))(1024);
    if (!pb) return 0;
    /* pixel-buffer ctor: ARGB8888 (13), height, width, stride, strategy, align 256,
       caching: cached=0 / uncached=1) */
    ((pbctor_t)FN_THUMB(ADDR_PB_CTOR))(pb, HB_HW2D_FMT_ARGB8888, (uint32_t)h, (uint32_t)w,
                                       (uint32_t)(w * 4), strat, 256, cached ? 0 : 1);
    vt = *(uint32_t **)pb;
    if (out_pixels) *out_pixels = ((getbuf_t)vt[2])(pb);   /* vt[2] = aligned-pixel-pointer getter */
    return pb;
}

/* Clean the CPU cache for a cached pixel buffer so the GPU sees CPU writes (and,
   after a 2D write, makes the buffer's contents readable by the CPU). No-op-safe
   for uncached buffers. */
void hb_compositor_pixbuf_flush(void *pb)
{
    typedef void (*flush_t)(void *);
    if (pb) ((flush_t)FN_THUMB(ADDR_PB_FLUSH))(pb);
}
