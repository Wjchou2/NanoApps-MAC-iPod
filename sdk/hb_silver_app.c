/*
 * hb_silver_app.c — interface to Silver from homebrew.
 *
 * Every visible screen is a Silver controller-derived class (home or list 
 * controller, singleton, etc.). Screens are pushed/popped on a stack.
 *
 * Navigation primitive: call a controller's media-category action handler as a
 * member function (hb_silver_fire_handler). The handler queues a screen push
 * that the UI task drains. What worked on device: tapping a custom home item,
 * firing the Music action on the home controller, Music opens. A direct call
 * instead of the queued push sometimes no-ops; never pinned why, so we always
 * go through the handler.
 */

#include "hb_sdk.h"
#include "hb_silver_app.h"

#define FN_THUMB(addr) ((addr) | 1u)

/* ---- direct handler invocation ----
 * Driving Podcasts navigation from a SCSI-loader payload by calling 
 * the Podcasts action handler on the current top controller. The 
 * handler queues its own follow-up via a local-event dispatch, which 
 * the UI task drains on its next pump, so the screen change lands cleanly 
 * even though we called from a non-UI context. */

#define ADDR_HISTORYMGR_GETINST 0x0842bd30u

typedef void *(*get_inst_t)(void);
typedef int   (*silver_handler_t)(void *self, void *msg);

/* Top of the history stack — whatever the user is currently looking at.
   history-stack field lives at history manager+0x10. Returns NULL if the manager
   has no top (boot state, transition). */
void *hb_silver_history_top(void)
{
    void *mgr = ((get_inst_t)FN_THUMB(ADDR_HISTORYMGR_GETINST))();
    if (!mgr) return 0;
    return *(void **)((uint8_t *)mgr + 0x10);
}

int hb_silver_fire_handler(void *cntlr, uintptr_t handler_addr, void *msg)
{
    if (!cntlr || !handler_addr) return 0;
    return ((silver_handler_t)FN_THUMB(handler_addr))(cntlr, msg);
}

/* ---- factory: create a controller for a screen and push it ----
 *
 * Reproduces the OS navigator's "push a screen" path: resolve the screen name to a
 * resource id (0x083f510a), get the controller-factory singleton (0x0841bc98), ask
 * it to build a controller for (screen_id, layout_id) with the default transition
 * (0x0841bc30), then push that controller onto a parent controller (0x081c4996).
 * `push_cntlr` is the controller to push onto (e.g. the current top / home). Returns
 * the created controller (NULL if the screen has no registered controller) so the
 * caller can inspect its vtable/fields.
 */
#define ADDR_SILVERID_GETID    0x083f510au
#define ADDR_CNTLR_FACTORY     0x0841bc98u
#define ADDR_CREATE_CNTLR      0x0841bc30u
#define ADDR_PUSH_CNTLR        0x081c4996u

/* The springboard's "zoom up from the tapped icon" launch animation. When the OS
 * home controller opens an app it does NOT use the default per-screen transition
 * (standard=0); instead it creates the controller with the full-layout-transition-
 * map mode and points the controller's transition-map field at a named map, then
 * pushes (traced from the home controller's springboard-launch path @0x083a6df4):
 * build the controller for (id, layout) with transition mode 7, store the id of the
 * named map "springboard_launch_transition" into the controller's transition-map
 * field, then push. Field offset 0xC0 is where the controller stores that map id
 * (str r0,[cntlr,#192]); it lives in the controller base, so the offset is the same
 * for any screen's controller. The named map is a stock OS resource: its "in" effect
 * animates the incoming screen from the clicked item's centre (centre point, zero
 * w/h) up to full bounds while the home layout fades out — i.e. the zoom-up. */
#define TRANS_FULL_LAYOUT_MAP  7        /* transition mode the home launch uses (value 7)   */
#define CNTLR_TRANSMAP_OFF     0xC0u    /* controller field: transition-map id              */
#define SPRINGBOARD_LAUNCH_MAP "springboard_launch_transition"

typedef long  (*silverid_getid_t)(const char *name);
typedef void *(*factory_inst_t)(void);
typedef void *(*create_cntlr_t)(void *, long, long, int, void *, void *);
typedef void  (*push_cntlr_t)(void *, void *);

/* Push a screen by its numeric SCRN id (e.g. ids from a precompiled DB we
 * injected ourselves — those are sequential, not name-hashes, so name-to-id lookup
 * by name won't find them; the factory resolves SCRN by id against the chain).
 * Launches with the stock springboard zoom-up, exactly like an OS app. */
