/* P0.3: which 4 KB pages of the cartridge the game reads in a run (amiga/notes/rom_map.md).
 * Linked into the VANILLA=1 host build with -Wl,--wrap=rom_load (amiga/tools/rommap.sh):
 * the ROM is copied to page-aligned memory with no access; the first read of each page faults, is
 * counted, and opens the page. Every $ROMTOUCH_EVERY vblanks (default 60; --wrap=hal_wait_vblank)
 * the window's pages go to $ROMTOUCH_OUT as one line, "<first frame> <page> <page> ...", each page
 * its 68k address in hex, and every page is closed again. Never any of the ROM's bytes. src/ is not
 * changed. */
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "rom.h"

#define PAGE 4096u
#define PAGES (ROM_SIZE / PAGE)
int __real_rom_load(const char *path);
int __wrap_rom_load(const char *path);
void __real_hal_wait_vblank(void);
void __wrap_hal_wait_vblank(void);
long native_frames(void);
static FILE *out;
static long every = 60, window_start;
static uint8_t *copy;
static unsigned char touched[PAGES];

static void on_fault(int sig, siginfo_t *si, void *ctx)
{
    (void)ctx;
    uint8_t *a = si->si_addr;
    if (!copy || a < copy || a >= copy + ROM_SIZE) { signal(sig, SIG_DFL); raise(sig); return; }
    size_t p = (size_t)(a - copy) / PAGE;
    touched[p] = 1;
    mprotect(copy + p * PAGE, PAGE, PROT_READ);
}

static void window(void)
{
    if (!out || !copy) return;
    fprintf(out, "%ld", window_start);
    for (unsigned p = 0; p < PAGES; p++)
        if (touched[p]) fprintf(out, " %06X", p * PAGE);
    fputc('\n', out);
    memset(touched, 0, sizeof touched);
    mprotect(copy, ROM_SIZE, PROT_NONE);
}

static void dump(void)
{
    window();
    if (out && out != stderr) fclose(out);
}

void __wrap_hal_wait_vblank(void)
{
    __real_hal_wait_vblank();
    long f = native_frames();
    if (f - window_start >= every) { window(); window_start = f; }
}

int __wrap_rom_load(const char *path)
{
    if (!__real_rom_load(path)) return 0;
    copy = mmap(NULL, ROM_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (copy == MAP_FAILED) return 0;
    memcpy(copy, rom_cart, ROM_SIZE);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fault;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    mprotect(copy, ROM_SIZE, PROT_NONE);
    rom_cart = copy;
    const char *o = getenv("ROMTOUCH_OUT"), *e = getenv("ROMTOUCH_EVERY");
    out = o ? fopen(o, "w") : stderr;
    if (e && atol(e) > 0) every = atol(e);
    atexit(dump);
    return 1;
}
