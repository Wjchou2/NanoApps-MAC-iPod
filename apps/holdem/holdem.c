/*
 * holdem — heads-up Texas Hold'em vs a simple AI.
 *
 * Blinds 10/20. Each side starts with 1000 chips. Streets:
 * pre-flop, flop, turn, river, showdown. Actions: fold,
 * check/call, raise (+50). AI uses a rough hand-strength estimator
 * (preflop chen-like score; post-flop seven-card eval) to decide.
 *
 * Hand evaluation enumerates all C(7,5)=21 combinations and picks
 * the highest. Categories packed into a 32-bit score for ordering.
 */
#include "hb_sdk.h"
#include "hb_prefs.h"
#include "lvgl/lvgl.h"

#define START_STACK 1000
#define SMALL_BLIND 10
#define BIG_BLIND   20

/* card = rank*4 + suit;  rank 0..12 (2..A); suit 0..3 (C D H S) */
typedef uint8_t card_t;
#define CARD_RANK(c) ((c) >> 2)
#define CARD_SUIT(c) ((c) & 3)

static const char s_rank_ch[13] = {'2','3','4','5','6','7','8','9','T','J','Q','K','A'};
static const char *s_suit_ch[4] = { "C", "D", "H", "S" };
static const uint32_t s_suit_color[4] = { 0x222222, 0xc62828, 0xc62828, 0x222222 };

static card_t s_deck[52];
static int    s_deck_pos;
static card_t s_hole_p[2];      /* player */
static card_t s_hole_a[2];      /* ai */
static card_t s_board[5];
static int    s_board_n;

static int s_pot;
static int s_p_chips, s_a_chips;
static int s_p_bet, s_a_bet;      /* amount put in this street */
static int s_to_act;              /* 0 player, 1 ai */
static int s_street;              /* 0 pre, 1 flop, 2 turn, 3 river, 4 showdown */
static int s_p_acted, s_a_acted;  /* has acted this street */
static int s_round_over;
static int s_p_dealer;            /* who has the dealer button this hand */
static const char *s_msg = "";
static const char *s_show_msg = "";

static lv_obj_t *s_lbl_hud;
static lv_obj_t *s_lbl_msg;
static lv_obj_t *s_lbl_player;
static lv_obj_t *s_lbl_ai;
static lv_obj_t *s_lbl_board;
static lv_obj_t *s_btn_fold, *s_btn_call, *s_btn_raise, *s_btn_next;
static lv_obj_t *s_lbl_call;

static uint32_t s_rng = 0xabcdef01;
static uint32_t rnd(void) { s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5; return s_rng; }

/* ---- deck ---- */
static void deck_reset(void)
{
    for (int i = 0; i < 52; i++) s_deck[i] = (card_t)i;
    for (int i = 51; i > 0; i--) {
        int j = (int)(rnd() % (i + 1));
        card_t t = s_deck[i]; s_deck[i] = s_deck[j]; s_deck[j] = t;
    }
    s_deck_pos = 0;
}
static card_t deck_pop(void) { return s_deck[s_deck_pos++]; }

/* ---- hand eval ---- */
/* Score: bits [24..27]=category, then 5 nibbles for tiebreakers.
 * Categories: 8=SF, 7=4K, 6=FH, 5=Fl, 4=St, 3=3K, 2=2P, 1=1P, 0=HC */