void *hb_silver_push_screen_id(void *push_cntlr, long screen_id, long layout_id)
{
    void *factory, *cntlr;
    hb_trace_log("SCRNIDX ", (uint32_t)screen_id, (uint32_t)layout_id);
    if (!screen_id)
        return 0;
    factory = ((factory_inst_t)FN_THUMB(ADDR_CNTLR_FACTORY))();
    cntlr = ((create_cntlr_t)FN_THUMB(ADDR_CREATE_CNTLR))(factory, screen_id, layout_id,
                                                          TRANS_FULL_LAYOUT_MAP,
                                                          (void *)0, (void *)0);
    hb_trace_log("SCNTLRX ", (uint32_t)(uintptr_t)cntlr, cntlr ? *(uint32_t *)cntlr : 0);
    if (cntlr) {
        /* aim the controller at the springboard zoom-up map (same as a stock launch) */
        long mapID = ((silverid_getid_t)FN_THUMB(ADDR_SILVERID_GETID))(SPRINGBOARD_LAUNCH_MAP);
        *(long *)((char *)cntlr + CNTLR_TRANSMAP_OFF) = mapID;
        hb_trace_log("SCNTRANS", (uint32_t)mapID, 0);
        if (push_cntlr)
            ((push_cntlr_t)FN_THUMB(ADDR_PUSH_CNTLR))(push_cntlr, cntlr);
    }
    return cntlr;
}

void *hb_silver_push_screen(void *push_cntlr, const char *screen_name,
                            const char *layout_name)
{
    long screen_id = ((silverid_getid_t)FN_THUMB(ADDR_SILVERID_GETID))(screen_name);
    long layout_id = layout_name
                  ? ((silverid_getid_t)FN_THUMB(ADDR_SILVERID_GETID))(layout_name) : 0;
    void *factory, *cntlr;

    hb_trace_log("SCRNID  ", (uint32_t)screen_id, (uint32_t)layout_id);
    if (!screen_id)
        return 0;

    factory = ((factory_inst_t)FN_THUMB(ADDR_CNTLR_FACTORY))();
    cntlr = ((create_cntlr_t)FN_THUMB(ADDR_CREATE_CNTLR))(factory, screen_id, layout_id,
                                                          0, (void *)0, (void *)0);
    /* dissect: controller ptr + its vtable (for the subclass route) */
    hb_trace_log("SCNTLR  ", (uint32_t)(uintptr_t)cntlr,
                 cntlr ? *(uint32_t *)cntlr : 0);
    if (cntlr && push_cntlr)
        ((push_cntlr_t)FN_THUMB(ADDR_PUSH_CNTLR))(push_cntlr, cntlr);
    return cntlr;
}

/* ---- build OUR OWN view and attach it to a live composite ----
 *
 * The base view draws nothing by itself, but with color-fill set it paints its
 * bounds with view-color field — the simplest self-contained "our pixels on a
 * native screen", and the foundation for hosting a custom-draw / GL surface in a
 * stock screen (same construct+attach path, only the view subclass differs).
 *
 * `parent` must be a live, presented composite view (e.g. a template view's
 * canvas, or a window view). It is not the controller and not the controller's
 * screen accessor — that returns the screen descriptor (a presentation
 * descriptor: controller-id/layout-map/orientation), not a view; passing it here
 * corrupts the descriptor and panics. The live view hierarchy is owned by the
 * presenter/window, reached via the presentation system, not the controller's
 * screen field, not directly.
 *
 * Recipe (all addresses iPod nano 7 OS 1.1.2, all on the UI task):
 *   view = heap alloc(size)                            @ 0x0842d444
 *          heap-allocate so the parent can delete it on teardown (a static
 *          buffer would corrupt the heap when the composite frees children).
 *   the programmatic view constructor @ 0x0847d14c. We pass: the view, a zero
 *   model, the parent, the message handler, the view id, a bounds rect, and flags.
 *          the ctor fully attaches the view under `parent`; a bare child-add
 *          instead leaves the parent field NULL and faults at draw, so use the
 *          ctor (not a manual attach).
 *   view->view-color field = rgba   (field at +0x210; painted by the OS fill
 *          drawing when color-fill is set)
 *
 * Geometry uses the view-rect encoding: 12 ints, a 3-int triple per edge
 * (top,left,bottom,right); we fill each triple for an absolute pixel coordinate.
 * rgba is the fill color as packed bytes r,g,b,a (little-endian uint32). Returns
 * the created view (NULL on failure).
 */
