#include "ecs.h"

#include "../core/array.h"
#include "../core/pin_conv.h"
#include "../core/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    ecs_component_desc desc;
    uint64_t *occupancy;
    uint32_t *dense;
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

struct ecs_world
{
    uint32_t generations[ECS_ENTITY_SLOT_COUNT];
    unsigned char occupied[ECS_ENTITY_SLOT_COUNT];
    unsigned char reserved[ECS_ENTITY_SLOT_COUNT];
    unsigned char pending_destroy[ECS_ENTITY_SLOT_COUNT];
    uint32_t live_count;
    uint32_t high_water;
    ecs_component *components;
    ecs_command *commands;
    uint64_t next_sequence;
    int iteration_depth;
    ecs_destroy_observer destroy_observer;
    void *destroy_observer_user;
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

static uint64_t hash_string(uint64_t hash, const char *text)
{
    if (!text) {
        return hash_bytes(hash, "", 1);
    }
    return hash_bytes(hash, text, strlen(text) + 1);
}

uint64_t ecs_layout_hash(const type_desc *type)
{
    if (!type) {
        return 0;
    }
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = hash_string(hash, type->name);
    hash = hash_bytes(hash, &type->size, sizeof type->size);
    hash = hash_bytes(hash, &type->field_count, sizeof type->field_count);
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        hash = hash_string(hash, field->name);
        hash = hash_bytes(hash, &field->offset, sizeof field->offset);
        hash = hash_bytes(hash, &field->size, sizeof field->size);
        hash = hash_bytes(hash, &field->type, sizeof field->type);
        hash = hash_string(hash, field->type_name);
    }
    return hash;
}

static ecs_component *find_component(ecs_world *world, ecs_component_id id)
{
    for (int i = 0; i < acount(world->components); i++) {
        if (world->components[i].desc.id == id) {
            return &world->components[i];
        }
    }
    return NULL;
}

static const ecs_component *find_component_const(const ecs_world *world, ecs_component_id id)
{
    return find_component((ecs_world *)world, id);
}

static int slot_valid(uint32_t slot)
{
    return slot > 0 && slot <= ECS_MAX_ENTITIES;
}

static int entity_matches(const ecs_world *world, ecs_entity entity)
{
    return slot_valid(entity.slot) && entity.generation &&
           world->occupied[entity.slot] &&
           world->generations[entity.slot] == entity.generation;
}

