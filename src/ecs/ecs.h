#ifndef CHIBI_ECS_H
#define CHIBI_ECS_H

#include "types/type.h"

#include <stddef.h>
#include <stdint.h>

#define ECS_ENTITY_SLOT_BITS 32u
#define ECS_ENTITY_GENERATION_BITS 32u

typedef struct ecs_world ecs_world;
typedef struct ecs_query ecs_query;
typedef struct ecs_query_expr ecs_query_expr;
struct ecs_execution_view;

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

typedef struct ecs_component_desc
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
    ECS_ERR_CYCLE = -8,
} ecs_result;

/* Relationships, after Flecs pairs (Rel, Target). A relationship is registered
 * like a component and shares its id space; each pair links a source entity
 * to a live target entity and carries no data. Forward and reverse indexes
 * answer an entity's targets and a target's sources without scanning. */
typedef enum
{
    ECS_REL_EXCLUSIVE = 1u << 0, /* One target per source; adding replaces it. */
    ECS_REL_ACYCLIC = 1u << 1,   /* Adding a pair that closes a cycle fails. */
    ECS_REL_TRAVERSABLE = 1u << 2, /* Queries may walk it; registration adds ACYCLIC. */
} ecs_relationship_trait;

/* What destroying a target does to the pairs that point at it. */
typedef enum
{
    ECS_CLEANUP_REMOVE, /* Drop the pairs. */
    ECS_CLEANUP_DELETE, /* Destroy the sources first, depth first. */
    ECS_CLEANUP_PANIC,  /* Fail the sync point while any source remains. */
} ecs_cleanup;

/* Built-in identities every world registers, after Flecs's ChildOf, Name and
 * Disabled. Each is the marker id of its key ("ecs.ChildOf" and so on).
 * - ChildOf: Exclusive, Traversable, and destroying a parent destroys its
 *   children first. Children keep the order they were parented in.
 * - Name: one name per entity, unique among its siblings (or among roots).
 *   Set it with ecs_world_record_name; direct adds and writes are rejected,
 *   and ecs_component_data returns NULL for it.
 * - Disabled: a tag that hides the entity from every query that does not name
 *   Disabled. It does not propagate to children. */
#define ECS_CHILD_OF UINT64_C(0x84771d550890f1a1)
#define ECS_NAME UINT64_C(0xc5c81abad4002dc1)
#define ECS_DISABLED UINT64_C(0xf9db78dc2d5f93d2)

typedef struct
{
    const char *value;
} ecs_name;

typedef struct ecs_relationship_desc
{
    ecs_component_id id;
    const char *key;
    const char *name;
    unsigned traits;
    ecs_cleanup on_delete_target;
} ecs_relationship_desc;

/* Lifecycle events, after Flecs's OnAdd/OnSet/OnRemove observers. Component
 * events name one registered component; ECS_ON_DESTROY names none (zero). */
typedef enum
{
    ECS_ON_ADD,
    ECS_ON_SET,
    ECS_ON_REMOVE,
    ECS_ON_DESTROY,
} ecs_event;

typedef void (*ecs_observer)(ecs_world *, ecs_event, ecs_entity, ecs_component_id, void *user);

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
    int component_count;
    uint32_t cursor;
    uint32_t limit;
    const uint32_t *dense;
    int dense_count;
    int dense_cursor;
    int dense_index;
    const ecs_query_expr *expression;
    void *component_refs;         /* private query plan */
    const void *single_component; /* Inline plan for a single-component query. */
    void *expression_plan;        /* private expression candidate plan */
    const void *disabled; /* Disabled column to skip, unless the query names it */
};

/* Entity storage grows automatically. Component rows remain contiguous, but a
 * structural change can relocate columns. Reacquire pointers after applying
 * commands or registering components; queries keep storage stable. */
ecs_world *ecs_world_new();
void ecs_world_free(ecs_world *world);
/* The view belongs to the world. Reacquire columns after structural changes. */
const struct ecs_execution_view *ecs_world_execution(const ecs_world *world);
/* Optional bulk reservation; normal creation needs no capacity setting. */
int ecs_world_reserve_entities(ecs_world *world, uint32_t capacity);
uint32_t ecs_world_entity_capacity(const ecs_world *world);
int ecs_world_register_component(ecs_world *world, const ecs_component_desc *desc, ecs_component_id *out_id);
int ecs_world_register_relationship(ecs_world *, const ecs_relationship_desc *, ecs_component_id *out_id);
const ecs_relationship_desc *ecs_world_relationship(const ecs_world *, ecs_component_id);
/* Adding component also adds implied when it is missing, as Flecs's With. */
int ecs_world_add_with(ecs_world *, ecs_component_id component, ecs_component_id implied);
int ecs_world_component_count(const ecs_world *world);
const ecs_component_desc *ecs_world_component_at(const ecs_world *world, int index);
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
/* Pair commands share the command buffer and its sort keys. A target may be a
 * handle reserved by a create recorded earlier in the same sort order. */
int ecs_world_record_add_pair(
    ecs_world *,
    ecs_entity entity,
    ecs_component_id relationship,
    ecs_entity target,
    uint64_t sort_key
);
int ecs_world_record_remove_pair(
    ecs_world *,
    ecs_entity entity,
    ecs_component_id relationship,
    ecs_entity target,
    uint64_t sort_key
);
/* Names must be nonempty and contain no '.', the path separator. Like other
 * records, the entity must already exist. Uniqueness is checked when each
 * command applies, so a rename or reparent that frees a name must apply first
 * (an earlier sort key). */
