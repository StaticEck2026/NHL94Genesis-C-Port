/* P3: amiga/host/arena.c on the host. Scenes of random allocations with marks and releases, pools
 * filled and drained in random order, overruns planted to see the guards catch them, and an
 * arena run out to see it fail by name. Exit 0 when every check holds.
 * Build and run: cc -std=gnu11 -O2 -Wall -Wextra -Werror -Iamiga/host amiga/host/arena.c amiga/tools/arenatest.c -o build/amiga-tools/arenatest */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arena.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static uint32_t rnd = 1994;
static uint32_t next(void) { rnd = rnd * 1103515245u + 12345u; return rnd >> 8; }

int main(void)
{
    enum { SIZE = 256 * 1024 };
    uint8_t *mem = malloc(SIZE);
    struct arena a;
    arena_init(&a, "SCENE", mem, SIZE, 1);
    CHECK(!arena_check(&a), "a new arena's guards");

    /* scenes: a mark, random blocks (each filled to its last byte), a check, the release */
    for (int scene = 0; scene < 200; scene++) {
        int m = arena_mark(&a);
        uint32_t used = a.used;
        for (int i = 0; i < 40; i++) {
            uint32_t n = next() % 3000 + 1, al = 4u << (next() % 4);
            uint8_t *p = arena_alloc(&a, n, al);
            if (!p) break;
            CHECK(((uintptr_t)p & (al - 1)) == 0, "alignment %u", al);
            memset(p, 0x5A, n);
        }
        CHECK(!arena_check(&a), "guards after scene %d", scene);
        arena_release(&a, m);
        CHECK(a.used == used, "release back to the mark (scene %d)", scene);
    }
    CHECK(!a.failed, "no scene ran the arena out");

    /* an overrun of one byte past a block is caught */
    int m = arena_mark(&a);
    uint8_t *p = arena_alloc(&a, 100, 4);
    p[100] = 0;
    p[101] = 0;
    p[102] = 0;
    p[103] = 0;
    CHECK(arena_check(&a), "an overrun past a block");
    arena_release(&a, m);
    CHECK(!arena_check(&a), "guards after releasing the overrun block");

    /* pools: random gets and puts against a shadow count, guards intact, then all at once */
    struct pool pl;
    CHECK(pool_init(&pl, &a, "BOB", 60, 64), "pool carved");
    void *held[64];
    int nheld = 0;
    for (int i = 0; i < 100000; i++) {
        if (nheld < 64 && (next() & 1)) {
            void *q = pool_get(&pl);
            CHECK(q != NULL, "a pool item while %d held", nheld);
            memset(q, 0x77, 60);
            held[nheld++] = q;
        } else if (nheld) {
            int k = (int)(next() % (uint32_t)nheld);
            pool_put(&pl, held[k]);
            held[k] = held[--nheld];
        }
        CHECK((int)pl.in_use == nheld, "pool count");
    }
    while (nheld < 64) held[nheld++] = pool_get(&pl);
    CHECK(pool_get(&pl) == NULL, "a full pool says so");
    CHECK(!pool_check(&pl), "pool guards");
    ((uint8_t *)held[3])[60] = 1;
    CHECK(pool_check(&pl), "an overrun in a pool item");

    /* running out fails by name, not by crashing */
    struct arena small;
    static uint8_t tiny[1024];
    arena_init(&small, "AUDIO", tiny, sizeof tiny, 0);
    CHECK(arena_alloc(&small, 2000, 4) == NULL && small.failed, "a full arena fails (%s)", small.name);

    printf("arenatest: %s (high-water %u of %u bytes, pool high %u of %u)\n", fails ? "FAIL" : "PASS", a.high, a.size, pl.high, pl.n);
    free(mem);
    return fails != 0;
}
