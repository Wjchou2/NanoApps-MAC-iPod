/*
 * pong — single-player vs AI paddle.
 *
 * Vertical pong because the device is taller than wide. Top edge is
 * the AI paddle, bottom edge is the player paddle. Drag anywhere on
 * the playfield to move your paddle horizontally. Ball physics is
 * straight-line + reflective collisions off the side walls and both
 * paddles. The AI lerps toward the ball's current x; lag is the only
 * difficulty knob (capped per-frame movement speed).
 *
 * First to 7 points wins; tap "Play Again" to restart.
 */

#include "hb_sdk.h"
#include "hb_prefs.h"
#include "lvgl/lvgl.h"

#define PLAY_W       240
#define PLAY_H       380     /* 432 - 52 header */
#define PADDLE_W     80
#define PADDLE_H     14
#define BALL_R       7

/* Player paddle sits well above the bottom edge so the user's
   finger doesn't physically cover the bar it's controlling. The
   ball-loss line is still the playfield bottom — anything past the
   paddle's bottom edge counts as a miss. */
#define AI_PADDLE_Y      8
#define PLAYER_PADDLE_Y  (PLAY_H - PADDLE_H - 60)

#define WIN_SCORE 7

typedef struct {
    int x, y;       /* center coords */
    int vx, vy;     /* px per frame */
} ball_t;

static ball_t s_ball;
static int    s_ai_x      = (PLAY_W - PADDLE_W) / 2;
static int    s_player_x  = (PLAY_W - PADDLE_W) / 2;
static int    s_score_ai  = 0;
static int    s_score_pl  = 0;
static bool   s_paused    = false;       /* between rounds */
static bool   s_game_over = false;

static lv_obj_t *s_play_obj;
static lv_obj_t *s_ai_paddle;
static lv_obj_t *s_pl_paddle;
static lv_obj_t *s_ball_obj;
static lv_obj_t *s_score_lbl;
static lv_obj_t *s_msg_lbl;
static lv_obj_t *s_overlay;
static lv_obj_t *s_overlay_lbl;

/* PRNG */
static uint32_t s_rng;
static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* ---- helpers ---- */

static void itoa_u(uint32_t v, char *out)
{
    char b[12]; int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    int k = 0; while (i) out[k++] = b[--i];
    out[k] = 0;
}

static void reset_ball(int toward_player)
{
    s_ball.x = PLAY_W / 2;
    s_ball.y = PLAY_H / 2;
    s_ball.vx = ((int)(rnd() % 3)) - 1;   /* -1..1 */
    if (s_ball.vx == 0) s_ball.vx = 1;
    s_ball.vy = toward_player ? 3 : -3;
}

static void refresh_score(void)
{
    char buf[24]; int k = 0;
    char nb[12]; itoa_u((uint32_t)s_score_ai, nb);
    for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k++] = ' '; buf[k++] = ':'; buf[k++] = ' ';
    itoa_u((uint32_t)s_score_pl, nb);
    for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k] = 0;
    lv_label_set_text(s_score_lbl, buf);
}

/* ---- per-frame tick ---- */

