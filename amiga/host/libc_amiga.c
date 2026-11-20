/* The few C library calls the game's C makes (src/rom.c, src/dispatch.c, src/ram.c, src/rosters.c):
 * files through dos.library, memory through exec AllocVec, and messages to the Shell. Declared by
 * the cross compiler's own headers, so the prototypes are the ones src/ was compiled against. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amiga.h"
#include "libc_amiga.h"

void *SysBase, *DOSBase;
FILE *stderr;
static char err[256];
void (*libc_abort_hook)(const char *why);

#define LVO_Seek     (-66)
#define LVO_AllocVec (-684)
#define LVO_FreeVec  (-690)
#define MODE_OLDFILE 1005

/* a little printf: %d %u %x %X %c %s with 0, a width and l */
int libc_vformat(char *out, size_t cap, const char *f, va_list ap)
{
    size_t n = 0;
#define PUT(c) do { if (n + 1 < cap) out[n] = (c); n++; } while (0)
    for (; *f; f++) {
        if (*f != '%') { PUT(*f); continue; }
        f++;
        int zero = 0, width = 0;
        if (*f == '0') { zero = 1; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        while (*f == 'l') f++;
        char t[16];
        int k = 0;
        unsigned long v;
        switch (*f) {
        case 'd': { long d = va_arg(ap, long); v = d < 0 ? (unsigned long)-d : (unsigned long)d; do t[k++] = (char)('0' + v % 10); while (v /= 10); if (d < 0) t[k++] = '-'; break; }
        case 'u': v = va_arg(ap, unsigned long); do t[k++] = (char)('0' + v % 10); while (v /= 10); break;
        case 'x': case 'X': { const char *h = *f == 'x' ? "0123456789abcdef" : "0123456789ABCDEF"; v = va_arg(ap, unsigned long); do t[k++] = h[v & 15]; while (v >>= 4); break; }
        case 'c': t[k++] = (char)va_arg(ap, int); break;
        case 's': { const char *s = va_arg(ap, const char *); if (!s) s = "(null)"; int len = (int)strlen(s); for (int i = len; i < width; i++) PUT(' '); while (*s) PUT(*s++); continue; }
        case '%': PUT('%'); continue;
        default: PUT('?'); continue;
        }
        for (int i = k; i < width; i++) PUT(zero ? '0' : ' ');
        while (k) PUT(t[--k]);
    }
    if (cap) out[n < cap ? n : cap - 1] = 0;
    return (int)n;
#undef PUT
}

int libc_format(char *out, size_t cap, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    int n = libc_vformat(out, cap, f, ap);
    va_end(ap);
    return n;
}

void libc_write(const char *s)
{
    ULONG fh = DOSBase ? CALL(DOSBase, LVO_Output) : 0;
    if (fh) CALL(DOSBase, LVO_Write, REG(D1, fh), REG(D2, s), REG(D3, strlen(s)));
}

int fprintf(FILE *f, const char *fmt, ...)
{
    (void)f;
    va_list ap;
    va_start(ap, fmt);
    int n = libc_vformat(err, sizeof err, fmt, ap);
    va_end(ap);
    libc_write(err);
    return n;
}

int fputs(const char *s, FILE *f) { (void)f; libc_write(s); return 0; }
size_t fwrite(const void *p, size_t size, size_t n, FILE *f)
{
    (void)f;
    ULONG fh = DOSBase ? CALL(DOSBase, LVO_Output) : 0;
    if (fh) CALL(DOSBase, LVO_Write, REG(D1, fh), REG(D2, p), REG(D3, size * n));
    return n;
}
int fputc(int c, FILE *f) { char b[2] = { (char)c, 0 }; (void)f; libc_write(b); return c; }

FILE *fopen(const char *path, const char *mode)
{
    (void)mode;
    return DOSBase ? (FILE *)CALL(DOSBase, LVO_Open, REG(D1, path), REG(D2, MODE_OLDFILE)) : NULL;
}
size_t fread(void *buf, size_t size, size_t n, FILE *f)
{
    LONG got = (LONG)CALL(DOSBase, LVO_Read, REG(D1, f), REG(D2, buf), REG(D3, size * n));
    return got > 0 && size ? (size_t)got / size : 0;
}
int fclose(FILE *f) { CALL(DOSBase, LVO_Close, REG(D1, f)); return 0; }

void *malloc(size_t n) { return (void *)CALL(SysBase, LVO_AllocVec, REG(D0, n), REG(D1, MEMF_ANY)); }
void free(void *p) { if (p) CALL(SysBase, LVO_FreeVec, REG(A1, p)); }

void abort(void)
{
    libc_write("NHL94AGA: the game stopped (abort)\n");
    if (libc_abort_hook) libc_abort_hook("abort");
    for (;;) {}
}

/* a whole file into memory (AllocVec): its size in *size, NULL when it cannot be read */
void *libc_load(const char *path, ULONG *size)
{
    ULONG fh = CALL(DOSBase, LVO_Open, REG(D1, path), REG(D2, MODE_OLDFILE));
    if (!fh) return NULL;
    CALL(DOSBase, LVO_Seek, REG(D1, fh), REG(D2, 0), REG(D3, 1));            /* OFFSET_END */
    LONG n = (LONG)CALL(DOSBase, LVO_Seek, REG(D1, fh), REG(D2, 0), REG(D3, (ULONG)-1));   /* back; the end's offset */
    void *p = n > 0 ? malloc((size_t)n + 1) : NULL;
    if (p && (LONG)CALL(DOSBase, LVO_Read, REG(D1, fh), REG(D2, p), REG(D3, n)) != n) { free(p); p = NULL; }
    CALL(DOSBase, LVO_Close, REG(D1, fh));
    if (p) { ((char *)p)[n] = 0; *size = (ULONG)n; }
    return p;
}

int libc_save(const char *path, const void *data, ULONG size)
{
    ULONG fh = CALL(DOSBase, LVO_Open, REG(D1, path), REG(D2, MODE_NEWFILE));
    if (!fh) return 0;
    LONG n = (LONG)CALL(DOSBase, LVO_Write, REG(D1, fh), REG(D2, data), REG(D3, size));
    CALL(DOSBase, LVO_Close, REG(D1, fh));
    return n == (LONG)size;
}

/* up to max bytes of a file into buf: the bytes read, -1 when it cannot be opened */
LONG libc_read_into(const char *path, void *buf, ULONG max)
{
    ULONG fh = CALL(DOSBase, LVO_Open, REG(D1, path), REG(D2, MODE_OLDFILE));
    if (!fh) return -1;
    LONG n = (LONG)CALL(DOSBase, LVO_Read, REG(D1, fh), REG(D2, buf), REG(D3, max));
    CALL(DOSBase, LVO_Close, REG(D1, fh));
    return n;
}
