#include "ecs/ecs.h"

#include "ecs/ecs_internal.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/schema.h"
#include "types/text.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
    ECS_COMMAND_CREATE,
    ECS_COMMAND_DESTROY,
    ECS_COMMAND_ADD,
    ECS_COMMAND_REMOVE,
    ECS_COMMAND_ADD_PAIR,
    ECS_COMMAND_REMOVE_PAIR,
    ECS_COMMAND_NAME,
    ECS_COMMAND_CHILD_INDEX,
} ecs_command_kind;

struct ecs_command
{
    uint64_t sort_key;
    uint64_t sequence;
    ecs_command_kind kind;
    ecs_entity entity;
    ecs_component_id component;
    ecs_entity target; /* Pair commands only. */
    void *value;
    size_t value_size;
};

typedef struct
{
    arena *storage;
    int references;
    size_t charged;
} cell_storage;
struct ecs_component_cell
{
    ecs_world *world;
    ecs_entity entity;
    ecs_component_id component;
    uint64_t layout;
    int references;
    int index;
    arena schemas;
    const pin_schema *schema;
    const type_desc *type;
    cell_storage *owners;
};
static void invalidate_cells(ecs_world *, ecs_entity, ecs_component_id);

/* Observers that keep recording commands or marking rows would never reach
 * the end of a sync point. */
enum
{
    APPLY_ROUND_LIMIT = 1024
};

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

uint64_t ecs_layout_hash(const type_desc *type)
{
    return type_layout_hash(type);
}

static int component_bucket(ecs_component_id id, int capacity)
{
    uint64_t hash = id * UINT64_C(11400714819323198485);
    return (int)((hash ^ (hash >> 32)) & (uint64_t)(capacity - 1));
}

static void component_index_insert(ecs_world *world, int index)
{
    int bucket = component_bucket(world->components[index].desc.id, world->component_lookup_capacity);
    while (world->component_lookup[bucket]) {
        bucket = (bucket + 1) & (world->component_lookup_capacity - 1);
    }
    world->component_lookup[bucket] = index + 1;
}

static void component_index_append(ecs_world *world)
{
    int count = acount(world->components);
    if (count * 2 <= world->component_lookup_capacity) {
        component_index_insert(world, count - 1);
        return;
    }
    world->component_lookup_capacity = world->component_lookup_capacity
                                         ? world->component_lookup_capacity * 2
                                         : 16;
    free(world->component_lookup);
    world->component_lookup = xcalloc(
        (size_t)world->component_lookup_capacity,
        sizeof *world->component_lookup
    );
    for (int i = 0; i < count; i++) {
        component_index_insert(world, i);
    }
}

ecs_component *find_component(ecs_world *world, ecs_component_id id)
{
    if (!world->component_lookup_capacity) {
        return NULL;
    }
    int bucket = component_bucket(id, world->component_lookup_capacity);
    while (world->component_lookup[bucket]) {
        ecs_component *component = &world->components[world->component_lookup[bucket] - 1];
        if (component->desc.id == id) {
            return component;
        }
        bucket = (bucket + 1) & (world->component_lookup_capacity - 1);
    }
    return NULL;
}

static const ecs_component *find_component_const(const ecs_world *world, ecs_component_id id)
{
    return find_component((ecs_world *)world, id);
}

static void update_execution_column(ecs_world *world, int index)
{
    const ecs_component *c = &world->components[index];
    world->execution_columns[index] = (ecs_execution_column){
        .id = c->desc.id,
        .layout = type_layout_hash(c->desc.type),
        .stride = c->desc.element_size,
        .data = c->desc.column_base,
        .occupancy = c->occupancy,
        .count = c->count,
        .singleton = c->singleton,
        .type = c->desc.type
    };
    world->execution.columns = world->execution_columns;
    world->execution.lookup = world->component_lookup;
    world->execution.capacity = world->component_lookup_capacity;
}

enum
{
    INITIAL_ENTITY_SLOTS = 256
};

/* Growth is a structural operation. Occupancy covers every entity slot, while
 * payload columns grow only when a component is actually added at that slot. */
static uint32_t grown_slots(uint32_t current, uint32_t required, size_t limit)
{
    if (required > limit) {
        return 0;
    }
    size_t count = current ? current : INITIAL_ENTITY_SLOTS;
    while (count < required) {
        count = count > limit / 2 ? limit : count * 2;
    }
    return (uint32_t)(count < limit ? count : limit);
}

void *ecs_grow_storage(void *data, size_t old_count, size_t count, size_t stride)
{
    assert(count <= SIZE_MAX / stride && old_count <= count);
    data = xrealloc(data, count * stride);
    memset((unsigned char *)data + old_count * stride, 0, (count - old_count) * stride);
    return data;
}

int ecs_world_reserve_entities(ecs_world *world, uint32_t capacity)
{
    assert(world);
    if (world->iteration_depth) {
        return ECS_ERR_ITERATING;
    }
    if (capacity < world->execution.slot_count) {
        return ECS_OK;
    }
    size_t limit = SIZE_MAX / sizeof *world->generations;
    if (limit > UINT32_MAX) {
        limit = UINT32_MAX;
    }
    if (capacity >= limit) {
        return ECS_ERR_CAPACITY;
    }
    uint32_t old = world->execution.slot_count;
    uint32_t count = grown_slots(old, capacity + 1, limit);
    world->generations = ecs_grow_storage(
        world->generations,
        old,
        count,
        sizeof *world->generations
    );
    world
        ->occupied = ecs_grow_storage(world->occupied, old, count, sizeof *world->occupied);
    world
        ->reserved = ecs_grow_storage(world->reserved, old, count, sizeof *world->reserved);
    world->pending_destroy = ecs_grow_storage(
        world->pending_destroy,
        old,
        count,
        sizeof *world->pending_destroy
    );
    for (uint32_t slot = old ? old : 1; slot < count; slot++) {
        world->generations[slot] = 1;
    }
    world->execution.generations = world->generations;
    world->execution.occupied = world->occupied;
    world->execution.slot_count = count;
    for (int i = 0; i < acount(world->components); i++) {
        ecs_component *c = &world->components[i];
        c->occupancy = ecs_grow_storage(
            c->occupancy,
            occupancy_words(old),
            occupancy_words(count),
            sizeof *c->occupancy
        );
        if (c->modified) {
            c->modified = ecs_grow_storage(
                c->modified,
                occupancy_words(old),
                occupancy_words(count),
                sizeof *c->modified
            );
        }
        if (c->versions) {
            c->versions = ecs_grow_storage(c->versions, old, count, sizeof *c->versions);
        }
        update_execution_column(world, i);
    }
    for (int i = 0; i < acount(world->relationships); i++) {
        relationship_grow(&world->relationships[i], old, count);
    }
    world->depth = ecs_grow_storage(world->depth, old, count, sizeof *world->depth);
    return ECS_OK;
}

uint32_t ecs_world_entity_capacity(const ecs_world *world)
{
    assert(world);
    return world->execution.slot_count - 1;
}

static int reserve_component(ecs_world *world, ecs_component *component, uint32_t slot)
{
    if (slot < component->slot_count) {
        return ECS_OK;
    }
    size_t limit = SIZE_MAX / component->desc.element_size;
    if (limit > world->execution.slot_count) {
        limit = world->execution.slot_count;
    }
    uint32_t count = grown_slots(component->slot_count, slot + 1, limit);
    if (!count) {
        return ECS_ERR_CAPACITY;
    }
    component->desc.column_base = ecs_grow_storage(
        component->desc.column_base,
        component->slot_count,
        count,
        component->desc.element_size
    );
    component->slot_count = count;
    update_execution_column(world, (int)(component - world->components));
    return ECS_OK;
}

static void occupancy_count(ecs_world *world, ecs_component *component, uint32_t slot, int change)
{
    query_revision_change(&component->query_revision);
    component->count += change;
    component->singleton ^= slot;
    ecs_execution_column *column = &world->execution_columns[component - world->components];
    column->count = component->count;
    column->singleton = component->singleton;
}
/* Drops the slots removed since the last compaction in one pass, so removing
 * many entities costs one scan instead of a shift each. */
void compact_dense(ecs_component *component)
{
    if (!component->dense_stale) {
        return;
    }
    int kept = 0;
    for (int i = 0; i < acount(component->dense); i++) {
        uint32_t slot = component->dense[i];
        if (component->occupancy[slot / 64u] & (UINT64_C(1) << (slot % 64u))) {
            component->dense[kept++] = slot;
        }
    }
    asetlen(component->dense, kept);
    component->dense_stale = false;
}

static void compact_all(ecs_world *world)
{
    for (int i = 0; i < acount(world->components); i++) {
        compact_dense(&world->components[i]);
    }
}

static void occupancy_add(ecs_world *world, ecs_component *component, uint32_t slot)
{
    uint64_t *word = &component->occupancy[slot / 64u];
    uint64_t bit = UINT64_C(1) << (slot % 64u);
    assert(!(*word & bit));
    *word |= bit;
    occupancy_count(world, component, slot, 1);
    if (component->desc.index != ECS_INDEX_DENSE) {
        return;
    }
    compact_dense(component);
    int count = acount(component->dense);
    asetlen(component->dense, count + 1);
    int at = count;
    while (at && component->dense[at - 1] > slot) {
        component->dense[at] = component->dense[at - 1];
        at--;
    }
    component->dense[at] = slot;
}

static void release_row(ecs_component *component, uint32_t slot)
{
    if (slot < (uint32_t)acount(component->owned_rows)) {
        arena_free(&component->owned_rows[slot]);
    }
}

static void occupancy_remove(ecs_world *world, ecs_component *component, uint32_t slot)
{
    invalidate_cells(
        world,
        (ecs_entity){ slot, world->generations[slot] },
        component->desc.id
    );
    uint64_t *word = &component->occupancy[slot / 64u];
    uint64_t bit = UINT64_C(1) << (slot % 64u);
    assert(*word & bit);
    *word &= ~bit;
    if (component->modified) {
        component->modified[slot / 64u] &= ~bit;
    }
    occupancy_count(world, component, slot, -1);
    release_row(component, slot);
    if (component->desc.index == ECS_INDEX_DENSE) {
        component->dense_stale = true;
    }
}

static void *component_address(const ecs_component *component, uint32_t slot)
{
    assert(slot < component->slot_count);
    unsigned char *base = (unsigned char *)component->desc.column_base;
    return base + (size_t)slot * component->desc.element_size;
}

