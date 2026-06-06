/*
 * calculator — touch calculator with decimal support.
 *
 * Big result display at the top, 4×5 keypad below. Typed input is kept
 * as a digit-string buffer so "1.20" stays "1.20" while the user is
 * still typing; on operator-press it's parsed into a double for the
 * actual math. Last op + last operand are remembered so tapping "="
 * again repeats the previous operation (5 + 3 = 8 → = → 11).
 */

#include "hb_sdk.h"
#include "hb_prefs.h"
#include "lvgl/lvgl.h"

static lv_obj_t *s_display;
static lv_obj_t *s_secondary;

/* Calculator state. */
static double s_acc        = 0.0;
static char   s_op         = 0;     /* 0 / + - * / */
static int    s_error      = 0;

/* Typing buffer — what the user is currently entering. Empty when not
   typing. Includes leading '-' for negation in typing mode. */
static char   s_typed[20];
static int    s_typed_len  = 0;

/* For equal-repeat: when the user taps =, we remember the operator
   and operand so a second = reapplies them. Cleared by any non-= key. */
static char   s_last_op    = 0;
static double s_last_oprnd = 0.0;

/* ---- small helpers ---- */

static int typed_has_dot(void) {
    for (int i = 0; i < s_typed_len; i++) if (s_typed[i] == '.') return 1;
    return 0;
}

static double typed_to_double(void) {
    if (s_typed_len == 0) return 0.0;
    int i = 0, neg = 0;
    if (s_typed[i] == '-') { neg = 1; i++; }
    double v = 0;
    while (i < s_typed_len && s_typed[i] != '.') {
        v = v * 10.0 + (double)(s_typed[i] - '0');
        i++;
    }
    if (i < s_typed_len && s_typed[i] == '.') {
        i++;
        double scale = 0.1;
        while (i < s_typed_len) {
            v += (double)(s_typed[i] - '0') * scale;
            scale *= 0.1;
            i++;
        }
    }
    return neg ? -v : v;
}

static void typed_clear(void) {
    s_typed_len = 0;
    s_typed[0] = 0;
}

/* Append a printable char if room. We cap at 16 chars to fit the
   display width; sign + 9 ints + dot + 5 fracs is comfortable. */
static void typed_append(char c) {
    if (s_typed_len < 16) {
        s_typed[s_typed_len++] = c;
        s_typed[s_typed_len] = 0;
    }
}

static void typed_backspace(void) {
    if (s_typed_len > 0) {
        s_typed[--s_typed_len] = 0;
    }
}

/* Format a double like a calculator does: up to 8 fractional digits,
   trim trailing zeros, drop the dot if integer-valued. Very large or
   very small values fall back to "%g"-style: print scaled mantissa.
   No printf in our freestanding build, so we do it ourselves. */
static void format_double(double v, char *out)
{
    int k = 0;
    if (v != v) { out[0]='N'; out[1]='a'; out[2]='N'; out[3]=0; return; }
    int neg = v < 0;
    if (neg) v = -v;

    /* Out-of-range: extremely simple fallback. */
    if (v >= 1e12) {
        out[k++] = neg ? '-' : ' ';
        out[k++] = 'B'; out[k++] = 'I'; out[k++] = 'G';
        out[k] = 0; return;
    }

    /* Scale to 1e8 fixed-point, with rounding. */
    int64_t scaled = (int64_t)(v * 1e8 + 0.5);
    int64_t ipart = scaled / 100000000;
    int64_t fpart = scaled - ipart * 100000000;

    if (neg && (ipart != 0 || fpart != 0)) out[k++] = '-';

    if (ipart == 0) {
        out[k++] = '0';
    } else {
        char ibuf[20]; int ii = 0;
        while (ipart > 0) {
            ibuf[ii++] = '0' + (int)(ipart % 10);
            ipart /= 10;
        }
        while (ii > 0) out[k++] = ibuf[--ii];
    }

    if (fpart != 0) {
        out[k++] = '.';
        /* 8 frac digits, leading zeros preserved, trailing trimmed. */
        char fbuf[10];
        for (int i = 7; i >= 0; i--) {
            fbuf[i] = '0' + (int)(fpart % 10);
            fpart /= 10;
        }
        int last = 7;
        while (last >= 0 && fbuf[last] == '0') last--;
        for (int i = 0; i <= last; i++) out[k++] = fbuf[i];
    }
    out[k] = 0;
}

