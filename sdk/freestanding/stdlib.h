/*
 * Minimal stdlib.h shim — our toolchain does not have a stdlib.h on the
 * include path, but LVGL bundled libs (qrcode, barcode) include it for the
 * handful of integer helpers below. We provide just what those translation
 * units need so they compile in our build.
 */
#ifndef _HB_SHIM_STDLIB_H
#define _HB_SHIM_STDLIB_H

#include <stddef.h>

static inline int abs(int x) { return x < 0 ? -x : x; }
static inline long labs(long x) { return x < 0 ? -x : x; }

#endif