static void advance_generation(ecs_world *world, uint32_t slot)
{
    if (slot < world->free_hint) {
        world->free_hint = slot;
    }
    world->generations[slot]++;
    if (!world->generations[slot]) {
        world->generations[slot] = 1;
    }
}

static void trim_high_water(ecs_world *world)
{
    while (world->high_water && !world->occupied[world->high_water] &&
           !world->reserved[world->high_water]) {
        world->high_water--;
    }
}

static void notify(ecs_world *world, ecs_event event, ecs_entity entity, ecs_component_id component)
{
    for (int i = 0; i < acount(world->observers); i++) {
        const ecs_observer_entry *observer = &world->observers[i];
        if (observer->event == event && observer->component == component) {
            observer->callback(world, event, entity, component, observer->user);
        }
    }
}

static bool observes(const ecs_component *component, ecs_event event)
{
    return component->observed & (1u << event);
}

static bool world_observes(const ecs_world *world, ecs_event event)
{
    return world->observed & (1u << event);
}

/* Writers of one component may run on several threads, so marks are atomic. */
static void mark_modified(ecs_component *component, uint32_t slot)
{
    if (component->versions) {
        uint64_t previous = __atomic_load_n(&component->versions[slot], __ATOMIC_RELAXED);
        while (previous < UINT64_MAX && !__atomic_compare_exchange_n(
                                            &component->versions[slot],
                                            &previous,
                                            previous + 1,
                                            false,
                                            __ATOMIC_RELAXED,
                                            __ATOMIC_RELAXED
                                        )) {
        }
    }
    if (component->modified) {
        __atomic_fetch_or(&component->modified[slot / 64u], UINT64_C(1) << (slot % 64u), __ATOMIC_RELAXED);
        __atomic_store_n(&component->dirty, true, __ATOMIC_RELAXED);
    }
}

/* Delivers ON_SET for every marked row and reports whether any was marked.
 * A row's own ON_SET observers may rewrite it without firing again; other
 * marks they make wait for the next round. */
static bool notify_modified(ecs_world *world)
{
    bool marked = false;
    for (int i = 0; i < acount(world->components); i++) {
        ecs_component *component = &world->components[i];
        if (!component->dirty) {
            continue;
        }
        component->dirty = false;
        for (uint32_t word = 0; word <= world->high_water / 64u; word++) {
            uint64_t bits = component->modified[word];
            component->modified[word] = 0;
            marked |= bits != 0;
            while (bits) {
                uint64_t bit = bits & -bits;
                bits &= bits - 1;
                uint32_t slot = word * 64u + (uint32_t)__builtin_ctzll(bit);
                assert(component_matches(component, slot));
                ecs_entity entity = { slot, world->generations[slot] };
                notify(world, ECS_ON_SET, entity, component->desc.id);
                component->modified[word] &= ~bit;
            }
        }
    }
    return marked;
}

/* Delivers a row's pending ON_SET before a command removes the row. */
static void flush_modified(ecs_world *world, ecs_component *component, ecs_entity entity)
{
    uint64_t bit = UINT64_C(1) << (entity.slot % 64u);
    if (!component->modified || !(component->modified[entity.slot / 64u] & bit)) {
        return;
    }
    component->modified[entity.slot / 64u] &= ~bit;
    notify(world, ECS_ON_SET, entity, component->desc.id);
}

static int command_rank(ecs_command_kind kind)
{
    switch (kind) {
        case ECS_COMMAND_CREATE: return 0;
        case ECS_COMMAND_DESTROY: return 1;
        case ECS_COMMAND_ADD: return 2;
        case ECS_COMMAND_REMOVE: return 3;
        case ECS_COMMAND_REMOVE_PAIR: return 4;
        case ECS_COMMAND_ADD_PAIR: return 5;
        case ECS_COMMAND_NAME: return 6;
        case ECS_COMMAND_CHILD_INDEX: return 7;
    }
    return 8;
}

static int compare_commands(const void *left_ptr, const void *right_ptr)
{
    const ecs_command *left = (const ecs_command *)left_ptr;
    const ecs_command *right = (const ecs_command *)right_ptr;
    if (left->sort_key != right->sort_key) {
        return left->sort_key < right->sort_key ? -1 : 1;
    }
    int left_rank = command_rank(left->kind);
    int right_rank = command_rank(right->kind);
    if (left_rank != right_rank) {
        return left_rank < right_rank ? -1 : 1;
    }
    if (left->entity.slot != right->entity.slot) {
        return left->entity.slot < right->entity.slot ? -1 : 1;
    }
    if (left->component != right->component) {
        return left->component < right->component ? -1 : 1;
    }
    if (left->entity.generation != right->entity.generation) {
        return left->entity.generation < right->entity.generation ? -1 : 1;
    }
    uint64_t left_target = ecs_entity_to_u64(left->target);
    uint64_t right_target = ecs_entity_to_u64(right->target);
    if (left_target != right_target) {
        return left_target < right_target ? -1 : 1;
    }
    if (left->value_size != right->value_size) {
        return left->value_size < right->value_size ? -1 : 1;
    }
    if (left->value_size) {
        int value_order = memcmp(left->value, right->value, left->value_size);
        if (value_order) {
            return value_order;
        }
    }
    return 0;
}

static void free_command(ecs_command *command)
{
    free(command->value);
    command->value = NULL;
}

static int apply_create(ecs_world *world, const ecs_command *command)
{
    uint32_t slot = command->entity.slot;
    if (!slot_valid(world, slot) || !world->reserved[slot] || world->occupied[slot] ||
        world->generations[slot] != command->entity.generation) {
        return ECS_ERR_ENTITY;
    }
    world->reserved[slot] = 0;
    world->occupied[slot] = 1;
    world->pending_destroy[slot] = DESTROY_NONE;
    world->live_count++;
    query_revision_change(&world->query_revision);
    return ECS_OK;
}

typedef struct
{
    ecs_entity entity;
    bool recorded; /* Its own destroy command is still pending. */
} cleanup_entry;

typedef struct
{
    cleanup_entry entry;
    int relationship; /* Next relationship whose sources to visit. */
    int source;
} cleanup_frame;

static void push_cleanup(ecs_world *world, cleanup_frame **stack, uint32_t slot)
{
    cleanup_frame frame = {
        .entry = { { slot, world->generations[slot] },
                   world->pending_destroy[slot] == DESTROY_RECORDED },
    };
    apush(*stack, frame);
    world->pending_destroy[slot] = DESTROY_RUNNING;
}

/* Returns the frame's next DELETE source not yet collected, or 0 when none
 * remain. Clears allowed when a PANIC relationship still has a source. */
static uint32_t next_cleanup_source(ecs_world *world, cleanup_frame *frame, bool *allowed)
{
    uint32_t slot = frame->entry.entity.slot;
    for (; frame->relationship < acount(world->relationships);
         frame->relationship++, frame->source = 0) {
        const ecs_relationship *relationship = &world->relationships[frame->relationship];
        const uint32_t *sources = relationship->sources[slot];
        ecs_cleanup policy = relationship->desc.on_delete_target;
        if (policy == ECS_CLEANUP_PANIC && acount(sources)) {
            *allowed = false;
            return 0;
        }
        while (policy == ECS_CLEANUP_DELETE && frame->source < acount(sources)) {
            uint32_t source = sources[frame->source++];
            if (world->pending_destroy[source] != DESTROY_RUNNING) {
                return source;
            }
        }
    }
    return 0;
}

/* Collects entity and every source its DELETE relationships would destroy in
 * postorder, so each source comes before its targets. Returns false when a
 * PANIC relationship still has a source, leaving every pending_destroy state
 * as it was. */
static bool collect_cleanup(ecs_world *world, ecs_entity entity, cleanup_entry **entries)
{
    cleanup_frame *stack = NULL;
    bool allowed = true;
    push_cleanup(world, &stack, entity.slot);
    while (allowed && acount(stack)) {
        uint32_t source = next_cleanup_source(world, &stack[acount(stack) - 1], &allowed);
        if (source) {
            push_cleanup(world, &stack, source);
            continue;
        }
        if (allowed) {
            apush(*entries, stack[acount(stack) - 1].entry);
            asetlen(stack, acount(stack) - 1);
        }
    }
    for (int i = 0; i < acount(stack); i++) {
        apush(*entries, stack[i].entry);
    }
    afree(stack);
    for (int i = 0; !allowed && i < acount(*entries); i++) {
        const cleanup_entry *entry = &(*entries)[i];
        world->pending_destroy[entry->entity.slot] = entry->recorded
                                                       ? DESTROY_RECORDED
                                                       : DESTROY_NONE;
    }
    return allowed;
}

/* Notifies and removes one entity whose cleanup was collected. */
static void retire_entity(ecs_world *world, ecs_entity entity)
{
    for (int i = 0; world_observes(world, ECS_ON_SET) && i < acount(world->components);
         i++) {
        ecs_component *component = &world->components[i];
        if (component_matches(component, entity.slot)) {
            flush_modified(world, component, entity);
        }
    }
    if (world_observes(world, ECS_ON_DESTROY)) {
        notify(world, ECS_ON_DESTROY, entity, 0);
    }
    for (int i = 0;
         world_observes(world, ECS_ON_REMOVE) && i < acount(world->components);
         i++) {
        ecs_component *component = &world->components[i];
        if (observes(component, ECS_ON_REMOVE) &&
            component_matches(component, entity.slot)) {
            notify(world, ECS_ON_REMOVE, entity, component->desc.id);
        }
    }
    relationship_detach(world, entity.slot);
    for (int i = 0; i < acount(world->components); i++) {
        ecs_component *component = &world->components[i];
        if (component_matches(component, entity.slot)) {
            occupancy_remove(world, component, entity.slot);
        }
    }
    world->occupied[entity.slot] = 0;
    world->depth[entity.slot] = 0;
    world->pending_destroy[entity.slot] = DESTROY_APPLIED;
    world->live_count--;
    query_revision_change(&world->query_revision);
    advance_generation(world, entity.slot);
    trim_high_water(world);
}

/* Applies OnDeleteTarget cleanup before the entity goes: DELETE sources are
 * destroyed before their targets, and a PANIC source fails it before any
 * change. */
