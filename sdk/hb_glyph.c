/* hb_glyph — see hb_glyph.h. */
#include "hb_glyph.h"

void hb_glyph_draw(lv_layer_t *layer, const lv_image_dsc_t *img,
                   int cx, int cy, int target_px,
                   lv_color_t fill, bool halo, lv_color_t halo_color)
{
    int nw = (int)img->header.w, nh = (int)img->header.h;
    if (nw <= 0 || target_px <= 0)
        return;

    /* The image is drawn from coords.x1/y1 (pivot 0,0) scaled by scale_x/y, so the
     * drawn extent == native * scale / 256. Pick the scale that maps native ->
     * target_px, and place the top-left at the centred position. coords are NATIVE
     * size (just clip headroom; the drawn glyph occupies the target_px square). */
    int sc = (target_px * 256) / nw;
    int x0 = cx - target_px / 2, y0 = cy - target_px / 2;

    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = img;
    d.scale_x = d.scale_y = sc;
    d.recolor_opa = LV_OPA_COVER;        /* A8: recolor is the color of visible px */

    if (halo) {
        d.recolor = halo_color;
        for (int oy = -1; oy <= 1; oy++) {
            for (int ox = -1; ox <= 1; ox++) {
                if (!ox && !oy) continue;
                lv_area_t a = { x0 + ox, y0 + oy, x0 + ox + nw - 1, y0 + oy + nh - 1 };
                lv_draw_image(layer, &d, &a);
            }
        }
    }
    d.recolor = fill;
    lv_area_t a = { x0, y0, x0 + nw - 1, y0 + nh - 1 };
    lv_draw_image(layer, &d, &a);
}

const lv_image_dsc_t *hb_glyph_suit(int suit)
{
    switch (suit) {
        case 0: return &hb_glyph_club;
        case 1: return &hb_glyph_diamond;
        case 2: return &hb_glyph_heart;
        case 3: return &hb_glyph_spade;
    }
    return NULL;
}
