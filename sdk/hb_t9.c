/*
 * hb_t9.c — multi-mode T9 keyboard for LVGL apps.
 *
 * Modes (cycle with the "Mode" key in col 3 row 2):
 *   1. Predictive ("Pred") — default; each digit press appends to a
 *      digit sequence; matching dictionary words appear in the
 *      suggestion strip; tap a candidate (or press 0/space) to
 *      commit. Best for common English words.
 *   2. Multi-tap ("abc")   — each digit-key cycles through its
 *      letters in-place (Nokia-style). Repeat the same key within
 *      ~800 ms to advance to the next letter; idle past 800 ms
 *      commits and a fresh tap starts a new letter. Lets the user
 *      type any word — proper nouns, slang, anything outside the
 *      dictionary.
 *   3. Numbers ("123")    — each digit-key inserts that digit
 *      character literally. Punctuation key 1 still cycles common
 *      punctuation. * inserts '*', # inserts '#'.
 *
 * The Mode label updates to show the *current* mode (Pred/abc/123).
 *
 * Punctuation key (1) cycles through . , ? ! ' : ; - in all modes
 * via fast-repeat detection (replaces the just-inserted char on the
 * next tap within 800 ms).
 *
 * Dictionary: ~300 hand-curated common, safe English words. Profanity
 * and crude terms intentionally excluded.
 */

#include "hb_t9.h"
#include "hb_sdk.h"

/* ---- digit map ---- */
static int t9_digit_for(char c)
{
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    switch (c) {
        case 'a': case 'b': case 'c':              return 2;
        case 'd': case 'e': case 'f':              return 3;
        case 'g': case 'h': case 'i':              return 4;
        case 'j': case 'k': case 'l':              return 5;
        case 'm': case 'n': case 'o':              return 6;
        case 'p': case 'q': case 'r': case 's':    return 7;
        case 't': case 'u': case 'v':              return 8;
        case 'w': case 'x': case 'y': case 'z':    return 9;
        default: return 0;
    }
}

/* ---- multi-tap letter table ----
   Each digit key cycles through these characters in order; the last
   slot is the digit itself, mirroring classic Nokia behavior. */
static const char *s_multitap[10] = {
    " 0",        /* 0: space + 0 */
    "",          /* 1: handled separately as punctuation cycle */
    "abc2",      /* 2 */
    "def3",      /* 3 */
    "ghi4",      /* 4 */
    "jkl5",      /* 5 */
    "mno6",      /* 6 */
    "pqrs7",     /* 7 */
    "tuv8",      /* 8 */
    "wxyz9",     /* 9 */
};

/* ---- dictionary ---- */
static const char *s_dict[] = {
    /* 1-letter */
    "i", "a",
    /* 2-letter */
    "be", "to", "of", "in", "it", "is", "on", "as", "at", "by",
    "we", "do", "or", "if", "up", "no", "so", "me", "my", "us",
    "an", "go", "he", "am",
    /* 3-letter */
    "the", "and", "for", "you", "not", "but", "had", "all", "can",
    "her", "was", "one", "our", "out", "day", "get", "has", "him",
    "how", "man", "new", "now", "old", "see", "two", "way", "who",
    "boy", "did", "its", "let", "put", "say", "she", "too", "use",
    "any", "may", "yes", "yet", "ago", "big", "end", "few",
    "his", "hot", "low", "off", "own", "set", "ten",
    "top", "win", "ask", "bad", "buy", "ear", "eat", "eye",
    "far", "fly", "gun", "job", "key", "kid", "lay", "leg", "lie",
    "lot", "map", "mom", "mrs", "oil", "pay", "pen", "red", "run",
    "sat", "sea", "sit", "son", "sun", "tax", "tea", "tie", "try",
    "war",
    /* 4-letter */
    "that", "with", "have", "this", "will", "your", "from", "they",
    "know", "want", "been", "good", "much", "some", "time", "very",
    "when", "come", "here", "just", "like", "long", "make", "many",
    "more", "only", "over", "such", "take", "than", "them", "well",
    "were", "what", "back", "call", "find", "give", "hand", "help",
    "high", "last", "left", "life", "live", "look", "made", "most",
    "move", "must", "name", "need", "next", "open", "play", "read",
    "real", "same", "seem", "show", "side", "tell", "turn", "used",
    "ways", "week", "went", "word", "work", "year",
    /* 5-letter */
    "about", "after", "again", "below", "could", "every", "first",
    "found", "great", "house", "large", "learn", "never", "other",
    "place", "right", "small", "sound", "still", "think", "three",
    "under", "water", "where", "while", "world", "would", "write",
    "years", "young", "above", "begin", "being", "early", "going",
    "music", "night", "often", "until",
    /* 6-letter */
    "always", "around", "before", "better", "during", "enough",
    "family", "follow", "friend", "having", "letter", "little",
    "mother", "people", "please", "really", "school", "should",
    "system", "though", "things", "turned",
    /* 7+ */
    "another", "between", "country", "example", "however",
    "morning", "nothing", "perhaps", "picture", "problem",
    "thought", "through", "without", "tomorrow", "yesterday",
    "important", "everyone", "everything", "remember", "different",
    /* everyday additions */
    "love", "like", "happy", "okay", "fine",
    "thanks", "today", "tonight", "soon", "later", "early", "late",
    "hello", "hi", "hey", "bye", "ok", "wow", "cool",
    "sorry", "please", "thank", "welcome", "maybe", "sure",
    "phone", "text", "send", "meet", "coming",
    "home", "store", "food", "lunch", "dinner",
    "evening", "weekend", "afternoon",
};
#define S_DICT_N (int)(sizeof s_dict / sizeof s_dict[0])

