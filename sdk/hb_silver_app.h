/*
 * hb_silver_app.h — interface to the stock UI controller stack: fire action
 * handlers, inspect/push controllers. Addresses are for iPod nano 7 OS 1.1.2.
 */
#ifndef HB_SILVER_APP_H
#define HB_SILVER_APP_H

#include "hb_sdk.h"

/*
 * Top of the history stack — the controller the user is currently looking at.
 * NULL during boot / transitions.
 */
void *hb_silver_history_top(void);

/* hb_wake_lock(bool) is declared in hb_sdk.h, implemented in hb_lv_surface.c. */

/*
 * THE navigation primitive: call an action handler at `handler_addr` as a
 * C++ member function on `cntlr` (e.g. the home controller's media-category
 * handler, which queues a screen-push the UI task then drains). `msg` may be
 * NULL. Returns the handler's result (1 = handled). This is the working path; do
 * not use the action-name / vtbl+0x194 route for navigation, that only queues a
 * malformed event.
 */
int hb_silver_fire_handler(void *cntlr, uintptr_t handler_addr, void *msg);

/*
 * Factory-create the controller registered for a named screen (+ optional
 * layout) and push it onto `push_cntlr` (e.g. the current top controller). This
 * is the OS's push-a-screen path; it builds a native, fully-wired screen
 * from its SCRN/VLyt resources. Returns the created controller (NULL if no
 * controller is registered for the screen) so the caller can inspect it.
 */
void *hb_silver_push_screen(void *push_cntlr, const char *screen_name,
                            const char *layout_name);

/*
 * Push a screen by numeric SCRN/layout id (for screens whose ids we know
 * directly — e.g. resources we compiled + injected ourselves, whose ids are
 * sequential rather than name-hashes). Returns the created controller.
 */
void *hb_silver_push_screen_id(void *push_cntlr, long screen_id, long layout_id);

/*
 * Build OUR OWN view and attach it to a LIVE composite so our pixels appear on a
 * native Silver screen. Constructs a base view flagged to fill its bounds with
 * `rgba` (bytes r,g,b,a; little-endian uint32) at absolute pixel rect (x,y,w,h),
 * heap-allocated so the parent owns/frees it. The construct+attach path the
 * custom-draw / GL surface will reuse (only the view subclass differs).
 *
 * `parent_composite` MUST be a live, presented composite view (a
 * Silver template view's canvas, or a Silver window view) — NOT a
 * controller and NOT Silver controller's screen accessor() (that returns the Silver screen
 * descriptor, not a view; passing it panics). `msg_handler` is the target
 * handler (e.g. the controller). Returns the created view (NULL on failure).
 * Call on the UI task.
 */
void *hb_silver_add_color_view(void *parent_composite, void *msg_handler,
                               int x, int y, int w, int h, uint32_t rgba);

/*
 * Construct + attach our color view to `parent_composite` but do not run the
 * realize/show/repaint calls — for use inside the OS screen build/present flow,
 * where the OS's own pass realizes + layers it.
 * Returns the created view (NULL on failure).
 */
void *hb_silver_make_color_view(void *parent_composite, void *msg_handler,
                                int x, int y, int w, int h, uint32_t rgba);

/*
 * Construct a real OS GL view (a view subclass that renders GL into its own
 * view-sized GPU pixel buffer via a GL pixmap context, composited in the 2D
 * view tree). Built on the programmatic view base ctor (no view resource).
 * the GL-view context field is NULL so the OS's GL-view draw creates the context on first draw
 * and wraps the subclass draw callback between its begin/end scene — install a custom
 * draw callback (issuing GL calls on the GL-view context field @ +0x260) then realize +
 * child-add like a color view. UI task only. Returns the view (NULL on failure).
 */
void *hb_silver_make_gl_view(void *parent_composite, void *msg_handler,
                             int x, int y, int w, int h);

/*
 * Run the realize/show/raise/repaint calls on an already-constructed view,
 * splitting hb_silver_add_color_view's realize half out so a caller can do
 * make_color_view -> install a custom draw callback -> realize, ensuring the
 * first repaint uses the custom draw (not the solid view-color field, nor a
 * black/unrealized frame). `parent_composite` may be NULL to skip the parent
 * invalidate. UI task only.
 */
void hb_silver_realize_view(void *view, void *parent_composite);

/*
 * Returns the content composite view (the canvas) you attach child views to,
 * from a live template view (e.g. the `this` of a layer-creation hook).
 */
void *hb_silver_template_canvas(void *template_view);

/*
 * Build our color view and attach it to the nearest LIVE plain composite view
 * above the current target view (found via the UI-app singleton) — the working
 * "our pixels on the current native screen" path. `msg_handler` is a valid
 * message handler (e.g. the current controller). Returns the view (NULL on miss).
 * UI task only.
 */
void *hb_silver_add_color_view_live(void *msg_handler, int x, int y, int w,
                                    int h, uint32_t rgba);

/*
 * Read-only probe of the LIVE view hierarchy via the UI-app singleton
 * (the current-UI-app getter -> its target-view accessor), tracing the target view and its
 * parent chain (+ vtables) so we can identify a live composite to child-add to.
 * Mutates nothing. Returns the current target view (NULL if none). UI task only.
 */
void *hb_silver_probe_live_views(void);

#endif /* HB_SILVER_APP_H */
