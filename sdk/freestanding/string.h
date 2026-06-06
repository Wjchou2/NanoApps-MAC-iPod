/*
 * Minimal string.h shim for LVGL bundled libs (qrcode, barcode).
 * Bare-name versions used by qrcodegen.c et al.
 *
 * memset/memcpy already have weak definitions in hb_mipi.c; we
 * provide memmove, strlen, strcmp, strcpy, memcmp here.
 */
#ifndef _HB_SHIM_STRING_H
#define _HB_SHIM_STRING_H

#include <stddef.h>

void   *memcpy (void *dst, const void *src, size_t n);
void   *memset (void *s, int c, size_t n);

static inline void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) { for (size_t i = 0; i < n; i++) d[i] = s[i]; }
    else if (d > s) { for (size_t i = n; i-- > 0; ) d[i] = s[i]; }
    return dst;
}
static inline size_t strlen(const char *s)
{
    const char *p = s; while (*p) p++; return (size_t)(p - s);
}
static inline int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static inline int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++) if (p[i] != q[i]) return (int)p[i] - (int)q[i];
    return 0;
}
static inline char *strcpy(char *dst, const char *src)
{
    char *p = dst; while ((*p++ = *src++)) {} return dst;
}
static inline char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char *)s;
        if (!*s) return (char *)0;
    }
}
static inline int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

#endif
