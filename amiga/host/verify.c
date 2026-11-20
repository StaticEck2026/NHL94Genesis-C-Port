/* NHL94VFY, phase P5 (amiga/STATUS.md): the game's own C on the A1200, with no display or sound
 * yet, played over the repository's input scripts. After every pass the work RAM hash
 * (amiga/host/hash.h) is checked against the reference (amiga/tools/halcheck's, from the host
 * build's golden runs), and the pass's time is measured with the E clock: gate A-LOGIC and the
 * feasibility measurement of PROMPT.md 6.5.
 *
 * It runs under the OS (Forbid while a script plays, so other tasks do not skew the times) and
 * reads everything first: the user's ROM (one file, or two halves on two disks named NHL94ROMA:
 * and NHL94ROMB:), each script, each reference. Between scripts the game is powered on again by
 * restoring the program's data and clearing its BSS, as a fresh load would. Results go to the
 * Shell, to the serial port (exec RawPutChar, 9600 baud) as it runs, and to NHL94VFY.log.
 * Left mouse button: stop the current script. */
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include "amiga.h"
#include "libc_amiga.h"
#include "version.h"
#include "game.h"
#include "mem.h"
#include "rom.h"
#include "rules.h"
#include "rosters.h"
#include "hal_core.h"
#include "hash.h"

const char nhl94vfy_vertag[] __attribute__((used)) = "\0$VER: NHL94VFY " NHL94AGA_VERSION;

extern char __data_start[], __data_end[], __bss_start[], __bss_end[];
extern void (*__init_array_start[])(void), (*__init_array_end[])(void);
void call_with_stack(void (*fn)(void), void *lower, void *upper);

#define LVO_RawPutChar (-516)
#define LVO_ReadEClock (-60)
#define ROM_CRC32 0x9438F5DDu
#define FRAME_US 16688u                         /* one NTSC Genesis frame: 1 / 59.923 Hz */

static const struct test { const char *name; long frames; const char *script; } tests[] = {
    { "attract", 7199, 0 },
    { "full_game", 15999, "full_game_30s.txt" },
    { "playoff_game", 14999, "playoff_game_30s.txt" },
    { "records_game", 15699, "records_game_30s.txt" },
    { "pause_menus", 8599, "pause_menus.txt" },
    { "shootout_menus", 4799, "shootout_menus.txt" },
    { "name_entry", 2999, "name_entry.txt" },
};
#define NTESTS (int)(sizeof tests / sizeof tests[0])

/* what survives the game's power on: kept in exec memory, its address in .keep */
struct result { long frames, bad, first_bad, max_us, max_frame, first_us, over90, over100, aborted, noref; unsigned long long sum_us; uint32_t dmas, commands; };
struct keep {
    void *sys, *dos, *timer;
    const uint8_t *rom;
    char *data_snap;
    struct result r[NTESTS];
    char log[6144];
    ULONG loglen;
    ULONG eclock_hz;
    int only;                                   /* -1: every test */
};
static struct keep *K __attribute__((section(".keep")));

/* ---- the log: the Shell, the serial port, and the file at the end ----------------------------- */

static void logs(const char *s)
{
    libc_write(s);
    for (const char *p = s; *p; p++) {
        CALL(K->sys, LVO_RawPutChar, REG(D0, (UBYTE)*p));
        if (K->loglen < sizeof K->log - 1) K->log[K->loglen++] = *p;
    }
}
static void logf_(const char *f, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, f);
    libc_vformat(b, sizeof b, f, ap);
    va_end(ap);
    logs(b);
}

/* ---- time: the E clock through timer.device ---------------------------------------------------- */

static ULONG eclock(void)
{
    ULONG ev[2];
    CALL(K->timer, LVO_ReadEClock, REG(A0, ev));
    return ev[1];
}
static long to_us(ULONG ticks) { return (long)((unsigned long long)ticks * 1000000u / K->eclock_hz); }

/* ---- one script --------------------------------------------------------------------------------- */

static struct { long frame; uint16_t b; } *script;
static long script_len, frames_wanted, test_index;
static const uint32_t *ref;
static uint32_t *made;                           /* (no reference on the disk: the hashes, written for the host to compare) */
static ULONG ref_len, t_start;
static jmp_buf stop;
static struct result *R;

uint16_t plat_buttons(int pad, long h)
{
    if (pad) return 0;
    uint16_t b = 0;
    for (long i = 0; i < script_len && script[i].frame <= h; i++) b = script[i].b;
    return b;
}

