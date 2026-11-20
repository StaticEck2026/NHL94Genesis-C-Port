/* amiga/host/prof.c: the verify build's sampling profiler */
#pragma once
#include "amiga.h"
int prof_start(void);
void prof_sample(int on);                         /* samples off (0) or on, between prof_start and prof_stop */
void prof_stop(void);
void prof_save(const char *path);
