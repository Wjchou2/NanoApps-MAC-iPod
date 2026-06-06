/*
 * hb_sysfx.c — device-color override. See hb_sysfx.h.
 *
 */
#include "hb_sysfx.h"

#define FN(a)               ((unsigned)((a) | 1u))

#define SYS_GET_INSTANCE    0x08423800u
#define SYS_SET_DEVCOLOR    0x0845af64u
#define SYS_DEVCOLOR_OFF    0x74u
#define SYS_DEVCOLOR_COUNT  12

typedef void *(*sys_getinst_fn)(void);
typedef int   (*sys_setcolor_fn)(void *self, int color);

static int s_factory = -1;            /* captured on first access, for reset */

static void *sys_instance(void)
{
    return ((sys_getinst_fn)FN(SYS_GET_INSTANCE))();
}

static volatile int *devcolor_field(void *inst)
{
    return (volatile int *)((char *)inst + SYS_DEVCOLOR_OFF);
}

int hb_devcolor_count(void) { return SYS_DEVCOLOR_COUNT; }

/* Approximate accent RGB for each of the 12 colors (order matches the OS set
   above). Used as the system tint so homebrew apps match the device color. */
static const unsigned int s_devcolor_rgb[SYS_DEVCOLOR_COUNT] = {
    0x6e7277, /* Space Gray */ 0x9aa5b1, /* Silver  */ 0x1e88e5, /* Blue        */
    0x43a047, /* Green      */ 0xf6be00, /* Yellow  */ 0xec407a, /* Pink        */
    0x8e24aa, /* Purple     */ 0xe53935, /* Prod Red*/ 0x546e7a, /* Gray (2015) */
    0xd4a017, /* Gold       */ 0xec407a, /* Pink(15)*/ 0x1e88e5, /* Blue (2015) */
};

unsigned int hb_devcolor_tint(void)
{
    int idx = hb_devcolor_get();
    if (idx < 0 || idx >= SYS_DEVCOLOR_COUNT) return 0xfca311u;   /* orange fallback */
    return s_devcolor_rgb[idx];
}

int hb_devcolor_get(void)
{
    void *inst = sys_instance();
    int v;
    if (!inst) return -1;
    v = *devcolor_field(inst);
    if (s_factory < 0 && v >= 0 && v < SYS_DEVCOLOR_COUNT) s_factory = v;
    return v;
}

void hb_devcolor_set(int index)
{
    void *inst = sys_instance();
    if (!inst) return;
    if (s_factory < 0) {
        int v = *devcolor_field(inst);
        if (v >= 0 && v < SYS_DEVCOLOR_COUNT) s_factory = v;
    }
    if (index < 0) index = 0;
    if (index >= SYS_DEVCOLOR_COUNT) index = SYS_DEVCOLOR_COUNT - 1;
    ((sys_setcolor_fn)FN(SYS_SET_DEVCOLOR))(inst, index);
}

void hb_devcolor_reset(void)
{
    if (s_factory >= 0) hb_devcolor_set(s_factory);
}
