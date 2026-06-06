/*
 * hb_sysfx.h — device-color override (for matched screenshots).
 *
 * The OS uses the device color to pick color wallpapers, clock faces
 * and accent palettes; overriding it lets any device render as if it were
 * another for screenshots.
 */
#ifndef HB_SYSFX_H
#define HB_SYSFX_H

/* Device color. Indices are 0..hb_devcolor_count()-1; setting one recolors
 * the OS's color resources (wallpaper, clock faces, palette). The factory
 * color is captured on first access and restored by _reset(). */
int  hb_devcolor_count(void);
int  hb_devcolor_get(void);      /* current index, or -1 if unavailable */
unsigned int hb_devcolor_tint(void);  /* accent RGB of the current device color */
void hb_devcolor_set(int index);
void hb_devcolor_reset(void);

#endif /* HB_SYSFX_H */