void plat_frame_done(long frame)
{
    long us = to_us(eclock() - t_start);
    if (frame == 1) R->first_us = us;                     /* (power on: the cartridge's checksum of 1 MB) */
    else {
        R->sum_us += (unsigned long long)us;
        if (us > R->max_us) { R->max_us = us; R->max_frame = frame; }
        if (us * 10 > (long)FRAME_US * 9) R->over90++;
        if (us > (long)FRAME_US) R->over100++;
    }
    uint32_t h = ram_hash32((const uint8_t *)&g);
    if ((ULONG)frame <= ref_len && h != ref[frame - 1]) { if (!R->bad) R->first_bad = frame; R->bad++; }
    if (made && frame <= frames_wanted) made[frame - 1] = h;
    R->frames = frame;
    if (frame % 1200 == 0) logf_("  %s: frame %ld, %ld differ\n", tests[test_index].name, frame, R->bad);
    if (!(CIAA_PRA & 0x40)) { R->aborted = 1; longjmp(stop, 1); }
    if (frame >= frames_wanted) longjmp(stop, 1);
    t_start = eclock();
}

void plat_vram_byte(uint16_t a, uint8_t b) { (void)a; (void)b; }
uint8_t plat_vram_read(uint16_t a) { (void)a; return 0; }
void plat_cram(int i, uint16_t c) { (void)i; (void)c; }
void plat_reg(int r, uint8_t v) { (void)r; (void)v; }
void plat_sound_command(const uint8_t *z) { (void)z; }
void plat_sound_restart(const uint8_t *z) { (void)z; }
void plat_sound_reset(int running) { (void)running; }
void plat_psg(uint8_t v) { (void)v; }

static void stopped(const char *why) { (void)why; R->aborted = 2; longjmp(stop, 1); }

/* the game powered on: the program's data and BSS as LoadSeg left them, the constructors again */
static void power_on(void)
{
    struct keep *k = K;
    memcpy(__data_start, k->data_snap, (size_t)(__data_end - __data_start));
    memset(__bss_start, 0, (size_t)(__bss_end - __bss_start));
    SysBase = k->sys;
    DOSBase = k->dos;
    for (void (**c)(void) = __init_array_start; c < __init_array_end; c++) (*c)();
    libc_abort_hook = stopped;
}

static int parse_script(char *t)
{
    static const struct { const char *n; uint16_t bit; } names[] = {
        { "UP", 1 }, { "DOWN", 2 }, { "LEFT", 4 }, { "RIGHT", 8 }, { "B", 0x10 }, { "C", 0x20 }, { "A", 0x40 }, { "START", 0x80 },
    };
    long cap = 0;
    for (char *p = t; *p; p++) if (*p == ':') cap++;
    script = malloc((size_t)(cap + 1) * sizeof *script);
    if (!script) return 0;
    script_len = 0;
    for (char *p = t; *p;) {
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        if (*p < '0' || *p > '9') { p++; continue; }
        long f = 0;
        while (*p >= '0' && *p <= '9') f = f * 10 + (*p++ - '0');
        if (*p != ':') continue;
        p++;
        uint16_t b = 0;
        while (*p && *p != ',' && *p != '\n' && *p != '\r') {
            char *e = p;
            while (*e && *e != '+' && *e != ',' && *e != '\n' && *e != '\r') e++;
            for (int i = 0; i < 8; i++)
                if ((size_t)(e - p) == strlen(names[i].n) && !memcmp(p, names[i].n, (size_t)(e - p))) b |= names[i].bit;
            p = *e == '+' ? e + 1 : e;
        }
        script[script_len].frame = f;
        script[script_len++].b = b;
    }
    return 1;
}