/* ---- widget state ---- */

#define MAX_SEQ        16
#define MAX_CANDIDATES 8
#define COLS           4
#define ROWS           4
#define PAD_W          240
#define KEY_W          (PAD_W / COLS)
#define KEY_H          48
#define STRIP_H        32

typedef enum {
    MODE_PRED = 0,
    MODE_ABC,
    MODE_123,
    MODE_COUNT
} mode_t;

static const char *s_mode_label[MODE_COUNT] = {
    "Pred", "abc", "123"
};

typedef struct {
    lv_obj_t *textarea;
    lv_obj_t *strip;
    lv_obj_t *strip_btns[MAX_CANDIDATES];
    lv_obj_t *strip_labels[MAX_CANDIDATES];
    lv_obj_t *mode_label;
    lv_obj_t *shift_btn;        /* the ▲ key — highlighted while shift is armed */

    mode_t   mode;

    /* Predictive: digit sequence under construction */
    char     seq[MAX_SEQ + 1];
    int      seq_len;

    bool     shift;

    /* Multi-tap state */
    int      mt_last_key;       /* 0..9 of last key in multi-tap cycle */
    int      mt_letter_idx;     /* index into s_multitap[mt_last_key] */
    uint32_t mt_last_ms;

    /* Punctuation cycle (key 1) */
    int      punct_idx;
    uint32_t punct_last_ms;
} t9_state_t;

#define MULTITAP_TIMEOUT_MS 800u

static const char s_punct[] = { '.', ',', '?', '!', '\'', ':', ';', '-' };
#define N_PUNCT (int)(sizeof s_punct)

/* ---- candidate matching (predictive) ---- */

static int word_matches(const char *word, const char *seq, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        if (!word[i]) return 0;
        int d = t9_digit_for(word[i]);
        if (d != (seq[i] - '0')) return 0;
    }
    return word[i] ? 2 : 1;
}

static int find_candidates(const char *seq, int len, const char **out)
{
    int n = 0;
    for (int i = 0; i < S_DICT_N && n < MAX_CANDIDATES; i++) {
        if (word_matches(s_dict[i], seq, len) == 1) out[n++] = s_dict[i];
    }
    for (int i = 0; i < S_DICT_N && n < MAX_CANDIDATES; i++) {
        if (word_matches(s_dict[i], seq, len) == 2) out[n++] = s_dict[i];
    }
    return n;
}

static void seq_to_letters(const char *seq, int len, char *out)
{
    /* First letter on each key — fallback when no dict match. */
    for (int i = 0; i < len; i++) {
        int d = seq[i] - '0';
        const char *letters = s_multitap[d];
        out[i] = letters[0] ? letters[0] : '?';
    }
    out[len] = 0;
}

/* ---- UI helpers ---- */

