/*
 * fs_test — one filesystem test harness (browse + read/write round-trip).
 *
 * Nothing here assumes a fixed layout: it starts at /iPod_Control and lists
 * whatever is actually there (hb_fs_dir_*), and you walk into real directories by
 * tapping them. The read/write check writes its probe file into the directory
 * you're currently in (a real, enumerated path), confirms it exists, reads it
 * back, and verifies the round trip — exercising hb_fs_write / hb_fs_exists /
 * hb_fs_read / hb_fs_size together.
 *
 *   tap a folder        -> open it (".." goes up)
 *   tap the RW bar      -> write a probe file here, read it back, verify
 *   either vol button   -> exit
 *
 * Deploy after `tools/eject.sh` so the filesystem is mounted.
 */

#include "hb_sdk.h"

#define BG      HB_BLACK
#define OK      HB_GREEN
#define ERR     HB_RED
#define DIR_FG  HB_GREEN
#define FILE_FG HB_WHITE
#define DIM     HB_RGB(0x80, 0x80, 0x80)

#define START_DIR   "/iPod_Control"
#define PROBE_NAME  "hb_fs_probe.txt"

#define PATH_MAX    256
#define NAME_BUF    96
#define MAX_ENTRIES 32

#define TITLE_Y      0
#define PATH_Y       22
#define CNT_Y        34
#define STATUS_Y     46
#define LIST_Y       60
#define ROW_H        13
#define RW_Y         (HB_SCREEN_H - 38)
#define RW_H         22
#define FOOT_Y       (HB_SCREEN_H - 14)
#define LIST_MAX_ROW ((RW_Y - 2 - LIST_Y) / ROW_H)

typedef struct {
    char name[NAME_BUF];
    bool is_dir;
} entry_t;

static entry_t   g_entries[MAX_ENTRIES];
static int       g_n;                          /* real entries found in g_cur */
static char      g_cur[PATH_MAX] = START_DIR;
static char      g_status[64] = "tap a folder, or RW to test";
static hb_color_t g_status_fg = HB_WHITE;
static uint32_t  g_probe_n;

/* ------- tiny string / path helpers (no libc) ------------------------------ */
static int s_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void s_set(char *dst, int max, const char *src)
{
    int i = 0;
    for (; i < max; i++) dst[i] = 0;
    for (i = 0; src[i] && i < max - 1; i++) dst[i] = src[i];
}

static void path_join(char *out, int max, const char *base, const char *name)
{
    int i = 0;
    for (const char *p = base; *p && i < max - 1; p++) out[i++] = *p;
    if (i > 0 && out[i - 1] != '/' && i < max - 1) out[i++] = '/';
    for (const char *p = name; *p && i < max - 1; p++) out[i++] = *p;
    out[i] = '\0';
}

static void path_up(char *p)
{
    int n = s_len(p);
    if (n <= 1) return;                        /* already root */
    if (p[n - 1] == '/') n--;                  /* drop trailing slash */
    while (n > 1 && p[n - 1] != '/') n--;      /* back to the parent's slash */
    if (n > 1 && p[n - 1] == '/') n--;         /* drop that slash too */
    if (n < 1) n = 1;
    p[n] = '\0';
}

/* subtract-based 6-digit itoa (no divmod) */
static void itoa6(char *dst, uint32_t v)
{
    static const uint32_t pow10[6] = { 100000, 10000, 1000, 100, 10, 1 };
    for (int i = 0; i < 6; i++) {
        char d = '0';
        while (v >= pow10[i]) { v -= pow10[i]; d++; }
        dst[i] = d;
    }
    dst[6] = '\0';
}

/* ------- model ------------------------------------------------------------- */
static void scan(void)
{
    g_n = 0;
    hb_dir_t d;
    if (!hb_fs_dir_open(&d, g_cur, false)) return;
    bool is_dir;
    while (g_n < MAX_ENTRIES &&
           hb_fs_dir_next(&d, g_entries[g_n].name, NAME_BUF, &is_dir)) {
        g_entries[g_n].is_dir = is_dir;
        g_n++;
    }
    hb_fs_dir_close(&d);
}

static bool has_up(void) { return s_len(g_cur) > 1; }

/* total rows shown = optional ".." + entries, clamped to what fits */
static int row_count(void)
{
    int n = g_n + (has_up() ? 1 : 0);
    return n > LIST_MAX_ROW ? LIST_MAX_ROW : n;
}

