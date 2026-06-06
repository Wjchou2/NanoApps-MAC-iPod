#include "hb_sdk.h"
#include "lvgl/lvgl.h"

extern void lv_demo_stress(void);

HB_APP_ENTRY(payload_entry)
{
    hb_trace_init();
    lv_demo_stress();
}
