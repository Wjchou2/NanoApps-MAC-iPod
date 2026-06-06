/*
 * hb_app_api.h — the call-out table a relocatable homebrew app blob receives.
 *
 * A homebrew app is built as a self-contained, position-independent blob (no
 * external symbols; libgcc folded in) linked at base 0 and loaded into an
 * OS-heap arena by hb_reloc (which rebases its ABS32 relocations). The blob
 * therefore can't call the OS or SDK by absolute address — instead the runtime
 * hands it THIS table at entry, and the blob reaches everything through it.
 *
 * The blob's entry point has the fixed signature:
 *     int hb_blob_main(const hb_app_api_t *api, uint32_t arg);
 * and must be the ELF entry symbol (link with -e hb_blob_main).
 *
 * Bump `version` whenever the table layout changes; a blob checks it and bails
 * if the runtime is older than it expects.
 */
#ifndef HB_APP_API_H
#define HB_APP_API_H

#include <stdint.h>

#define HB_APP_API_VERSION 3u

typedef struct hb_app_api {
    uint32_t version;                                    /* = HB_APP_API_VERSION */
    uint32_t reserved0;
    void  (*trace)(const char *tag, uint32_t a, uint32_t b);  /* debug trace ring */
    void *(*alloc)(uint32_t size);                       /* OS-heap allocation    */
    /* custom-draw surface: blit a w*h ARGB-8888 buffer into the app's draw context. */
    void  (*blit)(void *dst_ctx, void *src_ctx, int w, int h, const void *dst_rect,
                  unsigned char alpha);
    void *(*make_surface)(void *dev_storage, void *gfx_storage, void *pixels,
                          int w, int h);
    /* v2: the custom-draw framebuffer the app renders into (ARGB-8888, fb_w*fb_h*4 bytes).
     * The resident owns it and blits it onto the Silver screen each draw, so a
     * CUSTOM_SURFACE blob (e.g. LVGL) just renders straight into `fb`. */
    void *fb;
    int   fb_w;
    int   fb_h;
    /* v3: input + time, so a surface blob needs no OS/SDK symbols of its own.
     * get_touch fills x/y (screen coords) and returns 1 if a finger is down, 0
     * otherwise — the resident decides how (poll or event-subscribe), the blob
     * doesn't care. now_ms is a monotonic millisecond clock for the
     * UI tick. */
    int      (*get_touch)(int *x, int *y);
    uint32_t (*now_ms)(void);
} hb_app_api_t;

/* Blob entry signature (defined by the app, called by the runtime). */
typedef int (*hb_blob_main_t)(const hb_app_api_t *api, uint32_t arg);

#endif /* HB_APP_API_H */
