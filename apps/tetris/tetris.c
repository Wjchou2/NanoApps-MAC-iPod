/*
 * tetris — falling-blocks game.
 *
 * 10 cols × 20 rows of 22 px cells (220 × 440 logical — clipped to
 * 240 wide with a 10-px frame, and the bottom row drawn at the
 * screen's last row). 7 tetrominoes (I,O,T,S,Z,J,L). Each tick the
 * active piece drops by one cell; collision locks it into the board
 * and a new piece spawns from the bag. Lines that fill across all
 * 10 cells are cleared and the rows above shift down.
 *
 * Input: tap the LEFT half of the playfield to move left, RIGHT
 * half to move right, the TOP region above the playfield to rotate,
 * and the BOTTOM region to soft-drop.
 *
 * Speed ramps gently with line count.
 */

#include "hb_sdk.h"
#include "hb_prefs.h"
#include "lvgl/lvgl.h"

#define COLS        10
#define ROWS        20
#define CELL_PX     22
#define PLAY_W      (COLS * CELL_PX)        /* 220 */
#define PLAY_H      (ROWS * CELL_PX)        /* 440 — clipped to ~360 visible */

#define VIS_ROWS    18                       /* on-screen rows */
#define VIS_PLAY_H  (VIS_ROWS * CELL_PX)     /* 396 */
#define HEADER_H    36

/* Board: each cell is 0 (empty) or 1..7 (tetromino color id). */
static uint8_t s_board[ROWS][COLS];

typedef struct { int x, y; } pt_t;

/* Each piece: 4 cells at rotations 0..3. We store all 4 rotations
   pre-computed. Source coords are centered around (0,0). */
typedef struct {
    pt_t rot[4][4];
    uint32_t color;
} piece_t;

static const piece_t s_pieces[7] = {
    /* I */ { { { {-1,0},{0,0},{1,0},{2,0} },
              { {1,-1},{1,0},{1,1},{1,2} },
              { {-1,1},{0,1},{1,1},{2,1} },
              { {0,-1},{0,0},{0,1},{0,2} } }, 0x00d4ff },
    /* O */ { { { {0,0},{1,0},{0,1},{1,1} },
              { {0,0},{1,0},{0,1},{1,1} },
              { {0,0},{1,0},{0,1},{1,1} },
              { {0,0},{1,0},{0,1},{1,1} } }, 0xfcd34d },
    /* T */ { { { {-1,0},{0,0},{1,0},{0,1} },
              { {0,-1},{0,0},{0,1},{1,0} },
              { {-1,0},{0,0},{1,0},{0,-1} },
              { {0,-1},{0,0},{0,1},{-1,0} } }, 0xa663cc },
    /* S */ { { { {0,0},{1,0},{-1,1},{0,1} },
              { {0,-1},{0,0},{1,0},{1,1} },
              { {0,0},{1,0},{-1,1},{0,1} },
              { {0,-1},{0,0},{1,0},{1,1} } }, 0x2a9d8f },
    /* Z */ { { { {-1,0},{0,0},{0,1},{1,1} },
              { {1,-1},{0,0},{1,0},{0,1} },
              { {-1,0},{0,0},{0,1},{1,1} },
              { {1,-1},{0,0},{1,0},{0,1} } }, 0xe63946 },
    /* J */ { { { {-1,0},{0,0},{1,0},{1,1} },
              { {0,-1},{0,0},{0,1},{1,-1} },
              { {-1,0},{0,0},{1,0},{-1,-1} },
              { {0,-1},{0,0},{0,1},{-1,1} } }, 0x457b9d },
    /* L */ { { { {-1,0},{0,0},{1,0},{-1,1} },
              { {0,-1},{0,0},{0,1},{1,1} },
              { {-1,0},{0,0},{1,0},{1,-1} },
              { {0,-1},{0,0},{0,1},{-1,-1} } }, 0xf77f00 },
};

/* Active piece state */
static int s_piece_idx;
static int s_rot;
static int s_px, s_py;        /* center coordinates on the board */

/* Score + speed */
static int s_lines;
static int s_score;
static uint32_t s_step_ms;
static uint32_t s_last_step_ms;
static bool s_dead;

/* Rendering: one lv_obj per cell, pre-allocated. */
static lv_obj_t *s_play_obj;
static lv_obj_t *s_cells[VIS_ROWS][COLS];
static lv_obj_t *s_score_lbl;
static lv_obj_t *s_overlay;
static lv_obj_t *s_overlay_lbl;

/* PRNG */
static uint32_t s_rng;
static uint32_t rnd(void) {
    s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
    return s_rng;
}

static void itoa_u(uint32_t v, char *out)
{
    char b[12]; int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    int k = 0; while (i) out[k++] = b[--i];
    out[k] = 0;
}

