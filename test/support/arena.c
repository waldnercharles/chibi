#include "support/arena.h"

#include "third_party/pico_headers/pico_unit.h"

#include <stdint.h>
#include <string.h>

static bool aligned(const void *address)
{
    return (uintptr_t)address % _Alignof(max_align_t) == 0;
}

static bool filled(const unsigned char *bytes, size_t size, int value)
{
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != value) {
            return false;
        }
    }
    return true;
}

/* Growing keeps earlier allocations intact and aligned, and a reset hands out
 * the retained blocks again in the same order. */
TEST_CASE(arena_growth_keeps_allocations_and_reset_reuses_blocks)
{
    arena storage;
    arena_init(&storage, 1);
    unsigned char *allocations[24];
    size_t sizes[24];
    for (int i = 0; i < 24; i++) {
        sizes[i] = (size_t)(i + 1) * 137;
        allocations[i] = arena_alloc(&storage, sizes[i]);
        REQUIRE(aligned(allocations[i]));
        memset(allocations[i], i + 1, sizes[i]);
        for (int j = 0; j <= i; j++) {
            REQUIRE(filled(allocations[j], sizes[j], j + 1));
        }
    }
    arena_reset(&storage);
    for (int i = 0; i < 24; i++) {
        REQUIRE(arena_alloc(&storage, sizes[i]) == allocations[i]);
    }
    arena_reset(&storage);
    unsigned char *tiny = arena_alloc(&storage, 1);
    *tiny = 79;
    void *large = arena_alloc(&storage, 1024 * 1024);
    REQUIRE(aligned(large));
    memset(large, 42, 1024 * 1024);
    REQUIRE(*tiny == 79);
    arena_reset(&storage);
    REQUIRE(arena_alloc(&storage, 1) == tiny);
    REQUIRE(arena_alloc(&storage, 1024 * 1024) == large);
    arena_free(&storage);
    arena_free(&storage);
    return true;
}

/* A zero-initialized arena resets, and allocates on first use, even for zero
 * bytes. */
TEST_CASE(zero_arena_allocates_on_first_use)
{
    arena zero = { 0 };
    arena_reset(&zero);
    REQUIRE(arena_alloc(&zero, 0));
    int *value = arena_alloc(&zero, sizeof *value);
    *value = 17;
    REQUIRE(*value == 17);
    arena_free(&zero);
    return true;
}

void test_arena()
{
    RUN_TEST_CASE(arena_growth_keeps_allocations_and_reset_reuses_blocks);
    RUN_TEST_CASE(zero_arena_allocates_on_first_use);
}
