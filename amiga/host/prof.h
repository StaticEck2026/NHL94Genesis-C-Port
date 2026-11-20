/* amiga/host/prof.c: the verify build's sampling profiler */
#pragma once
#include "amiga.h"
int prof_start(void);
void prof_sample(int on);
/* while the picture is shown (screen.c takes the machine and its handler samples): the counts only */
extern volatile ULONG prof_on;
int prof_alloc(void);
void prof_write(const char *path);                /* NAME.prof, and the counts freed */
void show_handler(void);
extern ULONG show_dbg;                            /* screen.c's debug block, for show_handler */                         /* samples off (0) or on, between prof_start and prof_stop */
void prof_stop(void);
void prof_save(const char *path);
