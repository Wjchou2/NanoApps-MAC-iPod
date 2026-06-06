/*
 * hb_t9.h — predictive T9 keyboard widget for LVGL apps.
 *
 * Creates a self-contained widget tree under a parent container:
 *   - top: scrollable strip of candidate words (predictions)
 *   - bottom: 3×4 multi-tap keypad (1 punctuation, 2-9 letters,
 *     * shift/caps, 0 space, # symbol page, backspace)
 *
 * As the user taps digit keys 2..9 we build a digit-sequence and
 * look up matching words from a small embedded English dictionary
 * (~500 most common words). Tapping a candidate commits it into the
 * target textarea. If no candidate matches the sequence, the widget
 * falls back to multi-tap mode for that key — pressing the same key
 * within a short window cycles through its letters in-place.
 *
 * Usage:
 *   lv_obj_t *container = ...;       // 240 wide, ~230 tall
 *   lv_obj_t *ta = lv_textarea_create(...);
 *   hb_t9_create(container, ta);
 *
 * The widget intercepts presses on its own keys only — the textarea
 * stays focused for the cursor caret.
 */
#ifndef HB_T9_H_
#define HB_T9_H_

#include "lvgl/lvgl.h"

/* Create a T9 keyboard inside `parent` targeting `textarea`.
   Widget fills parent's full width and ~230 px of height (suggestion
   strip + keypad). Returns the keypad root or NULL on failure. */
lv_obj_t *hb_t9_create(lv_obj_t *parent, lv_obj_t *textarea);

#endif
