/* hb_lvgl_limits.h — minimal <limits.h> shim for freestanding LVGL. */
#ifndef HB_LVGL_LIMITS_H
#define HB_LVGL_LIMITS_H

#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif

#ifndef SCHAR_MIN
#define SCHAR_MIN (-128)
#endif
#ifndef SCHAR_MAX
#define SCHAR_MAX 127
#endif
#ifndef UCHAR_MAX
#define UCHAR_MAX 0xFFu
#endif
#ifndef CHAR_MIN
#define CHAR_MIN SCHAR_MIN
#endif
#ifndef CHAR_MAX
#define CHAR_MAX SCHAR_MAX
#endif

#ifndef SHRT_MIN
#define SHRT_MIN (-32768)
#endif
#ifndef SHRT_MAX
#define SHRT_MAX 32767
#endif
#ifndef USHRT_MAX
#define USHRT_MAX 0xFFFFu
#endif

#ifndef INT_MIN
#define INT_MIN  (-INT_MAX - 1)
#endif
#ifndef INT_MAX
#define INT_MAX  0x7FFFFFFF
#endif
#ifndef UINT_MAX
#define UINT_MAX 0xFFFFFFFFu
#endif

#ifndef LONG_MIN
#define LONG_MIN  (-LONG_MAX - 1L)
#endif
#ifndef LONG_MAX
#define LONG_MAX  0x7FFFFFFFL
#endif
#ifndef ULONG_MAX
#define ULONG_MAX 0xFFFFFFFFUL
#endif

#ifndef LLONG_MIN
#define LLONG_MIN (-LLONG_MAX - 1LL)
#endif
#ifndef LLONG_MAX
#define LLONG_MAX 0x7FFFFFFFFFFFFFFFLL
#endif
#ifndef ULLONG_MAX
#define ULLONG_MAX 0xFFFFFFFFFFFFFFFFULL
#endif

#ifndef SIZE_MAX
#define SIZE_MAX UINT_MAX
#endif

#endif