#define ADDR_OP_NEW        0x0842d444u  /* the OS heap alloc entry (size)    */
#define ADDR_TVIEW_CTOR    0x0847d14cu  /* view programmatic ctor           */
#define ADDR_VIEW_FINISHCREATE 0x0847b466u /* finalize the view after the ctor    */
#define ADDR_VIEW_PARENTSHOW   0x0847b274u /* attach the view under its parent    */
#define ADDR_VIEW_INVAL        0x08423b44u /* mark the view for redraw            */
#define ADDR_ZORDER_RAISE   0x080372b8u /* raise the view to the front of its z-order */
#define VIEW_ZORDER_OFF       0x238u      /* the view's z-order field byte offset      */
#define VIEW_OBJ_SIZE      0x400u       /* >= view size (608); over-alloc ok */
#define VIEW_VIEWCOLOR_OFF 0x210u       /* the view's color field byte offset      */
#define VIEW_FLAG_VISIBLE  (1u << 11)   /* visible                           */
#define VIEW_FLAG_FILL     (1u << 15)   /* color-fill                         */
#define VIEW_COORD_ABS     0x80000001u  /* absolute (sign-bit | 1)           */

typedef void *(*op_new_t)(unsigned int size);
/* programmatic view ctor. We pass the parent, the msg-handler, the bounds array
   and a flags word; the three integer args in between are undifferentiated stack
   words at the call — we fill them from one working call and don't tell them apart. */
typedef void *(*tview_ctor_t)(void *self, void *model, void *parent, void *msgh,
                              long a5, long a6, unsigned long a7,
                              const void *bounds, unsigned long flags);
typedef void  (*view_void_t)(void *view);
typedef void  (*view_show_t)(void *view, int show);
typedef void *(*view_void_ret_t)(void *view);

void *hb_silver_make_color_view(void *parent_composite, void *msg_handler,
                                int x, int y, int w, int h, uint32_t rgba)
{
    void *view;
    int32_t bounds[12];

    if (!parent_composite)
        return 0;

    /* per-edge absolute pixel coordinates */
    bounds[0] = 0; bounds[1]  = (int32_t)VIEW_COORD_ABS; bounds[2]  = y;        /* top    */
    bounds[3] = 0; bounds[4]  = (int32_t)VIEW_COORD_ABS; bounds[5]  = x;        /* left   */
    bounds[6] = 0; bounds[7]  = (int32_t)VIEW_COORD_ABS; bounds[8]  = y + h;    /* bottom */
    bounds[9] = 0; bounds[10] = (int32_t)VIEW_COORD_ABS; bounds[11] = x + w;    /* right  */

    view = ((op_new_t)FN_THUMB(ADDR_OP_NEW))(VIEW_OBJ_SIZE);
    hb_trace_log("VNEW    ", (uint32_t)(uintptr_t)view, VIEW_OBJ_SIZE);
    if (!view)
        return 0;

    ((tview_ctor_t)FN_THUMB(ADDR_TVIEW_CTOR))(view, (void *)0, parent_composite,
                                              msg_handler, -1, 0, 0x0dad0001u,
                                              bounds, VIEW_FLAG_VISIBLE | VIEW_FLAG_FILL);
    hb_trace_log("VCTOR   ", (uint32_t)(uintptr_t)view, *(uint32_t *)view);

    *(uint32_t *)((char *)view + VIEW_VIEWCOLOR_OFF) = rgba;
    return view;
}

/* ---- GL view: a real OS GL view (renders into its own view-sized GPU pixel
 * buffer via a GL pixmap context, composited in the 2D view tree) ---- */
#define ADDR_GLVIEW_VTABLE    0x087e7cd8u  /* vtable for the GL view         */
#define ADDR_TRECT_CTOR       0x0842d558u  /* rect ctor                      */
#define ADDR_TDRAWDEVICE_CTOR 0x08424310u  /* GL pixmap draw-device ctor     */
#define ADDR_GLPARAMS_CTOR    0x0801b068u  /* GL-context-params ctor         */
#define GLVIEW_CTX_OFF        0x260u       /* context field (NULL = create)  */
#define GLVIEW_BOUNDS_OFF     0x264u       /* bounds (rect sub-object)       */
#define GLVIEW_DRAWDEV_OFF    0x278u       /* draw-device sub-object         */
#define GLVIEW_PARAMS_OFF     0x33cu       /* context-params sub-object      */
#define GLVIEW_CFG0_OFF       0x370u       /* config byte the ctor sets      */
#define GLVIEW_CFG1_OFF       0x371u       /* config byte the ctor sets      */
#define GLVIEW_CFG2_OFF       0x372u       /* config byte the ctor sets      */
#define GLVIEW_OBJ_SIZE       0x400u       /* >= GL view object size         */

