/*
 * hb_mipi.c — direct MIPI DSIM MMIO driver for N7G LCD.
 *
 * Writes to the display DSIM controller at 0x3d800000 to send DCS
 * commands directly to the LCD panel. Used by hb_fill_rect /
 * hb_fill_screen.
 *
 * Pixel format: assumes the OS has set 24bpp RGB888 (DCS 0x3A = 0x77),
 * which is N7G's default. We write 3 bytes per pixel (R, G, B).
 */

#include "hb_sdk.h"

/* Freestanding build has no libc. Compiler still synthesizes memset
   for zero-init patterns. Provide it here so the SDK supplies it to
   any app that needs it. Weak so apps that already ship their own
   memset/memcpy (e.g. clock.c, paint.c) keep working unchanged. */
__attribute__((weak)) void *memset(void *s, int c, unsigned int n)
{
    unsigned char *p = (unsigned char *)s;
    for (unsigned i = 0; i < n; i++) p[i] = (unsigned char)c;
    return s;
}

__attribute__((weak)) void *memcpy(void *dst, const void *src, unsigned int n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (unsigned i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

#define MIPI_BASE     0x3d800000u
#define DSIM_CONFIG   (*(volatile uint32_t *)(MIPI_BASE + 0x10))
#define DSIM_PKTHDR   (*(volatile uint32_t *)(MIPI_BASE + 0x34))
#define DSIM_PAYLOAD  (*(volatile uint32_t *)(MIPI_BASE + 0x38))
#define DSIM_FIFOCTRL (*(volatile uint32_t *)(MIPI_BASE + 0x44))

#define DSIM_FullLSfr        (1u << 21)
#define DSIM_EmptyHSfr       (1u << 22)
#define DSIM_EmptyLSfr       (1u << 20)
#define DSIM_TYPE_LONG_WRITE 0x29

/* DSIM FIFO waits.
   Hardware-timer wall-clock timeouts so the bound stays predictable
   regardless of CPU speed or compiler choices. In healthy operation
   the FIFO drains in <1 µs, so the loop exits on its first or second
   iteration; the timeout only fires if the DSI has actually wedged,
   and we'd rather give up after a frame hitch than spin forever.
   These timer reads are cheap (one MMIO load, same cost class as the
   FIFOCTRL read itself). */
#define HB_DSIM_DRAIN_TIMEOUT_US  20000u   /* full long-packet completion */
#define HB_DSIM_FIFO_TIMEOUT_US    5000u   /* per-word FIFO room */

static inline void wait_long_done(void) {
    uint32_t start = hb_time_uptime_us();
    while ((DSIM_FIFOCTRL & (DSIM_EmptyHSfr | DSIM_EmptyLSfr))
           != (DSIM_EmptyHSfr | DSIM_EmptyLSfr)) {
        if ((uint32_t)(hb_time_uptime_us() - start) > HB_DSIM_DRAIN_TIMEOUT_US) return;
    }
}
static inline void wait_payload_fifo_space(void) {
    uint32_t start = hb_time_uptime_us();
    while (DSIM_FIFOCTRL & DSIM_FullLSfr) {
        if ((uint32_t)(hb_time_uptime_us() - start) > HB_DSIM_FIFO_TIMEOUT_US) return;
    }
}

/* Long-write packet payload cap: must fit the DSI payload SFR FIFO.
   Empirical ceiling found by bisection: 680 holds, 685+ crashes the
   device. At 3 bytes/pixel + 1 DCS cmd byte that's a 2041-byte
   packet — the FIFO is somewhere just above that.
   Cuts a full 240×432 flush from ~210 packets (CHUNK=500) to ~153. */
#define HB_MIPI_CHUNK 680u

/* Push a long-write packet to the LCD: CDS payload bytes followed by
   the long-write packet header that triggers transmission.
   Pixel data must already be in `data[0..length)`. */
static void mipi_long(const uint8_t *data, uint32_t length) {
    DSIM_CONFIG &= ~(1u << 28);
    uint32_t payload = 0; uint32_t i;
    for (i = 0; i < length; i++) {
        payload = (payload >> 8) | ((uint32_t)data[i] << 24);
        if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; }
    }
    uint32_t rem = i & 3;
    if (rem) {
        wait_payload_fifo_space();
        if (rem == 3)      DSIM_PAYLOAD = (payload >> 8);
        else if (rem == 2) DSIM_PAYLOAD = (payload >> 16);
        else if (rem == 1) DSIM_PAYLOAD = (payload >> 24);
    }
    DSIM_PKTHDR = DSIM_TYPE_LONG_WRITE | ((length & 0xFFFFu) << 8);
    wait_long_done();
}

/* Set the LCD's drawable window via DCS SET_COLUMN_ADDRESS (0x2A) and
   SET_PAGE_ADDRESS (0x2B). All subsequent writes target this region. */
static void mipi_set_window(int16_t x, int16_t y, int16_t w, int16_t h) {
    int16_t x1 = x + w - 1, y1 = y + h - 1;
    uint8_t bc[5] = { 0x2A, (uint8_t)((x  >> 8) & 0xFF), (uint8_t)(x  & 0xFF),
                            (uint8_t)((x1 >> 8) & 0xFF), (uint8_t)(x1 & 0xFF) };
    mipi_long(bc, 5);
    uint8_t br[5] = { 0x2B, (uint8_t)((y  >> 8) & 0xFF), (uint8_t)(y  & 0xFF),
                            (uint8_t)((y1 >> 8) & 0xFF), (uint8_t)(y1 & 0xFF) };
    mipi_long(br, 5);
}

/* Push a chunk of solid-color pixels. cmd is 0x2C (MEMORY_WRITE_START)
   for the first chunk in a window, 0x3C (MEMORY_WRITE_CONTINUE) after.
   Limit ~600 pixels/chunk to stay under FIFO size. */
static void mipi_fill_chunk(uint8_t cmd_byte, hb_color_t color, uint32_t n_pixels) {
    uint32_t total_len = 1u + n_pixels * 3u;
    DSIM_CONFIG &= ~(1u << 28);
    uint32_t payload = 0;
    uint32_t i = 0;
    uint8_t r = (uint8_t)(color >> 16);
    uint8_t g = (uint8_t)(color >> 8);
    uint8_t b = (uint8_t)color;

    payload = (payload >> 8) | ((uint32_t)cmd_byte << 24);
    i++;
    for (uint32_t p = 0; p < n_pixels; p++) {
        payload = (payload >> 8) | ((uint32_t)r << 24);
        if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; }
        i++;
        payload = (payload >> 8) | ((uint32_t)g << 24);
        if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; }
        i++;
        payload = (payload >> 8) | ((uint32_t)b << 24);
        if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; }
        i++;
    }
    uint32_t rem = i & 3;
    if (rem) {
        wait_payload_fifo_space();
        if (rem == 3)      DSIM_PAYLOAD = (payload >> 8);
        else if (rem == 2) DSIM_PAYLOAD = (payload >> 16);
        else if (rem == 1) DSIM_PAYLOAD = (payload >> 24);
    }
    DSIM_PKTHDR = DSIM_TYPE_LONG_WRITE | ((total_len & 0xFFFFu) << 8);
    wait_long_done();
}

