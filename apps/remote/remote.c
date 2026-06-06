/*
 * music_remote — full transport remote, talks to the OS media player.
 *
 * Five discrete actions:
 *   Play, Pause, Play/Pause toggle, Prev, Next
 * plus a status badge that polls the player state twice a second.
 *
 * Layout: title at the top, status badge in a card, two rows of
 * action buttons. The centre toggle is a real play/pause control — its
 * icon shows what the next tap will do (pause while playing, play while
 * paused). Separate Play and Pause buttons keep the action explicit.
 */
#include "hb_sdk.h"
#include "hb_prefs.h"
#include "lvgl/lvgl.h"

static lv_obj_t *s_state_badge;
static lv_obj_t *s_toggle_icon;     /* the play/pause toggle's icon label */
static uint32_t s_last_ms = 0;

static const char *state_text(int st)
{
    switch (st) {
        case 0:  return "Playing";
        case 1:  return "Paused";
        case 2:  return "Stopped";
        case -1: return "No session";
        default: return "Unknown";
    }
}

static lv_color_t state_color(int st)
{
    switch (st) {
        case 0:  return lv_color_hex(hb_color_success());
        case 1:  return lv_color_hex(0xf77f00);
        case 2:  return lv_color_hex(hb_color_text_dim());
        default: return lv_color_hex(hb_color_text_dim());
    }
}

static void refresh(void)
{
    int st = hb_media_state();
    lv_label_set_text(s_state_badge, state_text(st));
    lv_obj_set_style_bg_color(lv_obj_get_parent(s_state_badge),
                              state_color(st), 0);
    /* Toggle icon = the action the next tap performs: pause while playing,
       play otherwise. */
    if (s_toggle_icon)
        lv_label_set_text(s_toggle_icon, st == 0 ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static void on_play(lv_event_t *e)   { (void)e; hb_media_set_paused(false); refresh(); }
static void on_pause(lv_event_t *e)  { (void)e; hb_media_set_paused(true);  refresh(); }
static void on_toggle(lv_event_t *e) { (void)e; hb_media_toggle(); refresh(); }
static void on_prev(lv_event_t *e)   { (void)e; hb_media_prev();   refresh(); }
static void on_next(lv_event_t *e)   { (void)e; hb_media_next();   refresh(); }

static void on_tick(void)
{
    uint32_t now = hb_time_uptime_ms();
    if (now - s_last_ms >= 500) {
        refresh();
        s_last_ms = now;
    }
}

static lv_obj_t *make_button(lv_obj_t *parent, int16_t x, int16_t y,
                             int16_t w, int16_t h, uint32_t bg,
                             const char *symbol, const char *label,
                             lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    /* Vertical stack: symbol on top, text under it. */
    if (symbol) {
        lv_obj_t *l_sym = lv_label_create(btn);
        lv_label_set_text(l_sym, symbol);
        lv_obj_set_style_text_font(l_sym, &lv_font_montserrat_24, 0);
        lv_obj_align(l_sym, LV_ALIGN_CENTER, 0, label ? -8 : 0);
    }
    if (label) {
        lv_obj_t *l_txt = lv_label_create(btn);
        lv_label_set_text(l_txt, label);
        lv_obj_set_style_text_font(l_txt, &lv_font_montserrat_14, 0);
        lv_obj_align(l_txt, LV_ALIGN_CENTER, 0, symbol ? 14 : 0);
    }
    return btn;
}

HB_APP_ENTRY(payload_entry)
{
    hb_trace_init();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a0f), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Music Remote");
    lv_obj_set_style_text_color(title, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    /* State badge card */
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 220, 48);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_state_badge = lv_label_create(card);
    lv_obj_set_style_text_color(s_state_badge, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(s_state_badge, &lv_font_montserrat_20, 0);
    lv_label_set_text(s_state_badge, "—");
    lv_obj_center(s_state_badge);

    /* Row 1: Prev | Toggle | Next */
    int row1_y = 120;
    make_button(scr, 8,   row1_y, 70, 80, 0x4a5060, LV_SYMBOL_PREV, "Prev",    on_prev);
    /* Centre button: a real play/pause toggle. Its icon (captured here, updated
       in refresh) shows the action the next tap performs. */
    lv_obj_t *btn_toggle = make_button(scr, 86, row1_y, 70, 80, 0x457b9d,
                                       LV_SYMBOL_PLAY, "Toggle", on_toggle);
    s_toggle_icon = lv_obj_get_child(btn_toggle, 0);   /* the symbol label */
    make_button(scr, 164, row1_y, 70, 80, 0x4a5060, LV_SYMBOL_NEXT, "Next",    on_next);

    /* Row 2: Play | Pause */
    int row2_y = 216;
    make_button(scr, 8,   row2_y, 112, 76, 0x2a9d8f, LV_SYMBOL_PLAY,  "Play",  on_play);
    make_button(scr, 128, row2_y, 104, 76, 0xf77f00, LV_SYMBOL_PAUSE, "Pause", on_pause);

    refresh();
    hb_lv_set_frame_cb(on_tick);
}
