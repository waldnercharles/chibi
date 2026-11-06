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
/* An arena remembers its last copy by source until a reset, free or
 * collection, and a later copy replaces it. */
TEST_CASE(arena_remembers_last_copy_until_reset_or_collection)
{
    static int sources[2];
    arena storage = { 0 };
    void *copy = arena_alloc(&storage, 8), *later = arena_alloc(&storage, 8);
    REQUIRE(!arena_copy_find(&storage, &sources[0]));
    arena_copy_remember(&storage, &sources[0], copy);
    REQUIRE(arena_copy_find(&storage, &sources[0]) == copy);
    REQUIRE(!arena_copy_find(&storage, &sources[1]));
    arena_copy_remember(&storage, &sources[1], later);
    REQUIRE(arena_copy_find(&storage, &sources[1]) == later);
    REQUIRE(!arena_copy_find(&storage, &sources[0]));
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
    return true;
}

typedef struct retained_resource
{
    int releases;
} retained_resource;
static void release_retained_resource(void *resource)
{
    ((retained_resource *)resource)->releases++;
}
static void collect_one(arena *storage, const void *keep)
{
    arena *arenas[] = { storage };
    arena_collection collection;
    arena_collect_begin(&collection, arenas, 1);
    if (keep) {
        arena_collect_mark(&collection, keep);
    }
    arena_collect_end(&collection);
}

/* Both storage modes release attachments once, and reused allocation addresses
 * never inherit an old attachment. */
TEST_CASE(arena_cleanup_reset_and_free_release_each_attachment_once)
{
    for (int collectable = 0; collectable < 2; collectable++) {
        arena storage = { 0 };
        if (collectable) {
            arena_init_collectable(&storage);
        }
        retained_resource shared = { 0 }, separate = { 0 };
        void *first = arena_alloc(&storage, 16);
        arena_set_cleanup(&storage, first, release_retained_resource, &shared, 100);
        arena_set_cleanup(&storage, arena_alloc(&storage, 16), release_retained_resource, &shared, 100);
        arena_reset(&storage);
        REQUIRE(shared.releases == 2);
        REQUIRE(storage.retained_bytes == 0 && storage.resource_capacity == 0);
        REQUIRE(arena_alloc(&storage, 16) == first || collectable);
        arena_reset(&storage);
        REQUIRE(shared.releases == 2);
        arena_set_cleanup(&storage, arena_alloc(&storage, 16), release_retained_resource, &separate, 200);
        arena_free(&storage);
        REQUIRE(shared.releases == 2 && separate.releases == 1);
        arena_free(&storage);
        REQUIRE(shared.releases == 2 && separate.releases == 1);
    }
    return true;
}

/* Sweeping one alias releases that reference, while the marked alias retains
 * the shared resource's full byte charge until it too retires. */
TEST_CASE(arena_cleanup_collection_keeps_shared_resource_until_last_alias)
{
    arena storage;
    arena_init_collectable(&storage);
    retained_resource shared = { 0 }, separate = { 0 };
    void *keep = arena_alloc(&storage, 16);
    void *drop = arena_alloc(&storage, 16);
    void *other = arena_alloc(&storage, 16);
    arena_set_cleanup(&storage, keep, release_retained_resource, &shared, 1000);
    arena_set_cleanup(&storage, drop, release_retained_resource, &shared, 1000);
    arena_set_cleanup(&storage, other, release_retained_resource, &separate, 2000);
    size_t bytes, capacity;
    arena_usage(&storage, &bytes, &capacity);
    REQUIRE(bytes == 3048 && capacity == 3048);
    REQUIRE(storage.allocated == 3048 && storage.kept == 0);
    collect_one(&storage, keep);
    REQUIRE(shared.releases == 1 && separate.releases == 1);
    arena_usage(&storage, &bytes, &capacity);
    REQUIRE(bytes == 1016 && capacity == 1048);
    REQUIRE(storage.allocated == 0 && storage.kept == 1016);
    REQUIRE(arena_contains(&storage, keep));
    REQUIRE(!arena_contains(&storage, drop) && !arena_contains(&storage, other));
    arena_set_cleanup(&storage, arena_alloc(&storage, 16), release_retained_resource, &shared, 1000);
    REQUIRE(storage.allocated == 16 && storage.kept == 1016);
    collect_one(&storage, keep);
    REQUIRE(shared.releases == 2);
    REQUIRE(storage.allocated == 0 && storage.kept == 1016);
    collect_one(&storage, NULL);
    REQUIRE(shared.releases == 3 && separate.releases == 1);
    REQUIRE(storage.allocated == 0 && storage.kept == 0);
    arena_usage(&storage, &bytes, &capacity);
    REQUIRE(bytes == 0 && capacity == 48);
    void *reused = arena_alloc(&storage, 16);
    REQUIRE(arena_contains(&storage, reused));
    collect_one(&storage, NULL);
    arena_free(&storage);
    REQUIRE(shared.releases == 3 && separate.releases == 1);
    return true;
}

