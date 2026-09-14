#ifndef CHIBI_ECS_H
#define CHIBI_ECS_H

#include "graph/model/pin.h"
#include "graph/model/type.h"

#include <stddef.h>
#include <stdint.h>

#ifndef ECS_MAX_ENTITIES
#define ECS_MAX_ENTITIES 10000
#endif

#define ECS_ENTITY_SLOT_BITS 32u
#define ECS_ENTITY_GENERATION_BITS 32u
#define ECS_ENTITY_SLOT_COUNT (ECS_MAX_ENTITIES + 1u)
#define ECS_ENTITY_WORD_COUNT ((ECS_ENTITY_SLOT_COUNT + 63u) / 64u)

typedef struct ecs_world ecs_world;
typedef struct ecs_query ecs_query;
typedef struct ecs_query_expr ecs_query_expr;
typedef struct eval_component_reader eval_component_reader;

typedef struct
{
    uint32_t slot;
    uint32_t generation;
} ecs_entity;

typedef uint64_t ecs_component_id;

typedef enum
{
    ECS_INDEX_BITSET,
    ECS_INDEX_DENSE,
} ecs_index_kind;

typedef struct
{
    ecs_component_id id;
    const char *key; /* persistent reflected identity */
    const char *name;
    size_t element_size;
    const type_desc *type;
    uint64_t layout_hash;
    ecs_index_kind index;
    void *column_base; /* filled by ecs_world_register_component */
} ecs_component_desc;

typedef enum
{
    ECS_OK = 0,
    ECS_ERR_ARGUMENT = -1,
    ECS_ERR_ENTITY = -2,
    ECS_ERR_COMPONENT = -3,
    ECS_ERR_DUPLICATE = -4,
    ECS_ERR_COMMAND = -5,
    ECS_ERR_ITERATING = -6,
    ECS_ERR_CAPACITY = -7,
} ecs_result;

typedef void (*ecs_destroy_observer)(ecs_world *world, ecs_entity entity, void *user);

typedef struct
{
    int changed;
    int preserved_fields;
    int zeroed_fields;
} ecs_migration_report;

/* Query expressions are deliberately independent of the graph/editor layer.
 * A term matches component presence; groups provide the boolean vocabulary
 * used by authored system queries. */
typedef enum
{
    ECS_QUERY_ALL,
    ECS_QUERY_ANY,
    ECS_QUERY_NOT,
    ECS_QUERY_HAS,
} ecs_query_expr_kind;

ecs_query_expr *ecs_query_expr_new(ecs_query_expr_kind kind);
ecs_query_expr *ecs_query_expr_has(ecs_component_id component);
ecs_query_expr *ecs_query_expr_clone(const ecs_query_expr *expr);
int ecs_query_expr_add(ecs_query_expr *parent, ecs_query_expr *child);
ecs_query_expr_kind ecs_query_expr_get_kind(const ecs_query_expr *expr);
ecs_component_id ecs_query_expr_get_component(const ecs_query_expr *expr);
int ecs_query_expr_child_count(const ecs_query_expr *expr);
const ecs_query_expr *ecs_query_expr_child(const ecs_query_expr *expr, int index);
void ecs_query_expr_free(ecs_query_expr *expr);

struct ecs_query
{
    ecs_world *world;
    ecs_component_id *components;
    int component_count;
    uint32_t cursor;
    uint32_t limit;
    const uint32_t *dense;
    int dense_count;
    int dense_cursor;
    int dense_index;
    const ecs_query_expr *expression;
    void *component_refs;  /* private query plan */
    void *expression_plan; /* private expression candidate plan */
};

/* A world owns fixed-size entity slots and the registered component columns. */
ecs_world *ecs_world_new(void);
void ecs_world_free(ecs_world *world);
int ecs_world_register_component(ecs_world *world, const ecs_component_desc *desc, ecs_component_id *out_id);
int ecs_world_component_count(const ecs_world *world);
const ecs_component_desc *ecs_world_component_at(const ecs_world *world, int index);
int ecs_world_migrate_component(
    ecs_world *world,
    const ecs_component_desc *replacement,
    ecs_migration_report *report
);
const ecs_component_desc *ecs_world_component(const ecs_world *world, ecs_component_id id);

