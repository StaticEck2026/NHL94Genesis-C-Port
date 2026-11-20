/* P3: the port's memory manager (PROMPT.md 6.4). Memory is planned like the frame: a few arenas,
 * each one exec allocation made at start-up (Chip RAM where DMA reads it), carved by a bump/stack
 * allocator with scene marks, and fixed-size pools for things that come and go. Nothing is
 * allocated from exec after the match starts: an arena that runs out is a load-time failure with
 * its name, never a fallback.
 *
 * Every block and every arena has guard words at both ends (checked by arena_check), an arena's
 * free space is filled with a pattern in the verify build, and each arena keeps its high-water
 * mark for the report (STATUS.md's memory map). Portable C: the Amiga passes exec memory in, the
 * host tests (amiga/tools/arenatest.c) pass malloc'd memory. */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define ARENA_GUARD 0xA5E1A5E1u          /* a guard word */
#define ARENA_FILL  0xDEADBEEFu          /* free space, in the verify build */

struct arena {
    const char *name;
    uint8_t *base;                       /* the exec allocation, after its front guard */
    uint32_t size, used, high;           /* bytes usable, in use now, the most ever in use */
    uint32_t marks[8];                   /* scene marks (arena_mark / arena_release) */
    int nmarks, fill, failed;
};

/* a pool of n fixed-size items carved from an arena: a free list, O(1) both ways */
struct pool {
    const char *name;
    uint8_t *items;
    uint32_t item, n, in_use, high;      /* item: the size with its two guards */
    void *free;
};

void arena_init(struct arena *a, const char *name, void *mem, uint32_t bytes, int fill);
void *arena_alloc(struct arena *a, uint32_t bytes, uint32_t align);   /* NULL and a->failed when it is full */
int arena_mark(struct arena *a);                                     /* a scene starts: its mark */
void arena_release(struct arena *a, int mark);                       /* the scene ends: back to its mark */
int arena_check(const struct arena *a);                              /* 0: every guard intact */

int pool_init(struct pool *p, struct arena *a, const char *name, uint32_t item, uint32_t n);
void *pool_get(struct pool *p);                                      /* NULL when all are in use */
void pool_put(struct pool *p, void *item);
int pool_check(const struct pool *p);                                /* 0: every item's guards intact */
