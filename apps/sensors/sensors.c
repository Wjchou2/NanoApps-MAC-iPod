/*
 * toolbox — consolidates the standalone hardware/API test apps into
 * one tabbed LVGL app.
 *
 * Tabs (top tab-bar):
 *   - Sensors:  live button state + multi-touch dots + accelerometer
 *   - Display:  brightness slider + sample text
 *
 * A small per-tab refresh runs each frame via the surface frame hook's
 * per_frame callback so live values stay current without manual
 * timers.
 */

#include "hb_sdk.h"
#include "hb_prefs.h"
#include "lvgl/lvgl.h"


/* ---- shared ---- */

static lv_obj_t *s_tabview;

/* Sensors tab */
static lv_obj_t *s_lbl_btn_state;
static lv_obj_t *s_lbl_accel;
static lv_obj_t *s_touch_area;
#define MAX_FINGER_DOTS  10
static lv_obj_t *s_finger_dots[MAX_FINGER_DOTS];

/* Display tab */
static lv_obj_t *s_brightness_slider;
static lv_obj_t *s_brightness_lbl;

/* Files tab */
static lv_obj_t *s_files_list;
static lv_obj_t *s_files_preview;
static char      s_files_cwd[128] = "/Apps";

/* System tab */
static lv_obj_t *s_lbl_rtc;
static lv_obj_t *s_lbl_batt;

/* ---- tiny number helpers ---- */

static void itoa_signed(int v, char *out)
{
    char buf[12]; int i = 0;
    bool neg = v < 0;
    uint32_t u = neg ? (uint32_t)(-v) : (uint32_t)v;
    if (u == 0) { out[0] = '0'; out[1] = 0; return; }
    while (u) { buf[i++] = '0' + (u % 10); u /= 10; }
    int k = 0;
    if (neg) out[k++] = '-';
    while (i) out[k++] = buf[--i];
    out[k] = 0;
}

/* ---- SENSORS ---- */

