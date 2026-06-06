/*
 * hb_gl_surface.c — runtime entry for a GL_SURFACE .hbapp. See hb_gl_surface.h.
 *
 * The resident hb_reloc-loads this blob into an arena and calls entry(0, fb, w, h);
 * we run the app's one-time gl_app_init() and hand back the per-frame GL fn. The
 * resident stores it and calls it each frame inside the injected GL view's
 * begin-scene/end-scene. Same op/return convention as the LVGL surface runtime, so the
 * resident reuses its reloc-loader path. The reloc loader already zeroed .bss and
 * flushed the I-cache over the arena, so no prologue is needed here. fb is unused
 * (GL renders into the view's own pixmap, not a CPU framebuffer).
 */
#include "hb_gl_surface.h"

typedef void (*gl_frame_fn)(int w, int h, uint32_t frame);

__attribute__((section(".text.entry"), used, noinline))
void *payload_entry(int op, void *fb, int w, int h)
{
    (void)fb; (void)w; (void)h;
    if (op != 0)
        return 0;
    gl_app_init();
    return (void *)(gl_frame_fn)gl_app_frame;
}
