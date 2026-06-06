/* hb_lvgl_inttypes.h — minimal <inttypes.h> shim for freestanding LVGL.
   LVGL only needs PRId32/PRIu32/etc for its sprintf wrappers. */
#ifndef HB_LVGL_INTTYPES_H
#define HB_LVGL_INTTYPES_H

#ifndef PRId8
#define PRId8  "d"
#endif
#ifndef PRIu8
#define PRIu8  "u"
#endif
#ifndef PRId16
#define PRId16 "d"
#endif
#ifndef PRIu16
#define PRIu16 "u"
#endif
#ifndef PRId32
#define PRId32 "ld"
#endif
#ifndef PRIu32
#define PRIu32 "lu"
#endif
#ifndef PRIx32
#define PRIx32 "lx"
#endif
#ifndef PRIX32
#define PRIX32 "lX"
#endif
#ifndef PRId64
#define PRId64 "lld"
#endif
#ifndef PRIu64
#define PRIu64 "llu"
#endif
#ifndef PRIx64
#define PRIx64 "llx"
#endif

#endif
