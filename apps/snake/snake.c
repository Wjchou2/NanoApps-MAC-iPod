/*
 * snake — classic snake game.
 *
 * Grid: 12 cols × 18 rows of 20-px cells (240 × 360 playfield, with a
 * 72-px header for score + game-over overlay). The snake is a ring
 * buffer of (x,y) cells; on each step we push the new head in the
 * current direction and (if no food was eaten) pop the tail. Food
 * respawns on any free cell when eaten.
 *
 * Input: swipe in any cardinal direction on the playfield to turn.
 * Reversing onto yourself is rejected. Speed scales mildly with
 * score so the game ramps up.
 */

#include "hb_sdk.h"
#include "hb_prefs.h"
#include "lvgl/lvgl.h"

#define COLS        12
#define ROWS        18
#define CELL_PX     20
#define HEADER_H    72
#define PLAY_W      (COLS * CELL_PX)        /* 240 */
#define PLAY_H      (ROWS * CELL_PX)        /* 360 */
#define MAX_LEN     (COLS * ROWS)

typedef struct { int8_t x, y; } cell_t;

static cell_t s_snake[MAX_LEN];
static int    s_head = 0;          /* next slot to write a head into */
static int    s_len  = 0;
static int    s_dx = 1, s_dy = 0;  /* current direction */
static int    s_pending_dx = 1, s_pending_dy = 0;  /* applied at next step */
static cell_t s_food;
static int    s_score = 0;
static bool   s_dead = false;
static uint32_t s_last_step_ms = 0;
static uint32_t s_step_period_ms = 180;

static lv_obj_t *s_play_obj;
static lv_obj_t *s_head_obj;          /* head sprite (visual snake-head cell) */
static lv_obj_t *s_food_obj;
static lv_obj_t *s_body_cells[MAX_LEN];   /* visual cells indexed parallel to s_snake[] */
static lv_obj_t *s_score_lbl;
static lv_obj_t *s_overlay;
static lv_obj_t *s_overlay_lbl;

/* Touch state for swipe gesture. */
static int      s_press_x, s_press_y;
static bool     s_pressing = false;

/* ---- tiny PRNG (xorshift32, seeded from the µs counter) ---- */
static uint32_t s_rng;
static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* ---- snake ring buffer accessors ---- */

static cell_t *snake_at(int i)  /* 0 = oldest (tail), s_len-1 = newest (head) */
{
    int start = (s_head - s_len + MAX_LEN) % MAX_LEN;
    return &s_snake[(start + i) % MAX_LEN];
}

static bool cell_in_snake(int x, int y)
{
    for (int i = 0; i < s_len; i++) {
        cell_t *c = snake_at(i);
        if (c->x == x && c->y == y) return true;
    }
    return false;
}

static void place_food(void)
{
    /* sample until we hit an empty cell */
    for (int tries = 0; tries < 200; tries++) {
        int x = (int)(rnd() % COLS);
        int y = (int)(rnd() % ROWS);
        if (!cell_in_snake(x, y)) { s_food.x = x; s_food.y = y; return; }
    }
    /* fallback: scan */
    for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) {
        if (!cell_in_snake(x, y)) { s_food.x = x; s_food.y = y; return; }
    }
}

static void reset_game(void)
{
    s_len = 3;
    s_head = 0;
    /* Seed snake going right from center. */
    for (int i = 0; i < s_len; i++) {
        s_snake[i].x = COLS / 2 - s_len + 1 + i;
        s_snake[i].y = ROWS / 2;
    }
    s_head = s_len;
    s_dx = 1; s_dy = 0;
    s_pending_dx = 1; s_pending_dy = 0;
    s_score = 0;
    s_dead = false;
    s_step_period_ms = 180;
    place_food();
}

/* ---- view ---- */

static void itoa_u(uint32_t v, char *out)
{
    char b[12]; int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    int k = 0; while (i) out[k++] = b[--i];
    out[k] = 0;
}