static const uint32_t s_color_for[8] = {
    0x111522,
    0x00d4ff, 0xfcd34d, 0xa663cc, 0x2a9d8f,
    0xe63946, 0x457b9d, 0xf77f00,
};

/* True if placing the piece at (cx, cy, rotation) would collide
   with the board or walls. */
static bool collides(int idx, int rot, int cx, int cy)
{
    for (int i = 0; i < 4; i++) {
        int x = cx + s_pieces[idx].rot[rot][i].x;
        int y = cy + s_pieces[idx].rot[rot][i].y;
        if (x < 0 || x >= COLS) return true;
        if (y >= ROWS) return true;
        if (y < 0) continue;                /* above the board is fine */
        if (s_board[y][x] != 0) return true;
    }
    return false;
}

static void lock_piece(void)
{
    for (int i = 0; i < 4; i++) {
        int x = s_px + s_pieces[s_piece_idx].rot[s_rot][i].x;
        int y = s_py + s_pieces[s_piece_idx].rot[s_rot][i].y;
        if (y >= 0 && y < ROWS && x >= 0 && x < COLS) {
            s_board[y][x] = (uint8_t)(s_piece_idx + 1);
        }
    }
}

/* Clear full rows; return number cleared. */
static int clear_rows(void)
{
    int cleared = 0;
    for (int y = ROWS - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < COLS; x++) if (!s_board[y][x]) { full = false; break; }
        if (full) {
            cleared++;
            for (int yy = y; yy > 0; yy--) {
                for (int x = 0; x < COLS; x++) s_board[yy][x] = s_board[yy-1][x];
            }
            for (int x = 0; x < COLS; x++) s_board[0][x] = 0;
            y++;        /* re-check same row, now containing what was above */
        }
    }
    return cleared;
}

static void spawn_piece(void)
{
    s_piece_idx = (int)(rnd() % 7);
    s_rot = 0;
    s_px = COLS / 2;
    s_py = 0;
    if (collides(s_piece_idx, s_rot, s_px, s_py)) {
        s_dead = true;
    }
}

/* ---- rendering ---- */

static void refresh_score(void)
{
    char buf[32]; int k = 0;
    const char *p = "Lines: "; while (*p) buf[k++] = *p++;
    char nb[12]; itoa_u((uint32_t)s_lines, nb);
    for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    p = "  "; while (*p) buf[k++] = *p++;
    itoa_u((uint32_t)s_score, nb);
    for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k] = 0;
    lv_label_set_text(s_score_lbl, buf);
}

/* Snapshot of the last colors we pushed to each cell so we can do
   change-detection rendering. Re-applying a style invalidates the
   widget and forces LVGL to re-composite it, so for a static board
   with a 4-cell active piece we want to touch ~8 cells per render
   (4 old positions + 4 new), not all 180. Before this, the render
   was hot enough to starve input polling. */
static uint32_t s_last_cell_color[VIS_ROWS][COLS];

static void set_cell_color(int vy, int x, uint32_t col)
{
    if (s_last_cell_color[vy][x] == col) return;
    s_last_cell_color[vy][x] = col;
    lv_obj_set_style_bg_color(s_cells[vy][x], lv_color_hex(col), 0);
}

static void render_board(void)
{
    /* Board cells (locked tetrominoes + empty). */
    for (int y = 0; y < VIS_ROWS; y++) {
        int by = y + (ROWS - VIS_ROWS);
        for (int x = 0; x < COLS; x++) {
            set_cell_color(y, x, s_color_for[s_board[by][x]]);
        }
    }
    /* Overlay the active piece (after the board pass so it paints
       on top). The set_cell_color call still no-ops when the cell
       already has the active piece's color. */
    if (!s_dead) {
        uint32_t col = s_pieces[s_piece_idx].color;
        for (int i = 0; i < 4; i++) {
            int x = s_px + s_pieces[s_piece_idx].rot[s_rot][i].x;
            int y = s_py + s_pieces[s_piece_idx].rot[s_rot][i].y;
            int vy = y - (ROWS - VIS_ROWS);
            if (vy < 0 || vy >= VIS_ROWS || x < 0 || x >= COLS) continue;
            set_cell_color(vy, x, col);
        }
    }
}

static void show_game_over(void)
{
    char buf[64]; int k = 0;
    const char *p = "Game Over\nLines: "; while (*p) buf[k++] = *p++;
    char nb[12]; itoa_u((uint32_t)s_lines, nb);
    for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    p = "\nTap to retry"; while (*p) buf[k++] = *p++;
    buf[k] = 0;
    lv_label_set_text(s_overlay_lbl, buf);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

/* Reset to fresh game */
static void reset_game(void)
{
    for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) s_board[y][x] = 0;
    s_lines = 0;
    s_score = 0;
    s_step_ms = 500;
    s_dead = false;
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    spawn_piece();
    render_board();
    refresh_score();
}

