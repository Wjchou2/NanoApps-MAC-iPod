/*
 * hb_time.c — free-running microsecond uptime via the SoC microsecond counter.
 *
 */

#include "hb_sdk.h"

#define HWTIMER_BASE   0x3c700000u
#define HWTIMER_CNTH   (*(volatile uint32_t *)(HWTIMER_BASE + 0x80))
#define HWTIMER_CNTL   (*(volatile uint32_t *)(HWTIMER_BASE + 0x84))

uint32_t hb_time_uptime_us(void)
{
    /* CNTL alone is sufficient when we only care about a 32-bit µs
       window — the full retry-loop isn't needed for deltas. */
    return HWTIMER_CNTL;
}

uint32_t hb_time_uptime_ms(void)
{
    static bool     s_inited = false;
    static uint32_t s_last_us = 0;
    static uint32_t s_us_rem = 0;
    static uint32_t s_ms = 0;

    uint32_t now_us = HWTIMER_CNTL;
    if (!s_inited) {
        s_last_us = now_us;
        s_inited = true;
        return 0;
    }
    uint32_t delta = now_us - s_last_us;  /* wraps correctly mod 2^32 */
    s_last_us = now_us;
    uint32_t total = s_us_rem + delta;
    uint32_t add = total / 1000u;
    s_us_rem = total - add * 1000u;
    s_ms += add;
    return s_ms;
}
