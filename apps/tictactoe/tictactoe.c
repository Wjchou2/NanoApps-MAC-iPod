/*
 * tictactoe — 4x4 tic-tac-toe vs an AI.
 *
 * Player is X (goes first). AI is O. Win = four in a row (row, column or
 * diagonal). A full 16-cell minimax is far too large to search exhaustively,
 * so the AI uses a depth-limited minimax with a line-potential heuristic at the
 * cap — strong enough to block threats and play sensibly, fast enough per move.
 */
#include "hb_sdk.h"
#include "hb_prefs.h"
#include "lvgl/lvgl.h"

#define N      16            /* 4x4 board */
#define DEPTH_CAP 4          /* plies the AI looks ahead before the heuristic */

/* 0 = empty, 1 = X, 2 = O. */
static int8_t s_board[N];
static int    s_score_x = 0, s_score_o = 0, s_score_d = 0;
static bool   s_game_over = false;
static int    s_turn = 1;            /* whose turn (in vs-human mode) */
typedef enum { MODE_AI, MODE_HUMAN } mode_t;
static mode_t s_mode = MODE_AI;

static lv_obj_t *s_cell_btns[N];
static lv_obj_t *s_cell_labs[N];
static lv_obj_t *s_status_lbl;
static lv_obj_t *s_mode_btn_label;

/* Every four-in-a-row line on a 4x4 board: 4 rows, 4 cols, 2 diagonals. */
static const int8_t WIN_LINES[10][4] = {
    {0,1,2,3},   {4,5,6,7},   {8,9,10,11}, {12,13,14,15},
    {0,4,8,12},  {1,5,9,13},  {2,6,10,14}, {3,7,11,15},
    {0,5,10,15}, {3,6,9,12},
};

static int check_win(const int8_t *b)
{
    for (int i = 0; i < 10; i++) {
        int a = b[WIN_LINES[i][0]];
        if (a && a == b[WIN_LINES[i][1]] &&
                 a == b[WIN_LINES[i][2]] &&
                 a == b[WIN_LINES[i][3]]) return a;
    }
    for (int i = 0; i < N; i++) if (b[i] == 0) return 0;   /* still playable */
    return 3;   /* draw */
}

/* Non-terminal evaluation at the depth cap: score each open line by how many of
   one player's marks it holds (lines contested by both score 0). +ve favours X,
   -ve favours O. */
static int heuristic(const int8_t *b)
{
    static const int W[5] = { 0, 1, 6, 30, 0 };   /* 4 = a win, handled above */
    int score = 0;
    for (int i = 0; i < 10; i++) {
        int x = 0, o = 0;
        for (int j = 0; j < 4; j++) {
            int v = b[WIN_LINES[i][j]];
            if (v == 1) x++; else if (v == 2) o++;
        }
        if (x && o) continue;
        if (x)      score += W[x];
        else if (o) score -= W[o];
    }
    return score;
}

static int minimax(int8_t *b, int player, int depth)
{
    int w = check_win(b);
    if (w == 1) return  1000 - depth;
    if (w == 2) return -1000 + depth;
    if (w == 3) return 0;
    if (depth >= DEPTH_CAP) return heuristic(b);
    int best = (player == 1) ? -100000 : 100000;
    for (int i = 0; i < N; i++) {
        if (b[i]) continue;
        b[i] = (int8_t)player;
        int s = minimax(b, player == 1 ? 2 : 1, depth + 1);
        b[i] = 0;
        if (player == 1) { if (s > best) best = s; }
        else             { if (s < best) best = s; }
    }
    return best;
}

static int best_ai_move(void)
{
    int8_t copy[N];
    for (int i = 0; i < N; i++) copy[i] = s_board[i];
    int best_score = 100000, best_move = -1;
    for (int i = 0; i < N; i++) {
        if (copy[i]) continue;
        copy[i] = 2;
        int s = minimax(copy, 1, 1);
        copy[i] = 0;
        if (s < best_score) { best_score = s; best_move = i; }
    }
    return best_move;
}

static void itoa_u(uint32_t v, char *out)
{
    char b[12]; int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    int k = 0; while (i) out[k++] = b[--i];
    out[k] = 0;
}

