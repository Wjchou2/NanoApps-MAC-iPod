/*
 * spirit_level — bubble level using the accelerometer.
 *
 * A circular vial in the center; the "bubble" floats opposite the tilt
 * direction (so resting flat puts it dead-center). Two readouts:
 * pitch/roll in degrees, and a "level" indicator that flashes green
 * when both axes are within ±1°.
 *
 * Reading the accelerometer in mg lets us approximate the gravity
 * direction. With z=±1000 mg flat, x and y read the lateral tilt.
 * Convert to angles via the small-angle approximation atan(x/z) which
 * is plenty accurate for a visual level inside ±25°.
 */

#include "hb_sdk.h"
#include "hb_prefs.h"
#include "lvgl/lvgl.h"

#define VIAL_R   90            /* outer vial radius (px) */
#define BUBBLE_R 22            /* bubble radius (px) */
#define BUBBLE_MAX_OFFSET (VIAL_R - BUBBLE_R - 4)

static lv_obj_t *s_vial;
static lv_obj_t *s_bubble;
static lv_obj_t *s_center_mark;
static lv_obj_t *s_lbl_pitch;
static lv_obj_t *s_lbl_roll;
static lv_obj_t *s_lbl_level;

/* Smoothed accelerometer (low-pass to kill jitter). Stored as mg. */
static int32_t s_smooth[3] = { 0, 0, 1000 };

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

/* Cheap atan2-style angle in degrees, valid for small angles. We
   only need ±25° accuracy here for the bubble offset. */
static int angle_deg_small(int32_t lateral_mg, int32_t z_mg)
{
    if (z_mg < 100 && z_mg > -100) z_mg = (z_mg < 0 ? -100 : 100);
    /* atan(x/z) ≈ x/z * 57.2958°. Stay in integer arithmetic. */
    long n = ((long)lateral_mg * 5730) / z_mg;     /* deg × 100 */
    return (int)(n / 100);
}

static void on_frame(void)
{
    int32_t a[3];
    hb_accel_read_milli_g(a);
    /* Low-pass: s = s*7/8 + a/8. */
    for (int i = 0; i < 3; i++) {
        s_smooth[i] = (s_smooth[i] * 7 + a[i]) / 8;
    }

    int pitch = angle_deg_small(s_smooth[1], s_smooth[2]);
    int roll  = angle_deg_small(s_smooth[0], s_smooth[2]);

    /* Bubble offset within the vial — opposite to tilt direction so
       it acts like a real liquid level. Scale: 1° → ~6 px (≈ 14° for
       full deflection). */
    int dx = -roll  * 6;
    int dy = -pitch * 6;
    if (dx >  BUBBLE_MAX_OFFSET) dx =  BUBBLE_MAX_OFFSET;
    if (dx < -BUBBLE_MAX_OFFSET) dx = -BUBBLE_MAX_OFFSET;
    if (dy >  BUBBLE_MAX_OFFSET) dy =  BUBBLE_MAX_OFFSET;
    if (dy < -BUBBLE_MAX_OFFSET) dy = -BUBBLE_MAX_OFFSET;
    lv_obj_align(s_bubble, LV_ALIGN_CENTER, dx, dy);

    /* Numeric readouts. */
    char buf[32];
    int k = 0;
    const char *p = "Pitch: "; while (*p) buf[k++] = *p++;
    char nb[12]; itoa_signed(pitch, nb);
    for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k++] = (char)0xC2; buf[k++] = (char)0xB0;  /* UTF-8 degree sign */
    buf[k] = 0;
    lv_label_set_text(s_lbl_pitch, buf);

    k = 0; p = "Roll:  "; while (*p) buf[k++] = *p++;
    itoa_signed(roll, nb);
    for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k++] = (char)0xC2; buf[k++] = (char)0xB0;
    buf[k] = 0;
    lv_label_set_text(s_lbl_roll, buf);

    /* "Level!" indicator when within ±1°. */
    int abs_p = pitch < 0 ? -pitch : pitch;
    int abs_r = roll  < 0 ? -roll  : roll;
    bool level = (abs_p <= 1 && abs_r <= 1);
    lv_label_set_text(s_lbl_level, level ? "LEVEL" : "");
    lv_obj_set_style_text_color(s_lbl_level,
        lv_color_hex(level ? 0x2a9d8f : 0x6b7280), 0);
}

HB_APP_ENTRY(payload_entry)
{
    hb_trace_init();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(hb_color_bg()), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Spirit Level");
    lv_obj_set_style_text_color(title, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    /* Vial — circular dark disc with a contrasting inner ring. */
    s_vial = lv_obj_create(scr);
    lv_obj_set_size(s_vial, VIAL_R * 2, VIAL_R * 2);
    lv_obj_align(s_vial, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_bg_color(s_vial, lv_color_hex(hb_color_surface()), 0);
    lv_obj_set_style_radius(s_vial, VIAL_R, 0);
    lv_obj_set_style_border_color(s_vial, lv_color_hex(hb_color_text_dim()), 0);
    lv_obj_set_style_border_width(s_vial, 3, 0);
    lv_obj_clear_flag(s_vial, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_vial, LV_OBJ_FLAG_CLICKABLE);

    /* Center crosshair (target). */
    s_center_mark = lv_obj_create(s_vial);
    lv_obj_set_size(s_center_mark, BUBBLE_R * 2 + 8, BUBBLE_R * 2 + 8);
    lv_obj_align(s_center_mark, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(s_center_mark, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_center_mark, lv_color_hex(hb_color_text_dim()), 0);
    lv_obj_set_style_border_width(s_center_mark, 1, 0);
    lv_obj_set_style_radius(s_center_mark, BUBBLE_R + 4, 0);
    lv_obj_clear_flag(s_center_mark, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_center_mark, LV_OBJ_FLAG_CLICKABLE);

    /* Bubble. */
    s_bubble = lv_obj_create(s_vial);
    lv_obj_set_size(s_bubble, BUBBLE_R * 2, BUBBLE_R * 2);
    lv_obj_align(s_bubble, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_bubble, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_border_width(s_bubble, 0, 0);
    lv_obj_set_style_radius(s_bubble, BUBBLE_R, 0);
    lv_obj_clear_flag(s_bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_bubble, LV_OBJ_FLAG_CLICKABLE);

    /* Readouts. */
    s_lbl_pitch = lv_label_create(scr);
    lv_label_set_text(s_lbl_pitch, "Pitch: 0°");
    lv_obj_set_style_text_color(s_lbl_pitch, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(s_lbl_pitch, &lv_font_montserrat_20, 0);
    lv_obj_align(s_lbl_pitch, LV_ALIGN_BOTTOM_LEFT, 18, -60);

    s_lbl_roll = lv_label_create(scr);
    lv_label_set_text(s_lbl_roll, "Roll: 0°");
    lv_obj_set_style_text_color(s_lbl_roll, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(s_lbl_roll, &lv_font_montserrat_20, 0);
    lv_obj_align(s_lbl_roll, LV_ALIGN_BOTTOM_LEFT, 18, -32);

    s_lbl_level = lv_label_create(scr);
    lv_label_set_text(s_lbl_level, "");
    lv_obj_set_style_text_color(s_lbl_level, lv_color_hex(hb_color_success()), 0);
    lv_obj_set_style_text_font(s_lbl_level, &lv_font_montserrat_28, 0);
    lv_obj_align(s_lbl_level, LV_ALIGN_BOTTOM_RIGHT, -18, -40);

    hb_lv_set_frame_cb(on_frame);
}
