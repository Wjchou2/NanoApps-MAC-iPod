/*
 * hb_surface_input.c — shared surface touch decode. See hb_surface_input.h.
 *
 * Lifted out of hb_lv_surface.c so the LVGL and raw direct-draw runtimes use one
 * implementation. Prefer the resident's latched mailbox (event path: never
 * misses a tap); fall back to polling the OS touch list (works for holds).
 * Raw node: status@+12 (0/2 down, 1 up), x@+16, y@+20 (int16, screen px) — these
 * offsets and the 0/1/2 status set are what we've observed off the poll fallback;
 * the status set may be larger than three.
 */
#include "hb_surface_input.h"
#include "hb_touch_mb.h"

/* OS touch list head (first-finger queue) — the poll fallback when the resident
 * hasn't latched the mailbox. */
#define HB_TOUCH_HEAD ((volatile uint32_t *)0x089a5298u)

static int16_t s_last_x, s_last_y;

static void drain_to_one(volatile uint32_t *head)
{
    while (head[5] >= 2) {
        uint32_t end_node = head[4], node, next, prev;
        if (end_node < 0x08000000u || end_node >= 0x10000000u) break;
        node = *(volatile uint32_t *)end_node;
        if (node == end_node || node < 0x08000000u || node >= 0x10000000u) break;
        next = *(volatile uint32_t *)(node + 0);
        prev = *(volatile uint32_t *)(node + 4);
        *(volatile uint32_t *)(prev + 0) = next;
        *(volatile uint32_t *)(next + 4) = prev;
        head[5] = head[5] - 1;
        *(volatile uint32_t *)(node + 0) = head[1];
        head[1] = node;
    }
}

void hb_surface_touch_read(hb_spoint_t *out)
{
    volatile uint32_t *head = HB_TOUCH_HEAD;
    hb_touch_state_t t;
    uint32_t end_node, first_node;
    volatile int32_t *value;
    uint8_t status;

    if (hb_touch_mb_read(&t)) {              /* event path: resident latched it */
        if (t.down) { s_last_x = (int16_t)t.x; s_last_y = (int16_t)t.y; out->down = 1; }
        else        { out->down = 0; }
        out->x = s_last_x; out->y = s_last_y;
        return;
    }

    drain_to_one(head);
    out->x = s_last_x; out->y = s_last_y; out->down = 0;
    if (head[5] == 0) return;
    end_node = head[4];
    if (end_node < 0x08000000u || end_node >= 0x10000000u) return;
    first_node = *(volatile uint32_t *)end_node;
    if (first_node < 0x08000000u || first_node >= 0x10000000u) return;
    value  = (volatile int32_t *)(first_node + 8);
    status = *((volatile uint8_t *)(first_node + 8) + 4);
    if (status != 1) {                        /* down */
        s_last_x = (int16_t)value[2]; s_last_y = (int16_t)value[3];
        out->x = s_last_x; out->y = s_last_y; out->down = 1;
    }
}