static void on_frame(void)
{
    if (s_game_over) return;

    /* AI: lerp toward ball x, capped speed. */
    int target = s_ball.x - PADDLE_W / 2;
    int diff = target - s_ai_x;
    int adiff = diff < 0 ? -diff : diff;
    int step = adiff > 3 ? (diff > 0 ? 3 : -3) : diff;
    s_ai_x += step;
    if (s_ai_x < 0) s_ai_x = 0;
    if (s_ai_x > PLAY_W - PADDLE_W) s_ai_x = PLAY_W - PADDLE_W;
    lv_obj_set_pos(s_ai_paddle, s_ai_x, AI_PADDLE_Y);

    if (s_paused) return;

    /* Move ball */
    s_ball.x += s_ball.vx;
    s_ball.y += s_ball.vy;

    /* Side walls */
    if (s_ball.x - BALL_R < 0) { s_ball.x = BALL_R; s_ball.vx = -s_ball.vx; }
    if (s_ball.x + BALL_R > PLAY_W) { s_ball.x = PLAY_W - BALL_R; s_ball.vx = -s_ball.vx; }

    /* AI paddle (top) */
    if (s_ball.y - BALL_R <= AI_PADDLE_Y + PADDLE_H &&
        s_ball.x >= s_ai_x && s_ball.x <= s_ai_x + PADDLE_W &&
        s_ball.vy < 0) {
        s_ball.y = AI_PADDLE_Y + PADDLE_H + BALL_R;
        s_ball.vy = -s_ball.vy;
        /* English: spin x velocity based on where it struck the paddle */
        int offset = s_ball.x - (s_ai_x + PADDLE_W / 2);
        s_ball.vx += offset / 12;
        if (s_ball.vx > 5) s_ball.vx = 5;
        if (s_ball.vx < -5) s_ball.vx = -5;
    }

    /* Player paddle (bottom) */
    if (s_ball.y + BALL_R >= PLAYER_PADDLE_Y &&
        s_ball.x >= s_player_x && s_ball.x <= s_player_x + PADDLE_W &&
        s_ball.vy > 0) {
        s_ball.y = PLAYER_PADDLE_Y - BALL_R;
        s_ball.vy = -s_ball.vy;
        int offset = s_ball.x - (s_player_x + PADDLE_W / 2);
        s_ball.vx += offset / 12;
        if (s_ball.vx > 5) s_ball.vx = 5;
        if (s_ball.vx < -5) s_ball.vx = -5;
    }

    /* Score */
    if (s_ball.y < -BALL_R) {
        s_score_pl++;
        if (s_score_pl >= WIN_SCORE) {
            lv_label_set_text(s_overlay_lbl, "You win!\nTap to play again");
            lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
            s_game_over = true;
        } else {
            reset_ball(1);
            lv_label_set_text(s_msg_lbl, "Tap to serve");
            s_paused = true;
        }
        refresh_score();
    } else if (s_ball.y > PLAY_H + BALL_R) {
        s_score_ai++;
        if (s_score_ai >= WIN_SCORE) {
            lv_label_set_text(s_overlay_lbl, "AI wins\nTap to play again");
            lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
            s_game_over = true;
        } else {
            reset_ball(1);
            lv_label_set_text(s_msg_lbl, "Tap to serve");
            s_paused = true;
        }
        refresh_score();
    }

    lv_obj_set_pos(s_ball_obj, s_ball.x - BALL_R, s_ball.y - BALL_R);
}

/* ---- input ---- */

static void on_play_pressing(lv_event_t *e)
{
    (void)e;
    lv_indev_t *ind = lv_indev_active(); if (!ind) return;
    lv_point_t p; lv_indev_get_point(ind, &p);
    /* p.x is in screen coords; subtract play_obj x. */
    int x = p.x - lv_obj_get_x(s_play_obj);
    s_player_x = x - PADDLE_W / 2;
    if (s_player_x < 0) s_player_x = 0;
    if (s_player_x > PLAY_W - PADDLE_W) s_player_x = PLAY_W - PADDLE_W;
    lv_obj_set_pos(s_pl_paddle, s_player_x, PLAYER_PADDLE_Y);
}

static void on_play_pressed(lv_event_t *e)
{
    on_play_pressing(e);
    if (s_game_over) {
        s_score_ai = s_score_pl = 0;
        s_game_over = false;
        reset_ball(1);
        s_paused = true;
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_msg_lbl, "Tap to serve");
        refresh_score();
        return;
    }
    if (s_paused) {
        s_paused = false;
        lv_label_set_text(s_msg_lbl, "");
    }
}

