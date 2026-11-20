/* What GCC may call on its own in a freestanding build (struct copies and clears). No C library is
 * linked into the Amiga program. */
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (n >= 16 && !(((unsigned long)d | (unsigned long)s) & 1)) {   /* (even: the 68020 moves longs from there) */
        unsigned long *dl = (unsigned long *)d;
        const unsigned long *sl = (const unsigned long *)s;
        for (; n >= 16; n -= 16) { dl[0] = sl[0]; dl[1] = sl[1]; dl[2] = sl[2]; dl[3] = sl[3]; dl += 4; sl += 4; }
        for (; n >= 4; n -= 4) *dl++ = *sl++;
        d = (unsigned char *)dl;
        s = (const unsigned char *)sl;
    }
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) while (n--) *d++ = *s++;
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    if (n >= 16 && !((unsigned long)d & 1)) {
        unsigned long v = (unsigned char)c * 0x01010101ul, *dl = (unsigned long *)d;
        for (; n >= 16; n -= 16) { dl[0] = v; dl[1] = v; dl[2] = v; dl[3] = v; dl += 4; }
        for (; n >= 4; n -= 4) *dl++ = v;
        d = (unsigned char *)dl;
    }
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y) return *x - *y;
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}
