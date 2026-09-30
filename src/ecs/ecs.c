#include "ecs/ecs.h"

#include "ecs/execution.h"
#include "graph/model/pin_conv.h"
#include "graph/runtime/eval.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    ecs_component_desc desc;
    uint64_t *occupancy;
    uint32_t *dense;
    uint32_t slot_count; /* Allocated payload rows, independent of world capacity. */
    int count;
    uint32_t singleton;
} ecs_component;

typedef enum
{
    ECS_COMMAND_CREATE,
    ECS_COMMAND_DESTROY,
    ECS_COMMAND_ADD,
    ECS_COMMAND_REMOVE,
} ecs_command_kind;

typedef struct
{
    uint64_t sort_key;
    uint64_t sequence;
    ecs_command_kind kind;
    ecs_entity entity;
    ecs_component_id component;
    void *value;
    size_t value_size;
} ecs_command;

struct ecs_query_expr
{
    ecs_query_expr_kind kind;
    ecs_component_id component;
    ecs_query_expr **children;
};

struct ecs_world
{
    ecs_execution_view execution;
    ecs_execution_column *execution_columns;
    uint32_t *generations;
    unsigned char *occupied;
    unsigned char *reserved;
    unsigned char *pending_destroy;
    uint32_t free_hint;
    uint32_t live_count;
    uint32_t high_water;
    ecs_component *components;
    int *component_lookup; /* Component array index + 1; zero is an empty bucket. */
    int component_lookup_capacity;
    ecs_command *commands;
    char **field_strings; /* Interned values assigned through the field API. */
    uint64_t next_sequence;
    int iteration_depth;
    ecs_destroy_observer destroy_observer;
    void *destroy_observer_user;
};

typedef struct
{
    uint32_t *slots;
} query_candidate;

typedef struct
{
    uint32_t *slots;
} query_expression_plan;

ecs_query_expr *ecs_query_expr_new(ecs_query_expr_kind kind)
{
    assert(kind == ECS_QUERY_ALL || kind == ECS_QUERY_ANY || kind == ECS_QUERY_NOT);
    ecs_query_expr *expr = (ecs_query_expr *)xcalloc(1, sizeof *expr);
    expr->kind = kind;
    return expr;
}

ecs_query_expr *ecs_query_expr_has(ecs_component_id component)
{
    assert(component);
    ecs_query_expr *expr = (ecs_query_expr *)xcalloc(1, sizeof *expr);
    expr->kind = ECS_QUERY_HAS;
    expr->component = component;
    return expr;
}

ecs_query_expr *ecs_query_expr_clone(const ecs_query_expr *expr)
{
    assert(expr);
    ecs_query_expr *copy = expr->kind == ECS_QUERY_HAS
                             ? ecs_query_expr_has(expr->component)
                             : ecs_query_expr_new(expr->kind);
    for (int i = 0; i < acount(expr->children); i++) {
        int result = ecs_query_expr_add(copy, ecs_query_expr_clone(expr->children[i]));
        assert(result == ECS_OK);
        (void)result;
    }
    return copy;
}

int ecs_query_expr_add(ecs_query_expr *parent, ecs_query_expr *child)
{
    assert(parent && child);
    if (parent->kind == ECS_QUERY_HAS ||
        (parent->kind == ECS_QUERY_NOT && acount(parent->children) == 1)) {
        return ECS_ERR_ARGUMENT;
    }
    apush(parent->children, child);
    return ECS_OK;
}

ecs_query_expr_kind ecs_query_expr_get_kind(const ecs_query_expr *expr)
{
    assert(expr);
    return expr->kind;
}

ecs_component_id ecs_query_expr_get_component(const ecs_query_expr *expr)
{
    assert(expr && expr->kind == ECS_QUERY_HAS);
    return expr->component;
}

int ecs_query_expr_child_count(const ecs_query_expr *expr)
{
    assert(expr);
    return acount(expr->children);
}

const ecs_query_expr *ecs_query_expr_child(const ecs_query_expr *expr, int index)
{
    assert(expr && index >= 0 && index < acount(expr->children));
    return expr->children[index];
}

