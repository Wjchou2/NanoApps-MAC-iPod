/*
 * widgets — wrapper that runs LVGL's lv_demo_widgets() on the nano.
 *
 * The actual demo source lives in lvgl/demos/widgets/; we only
 * provide an HB_APP_ENTRY shim and the main loop.
 */
#include "hb_sdk.h"
#include "lvgl/lvgl.h"

extern void lv_demo_widgets(void);

HB_APP_ENTRY(payload_entry)
{
    hb_trace_init();

    lv_demo_widgets();

}
