/*
 * hb_touch_mb.h — resident-latched event-path touch mailbox (single source of truth).
 *
 * The resident's touch-posting entry hook writes [magic, x, y, pressed] to a fixed RAM
 * mailbox on every touch event; surface apps read it so a tap is never missed
 * (polling the transient OS touch list only caught ~1/4 of taps). The address +
 * layout are defined ONCE here and shared by the writer (resident) and every
 * reader (LVGL/GL surface apps), instead of being copied per app.
 */
#ifndef HB_TOUCH_MB_H
#define HB_TOUCH_MB_H

#include <stdint.h>

#define HB_TOUCH_MB_ADDR   0x09135f40u   /* [magic][x][y][pressed], 4x int32   */
#define HB_TOUCH_MB_MAGIC  0x54504e54u   /* 'TNPT' — resident has written it    */

typedef struct {
    int valid;   /* 1 if the resident has latched the mailbox (else poll)       */
    int down;    /* finger down                                                 */
    int x, y;    /* screen coordinates (only meaningful when valid)             */
} hb_touch_state_t;

/* Read the latched mailbox into *st. Returns st->valid (1 = armed, 0 = caller
 * should fall back to polling the OS touch list). */
int hb_touch_mb_read(hb_touch_state_t *st);

#endif /* HB_TOUCH_MB_H */