void hb_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, hb_color_t color) {
    mipi_set_window(x, y, w, h);
    const uint32_t total = (uint32_t)w * (uint32_t)h, CHUNK = 600;
    uint32_t pushed = 0;
    int first = 1;
    while (pushed < total) {
        uint32_t n = (total - pushed > CHUNK) ? CHUNK : (total - pushed);
        mipi_fill_chunk(first ? 0x2C : 0x3C, color, n);
        pushed += n;
        first = 0;
    }
}

void hb_fill_screen(hb_color_t color) {
    hb_fill_rect(0, 0, HB_SCREEN_W, HB_SCREEN_H, color);
}

/* Used by hb_text.c: push one horizontal strip (HB_DIGIT_W pixels wide,
   HB_TEXT_SCALE pixels tall) of foreground/background pixels driven by
   one byte of glyph bitmap. Single MIPI long-write per strip. */
void hb_text_push_strip(uint8_t cmd, uint8_t row_byte,
                        hb_color_t fg, hb_color_t bg) {
    const uint32_t strip_pixels = HB_DIGIT_W * HB_TEXT_SCALE;
    const uint32_t total_len = 1u + strip_pixels * 3u;
    DSIM_CONFIG &= ~(1u << 28);
    uint32_t payload = 0; uint32_t i = 0;
    uint8_t fr=(fg>>16), fgg=(fg>>8), fb=(uint8_t)fg;
    uint8_t br=(bg>>16), bgg=(bg>>8), bb=(uint8_t)bg;

    payload = (payload >> 8) | ((uint32_t)cmd << 24); i++;
    for (int sub_v = 0; sub_v < HB_TEXT_SCALE; sub_v++) {
        for (int bit = 7; bit >= 0; bit--) {
            int on = (row_byte >> bit) & 1;
            uint8_t r = on ? fr  : br;
            uint8_t g = on ? fgg : bgg;
            uint8_t b = on ? fb  : bb;
            for (int sc = 0; sc < HB_TEXT_SCALE; sc++) {
                payload = (payload >> 8) | ((uint32_t)r << 24);
                if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
                payload = (payload >> 8) | ((uint32_t)g << 24);
                if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
                payload = (payload >> 8) | ((uint32_t)b << 24);
                if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
            }
        }
    }
    uint32_t rem = i & 3;
    if (rem) {
        wait_payload_fifo_space();
        if (rem == 3)      DSIM_PAYLOAD = (payload >> 8);
        else if (rem == 2) DSIM_PAYLOAD = (payload >> 16);
        else if (rem == 1) DSIM_PAYLOAD = (payload >> 24);
    }
    DSIM_PKTHDR = DSIM_TYPE_LONG_WRITE | ((total_len & 0xFFFFu) << 8);
    wait_long_done();
}

