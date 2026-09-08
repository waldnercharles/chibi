#include "support/arena.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void test_arena()
{
    arena storage;
    arena_init(&storage, 1);
    unsigned char *allocations[24];
    size_t sizes[24];
    for (int i = 0; i < 24; i++) {
        sizes[i] = (size_t)(i + 1) * 137;
        allocations[i] = arena_alloc(&storage, sizes[i]);
        assert((uintptr_t)allocations[i] % _Alignof(max_align_t) == 0);
        memset(allocations[i], i + 1, sizes[i]);
        for (int j = 0; j <= i; j++) {
            for (size_t k = 0; k < sizes[j]; k++) {
                assert(allocations[j][k] == j + 1);
            }
        }
    }
    arena_reset(&storage);
    for (int i = 0; i < 24; i++) {
        assert(arena_alloc(&storage, sizes[i]) == allocations[i]);
    }
    arena_reset(&storage);
    unsigned char *tiny = arena_alloc(&storage, 1);
    *tiny = 79;
    void *large = arena_alloc(&storage, 1024 * 1024);
    assert((uintptr_t)large % _Alignof(max_align_t) == 0);
    memset(large, 42, 1024 * 1024);
    assert(*tiny == 79);
    arena_reset(&storage);
    assert(arena_alloc(&storage, 1) == tiny);
    assert(arena_alloc(&storage, 1024 * 1024) == large);
    arena_free(&storage);
    arena_free(&storage);
    arena zero = { 0 };
    arena_reset(&zero);
    assert(arena_alloc(&zero, 0));
    int *value = arena_alloc(&zero, sizeof *value);
    *value = 17;
    assert(*value == 17);
    arena_free(&zero);
    puts("PASS: arena growth preserves aligned allocations and reset reuses retained blocks");
}
