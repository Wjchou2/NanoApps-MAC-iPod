/*
 * hb_heap.c — OS main-heap interface. See hb_heap.h.
 *
 */
#include "hb_heap.h"

#define FN(a)            ((uintptr_t)((a) | 1u))
#define A_HEAP_FREESIZE   0x08417824u
#define A_HEAP_LARGEST    0x0800cbb8u
#define A_HEAP_ALLOC        0x0842b440u
#define A_OP_DELETE      0x0842d86cu
#define HEAP_MEM_OBJECT      2

typedef uint32_t (*u32_void_t)(void);
typedef void    *(*tmalloc_t)(uint32_t size, int type);
typedef void     (*free_t)(void *p);

uint32_t hb_os_heap_free(void)
{
    return ((u32_void_t)FN(A_HEAP_FREESIZE))();
}

uint32_t hb_os_heap_largest(void)
{
    return ((u32_void_t)FN(A_HEAP_LARGEST))();
}

void hb_os_free(void *p)
{
    if (p)
        ((free_t)FN(A_OP_DELETE))(p);
}

void *hb_os_alloc(uint32_t size)
{
    /* The allocator draws from the OS's real heap pool (SDRAM extends well past
     * 0x0A000000 — the framebuffer itself lives ~0x0bf00000), so the block is
     * valid RAM; just NULL-check. (An earlier DRAM_END=0x0A000000 clamp wrongly
     * rejected blocks above 32 MB — that 32 MB figure was incorrect.) */
    return ((tmalloc_t)FN(A_HEAP_ALLOC))(size, HEAP_MEM_OBJECT);
}