/* Helper only visible to hb_text.c */
void hb_mipi_set_window(int16_t x, int16_t y, int16_t w, int16_t h) {
    mipi_set_window(x, y, w, h);
}

/* ---- VSYNC poll ----
   The N7G LCM exposes its tearing-effect signal on GPIO 1.6 (PDAT
   group 1, bit 6 — see iPod OS board glue / GPIO_LCM_INT def).
   Pin is LOW during the vblank interval (LCD not scanning out) and
   HIGH while pixels are being driven to the panel. Pushing new pixel
   data while HIGH causes visible tearing. We poll for the falling
   edge to align writes with the next vblank.

   Cheap: just MMIO reads on GPIO_BASE + 0x0024. Caps at ~25 ms so
   we don't wedge if the panel ever stops generating vsync. */
#define HB_GPIO_PDAT1   (*(volatile uint32_t *)0x3CF00024u)
#define HB_VSYNC_BIT    (1u << 6)
#define HB_VSYNC_TIMEOUT_US  25000u

void hb_display_wait_vsync(void)
{
    uint32_t start = hb_time_uptime_us();
    /* If already in vblank (pin LOW), return immediately — back-to-
       back flushes within one vblank window all proceed without
       paying another full 16.7 ms wait. Only wait when we'd
       otherwise stomp the LCD's active scan. */
    if ((HB_GPIO_PDAT1 & HB_VSYNC_BIT) == 0) return;
    /* Active scan: wait for the next falling edge. */
    while ((HB_GPIO_PDAT1 & HB_VSYNC_BIT) != 0) {
        if ((uint32_t)(hb_time_uptime_us() - start) > HB_VSYNC_TIMEOUT_US) return;
    }
}

/* Stream a raw RGB888 region to the LCD as MIPI long-writes. Used by
   the LVGL flush callback and other bulk-pixel paths. Each pixel is
   3 bytes in `src` (R,G,B, row-major, no padding). Chunked at ~500 px
   per packet to stay inside the FIFO budget. */
