/*
 * hb_record.c — system-wide screen recorder. See hb_record.h.
 *
 * Capture: the display controller exposes the live framebuffer base + geometry
 * in MMIO regs at 0x38900000 (the same source the screenshot capture reads). We 
 * snapshot the primary display layer into a tightly-packed XRGB8888 buffer —
 * the panel's native format (no RGB565 down-convert -> full color, cheaper copy).
 *
 * Compress: each frame is encoded as skip/copy ops vs the previous frame —
 *   token u16:  bit15=0 -> SKIP `count` pixels (unchanged, take from prev)
 *               bit15=1 -> COPY `count` pixels (literal XRGB8888 values follow)
 * The keyframe diffs against a zeroed prev (so it stores the whole frame). Per-
 * frame records are [u32 oplen][ops] appended to the ring.
 *
 * Flush: replay the ops into one reconstruct buffer (SKIP leaves prior pixels,
 * COPY overwrites), writing each reconstructed frame as a bottom-up 24-bit BMP to
 * /recNNNN/frameNNNN.bmp.
 */
#include "hb_record.h"
#include "hb_sdk.h"
#include "hb_heap.h"

#define DISP_BASE   0x38900000u
#define R_CFG       0x20u   /* format = (cfg>>8)&0xf; 4 = RGB565 */
#define R_BASE      0x24u
#define R_SPAN      0x28u   /* stride in pixels, &0xfff */
#define R_SXY       0x2cu
#define R_WH        0x30u

/* The recording STREAMS to disk: a single work block is filled with the compressed
 * frame stream and flushed to the open /recNNNN.rec when it's full, then reused —
 * so length is disk-bound (15 GB free), not RAM-bound (~6 MB). The only RAM held is
 * this work block + the two frame buffers. The cost is a brief disk-write hitch
 * each time the block fills (rare for well-compressed UI). */
#define WORK_BLOCK    (2u * 1024u * 1024u)    /* flush granularity / RAM held    */
#define MAX_FRAMES    65535u                   /* safety cap                      */
#define REC_HEADER    32u

/* Frames are captured + stored as 32-bit XRGB (0x00RRGGBB) — the native panel
 * format, no RGB565 down-conversion (full color, and the copy is cheaper than
 * converting). Disk-bound, so 2x size vs RGB565 is fine. */
static int       s_active;
static uint8_t  *s_work;
static uint32_t  s_work_cap, s_work_used;
static uint32_t *s_prev, *s_cur;               /* XRGB8888, s_px pixels each      */
static int       s_w, s_h, s_px;             /* geometry; s_px = w*h            */
static uint32_t  s_span;                       /* row stride in pixels            */
static int       s_bpp;                        /* source bytes/px: 2 (RGB565) | 4 (ARGB8888) */
static uint32_t  s_frames;

static uint32_t mmio(uint32_t off) { return *(volatile uint32_t *)(DISP_BASE + off); }

/* Read primary-layer geometry + pixel format. Returns 1 for the formats we capture. */
static int read_geom(void)
{
    uint32_t cfg = mmio(R_CFG), fmt = (cfg >> 8) & 0xfu;
    uint32_t wh  = mmio(R_WH);
    if (fmt == 4u) s_bpp = 2;                  /* RGB565   */
    else if (fmt == 7u) s_bpp = 4;             /* ARGB8888 */
    else return 0;
    s_span = mmio(R_SPAN) & 0xfffu;
    s_w = (int)((wh >> 16) & 0x1ffu);
    s_h = (int)(wh & 0x1ffu);
    s_px = s_w * s_h;
    return (s_w > 0 && s_h > 0 && s_span >= (uint32_t)s_w);
}

/* Snapshot the live framebuffer into `dst` (tightly packed s_w x s_h XRGB8888,
 * alpha masked off). ARGB8888 is copied as-is; an RGB565 source is up-converted.
 *
 * We read the scanout buffer asynchronously while the OS compositor may be
 * updating it, which tears (partial / black regions). Mitigate by reading the
 * current scanout base, copying the frame, then checking the base register: if
 * it FLIPPED during the read, the buffer we read was being recomposed -> retry.
 * A few retries land a frame that stayed stable start-to-finish; after that
 * (very heavy motion) keep the last read rather than drop the frame. */