/* ---- simulation step ---- */

static void try_move(int dx)
{
    if (s_dead) return;
    if (!collides(s_piece_idx, s_rot, s_px + dx, s_py)) s_px += dx;
}

static void try_rotate(void)
{
    if (s_dead) return;
    int nr = (s_rot + 1) % 4;
    /* Try base position; if blocked, try wall-kick by ±1 col. */
    if (!collides(s_piece_idx, nr, s_px, s_py)) { s_rot = nr; return; }
    if (!collides(s_piece_idx, nr, s_px - 1, s_py)) { s_px--; s_rot = nr; return; }
    if (!collides(s_piece_idx, nr, s_px + 1, s_py)) { s_px++; s_rot = nr; return; }
}

static void step_down(bool soft_drop)
{
    if (s_dead) return;
    if (!collides(s_piece_idx, s_rot, s_px, s_py + 1)) {
        s_py++;
        if (soft_drop) s_score += 1;
    } else {
        lock_piece();
        int c = clear_rows();
        if (c) {
            s_lines += c;
            /* Tetris-y scoring: 100/300/500/800 */
            int delta = (c == 1) ? 100 : (c == 2) ? 300 : (c == 3) ? 500 : 800;
            s_score += delta;
            if (s_step_ms > 100u) s_step_ms -= (uint32_t)(c * 10);
        }
        spawn_piece();
        if (s_dead) show_game_over();
    }
    refresh_score();
}

/* ---- input ---- */

static void on_play_press(lv_event_t *e)
{
    (void)e;
    if (s_dead) {
        reset_game();
        return;
    }
    lv_indev_t *ind = lv_indev_active(); if (!ind) return;
    lv_point_t p; lv_indev_get_point(ind, &p);
    /* Quadrant input: top = rotate, bottom = drop, left = move
       left, right = move right. Compute relative to playfield. */
    lv_area_t area; lv_obj_get_coords(s_play_obj, &area);
    int local_x = p.x - area.x1;
    int local_y = p.y - area.y1;
    int w = area.x2 - area.x1 + 1;
    int h = area.y2 - area.y1 + 1;
    /* Top 25 % = rotate, bottom 25 % = soft drop, otherwise side
       quadrants for L/R. */
    if (local_y < h / 4) {
        try_rotate();
    } else if (local_y > h * 3 / 4) {
        step_down(true);
    } else if (local_x < w / 2) {
        try_move(-1);
    } else {
        try_move(+1);
    }
    render_board();
}

static void on_frame(void)
{
    if (s_dead) return;
    uint32_t now = hb_time_uptime_ms();
    if ((uint32_t)(now - s_last_step_ms) >= s_step_ms) {
        s_last_step_ms = now;
        step_down(false);
        render_board();
    }
}

HB_APP_ENTRY(payload_entry)
{
    hb_trace_init();
    s_rng = hb_time_uptime_us() | 1u;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(hb_color_bg()), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_score_lbl = lv_label_create(scr);
    lv_label_set_text(s_score_lbl, "Lines: 0");
    lv_obj_set_style_text_color(s_score_lbl, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_text_font(s_score_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(s_score_lbl, LV_ALIGN_TOP_LEFT, 8, 8);

    /* Controls aren't obvious (tap zones) — the rotate one especially. */
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "tap top to rotate");
    lv_obj_set_style_text_color(hint, lv_color_hex(hb_color_text_dim()), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -8, 12);

    s_play_obj = lv_obj_create(scr);
    lv_obj_set_size(s_play_obj, PLAY_W, VIS_PLAY_H);
    lv_obj_align(s_play_obj, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_play_obj, lv_color_hex(0x111522), 0);
    lv_obj_set_style_border_width(s_play_obj, 0, 0);
    lv_obj_set_style_pad_all(s_play_obj, 0, 0);
    lv_obj_set_style_radius(s_play_obj, 0, 0);
    lv_obj_clear_flag(s_play_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_play_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_play_obj, on_play_press, LV_EVENT_PRESSED, NULL);

    /* Pre-allocate the visible cells. */
    for (int y = 0; y < VIS_ROWS; y++) for (int x = 0; x < COLS; x++) {
        lv_obj_t *c = lv_obj_create(s_play_obj);
        lv_obj_set_size(c, CELL_PX - 1, CELL_PX - 1);
        lv_obj_set_pos(c, x * CELL_PX, y * CELL_PX);
        lv_obj_set_style_bg_color(c, lv_color_hex(0x111522), 0);
        lv_obj_set_style_radius(c, 2, 0);
        lv_obj_set_style_border_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_set_style_shadow_width(c, 0, 0);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
        s_cells[y][x] = c;
    }

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
    s_last_step_ms = hb_time_uptime_ms();
    hb_lv_set_frame_cb(on_frame);
}