static void refresh_display(void)
{
    char buf[32];
    if (s_error) {
        lv_label_set_text(s_display, "Error");
    } else if (s_typed_len > 0) {
        /* Show the typed buffer verbatim — preserves "1.20" etc. */
        lv_label_set_text(s_display, s_typed);
    } else {
        format_double(s_acc, buf);
        lv_label_set_text(s_display, buf);
    }

    /* Secondary line: "acc op" when an op is pending, blank otherwise. */
    char s[40] = {0};
    if (s_op && !s_error) {
        char tmp[32];
        format_double(s_acc, tmp);
        int k = 0;
        for (int i = 0; tmp[i] && k < 36; i++) s[k++] = tmp[i];
        s[k++] = ' '; s[k++] = s_op; s[k] = 0;
    }
    lv_label_set_text(s_secondary, s);
}

/* ---- arithmetic ---- */

static double apply_op(char op, double a, double b)
{
    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': if (b == 0.0) { s_error = 1; return 0.0; } return a / b;
    }
    return b;
}

/* Commit the currently-typed value as the operand of any pending op. */
static void commit_typed(void)
{
    double v = typed_to_double();
    if (s_op == 0) s_acc = v;
    else           s_acc = apply_op(s_op, s_acc, v);
    typed_clear();
}

/* ---- key actions ---- */

static void do_digit(int d)
{
    if (s_error) return;
    /* Any digit clears the last-op repeat memory: typing implies a
       new operand, not a re-run of the previous op. */
    s_last_op = 0;
    /* Avoid 00007 — drop leading zero unless followed by a dot. */
    if (s_typed_len == 1 && s_typed[0] == '0') s_typed_len = 0;
    if (s_typed_len == 2 && s_typed[0] == '-' && s_typed[1] == '0') {
        s_typed[1] = 0; s_typed_len = 1;
    }
    typed_append('0' + d);
}

static void do_dot(void)
{
    if (s_error) return;
    s_last_op = 0;
    if (s_typed_len == 0) typed_append('0');
    if (!typed_has_dot()) typed_append('.');
}

static void do_op(char op)
{
    if (s_error) return;
    s_last_op = 0;
    if (s_typed_len > 0) commit_typed();
    s_op = op;
}

static void do_equals(void)
{
    if (s_error) return;
    if (s_typed_len > 0) {
        /* Normal "=" : remember op + operand so a second tap repeats. */
        s_last_op    = s_op;
        s_last_oprnd = typed_to_double();
        commit_typed();
        s_op = 0;
    } else if (s_last_op) {
        /* No typing in progress → repeat the last operation with the
           remembered operand. */
        s_acc = apply_op(s_last_op, s_acc, s_last_oprnd);
    }
}

static void do_clear(void)
{
    s_acc = 0.0;
    s_op = 0;
    s_error = 0;
    s_last_op = 0;
    s_last_oprnd = 0.0;
    typed_clear();
}

static void do_backspace(void)
{
    if (s_error) { do_clear(); return; }
    if (s_typed_len > 0) {
        typed_backspace();
        /* Drop a stranded leading '-'. */
        if (s_typed_len == 1 && s_typed[0] == '-') typed_clear();
    } else {
        /* Not typing — clear the pending op so the user can recover
           from an accidental "+". Without this the < key did nothing
           outside typing mode, which felt broken. */
        if (s_op) s_op = 0;
        else      s_acc = 0.0;
    }
}

static void do_negate(void)
{
    if (s_error) return;
    if (s_typed_len > 0) {
        if (s_typed[0] == '-') {
            for (int i = 1; i <= s_typed_len; i++) s_typed[i-1] = s_typed[i];
            s_typed_len--;
        } else {
            if (s_typed_len < 16) {
                for (int i = s_typed_len; i > 0; i--) s_typed[i] = s_typed[i-1];
                s_typed[0] = '-';
                s_typed_len++;
                s_typed[s_typed_len] = 0;
            }
        }
    } else {
        s_acc = -s_acc;
    }
}

static void do_percent(void)
{
    if (s_error) return;
    if (s_typed_len > 0) {
        double v = typed_to_double() / 100.0;
        typed_clear();
        char buf[32];
        format_double(v, buf);
        for (int i = 0; buf[i]; i++) typed_append(buf[i]);
    } else {
        s_acc /= 100.0;
    }
}