static void snapshot(uint32_t *dst)
{
    int tries;
    for (tries = 0; tries < 4; tries++) {
        uint32_t base = mmio(R_BASE);
        uint32_t sxy = mmio(R_SXY);
        uint32_t sx = (sxy >> 16) & 0x7ffu, sy = sxy & 0x7ffu;
        int y, x;
        if (s_bpp == 4) {
            const uint32_t *src = (const uint32_t *)(uintptr_t)(base + (sy * s_span + sx) * 4u);
            for (y = 0; y < s_h; y++) {
                const uint32_t *s = src + (uint32_t)y * s_span;
                uint32_t *d = dst + (uint32_t)y * s_w;
                for (x = 0; x < s_w; x++) d[x] = s[x] & 0x00ffffffu;   /* drop alpha */
            }
        } else {
            const uint16_t *src = (const uint16_t *)(uintptr_t)(base + (sy * s_span + sx) * 2u);
            for (y = 0; y < s_h; y++) {
                const uint16_t *s = src + (uint32_t)y * s_span;
                uint32_t *d = dst + (uint32_t)y * s_w;
                for (x = 0; x < s_w; x++) {
                    uint32_t c = s[x], r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
                    d[x] = (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
                }
            }
        }
        if (mmio(R_BASE) == base)              /* no flip during the read -> stable */
            return;
    }
}

int hb_record_active(void) { return s_active; }

/* Next free /recNNNN.rec path. */
static char *u32a(char *p, uint32_t v, int width);   /* (defined below) */
static void put_le32(uint8_t *p, uint32_t v);
static void pick_session_path(char *path);

int hb_record_start(void)
{
    char path[24];
    uint8_t hdr[REC_HEADER];
    int i;
    if (s_active) return 1;
    if (!read_geom()) return 0;

    s_prev = (uint32_t *)hb_os_alloc((uint32_t)s_px * 4u);
    s_cur  = (uint32_t *)hb_os_alloc((uint32_t)s_px * 4u);
    s_work = (uint8_t  *)hb_os_alloc(WORK_BLOCK);
    if (!s_prev || !s_cur || !s_work) { hb_record_stop(); return 0; }

    /* open the .rec and write the header (frames/total left 0 — the splitter reads
     * frames to EOF, since with streaming we don't know the count up front) */
    pick_session_path(path);
    if (!hb_fs_stream_open(path)) { hb_record_stop(); return 0; }
    for (i = 0; i < (int)REC_HEADER; i++) hdr[i] = 0;
    hdr[0]='H'; hdr[1]='B'; hdr[2]='R'; hdr[3]='E'; hdr[4]='C'; hdr[5]='1';
    put_le32(&hdr[8],  (uint32_t)s_w);
    put_le32(&hdr[12], (uint32_t)s_h);
    put_le32(&hdr[24], 4u);                    /* stored bytes/pixel: XRGB8888 (0=legacy RGB565) */
    if (!hb_fs_stream_write(hdr, REC_HEADER)) { hb_record_stop(); return 0; }

    s_work_cap = WORK_BLOCK; s_work_used = 0; s_frames = 0;
    { for (i = 0; i < s_px; i++) s_prev[i] = 0; }   /* keyframe diffs against 0 */
    s_active = 1;
    hb_record_tick();                               /* capture frame 0 now      */
    return s_active;
}

/* Encode s_cur vs s_prev as [u32 oplen][ops] at `base` (cap bytes). Returns the
 * bytes written (incl the 4-byte length prefix), or 0 if it didn't fit. */
static uint32_t encode_into(uint8_t *base, uint32_t cap)
{
    uint8_t *lenp = base, *out = base + 4;
    uint32_t ocap, pos = 0;
    int i = 0, n = s_px;
    if (cap < 4u) return 0;
    ocap = cap - 4u;
    while (i < n) {
        int j = i;
        if (s_cur[i] == s_prev[i]) {                       /* SKIP run            */
            while (j < n && s_cur[j] == s_prev[j] && (j - i) < 0x7fff) j++;
            if (pos + 2u > ocap) return 0;
            out[pos++] = (uint8_t)(j - i);
            out[pos++] = (uint8_t)((j - i) >> 8);
        } else {                                           /* COPY run            */
            uint32_t m;
            while (j < n && s_cur[j] != s_prev[j] && (j - i) < 0x7fff) j++;
            m = (uint32_t)(j - i);
            if (pos + 2u + m * 4u > ocap) return 0;          /* 4 bytes/pixel (XRGB) */
            out[pos++] = (uint8_t)(0x8000u | m);
            out[pos++] = (uint8_t)((0x8000u | m) >> 8);
            { int k; for (k = i; k < j; k++) {
                uint32_t v = s_cur[k];
                out[pos++] = (uint8_t)v;        out[pos++] = (uint8_t)(v >> 8);
                out[pos++] = (uint8_t)(v >> 16); out[pos++] = (uint8_t)(v >> 24);
            } }
        }
        i = j;
    }
    lenp[0] = (uint8_t)pos; lenp[1] = (uint8_t)(pos >> 8);
    lenp[2] = (uint8_t)(pos >> 16); lenp[3] = (uint8_t)(pos >> 24);
    return 4u + pos;
}

/* Append the current frame to the work block; when it's full, flush it to the
 * open .rec and reuse it. 0 = a disk write failed (stop). */
static int append_frame(void)
{
    uint32_t w = encode_into(s_work + s_work_used, s_work_cap - s_work_used);
    if (w) { s_work_used += w; return 1; }
    if (s_work_used) {                         /* block full -> flush to disk     */
        if (!hb_fs_stream_write(s_work, s_work_used)) return 0;
        s_work_used = 0;
    }
    w = encode_into(s_work, s_work_cap);        /* now into the empty block        */
    if (w) { s_work_used += w; return 1; }
    return 0;                                   /* frame bigger than a block (n/a) */
}

void hb_record_tick(void)
{
    if (!s_active) return;
    snapshot(s_cur);
    if (!append_frame()) {                      /* disk error -> stop + flush       */
        hb_record_stop();
        return;
    }
    { uint32_t *t = s_prev; s_prev = s_cur; s_cur = t; }   /* cur becomes prev     */
    s_frames++;
    if (s_frames >= MAX_FRAMES)
        hb_record_stop();
}

static void put_le32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

static char *u32a(char *p, uint32_t v, int width)
{
    char t[12]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n < width) t[n++] = '0';
    while (n) *p++ = t[--n];
    return p;
}

/* Next free /recNNNN.rec path. */
static void pick_session_path(char *path)
{
    uint32_t n;
    for (n = 0; n < 10000u; n++) {
        char *p = path; const char *k = "/rec";
        while (*k) *p++ = *k++;
        p = u32a(p, n, 4);
        *p++='.'; *p++='r'; *p++='e'; *p++='c'; *p = 0;
        if (!hb_fs_exists(path)) return;
    }
}

static void free_buffers(void)
{
    if (s_cur)  { hb_os_free(s_cur);  s_cur = 0; }
    if (s_prev) { hb_os_free(s_prev); s_prev = 0; }
    if (s_work) { hb_os_free(s_work); s_work = 0; }
    s_work_used = s_work_cap = 0;
}

int hb_record_stop(void)
{
    int saved = 0;
    if (!s_active) {                            /* cleanup (start-failure / idle)  */
        hb_fs_stream_close();
        free_buffers();
        return 0;
    }
    s_active = 0;
    if (s_work && s_work_used)                  /* flush the final partial block   */
        hb_fs_stream_write(s_work, s_work_used);
    hb_fs_stream_close();
    saved = (int)s_frames;
    free_buffers();
    s_frames = 0;
    return saved;
}
