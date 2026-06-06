/*
 * hb_status_bar — shared status bar widget for non-game apps.
 *
 * Creates a 22 px tall row at the top of the screen with:
 *   left:  optional app title (NULL = blank)
 *   center: HH:MM from RTC
 *   right: battery icon + percent (charging glyph when plugged in)
 *
 * Apps should:
 *   hb_status_bar_create(scr, "Notes");          // build
 *   hb_lvgl_main_loop(hb_status_bar_tick);       // refresh per frame
 *
 * The bar occupies y=[0..22]. Place your other UI starting at y=24.
 */
#ifndef HB_STATUS_BAR_H_
#define HB_STATUS_BAR_H_

#include "lvgl/lvgl.h"

void hb_status_bar_create(lv_obj_t *parent, const char *title);
void hb_status_bar_tick(void);

/* Convenience: attach to the LVGL top layer (no title) so the bar
 * floats over any app UI. Games should NOT call this — they pass
 * NULL or use their own full-screen canvas. Productivity apps can
 * call this once after hb_lvgl_init + hb_lvgl_apply_theme and get
 * the bar without restructuring their own layout. */
void hb_status_bar_attach(void);

#endif
