/*
 * hb_media.c, drive the OS media player (state + transport).
 *
 */

#include "hb_sdk.h"

#define FN_THUMB(addr) ((addr) | 1u)

/* front media player, state query + track advance */
#define A_SMP_GET    0x083f3590u  /* singleton accessor */
#define A_SMP_STATE  0x082676ccu  /* state getter -> 0 stopped / 1 playing / 2 paused */
#define A_SMP_NEXT   0x08267524u  /* advance to next item (arg = step unit) */

/* playback worker, transport via its command queue */
#define A_TASK_GET    0x0841bfb4u /* singleton accessor */
#define A_TASK_PAUSE  0x0824de54u /* queue "pause" + wake worker */
#define A_TASK_RESUME 0x0824de7cu /* queue "resume" + wake worker */
#define A_TASK_STOP   0x083bf040u /* queue "stop" + wake worker */

/* lower-level player, previous item (no queue-side entry for this one) */
#define A_MP_GET     0x08414f40u  /* singleton accessor */
#define A_MP_PREV    0x081f8c9cu  /* go to previous item */

#define OPT_TRACK    0x03         /* arg we pass to next/prev */
#define FADE_DEFAULT (-1)         /* arg we pass to pause */

typedef void *(*get_t)(void);
typedef int   (*state_fn_t)(void *self);
typedef void  (*act_fn_t)(void *self);
typedef void  (*opt_fn_t)(void *self, int arg);

static inline void *sys_player(void)  { return ((get_t)FN_THUMB(A_SMP_GET))(); }
static inline void *media_task(void)  { return ((get_t)FN_THUMB(A_TASK_GET))(); }

static int s_cmd = -1;     /* 0=playing, 1=paused */
static int s_last_os = -2; /* last OS state seen */

static int os_state(void)
{
    void *p = sys_player();
    if (!p) return -1;
    switch (((state_fn_t)FN_THUMB(A_SMP_STATE))(p)) {
        case 1:  return 0;
        case 2:  return 1;
        case 0:  return 2;
        default: return -1;
    }
}

int hb_media_state(void)
{
    int os = os_state();
    if (os != s_last_os) { s_cmd = -1; s_last_os = os; }  /* OS moved -> trust it */
    if (os == 2 || os == -1) { s_cmd = -1; return os; }   /* stopped/none: no override */
    return s_cmd >= 0 ? s_cmd : os;
}

bool hb_media_has_session(void)
{
    int s = hb_media_state();
    return s == 0 || s == 1;     /* playing or paused */
}

void hb_media_set_paused(bool paused)
{
    void *t = media_task();
    if (!t) return;
    if (paused)
        ((opt_fn_t)FN_THUMB(A_TASK_PAUSE))(t, FADE_DEFAULT);
    else
        ((act_fn_t)FN_THUMB(A_TASK_RESUME))(t);
    s_cmd = paused ? 1 : 0;   /* remember intent; the state getter won't reflect it */
}

void hb_media_toggle(void)
{
    hb_media_set_paused(hb_media_state() == 0);   /* playing -> pause, else play */
}

void hb_media_next(void)
{
    void *p = sys_player();
    if (!p) return;
    ((opt_fn_t)FN_THUMB(A_SMP_NEXT))(p, OPT_TRACK);
    s_cmd = 0;   /* skipping plays the new track */
}

void hb_media_prev(void)
{
    void *p = ((get_t)FN_THUMB(A_MP_GET))();
    if (!p) return;
    ((opt_fn_t)FN_THUMB(A_MP_PREV))(p, OPT_TRACK);
    s_cmd = 0;   /* skipping plays the new track */
}
