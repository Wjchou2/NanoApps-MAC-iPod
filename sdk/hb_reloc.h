/*
 * hb_reloc.h — load a relocatable homebrew app blob (.hbapp) into an OS-heap
 * arena and rebase it, so position-independent app/LVGL code can run from any
 * heap address (no fixed VA, no chainload).
 *
 * The blob is produced by tools/mkrelocapp.py from a non-PIC, base-0, -Wl,-q
 * ELF: a header, a flat image, and a table of ABS32 relocation offsets. This
 * loader allocates an arena, copies the image, zeroes bss, adds the arena base
 * to every ABS32 location, flushes the I-cache, and returns the entry point.
 *
 * The blob is self-contained (no external symbols) and reaches the OS/SDK only
 * through the hb_app_api_t table the caller passes to its entry (see
 * hb_app_api.h).
 */
#ifndef HB_RELOC_H
#define HB_RELOC_H

#include "hb_sdk.h"

#define HB_RELOC_MAGIC 0x314c5248u   /* 'HRL1' */

/*
 * Load + relocate a packed .hbapp at `packed` (already resident in memory).
 * `alloc(size)` returns `size` bytes of OS heap (the OS allocator). Returns the
 * blob's entry point (thumb, cast to hb_blob_main_t and call), or NULL on a bad
 * header / failed allocation.
 *
 * If `out_arena` is non-NULL it receives the underlying allocation pointer, so
 * the caller can free the app's arena (matched free) when done — e.g. before
 * loading the next app. Pass NULL to keep the arena for the session.
 */
void *hb_reloc_load(const void *packed, void *(*alloc)(uint32_t size),
                    void **out_arena);

#endif /* HB_RELOC_H */