void hb_mipi_blit_rgb888(int16_t x, int16_t y, int16_t w, int16_t h,
                         const uint8_t *src) {
    if (w <= 0 || h <= 0 || !src) return;
    mipi_set_window(x, y, w, h);

    const uint32_t total = (uint32_t)w * (uint32_t)h;
    const uint32_t CHUNK = HB_MIPI_CHUNK;
    uint32_t pushed = 0;
    int first = 1;
    while (pushed < total) {
        uint32_t n = (total - pushed > CHUNK) ? CHUNK : (total - pushed);
        uint32_t total_len = 1u + n * 3u;
        DSIM_CONFIG &= ~(1u << 28);
        uint32_t payload = 0; uint32_t i = 0;
        uint8_t cmd = first ? 0x2C : 0x3C;
        payload = (payload >> 8) | ((uint32_t)cmd << 24); i++;
        const uint8_t *p = src + pushed * 3u;
        for (uint32_t k = 0; k < n; k++) {
            payload = (payload >> 8) | ((uint32_t)p[0] << 24);
            if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
            payload = (payload >> 8) | ((uint32_t)p[1] << 24);
            if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
            payload = (payload >> 8) | ((uint32_t)p[2] << 24);
            if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
            p += 3;
        }
        uint32_t rem = i & 3;
        if (rem) {
            wait_payload_fifo_space();
            if (rem == 3)      DSIM_PAYLOAD = (payload >> 8);
            else if (rem == 2) DSIM_PAYLOAD = (payload >> 16);
            else if (rem == 1) DSIM_PAYLOAD = (payload >> 24);
        }
        DSIM_PKTHDR = DSIM_TYPE_LONG_WRITE | ((total_len & 0xFFFFu) << 8);
        wait_long_done();

        pushed += n;
        first = 0;
    }
}

/* Stream an XRGB8888 source region to the LCD. Each source pixel is
   4 bytes (B, G, R, X) in little-endian XRGB8888 (LVGL's internal
   layout when LV_COLOR_DEPTH=32). We pack 3 bytes per pixel into the
   MIPI long-write payload. */
void hb_mipi_blit_xrgb8888(int16_t x, int16_t y, int16_t w, int16_t h,
                           const uint32_t *src) {
    if (w <= 0 || h <= 0 || !src) return;
    mipi_set_window(x, y, w, h);

    const uint32_t total = (uint32_t)w * (uint32_t)h;
    const uint32_t CHUNK = HB_MIPI_CHUNK;
    uint32_t pushed = 0;
    int first = 1;
    while (pushed < total) {
        uint32_t n = (total - pushed > CHUNK) ? CHUNK : (total - pushed);
        uint32_t total_len = 1u + n * 3u;
        DSIM_CONFIG &= ~(1u << 28);
        uint32_t payload = 0; uint32_t i = 0;
        uint8_t cmd = first ? 0x2C : 0x3C;
        payload = (payload >> 8) | ((uint32_t)cmd << 24); i++;
        const uint32_t *p = src + pushed;
        for (uint32_t k = 0; k < n; k++) {
            uint32_t v = p[k];
            uint8_t r = (uint8_t)(v >> 16);
            uint8_t g = (uint8_t)(v >> 8);
            uint8_t b = (uint8_t)v;
            payload = (payload >> 8) | ((uint32_t)r << 24);
            if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
            payload = (payload >> 8) | ((uint32_t)g << 24);
            if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
            payload = (payload >> 8) | ((uint32_t)b << 24);
            if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
        }
        uint32_t rem = i & 3;
        if (rem) {
            wait_payload_fifo_space();
            if (rem == 3)      DSIM_PAYLOAD = (payload >> 8);
            else if (rem == 2) DSIM_PAYLOAD = (payload >> 16);
            else if (rem == 1) DSIM_PAYLOAD = (payload >> 24);
        }
        DSIM_PKTHDR = DSIM_TYPE_LONG_WRITE | ((total_len & 0xFFFFu) << 8);
        wait_long_done();

        pushed += n;
        first = 0;
    }
}

/* Same, but source is RGB565. Converts each 16-bit pixel to RGB888 on
   the fly. Most efficient for LVGL since LVGL renders to 16bpp buffer. */