void ecs_query_expr_free(ecs_query_expr *expr)
{
    if (!expr) {
        return;
    }
    for (int i = 0; i < acount(expr->children); i++) {
        ecs_query_expr_free(expr->children[i]);
    }
    afree(expr->children);
    free(expr);
}

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

static ecs_component *find_component(ecs_world *world, ecs_component_id id)
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
        .singleton = c->singleton
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

static size_t occupancy_words(uint32_t slots)
{
    return ((size_t)slots + 63) / 64;
}

static void *grow_storage(void *data, size_t old_count, size_t count, size_t stride)
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
    world->generations = grow_storage(
        world->generations,
        old,
        count,
        sizeof *world->generations
    );
    world->occupied = grow_storage(world->occupied, old, count, sizeof *world->occupied);
    world->reserved = grow_storage(world->reserved, old, count, sizeof *world->reserved);
    world->pending_destroy = grow_storage(
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
        c->occupancy = grow_storage(
            c->occupancy,
            occupancy_words(old),
            occupancy_words(count),
            sizeof *c->occupancy
        );
        update_execution_column(world, i);
    }
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
    component->desc.column_base = grow_storage(
        component->desc.column_base,
        component->slot_count,
        count,
        component->desc.element_size
    );
    component->slot_count = count;
    update_execution_column(world, (int)(component - world->components));
    return ECS_OK;
}

static int slot_valid(const ecs_world *world, uint32_t slot)
{
    return slot > 0 && slot < world->execution.slot_count;
}

static int entity_matches(const ecs_world *world, ecs_entity entity)
{
    return slot_valid(world, entity.slot) && entity.generation &&
           world->occupied[entity.slot] &&
           world->generations[entity.slot] == entity.generation;
}

static int component_matches(const ecs_component *component, uint32_t slot)
{
    uint64_t bit = UINT64_C(1) << (slot % 64u);
    return (component->occupancy[slot / 64u] & bit) != 0;
}

static void occupancy_count(ecs_world *world, ecs_component *component, uint32_t slot, int change)
{
    component->count += change;
    component->singleton ^= slot;
    ecs_execution_column *column = &world->execution_columns[component - world->components];
    column->count = component->count;
    column->singleton = component->singleton;
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
    int count = acount(component->dense);
    asetlen(component->dense, count + 1);
    int at = count;
    while (at && component->dense[at - 1] > slot) {
        component->dense[at] = component->dense[at - 1];
        at--;
    }
    component->dense[at] = slot;
}

