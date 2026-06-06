/*
 * hb_surface_input.h — the one Silver-surface touch source.
 *
 * Reads the resident's event-path mailbox (preferred — never misses a tap) and
 * falls back to polling the OS touch list. Shared by every surface runtime
 * (hb_lv_surface for LVGL apps, hb_raw_surface for direct-draw apps) so the
 * touch-decode lives in exactly one place.
 */
#ifndef HB_SURFACE_INPUT_H
#define HB_SURFACE_INPUT_H

#include <stdint.h>

/* Current pointer state. x/y are screen pixels; `down` is 1 while pressed and 0
 * on release. On release x/y hold the LAST pressed position (so a tap handler
 * knows where the finger lifted). */
typedef struct { int16_t x, y; int down; } hb_spoint_t;

void hb_surface_touch_read(hb_spoint_t *out);

#endif /* HB_SURFACE_INPUT_H */
