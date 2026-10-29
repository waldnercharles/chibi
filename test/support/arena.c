#include "support/arena.h"

#include "support/array.h"
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

static size_t indexed_value_size(int i)
{
    return i % 100 ? (size_t)(i % 7) + 1 : 3 * 4096 + 5;
}
static bool indexed_values_match(const arena *storage, unsigned char **values, int keep)
{
    for (int i = 0; i < acount(values); i++) {
        size_t size = indexed_value_size(i);
        bool live = i % keep == 0;
        if (arena_contains(storage, values[i]) != live ||
            arena_contains(storage, values[i] + size / 2) != live ||
            arena_contains(storage, values[i] + size - 1) != live ||
            arena_contains(storage, values[i] + size)) {
            return false;
        }
    }
    return true;
}
static void collect_every(arena *storage, unsigned char **values, int keep)
{
    arena *arenas[] = { storage };
    arena_collection collection;
    arena_collect_begin(&collection, arenas, 1);
    for (int i = 0; i < acount(values); i += keep) {
        arena_collect_mark(&collection, values[i]);
    }
    arena_collect_end(&collection);
}

/* Collectable values are found by address without walking the live values:
 * every byte of small and multi-page values hits, the byte past each value and
 * other arenas' values miss, and swept values leave the index. */
TEST_CASE(collectable_arena_finds_values_by_address)
{
    enum
    {
        COUNT = 2000
    };
    arena storage, other;
    arena_init_collectable(&storage);
    arena_init_collectable(&other);
    unsigned char **values = NULL;
    for (int i = 0; i < COUNT; i++) {
        apush(values, arena_alloc(&storage, indexed_value_size(i)));
    }
    unsigned char *foreign = arena_alloc(&other, 16);
    arena_set_trace(&storage, values[0], NULL, NULL);
    REQUIRE(indexed_values_match(&storage, values, 1));
    REQUIRE(!arena_contains(&storage, foreign) && !arena_contains(&other, values[1]));
    collect_every(&storage, values, 3);
    REQUIRE(indexed_values_match(&storage, values, 3));
    collect_every(&storage, values, 300);
    REQUIRE(indexed_values_match(&storage, values, 300));
    unsigned char *reused = arena_alloc(&storage, 4);
    REQUIRE(arena_contains(&storage, reused + 3));
    /* A reset keeps the index, so refilling the arena allocates nothing new. */
    arena_reset(&storage);
    REQUIRE(!arena_contains(&storage, values[0]) && !arena_contains(&storage, reused));
    size_t retained = arena_storage_bytes(&storage);
    uint64_t blocks = arena_block_allocations();
    unsigned char *again = arena_alloc(&storage, 4);
    REQUIRE(arena_contains(&storage, again + 3));
    REQUIRE(arena_storage_bytes(&storage) == retained && arena_block_allocations() == blocks);
    afree(values);
    arena_free(&storage);
    arena_free(&other);
    return true;
}
/* Small collectable values share slabs, which double from 1 KiB: a thousand of
 * them take six backing allocations, start zeroed and aligned, and swept
 * values' slots serve later allocations without another. */
TEST_CASE(collectable_arena_packs_small_values_into_slabs)
{
    enum
    {
        COUNT = 1000,
        SIZE = 24
    };
    arena storage;
    arena_init_collectable(&storage);
    unsigned char **values = NULL;
    uint64_t blocks = arena_block_allocations();
    for (int i = 0; i < COUNT; i++) {
        unsigned char *value = arena_alloc(&storage, SIZE);
        REQUIRE(aligned(value) && filled(value, SIZE, 0));
        memset(value, 0xab, SIZE);
        apush(values, value);
    }
    REQUIRE(arena_block_allocations() - blocks == 6);
    collect_every(&storage, values, 2);
    for (int i = 0; i < COUNT / 2; i++) {
        unsigned char *value = arena_alloc(&storage, SIZE);
        REQUIRE(filled(value, SIZE, 0) && !arena_contains(&storage, value + SIZE));
    }
    REQUIRE(arena_block_allocations() - blocks == 6);
    afree(values);
    arena_free(&storage);
    return true;
}
/* An arena remembers each source's latest copy until a reset, free or
 * collection, however many sources it holds, and counts the table toward its
 * storage. */
TEST_CASE(arena_remembers_copies_until_reset_or_collection)
{
    enum
    {
        SOURCES = 100
    };
    static int sources[SOURCES];
    arena storage = { 0 };
    void *copies[SOURCES];
    REQUIRE(!arena_copy_find(&storage, &sources[0]));
    for (int i = 0; i < SOURCES; i++) {
        REQUIRE(!arena_copy_find(&storage, &sources[i]));
        copies[i] = arena_alloc(&storage, 8);
        arena_copy_remember(&storage, &sources[i], copies[i]);
        REQUIRE(arena_copy_find(&storage, &sources[i]) == copies[i]);
        REQUIRE(arena_copy_find(&storage, &sources[0]) == copies[0]);
    }
    void *replacement = arena_alloc(&storage, 8);
    arena_copy_remember(&storage, &sources[1], replacement);
    REQUIRE(arena_copy_find(&storage, &sources[1]) == replacement);
    REQUIRE(arena_copy_find(&storage, &sources[SOURCES - 1]) == copies[SOURCES - 1]);
    arena_reset(&storage);
    REQUIRE(!arena_copy_find(&storage, &sources[1]));
    arena_copy_remember(&storage, &sources[0], arena_alloc(&storage, 8));
    arena_free(&storage);
    REQUIRE(!arena_copy_find(&storage, &sources[0]));
    arena collectable;
    arena_init_collectable(&collectable);
    void *kept = arena_alloc(&collectable, 8);
    arena_copy_remember(&collectable, &sources[0], kept);
    REQUIRE(arena_copy_find(&collectable, &sources[0]) == kept);
    arena *arenas[] = { &collectable };
    arena_collection collection;
    arena_collect_begin(&collection, arenas, 1);
    arena_collect_mark(&collection, kept);
    arena_collect_end(&collection);
    REQUIRE(!arena_copy_find(&collectable, &sources[0]));
    arena_free(&collectable);
    /* The table of copies counts toward the arena's storage. */
    arena counted = { 0 };
    void *copy = arena_alloc(&counted, 8);
    size_t bytes = arena_storage_bytes(&counted);
    arena_copy_remember(&counted, &sources[0], copy);
    REQUIRE(
        arena_storage_bytes(&counted) ==
        bytes + counted.copy_capacity * sizeof *counted.copies
    );
    arena_free(&counted);
    return true;
}

void test_arena()
{
    RUN_TEST_CASE(arena_growth_keeps_allocations_and_reset_reuses_blocks);
    RUN_TEST_CASE(zero_arena_allocates_on_first_use);
    RUN_TEST_CASE(collectable_arena_finds_values_by_address);
    RUN_TEST_CASE(collectable_arena_packs_small_values_into_slabs);
    RUN_TEST_CASE(arena_remembers_copies_until_reset_or_collection);
}