static int destroy_entity(ecs_world *world, ecs_entity entity)
{
    cleanup_entry *entries = NULL;
    if (!collect_cleanup(world, entity, &entries)) {
        afree(entries);
        return ECS_ERR_COMMAND;
    }
    for (int i = 0; i < acount(entries); i++) {
        retire_entity(world, entries[i].entity);
    }
    afree(entries);
    return ECS_OK;
}

/* A sync point that already destroyed an entity skips its later commands. */
static bool destroyed_earlier(const ecs_world *world, ecs_entity entity)
{
    return slot_valid(world, entity.slot) && !world->occupied[entity.slot] &&
           world->pending_destroy[entity.slot] == DESTROY_APPLIED;
}

static int apply_destroy(ecs_world *world, const ecs_command *command)
{
    if (entity_matches(world, command->entity)) {
        return destroy_entity(world, command->entity);
    }
    return destroyed_earlier(world, command->entity) ? ECS_OK : ECS_ERR_ENTITY;
}

/* Adds the row, then any missing components it implies, before ON_ADD. */
static int add_component(ecs_world *world, ecs_component *component, ecs_entity entity, const void *value)
{
    int result = reserve_component(world, component, entity.slot);
    if (result) {
        return result;
    }
    void *address = component_address(component, entity.slot);
    memset(address, 0, component->desc.element_size);
    if (value) {
        memcpy(address, value, component->desc.element_size);
    }
    type_validate_ranges(component->desc.type, address);
    occupancy_add(world, component, entity.slot);
    if (component->versions && !value) {
        query_revision_change(&component->versions[entity.slot]);
    }
    for (int i = 0; result == ECS_OK && i < acount(component->with); i++) {
        ecs_component *implied = find_component(world, component->with[i]);
        if (!component_matches(implied, entity.slot)) {
            result = add_component(world, implied, entity, NULL);
        }
    }
    if (observes(component, ECS_ON_ADD)) {
        notify(world, ECS_ON_ADD, entity, component->desc.id);
    }
    if (value) {
        mark_modified(component, entity.slot);
    }
    return result;
}

/* An add of a component already present, for example implied by With
 * earlier in the sync point, writes its value instead. */
static int apply_add(ecs_world *world, const ecs_command *command)
{
    ecs_component *component = find_component(world, command->component);
    if (destroyed_earlier(world, command->entity)) {
        return ECS_OK;
    }
    if (!component || !entity_matches(world, command->entity)) {
        return ECS_ERR_ENTITY;
    }
    if (!component_matches(component, command->entity.slot)) {
        return add_component(world, component, command->entity, command->value);
    }
    if (command->value) {
        release_row(component, command->entity.slot);
        memcpy(
            component_address(component, command->entity.slot),
            command->value,
            component->desc.element_size
        );
        type_validate_ranges(
            component->desc.type,
            component_address(component, command->entity.slot)
        );
        mark_modified(component, command->entity.slot);
    }
    return ECS_OK;
}

/* Removes the row, then the components derived from it, which only their
 * source keeps meaningful. */
static void remove_component(ecs_world *world, ecs_component *component, ecs_entity entity)
{
    flush_modified(world, component, entity);
    if (observes(component, ECS_ON_REMOVE)) {
        notify(world, ECS_ON_REMOVE, entity, component->desc.id);
    }
    occupancy_remove(world, component, entity.slot);
    for (int i = 0; i < acount(component->with); i++) {
        ecs_component *implied = find_component(world, component->with[i]);
        if (implied->desc.derived_from == component->desc.id &&
            component_matches(implied, entity.slot)) {
            remove_component(world, implied, entity);
        }
    }
}

static int apply_remove(ecs_world *world, const ecs_command *command)
{
    ecs_component *component = find_component(world, command->component);
    if (destroyed_earlier(world, command->entity)) {
        return ECS_OK;
    }
    if (!component || !entity_matches(world, command->entity)) {
        return ECS_ERR_ENTITY;
    }
    if (!ecs_component_has(world, command->entity, command->component)) {
        return ECS_ERR_COMPONENT;
    }
    remove_component(world, component, command->entity);
    return ECS_OK;
}

static bool closes_cycle(
    const ecs_world *world,
    const ecs_relationship *relationship,
    ecs_entity entity,
    ecs_entity target
)
{
    return (relationship->desc.traits & ECS_REL_ACYCLIC) &&
           relationship_reaches(world, relationship, target, entity.slot);
}

static int apply_add_pair(ecs_world *world, const ecs_command *command)
{
    ecs_relationship *relationship = world_relationship(world, command->component);
    if (destroyed_earlier(world, command->entity) ||
        destroyed_earlier(world, command->target)) {
        return ECS_OK;
    }
    if (!entity_matches(world, command->entity) ||
        !entity_matches(world, command->target)) {
        return ECS_ERR_ENTITY;
    }
    if (ecs_has_pair(world, command->entity, command->component, command->target)) {
        return ECS_ERR_DUPLICATE;
    }
    if (closes_cycle(world, relationship, command->entity, command->target)) {
        return ECS_ERR_CYCLE;
    }
    bool child_of = command->component == ECS_CHILD_OF;
    const char *name = child_of ? hierarchy_name(world, command->entity.slot) : NULL;
    if (name &&
        hierarchy_name_taken(world, command->target.slot, name, command->entity.slot)) {
        return ECS_ERR_DUPLICATE;
    }
    relationship_link(relationship, command->entity.slot, command->target);
    if (child_of) {
        hierarchy_update_depth(world, command->entity.slot);
    }
    return ECS_OK;
}

static int apply_remove_pair(ecs_world *world, const ecs_command *command)
{
    if (destroyed_earlier(world, command->entity) ||
        destroyed_earlier(world, command->target)) {
        return ECS_OK;
    }
    if (!entity_matches(world, command->entity)) {
        return ECS_ERR_ENTITY;
    }
    if (!ecs_has_pair(world, command->entity, command->component, command->target)) {
        return ECS_ERR_COMPONENT;
    }
    bool child_of = command->component == ECS_CHILD_OF;
    const char *name = child_of ? hierarchy_name(world, command->entity.slot) : NULL;
    if (name && hierarchy_name_taken(world, 0, name, command->entity.slot)) {
        return ECS_ERR_DUPLICATE;
    }
    relationship_unlink(
        world_relationship(world, command->component),
        command->entity.slot,
        command->target
    );
    if (child_of) {
        hierarchy_update_depth(world, command->entity.slot);
    }
    return ECS_OK;
}

static int write_value(ecs_world *, ecs_component *, ecs_entity, size_t offset, pin_value declared, pin_value value);
static int write_value_resolved(ecs_world *, ecs_component *, ecs_entity, size_t, pin_value, pin_value, type_resolver);

static const pin_value NAME_VALUE = { .type = PIN_NAME };

static int apply_name(ecs_world *world, const ecs_command *command)
{
    if (destroyed_earlier(world, command->entity)) {
        return ECS_OK;
    }
    if (!entity_matches(world, command->entity)) {
        return ECS_ERR_ENTITY;
    }
    const char *name = command->value;
    uint32_t slot = command->entity.slot;
    const char *previous = hierarchy_name(world, slot);
    if (previous && !strcmp(previous, name)) {
        return ECS_OK;
    }
    if (hierarchy_name_taken(world, hierarchy_parent(world, slot), name, slot)) {
        return ECS_ERR_DUPLICATE;
    }
    ecs_component *names = &world->components[NAME_COMPONENT];
    /* ON_ADD sees the command's copy until write_value moves it to the row. */
    ecs_name staged = { name };
    int result = component_matches(names, slot)
                   ? ECS_OK
                   : add_component(world, names, command->entity, &staged);
    pin_value value = NAME_VALUE;
    value.str = name;
    if (result == ECS_OK) {
        result = write_value(world, names, command->entity, 0, NAME_VALUE, value);
    }
    if (result == ECS_OK) {
        mark_modified(names, slot);
    }
    return result;
}

static int apply_child_index(ecs_world *world, const ecs_command *command)
{
    if (destroyed_earlier(world, command->entity)) {
        return ECS_OK;
    }
    if (!entity_matches(world, command->entity)) {
        return ECS_ERR_ENTITY;
    }
    if (!hierarchy_parent(world, command->entity.slot)) {
        return ECS_OK; /* Unparented earlier in the sync point. */
    }
    hierarchy_move_child(world, command->entity.slot, *(const int *)command->value);
    return ECS_OK;
}

static const char *execution_intern(const ecs_execution_view *world, const char *text)
{
    return ecs_world_intern_string((ecs_world *)world, text);
}

static const type_field NAME_FIELDS[] = {
    { .name = "value",
      .type = PIN_NAME,
      .offset = 0,
      .size = sizeof(const char *),
      .type_name = "const char *" },
};
static const type_desc NAME_TYPE = {
    .name = "ecs.Name",
    .size = sizeof(ecs_name),
    .fields = NAME_FIELDS,
    .field_count = 1,
};
/* Indexed by NAME_COMPONENT and DISABLED_COMPONENT. */
static const ecs_component_desc BUILTIN_COMPONENTS[] = {
    { .id = ECS_NAME,
      .key = "ecs.Name",
      .name = "Name",
      .element_size = sizeof(ecs_name),
      .type = &NAME_TYPE },
    { .id = ECS_DISABLED,
      .key = "ecs.Disabled",
      .name = "Disabled",
      .element_size = 1,
      .type = &TYPE_MARKER },
};
static const ecs_relationship_desc CHILD_OF_DESC = {
    .id = ECS_CHILD_OF,
    .key = "ecs.ChildOf",
    .name = "ChildOf",
    .traits = ECS_REL_EXCLUSIVE | ECS_REL_TRAVERSABLE,
    .on_delete_target = ECS_CLEANUP_DELETE,
};

static uint64_t execution_query_source(
    const ecs_execution_view *view,
    uint64_t owner,
    uint64_t component,
    uint64_t relationship,
    int traversal
)
{
    return ecs_entity_to_u64(ecs_query_source(
        (const ecs_world *)view,
        ecs_entity_from_u64(owner),
        component,
        relationship,
        (ecs_trav)traversal
    ));
}
static uint64_t execution_query_target(
    const ecs_execution_view *view,
    uint64_t owner,
    uint64_t relationship,
    int index
)
{
    const ecs_world *world = (const ecs_world *)view;
    ecs_entity entity = ecs_entity_from_u64(owner);
    return index >= 0 && index < ecs_pair_target_count(world, entity, relationship)
             ? ecs_entity_to_u64(ecs_pair_target(world, entity, relationship, index))
             : 0;
}