typedef void (*ctor0_t)(void *self);
typedef void (*tglparams_ctor_t)(void *, int, int, unsigned int, unsigned int);

void *hb_silver_make_gl_view(void *parent_composite, void *msg_handler,
                             int x, int y, int w, int h)
{
    void *view;
    char *o;
    int32_t bounds[12];
    unsigned int i;

    if (!parent_composite)
        return 0;

    bounds[0] = 0; bounds[1]  = (int32_t)VIEW_COORD_ABS; bounds[2]  = y;
    bounds[3] = 0; bounds[4]  = (int32_t)VIEW_COORD_ABS; bounds[5]  = x;
    bounds[6] = 0; bounds[7]  = (int32_t)VIEW_COORD_ABS; bounds[8]  = y + h;
    bounds[9] = 0; bounds[10] = (int32_t)VIEW_COORD_ABS; bounds[11] = x + w;

    view = ((op_new_t)FN_THUMB(ADDR_OP_NEW))(GLVIEW_OBJ_SIZE);
    if (!view)
        return 0;
    o = (char *)view;
    for (i = 0; i < GLVIEW_OBJ_SIZE; i++) o[i] = 0;     /* zero GL view region first */

    /* view base (programmatic ctor, no resource). visible only — the GL view
       paints via draw callback (GL), not a color fill. */
    ((tview_ctor_t)FN_THUMB(ADDR_TVIEW_CTOR))(view, (void *)0, parent_composite,
                                              msg_handler, -1, 0, 0x0dad0002u,
                                              bounds, VIEW_FLAG_VISIBLE);

    /* point the view at the GL-view vtable */
    *(uint32_t *)view = ADDR_GLVIEW_VTABLE;

    /* construct the embedded sub-objects */
    ((ctor0_t)FN_THUMB(ADDR_TRECT_CTOR))(o + GLVIEW_BOUNDS_OFF);
    ((ctor0_t)FN_THUMB(ADDR_TDRAWDEVICE_CTOR))(o + GLVIEW_DRAWDEV_OFF);
    ((tglparams_ctor_t)FN_THUMB(ADDR_GLPARAMS_CTOR))(o + GLVIEW_PARAMS_OFF,
                                                      0, 0, (unsigned)w, (unsigned)h);

    *(uint32_t *)(o + GLVIEW_CTX_OFF) = 0;   /* NULL: context created on first draw */
    o[GLVIEW_CFG0_OFF] = 0;
    o[GLVIEW_CFG1_OFF] = 1;
    o[GLVIEW_CFG2_OFF] = 0;
    return view;
}

void hb_silver_realize_view(void *view, void *parent_composite)
{
    if (!view)
        return;

    /* Realize + show + repaint: the ctor leaves a visible-flagged view
     * latently-visible (the OS would normally finish this during screen build).
     * Finish-create orients it under the parent; parent-show(true) flips
     * latent->visible; inval forces a repaint now (neither call invalidates,
     * since in the normal flow the whole parent is already dirty). */
    ((view_void_t)FN_THUMB(ADDR_VIEW_FINISHCREATE))(view);
    hb_trace_log("VFC     ", (uint32_t)(uintptr_t)view, 0);
    ((view_show_t)FN_THUMB(ADDR_VIEW_PARENTSHOW))(view, 1);
    hb_trace_log("VPS     ", (uint32_t)(uintptr_t)view, 0);
    /* bring to the top of our siblings (default z is lower-middle 255, so the
     * springboard/wallpaper would draw over us), then invalidate to repaint */
    ((view_show_t)FN_THUMB(ADDR_ZORDER_RAISE))((char *)view + VIEW_ZORDER_OFF, 1);
    hb_trace_log("VZTOP   ", (uint32_t)(uintptr_t)view, 0);
    ((view_void_t)FN_THUMB(ADDR_VIEW_INVAL))(view);
    /* also invalidate the PARENT composite: a child Inval may not re-render the
     * parent's (possibly cached) layer, so force the whole subtree to repaint.
     * flags field is at +0x1b0 (verified: reads 0xce00 = visible|color-fill|...). */
    if (parent_composite)
        ((view_void_t)FN_THUMB(ADDR_VIEW_INVAL))(parent_composite);
    hb_trace_log("VINV    ", (uint32_t)(uintptr_t)view,
                 *(uint32_t *)((char *)view + 0x1b0));
}

