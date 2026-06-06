/*
 * hb_reloc.c — relocatable app-blob loader. See hb_reloc.h.
 */

#include "hb_reloc.h"

/* Cold path (once per app launch), so the trace stays in permanently — if the
 * loader ever misbehaves again, `start trace` shows the header + arena + base.
 * We deliberately DON'T guard/reject on those values — just log and let it run
 * (a bad-bounds guard mis-rejected a valid arena and broke launches). */
extern void hb_trace_log(const char *tag, uint32_t v1, uint32_t v2);

/* Required before jumping into freshly-relocated code (the loader rebases ABS32
 * relocs in place, so the I-cache may hold stale lines). Exposed publicly via
 * hb_sdk.h for runtime patchers; routes through the firmware cache-maintenance
 * SVC. */
void hb_icache_invalidate(uint32_t addr, uint32_t size)
{
    register uint32_t r12 __asm__("r12") = 7;     /* svc #70 with selector 7 in r12 */
    register uint32_t r0  __asm__("r0")  = addr;  /* arg1 */
    register uint32_t r1  __asm__("r1")  = size;  /* arg2 */
    __asm__ volatile("svc #70" : "+r"(r12), "+r"(r0), "+r"(r1) : : "memory");
}

typedef struct {
    uint32_t magic;        /* HB_RELOC_MAGIC                                  */
    uint32_t entry_off;    /* offset of the entry point within the image     */
    uint32_t image_size;   /* PROGBITS bytes to copy (text+rodata+data)      */
    uint32_t span;         /* full extent incl. bss (alloc this much, zero rest) */
    uint32_t reloc_count;  /* number of ABS32 reloc offsets following image  */
    uint32_t align;        /* required base alignment                        */
} hb_reloc_hdr_t;

void *hb_reloc_load(const void *packed, void *(*alloc)(uint32_t size),
                    void **out_arena)
{
    const hb_reloc_hdr_t *h = (const hb_reloc_hdr_t *)packed;
    const uint8_t *image, *relocs;
    uint8_t *arena, *base;
    uint32_t i;

    if (out_arena)
        *out_arena = 0;
    hb_trace_log("RLLOAD  ", h->entry_off, h->align);
    hb_trace_log("RLSPAN  ", h->span, h->image_size);
    if (!packed || !alloc || h->magic != HB_RELOC_MAGIC || h->align == 0)
        return 0;
    image  = (const uint8_t *)packed + sizeof(hb_reloc_hdr_t);
    relocs = image + h->image_size;

    arena = (uint8_t *)alloc(h->span + h->align);
    hb_trace_log("RLARENA ", (uint32_t)(uintptr_t)arena, h->span + h->align);
    if (!arena)
        return 0;
    /* hand the caller the original allocation so it can free the app's arena
     * (e.g. when switching apps); the entry/base may be aligned past it. */
    if (out_arena)
        *out_arena = arena;
    base = (uint8_t *)(((uintptr_t)arena + (h->align - 1u)) & ~((uintptr_t)h->align - 1u));
    hb_trace_log("RLBASE  ", (uint32_t)(uintptr_t)base, h->reloc_count);

    /* copy the PROGBITS image, zero the remainder (inter-section gaps + bss) */
    for (i = 0; i < h->image_size; i++)
        base[i] = image[i];
    for (i = h->image_size; i < h->span; i++)
        base[i] = 0;

    /* rebase: each ABS32 location holds a link-time (base-0) absolute value;
     * add the load base so it points into the arena. */
    for (i = 0; i < h->reloc_count; i++) {
        uint32_t off = ((const uint32_t *)relocs)[i];
        *(uint32_t *)(base + off) += (uint32_t)(uintptr_t)base;
    }

    hb_icache_invalidate((uint32_t)(uintptr_t)base, h->span);
    return (void *)((uintptr_t)(base + h->entry_off) | 1u);   /* thumb */
}