static void run_one(void)
{
    const struct test *t = &tests[test_index];
    char path[96];
    ULONG n;
    script = NULL;
    script_len = 0;
    if (t->script) {
        libc_format(path, sizeof path, "PROGDIR:scripts/%s", t->script);
        char *text = libc_load(path, &n);
        if (!text || !parse_script(text)) { logf_("%s: cannot read %s\n", t->name, path); R->aborted = 3; return; }
        free(text);
    }
    libc_format(path, sizeof path, "PROGDIR:refs/%s.h32", t->name);
    ref = libc_load(path, &n);
    ref_len = ref ? n / 4 : 0;
    made = ref ? NULL : malloc((size_t)t->frames * 4);
    if (!ref) logf_("%s: no reference on this disk: its hashes go to PROGDIR:%s.h32 for amiga/tools/h32compare.py\n", t->name, t->name);
    frames_wanted = t->frames;
    memcpy(rules, rule_classic, sizeof rules);            /* the vanilla game (native/vanilla/stubs.c) */
    rom_cart = K->rom;
    rom_art = NULL;
    rom_limit = ROM_SIZE;
    rosters_retail();
    core_power_on(0);
    CALL(DOSBase, -198, REG(D1, 150));                    /* Delay(3 s): the floppy motor stops before Forbid */
    CALL(SysBase, LVO_Forbid);
    t_start = eclock();
    if (!setjmp(stop)) run68k(Start);
    CALL(SysBase, LVO_Permit);
    R->dmas = core.dmas;
    R->commands = core.commands;
    if (made) {
        libc_format(path, sizeof path, "PROGDIR:%s.h32", t->name);
        if (!libc_save(path, made, (ULONG)R->frames * 4)) logf_("%s: cannot write %s\n", t->name, path);
        free(made);
        R->noref = 1;
    }
    free((void *)ref);
    free(script);
}

static void run_tests(void)
{
    for (int i = 0; i < NTESTS; i++) {                    /* (i on the stack: power_on clears the BSS) */
        if (K->only >= 0 && i != K->only) continue;
        power_on();
        test_index = i;
        R = &K->r[test_index];
        memset(R, 0, sizeof *R);
        R->first_bad = -1;
        logf_("%s: %ld frames\n", tests[test_index].name, tests[test_index].frames);
        run_one();
        while (!(CIAA_PRA & 0x40)) {}                     /* (a skip's button let go, or it skips the next too) */
        struct result *r = &K->r[test_index];
        long passes = r->frames > 1 ? r->frames - 1 : 1;
        logf_("%s: %ld frames, %ld differ%s; pass time max %ld us (%ld%% of a frame) at frame %ld, mean %ld us, %ld over 90%%, %ld over 100%%; power-on pass %ld us%s\n",
              tests[test_index].name, r->frames, r->bad, r->bad ? " (FAIL)" : "", r->max_us, r->max_us * 100 / (long)FRAME_US, r->max_frame,
              (long)(r->sum_us / (unsigned long long)passes), r->over90, r->over100, r->first_us,
              r->aborted == 1 ? " (stopped by the mouse button)" : r->aborted ? " (stopped)" : "");
        if (r->first_bad >= 0) logf_("%s: first difference at frame %ld\n", tests[test_index].name, r->first_bad);
    }
}

/* ---- the ROM: one file, or two halves on two disks ------------------------------------------------ */