/* Action codes — encoded into the user_data pointer of each button. */
enum {
    A_DIGIT_BASE = 0x100,  /* 0x100..0x109 = digit 0..9 */
    A_OP_ADD = 0x200,
    A_OP_SUB,
    A_OP_MUL,
    A_OP_DIV,
    A_EQUALS,
    A_CLEAR,
    A_BACKSP,
    A_NEGATE,
    A_PERCENT,
    A_DOT,
    /* Scientific (extended panel — visible in sci mode). */
    A_SCI_TOGGLE,
    A_SIN, A_COS, A_TAN, A_LN, A_LOG10, A_SQRT, A_SQR, A_INV,
    A_PI, A_E, A_POW,
};

/* ---- scientific helpers ---- */

#define MY_PI  3.14159265358979323846
#define MY_E   2.71828182845904523536

static double my_fabs(double x) { return x < 0 ? -x : x; }

static double my_sqrt(double x)
{
    if (x <= 0) return 0;
    double g = x > 1 ? x / 2 : 1;
    for (int i = 0; i < 20; i++) g = 0.5 * (g + x / g);
    return g;
}
static double my_sin(double x)
{
    /* reduce to [-pi, pi] */
    while (x > MY_PI)  x -= 2 * MY_PI;
    while (x < -MY_PI) x += 2 * MY_PI;
    double term = x, sum = x, x2 = x * x;
    for (int i = 1; i < 12; i++) {
        term *= -x2 / ((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}
static double my_cos(double x)
{
    while (x > MY_PI)  x -= 2 * MY_PI;
    while (x < -MY_PI) x += 2 * MY_PI;
    double term = 1, sum = 1, x2 = x * x;
    for (int i = 1; i < 12; i++) {
        term *= -x2 / ((2 * i - 1) * (2 * i));
        sum += term;
    }
    return sum;
}
static double my_tan(double x)
{
    double c = my_cos(x);
    if (my_fabs(c) < 1e-9) return 0;
    return my_sin(x) / c;
}
static double my_ln(double x)
{
    if (x <= 0) return 0;
    /* x = m * 2^k, ln(x) = k*ln2 + ln(m), m in [1, 2). */
    int k = 0;
    while (x >= 2) { x /= 2; k++; }
    while (x < 1)  { x *= 2; k--; }
    /* Newton ln on m. Use atanh series: ln(m) = 2 * atanh((m-1)/(m+1)). */
    double y = (x - 1) / (x + 1);
    double y2 = y * y;
    double term = y, sum = y;
    for (int i = 1; i < 15; i++) {
        term *= y2;
        sum += term / (2 * i + 1);
    }
    return 2 * sum + k * 0.69314718055994530942;
}
static double my_log10(double x) { return my_ln(x) / 2.30258509299404568402; }
static double my_exp(double x)
{
    /* x = k*ln2 + r, exp(x) = 2^k * exp(r). */
    double k = (int)(x / 0.69314718055994530942);
    double r = x - k * 0.69314718055994530942;
    double term = 1, sum = 1;
    for (int i = 1; i < 20; i++) { term *= r / i; sum += term; }
    double two_k = 1;
    int kk = (int)k;
    if (kk >= 0) for (int i = 0; i < kk; i++) two_k *= 2;
    else for (int i = 0; i < -kk; i++) two_k *= 0.5;
    return sum * two_k;
}
static double my_pow(double a, double b)
{
    if (a <= 0) return 0;
    return my_exp(b * my_ln(a));
}
double my_sq(double x)  { return x * x; }
double my_inv(double x) { return x ? 1.0 / x : 0; }

/* Accelerometer-driven landscape detection. The device held in
 * portrait reads ~y=+1000 mg; rotated 90° to landscape one side or
 * the other dominates x. We only flip mode when a stable hold of
 * >800 ms passes the threshold to avoid jittering on small tilts. */
static int      s_accel_last_mode = 0;        /* 0 portrait, 1 landscape */
static uint32_t s_accel_changed_ms = 0;
extern int      s_sci_mode;
extern void     calc_rebuild_keypad(void);

/* Scientific mode is currently broken, so it's gated OFF (the code is kept for
   when it's fixed). Both entry points — the Sci toggle button and the
   accelerometer landscape flip — check this. */
static int      s_sci_enabled = 0;

void calc_per_frame(void)
{
    int32_t xyz[3]; hb_accel_read_milli_g(xyz);
    int ax = xyz[0] < 0 ? -xyz[0] : xyz[0];
    int ay = xyz[1] < 0 ? -xyz[1] : xyz[1];
    int desired = (ax > 600 && ax > ay) ? 1 : 0;
    uint32_t now = hb_time_uptime_ms();
    if (desired != s_accel_last_mode) {
        if (s_accel_changed_ms == 0) s_accel_changed_ms = now;
        else if (now - s_accel_changed_ms > 800) {
            s_accel_last_mode = desired;
            s_accel_changed_ms = 0;
            if (s_sci_enabled && s_sci_mode != desired) {
                s_sci_mode = desired;
                calc_rebuild_keypad();
            }
        }
    } else {
        s_accel_changed_ms = 0;
    }
}

int  s_sci_mode = 0;
static int  s_pow_pending = 0;
static lv_obj_t *s_kbd_root = NULL;
static lv_obj_t *s_sci_toggle = NULL;

static void make_key(lv_obj_t *parent, int col, int row,
                     uint32_t bg, const char *label, int code);

void calc_rebuild_keypad(void)
{
    if (!s_kbd_root) return;
    lv_obj_clean(s_kbd_root);
    const uint32_t COL_DIGIT = 0x333540;
    const uint32_t COL_FN    = 0x4a5060;
    const uint32_t COL_OP    = 0xf77f00;
    const uint32_t COL_EQ    = 0xe63946;
    const uint32_t COL_SCI   = 0x6a4c93;
    if (!s_sci_mode) {
        make_key(s_kbd_root, 0, 0, COL_FN, "C",       A_CLEAR);
        make_key(s_kbd_root, 1, 0, COL_FN, "+/-",     A_NEGATE);
        make_key(s_kbd_root, 2, 0, COL_FN, "%",       A_PERCENT);
        make_key(s_kbd_root, 3, 0, COL_OP, "<",       A_BACKSP);
        make_key(s_kbd_root, 0, 1, COL_DIGIT, "7",  A_DIGIT_BASE + 7);
        make_key(s_kbd_root, 1, 1, COL_DIGIT, "8",  A_DIGIT_BASE + 8);
        make_key(s_kbd_root, 2, 1, COL_DIGIT, "9",  A_DIGIT_BASE + 9);
        make_key(s_kbd_root, 3, 1, COL_OP,    "/",  A_OP_DIV);
        make_key(s_kbd_root, 0, 2, COL_DIGIT, "4",  A_DIGIT_BASE + 4);
        make_key(s_kbd_root, 1, 2, COL_DIGIT, "5",  A_DIGIT_BASE + 5);
        make_key(s_kbd_root, 2, 2, COL_DIGIT, "6",  A_DIGIT_BASE + 6);
        make_key(s_kbd_root, 3, 2, COL_OP,    "x",  A_OP_MUL);
        make_key(s_kbd_root, 0, 3, COL_DIGIT, "1",  A_DIGIT_BASE + 1);
        make_key(s_kbd_root, 1, 3, COL_DIGIT, "2",  A_DIGIT_BASE + 2);
        make_key(s_kbd_root, 2, 3, COL_DIGIT, "3",  A_DIGIT_BASE + 3);
        make_key(s_kbd_root, 3, 3, COL_OP,    "-",  A_OP_SUB);
        make_key(s_kbd_root, 0, 4, COL_DIGIT, "0",  A_DIGIT_BASE + 0);
        make_key(s_kbd_root, 1, 4, COL_DIGIT, ".",  A_DOT);
        make_key(s_kbd_root, 2, 4, COL_OP,    "+",  A_OP_ADD);
        make_key(s_kbd_root, 3, 4, COL_EQ,    "=",  A_EQUALS);
    } else {
        make_key(s_kbd_root, 0, 0, COL_SCI, "sin",  A_SIN);
        make_key(s_kbd_root, 1, 0, COL_SCI, "cos",  A_COS);
        make_key(s_kbd_root, 2, 0, COL_SCI, "tan",  A_TAN);
        make_key(s_kbd_root, 3, 0, COL_OP,  "<",    A_BACKSP);
        make_key(s_kbd_root, 0, 1, COL_SCI, "ln",   A_LN);
        make_key(s_kbd_root, 1, 1, COL_SCI, "log",  A_LOG10);
        make_key(s_kbd_root, 2, 1, COL_SCI, "sqr",  A_SQR);
        make_key(s_kbd_root, 3, 1, COL_SCI, "sqrt", A_SQRT);
        make_key(s_kbd_root, 0, 2, COL_SCI, "1/x",  A_INV);
        make_key(s_kbd_root, 1, 2, COL_SCI, "x^y",  A_POW);
        make_key(s_kbd_root, 2, 2, COL_SCI, "pi",   A_PI);
        make_key(s_kbd_root, 3, 2, COL_SCI, "e",    A_E);
        make_key(s_kbd_root, 0, 3, COL_DIGIT, "7",  A_DIGIT_BASE + 7);
        make_key(s_kbd_root, 1, 3, COL_DIGIT, "8",  A_DIGIT_BASE + 8);
        make_key(s_kbd_root, 2, 3, COL_DIGIT, "9",  A_DIGIT_BASE + 9);
        make_key(s_kbd_root, 3, 3, COL_OP,    "/",  A_OP_DIV);
        make_key(s_kbd_root, 0, 4, COL_DIGIT, "0",  A_DIGIT_BASE + 0);
        make_key(s_kbd_root, 1, 4, COL_DIGIT, ".",  A_DOT);
        make_key(s_kbd_root, 2, 4, COL_OP,    "+",  A_OP_ADD);
        make_key(s_kbd_root, 3, 4, COL_EQ,    "=",  A_EQUALS);
    }
}

void calc_rebuild_keypad(void);

static void resolve_pending(double n)
{
    /* If user is mid-typing, commit number from buffer instead. */
    (void)n;
}

static void apply_unary(double (*fn)(double))
{
    double v = s_typed_len > 0 ? typed_to_double() : s_acc;
    double r = fn(v);
    s_acc = r;
    typed_clear();
    /* push result back into the typed buffer so the display reflects it */
    char buf[32]; format_double(r, buf);
    for (int i = 0; buf[i]; i++) typed_append(buf[i]);
}

static void apply_pow(void)
{
    double base = s_typed_len > 0 ? typed_to_double() : s_acc;
    s_acc = base;
    s_op = '^';
    typed_clear();
    s_pow_pending = 1;
}

static void on_btn(lv_event_t *e)
{
    int code = (int)(uintptr_t)lv_event_get_user_data(e);
    if (code >= A_DIGIT_BASE && code < A_DIGIT_BASE + 10) {
        do_digit(code - A_DIGIT_BASE);
    } else switch (code) {
        case A_OP_ADD:  do_op('+'); break;
        case A_OP_SUB:  do_op('-'); break;
        case A_OP_MUL:  do_op('*'); break;
        case A_OP_DIV:  do_op('/'); break;
        case A_EQUALS:  do_equals(); break;
        case A_CLEAR:   do_clear(); break;
        case A_BACKSP:  do_backspace(); break;
        case A_NEGATE:  do_negate(); break;
        case A_PERCENT: do_percent(); break;
        case A_DOT:     do_dot(); break;
        case A_SCI_TOGGLE: { extern void calc_rebuild_keypad(void); if (s_sci_enabled) { s_sci_mode = !s_sci_mode; calc_rebuild_keypad(); } break; }
        case A_SIN:   apply_unary(my_sin);   break;
        case A_COS:   apply_unary(my_cos);   break;
        case A_TAN:   apply_unary(my_tan);   break;
        case A_LN:    apply_unary(my_ln);    break;
        case A_LOG10: apply_unary(my_log10); break;
        case A_SQRT:  apply_unary(my_sqrt);  break;
        case A_SQR:   { extern double my_sq(double); apply_unary(my_sq); } break;
        case A_INV:   { extern double my_inv(double); apply_unary(my_inv); } break;
        case A_PI:    typed_clear(); { char b[32]; format_double(MY_PI, b); for (int i = 0; b[i]; i++) typed_append(b[i]); } break;
        case A_E:     typed_clear(); { char b[32]; format_double(MY_E,  b); for (int i = 0; b[i]; i++) typed_append(b[i]); } break;
        case A_POW:   apply_pow(); break;
    }
    refresh_display();
}

/* Circular buttons on a square hit area.
   The button object is 54×54 (the square hit zone the user can tap
   anywhere within), centered in a 60-wide × 64-tall grid cell so
   adjacent buttons have ~6 px of horizontal breathing room. Its bg
   radius is half its size so it paints as a perfect circle. Visual
   circle vs square touch zone gives a finger-friendly target
   without the visual clutter of square buttons. */
#define BTN_SIZE   54
#define COL_STRIDE 60
#define ROW_STRIDE 64
/* Push the whole grid down a touch so the leftover space below the last
   row (the keypad container is taller than the 5-row grid) is shared as
   padding instead of all pooling at the bottom. */
#define GRID_Y_PAD 14

static void make_key(lv_obj_t *parent, int col, int row,
                     uint32_t bg, const char *label, int code)
{
    int cell_x = col * COL_STRIDE;
    int cell_y = row * ROW_STRIDE + GRID_Y_PAD;
    int x = cell_x + (COL_STRIDE - BTN_SIZE) / 2;
    int y = cell_y + (ROW_STRIDE - BTN_SIZE) / 2;
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, BTN_SIZE, BTN_SIZE);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg), 0);
    lv_obj_set_style_radius(btn, BTN_SIZE / 2, 0);  /* full circle */
    lv_obj_set_style_pad_all(btn, 0, 0);
    /* Touch-down highlight: dim the whole button while pressed for tap feedback. */
    lv_obj_set_style_opa(btn, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, on_btn, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)code);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, label);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(hb_color_text()), 0);
    lv_obj_center(l);
}

