#ifndef CHIBI_ECS_EXECUTION_H
#define CHIBI_ECS_EXECUTION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Read-only storage metadata. Rows may be written under the node's declared
 * access contract. The world owns these arrays and refreshes them on component
 * registration and entity growth. Retain column or row pointers only within spans
 * proven to preserve storage, then reacquire them before the next span. */
typedef struct ecs_execution_column
{
    uint64_t id, layout;
    size_t stride;
    void *data;
    const uint64_t *occupancy;
    int count;
    /* XOR of occupied slots; identifies the sole row when count is one. */
    uint32_t singleton;
    /* Same borrowed lifetime as the registered component descriptor; resolving
     * it through the global catalog could select a newer schema. */
    const struct type_desc *type;
} ecs_execution_column;

typedef struct ecs_execution_view
{
    const uint32_t *generations;
    const unsigned char *occupied;
    uint32_t slot_count;
    const ecs_execution_column *columns;
    const int *lookup;
    int capacity;
    /* Retains reflected string writes for the world's lifetime. */
    const char *(*intern_string)(const struct ecs_execution_view *world, const char *text);
    uint64_t (*query_source)(
        const struct ecs_execution_view *,
        uint64_t owner,
        uint64_t component,
        uint64_t relationship,
        int traversal
    );
    uint64_t (*query_target)(
        const struct ecs_execution_view *,
        uint64_t owner,
        uint64_t relationship,
        int index
    );
} ecs_execution_view;

/* ecs_world begins with this view. Its ecs.world service pointer therefore
 * supplies this ABI without exposing command queues or private ECS state. */
static inline uint32_t ecs_execution_entity(const ecs_execution_view *world, uint64_t entity)
{
    uint32_t slot = (uint32_t)entity, generation = (uint32_t)(entity >> 32);
    return slot && slot < world->slot_count && generation &&
                   world->occupied[slot] && world->generations[slot] == generation
             ? slot
             : 0;
}
static inline const ecs_execution_column *ecs_execution_find_column(
    const ecs_execution_view *world,
    uint64_t id
)
{
    if (!world || !world->capacity) {
        return NULL;
    }
    uint64_t hash = id * UINT64_C(11400714819323198485);
    int mask = world->capacity - 1;
    int bucket = (int)((hash ^ (hash >> 32)) & (uint64_t)mask);
    for (;;) {
        int index = world->lookup[bucket];
        if (!index) {
            return NULL;
        }
        const ecs_execution_column *column = &world->columns[index - 1];
        if (column->id == id) {
            return column;
        }
        bucket = (bucket + 1) & mask;
    }
}
static inline const ecs_execution_column *ecs_execution_present(
    const ecs_execution_column *column,
    uint32_t slot
)
{
    return slot && column && ((column->occupancy[slot >> 6] >> (slot & 63)) & 1)
             ? column
             : NULL;
}
static inline const ecs_execution_column *ecs_execution_component(
    const ecs_execution_view *world,
    uint32_t slot,
    uint64_t id
)
{
    return slot ? ecs_execution_present(ecs_execution_find_column(world, id), slot)
                : NULL;
}
#endif