static uint32_t crc32(const uint8_t *b, ULONG n)
{
    static uint32_t table[256];
    for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; }
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = table[(c ^ *b++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static const uint8_t *load_rom(const char *given)
{
    void *me = (void *)CALL(SysBase, LVO_FindTask, REG(A1, 0));
    void **winptr = (void **)((UBYTE *)me + 184);         /* pr_WindowPtr: no "insert volume" requester while looking */
    void *saved = *winptr;
    static const char *const single[] = { "NHL94ROM:nhl94.bin", "PROGDIR:nhl94.bin", "RAM:nhl94.bin" };
    uint8_t *rom = malloc(ROM_SIZE + 1);                  /* (one buffer: the halves are read straight into it) */
    if (!rom) { logs("ROM: not enough memory for 1 MB\n"); return NULL; }
    LONG n = given ? libc_read_into(given, rom, ROM_SIZE + 1) : -1;
    *winptr = (void *)-1;
    for (unsigned i = 0; n < 0 && i < 3; i++) n = libc_read_into(single[i], rom, ROM_SIZE + 1);
    *winptr = saved;
    if (n < 0) {                                          /* the halves: DOS asks for each disk */
        logs("ROM: insert the disk NHL94ROMA when asked, then NHL94ROMB\n");
        LONG a = libc_read_into("NHL94ROMA:nhl94.rom.1", rom, ROM_SIZE / 2);
        LONG b = a == ROM_SIZE / 2 ? libc_read_into("NHL94ROMB:nhl94.rom.2", rom + ROM_SIZE / 2, ROM_SIZE / 2 + 1) : -1;
        n = a == ROM_SIZE / 2 && b == ROM_SIZE / 2 ? (LONG)ROM_SIZE : -1;
    }
    if (n < 0) { logs("ROM: not found (NHL94ROM:nhl94.bin, PROGDIR:nhl94.bin, RAM:nhl94.bin, or NHL94ROMA: + NHL94ROMB:)\n"); free(rom); return NULL; }
    uint32_t c = n == (LONG)ROM_SIZE ? crc32(rom, ROM_SIZE) : 0;
    if (c != ROM_CRC32) { logf_("ROM: %ld bytes, CRC32 %08lx: not the 1 MB US/Europe NHL '94 cartridge\n", n, c); free(rom); return NULL; }
    logs("ROM: the US/Europe cartridge (CRC32 9438F5DD)\n");
    return rom;
}

/* ---- main ---------------------------------------------------------------------------------------- */

static void *open_lib(const char *name, ULONG v) { return (void *)CALL(SysBase, LVO_OpenLibrary, REG(A1, name), REG(D0, v)); }

LONG amiga_main(void);
LONG amiga_main(void)
{
    SysBase = *(void **)4;
    DOSBase = open_lib("dos.library", 36);
    if (!DOSBase) return 20;
    K = (struct keep *)CALL(SysBase, LVO_AllocMem, REG(D0, sizeof *K), REG(D1, MEMF_ANY | MEMF_CLEAR));
    size_t dsize = (size_t)(__data_end - __data_start);
    char *snap = K ? malloc(dsize + 4) : NULL;
    if (!K || !snap) { libc_write("NHL94VFY: not enough memory\n"); return 20; }
    memcpy(snap, __data_start, dsize);                    /* (before anything changes the data) */
    K->data_snap = snap;
    K->sys = SysBase;
    K->dos = DOSBase;
    K->only = -1;
    logs("NHL94VFY " NHL94AGA_VERSION " (P5: the game's logic on this Amiga, checked against the reference)\n");
    UWORD attn = EXEC_ATTNFLAGS(SysBase);
    logf_("Kickstart %u.%u, CPU %s, chip free %lu KB, other free %lu KB\n", EXEC_VERSION(SysBase), EXEC_SOFTVER(SysBase),
          attn & AFF_68040 ? "68040+" : attn & AFF_68030 ? "68030" : attn & AFF_68020 ? "68020" : "68000/010",
          CALL(SysBase, LVO_AvailMem, REG(D1, MEMF_CHIP)) >> 10, CALL(SysBase, LVO_AvailMem, REG(D1, MEMF_FAST)) >> 10);
    LONG rc = 20;
    void *port = (void *)CALL(SysBase, LVO_CreateMsgPort);
    void *io = port ? (void *)CALL(SysBase, LVO_CreateIORequest, REG(A0, port), REG(D0, 40)) : NULL;
    if (!io || CALL(SysBase, LVO_OpenDevice, REG(A0, "timer.device"), REG(D0, 2), REG(A1, io), REG(D1, 0)) != 0) {
        logs("timer.device would not open\n");
    } else {
        K->timer = *(void **)((UBYTE *)io + 20);          /* io_Device */
        ULONG ev[2];
        K->eclock_hz = CALL(K->timer, LVO_ReadEClock, REG(A0, ev));
        logf_("E clock %lu Hz\n", K->eclock_hz);
        K->rom = load_rom(0);
        void *stack = K->rom ? malloc(65536) : NULL;
        if (stack) {
            call_with_stack(run_tests, stack, (char *)stack + 65536);
            free(stack);
            long bad = 0, ran = 0, noref = 0;
            for (int i = 0; i < NTESTS; i++) { bad += K->r[i].bad; if (K->r[i].frames) ran++; noref += K->r[i].noref; }
            if (noref) logf_("A-LOGIC: checked on the host (%ld scripts' hashes written beside the program)\n", noref);
            else logf_("A-LOGIC: %s (%ld scripts, %ld differing frames)\n", bad || !ran ? "FAIL" : "PASS", ran, bad);
            rc = bad || !ran ? 10 : 0;
        }
        free((void *)K->rom);
        CALL(SysBase, LVO_CloseDevice, REG(A1, io));
    }
    if (io) CALL(SysBase, LVO_DeleteIORequest, REG(A0, io));
    if (port) CALL(SysBase, LVO_DeleteMsgPort, REG(A0, port));
    if (!libc_save("PROGDIR:NHL94VFY.log", K->log, K->loglen)) libc_save("RAM:NHL94VFY.log", K->log, K->loglen);
    free(snap);
    CALL(SysBase, LVO_FreeMem, REG(A1, K), REG(D0, sizeof *K));
    CALL(SysBase, LVO_CloseLibrary, REG(A1, DOSBase));
    return rc;
}