HB_APP_ENTRY(payload_entry)
{
    hb_trace_init();
    s_rng = hb_time_uptime_us() | 1u;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(hb_color_bg()), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Header */
    s_score_lbl = lv_label_create(scr);
    lv_label_set_text(s_score_lbl, "0 : 0");
    lv_obj_set_style_text_color(s_score_lbl, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_text_font(s_score_lbl, &lv_font_montserrat_28, 0);
    lv_obj_align(s_score_lbl, LV_ALIGN_TOP_MID, 0, 14);

    /* Playfield */
    s_play_obj = lv_obj_create(scr);
    lv_obj_set_size(s_play_obj, PLAY_W, PLAY_H);
    lv_obj_align(s_play_obj, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_play_obj, lv_color_hex(0x111522), 0);
    lv_obj_set_style_border_width(s_play_obj, 0, 0);
    lv_obj_set_style_pad_all(s_play_obj, 0, 0);
    lv_obj_set_style_radius(s_play_obj, 0, 0);
    lv_obj_clear_flag(s_play_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_play_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_play_obj, on_play_pressed,  LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(s_play_obj, on_play_pressing, LV_EVENT_PRESSING, NULL);

    /* Center line — purely decorative */
    lv_obj_t *cline = lv_obj_create(s_play_obj);
    lv_obj_set_size(cline, PLAY_W, 1);
    lv_obj_set_pos(cline, 0, PLAY_H / 2);
    lv_obj_set_style_bg_color(cline, lv_color_hex(hb_color_text_dim()), 0);
    lv_obj_set_style_border_width(cline, 0, 0);
    lv_obj_clear_flag(cline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(cline, LV_OBJ_FLAG_CLICKABLE);

    /* Paddles + ball */
    s_ai_paddle = lv_obj_create(s_play_obj);
    lv_obj_set_size(s_ai_paddle, PADDLE_W, PADDLE_H);
    lv_obj_set_pos(s_ai_paddle, s_ai_x, AI_PADDLE_Y);
    lv_obj_set_style_bg_color(s_ai_paddle, lv_color_hex(hb_color_danger()), 0);
    lv_obj_set_style_radius(s_ai_paddle, 4, 0);
    lv_obj_set_style_border_width(s_ai_paddle, 0, 0);
    lv_obj_clear_flag(s_ai_paddle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_ai_paddle, LV_OBJ_FLAG_CLICKABLE);

    s_pl_paddle = lv_obj_create(s_play_obj);
    lv_obj_set_size(s_pl_paddle, PADDLE_W, PADDLE_H);
    lv_obj_set_pos(s_pl_paddle, s_player_x, PLAYER_PADDLE_Y);
    lv_obj_set_style_bg_color(s_pl_paddle, lv_color_hex(hb_color_success()), 0);
    lv_obj_set_style_radius(s_pl_paddle, 4, 0);
    lv_obj_set_style_border_width(s_pl_paddle, 0, 0);
    lv_obj_clear_flag(s_pl_paddle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_pl_paddle, LV_OBJ_FLAG_CLICKABLE);

    s_ball_obj = lv_obj_create(s_play_obj);
    lv_obj_set_size(s_ball_obj, BALL_R * 2, BALL_R * 2);
    lv_obj_set_style_radius(s_ball_obj, BALL_R, 0);
    lv_obj_set_style_bg_color(s_ball_obj, lv_color_hex(0xfcbf49), 0);
    lv_obj_set_style_border_width(s_ball_obj, 0, 0);
    lv_obj_clear_flag(s_ball_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_ball_obj, LV_OBJ_FLAG_CLICKABLE);

    s_msg_lbl = lv_label_create(s_play_obj);
    lv_label_set_text(s_msg_lbl, "Tap to serve");
    lv_obj_set_style_text_color(s_msg_lbl, lv_color_hex(hb_color_text_dim()), 0);
    lv_obj_set_style_text_font(s_msg_lbl, &lv_font_montserrat_16, 0);
    /* Up off centre so it doesn't sit on the middle net line. */
    lv_obj_align(s_msg_lbl, LV_ALIGN_CENTER, 0, -48);

    /* Game-over overlay */
    s_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_overlay, 200, 120);
    lv_obj_center(s_overlay);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(hb_color_surface()), 0);
    lv_obj_set_style_border_color(s_overlay, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_border_width(s_overlay, 2, 0);
    lv_obj_set_style_radius(s_overlay, 12, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    s_overlay_lbl = lv_label_create(s_overlay);
    lv_obj_set_style_text_color(s_overlay_lbl, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(s_overlay_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(s_overlay_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_overlay_lbl);

    reset_ball(1);
    s_paused = true;
    refresh_score();

    hb_lv_set_frame_cb(on_frame);
}
