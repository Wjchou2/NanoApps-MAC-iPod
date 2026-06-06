/* hb_touch_mb.c — read the resident-latched touch mailbox. See hb_touch_mb.h. */
#include "hb_touch_mb.h"

int hb_touch_mb_read(hb_touch_state_t *st)
{
    volatile int32_t *mb = (volatile int32_t *)(uintptr_t)HB_TOUCH_MB_ADDR;
    if ((uint32_t)mb[0] != HB_TOUCH_MB_MAGIC) {
        st->valid = 0;
        return 0;
    }
    st->valid = 1;
    st->down  = mb[3] ? 1 : 0;
    st->x     = mb[1];
    st->y     = mb[2];
    return 1;
}