void *hb_silver_add_color_view(void *parent_composite, void *msg_handler,
                               int x, int y, int w, int h, uint32_t rgba)
{
    void *view = hb_silver_make_color_view(parent_composite, msg_handler,
                                           x, y, w, h, rgba);
    if (!view)
        return 0;
    hb_silver_realize_view(view, parent_composite);
    return view;
}

/* ---- bridge to the LIVE view hierarchy (read-only probe) ----
 *
 * A controller reaches live views via the UI-app singleton, not the presenter: get
 * the current UI-framework app (0x0841b40c); its target-view accessor (0x081fba3c)
 * returns the currently focused/target live view (a real, presented view); the app
 * can also find any live view by its id (0x081fb926). From a live view, the parent
 * field (+0x198) walks up to its composite ancestors — the valid attach targets.
 * This probe just traces the target view + two parents (+ their vtables) so we can
 * identify a live composite to attach to; it mutates nothing. Returns the target
 * view (NULL if none). UI task only.
 */
#define ADDR_UIAPP_CURRENT     0x0841b40cu  /* get the current UI-framework app */
#define ADDR_UIAPP_TARGETVIEW  0x081fba3cu  /* the app's target-view accessor  */
#define VIEW_PARENT_OFF       0x198u       /* the view's parent field byte offset     */

typedef void *(*uiapp_current_t)(void);
typedef void *(*uiapp_targetview_t)(void *app);

#define ADDR_TEMPLATE_GETCANVAS 0x084ac002u /* a template view's canvas accessor */
void *hb_silver_template_canvas(void *template_view)
{
    if (!template_view)
        return 0;
    return ((view_void_ret_t)FN_THUMB(ADDR_TEMPLATE_GETCANVAS))(template_view);
}

/* The plain composite-view class vtable. All plain composites share it, so it's a
 * reliable "is this a clean attach target?" test as we walk up the live parent
 * chain. */
#define COMPOSITEVIEW_VTABLE   0x0879f9f0u

/* Build our color view and attach it to the nearest LIVE plain composite view
 * above the current target view — the real, presented composite the OS will
 * draw. Walks from the target view up the parent chain until a plain composite-view
 * vtable. This is the working "our pixels on the current native screen" path, and the
 * same attach the custom-draw/GL surface will reuse. `msg_handler` = a valid
 * message handler (e.g. the current controller). Returns the view (NULL on miss). */
void *hb_silver_add_color_view_live(void *msg_handler, int x, int y, int w,
                                    int h, uint32_t rgba)
{
    void *app = ((uiapp_current_t)FN_THUMB(ADDR_UIAPP_CURRENT))();
    void *v   = app ? ((uiapp_targetview_t)FN_THUMB(ADDR_UIAPP_TARGETVIEW))(app) : 0;
    int depth = 0;
    while (v && *(uint32_t *)v != COMPOSITEVIEW_VTABLE && depth < 8) {
        v = *(void **)((char *)v + VIEW_PARENT_OFF);
        depth++;
    }
    hb_trace_log("LIVECMP ", (uint32_t)(uintptr_t)v, (uint32_t)depth);
    if (!v || *(uint32_t *)v != COMPOSITEVIEW_VTABLE)
        return 0;
    return hb_silver_add_color_view(v, msg_handler, x, y, w, h, rgba);
}

void *hb_silver_probe_live_views(void)
{
    void *app = ((uiapp_current_t)FN_THUMB(ADDR_UIAPP_CURRENT))();
    void *tv  = app ? ((uiapp_targetview_t)FN_THUMB(ADDR_UIAPP_TARGETVIEW))(app) : 0;
    hb_trace_log("UIAPP   ", (uint32_t)(uintptr_t)app, (uint32_t)(uintptr_t)tv);
    if (tv) {
        void *p1 = *(void **)((char *)tv + VIEW_PARENT_OFF);
        hb_trace_log("TGTVIEW ", *(uint32_t *)tv, (uint32_t)(uintptr_t)p1);
        if (p1) {
            void *p2 = *(void **)((char *)p1 + VIEW_PARENT_OFF);
            hb_trace_log("TGTPAR1 ", *(uint32_t *)p1, (uint32_t)(uintptr_t)p2);
            if (p2)
                hb_trace_log("TGTPAR2 ", *(uint32_t *)p2,
                             (uint32_t)(uintptr_t)*(void **)((char *)p2 + VIEW_PARENT_OFF));
        }
    }
    return tv;
}