static uint32_t score5(const card_t c[5])
{
    int rc[13] = {0};
    int sc[4] = {0};
    int ranks[5];
    for (int i = 0; i < 5; i++) {
        ranks[i] = CARD_RANK(c[i]);
        rc[ranks[i]]++;
        sc[CARD_SUIT(c[i])]++;
    }
    /* sort ranks desc */
    for (int i = 0; i < 5; i++) for (int j = i + 1; j < 5; j++)
        if (ranks[j] > ranks[i]) { int t = ranks[i]; ranks[i] = ranks[j]; ranks[j] = t; }

    bool is_flush = (sc[0] == 5 || sc[1] == 5 || sc[2] == 5 || sc[3] == 5);

    /* straight: highest rank of straight, or 0 if none. Handle A-2-3-4-5 (wheel). */
    int straight_high = 0;
    int distinct[5], dn = 0;
    int prev = -1;
    for (int i = 0; i < 5; i++) {
        if (ranks[i] != prev) { distinct[dn++] = ranks[i]; prev = ranks[i]; }
    }
    if (dn == 5) {
        if (distinct[0] - distinct[4] == 4) straight_high = distinct[0];
        else if (distinct[0] == 12 && distinct[1] == 3 && distinct[2] == 2 && distinct[3] == 1 && distinct[4] == 0)
            straight_high = 3;        /* wheel: 5-high */
    }

    /* Group counts. */
    int quads = -1, trips = -1, pair1 = -1, pair2 = -1;
    for (int r = 12; r >= 0; r--) {
        if (rc[r] == 4) quads = r;
        else if (rc[r] == 3) { if (trips < 0) trips = r; else if (pair1 < 0) pair1 = trips; }
        else if (rc[r] == 2) { if (pair1 < 0) pair1 = r; else if (pair2 < 0) pair2 = r; }
    }
    (void)pair2;

    uint32_t score = 0;
    if (is_flush && straight_high) score = (8u << 24) | (uint32_t)straight_high;
    else if (quads >= 0) {
        int kick = 0; for (int r = 12; r >= 0; r--) if (r != quads && rc[r]) { kick = r; break; }
        score = (7u << 24) | ((uint32_t)quads << 4) | (uint32_t)kick;
    } else if (trips >= 0 && pair1 >= 0) {
        score = (6u << 24) | ((uint32_t)trips << 4) | (uint32_t)pair1;
    } else if (is_flush) {
        uint32_t k = 0; for (int i = 0; i < 5; i++) k = (k << 4) | (uint32_t)ranks[i];
        score = (5u << 24) | k;
    } else if (straight_high) {
        score = (4u << 24) | (uint32_t)straight_high;
    } else if (trips >= 0) {
        uint32_t k = 0; int cnt = 0;
        for (int r = 12; r >= 0 && cnt < 2; r--) if (rc[r] == 1) { k = (k << 4) | (uint32_t)r; cnt++; }
        score = (3u << 24) | ((uint32_t)trips << 8) | k;
    } else if (pair1 >= 0 && pair2 >= 0) {
        int hi = pair1 > pair2 ? pair1 : pair2;
        int lo = pair1 > pair2 ? pair2 : pair1;
        int kick = 0; for (int r = 12; r >= 0; r--) if (rc[r] == 1) { kick = r; break; }
        score = (2u << 24) | ((uint32_t)hi << 8) | ((uint32_t)lo << 4) | (uint32_t)kick;
    } else if (pair1 >= 0) {
        uint32_t k = 0; int cnt = 0;
        for (int r = 12; r >= 0 && cnt < 3; r--) if (rc[r] == 1) { k = (k << 4) | (uint32_t)r; cnt++; }
        score = (1u << 24) | ((uint32_t)pair1 << 12) | k;
    } else {
        uint32_t k = 0; for (int i = 0; i < 5; i++) k = (k << 4) | (uint32_t)ranks[i];
        score = k;
    }
    return score;
}

static uint32_t best7(const card_t hole[2], const card_t board[5], int board_n)
{
    if (board_n < 3) {
        /* approximate: use whatever we have padded — but we only call this at showdown */
        return 0;
    }
    card_t cards[7];
    int n = 0;
    cards[n++] = hole[0]; cards[n++] = hole[1];
    for (int i = 0; i < board_n; i++) cards[n++] = board[i];
    /* enumerate C(n,5) */
    uint32_t best = 0;
    if (n != 7) return 0;
    for (int a = 0; a < 7; a++) for (int b = a + 1; b < 7; b++) {
        card_t five[5]; int k = 0;
        for (int i = 0; i < 7; i++) if (i != a && i != b) five[k++] = cards[i];
        uint32_t s = score5(five);
        if (s > best) best = s;
    }
    return best;
}

