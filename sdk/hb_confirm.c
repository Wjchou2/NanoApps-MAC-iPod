#include "hb_confirm.h"
#include "hb_prefs.h"

static lv_obj_t       *s_modal      = NULL;
static lv_obj_t       *s_scrim      = NULL;
static hb_confirm_cb_t s_cb         = NULL;
static void           *s_cookie     = NULL;

static void teardown(void)
{
    if (s_modal) { lv_obj_del(s_modal); s_modal = NULL; }
    if (s_scrim) { lv_obj_del(s_scrim); s_scrim = NULL; }
    s_cb = NULL;
    s_cookie = NULL;
}

static void on_cancel(lv_event_t *e) { (void)e; teardown(); }

static void on_confirm(lv_event_t *e)
{
    (void)e;
    hb_confirm_cb_t cb = s_cb;
    void *ck = s_cookie;
    teardown();
    if (cb) cb(ck);
}

void hb_confirm_show(const char *title, const char *body,
                     const char *confirm_label,
                     hb_confirm_cb_t cb, void *cookie)
{
    teardown();        /* dismiss any existing */
    s_cb = cb;
    s_cookie = cookie;

    lv_obj_t *top = lv_layer_top();

    /* Scrim: full-screen dim, swallows taps outside the card. */
    s_scrim = lv_obj_create(top);
    lv_obj_set_size(s_scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_scrim, 0, 0);
    lv_obj_set_style_bg_color(s_scrim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_scrim, 0, 0);
    lv_obj_set_style_radius(s_scrim, 0, 0);
    lv_obj_clear_flag(s_scrim, LV_OBJ_FLAG_SCROLLABLE);

    /* Card */
    s_modal = lv_obj_create(top);
    lv_obj_set_size(s_modal, 220, 180);
    lv_obj_align(s_modal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_modal, lv_color_hex(hb_color_surface()), 0);
    lv_obj_set_style_radius(s_modal, 12, 0);
    lv_obj_set_style_border_width(s_modal, 0, 0);
    lv_obj_set_style_pad_all(s_modal, 12, 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(s_modal);
    lv_label_set_text(t, title ? title : "Confirm?");
    lv_obj_set_style_text_color(t, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_width(t, 196);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    if (body) {
        lv_obj_t *b = lv_label_create(s_modal);
        lv_label_set_text(b, body);
        lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(b, 196);
        lv_obj_set_style_text_color(b, lv_color_hex(hb_color_text_dim()), 0);
        lv_obj_align(b, LV_ALIGN_TOP_LEFT, 0, 36);
    }

    lv_obj_t *btn_no = lv_button_create(s_modal);
    lv_obj_set_size(btn_no, 88, 38);
    lv_obj_align(btn_no, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_no, lv_color_hex(hb_color_surface()), 0);
    lv_obj_set_style_border_width(btn_no, 1, 0);
    lv_obj_set_style_border_color(btn_no, lv_color_hex(hb_color_text_dim()), 0);
    lv_obj_set_style_radius(btn_no, 8, 0);
    lv_obj_add_event_cb(btn_no, on_cancel, LV_EVENT_CLICKED, NULL);
    lv_obj_t *nl = lv_label_create(btn_no);
    lv_label_set_text(nl, "Cancel");
    lv_obj_set_style_text_color(nl, lv_color_hex(hb_color_text()), 0);
    lv_obj_center(nl);

    lv_obj_t *btn_yes = lv_button_create(s_modal);
    lv_obj_set_size(btn_yes, 88, 38);
    lv_obj_align(btn_yes, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_yes, lv_color_hex(hb_color_danger()), 0);
    lv_obj_set_style_radius(btn_yes, 8, 0);
    lv_obj_add_event_cb(btn_yes, on_confirm, LV_EVENT_CLICKED, NULL);
    lv_obj_t *yl = lv_label_create(btn_yes);
    lv_label_set_text(yl, confirm_label ? confirm_label : "Delete");
    lv_obj_set_style_text_color(yl, lv_color_hex(0xffffff), 0);
    lv_obj_center(yl);
}