static void occupancy_add(ecs_component *component, uint32_t slot)
{
    uint64_t *word = &component->occupancy[slot / 64u];
    uint64_t bit = UINT64_C(1) << (slot % 64u);
    assert(!(*word & bit));
    *word |= bit;
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

static void occupancy_remove(ecs_component *component, uint32_t slot)
{
    uint64_t *word = &component->occupancy[slot / 64u];
    uint64_t bit = UINT64_C(1) << (slot % 64u);
    assert(*word & bit);
    *word &= ~bit;
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
    unsigned char *base = (unsigned char *)component->desc.column_base;
    return base + (size_t)slot * component->desc.element_size;
}

static void advance_generation(ecs_world *world, uint32_t slot)
{
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
    if (!slot_valid(slot) || !world->reserved[slot] || world->occupied[slot] ||
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
            occupancy_remove(component, entity.slot);
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
    void *address = component_address(component, command->entity.slot);
    memset(address, 0, component->desc.element_size);
    if (command->value) {
        memcpy(address, command->value, component->desc.element_size);
    }
    occupancy_add(component, command->entity.slot);
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
    occupancy_remove(component, command->entity.slot);
    return ECS_OK;
}

ecs_world *ecs_world_new(void)
{
    ecs_world *world = (ecs_world *)xcalloc(1, sizeof *world);
    for (uint32_t slot = 1; slot <= ECS_MAX_ENTITIES; slot++) {
        world->generations[slot] = 1;
    }
    return world;
}

void ecs_world_free(ecs_world *world)
{
    if (!world) {
        return;
    }
    ecs_world_clear_commands(world);
    for (int i = 0; i < acount(world->components); i++) {
        free(world->components[i].desc.column_base);
        free(world->components[i].occupancy);
        afree(world->components[i].dense);
    }
    afree(world->components);
    afree(world->commands);
    free(world);
}

int ecs_world_register_component(ecs_world *world, const ecs_component_desc *desc, ecs_component_id *out_id)
{
    if (!world || !desc || !desc->name || !desc->name[0] || !desc->element_size ||
        !desc->type || desc->type->size != desc->element_size) {
        return ECS_ERR_ARGUMENT;
    }
    if (desc->id && find_component(world, desc->id)) {
        return ECS_ERR_DUPLICATE;
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
    component.desc = *desc;
    component.desc.id = id;
    component.desc.layout_hash = desc->layout_hash ? desc->layout_hash
                                                   : ecs_layout_hash(desc->type);
    component.desc.column_base = xcalloc(ECS_ENTITY_SLOT_COUNT, desc->element_size);
    component.occupancy = (uint64_t *)
        xcalloc(ECS_ENTITY_WORD_COUNT, sizeof *component.occupancy);
    apush(world->components, component);
    if (out_id) {
        *out_id = id;
    }
    return ECS_OK;
}

int ecs_world_component_count(const ecs_world *world)
{
    return world ? acount(world->components) : 0;
}

static const type_field *migration_field(const type_desc *type, const char *name)
{
    if (!type) {
        return NULL;
    }
    for (int i = 0; i < type->field_count; i++) {
        if (strcmp(type->fields[i].name, name) == 0) {
            return &type->fields[i];
        }
    }
    return NULL;
}

int ecs_world_migrate_component(
    ecs_world *world,
    const ecs_component_desc *replacement,
    ecs_migration_report *report
)
{
    if (report) {
        *report = (ecs_migration_report){ 0 };
    }
    ecs_component *component = world && replacement
                                 ? find_component(world, replacement->id)
                                 : NULL;
    if (!component || !replacement->type || !replacement->name ||
        strcmp(component->desc.name, replacement->name) != 0 ||
        replacement->element_size != replacement->type->size) {
        return ECS_ERR_ARGUMENT;
    }
    if (component->desc.layout_hash == replacement->layout_hash) {
        void *column = component->desc.column_base;
        component->desc = *replacement;
        component->desc.column_base = column;
        return ECS_OK;
    }
    unsigned char *column = (unsigned char *)
        xcalloc(ECS_ENTITY_SLOT_COUNT, replacement->element_size);
    for (uint32_t slot = 1; slot <= ECS_MAX_ENTITIES; slot++) {
        if (!world->occupied[slot] || !ecs_component_has(
                                          world,
                                          (ecs_entity){ slot, world->generations[slot] },
                                          replacement->id
                                      )) {
            continue;
        }
        void *old_value = component_address(component, slot);
        void *new_value = column + (size_t)slot * replacement->element_size;
        for (int i = 0; i < replacement->type->field_count; i++) {
            const type_field *new_field = &replacement->type->fields[i];
            const type_field
                *old_field = migration_field(component->desc.type, new_field->name);
            if (!old_field ||
                !pin_compatible(
                    &(pin_spec){ .type = old_field->type, .type_name = old_field->type_name },
                    &(pin_spec){ .type = new_field->type, .type_name = new_field->type_name }
                )) {
                if (report) {
                    report->zeroed_fields++;
                }
                continue;
            }
            pin_value value;
            pin_read_field(old_value, old_field, &value);
            pin_write_field(new_value, new_field, value);
            if (report) {
                report->preserved_fields++;
            }
        }
    }
    free(component->desc.column_base);
    component->desc = *replacement;
    component->desc.column_base = column;
    if (report) {
        report->changed = 1;
    }
    return ECS_OK;
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
    for (uint32_t slot = 1; slot <= ECS_MAX_ENTITIES; slot++) {
        if (world->occupied[slot] || world->reserved[slot]) {
            continue;
        }
        world->reserved[slot] = 1;
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
    return (ecs_entity){ 0, 0 };
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
    qsort(world->commands, (size_t)acount(world->commands), sizeof *world->commands, compare_commands);
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
        if (command->kind == ECS_COMMAND_CREATE && slot_valid(command->entity.slot) &&
            !world->occupied[command->entity.slot]) {
            world->reserved[command->entity.slot] = 0;
        }
        if (command->kind == ECS_COMMAND_DESTROY && slot_valid(command->entity.slot)) {
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
    if (component_count) {
        query->components = (ecs_component_id *)xmalloc(
            (size_t)component_count * sizeof *query->components
        );
        for (int i = 0; i < component_count; i++) {
            if (!find_component(world, components[i])) {
                free(query->components);
                memset(query, 0, sizeof *query);
                return ECS_ERR_COMPONENT;
            }
            query->components[i] = components[i];
        }
    }
    query->limit = world->high_water;
    world->iteration_depth++;
    return ECS_OK;
}

int ecs_query_next(ecs_query *query, ecs_entity *out_entity)
{
    if (!query || !query->world || !out_entity) {
        return ECS_ERR_ARGUMENT;
    }
    while (++query->cursor <= query->limit) {
        ecs_entity entity = { query->cursor, query->world->generations[query->cursor] };
        if (!entity_matches(query->world, entity)) {
            continue;
        }
        int found = 1;
        for (int i = 0; i < query->component_count; i++) {
            if (!ecs_component_has(query->world, entity, query->components[i])) {
                found = 0;
                break;
            }
        }
        if (found) {
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
    free(query->components);
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
    if (!world || !slot_valid(slot)) {
        return 0;
    }
    return world->generations[slot];
}