static void refresh_score(void)
{
    char buf[24]; int k = 0;
    const char *p = "Score: "; while (*p) buf[k++] = *p++;
    char nb[12]; itoa_u((uint32_t)s_score, nb);
    for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k] = 0;
    lv_label_set_text(s_score_lbl, buf);
}

static void place_cell(lv_obj_t *o, int x, int y)
{
    lv_obj_set_pos(o, x * CELL_PX, y * CELL_PX);
}

/* Rebuild the snake's visual cells. We allocate up to MAX_LEN once
   and toggle visibility / position each frame. */
static void refresh_snake(void)
{
    for (int i = 0; i < MAX_LEN; i++) {
        lv_obj_add_flag(s_body_cells[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < s_len; i++) {
        cell_t *c = snake_at(i);
        lv_obj_t *o = s_body_cells[i];
        place_cell(o, c->x, c->y);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
        /* Color: head bright, body medium, tail faded. */
        uint32_t col = (i == s_len - 1) ? 0x2a9d8f
                     : (i == 0)         ? 0x1a5c54
                                        : 0x2a9d8f;
        lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
    }
    /* Head sprite: position over the head cell with a brighter color. */
    if (s_len > 0) {
        cell_t *h = snake_at(s_len - 1);
        place_cell(s_head_obj, h->x, h->y);
        lv_obj_clear_flag(s_head_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_head_obj, LV_OBJ_FLAG_HIDDEN);
    }
    place_cell(s_food_obj, s_food.x, s_food.y);
}

static void show_game_over(void)
{
    char buf[40]; int k = 0;
    const char *p = "Game Over\nScore: "; while (*p) buf[k++] = *p++;
    char nb[12]; itoa_u((uint32_t)s_score, nb);
    for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    p = "\nTap to retry"; while (*p) buf[k++] = *p++;
    buf[k] = 0;
    lv_label_set_text(s_overlay_lbl, buf);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void hide_game_over(void)
{
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

/* ---- step the simulation ---- */

static void step(void)
{
    if (s_dead) return;

    /* Apply pending direction if it isn't a reversal. */
    if (!(s_pending_dx == -s_dx && s_pending_dy == -s_dy)) {
        s_dx = s_pending_dx;
        s_dy = s_pending_dy;
    }

    cell_t *head = snake_at(s_len - 1);
    int nx = head->x + s_dx;
    int ny = head->y + s_dy;

    /* Walls. */
    if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) {
        s_dead = true;
        show_game_over();
        return;
    }

    /* Self-collision check — but skip the current tail since it
       moves out of the way on this same step (unless we eat). */
    bool ate = (nx == s_food.x && ny == s_food.y);
    for (int i = (ate ? 0 : 1); i < s_len; i++) {
        cell_t *c = snake_at(i);
        if (c->x == nx && c->y == ny) {
            s_dead = true;
            show_game_over();
            return;
        }
    }

    /* Append new head; pop old tail unless we ate. */
    s_snake[s_head].x = (int8_t)nx;
    s_snake[s_head].y = (int8_t)ny;
    s_head = (s_head + 1) % MAX_LEN;
    if (ate) {
        s_len++;
        s_score++;
        place_food();
        /* Speed up slightly — caps at ~70 ms per step. */
        if (s_step_period_ms > 75) s_step_period_ms -= 5;
    }

    refresh_snake();
    refresh_score();
}

/* ---- input ---- */

static void on_play_press(lv_event_t *e)
{
    (void)e;
    if (s_dead) {
        /* Restart on tap. */
        reset_game();
        hide_game_over();
        refresh_snake();
        refresh_score();
        return;
    }
    lv_indev_t *ind = lv_indev_active();
    if (!ind) return;
    lv_point_t p; lv_indev_get_point(ind, &p);
    s_press_x = p.x;
    s_press_y = p.y;
    s_pressing = true;
}

static void on_play_release(lv_event_t *e)
{
    (void)e;
    if (!s_pressing) return;
    s_pressing = false;
    lv_indev_t *ind = lv_indev_active();
    if (!ind) return;
    lv_point_t p; lv_indev_get_point(ind, &p);
    int dx = p.x - s_press_x;
    int dy = p.y - s_press_y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx < 12 && ady < 12) return;       /* too small — ignore */
    if (adx > ady) {
        s_pending_dx = dx > 0 ? 1 : -1;
        s_pending_dy = 0;
    } else {
        s_pending_dx = 0;
        s_pending_dy = dy > 0 ? 1 : -1;
    }
}

/* Frame-level driver — stepped by the main loop. */
static void on_frame(void)
{
    uint32_t now = hb_time_uptime_ms();
    if ((uint32_t)(now - s_last_step_ms) >= s_step_period_ms) {
        s_last_step_ms = now;
        step();
    }
}

HB_APP_ENTRY(payload_entry)
{
    hb_trace_init();

    s_rng = hb_time_uptime_us() | 1u;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(hb_color_bg()), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Header: score */
    s_score_lbl = lv_label_create(scr);
    lv_label_set_text(s_score_lbl, "Score: 0");
    lv_obj_set_style_text_color(s_score_lbl, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_text_font(s_score_lbl, &lv_font_montserrat_28, 0);
    lv_obj_align(s_score_lbl, LV_ALIGN_TOP_MID, 0, 20);

    /* Playfield. */
    s_play_obj = lv_obj_create(scr);
    lv_obj_set_size(s_play_obj, PLAY_W, PLAY_H);
    lv_obj_align(s_play_obj, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_play_obj, lv_color_hex(0x111522), 0);
    lv_obj_set_style_border_width(s_play_obj, 0, 0);
    lv_obj_set_style_pad_all(s_play_obj, 0, 0);
    lv_obj_set_style_radius(s_play_obj, 0, 0);
    lv_obj_clear_flag(s_play_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_play_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_play_obj, on_play_press,   LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(s_play_obj, on_play_release, LV_EVENT_RELEASED, NULL);

    /* Pre-allocate snake body cells. */
    for (int i = 0; i < MAX_LEN; i++) {
        lv_obj_t *o = lv_obj_create(s_play_obj);
        lv_obj_set_size(o, CELL_PX - 2, CELL_PX - 2);
        lv_obj_set_style_radius(o, 3, 0);
        lv_obj_set_style_border_width(o, 0, 0);
        lv_obj_set_style_pad_all(o, 0, 0);
        lv_obj_set_style_bg_color(o, lv_color_hex(hb_color_success()), 0);
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
        s_body_cells[i] = o;
    }

    /* Head sprite (drawn on top with brighter color). */
    s_head_obj = lv_obj_create(s_play_obj);
    lv_obj_set_size(s_head_obj, CELL_PX - 2, CELL_PX - 2);
    lv_obj_set_style_radius(s_head_obj, 4, 0);
    lv_obj_set_style_border_width(s_head_obj, 0, 0);
    lv_obj_set_style_pad_all(s_head_obj, 0, 0);
    lv_obj_set_style_bg_color(s_head_obj, lv_color_hex(0x7fffd4), 0);
    lv_obj_clear_flag(s_head_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_head_obj, LV_OBJ_FLAG_CLICKABLE);

    /* Food sprite. */
    s_food_obj = lv_obj_create(s_play_obj);
    lv_obj_set_size(s_food_obj, CELL_PX - 4, CELL_PX - 4);
    lv_obj_set_style_radius(s_food_obj, (CELL_PX - 4) / 2, 0);
    lv_obj_set_style_border_width(s_food_obj, 0, 0);
    lv_obj_set_style_pad_all(s_food_obj, 0, 0);
    lv_obj_set_style_bg_color(s_food_obj, lv_color_hex(hb_color_danger()), 0);
    lv_obj_clear_flag(s_food_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_food_obj, LV_OBJ_FLAG_CLICKABLE);

    /* Game-over overlay (hidden by default). */
    s_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_overlay, 200, 140);
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

    reset_game();
    refresh_snake();
    refresh_score();
    s_last_step_ms = hb_time_uptime_ms();

    hb_lv_set_frame_cb(on_frame);
}
