/*
 * hb_screenshot.c — system screenshots via the OS capturer.
 *
 * hb_screenshot_take() drives the OS screenshot handler (singleton capture
 * entry, 0x08240860): it grabs the live display (the primary display layer) through
 * the OS compositor and writes /screenshotNNNN.bmp at the filesystem root.
 * Because homebrew apps render through the OS compositor too (a real Silver
 * view), this single path captures both system UI and homebrew app screens.
 */

#include "hb_sdk.h"

#define HB_SCREENSHOT_HANDLER_ADDR  (0x08240860u | 1u)
#define HB_GLOBAL_CONTROLLER_ADDR   (0x083fb524u | 1u)

typedef void *(*global_controller_get_t)(void);
typedef void  (*screenshot_handler_t)(void *controller);

bool hb_screenshot_take(void)
{
    void *controller = ((global_controller_get_t)HB_GLOBAL_CONTROLLER_ADDR)();
    if (!controller) return false;
    ((screenshot_handler_t)HB_SCREENSHOT_HANDLER_ADDR)(controller);
    return true;
}