void hb_mipi_blit_rgb565(int16_t x, int16_t y, int16_t w, int16_t h,
                         const uint16_t *src) {
    if (w <= 0 || h <= 0 || !src) return;
    mipi_set_window(x, y, w, h);

    const uint32_t total = (uint32_t)w * (uint32_t)h;
    const uint32_t CHUNK = HB_MIPI_CHUNK;
    uint32_t pushed = 0;
    int first = 1;
    while (pushed < total) {
        uint32_t n = (total - pushed > CHUNK) ? CHUNK : (total - pushed);
        uint32_t total_len = 1u + n * 3u;
        DSIM_CONFIG &= ~(1u << 28);
        uint32_t payload = 0; uint32_t i = 0;
        uint8_t cmd = first ? 0x2C : 0x3C;
        payload = (payload >> 8) | ((uint32_t)cmd << 24); i++;
        const uint16_t *p = src + pushed;
        for (uint32_t k = 0; k < n; k++) {
            uint16_t v = p[k];
            uint8_t r = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
            uint8_t g = (uint8_t)(((v >> 5)  & 0x3F) * 255 / 63);
            uint8_t b = (uint8_t)(( v        & 0x1F) * 255 / 31);
            payload = (payload >> 8) | ((uint32_t)r << 24);
            if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
            payload = (payload >> 8) | ((uint32_t)g << 24);
            if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
            payload = (payload >> 8) | ((uint32_t)b << 24);
            if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
        }
        uint32_t rem = i & 3;
        if (rem) {
            wait_payload_fifo_space();
            if (rem == 3)      DSIM_PAYLOAD = (payload >> 8);
            else if (rem == 2) DSIM_PAYLOAD = (payload >> 16);
            else if (rem == 1) DSIM_PAYLOAD = (payload >> 24);
        }
        DSIM_PKTHDR = DSIM_TYPE_LONG_WRITE | ((total_len & 0xFFFFu) << 8);
        wait_long_done();

        pushed += n;
        first = 0;
    }
}

/* Blit a grayscale bitmap glyph (w*h bytes, 8bpp) to the LCD. Thresholds
   each source byte: >= threshold draws fg, else (if draw_bg) draws bg
   else SKIPPED (so under-text isn't clobbered). Chunks at ~400 px to
   stay under FIFO size.
   Drawing pixels in raster scan order with set_window'd target rect. */
void hb_mipi_blit_glyph(int16_t x, int16_t y,
                        int16_t w, int16_t h,
                        const uint8_t *gray, uint8_t threshold,
                        hb_color_t fg, hb_color_t bg, int draw_bg)
{
    if (w <= 0 || h <= 0) return;
    mipi_set_window(x, y, w, h);

    const uint32_t total = (uint32_t)w * (uint32_t)h;
    const uint32_t CHUNK = 400;
    uint32_t pushed = 0;
    int first = 1;
    uint8_t fr=(uint8_t)(fg>>16), fgg=(uint8_t)(fg>>8), fb=(uint8_t)fg;
    uint8_t br=(uint8_t)(bg>>16), bgg=(uint8_t)(bg>>8), bb=(uint8_t)bg;

    /* If draw_bg is false and any pixel falls below threshold, we'd
       need to leave the existing framebuffer alone — but DSIM can't
       "skip" pixels in a memory_write. Workaround: substitute the
       caller's bg color for under-threshold pixels even when
       draw_bg=false (caller passes fg/bg matching their backdrop). */

    while (pushed < total) {
        uint32_t n = (total - pushed > CHUNK) ? CHUNK : (total - pushed);
        uint32_t total_len = 1u + n * 3u;
        DSIM_CONFIG &= ~(1u << 28);
        uint32_t payload = 0; uint32_t i = 0;
        uint8_t cmd = first ? 0x2C : 0x3C;
        payload = (payload >> 8) | ((uint32_t)cmd << 24); i++;

        for (uint32_t p = 0; p < n; p++) {
            uint8_t v = gray[pushed + p];
            int on = (v >= threshold);
            uint8_t r = on ? fr  : br;
            uint8_t g = on ? fgg : bgg;
            uint8_t b = on ? fb  : bb;
            payload = (payload >> 8) | ((uint32_t)r << 24);
            if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
            payload = (payload >> 8) | ((uint32_t)g << 24);
            if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
            payload = (payload >> 8) | ((uint32_t)b << 24);
            if ((i & 3) == 3) { wait_payload_fifo_space(); DSIM_PAYLOAD = payload; } i++;
        }
        uint32_t rem = i & 3;
        if (rem) {
            wait_payload_fifo_space();
            if (rem == 3)      DSIM_PAYLOAD = (payload >> 8);
            else if (rem == 2) DSIM_PAYLOAD = (payload >> 16);
            else if (rem == 1) DSIM_PAYLOAD = (payload >> 24);
        }
        DSIM_PKTHDR = DSIM_TYPE_LONG_WRITE | ((total_len & 0xFFFFu) << 8);
        wait_long_done();

        pushed += n;
        first = 0;
        (void)draw_bg;  /* see comment above — we always emit bg for off pixels */
    }
}