/* ------- read/write round trip into the current directory ------------------ */
static void rw_probe(void)
{
    char path[PATH_MAX];
    path_join(path, sizeof path, g_cur, PROBE_NAME);

    char payload[32];
    s_set(payload, sizeof payload, "homebrew_");
    int idx = s_len(payload);
    itoa6(&payload[idx], g_probe_n);
    idx += 6;
    payload[idx++] = '\n';

    if (!hb_fs_write(path, payload, idx)) {
        s_set(g_status, sizeof g_status, "RW write FAILED (read-only here?)");
        g_status_fg = ERR;
        return;
    }

    if (!hb_fs_exists(path)) {
        s_set(g_status, sizeof g_status, "RW exists FAILED (wrote, not found)");
        g_status_fg = ERR;
        scan();
        return;
    }

    char rbuf[64];
    for (uint32_t i = 0; i < sizeof rbuf; i++) rbuf[i] = 0;
    uint32_t got = hb_fs_read(path, rbuf, sizeof rbuf - 1);

    bool match = (got == (uint32_t)idx);
    for (uint32_t i = 0; i < got && match; i++)
        if (rbuf[i] != payload[i]) match = false;

    if (got == 0) {
        s_set(g_status, sizeof g_status, "RW read FAILED");
        g_status_fg = ERR;
    } else if (!match) {
        s_set(g_status, sizeof g_status, "RW MISMATCH");
        g_status_fg = ERR;
    } else {
        int si = 0;
        const char *tag = "RW OK ";
        for (int i = 0; tag[i] && si < (int)sizeof g_status - 1; i++) g_status[si++] = tag[i];
        for (uint32_t i = 0; i + 1 < got && si < (int)sizeof g_status - 1; i++) {
            char c = rbuf[i];
            g_status[si++] = (c >= 0x20 && c < 0x7F) ? c : '.';
        }
        g_status[si] = '\0';
        g_status_fg = OK;
        g_probe_n++;
    }
    scan();                                     /* the probe file now shows up */
}

/* ------- view -------------------------------------------------------------- */
static void redraw(void)
{
    hb_fill_screen(BG);
    hb_draw_str(4, TITLE_Y, "FS TEST", 2, HB_YELLOW, BG);
    hb_draw_str(4, PATH_Y, g_cur, 1, HB_CYAN, BG);

    hb_draw_str(4, CNT_Y, "entries:", 1, DIM, BG);
    hb_draw_uint(70, CNT_Y, (uint32_t)g_n, 3, HB_WHITE, BG);

    hb_draw_str(4, STATUS_Y, g_status, 1, g_status_fg, BG);

    int y = LIST_Y;
    int shown = 0;
    if (has_up()) {
        hb_draw_str(8, y, "..", 1, DIR_FG, BG);
        y += ROW_H; shown++;
    }
    for (int i = 0; i < g_n && shown < LIST_MAX_ROW; i++, shown++) {
        hb_color_t fg = g_entries[i].is_dir ? DIR_FG : FILE_FG;
        hb_draw_str(8, y, g_entries[i].name, 1, fg, BG);
        if (!g_entries[i].is_dir) {
            char fp[PATH_MAX];
            path_join(fp, sizeof fp, g_cur, g_entries[i].name);
            hb_draw_uint(HB_SCREEN_W - 56, y, hb_fs_size(fp), 6, DIM, BG);
        }
        y += ROW_H;
    }

    /* read/write test bar */
    hb_ui_button_draw(4, RW_Y, HB_SCREEN_W - 8, RW_H, "RW TEST (write + read here)",
                      HB_RGB(0x30, 0x10, 0x50), HB_WHITE);

    hb_draw_str(4, FOOT_Y, "TAP folder/..  RW bar  VOL: exit", 1, DIM, BG);
}

/* map a tap y to a displayed row, or -1 */
static int row_at(int16_t ty)
{
    if (ty < LIST_Y) return -1;
    int r = (ty - LIST_Y) / ROW_H;
    return (r >= 0 && r < row_count()) ? r : -1;
}

HB_APP_ENTRY(payload_entry)
{
    hb_ui_init();
    scan();
    redraw();

    for (uint32_t frame = 0; frame < 20000000; frame++) {
        int16_t tx, ty;
        hb_ui_event_t e = hb_ui_poll(&tx, &ty);
        if (e == HB_UI_EXIT) break;

        if (e == HB_UI_TAP) {
            if (hb_ui_button_hit(tx, ty, 4, RW_Y, HB_SCREEN_W - 8, RW_H)) {
                rw_probe();
                redraw();
            } else {
                int r = row_at(ty);
                if (r >= 0) {
                    int up = has_up() ? 1 : 0;
                    if (up && r == 0) {
                        path_up(g_cur);
                        scan(); redraw();
                    } else {
                        int ei = r - up;
                        if (ei >= 0 && ei < g_n && g_entries[ei].is_dir) {
                            char next[PATH_MAX];
                            path_join(next, sizeof next, g_cur, g_entries[ei].name);
                            s_set(g_cur, sizeof g_cur, next);
                            scan(); redraw();
                        }
                    }
                }
            }
        }

        hb_ui_pace();
    }

    hb_ui_done();
}