/* Handles are reserved when a create command is recorded and become alive only
 * when the command buffer is applied. Slot zero is always invalid. */
ecs_entity ecs_world_record_create(ecs_world *world, uint64_t sort_key);
int ecs_world_record_destroy(ecs_world *world, ecs_entity entity, uint64_t sort_key);
int ecs_world_record_add(
    ecs_world *world,
    ecs_entity entity,
    ecs_component_id component,
    const void *initial_value,
    uint64_t sort_key
);
int ecs_world_record_remove(ecs_world *world, ecs_entity entity, ecs_component_id component, uint64_t sort_key);
int ecs_world_apply_commands(ecs_world *world);
void ecs_world_clear_commands(ecs_world *world);
int ecs_world_command_count(const ecs_world *world);

/* Convenience for tests and tools that need one synchronous entity. It still
 * uses the command buffer and sync point internally. */
ecs_entity ecs_world_create_entity(ecs_world *world);
int ecs_world_destroy_entity(ecs_world *world, ecs_entity entity);

uint64_t ecs_entity_to_u64(ecs_entity entity);
ecs_entity ecs_entity_from_u64(uint64_t value);
int ecs_entity_is_alive(const ecs_world *world, ecs_entity entity);
bool ecs_entity_is_retiring(const ecs_world *world, ecs_entity entity);
int ecs_component_has(const ecs_world *world, ecs_entity entity, ecs_component_id component);
/* Read the existing component index without allocating a query plan. Pending
 * destruction and explicitly excluded handles do not count as matches. */
bool ecs_component_has_entities(
    const ecs_world *world,
    ecs_component_id component,
    const ecs_entity *excluded,
    int excluded_count
);
void *ecs_component_data(ecs_world *world, ecs_entity entity, ecs_component_id component);
const void *ecs_component_data_const(const ecs_world *world, ecs_entity entity, ecs_component_id component);
int ecs_component_read_field(
    const ecs_world *world,
    ecs_entity entity,
    ecs_component_id component,
    int field,
    pin_value *out
);
/* Bind the ECS reflected component reader used by authored graph input
 * bindings. Its field_name argument is NULL for a whole component snapshot or
 * a dot-delimited path for a nested field. */
void ecs_eval_component_reader_init(eval_component_reader *reader, ecs_world *world);
int ecs_component_write_field(
    ecs_world *world,
    ecs_entity entity,
    ecs_component_id component,
    int field,
    pin_value value
);
/* Retain reflected string values until the world is freed. */
const char *ecs_world_intern_string(ecs_world *world, const char *text);

/* Simple component lists visit their intersection in ascending slot order.
 * Expression queries also visit in ascending slot order and support boolean
 * component presence tests. */
int ecs_query_begin(ecs_world *world, ecs_query *query, const ecs_component_id *components, int component_count);
int ecs_query_begin_expression(ecs_world *world, ecs_query *query, const ecs_query_expr *expression);
int ecs_query_next(ecs_query *query, ecs_entity *out_entity);
void ecs_query_end(ecs_query *query);
int ecs_world_iteration_begin(ecs_world *world);
int ecs_world_iteration_end(ecs_world *world);

void ecs_world_set_destroy_observer(ecs_world *world, ecs_destroy_observer observer, void *user);
uint64_t ecs_world_hash(const ecs_world *world);
uint32_t ecs_world_live_count(const ecs_world *world);
uint32_t ecs_world_high_water_mark(const ecs_world *world);
uint32_t ecs_world_generation(const ecs_world *world, uint32_t slot);

/* Shared by reflection and tests when a descriptor has no precomputed hash. */
uint64_t ecs_layout_hash(const type_desc *type);

#endif