int ecs_world_record_name(ecs_world *, ecs_entity, const char *name, uint64_t sort_key);
/* Moves a child to index among its parent's children, clamped to the end. */
int ecs_world_record_child_index(ecs_world *, ecs_entity child, int index, uint64_t sort_key);
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
bool ecs_has_pair(const ecs_world *, ecs_entity, ecs_component_id relationship, ecs_entity target);
/* Targets in ascending handle order; an exclusive relationship has at most one. */
int ecs_pair_target_count(const ecs_world *, ecs_entity, ecs_component_id relationship);
ecs_entity ecs_pair_target(const ecs_world *, ecs_entity, ecs_component_id relationship, int index);
/* Sources in the order their pairs were added. */
int ecs_pair_source_count(const ecs_world *, ecs_component_id relationship, ecs_entity target);
ecs_entity ecs_pair_source(const ecs_world *, ecs_component_id relationship, ecs_entity target, int index);
/* The entity's name, or NULL. Valid until the name changes or is removed. */
const char *ecs_entity_name(const ecs_world *, ecs_entity);
/* Resolves a '.'-separated path of names from scope's children, or from the
 * roots when scope is zero. Returns zero when any segment is missing. */
ecs_entity ecs_lookup(const ecs_world *, ecs_entity scope, const char *path);
/* ChildOf ancestors above the entity; zero for roots. */
int ecs_entity_depth(const ecs_world *, ecs_entity);
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
int ecs_component_write_field(
    ecs_world *world,
    ecs_entity entity,
    ecs_component_id component,
    int field,
    pin_value value
);
/* Retain a reflected value in its component row. offset and declared identify
 * either the whole component or one nested reflected field. Managed writes
 * snapshot the full row before publication and retire the previous row storage;
 * returned managed pointers expire on replacement/removal/entity destruction.
 * Plain numeric fields write without allocation. Containers are rejected until
 * reflected fields describe their storage. Direct row writes retain their
 * existing borrowed-pointer contract; this API never frees borrowed payloads. */
int ecs_component_write_value(
    ecs_world *,
    ecs_entity,
    ecs_component_id,
    size_t offset,
    pin_value declared,
    pin_value value
);
/* Validate the same reflected destination contract without requiring an entity
 * or mutating component storage. */
bool ecs_component_accepts_value(const ecs_world *, ecs_component_id, size_t offset, pin_value declared);
/* Retain reflected string values until the world is freed. */
const char *ecs_world_intern_string(ecs_world *world, const char *text);

/* Simple component lists visit their intersection in ascending slot order.
 * Expression queries also visit in ascending slot order and support boolean
 * component presence tests. Both skip Disabled entities unless they name
 * ECS_DISABLED. */
int ecs_query_begin(ecs_world *world, ecs_query *query, const ecs_component_id *components, int component_count);
int ecs_query_begin_expression(ecs_world *world, ecs_query *query, const ecs_query_expr *expression);
int ecs_query_next(ecs_query *query, ecs_entity *out_entity);
void ecs_query_end(ecs_query *query);
int ecs_world_iteration_begin(ecs_world *world);
int ecs_world_iteration_end(ecs_world *world);

/* Observers run on the applying thread inside ecs_world_apply_commands, in
 * registration order, so delivery is deterministic:
 * - ON_SET first, for rows written through ecs_component_write_field,
 *   ecs_component_write_value or ecs_component_modified since the previous
 *   sync point, by component registration order and then ascending slot.
 *   Direct writes through row pointers or the execution view fire nothing
 *   until marked with ecs_component_modified.
 * - Then the sorted commands. ON_ADD follows an add once its row is
 *   initialized; an add that carried a value also marks the row for ON_SET.
 *   ON_REMOVE precedes a remove while the row is still readable. A destroy
 *   fires ON_DESTROY and then ON_REMOVE for each of its components before any
 *   of them is removed.
 * Observers may read and write rows, run queries and record commands.
 * Recorded commands and new ON_SET marks apply in further rounds of the same
 * sync point; marks on the row being delivered are dropped, so ON_SET
 * observers may normalize their own row. Commands for an entity being
 * destroyed are rejected, as are the synchronous create/destroy helpers.
 * Registration is rejected while commands are applied or queries iterate.
 * Returns a positive observer id or a negative ecs_result. */
int ecs_world_observe(ecs_world *, ecs_event, ecs_component_id, ecs_observer, void *user);
int ecs_world_unobserve(ecs_world *, int observer);
/* Queue ON_SET for a row written in place. */
int ecs_component_modified(ecs_world *, ecs_entity, ecs_component_id);
uint64_t ecs_world_hash(const ecs_world *world);
uint32_t ecs_world_live_count(const ecs_world *world);
uint32_t ecs_world_high_water_mark(const ecs_world *world);
uint32_t ecs_world_generation(const ecs_world *world, uint32_t slot);

/* Shared by reflection and tests when a descriptor has no precomputed hash. */
uint64_t ecs_layout_hash(const type_desc *type);

#endif