static void build_sensors_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 6, 0);

    lv_obj_t *btn_hdr = lv_label_create(tab);
    lv_label_set_text(btn_hdr, "Buttons:");
    lv_obj_set_style_text_color(btn_hdr, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_text_font(btn_hdr, &lv_font_montserrat_16, 0);
    lv_obj_align(btn_hdr, LV_ALIGN_TOP_LEFT, 0, 0);

    s_lbl_btn_state = lv_label_create(tab);
    lv_label_set_text(s_lbl_btn_state, "(none)");
    lv_obj_set_style_text_color(s_lbl_btn_state, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(s_lbl_btn_state, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_lbl_btn_state, 220);
    lv_obj_align(s_lbl_btn_state, LV_ALIGN_TOP_LEFT, 0, 22);

    lv_obj_t *acc_hdr = lv_label_create(tab);
    lv_label_set_text(acc_hdr, "Accelerometer (mg):");
    lv_obj_set_style_text_color(acc_hdr, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_text_font(acc_hdr, &lv_font_montserrat_16, 0);
    lv_obj_align(acc_hdr, LV_ALIGN_TOP_LEFT, 0, 60);

    s_lbl_accel = lv_label_create(tab);
    lv_label_set_text(s_lbl_accel, "x=0 y=0 z=0");
    lv_obj_set_style_text_color(s_lbl_accel, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(s_lbl_accel, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_accel, LV_ALIGN_TOP_LEFT, 0, 82);

    lv_obj_t *tch_hdr = lv_label_create(tab);
    lv_label_set_text(tch_hdr, "Touch (paint with fingers):");
    lv_obj_set_style_text_color(tch_hdr, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_text_font(tch_hdr, &lv_font_montserrat_16, 0);
    lv_obj_align(tch_hdr, LV_ALIGN_TOP_LEFT, 0, 120);

    s_touch_area = lv_obj_create(tab);
    lv_obj_set_size(s_touch_area, 224, 180);
    lv_obj_align(s_touch_area, LV_ALIGN_TOP_LEFT, 0, 142);
    lv_obj_set_style_bg_color(s_touch_area, lv_color_hex(0x111522), 0);
    lv_obj_set_style_border_color(s_touch_area, lv_color_hex(0x333540), 0);
    lv_obj_set_style_border_width(s_touch_area, 1, 0);
    lv_obj_set_style_radius(s_touch_area, 8, 0);
    lv_obj_clear_flag(s_touch_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_touch_area, LV_OBJ_FLAG_CLICKABLE);

    static const uint32_t dot_colors[] = {
        0xe63946, 0xf77f00, 0xfcbf49, 0x2a9d8f, 0x457b9d,
        0xa663cc, 0xff006e, 0x3a86ff, 0x14b8a6, 0xfb7185,
    };
    for (int i = 0; i < MAX_FINGER_DOTS; i++) {
        s_finger_dots[i] = lv_obj_create(s_touch_area);
        lv_obj_set_size(s_finger_dots[i], 14, 14);
        lv_obj_set_style_radius(s_finger_dots[i], 7, 0);
        lv_obj_set_style_bg_color(s_finger_dots[i],
            lv_color_hex(dot_colors[i]), 0);
        lv_obj_set_style_border_width(s_finger_dots[i], 0, 0);
        lv_obj_add_flag(s_finger_dots[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_finger_dots[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(s_finger_dots[i], LV_OBJ_FLAG_CLICKABLE);
    }
}

static void refresh_sensors(void)
{
    /* Live button readout */
    char buf[80]; int k = 0;
    const char *labels[] = { "Vol+", "Vol-", "Home", "Power", "Play" };
    hb_button_t btns[] = {
        HB_BTN_VOL_UP, HB_BTN_VOL_DOWN, HB_BTN_HOME,
        HB_BTN_POWER, HB_BTN_PLAY_PAUSE
    };
    bool any = false;
    for (int i = 0; i < 5; i++) {
        if (hb_button_pressed(btns[i])) {
            if (any) { buf[k++] = ' '; }
            for (int j = 0; labels[i][j] && k < (int)sizeof buf - 1; j++)
                buf[k++] = labels[i][j];
            any = true;
        }
    }
    if (!any) {
        const char *e = "(none pressed)";
        for (int j = 0; e[j] && k < (int)sizeof buf - 1; j++) buf[k++] = e[j];
    }
    buf[k] = 0;
    lv_label_set_text(s_lbl_btn_state, buf);

    /* Accelerometer */
    int32_t xyz[3];
    hb_accel_read_milli_g(xyz);
    char a[80]; int ak = 0;
    const char *axes[3] = { "x=", " y=", " z=" };
    for (int i = 0; i < 3; i++) {
        const char *l = axes[i];
        while (*l) a[ak++] = *l++;
        char nb[12]; itoa_signed(xyz[i], nb);
        for (int j = 0; nb[j]; j++) a[ak++] = nb[j];
    }
    a[ak] = 0;
    lv_label_set_text(s_lbl_accel, a);

    /* Multi-finger touch dots.
       Bug-fix: we used lv_obj_get_x/y, which returns coords relative
       to the immediate parent. The touch_area is nested several
       layers deep (tabview → tab → touch_area), so the resulting
       offset was off by however far that nesting moved the widget.
       Use lv_obj_get_coords for the ABSOLUTE screen rect, since
       touch reports come in screen coordinates too. */
    hb_touch_t fingers[HB_MAX_FINGERS];
    int n = hb_touch_poll_multi(fingers);
    lv_area_t area;
    lv_obj_get_coords(s_touch_area, &area);
    int shown = 0;
    for (int i = 0; i < n && shown < MAX_FINGER_DOTS; i++) {
        bool down = (fingers[i].status == 0 || fingers[i].status == 2);
        if (!down) continue;
        if (fingers[i].x < area.x1 || fingers[i].x > area.x2) continue;
        if (fingers[i].y < area.y1 || fingers[i].y > area.y2) continue;
        /* Position is relative to touch_area's CHILDREN coordinate
           system (i.e. relative to area.x1,y1). Subtract -7 so the
           dot is centered on the finger. */
        int x = fingers[i].x - area.x1 - 7;
        int y = fingers[i].y - area.y1 - 7;
        lv_obj_clear_flag(s_finger_dots[shown], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_finger_dots[shown], x, y);
        shown++;
    }
    for (int i = shown; i < MAX_FINGER_DOTS; i++) {
        lv_obj_add_flag(s_finger_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---- DISPLAY ---- */

static void on_brightness_slider(lv_event_t *e)
{
    (void)e;
    int v = (int)lv_slider_get_value(s_brightness_slider);
    hb_brightness_set_percent(v);
    char buf[16]; int k = 0;
    char nb[12]; itoa_signed(v, nb);
    for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k++] = '%';
    buf[k] = 0;
    lv_label_set_text(s_brightness_lbl, buf);
}

static void build_display_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 6, 0);

    lv_obj_t *hdr = lv_label_create(tab);
    lv_label_set_text(hdr, "Brightness:");
    lv_obj_set_style_text_color(hdr, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_16, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);

    s_brightness_lbl = lv_label_create(tab);
    lv_label_set_text(s_brightness_lbl, "—");
    lv_obj_set_style_text_color(s_brightness_lbl, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(s_brightness_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(s_brightness_lbl, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_brightness_slider = lv_slider_create(tab);
    lv_obj_set_size(s_brightness_slider, 220, 16);
    lv_obj_align(s_brightness_slider, LV_ALIGN_TOP_MID, 0, 32);
    lv_slider_set_range(s_brightness_slider, 0, 100);
    lv_slider_set_value(s_brightness_slider, 60, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_brightness_slider, on_brightness_slider,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* Font samples */
    lv_obj_t *fhdr = lv_label_create(tab);
    lv_label_set_text(fhdr, "Font samples:");
    lv_obj_set_style_text_color(fhdr, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_text_font(fhdr, &lv_font_montserrat_16, 0);
    lv_obj_align(fhdr, LV_ALIGN_TOP_LEFT, 0, 80);

    static const struct { const lv_font_t *f; const char *name; } fonts[] = {
        { &lv_font_montserrat_14, "Mont 14" },
        { &lv_font_montserrat_16, "Mont 16" },
        { &lv_font_montserrat_20, "Mont 20" },
        { &lv_font_montserrat_24, "Mont 24" },
    };
    int y = 108;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *l = lv_label_create(tab);
        char buf[40]; int k = 0;
        const char *n = fonts[i].name;
        while (*n) buf[k++] = *n++;
        buf[k++] = ' '; buf[k++] = '-'; buf[k++] = ' ';
        const char *s = "The quick brown fox";
        while (*s) buf[k++] = *s++;
        buf[k] = 0;
        lv_label_set_text(l, buf);
        lv_obj_set_style_text_color(l, lv_color_hex(hb_color_text()), 0);
        lv_obj_set_style_text_font(l, fonts[i].f, 0);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, y);
        y += 32;
    }

    lv_label_set_text(s_brightness_lbl, "60%");
}

/* ---- FILES ---- */

static void files_repopulate(void);

static void on_files_row(lv_event_t *e)
{
    const char *name = (const char *)lv_event_get_user_data(e);
    if (!name) return;
    /* Build absolute path. */
    char path[256]; int k = 0;
    for (int i = 0; s_files_cwd[i] && k < 250; i++) path[k++] = s_files_cwd[i];
    if (k == 0 || path[k-1] != '/') path[k++] = '/';
    for (int i = 0; name[i] && k < 254; i++) path[k++] = name[i];
    path[k] = 0;

    if (hb_fs_is_dir(path)) {
        /* descend */
        for (int i = 0; i < k && i < (int)sizeof s_files_cwd - 1; i++) s_files_cwd[i] = path[i];
        s_files_cwd[k] = 0;
        files_repopulate();
    } else {
        /* read first ~512 bytes and show as preview */
        static char buf[513];
        uint32_t n = hb_fs_read(path, buf, sizeof buf - 1);
        buf[n < sizeof buf ? n : sizeof buf - 1] = 0;
        /* sanitize: strip non-printables that aren't \n or \t */
        for (uint32_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n' || c == '\t') continue;
            if ((uint8_t)c < 0x20 || (uint8_t)c >= 0x7F) buf[i] = '.';
        }
        lv_label_set_text(s_files_preview, n > 0 ? buf : "(empty / binary)");
    }
}

static void on_files_up(lv_event_t *e)
{
    (void)e;
    int n = 0; while (s_files_cwd[n]) n++;
    if (n <= 1) return;
    /* trim a trailing '/' first */
    if (n > 0 && s_files_cwd[n-1] == '/') { s_files_cwd[--n] = 0; }
    while (n > 0 && s_files_cwd[n-1] != '/') { s_files_cwd[--n] = 0; }
    if (n == 0) { s_files_cwd[0] = '/'; s_files_cwd[1] = 0; }
    else if (n > 1) s_files_cwd[n - 1] = 0;     /* drop trailing slash unless root */
    files_repopulate();
}

static void files_repopulate(void)
{
    lv_obj_clean(s_files_list);
    lv_label_set_text(s_files_preview, s_files_cwd);

    hb_dir_t d;
    if (!hb_fs_dir_open(&d, s_files_cwd, false)) {
        lv_label_set_text(s_files_preview, "(cannot open directory)");
        return;
    }
    char name[64]; bool is_dir = false;
    while (hb_fs_dir_next(&d, name, sizeof name, &is_dir)) {
        if (name[0] == '.') continue;
        /* Heap-dup name so the event_cb cookie outlives this loop. */
        int nl = 0; while (name[nl]) nl++;
        char *dup = lv_malloc((uint32_t)nl + 1);
        if (!dup) break;
        for (int i = 0; i <= nl; i++) dup[i] = name[i];

        lv_obj_t *row = lv_button_create(s_files_list);
        lv_obj_set_size(row, 224, 32);
        lv_obj_set_style_bg_color(row, lv_color_hex(hb_color_surface()), 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_margin_bottom(row, 2, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, on_files_row, LV_EVENT_CLICKED, dup);

        lv_obj_t *ic = lv_label_create(row);
        lv_label_set_text(ic, is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
        lv_obj_set_style_text_color(ic,
            is_dir ? lv_color_hex(hb_color_primary()) : lv_color_hex(hb_color_text_dim()), 0);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t *lab = lv_label_create(row);
        lv_label_set_text(lab, name);
        lv_obj_set_style_text_color(lab, lv_color_hex(hb_color_text()), 0);
        lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lab, 190);
        lv_obj_align(lab, LV_ALIGN_LEFT_MID, 32, 0);
    }
    hb_fs_dir_close(&d);
}

static void build_files_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 6, 0);

    lv_obj_t *btn_up = lv_button_create(tab);
    lv_obj_set_size(btn_up, 70, 28);
    lv_obj_align(btn_up, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_up, lv_color_hex(hb_color_text_dim()), 0);
    lv_obj_add_event_cb(btn_up, on_files_up, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ul = lv_label_create(btn_up);
    lv_label_set_text(ul, LV_SYMBOL_UP " Up");
    lv_obj_set_style_text_color(ul, lv_color_hex(hb_color_text()), 0);
    lv_obj_center(ul);

    s_files_preview = lv_label_create(tab);
    lv_label_set_text(s_files_preview, s_files_cwd);
    lv_obj_set_style_text_color(s_files_preview, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_text_font(s_files_preview, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_files_preview, 144);
    lv_label_set_long_mode(s_files_preview, LV_LABEL_LONG_DOT);
    lv_obj_align(s_files_preview, LV_ALIGN_TOP_LEFT, 78, 6);

    s_files_list = lv_obj_create(tab);
    lv_obj_set_size(s_files_list, 224, 264);
    lv_obj_align(s_files_list, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_obj_set_style_bg_color(s_files_list, lv_color_hex(0x111522), 0);
    lv_obj_set_style_border_width(s_files_list, 0, 0);
    lv_obj_set_style_pad_all(s_files_list, 4, 0);
    lv_obj_set_flex_flow(s_files_list, LV_FLEX_FLOW_COLUMN);

    files_repopulate();
}

/* ---- SYSTEM ---- */

static void build_system_tab(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 6, 0);

    lv_obj_t *rhdr = lv_label_create(tab);
    lv_label_set_text(rhdr, "Clock:");
    lv_obj_set_style_text_color(rhdr, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_text_font(rhdr, &lv_font_montserrat_16, 0);
    lv_obj_align(rhdr, LV_ALIGN_TOP_LEFT, 0, 0);

    s_lbl_rtc = lv_label_create(tab);
    lv_label_set_text(s_lbl_rtc, "—");
    lv_obj_set_style_text_color(s_lbl_rtc, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(s_lbl_rtc, &lv_font_montserrat_20, 0);
    lv_obj_align(s_lbl_rtc, LV_ALIGN_TOP_LEFT, 0, 24);

    lv_obj_t *bhdr = lv_label_create(tab);
    lv_label_set_text(bhdr, "Battery:");
    lv_obj_set_style_text_color(bhdr, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_text_font(bhdr, &lv_font_montserrat_16, 0);
    lv_obj_align(bhdr, LV_ALIGN_TOP_LEFT, 0, 78);

    s_lbl_batt = lv_label_create(tab);
    lv_label_set_text(s_lbl_batt, "—");
    lv_obj_set_style_text_color(s_lbl_batt, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(s_lbl_batt, &lv_font_montserrat_16, 0);
    lv_obj_set_width(s_lbl_batt, 224);
    lv_label_set_long_mode(s_lbl_batt, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_lbl_batt, LV_ALIGN_TOP_LEFT, 0, 102);
}

static void refresh_system(void)
{
    hb_rtc_time_t t; hb_rtc_read(&t);
    char buf[40]; int k = 0;
    char nb[12];
    itoa_signed(t.year, nb); for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k++] = '-';
    if (t.month < 10) buf[k++] = '0';
    itoa_signed(t.month, nb); for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k++] = '-';
    if (t.day_of_month < 10) buf[k++] = '0';
    itoa_signed(t.day_of_month, nb); for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k++] = ' ';
    if (t.hours < 10) buf[k++] = '0';
    itoa_signed(t.hours, nb); for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k++] = ':';
    if (t.minutes < 10) buf[k++] = '0';
    itoa_signed(t.minutes, nb); for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k++] = ':';
    if (t.seconds < 10) buf[k++] = '0';
    itoa_signed(t.seconds, nb); for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k] = 0;
    lv_label_set_text(s_lbl_rtc, buf);

    uint32_t mv  = hb_battery_voltage_mv();
    uint32_t lvl = hb_battery_level_0_to_15();
    bool charging = hb_battery_is_charging();
    char b[80]; int bk = 0;
    const char *p = "Level "; while (*p) b[bk++] = *p++;
    itoa_signed((int)lvl, nb); for (int i = 0; nb[i]; i++) b[bk++] = nb[i];
    p = "/15  "; while (*p) b[bk++] = *p++;
    itoa_signed((int)mv, nb); for (int i = 0; nb[i]; i++) b[bk++] = nb[i];
    p = " mV"; while (*p) b[bk++] = *p++;
    if (charging) { p = "  charging"; while (*p) b[bk++] = *p++; }
    b[bk] = 0;
    lv_label_set_text(s_lbl_batt, b);
}

/* ---- frame tick ---- */

static void on_frame(void)
{
    int active = (int)lv_tabview_get_tab_active(s_tabview);
    if (active == 0)      refresh_sensors();
    else if (active == 3) refresh_system();
    /* Display + Files tabs are event-driven; no per-frame refresh. */
}

HB_APP_ENTRY(payload_entry)
{
    hb_trace_init();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(hb_color_bg()), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_tabview = lv_tabview_create(scr);
    lv_tabview_set_tab_bar_size(s_tabview, 34);
    lv_obj_set_size(s_tabview, 240, 432);
    lv_obj_set_style_radius(s_tabview, 0, 0);
    lv_obj_align(s_tabview, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_tabview, lv_color_hex(hb_color_bg()), 0);

    /* "Sensors" app: keep the sensor + display diagnostics. The Files (hex) tab
       folded into the Files app, and the System (RTC/battery) tab is redundant with
       Clock + the status bar, so both are dropped. (Their build_* fns are kept.) */
    lv_obj_t *t_sensors = lv_tabview_add_tab(s_tabview, "Sensors");
    lv_obj_t *t_display = lv_tabview_add_tab(s_tabview, "Display");

    build_sensors_tab(t_sensors);
    build_display_tab(t_display);
    (void)build_files_tab; (void)build_system_tab;

    hb_lv_set_frame_cb(on_frame);
}