static void occupancy_remove(ecs_world *world, ecs_component *component, uint32_t slot)
{
    uint64_t *word = &component->occupancy[slot / 64u];
    uint64_t bit = UINT64_C(1) << (slot % 64u);
    assert(*word & bit);
    *word &= ~bit;
    occupancy_count(world, component, slot, -1);
    if (component->desc.index != ECS_INDEX_DENSE) {
        return;
    }
    for (int i = 0; i < acount(component->dense); i++) {
        if (component->dense[i] == slot) {
            memmove(
                component->dense + i,
                component->dense + i + 1,
                (size_t)(acount(component->dense) - i - 1) * sizeof *component->dense
            );
            asetlen(component->dense, acount(component->dense) - 1);
            return;
        }
    }
    assert(!"dense occupancy entry missing");
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

static int command_rank(ecs_command_kind kind)
{
    switch (kind) {
        case ECS_COMMAND_CREATE: return 0;
        case ECS_COMMAND_DESTROY: return 1;
        case ECS_COMMAND_ADD: return 2;
        case ECS_COMMAND_REMOVE: return 3;
    }
    return 4;
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
    world->live_count++;
    return ECS_OK;
}

static int apply_destroy(ecs_world *world, const ecs_command *command)
{
    ecs_entity entity = command->entity;
    if (!entity_matches(world, entity)) {
        return ECS_ERR_ENTITY;
    }
    if (world->destroy_observer) {
        world->destroy_observer(world, entity, world->destroy_observer_user);
    }
    for (int i = 0; i < acount(world->components); i++) {
        ecs_component *component = &world->components[i];
        if (ecs_component_has(world, entity, component->desc.id)) {
            occupancy_remove(world, component, entity.slot);
        }
    }
    world->occupied[entity.slot] = 0;
    world->pending_destroy[entity.slot] = 0;
    world->live_count--;
    advance_generation(world, entity.slot);
    trim_high_water(world);
    return ECS_OK;
}

static int apply_add(ecs_world *world, const ecs_command *command)
{
    ecs_component *component = find_component(world, command->component);
    if (!component || !entity_matches(world, command->entity)) {
        return ECS_ERR_ENTITY;
    }
    if (ecs_component_has(world, command->entity, command->component)) {
        return ECS_ERR_DUPLICATE;
    }
    int result = reserve_component(world, component, command->entity.slot);
    if (result) {
        return result;
    }
    void *address = component_address(component, command->entity.slot);
    memset(address, 0, component->desc.element_size);
    if (command->value) {
        memcpy(address, command->value, component->desc.element_size);
    }
    occupancy_add(world, component, command->entity.slot);
    return ECS_OK;
}

static int apply_remove(ecs_world *world, const ecs_command *command)
{
    ecs_component *component = find_component(world, command->component);
    if (!component || !entity_matches(world, command->entity)) {
        return ECS_ERR_ENTITY;
    }
    if (!ecs_component_has(world, command->entity, command->component)) {
        return ECS_ERR_COMPONENT;
    }
    occupancy_remove(world, component, command->entity.slot);
    return ECS_OK;
}

static const char *execution_intern(const ecs_execution_view *world, const char *text)
{
    return ecs_world_intern_string((ecs_world *)world, text);
}

ecs_world *ecs_world_new(void)
{
    ecs_world *world = (ecs_world *)xcalloc(1, sizeof *world);
    world->free_hint = 1;
    world->execution.intern_string = execution_intern;
    int result = ecs_world_reserve_entities(world, INITIAL_ENTITY_SLOTS - 1);
    assert(!result);
    (void)result;
    return world;
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
    for (int i = 0; i < acount(world->components); i++) {
        component_desc_free_names(&world->components[i].desc);
        free(world->components[i].desc.column_base);
        free(world->components[i].occupancy);
        afree(world->components[i].dense);
    }
    afree(world->components);
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

int ecs_world_register_component(ecs_world *world, const ecs_component_desc *desc, ecs_component_id *out_id)
{
    if (!world || !desc || !desc->name || !desc->name[0] || !desc->element_size ||
        !desc->type || desc->type->size != desc->element_size) {
        return ECS_ERR_ARGUMENT;
    }
    if (world->iteration_depth) {
        return ECS_ERR_ITERATING;
    }
    if (desc->id && find_component(world, desc->id)) {
        return ECS_ERR_DUPLICATE;
    }
    if (desc->key) {
        for (int i = 0; i < acount(world->components); i++) {
            if (world->components[i].desc.key &&
                strcmp(world->components[i].desc.key, desc->key) == 0) {
                return ECS_ERR_DUPLICATE;
            }
        }
    }
    ecs_component_id id = desc->id;
    if (!id) {
        id = 1;
        for (;;) {
            if (!find_component(world, id)) {
                break;
            }
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
    if (out_id) {
        *out_id = id;
    }
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
    world->pending_destroy[entity.slot] = 1;
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
        !entity_matches(world, entity)) {
        return ECS_ERR_ENTITY;
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
        !entity_matches(world, entity)) {
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

int ecs_world_apply_commands(ecs_world *world)
{
    if (!world) {
        return ECS_ERR_ARGUMENT;
    }
    if (world->iteration_depth) {
        return ECS_ERR_ITERATING;
    }
    if (!acount(world->commands)) {
        return ECS_OK;
    }
    asort(world->commands, compare_commands);
    int result = ECS_OK;
    for (int i = 0; i < acount(world->commands); i++) {
        const ecs_command *command = &world->commands[i];
        switch (command->kind) {
            case ECS_COMMAND_CREATE:
                result = apply_create(world, command);
                break;
            case ECS_COMMAND_DESTROY:
                result = apply_destroy(world, command);
                break;
            case ECS_COMMAND_ADD: result = apply_add(world, command); break;
            case ECS_COMMAND_REMOVE:
                result = apply_remove(world, command);
                break;
        }
        if (result != ECS_OK) {
            break;
        }
    }
    ecs_world_clear_commands(world);
    return result;
}

void ecs_world_clear_commands(ecs_world *world)
{
    if (!world) {
        return;
    }
    for (int i = 0; i < acount(world->commands); i++) {
        ecs_command *command = &world->commands[i];
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
    aclear(world->commands);
    trim_high_water(world);
}

int ecs_world_command_count(const ecs_world *world)
{
    return world ? acount(world->commands) : 0;
}

ecs_entity ecs_world_create_entity(ecs_world *world)
{
    ecs_entity entity = ecs_world_record_create(world, 0);
    if (!entity.slot || ecs_world_apply_commands(world) != ECS_OK) {
        return (ecs_entity){ 0, 0 };
    }
    return entity;
}

int ecs_world_destroy_entity(ecs_world *world, ecs_entity entity)
{
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
    for (uint32_t word = 0; word <= world->high_water / 64u; word++) {
        uint64_t occupied = component->occupancy[word];
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

void *ecs_component_data(ecs_world *world, ecs_entity entity, ecs_component_id component_id)
{
    ecs_component *component = world ? find_component(world, component_id) : NULL;
    if (!component || !ecs_component_has(world, entity, component_id)) {
        return NULL;
    }
    return component_address(component, entity.slot);
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
    pin_read_field(data, type_field, out);
    return ECS_OK;
}

static int eval_read_component_field(
    void *user,
    uint64_t owner,
    const char *component_key,
    const char *field_name,
    pin_value *out
)
{
    ecs_world *world = user;
    ecs_entity entity = ecs_entity_from_u64(owner);
    if (!world || !out || !ecs_entity_is_alive(world, entity)) {
        return EVAL_COMPONENT_READ_INVALID_OWNER;
    }
    const ecs_component *component = NULL;
    for (int i = 0; i < ecs_world_component_count(world); i++) {
        const ecs_component_desc *candidate = ecs_world_component_at(world, i);
        if (candidate && candidate->key && component_key &&
            strcmp(candidate->key, component_key) == 0) {
            component = find_component_const(world, candidate->id);
            break;
        }
    }
    if (!component) {
        return EVAL_COMPONENT_READ_MISSING_COMPONENT;
    }
    if (!ecs_component_has(world, entity, component->desc.id)) {
        return EVAL_COMPONENT_READ_MISSING_COMPONENT;
    }
    const void *data = ecs_component_data_const(world, entity, component->desc.id);
    if (!component->desc.type || !data || !out) {
        return EVAL_COMPONENT_READ_MISSING_COMPONENT;
    }
    if (!field_name) {
        *out = (pin_value){
            .type = PIN_STRUCT,
            .container = PIN_CONTAINER_SINGLE,
            .type_name = component->desc.type->name,
            .ptr = (void *)data,
        };
        return EVAL_COMPONENT_READ_OK;
    }
    return pin_read_field_path(data, component->desc.type, field_name, out) == 0
             ? EVAL_COMPONENT_READ_OK
             : EVAL_COMPONENT_READ_MISSING_FIELD;
}

void ecs_eval_component_reader_init(eval_component_reader *reader, ecs_world *world)
{
    if (reader) {
        *reader = (eval_component_reader){ .read = eval_read_component_field,
                                           .user = world,
                                           .execution = (const ecs_execution_view *)world };
    }
}

const char *ecs_world_intern_string(ecs_world *world, const char *text)
{
    assert(world);
    text = text ? text : "";
    for (int i = 0; i < acount(world->field_strings); i++) {
        if (!strcmp(world->field_strings[i], text)) {
            return world->field_strings[i];
        }
    }
    char *owned = xstrdup(text);
    apush(world->field_strings, owned);
    return owned;
}

int ecs_component_write_field(
    ecs_world *world,
    ecs_entity entity,
    ecs_component_id component_id,
    int field,
    pin_value value
)
{
    ecs_component *component = world ? find_component(world, component_id) : NULL;
    const type_field *type_field = component ? component_field(component, field) : NULL;
    void *data = ecs_component_data(world, entity, component_id);
    if (!type_field || !data) {
        return ECS_ERR_ARGUMENT;
    }
    if (type_field->type == PIN_STR || type_field->type == PIN_NAME) {
        value.str = ecs_world_intern_string(world, value.str);
    }
    pin_write_field(data, type_field, value);
    return ECS_OK;
}

int ecs_query_begin(ecs_world *world, ecs_query *query, const ecs_component_id *components, int component_count)
{
    if (!world || !query || component_count < 0 ||
        (component_count && !components) || world->iteration_depth) {
        return ECS_ERR_ARGUMENT;
    }
    memset(query, 0, sizeof *query);
    query->world = world;
    query->component_count = component_count;
    query->dense_count = -1;
    query->dense_index = -1;
    if (component_count) {
        const void **refs = component_count == 1
                              ? &query->single_component
                              : xmalloc((size_t)component_count * sizeof *refs);
        query->component_refs = refs;
        const ecs_component *candidate = NULL;
        for (int i = 0; i < component_count; i++) {
            const ecs_component *component = find_component(world, components[i]);
            if (!component) {
                if (component_count > 1) {
                    free(query->component_refs);
                }
                memset(query, 0, sizeof *query);
                return ECS_ERR_COMPONENT;
            }
            refs[i] = component;
            if (component->desc.index == ECS_INDEX_DENSE &&
                (!candidate || acount(component->dense) < acount(candidate->dense))) {
                candidate = component;
                query->dense_index = i;
            }
        }
        if (candidate) {
            query->dense = candidate->dense;
            query->dense_count = acount(candidate->dense);
            query->dense_cursor = -1;
        }
    }
    query->limit = world->high_water;
    world->iteration_depth++;
    return ECS_OK;
}

static int query_expression_valid(const ecs_world *world, const ecs_query_expr *expr)
{
    if (!expr) {
        return 0;
    }
    if (expr->kind == ECS_QUERY_HAS) {
        return find_component_const(world, expr->component) != NULL;
    }
    if (expr->kind == ECS_QUERY_NOT && acount(expr->children) != 1) {
        return 0;
    }
    for (int i = 0; i < acount(expr->children); i++) {
        if (!query_expression_valid(world, expr->children[i])) {
            return 0;
        }
    }
    return 1;
}

static int compare_slots(const void *left_ptr, const void *right_ptr)
{
    uint32_t left = *(const uint32_t *)left_ptr;
    uint32_t right = *(const uint32_t *)right_ptr;
    return left < right ? -1 : left != right;
}

static void candidate_union(uint32_t **slots, const uint32_t *other)
{
    for (int i = 0; i < acount(other); i++) {
        apush(*slots, other[i]);
    }
}

static int query_expression_candidate(
    const ecs_world *world,
    const ecs_query_expr *expr,
    query_candidate *candidate
)
{
    candidate->slots = NULL;
    if (expr->kind == ECS_QUERY_HAS) {
        const ecs_component *component = find_component_const(world, expr->component);
        if (component->desc.index != ECS_INDEX_DENSE) {
            return 0;
        }
        for (int i = 0; i < acount(component->dense); i++) {
            apush(candidate->slots, component->dense[i]);
        }
        return 1;
    }
    if (expr->kind == ECS_QUERY_NOT) {
        return 0;
    }
    if (expr->kind == ECS_QUERY_ANY) {
        for (int i = 0; i < acount(expr->children); i++) {
            query_candidate child;
            if (!query_expression_candidate(world, expr->children[i], &child)) {
                afree(candidate->slots);
                return 0;
            }
            candidate_union(&candidate->slots, child.slots);
            afree(child.slots);
        }
        asort(candidate->slots, compare_slots);
        int write = 0;
        for (int i = 0; i < acount(candidate->slots); i++) {
            if (!write || candidate->slots[i] != candidate->slots[write - 1]) {
                candidate->slots[write++] = candidate->slots[i];
            }
        }
        asetlen(candidate->slots, write);
        return 1;
    }
    int found = 0;
    for (int i = 0; i < acount(expr->children); i++) {
        query_candidate child;
        if (!query_expression_candidate(world, expr->children[i], &child)) {
            continue;
        }
        if (!found || acount(child.slots) < acount(candidate->slots)) {
            afree(candidate->slots);
            candidate->slots = child.slots;
            found = 1;
        } else {
            afree(child.slots);
        }
    }
    return found;
}

int ecs_query_begin_expression(ecs_world *world, ecs_query *query, const ecs_query_expr *expression)
{
    if (!world || !query || world->iteration_depth ||
        !query_expression_valid(world, expression)) {
        return ECS_ERR_ARGUMENT;
    }
    memset(query, 0, sizeof *query);
    query->world = world;
    query->expression = expression;
    query->dense_count = -1;
    query->dense_index = -1;
    query->limit = world->high_water;
    query_expression_plan *plan = (query_expression_plan *)xcalloc(1, sizeof *plan);
    query_candidate candidate;
    if (query_expression_candidate(world, expression, &candidate)) {
        plan->slots = candidate.slots;
        query->dense = plan->slots;
        query->dense_count = acount(plan->slots);
        query->dense_cursor = -1;
    }
    query->expression_plan = plan;
    world->iteration_depth++;
    return ECS_OK;
}

static int query_expression_matches(const ecs_world *world, const ecs_query_expr *expr, uint32_t slot)
{
    if (expr->kind == ECS_QUERY_HAS) {
        const ecs_component *component = find_component_const(world, expr->component);
        return component_matches(component, slot);
    }
    if (expr->kind == ECS_QUERY_NOT) {
        return !query_expression_matches(world, expr->children[0], slot);
    }
    if (expr->kind == ECS_QUERY_ANY) {
        for (int i = 0; i < acount(expr->children); i++) {
            if (query_expression_matches(world, expr->children[i], slot)) {
                return 1;
            }
        }
        return 0;
    }
    for (int i = 0; i < acount(expr->children); i++) {
        if (!query_expression_matches(world, expr->children[i], slot)) {
            return 0;
        }
    }
    return 1;
}

int ecs_query_next(ecs_query *query, ecs_entity *out_entity)
{
    if (!query || !query->world || !out_entity) {
        return ECS_ERR_ARGUMENT;
    }
    while (query->dense_count >= 0 ? ++query->dense_cursor < query->dense_count
                                   : ++query->cursor <= query->limit) {
        uint32_t slot = query->dense_count >= 0 ? query->dense[query->dense_cursor]
                                                : query->cursor;
        ecs_entity entity = { slot, query->world->generations[slot] };
        if (query->dense_count < 0 && !entity_matches(query->world, entity)) {
            continue;
        }
        if (query->expression &&
            !query_expression_matches(query->world, query->expression, slot)) {
            continue;
        }
        const void *const *refs = query->component_refs;
        int i;
        for (i = 0; i < query->component_count; i++) {
            if (i != query->dense_index && !component_matches(refs[i], slot)) {
                break;
            }
        }
        if (i == query->component_count) {
            *out_entity = entity;
            return 1;
        }
    }
    return 0;
}

void ecs_query_end(ecs_query *query)
{
    if (!query || !query->world) {
        return;
    }
    assert(query->world->iteration_depth > 0);
    query->world->iteration_depth--;
    if (query->component_count > 1) {
        free(query->component_refs);
    }
    query_expression_plan *plan = (query_expression_plan *)query->expression_plan;
    if (plan) {
        afree(plan->slots);
        free(plan);
    }
    memset(query, 0, sizeof *query);
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

void ecs_world_set_destroy_observer(ecs_world *world, ecs_destroy_observer observer, void *user)
{
    if (!world) {
        return;
    }
    world->destroy_observer = observer;
    world->destroy_observer_user = user;
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
            hash = hash_bytes(
                hash,
                component_address(component, slot),
                component->desc.element_size
            );
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
