/* hb_glyph — draw the generated A8 card-game glyph bitmaps (sdk/generated/
 * hb_glyphs.{c,h}) onto a canvas layer, recolored, with an optional outline
 * halo. Replaces the letter glyphs the card games used to draw with lv_draw_label
 * (chess pieces, card suits). */
#ifndef HB_GLYPH_H
#define HB_GLYPH_H

#include "lvgl.h"
#include "generated/hb_glyphs.h"

/* Draw glyph image `img` (an A8 lv_image_dsc_t) centred at (cx, cy), scaled to a
 * target_px square, with the visible pixels recolored to `fill`. If `halo`, an
 * 8-direction outline in `halo_color` is drawn first so the glyph reads on any
 * background (chess pieces over light/dark squares). */
void hb_glyph_draw(lv_layer_t *layer, const lv_image_dsc_t *img,
                   int cx, int cy, int target_px,
                   lv_color_t fill, bool halo, lv_color_t halo_color);

/* Card suit by the card games' index order (0=club, 1=diamond, 2=heart, 3=spade)
 * -> its glyph bitmap, or NULL if out of range. */
const lv_image_dsc_t *hb_glyph_suit(int suit);

#endif /* HB_GLYPH_H */