ecs_world *ecs_world_new()
{
    ecs_world *world = (ecs_world *)xcalloc(1, sizeof *world);
    world->free_hint = 1;
    world->execution.intern_string = execution_intern;
    world->execution.query_source = execution_query_source;
    world->execution.query_target = execution_query_target;
    int result = ecs_world_reserve_entities(world, INITIAL_ENTITY_SLOTS - 1);
    assert(!result);
    for (int i = 0; i < (int)(sizeof BUILTIN_COMPONENTS / sizeof *BUILTIN_COMPONENTS);
         i++) {
        result |= ecs_world_register_component(world, &BUILTIN_COMPONENTS[i], NULL);
    }
    result |= ecs_world_register_relationship(world, &CHILD_OF_DESC, NULL);
    assert(!result);
    (void)result;
    return world;
}

const ecs_execution_view *ecs_world_execution(const ecs_world *world)
{
    return world ? &world->execution : NULL;
}

/* Descriptors own their identity strings; reflected type layouts stay borrowed. */
static ecs_component_desc component_desc_copy(const ecs_component_desc *source)
{
    ecs_component_desc copy = *source;
    copy.name = xstrdup(source->name);
    copy.key = source->key ? xstrdup(source->key) : NULL;
    return copy;
}

static void component_desc_free_names(ecs_component_desc *desc)
{
    free((char *)desc->name);
    free((char *)desc->key);
}

void ecs_world_free(ecs_world *world)
{
    if (!world) {
        return;
    }
    ecs_world_clear_commands(world);
    while (acount(world->cells)) {
        ecs_component_cell *cell = world->cells[acount(world->cells) - 1];
        invalidate_cells(world, cell->entity, cell->component);
    }
    afree(world->cells);
    for (int i = 0; i < acount(world->components); i++) {
        ecs_component *component = &world->components[i];
        for (int slot = 0; slot < acount(component->owned_rows); slot++) {
            arena_free(&component->owned_rows[slot]);
        }
        afree(component->owned_rows);
        afree(component->cells);
        component_desc_free_names(&world->components[i].desc);
        free(world->components[i].desc.column_base);
        free(world->components[i].occupancy);
        afree(world->components[i].dense);
        free(world->components[i].modified);
        free(world->components[i].versions);
        afree(world->components[i].with);
    }
    afree(world->components);
    for (int i = 0; i < acount(world->relationships); i++) {
        relationship_free(&world->relationships[i], world->execution.slot_count);
    }
    afree(world->relationships);
    free(world->depth);
    afree(world->observers);
    afree(world->execution_columns);
    free(world->component_lookup);
    afree(world->commands);
    for (int i = 0; i < acount(world->field_strings); i++) {
        free(world->field_strings[i]);
    }
    afree(world->field_strings);
    free(world->generations);
    free(world->occupied);
    free(world->reserved);
    free(world->pending_destroy);
    free(world);
}

bool world_key_taken(const ecs_world *world, const char *key)
{
    for (int i = 0; i < acount(world->components); i++) {
        const char *taken = world->components[i].desc.key;
        if (taken && !strcmp(taken, key)) {
            return true;
        }
    }
    for (int i = 0; i < acount(world->relationships); i++) {
        const char *taken = world->relationships[i].desc.key;
        if (taken && !strcmp(taken, key)) {
            return true;
        }
    }
    return false;
}

/* A derived component comes with its source, whichever registers second,
 * including on the entities that already have the source. */
static void link_derived(ecs_world *world, ecs_component_id id)
{
    for (int i = 0; i < acount(world->components); i++) {
        const ecs_component_desc *desc = &world->components[i].desc;
        if (!desc->derived_from || (desc->id != id && desc->derived_from != id)) {
            continue;
        }
        ecs_component *source = find_component(world, desc->derived_from);
        if (!source) {
            continue;
        }
        int result = ecs_world_add_with(world, desc->derived_from, desc->id);
        ecs_component *derived = &world->components[i];
        for (uint32_t slot = 1; result == ECS_OK && slot <= world->high_water; slot++) {
            if (world->occupied[slot] && component_matches(source, slot) &&
                !component_matches(derived, slot)) {
                ecs_entity entity = { slot, ecs_world_generation(world, slot) };
                result = add_component(world, derived, entity, NULL);
            }
        }
        assert(result == ECS_OK);
        (void)result;
    }
}

/* Whether an automatic id must skip this one: it is registered, or it is a
 * derived component's source still to come. */
static bool id_taken(ecs_world *world, const ecs_component_desc *desc, ecs_component_id id)
{
    if (id == desc->derived_from || find_component(world, id) ||
        world_relationship(world, id)) {
        return true;
    }
    for (int i = 0; i < acount(world->components); i++) {
        if (world->components[i].desc.derived_from == id) {
            return true;
        }
    }
    return false;
}

/* Follow precisely the nested layouts that owning range validation visits.
 * Component rows have no owner for task handles or tagged union payloads. */