static void render(void)
{
    for (int i = 0; i < N; i++) {
        const char *txt = (s_board[i] == 1) ? "X" : (s_board[i] == 2) ? "O" : "";
        lv_label_set_text(s_cell_labs[i], txt);
        lv_obj_set_style_text_color(s_cell_labs[i],
            (s_board[i] == 1) ? lv_color_hex(hb_color_success()) :
            (s_board[i] == 2) ? lv_color_hex(hb_color_danger()) :
                                lv_color_hex(hb_color_text_dim()), 0);
    }
    char buf[64]; int k = 0;
    const char *p = "X "; while (*p) buf[k++] = *p++;
    char nb[12]; itoa_u((uint32_t)s_score_x, nb);
    for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    p = "  O "; while (*p) buf[k++] = *p++;
    itoa_u((uint32_t)s_score_o, nb); for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    p = "  Draws "; while (*p) buf[k++] = *p++;
    itoa_u((uint32_t)s_score_d, nb); for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k] = 0;
    lv_label_set_text(s_status_lbl, buf);
}

static void new_round(void)
{
    for (int i = 0; i < N; i++) s_board[i] = 0;
    s_game_over = false;
    s_turn = 1;
}

static void on_cell(lv_event_t *e)
{
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    if (s_game_over) { new_round(); render(); return; }
    if (s_board[idx]) return;
    if (s_mode == MODE_AI) {
        s_board[idx] = 1;
        int w = check_win(s_board);
        if (!w) {
            int m = best_ai_move();
            if (m >= 0) s_board[m] = 2;
            w = check_win(s_board);
        }
        if (w == 1) { s_score_x++; s_game_over = true; }
        else if (w == 2) { s_score_o++; s_game_over = true; }
        else if (w == 3) { s_score_d++; s_game_over = true; }
    } else {
        /* vs-human: tap places the current player's mark, swap turns. */
        s_board[idx] = (int8_t)s_turn;
        int w = check_win(s_board);
        if (w == 1) { s_score_x++; s_game_over = true; }
        else if (w == 2) { s_score_o++; s_game_over = true; }
        else if (w == 3) { s_score_d++; s_game_over = true; }
        else s_turn = (s_turn == 1) ? 2 : 1;
    }
    render();
}

static void on_mode_toggle(lv_event_t *e)
{
    (void)e;
    s_mode = (s_mode == MODE_AI) ? MODE_HUMAN : MODE_AI;
    s_score_x = s_score_o = s_score_d = 0;
    new_round();
    lv_label_set_text(s_mode_btn_label, s_mode == MODE_AI ? "vs AI" : "vs Human");
    render();
}

HB_APP_ENTRY(payload_entry)
{
    hb_trace_init();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(hb_color_bg()), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Tic-Tac-Toe");
    lv_obj_set_style_text_color(title, lv_color_hex(hb_color_primary()), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);

    lv_obj_t *mode_btn = lv_button_create(scr);
    lv_obj_set_size(mode_btn, 90, 28);
    lv_obj_align(mode_btn, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_set_style_bg_color(mode_btn, lv_color_hex(hb_color_surface()), 0);
    lv_obj_add_event_cb(mode_btn, on_mode_toggle, LV_EVENT_CLICKED, NULL);
    s_mode_btn_label = lv_label_create(mode_btn);
    lv_label_set_text(s_mode_btn_label, "vs AI");
    lv_obj_set_style_text_color(s_mode_btn_label, lv_color_hex(hb_color_text()), 0);
    lv_obj_center(s_mode_btn_label);

    s_status_lbl = lv_label_create(scr);
    lv_label_set_text(s_status_lbl, "");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(hb_color_text_dim()), 0);
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* 4x4 grid filling the width, centred below the header. */
    int cell = 54, gap = 6;
    int grid_w = 4 * cell + 3 * gap;          /* 234 */
    int start_x = (240 - grid_w) / 2;
    int start_y = 78;
    for (int i = 0; i < N; i++) {
        int col = i % 4, row = i / 4;
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_set_size(b, cell, cell);
        lv_obj_set_pos(b, start_x + col * (cell + gap),
                          start_y + row * (cell + gap));
        lv_obj_set_style_bg_color(b, lv_color_hex(hb_color_surface()), 0);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_add_event_cb(b, on_cell, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        s_cell_btns[i] = b;
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, "");
        lv_obj_set_style_text_font(l, &lv_font_montserrat_36, 0);
        lv_obj_center(l);
        s_cell_labs[i] = l;
    }
    render();
}
