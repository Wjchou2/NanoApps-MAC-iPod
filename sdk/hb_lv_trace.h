/*
 * hb_lv_trace.h — optional trace hook threaded through LVGL's render path so a
 * crash inside lv_timer_handler can be localised on-device (the resident's ring
 * buffer survives the panic-reboot). The blob sets hb_lv_trace to the resident's
 * api->trace; everywhere else HB_LVT() compiles to nothing.
 */
#ifndef HB_LV_TRACE_H
#define HB_LV_TRACE_H

#ifdef HB_LVGL_BLOB
extern void (*hb_lv_trace)(const char *tag, unsigned a, unsigned b);
#define HB_LVT(tag, a, b)                                                   \
    do {                                                                    \
        if (hb_lv_trace) hb_lv_trace((tag), (unsigned)(a), (unsigned)(b));  \
    } while (0)
#else
#define HB_LVT(tag, a, b) do { (void)0; } while (0)
#endif

#endif /* HB_LV_TRACE_H */
