/*
 * hb_app_bundle.c — parse a staged app PACK and register every app. See
 * hb_app_bundle.h.
 *
 * The registrar drives the existing SDK mechanisms: all icons are registered in
 * one batched bitmap cache-table extension (hb_silver_icon_register_bitmaps);
 * label blobs are published into the resource chain (heap-allocated DB objects,
 * since the chain walks nodes rather than a fixed address); home items are
 * added; and each app's launch info is recorded for the tap router.
 *
 * The screen factory reads them through the resident's resource getter patch, which
 * is bound to a fixed DB-object address, so the resident still owns screen publishing.
 * That patch point is the only one we found the factory will read; there may be a
 * cleaner hook we haven't located. The launch record carries screen_id/layout_id so 
 * the tap router can push it.
 */

#include "hb_app_bundle.h"
#include "hb_silver_icon.h"
#include "hb_silver_res.h"

static const hb_app_section_t *hb__find_section(const uint8_t *b,
                                                const hb_app_bundle_t *hdr,
                                                uint32_t kind)
{
    const hb_app_section_t *secs =
        (const hb_app_section_t *)(b + sizeof(hb_app_bundle_t));
    uint32_t i;
    for (i = 0; i < hdr->section_count; i++)
        if (secs[i].kind == kind && secs[i].len)
            return &secs[i];
    return 0;
}

int hb_app_register_pack(const void *pack_v, void *(*alloc)(uint32_t size),
                         void (*trace)(const char *, uint32_t, uint32_t),
                         hb_app_launch_t *out_apps, int max,
                         void *screen_obj, void *screen_node, int register_to_os)
{
    int screen_done = 0;
    const uint8_t *pack = (const uint8_t *)pack_v;
    const hb_app_pack_t *ph = (const hb_app_pack_t *)pack;
    const uint32_t *offs;
    uint32_t n, a;
    uint32_t bmh[64], lens[64];
    int icon_app[64], nicons = 0, count = 0;
    long first_icon, app_icon[64];

    if (!pack || ph->magic != HB_APP_PACK_MAGIC || !alloc || !out_apps || max <= 0)
        return 0;
    n = ph->count;
    if (n == 0u || n > 64u)
        return 0;
    offs = (const uint32_t *)(pack + sizeof(hb_app_pack_t));

    for (a = 0; a < n; a++)
        app_icon[a] = -1;

    /* phase 1 (OS mutation, once per boot): register every app's icon LAZILY —
     * disk-backed, served on demand via the cache's alt-resolver through a small
     * LRU. The bitmaps live on disk (/Apps/Icons/<CFBundleName>.bin), NOT in heap,
     * so heap stays bounded regardless of count (vs the old pin-all pack copy).
     * Each app a gets icon id first_icon + a (the resolver maps id-first -> the
     * a-th disk file). Skipped on a re-deploy. */
    if (register_to_os) {
        /* Grow the OS bitmap cache so our 56 ring entries don't make its frequent
         * evictions (the 625 KB pool is already near-full) walk a long ring each
         * time. Smaller than pin-all needed since our bitmaps aren't in the pool. */
        if (n > 8u)
            hb_silver_icon_grow_cache(1u * 1024u * 1024u, alloc);
        first_icon = hb_silver_icon_register_lazy((int)n, alloc);
        if (trace)
            trace("PKICON  ", (uint32_t)first_icon, (uint32_t)n);
        for (a = 0; a < n; a++)
            app_icon[a] = first_icon + (long)a;
        (void)bmh; (void)lens; (void)icon_app; (void)nicons;
    }

    /* Pre-create enough springboard pages for all our items BEFORE adding any,
     * so each add fills a slot on an already-present page (the per-item show's
     * own new-page path doesn't take from a batched action-task add). */
    if (register_to_os)
        hb_silver_home_ensure_capacity((int)n);

    /* phase 2: publish labels, add home items, record launch info */
    for (a = 0; a < n && count < max; a++) {
        const uint8_t *b = pack + offs[a];
        const hb_app_bundle_t *hdr = (const hb_app_bundle_t *)b;
        const hb_app_section_t *lab, *code;
        const char *sbid, *name;
        long icon_id;
        hb_app_launch_t *L;
        int s;

        if (hdr->magic != HB_APP_BUNDLE_MAGIC)
            continue;

        sbid = (const char *)(b + hdr->sbid_off);
        name = (const char *)(b + hdr->name_off);  /* CFBundleName: disk file name */
        icon_id = (app_icon[a] >= 0) ? app_icon[a] : 0;

        if (register_to_os) {
            /* publish this app's label DB (once per boot; apps may share a chain
             * tag, e.g. 'HBap') */
            lab = hb__find_section(b, hdr, HB_SEC_LABEL);
            if (lab && hdr->label_tag) {
                void *obj  = alloc(HB_SILVER_RESDB_OBJ_SIZE);
                void *node = alloc(HB_SILVER_RESDB_NODE_SIZE);
                if (obj && node) {
                    void *db = hb_silver_resdb_open(obj, b + lab->off, lab->len);
                    hb_silver_resdb_publish(db, hdr->label_tag, node);
                }
            }
            /* open the first app's screen DB at the caller's fixed storage so the
             * resident's resource getter patch serves it (one custom screen for now) */
            if (!screen_done && screen_obj) {
                const hb_app_section_t *scr = hb__find_section(b, hdr, HB_SEC_SCREEN);
                if (scr) {
                    void *db = hb_silver_resdb_open(screen_obj, b + scr->off, scr->len);
                    if (hdr->screen_tag && screen_node)
                        hb_silver_resdb_publish(db, hdr->screen_tag, screen_node);
                    screen_done = 1;
                    if (trace)
                        trace("PKSCRN  ", hdr->app_id, scr->len);
                }
            }
            hb_silver_home_add_item(sbid, (int)hdr->label_id, (int)icon_id);
            /* name the k-th disk icon (k == pack index a) by CFBundleName */
            hb_silver_icon_set_name((int)a, name);
        }

        L = &out_apps[count];
        L->app_id    = hdr->app_id;
        L->app_kind  = hdr->app_kind;
        L->_pad      = 0;
        L->screen_id = (long)hdr->screen_id;
        L->layout_id = (long)hdr->layout_id;
        L->os_handler = (uintptr_t)hdr->os_handler;
        L->icon_id   = icon_id;
        for (s = 0; s < 23 && sbid[s]; s++)
            L->sbid[s] = sbid[s];
        L->sbid[s] = 0;
        for (s = 0; s < 23 && name[s]; s++)
            L->name[s] = name[s];
        L->name[s] = 0;
        code = hb__find_section(b, hdr, HB_SEC_CODE);
        L->code_addr = code ? (uint32_t)(uintptr_t)(b + code->off) : 0;
        L->code_len  = code ? code->len : 0;
        count++;
        if (trace)
            trace("PKAPP   ", hdr->app_id, (uint32_t)icon_id);
    }

    /* Drop any page ensure_capacity over-provisioned (existing free slots were
     * unknown), then rebuild every page cell from the now-complete model. The
     * reload runs unconditionally: it is idempotent (no-op if the springboard
     * view isn't up), so a bare resident re-push — which skips the per-boot OS
     * mutation but inherits a model already holding all items — still realises
     * the pages on screen. */
    if (register_to_os)
        hb_silver_home_trim_pages();
    hb_silver_home_reload();

    return count;
}