static void hide_all_candidates(t9_state_t *st)
{
    for (int i = 0; i < MAX_CANDIDATES; i++) {
        lv_obj_add_flag(st->strip_btns[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_strip(t9_state_t *st)
{
    hide_all_candidates(st);

    /* The strip is only used in predictive mode. Hide the whole CONTAINER (not just
       the buttons) in abc/123 modes — otherwise it sat over the top-left "1" key and
       swallowed its taps. */
    if (st->mode != MODE_PRED) {
        lv_obj_add_flag(st->strip, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(st->strip, LV_OBJ_FLAG_HIDDEN);
    if (st->seq_len == 0) return;

    const char *cands[MAX_CANDIDATES];
    int n = find_candidates(st->seq, st->seq_len, cands);
    if (n == 0) {
        static char fallback[MAX_SEQ + 1];
        seq_to_letters(st->seq, st->seq_len, fallback);
        lv_obj_clear_flag(st->strip_btns[0], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(st->strip_labels[0], fallback);
        return;
    }
    for (int i = 0; i < n; i++) {
        lv_obj_clear_flag(st->strip_btns[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(st->strip_labels[i], cands[i]);
    }
}

static void insert_char(t9_state_t *st, char c)
{
    lv_textarea_add_char(st->textarea, (uint32_t)(uint8_t)c);
}

/* Highlight the ▲ key while shift is armed so it's obvious capitals are coming. */
static void update_shift_visual(t9_state_t *st)
{
    if (st->shift_btn)
        lv_obj_set_style_bg_color(st->shift_btn,
            lv_color_hex(st->shift ? 0x2a9d8f : 0x4a5060), 0);
}

static void commit_word(t9_state_t *st, const char *w, bool add_space)
{
    bool first = true;
    for (int i = 0; w[i]; i++) {
        char c = w[i];
        if (first && st->shift) {
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        }
        insert_char(st, c);
        first = false;
    }
    st->shift = false;
    update_shift_visual(st);
    st->seq[0] = 0;
    st->seq_len = 0;
    if (add_space) insert_char(st, ' ');
    refresh_strip(st);
}

static void commit_top_candidate(t9_state_t *st, bool with_space)
{
    if (st->seq_len == 0) return;
    const char *cands[MAX_CANDIDATES];
    int n = find_candidates(st->seq, st->seq_len, cands);
    static char fallback[MAX_SEQ + 1];
    const char *w;
    if (n > 0) {
        w = cands[0];
    } else {
        seq_to_letters(st->seq, st->seq_len, fallback);
        w = fallback;
    }
    commit_word(st, w, with_space);
}

/* End of multitap cycle: clear the "next press cycles" state. */
static void multitap_commit(t9_state_t *st)
{
    st->mt_last_key = -1;
    st->mt_letter_idx = 0;
}

/* ---- event handlers ---- */

static void on_digit(lv_event_t *e)
{
    t9_state_t *st = (t9_state_t *)lv_event_get_user_data(e);
    lv_obj_t   *btn = (lv_obj_t *)lv_event_get_target(e);
    int digit = (int)(uintptr_t)lv_obj_get_user_data(btn);
    uint32_t now = hb_time_uptime_ms();

    if (st->mode == MODE_PRED) {
        /* In predictive mode if any pending multi-tap, commit nothing
           extra — we never enter multi-tap state in this mode. */
        if (st->seq_len >= MAX_SEQ) return;
        st->seq[st->seq_len++] = (char)('0' + digit);
        st->seq[st->seq_len] = 0;
        refresh_strip(st);
        return;
    }

    if (st->mode == MODE_123) {
        /* Literal digit. */
        multitap_commit(st);
        insert_char(st, (char)('0' + digit));
        return;
    }

    /* MODE_ABC — classic multi-tap */
    const char *letters = s_multitap[digit];
    if (!letters[0]) return;        /* key 1 handled separately */
    int n_letters = 0; while (letters[n_letters]) n_letters++;

    bool same_quick = (st->mt_last_key == digit) &&
                      ((uint32_t)(now - st->mt_last_ms) < MULTITAP_TIMEOUT_MS);
    if (same_quick) {
        /* Cycle to next letter on this key — replace last char. */
        lv_textarea_delete_char(st->textarea);
        st->mt_letter_idx = (st->mt_letter_idx + 1) % n_letters;
    } else {
        st->mt_letter_idx = 0;
        st->mt_last_key = digit;
    }
    char c = letters[st->mt_letter_idx];
    if (st->shift && c >= 'a' && c <= 'z') {
        c = (char)(c - 32);
        st->shift = false;
        update_shift_visual(st);
    }
    insert_char(st, c);
    st->mt_last_ms = now;
}

static void on_candidate(lv_event_t *e)
{
    t9_state_t *st = (t9_state_t *)lv_event_get_user_data(e);
    lv_obj_t   *btn = (lv_obj_t *)lv_event_get_target(e);
    int idx = (int)(uintptr_t)lv_obj_get_user_data(btn);
    if (idx < 0 || idx >= MAX_CANDIDATES) return;
    const char *w = lv_label_get_text(st->strip_labels[idx]);
    commit_word(st, w, true);
}

static void on_space(lv_event_t *e)
{
    t9_state_t *st = (t9_state_t *)lv_event_get_user_data(e);
    if (st->mode == MODE_PRED && st->seq_len > 0) {
        commit_top_candidate(st, true);
    } else {
        multitap_commit(st);
        insert_char(st, ' ');
    }
}

static void on_backspace(lv_event_t *e)
{
    t9_state_t *st = (t9_state_t *)lv_event_get_user_data(e);
    if (st->mode == MODE_PRED && st->seq_len > 0) {
        st->seq[--st->seq_len] = 0;
        refresh_strip(st);
        return;
    }
    multitap_commit(st);
    lv_textarea_delete_char(st->textarea);
}

static void on_shift(lv_event_t *e)
{
    t9_state_t *st = (t9_state_t *)lv_event_get_user_data(e);
    st->shift = !st->shift;
    update_shift_visual(st);
}

static void on_mode_cycle(lv_event_t *e)
{
    t9_state_t *st = (t9_state_t *)lv_event_get_user_data(e);
    /* Commit any in-flight predictive sequence before switching mode
       so we don't leave a phantom sequence around. */
    if (st->mode == MODE_PRED && st->seq_len > 0) {
        commit_top_candidate(st, false);
    }
    multitap_commit(st);
    st->mode = (st->mode + 1) % MODE_COUNT;
    lv_label_set_text(st->mode_label, s_mode_label[st->mode]);
    refresh_strip(st);
}

static void on_hash(lv_event_t *e)
{
    t9_state_t *st = (t9_state_t *)lv_event_get_user_data(e);
    if (st->mode == MODE_PRED && st->seq_len > 0) {
        commit_top_candidate(st, false);
    }
    multitap_commit(st);
    insert_char(st, '#');
}

static void on_punct(lv_event_t *e)
{
    t9_state_t *st = (t9_state_t *)lv_event_get_user_data(e);
    if (st->mode == MODE_123) {
        /* In numeric mode, key 1 inserts a literal '1'. */
        multitap_commit(st);
        insert_char(st, '1');
        return;
    }
    uint32_t now = hb_time_uptime_ms();
    bool quick = ((uint32_t)(now - st->punct_last_ms) < MULTITAP_TIMEOUT_MS);
    if (!quick) {
        /* Commit any pending word in predictive mode before adding
           the new punctuation char. */
        if (st->mode == MODE_PRED && st->seq_len > 0) {
            commit_top_candidate(st, false);
        }
        multitap_commit(st);
        st->punct_idx = 0;
    } else {
        lv_textarea_delete_char(st->textarea);
        st->punct_idx = (st->punct_idx + 1) % N_PUNCT;
    }
    insert_char(st, s_punct[st->punct_idx]);
    st->punct_last_ms = now;
}

/* ---- key factory ---- */

static lv_obj_t *make_key(lv_obj_t *parent, t9_state_t *st,
                          int col, int row, const char *label,
                          lv_event_cb_t cb, void *udata, uint32_t bg)
{
    int x = col * KEY_W;
    int y = row * KEY_H + STRIP_H + 2;
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, KEY_W - 4, KEY_H - 4);
    lv_obj_set_pos(btn, x + 2, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_user_data(btn, udata);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, st);

    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    return btn;
}

lv_obj_t *hb_t9_create(lv_obj_t *parent, lv_obj_t *textarea)
{
    t9_state_t *st = lv_malloc(sizeof *st);
    if (!st) return NULL;
    st->textarea = textarea;
    st->seq[0] = 0;
    st->seq_len = 0;
    st->shift = false;
    st->mode = MODE_ABC;        /* default to multi-tap, not dictionary */
    st->mt_last_key = -1;
    st->mt_letter_idx = 0;
    st->mt_last_ms = 0;
    st->punct_idx = 0;
    st->punct_last_ms = 0;

    /* Suggestion strip — predictive mode only. */
    st->strip = lv_obj_create(parent);
    lv_obj_set_size(st->strip, PAD_W, STRIP_H);
    lv_obj_align(st->strip, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(st->strip, lv_color_hex(0x1a1f2e), 0);
    lv_obj_set_style_border_width(st->strip, 0, 0);
    lv_obj_set_style_pad_all(st->strip, 2, 0);
    lv_obj_set_flex_flow(st->strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_scroll_dir(st->strip, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(st->strip, LV_SCROLLBAR_MODE_OFF);  /* hide candidate scrollbar */
    lv_obj_add_flag(st->strip, LV_OBJ_FLAG_HIDDEN);   /* default mode is abc -> strip hidden */

    for (int i = 0; i < MAX_CANDIDATES; i++) {
        lv_obj_t *b = lv_button_create(st->strip);
        lv_obj_set_size(b, 56, STRIP_H - 4);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x14213d), 0);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_set_style_pad_hor(b, 4, 0);
        lv_obj_set_style_pad_ver(b, 0, 0);
        lv_obj_set_style_margin_right(b, 4, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_user_data(b, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(b, on_candidate, LV_EVENT_CLICKED, st);
        lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, "");
        lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_center(l);
        st->strip_btns[i]   = b;
        st->strip_labels[i] = l;
    }

    /* Keypad — 4 cols × 4 rows. Col 3 holds the utility keys. */
    make_key(parent, st, 0, 0, "1 .,?",  on_punct,    NULL,            0x4a5060);
    make_key(parent, st, 1, 0, "2 abc",  on_digit, (void *)(uintptr_t)2, 0x333540);
    make_key(parent, st, 2, 0, "3 def",  on_digit, (void *)(uintptr_t)3, 0x333540);
    make_key(parent, st, 3, 0, LV_SYMBOL_BACKSPACE, on_backspace, NULL, 0x4a5060);

    make_key(parent, st, 0, 1, "4 ghi",  on_digit, (void *)(uintptr_t)4, 0x333540);
    make_key(parent, st, 1, 1, "5 jkl",  on_digit, (void *)(uintptr_t)5, 0x333540);
    make_key(parent, st, 2, 1, "6 mno",  on_digit, (void *)(uintptr_t)6, 0x333540);
    st->shift_btn = make_key(parent, st, 3, 1, LV_SYMBOL_UP, on_shift, NULL, 0x4a5060);

    make_key(parent, st, 0, 2, "7 pqrs", on_digit, (void *)(uintptr_t)7, 0x333540);
    make_key(parent, st, 1, 2, "8 tuv",  on_digit, (void *)(uintptr_t)8, 0x333540);
    make_key(parent, st, 2, 2, "9 wxyz", on_digit, (void *)(uintptr_t)9, 0x333540);
    /* Mode key — its label is captured here so on_mode_cycle can
       repaint it. */
    {
        lv_obj_t *m = make_key(parent, st, 3, 2, "Pred",
                               on_mode_cycle, NULL, 0x14213d);
        st->mode_label = lv_obj_get_child(m, 0);
        lv_label_set_text(st->mode_label, s_mode_label[st->mode]);  /* reflect default */
    }

    make_key(parent, st, 0, 3, "*",      on_shift,  NULL, 0x4a5060);
    /* Wide space bar spanning cols 1–2 — synthesized by hand since
       make_key forces equal-width cells. */
    {
        lv_obj_t *spc = lv_button_create(parent);
        int x = 1 * KEY_W + 2;
        int y = 3 * KEY_H + STRIP_H + 2;
        lv_obj_set_size(spc, 2 * KEY_W - 4, KEY_H - 4);
        lv_obj_set_pos(spc, x, y);
        lv_obj_set_style_bg_color(spc, lv_color_hex(0x4a5060), 0);
        lv_obj_set_style_radius(spc, 8, 0);
        lv_obj_set_style_pad_all(spc, 0, 0);
        lv_obj_set_style_shadow_width(spc, 0, 0);
        lv_obj_add_event_cb(spc, on_space, LV_EVENT_CLICKED, st);
        lv_obj_t *spl = lv_label_create(spc);
        lv_label_set_text(spl, "0 space");
        lv_obj_set_style_text_color(spl, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_font(spl, &lv_font_montserrat_14, 0);
        lv_obj_center(spl);
    }
    /* Col 3 row 3: '#' — inserts a literal '#' (hashtag / programs). */
    make_key(parent, st, 3, 3, "#", on_hash, NULL, 0x4a5060);

    return parent;
}
