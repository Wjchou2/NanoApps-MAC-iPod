/*
 * hb_status_bar — see header for usage.
 */
#include "hb_status_bar.h"
#include "hb_sdk.h"
#include "hb_prefs.h"

static lv_obj_t *s_time_lbl = NULL;
static lv_obj_t *s_batt_lbl = NULL;

void hb_status_bar_attach(void)
{
    if (!hb_status_bar_enabled()) return;
    if (s_time_lbl) return;   /* already attached */
    hb_status_bar_create(NULL, NULL);
}

void hb_status_bar_create(lv_obj_t *parent, const char *title)
{
    if (!hb_status_bar_enabled()) {
        s_time_lbl = NULL; s_batt_lbl = NULL;
        return;
    }
    /* If parent is NULL, attach to the LVGL top layer so the bar
     * floats above whatever the app draws. Apps that explicitly
     * want the bar inside their own root can still pass a parent. */
    if (parent == NULL) parent = lv_layer_top();
    /* Background strip to make the bar visually distinct. */
    lv_obj_t *bg = lv_obj_create(parent);
    lv_obj_set_size(bg, LV_PCT(100), 22);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_hex(hb_color_bg()), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    if (title) {
        lv_obj_t *t = lv_label_create(bg);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_color(t, lv_color_hex(hb_tint_color()), 0);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 6, 0);
    }

    s_time_lbl = lv_label_create(bg);
    lv_label_set_text(s_time_lbl, "--:--");
    lv_obj_set_style_text_color(s_time_lbl, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(s_time_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 0);

    s_batt_lbl = lv_label_create(bg);
    lv_label_set_text(s_batt_lbl, LV_SYMBOL_BATTERY_3 " --%");
    lv_obj_set_style_text_color(s_batt_lbl, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(s_batt_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_batt_lbl, LV_ALIGN_RIGHT_MID, -6, 0);

    hb_status_bar_tick();
}

void hb_status_bar_tick(void)
{
    if (s_time_lbl) {
        hb_rtc_time_t t; hb_rtc_read(&t);
        char buf[8];
        buf[0] = '0' + (t.hours / 10) % 10;
        buf[1] = '0' + (t.hours % 10);
        buf[2] = ':';
        buf[3] = '0' + (t.minutes / 10) % 10;
        buf[4] = '0' + (t.minutes % 10);
        buf[5] = 0;
        lv_label_set_text(s_time_lbl, buf);
    }
    if (s_batt_lbl) {
        uint32_t lvl = hb_battery_level_0_to_15();
        int pct = (int)((lvl * 100u + 7u) / 15u);
        if (pct > 100) pct = 100;
        bool charging = hb_battery_is_charging();
        const char *icon =
            pct <= 12 ? LV_SYMBOL_BATTERY_EMPTY  :
            pct <= 37 ? LV_SYMBOL_BATTERY_1      :
            pct <= 62 ? LV_SYMBOL_BATTERY_2      :
            pct <= 87 ? LV_SYMBOL_BATTERY_3      :
                        LV_SYMBOL_BATTERY_FULL;
        if (charging) icon = LV_SYMBOL_CHARGE;
        char buf[16]; int k = 0;
        while (*icon) buf[k++] = *icon++;
        buf[k++] = ' ';
        if (pct == 100) { buf[k++] = '1'; buf[k++] = '0'; buf[k++] = '0'; }
        else if (pct >= 10) {
            buf[k++] = '0' + (pct / 10);
            buf[k++] = '0' + (pct % 10);
        } else {
            buf[k++] = '0' + pct;
        }
        buf[k++] = '%';
        buf[k] = 0;
        lv_label_set_text(s_batt_lbl, buf);
    }
}
