/* amiga/host/libc_amiga.c: the library bases it uses, and its helpers */
#pragma once
#include <stdarg.h>
#include <stddef.h>
#include "amiga.h"

extern void *SysBase, *DOSBase;
extern void (*libc_abort_hook)(const char *why);
int libc_vformat(char *out, size_t cap, const char *f, va_list ap);
int libc_format(char *out, size_t cap, const char *f, ...);
void libc_write(const char *s);
void *libc_load(const char *path, ULONG *size);
int libc_save(const char *path, const void *data, ULONG size);
LONG libc_read_into(const char *path, void *buf, ULONG max);