static bool component_fields_valid(const type_desc *type, const type_desc **parents, int depth)
{
    if (depth >= TYPE_MAX_NESTING) {
        return false;
    }
    for (int i = 0; i < depth; i++) {
        if (parents[i] == type) {
            return false;
        }
    }
    parents[depth] = type;
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (!type_field_range_valid(field) || field->type == PIN_TASK ||
            field->type == PIN_UNION) {
            return false;
        }
        if (field->type == PIN_STRUCT) {
            const type_desc *nested = type_catalog_find(field->type_name);
            if (nested && nested->size == field->size &&
                !component_fields_valid(nested, parents, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}

int ecs_world_register_component(ecs_world *world, const ecs_component_desc *desc, ecs_component_id *out_id)
{
    if (!world || !desc || !desc->name || !desc->name[0] || !desc->element_size ||
        !desc->type || desc->type->size != desc->element_size ||
        (desc->derived_from && desc->derived_from == desc->id)) {
        return ECS_ERR_ARGUMENT;
    }
    if (world->iteration_depth || world->applying) {
        return ECS_ERR_ITERATING;
    }
    const type_desc *parents[TYPE_MAX_NESTING];
    if (!component_fields_valid(desc->type, parents, 0)) {
        return ECS_ERR_ARGUMENT;
    }
    if (desc->id &&
        (find_component(world, desc->id) || world_relationship(world, desc->id))) {
        return ECS_ERR_DUPLICATE;
    }
    if (desc->key && world_key_taken(world, desc->key)) {
        return ECS_ERR_DUPLICATE;
    }
    ecs_component_id id = desc->id;
    if (!id) {
        id = 1;
        while (id_taken(world, desc, id)) {
            id++;
            if (!id) {
                return ECS_ERR_CAPACITY;
            }
        }
    }
    ecs_component component = { 0 };
    component.desc = component_desc_copy(desc);
    component.desc.id = id;
    component.desc.layout_hash = desc->layout_hash ? desc->layout_hash
                                                   : ecs_layout_hash(desc->type);
    component.slot_count = 1;
    component.desc.column_base = xcalloc(component.slot_count, desc->element_size);
    component.occupancy = xcalloc(
        occupancy_words(world->execution.slot_count),
        sizeof *component.occupancy
    );
    apush(world->components, component);
    component_index_append(world);
    apush(world->execution_columns, ((ecs_execution_column){ 0 }));
    update_execution_column(world, acount(world->components) - 1);
    link_derived(world, id);
    if (out_id) {
        *out_id = id;
    }
    return ECS_OK;
}

int ecs_world_add_with(ecs_world *world, ecs_component_id component_id, ecs_component_id implied)
{
    assert(world);
    if (world->iteration_depth || world->applying) {
        return ECS_ERR_ITERATING;
    }
    ecs_component *component = find_component(world, component_id);
    if (!component || !find_component(world, implied) || implied == component_id) {
        return ECS_ERR_COMPONENT;
    }
    if (implied == ECS_NAME) {
        return ECS_ERR_ARGUMENT; /* Names are set only through record_name. */
    }
    for (int i = 0; i < acount(component->with); i++) {
        if (component->with[i] == implied) {
            return ECS_ERR_DUPLICATE;
        }
    }
    apush(component->with, implied);
    return ECS_OK;
}

int ecs_world_component_count(const ecs_world *world)
{
    return world ? acount(world->components) : 0;
}

const ecs_component_desc *ecs_world_component_at(const ecs_world *world, int index)
{
    return world && index >= 0 && index < acount(world->components)
             ? &world->components[index].desc
             : NULL;
}

const ecs_component_desc *ecs_world_component(const ecs_world *world, ecs_component_id id)
{
    const ecs_component *component = world ? find_component_const(world, id) : NULL;
    return component ? &component->desc : NULL;
}

/* Commands recorded by observers apply in a later round, after a destroy in
 * progress has finished. */
static bool dying(const ecs_world *world, ecs_entity entity)
{
    return world->applying && world->pending_destroy[entity.slot];
}

ecs_entity ecs_world_record_create(ecs_world *world, uint64_t sort_key)
{
    if (!world || world->iteration_depth) {
        return (ecs_entity){ 0, 0 };
    }
    for (uint32_t slot = world->free_hint;; slot++) {
        if (slot >= world->execution.slot_count &&
            ecs_world_reserve_entities(world, slot)) {
            return (ecs_entity){ 0, 0 };
        }
        if (world->occupied[slot] || world->reserved[slot]) {
            continue;
        }
        world->reserved[slot] = 1;
        world->free_hint = slot + 1;
        if (slot > world->high_water) {
            world->high_water = slot;
        }
        ecs_entity entity = { slot, world->generations[slot] };
        apush(
            world->commands,
            (ecs_command){
                .sort_key = sort_key,
                .sequence = world->next_sequence++,
                .kind = ECS_COMMAND_CREATE,
                .entity = entity,
            }
        );
        return entity;
    }
}

int ecs_world_record_destroy(ecs_world *world, ecs_entity entity, uint64_t sort_key)
{
    if (!world || world->iteration_depth || !entity_matches(world, entity) ||
        world->pending_destroy[entity.slot]) {
        return ECS_ERR_ENTITY;
    }
    world->pending_destroy[entity.slot] = DESTROY_RECORDED;
    apush(
        world->commands,
        (ecs_command){
            .sort_key = sort_key,
            .sequence = world->next_sequence++,
            .kind = ECS_COMMAND_DESTROY,
            .entity = entity,
        }
    );
    return ECS_OK;
}

int ecs_world_record_add(
    ecs_world *world,
    ecs_entity entity,
    ecs_component_id component_id,
    const void *initial_value,
    uint64_t sort_key
)
{
    ecs_component *component = world ? find_component(world, component_id) : NULL;
    if (!world || world->iteration_depth || !component ||
        !entity_matches(world, entity) || dying(world, entity)) {
        return ECS_ERR_ENTITY;
    }
    if (component_id == ECS_NAME) {
        return ECS_ERR_ARGUMENT;
    }
    if (ecs_component_has(world, entity, component_id)) {
        return ECS_ERR_DUPLICATE;
    }
    ecs_command command = {
        .sort_key = sort_key,
        .sequence = world->next_sequence++,
        .kind = ECS_COMMAND_ADD,
        .entity = entity,
        .component = component_id,
    };
    if (initial_value) {
        command.value = xmalloc(component->desc.element_size);
        command.value_size = component->desc.element_size;
        memcpy(command.value, initial_value, command.value_size);
    }
    apush(world->commands, command);
    return ECS_OK;
}

int ecs_world_record_remove(ecs_world *world, ecs_entity entity, ecs_component_id component_id, uint64_t sort_key)
{
    ecs_component *component = world ? find_component(world, component_id) : NULL;
    if (!world || world->iteration_depth || !component ||
        !entity_matches(world, entity) || dying(world, entity)) {
        return ECS_ERR_ENTITY;
    }
    if (!ecs_component_has(world, entity, component_id)) {
        return ECS_ERR_COMPONENT;
    }
    apush(
        world->commands,
        (ecs_command){
            .sort_key = sort_key,
            .sequence = world->next_sequence++,
            .kind = ECS_COMMAND_REMOVE,
            .entity = entity,
            .component = component_id,
        }
    );
    return ECS_OK;
}

static int apply_command(ecs_world *world, const ecs_command *command)
{
    switch (command->kind) {
        case ECS_COMMAND_CREATE: return apply_create(world, command);
        case ECS_COMMAND_DESTROY: return apply_destroy(world, command);
        case ECS_COMMAND_ADD: return apply_add(world, command);
        case ECS_COMMAND_REMOVE: return apply_remove(world, command);
        case ECS_COMMAND_ADD_PAIR: return apply_add_pair(world, command);
        case ECS_COMMAND_REMOVE_PAIR: return apply_remove_pair(world, command);
        case ECS_COMMAND_NAME: return apply_name(world, command);
        case ECS_COMMAND_CHILD_INDEX: return apply_child_index(world, command);
    }
    return ECS_ERR_COMMAND;
}

/* Cancels unapplied reservations and pending destroys, then frees values. */
static void release_commands(ecs_world *world, ecs_command *commands)
{
    for (int i = 0; i < acount(commands); i++) {
        ecs_command *command = &commands[i];
        if (command->kind == ECS_COMMAND_CREATE &&
            slot_valid(world, command->entity.slot) &&
            world->reserved[command->entity.slot] &&
            world->generations[command->entity.slot] == command->entity.generation) {
            world->reserved[command->entity.slot] = 0;
            advance_generation(world, command->entity.slot);
        }
        if (command->kind == ECS_COMMAND_DESTROY &&
            slot_valid(world, command->entity.slot)) {
            world->pending_destroy[command->entity.slot] = 0;
        }
        free_command(command);
    }
}

/* Observers record into a fresh buffer, applied by the next round. */
static int apply_batch(ecs_world *world)
{
    ecs_command *batch = world->commands;
    world->commands = NULL;
    asort(batch, compare_commands);
    int result = ECS_OK;
    for (int i = 0; result == ECS_OK && i < acount(batch); i++) {
        result = apply_command(world, &batch[i]);
    }
    release_commands(world, batch);
    afree(batch);
    return result;
}

/* A target is live, or reserved by a create that applies earlier. */
static bool target_valid(const ecs_world *world, ecs_entity target, uint64_t sort_key)
{
    if (!slot_valid(world, target.slot) || !target.generation ||
        world->generations[target.slot] != target.generation) {
        return false;
    }
    if (world->occupied[target.slot]) {
        return !dying(world, target);
    }
    for (int i = 0; world->reserved[target.slot] && i < acount(world->commands); i++) {
        const ecs_command *command = &world->commands[i];
        if (command->kind == ECS_COMMAND_CREATE &&
            command->entity.slot == target.slot) {
            return command->sort_key <= sort_key;
        }
    }
    return false;
}

static int record_pair(
    ecs_world *world,
    ecs_command_kind kind,
    ecs_entity entity,
    ecs_component_id id,
    ecs_entity target,
    uint64_t sort_key
)
{
    assert(world);
    const ecs_relationship *relationship = world_relationship(world, id);
    if (world->iteration_depth || !relationship || !entity_matches(world, entity) ||
        dying(world, entity) || !target_valid(world, target, sort_key)) {
        return ECS_ERR_ENTITY;
    }
    bool present = ecs_has_pair(world, entity, id, target);
    if (kind == ECS_COMMAND_ADD_PAIR && present) {
        return ECS_ERR_DUPLICATE;
    }
    if (kind == ECS_COMMAND_REMOVE_PAIR && !present) {
        return ECS_ERR_COMPONENT;
    }
    if (kind == ECS_COMMAND_ADD_PAIR && world->occupied[target.slot] &&
        closes_cycle(world, relationship, entity, target)) {
        return ECS_ERR_CYCLE;
    }
    apush(
        world->commands,
        (ecs_command){
            .sort_key = sort_key,
            .sequence = world->next_sequence++,
            .kind = kind,
            .entity = entity,
            .component = id,
            .target = target,
        }
    );
    return ECS_OK;
}

int ecs_world_record_add_pair(
    ecs_world *world,
    ecs_entity entity,
    ecs_component_id relationship,
    ecs_entity target,
    uint64_t sort_key
)
{
    return record_pair(world, ECS_COMMAND_ADD_PAIR, entity, relationship, target, sort_key);
}

int ecs_world_record_remove_pair(
    ecs_world *world,
    ecs_entity entity,
    ecs_component_id relationship,
    ecs_entity target,
    uint64_t sort_key
)
{
    return record_pair(world, ECS_COMMAND_REMOVE_PAIR, entity, relationship, target, sort_key);
}

static bool name_valid(const char *name)
{
    return name && name[0] && !strchr(name, '.');
}

int ecs_world_record_name(ecs_world *world, ecs_entity entity, const char *name, uint64_t sort_key)
{
    assert(world);
    if (!name_valid(name)) {
        return ECS_ERR_ARGUMENT;
    }
    if (world->iteration_depth || !entity_matches(world, entity) ||
        dying(world, entity)) {
        return ECS_ERR_ENTITY;
    }
    size_t size = strlen(name) + 1;
    ecs_command command = {
        .sort_key = sort_key,
        .sequence = world->next_sequence++,
        .kind = ECS_COMMAND_NAME,
        .entity = entity,
        .value = xmalloc(size),
        .value_size = size,
    };
    memcpy(command.value, name, size);
    apush(world->commands, command);
    return ECS_OK;
}

int ecs_world_record_child_index(ecs_world *world, ecs_entity child, int index, uint64_t sort_key)
{
    assert(world);
    if (index < 0) {
        return ECS_ERR_ARGUMENT;
    }
    if (world->iteration_depth || !entity_matches(world, child) || dying(world, child)) {
        return ECS_ERR_ENTITY;
    }
    if (!hierarchy_parent(world, child.slot)) {
        return ECS_ERR_COMPONENT;
    }
    ecs_command command = {
        .sort_key = sort_key,
        .sequence = world->next_sequence++,
        .kind = ECS_COMMAND_CHILD_INDEX,
        .entity = child,
        .value = xmalloc(sizeof index),
        .value_size = sizeof index,
    };
    memcpy(command.value, &index, sizeof index);
    apush(world->commands, command);
    return ECS_OK;
}

int ecs_world_apply_commands(ecs_world *world)
{
    if (!world) {
        return ECS_ERR_ARGUMENT;
    }
    if (world->iteration_depth || world->applying) {
        return ECS_ERR_ITERATING;
    }
    world->applying = true;
    int result = ECS_OK;
    int rounds = 0;
    while (result == ECS_OK && (notify_modified(world) || acount(world->commands))) {
        rounds++;
        assert(rounds <= APPLY_ROUND_LIMIT);
        if (rounds > APPLY_ROUND_LIMIT) {
            result = ECS_ERR_COMMAND;
        } else if (acount(world->commands)) {
            result = apply_batch(world);
        }
    }
    world->applying = false;
    compact_all(world);
    ecs_world_clear_commands(world);
    return result;
}

void ecs_world_clear_commands(ecs_world *world)
{
    if (!world) {
        return;
    }
    release_commands(world, world->commands);
    aclear(world->commands);
    trim_high_water(world);
}

int ecs_world_command_count(const ecs_world *world)
{
    return world ? acount(world->commands) : 0;
}

ecs_entity ecs_world_create_entity(ecs_world *world)
{
    if (world && world->applying) {
        return (ecs_entity){ 0, 0 };
    }
    ecs_entity entity = ecs_world_record_create(world, 0);
    if (!entity.slot || ecs_world_apply_commands(world) != ECS_OK) {
        return (ecs_entity){ 0, 0 };
    }
    return entity;
}

int ecs_world_prepare_owned_destruction(ecs_world *world, const ecs_entity *entities, int count)
{
    if (!world || count < 0 || (count && !entities)) {
        return ECS_ERR_ARGUMENT;
    }
    if (!count) {
        return ECS_OK;
    }
    if (world->applying || world->iteration_depth || acount(world->commands)) {
        return ECS_ERR_ITERATING;
    }
    unsigned char *owned = xcalloc(world->execution.slot_count, 1);
    unsigned char *visited = xcalloc(world->execution.slot_count, 1);
    uint32_t *pending = NULL;
    for (int i = 0; i < count; i++) {
        if (entity_matches(world, entities[i]) && !owned[entities[i].slot]) {
            owned[entities[i].slot] = 1;
            visited[entities[i].slot] = 1;
            apush(pending, entities[i].slot);
        }
    }
    int result = ECS_OK;
    /* Inspect DELETE cascades too, before unlinking anything in the owned set. */
    for (int i = 0; result == ECS_OK && i < acount(pending); i++) {
        uint32_t target = pending[i];
        for (int j = 0; result == ECS_OK && j < acount(world->relationships); j++) {
            ecs_relationship *relationship = &world->relationships[j];
            const uint32_t *sources = relationship->sources[target];
            for (int k = 0; k < acount(sources); k++) {
                uint32_t source = sources[k];
                if (relationship->desc.on_delete_target == ECS_CLEANUP_PANIC &&
                    !(owned[source] && owned[target])) {
                    result = ECS_ERR_COMMAND;
                    break;
                }
                if (relationship->desc.on_delete_target == ECS_CLEANUP_DELETE &&
                    !visited[source]) {
                    visited[source] = 1;
                    apush(pending, source);
                }
            }
        }
    }
    for (int i = 0; result == ECS_OK && i < count; i++) {
        if (!entity_matches(world, entities[i])) {
            continue;
        }
        uint32_t source = entities[i].slot;
        for (int j = 0; j < acount(world->relationships); j++) {
            ecs_relationship *relationship = &world->relationships[j];
            if (relationship->desc.id == ECS_CHILD_OF) {
                continue;
            }
            for (int k = relationship_target_count(relationship, source) - 1; k >= 0;
                 k--) {
                ecs_entity target = relationship_target(relationship, source, k);
                if (owned[target.slot]) {
                    relationship_unlink(relationship, source, target);
                }
            }
        }
    }
    afree(pending);
    free(visited);
    free(owned);
    return result;
}

int ecs_world_destroy_entity(ecs_world *world, ecs_entity entity)
{
    if (world && world->applying) {
        return ECS_ERR_ITERATING;
    }
    int result = ecs_world_record_destroy(world, entity, 0);
    if (result != ECS_OK) {
        return result;
    }
    return ecs_world_apply_commands(world);
}

uint64_t ecs_entity_to_u64(ecs_entity entity)
{
    return ((uint64_t)entity.generation << ECS_ENTITY_SLOT_BITS) | entity.slot;
}

ecs_entity ecs_entity_from_u64(uint64_t value)
{
    return (ecs_entity){
        .slot = (uint32_t)value,
        .generation = (uint32_t)(value >> ECS_ENTITY_SLOT_BITS),
    };
}

int ecs_entity_is_alive(const ecs_world *world, ecs_entity entity)
{
    return world && entity_matches(world, entity);
}

bool ecs_entity_is_retiring(const ecs_world *world, ecs_entity entity)
{
    return ecs_entity_is_alive(world, entity) && world->pending_destroy[entity.slot];
}

int ecs_component_has(const ecs_world *world, ecs_entity entity, ecs_component_id component_id)
{
    const ecs_component *component = world ? find_component_const(world, component_id)
                                           : NULL;
    if (!component || !entity_matches(world, entity)) {
        return 0;
    }
    uint64_t bit = UINT64_C(1) << (entity.slot % 64u);
    return (component->occupancy[entity.slot / 64u] & bit) != 0;
}

bool ecs_component_has_entities(
    const ecs_world *world,
    ecs_component_id component_id,
    const ecs_entity *excluded,
    int excluded_count
)
{
    assert(world && excluded_count >= 0 && (!excluded_count || excluded));
    const ecs_component *component = find_component_const(world, component_id);
    assert(component);
    const ecs_component *disabled = skipped_disabled(world, component_id == ECS_DISABLED);
    for (uint32_t word = 0; word <= world->high_water / 64u; word++) {
        uint64_t occupied = component->occupancy[word];
        if (disabled) {
            occupied &= ~disabled->occupancy[word];
        }
        while (occupied) {
            uint32_t slot = word * 64u + (uint32_t)__builtin_ctzll(occupied);
            occupied &= occupied - 1;
            if (world->pending_destroy[slot]) {
                continue;
            }
            ecs_entity entity = { slot, world->generations[slot] };
            int i = 0;
            while (i < excluded_count &&
                   ecs_entity_to_u64(entity) != ecs_entity_to_u64(excluded[i])) {
                i++;
            }
            if (i == excluded_count) {
                return true;
            }
        }
    }
    return false;
}

static void *row_data(const ecs_world *world, ecs_component *component, ecs_entity entity)
{
    if (!component || !ecs_component_has(world, entity, component->desc.id)) {
        return NULL;
    }
    return component_address(component, entity.slot);
}

/* Name rows change only through ecs_world_record_name. */
void *ecs_component_data(ecs_world *world, ecs_entity entity, ecs_component_id component_id)
{
    if (!world || component_id == ECS_NAME) {
        return NULL;
    }
    return row_data(world, find_component(world, component_id), entity);
}

const void *ecs_component_data_const(const ecs_world *world, ecs_entity entity, ecs_component_id component_id)
{
    const ecs_component *component = world ? find_component_const(world, component_id)
                                           : NULL;
    if (!component || !ecs_component_has(world, entity, component_id)) {
        return NULL;
    }
    return component_address(component, entity.slot);
}

static const type_field *component_field(const ecs_component *component, int field)
{
    if (!component->desc.type || field < 0 || field >= component->desc.type->field_count) {
        return NULL;
    }
    return &component->desc.type->fields[field];
}

int ecs_component_read_field(
    arena *storage,
    const ecs_world *world,
    ecs_entity entity,
    ecs_component_id component_id,
    int field,
    pin_value *out
)
{
    const ecs_component *component = world ? find_component_const(world, component_id)
                                           : NULL;
    const type_field *type_field = component ? component_field(component, field) : NULL;
    const void *data = ecs_component_data_const(world, entity, component_id);
    if (!type_field || !data || !out) {
        return ECS_ERR_ARGUMENT;
    }
    return pin_read_field(storage, data, type_field, out) ? ECS_ERR_ARGUMENT : ECS_OK;
}

static unsigned string_hash(const char *text)
{
    unsigned hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        hash = (hash ^ *p) * 16777619u;
    }
    return hash;
}

static int string_bucket(char *const *strings, int capacity, const char *text)
{
    unsigned mask = (unsigned)capacity - 1;
    unsigned bucket = string_hash(text) & mask;
    while (strings[bucket] && strcmp(strings[bucket], text)) {
        bucket = (bucket + 1) & mask;
    }
    return (int)bucket;
}

static void string_usage_add(uint64_t *total, uint64_t bytes, bool *overflow)
{
    if (UINT64_MAX - *total < bytes) {
        *total = UINT64_MAX;
        *overflow = true;
        return;
    }
    *total += bytes;
}

ecs_string_usage ecs_world_string_usage(const ecs_world *world)
{
    assert(world);
    ecs_string_usage result = { .entries = (uint64_t)world->field_string_count };
    int capacity = acap(world->field_strings);
    if (capacity) {
        result.table_backing_bytes = sizeof(dyna_hdr);
        string_usage_add(
            &result.table_backing_bytes,
            (uint64_t)capacity * sizeof(char *),
            &result.overflow
        );
    }
    for (int i = 0; i < acount(world->field_strings); i++) {
        if (world->field_strings[i]) {
            string_usage_add(
                &result.payload_bytes,
                strlen(world->field_strings[i]),
                &result.overflow
            );
            string_usage_add(&result.payload_bytes, 1, &result.overflow);
        }
    }
    return result;
}

const char *ecs_world_intern_string(ecs_world *world, const char *text)
{
    assert(world);
    text = text ? text : "";
    int capacity = acount(world->field_strings);
    int bucket = capacity ? string_bucket(world->field_strings, capacity, text) : 0;
    if (capacity && world->field_strings[bucket]) {
        return world->field_strings[bucket];
    }
    if (world->field_string_count >= capacity - capacity / 4) {
        assert(capacity <= INT_MAX / 2);
        int next = capacity ? capacity * 2 : 16;
        char **strings = NULL;
        asetlen(strings, next);
        memset(strings, 0, (size_t)next * sizeof *strings);
        for (int i = 0; i < capacity; i++) {
            char *entry = world->field_strings[i];
            if (entry) {
                strings[string_bucket(strings, next, entry)] = entry;
            }
        }
        afree(world->field_strings);
        world->field_strings = strings;
        bucket = string_bucket(strings, next, text);
    }
    char *owned = xstrdup(text);
    world->field_strings[bucket] = owned;
    world->field_string_count++;
    return owned;
}

/* Names change only through record_name; engine systems write derived rows. */
static bool value_writable(const ecs_component *component)
{
    return component->desc.id != ECS_NAME && !component->desc.derived_from;
}

int ecs_component_write_field_resolved(
    ecs_world *world,
    ecs_entity entity,
    ecs_component_id component_id,
    int field,
    pin_value value,
    type_resolver resolver
)
{
    ecs_component *component = world ? find_component(world, component_id) : NULL;
    const type_field *type_field = component && value_writable(component)
                                     ? component_field(component, field)
                                     : NULL;
    void *data = ecs_component_data(world, entity, component_id);
    if (!type_field || !data) {
        return ECS_ERR_ARGUMENT;
    }
    pin_value declared = value;
    declared.type = type_field->type;
    if (type_field->type == PIN_STRUCT || type_field->type == PIN_ENUM ||
        type_field->type == PIN_HANDLE) {
        declared.type_name = type_field->type_name;
        if (!value.type_name) {
            value.type_name = type_field->type_name;
        }
    }
    int result = write_value_resolved(world, component, entity, type_field->offset, declared, value, resolver);
    if (result == ECS_OK) {
        if (type_field->type == PIN_NAME ||
            (type_field->type == PIN_STR && !type_field->text_descriptor)) {
            const char **stored = (const char **)((char *)data + type_field->offset);
            *stored = ecs_world_intern_string(world, *stored);
        }
        mark_modified(component, entity.slot);
    }
    return result;
}

int ecs_component_write_field(ecs_world *world, ecs_entity entity, ecs_component_id id, int field, pin_value value)
{
    return ecs_component_write_field_resolved(world, entity, id, field, value, (type_resolver){ 0 });
}

static bool value_type_matches(pin_value declared, pin_value value)
{
    return declared.type == value.type && declared.container == value.container &&
           !strcmp(
               declared.type_name ? declared.type_name : "",
               value.type_name ? value.type_name : ""
           );
}

static bool field_size_valid(const type_field *field, type_resolver resolver)
{
    size_t size = 0;
    switch (field->type) {
        case PIN_BOOL: size = sizeof(bool); break;
        case PIN_I8:
        case PIN_U8: size = sizeof(uint8_t); break;
        case PIN_I16:
        case PIN_U16: size = sizeof(uint16_t); break;
        case PIN_I32:
        case PIN_U32: size = sizeof(uint32_t); break;
        case PIN_I64:
        case PIN_U64:
        case PIN_HANDLE: size = sizeof(uint64_t); break;
        case PIN_F32: size = sizeof(float); break;
        case PIN_F64: size = sizeof(double); break;
        case PIN_STR:
        case PIN_NAME: size = sizeof(const char *); break;
        case PIN_ENUM: return field->size > 0 && field->size <= sizeof(int32_t);
        case PIN_STRUCT: {
            const type_desc *type = type_resolve(resolver, field->type_name);
            return type && type->size && type->size == field->size;
        }
        case PIN_FLOW:
        case PIN_TASK:
        case PIN_QUERY:
        case PIN_POINTER:
        case PIN_UNION: return false;
    }
    return field->size == size;
}

static const type_field *write_target(
    const type_desc *type,
    size_t offset,
    pin_value declared,
    int depth,
    type_resolver resolver
)
{
    if (depth == 16) {
        return NULL;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->offset > type->size || field->size > type->size - field->offset) {
            return NULL;
        }
        bool nominal = field->type == PIN_STRUCT || field->type == PIN_ENUM ||
                       field->type == PIN_HANDLE;
        pin_value expected = { .type = field->type,
                               .type_name = nominal ? field->type_name
                                                    : declared.type_name };
        if (offset == field->offset && field_size_valid(field, resolver) &&
            value_type_matches(expected, declared)) {
            return field;
        }
        if (field->type != PIN_STRUCT || offset < field->offset ||
            offset - field->offset >= field->size) {
            continue;
        }
        const type_desc *nested = type_resolve(resolver, field->type_name);
        if (!nested || nested->size != field->size) {
            return NULL;
        }
        const type_field *found = write_target(nested, offset - field->offset, declared, depth + 1, resolver);
        if (found) {
            return found;
        }
    }
    return NULL;
}

static bool row_equal(const type_desc *type, const void *left, const void *right)
{
    arena storage = { 0 };
    bool equal = true;
    for (int i = 0; equal && i < type->field_count; i++) {
        pin_value a, b;
        equal = !pin_read_field(&storage, left, &type->fields[i], &a) &&
                !pin_read_field(&storage, right, &type->fields[i], &b) &&
                pin_value_equal(&a, &b);
    }
    arena_free(&storage);
    return equal;
}

static bool component_value_target(
    const ecs_component *component,
    size_t offset,
    pin_value declared,
    bool *whole,
    const type_field **field,
    type_resolver resolver
)
{
    if (!component || declared.container != PIN_CONTAINER_SINGLE ||
        declared.type == PIN_FLOW) {
        return false;
    }
    const type_desc *type = component->desc.type;
    *whole = offset == 0 && declared.type == PIN_STRUCT && declared.type_name &&
             !strcmp(declared.type_name, type->name);
    *field = *whole ? NULL : write_target(type, offset, declared, 0, resolver);
    return *whole || *field;
}

bool ecs_component_accepts_value(const ecs_world *world, ecs_component_id id, size_t offset, pin_value declared)
{
    bool whole;
    const type_field *field;
    const ecs_component *component = world ? find_component_const(world, id) : NULL;
    return component_value_target(
        component && value_writable(component) ? component : NULL,
        offset,
        declared,
        &whole,
        &field,
        (type_resolver){ 0 }
    );
}

static int write_value_resolved(
    ecs_world *world,
    ecs_component *component,
    ecs_entity entity,
    size_t offset,
    pin_value declared,
    pin_value value,
    type_resolver resolver
)
{
    void *row = row_data(world, component, entity);
    bool whole;
    const type_field *field;
    if (!row || !value_type_matches(declared, value) ||
        !component_value_target(component, offset, declared, &whole, &field, resolver)) {
        return ECS_ERR_ARGUMENT;
    }
    const type_desc *type = component->desc.type;
    const type_desc *nested = declared.type == PIN_STRUCT
                                ? (whole ? type
                                         : type_resolve(resolver, declared.type_name))
                                : NULL;
    if (declared.type == PIN_STRUCT &&
        (!nested || !value.ptr || (!whole && nested->size != field->size))) {
        return ECS_ERR_ARGUMENT;
    }
    bool managed = declared.type == PIN_STR || declared.type == PIN_NAME ||
                   (nested && (resolver.find || !type_is_plain(nested)));
    type_field target = whole ? (type_field){ .type = PIN_STRUCT,
                                              .size = type->size,
                                              .type_name = type->name }
                              : *field;
    target.offset = offset;
    if (!managed) {
        if (whole) {
            memmove(row, value.ptr, type->size);
            type_validate_ranges_resolved(type, row, resolver);
        } else {
            if (pin_write_field_resolved(NULL, row, &target, value, resolver)) {
                return ECS_ERR_ARGUMENT;
            }
        }
        return ECS_OK;
    }
    bool owned = entity.slot < (uint32_t)acount(component->owned_rows) &&
                 component->owned_rows[entity.slot].first;
    arena comparison = { 0 };
    pin_value previous;
    bool unchanged = owned && !resolver.find &&
                     (!nested || pin_value_type(&value) == nested) &&
                     !pin_read_field(&comparison, row, &target, &previous) &&
                     (whole ? row_equal(type, row, value.ptr)
                            : pin_value_equal(&previous, &value));
    arena_free(&comparison);
    if (unchanged) {
        type_validate_ranges_resolved(type, row, resolver);
        return ECS_OK;
    }
    if (entity.slot >= INT_MAX) {
        return ECS_ERR_CAPACITY;
    }
    int slot = (int)entity.slot;
    arena next = { 0 };
    void *snapshot = arena_alloc(&next, type->size);
    memcpy(snapshot, row, type->size);
    const char *write_error = whole
                                ? pin_write_struct_resolved(&next, snapshot, type, value, resolver)
                                : pin_write_field_resolved(&next, snapshot, &target, value, resolver);
    if (write_error) {
        arena_free(&next);
        return ECS_ERR_ARGUMENT;
    }
    if (pin_struct_snapshot_fields_resolved(&next, type, snapshot, resolver)) {
        arena_free(&next);
        return ECS_ERR_ARGUMENT;
    }
    type_validate_ranges_resolved(type, snapshot, resolver);
    if (entity.slot >= (uint32_t)acount(component->owned_rows)) {
        int before = acount(component->owned_rows);
        asetlen(component->owned_rows, slot + 1);
        memset(
            component->owned_rows + before,
            0,
            (size_t)(acount(component->owned_rows) - before) * sizeof *component->owned_rows
        );
    }
    arena old = component->owned_rows[entity.slot];
    memcpy(row, snapshot, type->size);
    component->owned_rows[entity.slot] = next;
    arena_free(&old);
    return ECS_OK;
}

static int write_value(
    ecs_world *world,
    ecs_component *component,
    ecs_entity entity,
    size_t offset,
    pin_value declared,
    pin_value value
)
{
    return write_value_resolved(world, component, entity, offset, declared, value, (type_resolver){ 0 });
}

int ecs_component_write_value(
    ecs_world *world,
    ecs_entity entity,
    ecs_component_id id,
    size_t offset,
    pin_value declared,
    pin_value value
)
{
    ecs_component *component = world ? find_component(world, id) : NULL;
    if (component && !value_writable(component)) {
        component = NULL;
    }
    int result = write_value(world, component, entity, offset, declared, value);
    if (result == ECS_OK) {
        mark_modified(component, entity.slot);
    }
    return result;
}

static void cell_free(ecs_component_cell *cell)
{
    assert(!acount(cell->owners));
    arena_free(&cell->schemas);
    afree(cell->owners);
    free(cell);
}
static void cell_charge(ecs_component_cell *cell)
{
    size_t bytes = sizeof *cell + arena_storage_bytes(&cell->schemas);
    if (cell->owners) {
        bytes += sizeof(dyna_hdr) + (size_t)acap(cell->owners) * sizeof *cell->owners;
    }
    for (int i = 0; i < acount(cell->owners); i++) {
        cell_storage *owner = &cell->owners[i];
        arena_external_resize(owner->storage, owner->charged, bytes);
        owner->charged = bytes;
    }
}
void ecs_component_cell_retain_storage(ecs_component_cell *cell, arena *storage)
{
    assert(storage && storage->collectable);
    for (int i = 0; i < acount(cell->owners); i++) {
        if (cell->owners[i].storage == storage) {
            cell->owners[i].references++;
            return;
        }
    }
    apush(cell->owners, ((cell_storage){ .storage = storage, .references = 1 }));
    cell_charge(cell);
}
void ecs_component_cell_release_storage(ecs_component_cell *cell, arena *storage)
{
    for (int i = 0; i < acount(cell->owners); i++) {
        cell_storage *owner = &cell->owners[i];
        if (owner->storage != storage) {
            continue;
        }
        assert(owner->references > 0);
        if (!--owner->references) {
            arena_external_resize(storage, owner->charged, 0);
            adel(cell->owners, i);
        }
        ecs_component_cell_release(cell);
        return;
    }
    assert(false);
}
static void cell_unlink(ecs_component_cell *cell)
{
    ecs_world *world = cell->world;
    ecs_component *component = find_component(world, cell->component);
    assert(component && component->cells[cell->entity.slot] == cell);
    component->cells[cell->entity.slot] = NULL;
    int last = acount(world->cells) - 1;
    assert(cell->index >= 0 && cell->index <= last && world->cells[cell->index] == cell);
    world->cells[cell->index] = world->cells[last];
    world->cells[cell->index]->index = cell->index;
    asetlen(world->cells, last);
    cell->world = NULL;
}
void ecs_component_cell_retain(ecs_component_cell *cell)
{
    assert(cell && cell->references > 0);
    cell->references++;
}
ecs_component_cell *ecs_component_cell_acquire(ecs_world *world, ecs_entity entity, ecs_component_id id)
{
    ecs_component *component = world ? find_component(world, id) : NULL;
    if (!component || !component->desc.type || entity.slot >= INT_MAX ||
        !ecs_component_has(world, entity, id)) {
        return NULL;
    }
    if (entity.slot < (uint32_t)acount(component->cells) &&
        component->cells[entity.slot]) {
        ecs_component_cell *cell = component->cells[entity.slot];
        ecs_component_cell_retain(cell);
        return cell;
    }
    ecs_component_cell *cell = xcalloc(1, sizeof *cell);
    cell->schema = pin_schema_create(&cell->schemas, component->desc.type, (type_resolver){ 0 });
    if (!cell->schema) {
        cell_free(cell);
        return NULL;
    }
    cell->type = pin_schema_type(cell->schema, component->desc.type->name);
    cell->world = world;
    cell->entity = entity;
    cell->component = id;
    cell->layout = component->desc.layout_hash;
    cell->references = 2; /* World plus returned ownership. */
    cell->index = acount(world->cells);
    apush(world->cells, cell);
    if (entity.slot >= (uint32_t)acount(component->cells)) {
        int before = acount(component->cells);
        asetlen(component->cells, (int)entity.slot + 1);
        memset(
            component->cells + before,
            0,
            (size_t)(acount(component->cells) - before) * sizeof *component->cells
        );
    }
    component->cells[entity.slot] = cell;
    return cell;
}
void ecs_component_cell_release(ecs_component_cell *cell)
{
    assert(cell && cell->references > 0);
    cell->references--;
    if (cell->references == 1 && cell->world) {
        cell_unlink(cell);
        cell->references--;
    }
    if (!cell->references) {
        cell_free(cell);
    }
}
const type_desc *ecs_component_cell_type(const ecs_component_cell *cell)
{
    return cell->type;
}
type_resolver ecs_component_cell_resolver(const ecs_component_cell *cell)
{
    return pin_schema_resolver(cell->schema);
}
bool ecs_component_cell_target(
    ecs_component_cell *cell,
    const ecs_execution_view *world,
    uint64_t *entity,
    ecs_component_id *component,
    uint64_t *layout
)
{
    if (!cell || !cell->world || ecs_world_execution(cell->world) != world) {
        return false;
    }
    if (!ecs_component_cell_value(cell).ptr) {
        return false;
    }
    *entity = ecs_entity_to_u64(cell->entity);
    *component = cell->component;
    *layout = cell->layout;
    return true;
}
pin_value ecs_component_cell_value(ecs_component_cell *cell)
{
    void *data = NULL;
    if (cell->world) {
        const ecs_execution_view *world = ecs_world_execution(cell->world);
        uint32_t row = ecs_execution_entity(world, ecs_entity_to_u64(cell->entity));
        const ecs_execution_column *column = ecs_execution_component(world, row, cell->component);
        if (column && column->data && column->layout == cell->layout &&
            column->stride == cell->type->size) {
            data = (char *)column->data + (size_t)row * column->stride;
        } else {
            cell_unlink(cell);
            ecs_component_cell_release(cell); /* Drop world ownership. */
        }
    }
    return (pin_value){ .type = PIN_STRUCT,
                        .type_name = cell->type->name,
                        .schema = cell->schema,
                        .ptr = data };
}
bool ecs_component_cell_alive(ecs_component_cell *cell)
{
    return cell && ecs_component_cell_value(cell).ptr;
}
static void invalidate_cells(ecs_world *world, ecs_entity entity, ecs_component_id component)
{
    ecs_component *owner = find_component(world, component);
    if (!owner || entity.slot >= (uint32_t)acount(owner->cells) ||
        !owner->cells[entity.slot]) {
        return;
    }
    ecs_component_cell *cell = owner->cells[entity.slot];
    cell_unlink(cell);
    ecs_component_cell_release(cell); /* Drop world ownership. */
}
int ecs_component_cell_write(ecs_component_cell *cell, size_t offset, pin_value declared, pin_value value)
{
    if (ecs_component_cell_value(cell).ptr) {
        return ecs_component_write_value(cell->world, cell->entity, cell->component, offset, declared, value);
    }
    return ECS_ERR_COMPONENT;
}

int ecs_component_modified(ecs_world *world, ecs_entity entity, ecs_component_id id)
{
    assert(world);
    ecs_component *component = find_component(world, id);
    if (!component || !entity_matches(world, entity) ||
        !component_matches(component, entity.slot)) {
        return ECS_ERR_COMPONENT;
    }
    type_validate_ranges(
        component->desc.type,
        component_address(component, entity.slot)
    );
    mark_modified(component, entity.slot);
    return ECS_OK;
}

int ecs_component_track_changes(ecs_world *world, ecs_component_id id)
{
    ecs_component *component = world ? find_component(world, id) : NULL;
    if (!component) {
        return ECS_ERR_COMPONENT;
    }
    if (world->iteration_depth || world->applying) {
        return ECS_ERR_ITERATING;
    }
    if (!component->versions) {
        component
            ->versions = xcalloc(world->execution.slot_count, sizeof *component->versions);
    }
    return ECS_OK;
}

uint64_t ecs_component_version(const ecs_world *world, ecs_entity entity, ecs_component_id id)
{
    const ecs_component *component = world ? find_component((ecs_world *)world, id)
                                           : NULL;
    if (!component || !component->versions || !entity_matches(world, entity) ||
        !component_matches(component, entity.slot)) {
        return 0;
    }
    return __atomic_load_n(&component->versions[entity.slot], __ATOMIC_RELAXED);
}

size_t ecs_world_change_tracking_bytes(const ecs_world *world)
{
    size_t bytes = 0;
    for (int i = 0; i < acount(world->components); i++) {
        if (world->components[i].versions) {
            bytes += (size_t)world->execution.slot_count * sizeof(uint64_t);
        }
    }
    return bytes;
}

int ecs_world_iteration_begin(ecs_world *world)
{
    if (!world) {
        return ECS_ERR_ARGUMENT;
    }
    world->iteration_depth++;
    return ECS_OK;
}

int ecs_world_iteration_end(ecs_world *world)
{
    if (!world || !world->iteration_depth) {
        return ECS_ERR_ARGUMENT;
    }
    world->iteration_depth--;
    return ECS_OK;
}

int ecs_world_observe(
    ecs_world *world,
    ecs_event event,
    ecs_component_id component_id,
    ecs_observer callback,
    void *user
)
{
    assert(world && callback && event >= ECS_ON_ADD && event <= ECS_ON_DESTROY);
    if (world->applying || world->iteration_depth) {
        return ECS_ERR_ITERATING;
    }
    if ((event == ECS_ON_DESTROY) != !component_id) {
        return ECS_ERR_ARGUMENT;
    }
    ecs_component *component = component_id ? find_component(world, component_id)
                                            : NULL;
    if (component_id && !component) {
        return ECS_ERR_COMPONENT;
    }
    world->observed |= 1u << event;
    if (component) {
        component->observed |= 1u << event;
    }
    if (event == ECS_ON_SET && !component->modified) {
        component->modified = xcalloc(
            occupancy_words(world->execution.slot_count),
            sizeof *component->modified
        );
    }
    ecs_observer_entry entry = {
        .id = ++world->last_observer,
        .event = event,
        .component = component_id,
        .callback = callback,
        .user = user,
    };
    apush(world->observers, entry);
    return entry.id;
}

static void refresh_observed(ecs_world *world)
{
    world->observed = 0;
    for (int i = 0; i < acount(world->components); i++) {
        world->components[i].observed = 0;
    }
    for (int i = 0; i < acount(world->observers); i++) {
        const ecs_observer_entry *observer = &world->observers[i];
        world->observed |= 1u << observer->event;
        if (observer->component) {
            find_component(world, observer->component)->observed |= 1u
                                                                 << observer->event;
        }
    }
    for (int i = 0; i < acount(world->components); i++) {
        ecs_component *component = &world->components[i];
        if (!observes(component, ECS_ON_SET)) {
            free(component->modified);
            component->modified = NULL;
            component->dirty = false;
        }
    }
}

int ecs_world_unobserve(ecs_world *world, int observer)
{
    assert(world);
    if (world->applying || world->iteration_depth) {
        return ECS_ERR_ITERATING;
    }
    int count = acount(world->observers);
    int at = 0;
    while (at < count && world->observers[at].id != observer) {
        at++;
    }
    if (at == count) {
        return ECS_ERR_ARGUMENT;
    }
    memmove(
        &world->observers[at],
        &world->observers[at + 1],
        (size_t)(count - at - 1) * sizeof *world->observers
    );
    asetlen(world->observers, count - 1);
    refresh_observed(world);
    return ECS_OK;
}

/* A component's bytes, each string field by its text, so the hash sees what
 * the world holds rather than where its strings were allocated. */
static uint64_t component_hash(uint64_t hash, const ecs_component *component, const unsigned char *data)
{
    const type_desc *type = component->desc.type;
    size_t at = 0;
    for (int i = 0; type && i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->type != PIN_STR || field->offset < at) {
            continue;
        }
        hash = hash_bytes(hash, data + at, field->offset - at);
        const char *text;
        memcpy(&text, data + field->offset, sizeof text);
        hash = text ? hash_bytes(hash, text, strlen(text) + 1)
                    : hash_bytes(hash, "", 0);
        at = field->offset + sizeof text;
    }
    return hash_bytes(hash, data + at, component->desc.element_size - at);
}
uint64_t ecs_world_hash(const ecs_world *world)
{
    if (!world) {
        return 0;
    }
    uint64_t hash = UINT64_C(1469598103934665603);
    for (uint32_t slot = 1; slot <= world->high_water; slot++) {
        if (!world->occupied[slot]) {
            continue;
        }
        ecs_entity entity = { slot, world->generations[slot] };
        hash = hash_bytes(hash, &entity, sizeof entity);
        for (int i = 0; i < acount(world->components); i++) {
            const ecs_component *component = &world->components[i];
            if (!ecs_component_has(world, entity, component->desc.id)) {
                continue;
            }
            hash = hash_bytes(hash, &component->desc.id, sizeof component->desc.id);
            if (i == NAME_COMPONENT) {
                const char *name = hierarchy_name(world, slot);
                hash = hash_bytes(hash, name, strlen(name));
                continue;
            }
            hash = component_hash(hash, component, component_address(component, slot));
        }
        for (int i = 0; i < acount(world->relationships); i++) {
            hash = relationship_hash(&world->relationships[i], slot, hash);
        }
    }
    return hash;
}

uint32_t ecs_world_live_count(const ecs_world *world)
{
    return world ? world->live_count : 0;
}

uint32_t ecs_world_high_water_mark(const ecs_world *world)
{
    return world ? world->high_water : 0;
}

uint32_t ecs_world_generation(const ecs_world *world, uint32_t slot)
{
    if (!world || !slot_valid(world, slot)) {
        return 0;
    }
    return world->generations[slot];
}