/* ---- UI helpers ---- */
static void card_str(card_t c, char *out)
{
    out[0] = s_rank_ch[CARD_RANK(c)];
    out[1] = s_suit_ch[CARD_SUIT(c)][0];
    out[2] = 0;
}

static void itoa_i(int v, char *out)
{
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    char b[12]; int i = 0;
    bool neg = v < 0; uint32_t u = neg ? (uint32_t)(-v) : (uint32_t)v;
    while (u) { b[i++] = '0' + (u % 10); u /= 10; }
    int k = 0; if (neg) out[k++] = '-';
    while (i) out[k++] = b[--i];
    out[k] = 0;
}

static void refresh_hud(void)
{
    char buf[80]; int k = 0;
    const char *p = "Pot "; while (*p) buf[k++] = *p++;
    char nb[12]; itoa_i(s_pot, nb);
    for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    p = "  You "; while (*p) buf[k++] = *p++;
    itoa_i(s_p_chips, nb); for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    p = "  AI "; while (*p) buf[k++] = *p++;
    itoa_i(s_a_chips, nb); for (int i = 0; nb[i]; i++) buf[k++] = nb[i];
    buf[k] = 0;
    lv_label_set_text(s_lbl_hud, buf);

    /* player cards */
    k = 0;
    p = "You: "; while (*p) buf[k++] = *p++;
    card_str(s_hole_p[0], buf + k); k += 2; buf[k++] = ' ';
    card_str(s_hole_p[1], buf + k); k += 2;
    buf[k] = 0;
    lv_label_set_text(s_lbl_player, buf);

    /* AI cards */
    k = 0; p = "AI : "; while (*p) buf[k++] = *p++;
    if (s_street >= 4) {
        card_str(s_hole_a[0], buf + k); k += 2; buf[k++] = ' ';
        card_str(s_hole_a[1], buf + k); k += 2;
    } else {
        p = "?? ??"; while (*p) buf[k++] = *p++;
    }
    buf[k] = 0;
    lv_label_set_text(s_lbl_ai, buf);

    /* board */
    k = 0; p = "Board: "; while (*p) buf[k++] = *p++;
    for (int i = 0; i < s_board_n; i++) {
        card_str(s_board[i], buf + k); k += 2; buf[k++] = ' ';
    }
    if (s_board_n == 0) { p = "(none)"; while (*p) buf[k++] = *p++; }
    buf[k] = 0;
    lv_label_set_text(s_lbl_board, buf);

    /* call label */
    int need = (s_to_act == 0) ? (s_a_bet - s_p_bet) : (s_p_bet - s_a_bet);
    if (need <= 0) lv_label_set_text(s_lbl_call, "Check");
    else {
        char cb[32]; int j = 0; const char *pp = "Call "; while (*pp) cb[j++] = *pp++;
        itoa_i(need, nb); for (int i = 0; nb[i]; i++) cb[j++] = nb[i]; cb[j] = 0;
        lv_label_set_text(s_lbl_call, cb);
    }

    /* msg */
    char mb[96]; int j = 0;
    const char *pp = s_msg; while (*pp) mb[j++] = *pp++;
    if (s_show_msg[0]) { mb[j++] = ' '; mb[j++] = '|'; mb[j++] = ' ';
        pp = s_show_msg; while (*pp) mb[j++] = *pp++; }
    mb[j] = 0;
    lv_label_set_text(s_lbl_msg, mb);

    /* button states */
    bool show_next = s_round_over;
    lv_obj_t *acts[3] = { s_btn_fold, s_btn_call, s_btn_raise };
    for (int i = 0; i < 3; i++) {
        if (show_next) lv_obj_add_flag(acts[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(acts[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (show_next) lv_obj_clear_flag(s_btn_next, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_btn_next, LV_OBJ_FLAG_HIDDEN);
}

/* ---- round flow ---- */
static void deal_to_street(int target)
{
    while (s_board_n < 3 && target >= 1) { s_board[s_board_n++] = deck_pop(); }
    while (s_board_n < 4 && target >= 2) { s_board[s_board_n++] = deck_pop(); }
    while (s_board_n < 5 && target >= 3) { s_board[s_board_n++] = deck_pop(); }
}

static void advance_street(void);

static void award(int winner, const char *why)
{
    if (winner == 0) s_p_chips += s_pot;
    else if (winner == 1) s_a_chips += s_pot;
    else { s_p_chips += s_pot / 2; s_a_chips += s_pot / 2; }
    s_pot = 0;
    s_show_msg = why;
    s_round_over = 1;
}

static int evaluate_winner(void)
{
    uint32_t ps = best7(s_hole_p, s_board, s_board_n);
    uint32_t as = best7(s_hole_a, s_board, s_board_n);
    if (ps > as) return 0;
    if (as > ps) return 1;
    return 2;
}

/* ---- AI ---- */
/* very rough preflop strength score 0..30 (Chen-like) */
static int preflop_strength(const card_t h[2])
{
    int r1 = CARD_RANK(h[0]), r2 = CARD_RANK(h[1]);
    int s1 = CARD_SUIT(h[0]), s2 = CARD_SUIT(h[1]);
    int hi = r1 > r2 ? r1 : r2;
    int lo = r1 > r2 ? r2 : r1;
    int score = 0;
    /* base points by high card (A=10, K=8, Q=7, J=6, T=5) */
    if (hi == 12) score += 10;
    else if (hi == 11) score += 8;
    else if (hi == 10) score += 7;
    else if (hi == 9) score += 6;
    else if (hi == 8) score += 5;
    else score += (hi + 2) / 2;
    if (r1 == r2) score *= 2;
    if (s1 == s2) score += 2;
    int gap = hi - lo;
    if (gap == 1) score += 1;
    else if (gap == 2) score -= 1;
    else if (gap == 3) score -= 2;
    else if (gap >= 4 && hi != 12) score -= 4;
    return score;
}

static int post_strength(void)
{
    /* number of pairs/trips/flush/straight in 5-card score, scaled 0..30 */
    uint32_t s = best7(s_hole_a, s_board, s_board_n);
    int cat = (int)(s >> 24);
    static const int cat_score[9] = { 8, 14, 18, 22, 24, 24, 27, 29, 30 };
    return cat_score[cat & 7];
}

static void ai_act(void);

static void player_call_or_check(void)
{
    int need = s_a_bet - s_p_bet;
    if (need < 0) need = 0;
    if (need > s_p_chips) need = s_p_chips;
    s_p_chips -= need; s_pot += need; s_p_bet += need;
    s_p_acted = 1;
    s_msg = need == 0 ? "You check." : "You call.";
    s_to_act = 1;
    if (s_p_acted && s_a_acted && s_p_bet == s_a_bet) advance_street();
    else ai_act();
}

static void player_raise(void)
{
    int call_amt = s_a_bet - s_p_bet; if (call_amt < 0) call_amt = 0;
    int total = call_amt + 50;
    if (total > s_p_chips) total = s_p_chips;
    s_p_chips -= total; s_pot += total; s_p_bet += total;
    s_p_acted = 1; s_a_acted = 0;
    s_msg = "You raise 50.";
    s_to_act = 1;
    ai_act();
}

static void player_fold(void)
{
    s_msg = "You fold.";
    award(1, "AI wins pot.");
}

static void ai_act(void)
{
    if (s_round_over) return;
    int strength = (s_board_n == 0) ? preflop_strength(s_hole_a) : post_strength();
    int need = s_p_bet - s_a_bet;
    int rand_pct = (int)(rnd() % 100);

    /* simple thresholds */
    if (strength < 7 && need > 0 && rand_pct > 30) {
        /* fold */
        s_msg = "AI folds.";
        award(0, "You win pot.");
        return;
    }
    if (strength >= 18 && rand_pct > 50) {
        /* raise */
        int call_amt = need < 0 ? 0 : need;
        int total = call_amt + 50;
        if (total > s_a_chips) total = s_a_chips;
        s_a_chips -= total; s_pot += total; s_a_bet += total;
        s_a_acted = 1; s_p_acted = 0;
        s_msg = "AI raises 50.";
        s_to_act = 0;
        return;
    }
    /* call/check */
    int call_amt = need < 0 ? 0 : need;
    if (call_amt > s_a_chips) call_amt = s_a_chips;
    s_a_chips -= call_amt; s_pot += call_amt; s_a_bet += call_amt;
    s_a_acted = 1;
    s_msg = call_amt == 0 ? "AI checks." : "AI calls.";
    s_to_act = 0;
    if (s_p_acted && s_a_acted && s_p_bet == s_a_bet) advance_street();
}

static void advance_street(void)
{
    s_p_bet = 0; s_a_bet = 0;
    s_p_acted = 0; s_a_acted = 0;
    s_street++;
    if (s_street == 1) deal_to_street(1);
    else if (s_street == 2) deal_to_street(2);
    else if (s_street == 3) deal_to_street(3);
    else if (s_street >= 4) {
        int w = evaluate_winner();
        award(w, w == 0 ? "You win showdown." : w == 1 ? "AI wins showdown." : "Split pot.");
        return;
    }
    /* small-blind side acts first post-flop too in heads-up the dealer acts first */
    s_to_act = s_p_dealer == 0 ? 0 : 1;
    if (s_to_act == 1) ai_act();
}

static void new_hand(void)
{
    if (s_p_chips <= 0 || s_a_chips <= 0) {
        s_show_msg = s_p_chips <= 0 ? "You bust. Restarting." : "AI busts. Restarting.";
        s_p_chips = s_a_chips = START_STACK;
    }
    deck_reset();
    s_hole_p[0] = deck_pop(); s_hole_a[0] = deck_pop();
    s_hole_p[1] = deck_pop(); s_hole_a[1] = deck_pop();
    s_board_n = 0;
    s_pot = 0;
    s_p_bet = 0; s_a_bet = 0;
    s_street = 0;
    s_p_acted = 0; s_a_acted = 0;
    s_round_over = 0;
    s_msg = "New hand.";
    /* alternate dealer */
    s_p_dealer = 1 - s_p_dealer;
    /* blinds: dealer posts small (heads-up rule) */
    int sb_p = s_p_dealer == 0 ? 0 : 1;
    if (sb_p == 0) {
        int sb = SMALL_BLIND; if (sb > s_p_chips) sb = s_p_chips;
        s_p_chips -= sb; s_pot += sb; s_p_bet += sb;
        int bb = BIG_BLIND; if (bb > s_a_chips) bb = s_a_chips;
        s_a_chips -= bb; s_pot += bb; s_a_bet += bb;
        s_to_act = 0;        /* SB acts first preflop */
    } else {
        int sb = SMALL_BLIND; if (sb > s_a_chips) sb = s_a_chips;
        s_a_chips -= sb; s_pot += sb; s_a_bet += sb;
        int bb = BIG_BLIND; if (bb > s_p_chips) bb = s_p_chips;
        s_p_chips -= bb; s_pot += bb; s_p_bet += bb;
        s_to_act = 1;
    }
    refresh_hud();
    if (s_to_act == 1) { ai_act(); refresh_hud(); }
}

/* ---- events ---- */
static void on_fold(lv_event_t *e) { (void)e; if (!s_round_over) { player_fold(); refresh_hud(); } }
static void on_call(lv_event_t *e) { (void)e; if (!s_round_over) { player_call_or_check(); refresh_hud(); } }
static void on_raise(lv_event_t *e){ (void)e; if (!s_round_over) { player_raise(); refresh_hud(); } }
static void on_next(lv_event_t *e) { (void)e; new_hand(); }

HB_APP_ENTRY(payload_entry)
{
    hb_trace_init();

    s_rng ^= hb_time_uptime_ms() | 1;
    s_p_chips = s_a_chips = START_STACK;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b3d27), 0);
    lv_obj_set_style_pad_all(scr, 6, 0);

    /* Poker-felt background; no title (removed). */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x14532d), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Pot / chips HUD, top-left. */
    s_lbl_hud = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_hud, lv_color_hex(0xfff8e7), 0);
    lv_obj_set_style_text_font(s_lbl_hud, &lv_font_montserrat_16, 0);
    lv_obj_align(s_lbl_hud, LV_ALIGN_TOP_LEFT, 0, 2);

    /* AI hand near the top, board in the middle, your hand below — clear bands. */
    s_lbl_ai = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_ai, lv_color_hex(0xe8e8e8), 0);
    lv_obj_set_style_text_font(s_lbl_ai, &lv_font_montserrat_16, 0);
    lv_obj_align(s_lbl_ai, LV_ALIGN_TOP_MID, 0, 40);

    s_lbl_board = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_board, lv_color_hex(0xfff59d), 0);
    lv_obj_set_style_text_font(s_lbl_board, &lv_font_montserrat_24, 0);
    lv_obj_align(s_lbl_board, LV_ALIGN_TOP_MID, 0, 110);

    s_lbl_player = lv_label_create(scr);
    lv_obj_set_style_text_color(s_lbl_player, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_lbl_player, &lv_font_montserrat_16, 0);
    lv_obj_align(s_lbl_player, LV_ALIGN_TOP_MID, 0, 180);

    s_lbl_msg = lv_label_create(scr);
    lv_obj_set_width(s_lbl_msg, 224);
    lv_label_set_long_mode(s_lbl_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_lbl_msg, LV_TEXT_ALIGN_CENTER, 0);
    /* Was hb_color_bg() — identical to the background, so the message was invisible. */
    lv_obj_set_style_text_color(s_lbl_msg, lv_color_hex(0xffd166), 0);
    lv_obj_align(s_lbl_msg, LV_ALIGN_TOP_MID, 0, 232);

    /* Action buttons */
    s_btn_fold = lv_button_create(scr);
    lv_obj_set_size(s_btn_fold, 64, 48);
    lv_obj_align(s_btn_fold, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *fl = lv_label_create(s_btn_fold); lv_label_set_text(fl, "Fold");
    lv_obj_center(fl);
    lv_obj_add_event_cb(s_btn_fold, on_fold, LV_EVENT_CLICKED, NULL);

    s_btn_call = lv_button_create(scr);
    lv_obj_set_size(s_btn_call, 88, 48);
    lv_obj_align(s_btn_call, LV_ALIGN_BOTTOM_MID, 0, 0);
    s_lbl_call = lv_label_create(s_btn_call);
    lv_label_set_text(s_lbl_call, "Call");
    lv_obj_center(s_lbl_call);
    lv_obj_add_event_cb(s_btn_call, on_call, LV_EVENT_CLICKED, NULL);

    s_btn_raise = lv_button_create(scr);
    lv_obj_set_size(s_btn_raise, 64, 48);
    lv_obj_align(s_btn_raise, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_t *rl = lv_label_create(s_btn_raise); lv_label_set_text(rl, "+50");
    lv_obj_center(rl);
    lv_obj_add_event_cb(s_btn_raise, on_raise, LV_EVENT_CLICKED, NULL);

    s_btn_next = lv_button_create(scr);
    lv_obj_set_size(s_btn_next, 160, 48);
    lv_obj_align(s_btn_next, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t *nl = lv_label_create(s_btn_next); lv_label_set_text(nl, "Next hand");
    lv_obj_center(nl);
    lv_obj_add_event_cb(s_btn_next, on_next, LV_EVENT_CLICKED, NULL);

    s_p_dealer = 1;     /* will flip to 0 on first new_hand */
    new_hand();
    refresh_hud();

}