/* Attaching an alias adds metadata but never charges the external bytes twice.
 * A second arena accounts for its own retained ownership independently. */
TEST_CASE(arena_cleanup_accounts_unique_external_storage)
{
    for (int collectable = 0; collectable < 2; collectable++) {
        arena storage = { 0 }, other = { 0 };
        if (collectable) {
            arena_init_collectable(&storage);
            arena_init_collectable(&other);
        }
        retained_resource shared = { 0 };
        void *first = arena_alloc(&storage, 16);
        void *second = arena_alloc(&storage, 16);
        size_t before = arena_storage_bytes(&storage), bytes, capacity;
        arena_set_cleanup(&storage, first, release_retained_resource, &shared, 12345);
        size_t attached = arena_storage_bytes(&storage);
        REQUIRE(attached > before + 12345); /* Resource index is owned metadata. */
        arena_usage(&storage, &bytes, &capacity);
        REQUIRE(bytes == 32 + 12345 && capacity >= bytes);
        arena_set_cleanup(&storage, second, release_retained_resource, &shared, 12345);
        size_t aliased = arena_storage_bytes(&storage);
        REQUIRE(aliased >= attached && aliased - attached < 12345);
        arena_usage(&storage, &bytes, &capacity);
        REQUIRE(bytes == 32 + 12345);
        arena_set_cleanup(&other, arena_alloc(&other, 16), release_retained_resource, &shared, 12345);
        arena_usage(&other, &bytes, &capacity);
        REQUIRE(bytes == 16 + 12345);
        arena_reset(&storage);
        REQUIRE(shared.releases == 2);
        REQUIRE(arena_storage_bytes(&storage) < before + 12345);
        arena_usage(&other, &bytes, &capacity);
        REQUIRE(bytes == 16 + 12345);
        arena_free(&storage);
        arena_free(&other);
        REQUIRE(shared.releases == 3);
    }
    return true;
}

/* Repeated sweeps delete old resource keys and return peak metadata, even when
 * many resources share hash probe chains and one alias survives each sweep. */
TEST_CASE(arena_cleanup_resource_index_reuses_after_collection)
{
    enum
    {
        COUNT = 257
    };
    arena storage;
    arena_init_collectable(&storage);
    retained_resource resources[COUNT] = { 0 };
    size_t steady = 0;
    for (int round = 0; round < 4; round++) {
        void *keep = NULL;
        for (int i = 0; i < COUNT; i++) {
            void *allocation = arena_alloc(&storage, 16);
            arena_set_cleanup(
                &storage,
                allocation,
                release_retained_resource,
                &resources[i],
                (size_t)i + 1
            );
            if (i == COUNT / 2) {
                keep = allocation;
            }
        }
        collect_one(&storage, keep);
        for (int i = 0; i < COUNT; i++) {
            REQUIRE(resources[i].releases == round + (i != COUNT / 2));
        }
        REQUIRE(storage.retained_bytes == COUNT / 2 + 1);
        collect_one(&storage, NULL);
        REQUIRE(!storage.resources && !storage.resource_count && !storage.retained_bytes);
        size_t retained = arena_storage_bytes(&storage);
        if (round) {
            REQUIRE(retained == steady);
        }
        steady = retained;
    }
    arena_free(&storage);
    for (int i = 0; i < COUNT; i++) {
        REQUIRE(resources[i].releases == 4);
    }
    return true;
}

void test_arena()
{
    RUN_TEST_CASE(arena_growth_keeps_allocations_and_reset_reuses_blocks);
    RUN_TEST_CASE(zero_arena_allocates_on_first_use);
    RUN_TEST_CASE(collectable_arena_finds_values_by_address);
    RUN_TEST_CASE(arena_remembers_last_copy_until_reset_or_collection);
    RUN_TEST_CASE(arena_cleanup_reset_and_free_release_each_attachment_once);
    RUN_TEST_CASE(arena_cleanup_collection_keeps_shared_resource_until_last_alias);
    RUN_TEST_CASE(arena_cleanup_accounts_unique_external_storage);
    RUN_TEST_CASE(arena_cleanup_resource_index_reuses_after_collection);
}
