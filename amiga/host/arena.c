/* P3: amiga/host/arena.h. Each block is laid out as: guard, size, the caller's bytes (aligned),
 * guard. The arena has a guard before its first byte and after its last. */
#include <string.h>
#include "arena.h"

static void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t get32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

void arena_init(struct arena *a, const char *name, void *mem, uint32_t bytes, int fill)
{
    memset(a, 0, sizeof *a);
    a->name = name;
    a->fill = fill;
    uint8_t *m = mem;
    put32(m, ARENA_GUARD);
    put32(m + bytes - 4, ARENA_GUARD);
    a->base = m + 4;
    a->size = bytes - 8;
    if (fill)
        for (uint32_t i = 0; i + 4 <= a->size; i += 4) put32(a->base + i, ARENA_FILL);
}

void *arena_alloc(struct arena *a, uint32_t bytes, uint32_t align)
{
    if (align < 4) align = 4;
    uintptr_t at = (uintptr_t)a->base + a->used + 8;      /* (after the block's guard and size) */
    at = (at + align - 1) & ~(uintptr_t)(align - 1);      /* (the address aligned: 64-bit fetch needs it) */
    uint32_t start = (uint32_t)(at - (uintptr_t)a->base);
    uint32_t end = start + ((bytes + 3) & ~3u) + 4;       /* (and its back guard) */
    if (end > a->size || end < start) { a->failed = 1; return 0; }
    put32(a->base + start - 8, ARENA_GUARD);
    put32(a->base + start - 4, bytes);
    put32(a->base + end - 4, ARENA_GUARD);
    a->used = end;
    if (a->used > a->high) a->high = a->used;
    memset(a->base + start, 0, bytes);
    return a->base + start;
}

int arena_mark(struct arena *a)
{
    if (a->nmarks == (int)(sizeof a->marks / sizeof a->marks[0])) { a->failed = 1; return -1; }
    a->marks[a->nmarks] = a->used;
    return a->nmarks++;
}

void arena_release(struct arena *a, int mark)
{
    if (mark < 0 || mark >= a->nmarks) { a->failed = 1; return; }
    uint32_t to = a->marks[mark];
    if (a->fill)
        for (uint32_t i = to & ~3u; i + 4 <= a->used; i += 4) put32(a->base + i, ARENA_FILL);
    a->used = to;
    a->nmarks = mark;
}

/* the arena's own guards, then every block's from the start (each block names its size) */
int arena_check(const struct arena *a)
{
    if (get32(a->base - 4) != ARENA_GUARD || get32(a->base + a->size) != ARENA_GUARD) return 1;
    uint32_t at = 0;
    while (at < a->used) {
        uint32_t g;
        while (at + 8 <= a->used && (g = get32(a->base + at)) != ARENA_GUARD) at += 4;   /* (alignment padding) */
        if (at + 8 > a->used) break;
        uint32_t bytes = get32(a->base + at + 4), start = at + 8, end = start + ((bytes + 3) & ~3u) + 4;
        if (end > a->used || get32(a->base + end - 4) != ARENA_GUARD) return 1;
        at = end;
    }
    return 0;
}

int pool_init(struct pool *p, struct arena *a, const char *name, uint32_t item, uint32_t n)
{
    memset(p, 0, sizeof *p);
    p->name = name;
    p->item = ((item + 3) & ~3u) + 8;                     /* (a guard each side) */
    if (p->item < 12) p->item = 12;
    p->n = n;
    p->items = arena_alloc(a, p->item * n, 4);
    if (!p->items) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *it = p->items + i * p->item;
        put32(it, ARENA_GUARD);
        put32(it + p->item - 4, ARENA_GUARD);
        memcpy(it + 4, &p->free, sizeof p->free);
        p->free = it;
    }
    return 1;
}

void *pool_get(struct pool *p)
{
    uint8_t *it = p->free;
    if (!it) return 0;
    memcpy(&p->free, it + 4, sizeof p->free);
    if (++p->in_use > p->high) p->high = p->in_use;
    memset(it + 4, 0, p->item - 8);
    return it + 4;
}

void pool_put(struct pool *p, void *item)
{
    uint8_t *it = (uint8_t *)item - 4;
    memcpy(it + 4, &p->free, sizeof p->free);
    p->free = it;
    p->in_use--;
}

int pool_check(const struct pool *p)
{
    for (uint32_t i = 0; i < p->n; i++) {
        const uint8_t *it = p->items + i * p->item;
        if (get32(it) != ARENA_GUARD || get32(it + p->item - 4) != ARENA_GUARD) return 1;
    }
    return 0;
}
