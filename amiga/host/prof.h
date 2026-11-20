/* amiga/host/prof.c: the verify build's sampling profiler */
#pragma once
#include "amiga.h"
int prof_start(void);
void prof_stop(void);
void prof_save(const char *path);