HB_APP_ENTRY(payload_entry)
{
    hb_trace_init();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Display HUD — same black background as the keypad (no separate
       panel), and a more compact 88 px so the keypad gets the bulk
       of the screen. The display sits inline above the keys; the
       secondary "acc op" line tucks just above it. */
    lv_obj_t *panel = lv_obj_create(scr);
    lv_obj_set_size(panel, 240, 88);
    lv_obj_set_pos(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    s_secondary = lv_label_create(panel);
    lv_obj_set_style_text_color(s_secondary, lv_color_hex(hb_color_text_dim()), 0);
    lv_obj_set_style_text_font(s_secondary, &lv_font_montserrat_16, 0);
    lv_obj_set_width(s_secondary, 232);
    lv_obj_set_style_pad_all(s_secondary, 0, 0);
    lv_label_set_long_mode(s_secondary, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_secondary, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_secondary, "");
    lv_obj_align(s_secondary, LV_ALIGN_TOP_RIGHT, -8, 8);

    s_display = lv_label_create(panel);
    lv_obj_set_style_text_color(s_display, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(s_display, &lv_font_montserrat_36, 0);
    lv_obj_set_width(s_display, 232);
    lv_obj_set_style_pad_all(s_display, 0, 0);
    lv_label_set_long_mode(s_display, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_display, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_display, "0");
    lv_obj_align(s_display, LV_ALIGN_BOTTOM_RIGHT, -8, -6);

    /* Keypad container — fills the remaining 344 px below the HUD. */
    s_kbd_root = lv_obj_create(scr);
    lv_obj_set_size(s_kbd_root, 240, 432 - 88);
    lv_obj_align(s_kbd_root, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_kbd_root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(s_kbd_root, 0, 0);
    lv_obj_set_style_radius(s_kbd_root, 0, 0);
    lv_obj_set_style_pad_all(s_kbd_root, 0, 0);
    lv_obj_clear_flag(s_kbd_root, LV_OBJ_FLAG_SCROLLABLE);

    /* Sci toggle pill in the top-right corner of the HUD. Tap to swap
     * the keypad layout in/out of scientific mode. (Accelerometer-
     * driven landscape rotation is a stretch goal; the toggle gives
     * users the same effect without needing to physically rotate.) */
    s_sci_toggle = lv_button_create(scr);
    /* Hidden: scientific mode is broken, so don't expose the entry point (code kept). */
    lv_obj_add_flag(s_sci_toggle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_sci_toggle, 52, 26);
    lv_obj_align(s_sci_toggle, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_obj_set_style_bg_color(s_sci_toggle, lv_color_hex(hb_color_text_dim()), 0);
    lv_obj_set_style_radius(s_sci_toggle, 13, 0);
    lv_obj_add_event_cb(s_sci_toggle, on_btn, LV_EVENT_CLICKED, (void *)(uintptr_t)A_SCI_TOGGLE);
    lv_obj_t *sl = lv_label_create(s_sci_toggle);
    lv_label_set_text(sl, "Sci");
    lv_obj_set_style_text_color(sl, lv_color_hex(hb_color_text()), 0);
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_14, 0);
    lv_obj_center(sl);

    calc_rebuild_keypad();
    refresh_display();
    extern void calc_per_frame(void);
    hb_lv_set_frame_cb(calc_per_frame);
}
