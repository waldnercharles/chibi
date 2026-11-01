#ifndef CHIBI_ECS_INTERNAL_H
#define CHIBI_ECS_INTERNAL_H

/* Private world layout shared by the ECS translation units. */

#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "support/arena.h"

typedef struct
{
    ecs_component_desc desc;
    uint64_t *occupancy;
    uint32_t *dense;
    bool dense_stale; /* dense still lists slots removed since it was compacted */
    uint32_t slot_count; /* Allocated payload rows, independent of world capacity. */
    int count;
    uint32_t singleton;
    arena *owned_rows; /* Lazy per-row snapshots, independent of borrowed writes. */
    ecs_component_cell **cells; /* Lazy retained row identities. */
    unsigned observed; /* Bit per ecs_event with at least one observer. */
    uint64_t *modified; /* Rows awaiting ON_SET; allocated while ON_SET is observed. */
    bool dirty;         /* Some modified bit may be set. */
    ecs_component_id *with; /* Components an add of this one implies. */
    uint64_t query_revision; /* Membership only; payload writes keep it unchanged. */
    uint64_t *versions;      /* Lazily tracked per-row publication versions. */
} ecs_component;

typedef struct
{
    ecs_relationship_desc desc;
    ecs_entity *target;   /* Exclusive: each source slot's target, or zero. */
    ecs_entity **targets; /* Otherwise: each source slot's targets, ascending. */
    uint32_t **sources;   /* Each target slot's sources, in insertion order. */
    uint64_t query_revision; /* Edges and source order. */
} ecs_relationship;

typedef struct ecs_command ecs_command;

typedef struct
{
    int id;
    ecs_event event;
    ecs_component_id component;
    ecs_observer callback;
    void *user;
} ecs_observer_entry;

/* Built-ins are registered first, so they keep these array indices. */
enum
{
    NAME_COMPONENT,
    DISABLED_COMPONENT,
};
enum
{
    CHILD_OF_RELATIONSHIP,
};

/* pending_destroy states of a slot. */
enum
{
    DESTROY_NONE,
    DESTROY_RECORDED, /* A destroy command waits for the sync point. */
    DESTROY_RUNNING,  /* Collected by a destroy whose cleanup is running. */
    DESTROY_APPLIED,  /* Destroyed by a sync point; its later commands no-op. */
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
    ecs_component_cell **cells; /* Retained identities, only while attached. */
    int *component_lookup; /* Component array index + 1; zero is an empty bucket. */
    int component_lookup_capacity;
    ecs_relationship *relationships;
    int *depth; /* ChildOf ancestors of each slot. */
    ecs_command *commands;
    char **field_strings; /* Open-addressed, world-owned interned string table. */
    int field_string_count;
    uint64_t next_sequence;
    int iteration_depth;
    bool applying;
    ecs_observer_entry *observers;
    int last_observer;
    unsigned observed;       /* Union of every observer's event bit. */
    uint64_t query_revision; /* Entity creation/destruction, including generations. */
};

/* Saturation disables reuse of this dependency instead of wrapping to an old
 * cache stamp. This remains safe in release builds too. */
static inline void query_revision_change(uint64_t *revision)
{
    if (*revision < UINT64_MAX) {
        (*revision)++;
    }
}

static inline bool slot_valid(const ecs_world *world, uint32_t slot)
{
    return slot > 0 && slot < world->execution.slot_count;
}

static inline bool entity_matches(const ecs_world *world, ecs_entity entity)
{
    return slot_valid(world, entity.slot) && entity.generation &&
           world->occupied[entity.slot] &&
           world->generations[entity.slot] == entity.generation;
}

static inline bool bit_test(const uint64_t *bits, uint32_t slot)
{
    return (bits[slot / 64u] >> (slot % 64u)) & 1;
}

static inline void bit_set(uint64_t *bits, uint32_t slot)
{
    bits[slot / 64u] |= UINT64_C(1) << (slot % 64u);
}

static inline void bit_clear(uint64_t *bits, uint32_t slot)
{
    bits[slot / 64u] &= ~(UINT64_C(1) << (slot % 64u));
}

static inline size_t occupancy_words(uint32_t slots)
{
    return ((size_t)slots + 63) / 64;
}

static inline int component_matches(const ecs_component *component, uint32_t slot)
{
    return bit_test(component->occupancy, slot);
}

/* The Disabled column a query skips, or NULL when it names Disabled. */
static inline const ecs_component *skipped_disabled(const ecs_world *world, bool named)
{
    const ecs_component *disabled = &world->components[DISABLED_COMPONENT];
    return named || !disabled->count ? NULL : disabled;
}

void *ecs_grow_storage(void *data, size_t old_count, size_t count, size_t stride);
ecs_component *find_component(ecs_world *, ecs_component_id);
/* Drops slots removed since the dense list was last compacted. */
void compact_dense(ecs_component *);

ecs_relationship *world_relationship(const ecs_world *, ecs_component_id);
/* Whether a component or relationship already uses the persistent key. */
bool world_key_taken(const ecs_world *, const char *key);
void relationship_grow(ecs_relationship *, uint32_t old_slots, uint32_t slots);
void relationship_free(ecs_relationship *, uint32_t slots);
/* Whether target already reaches source, so linking them closes a cycle. */
bool relationship_reaches(const ecs_world *, const ecs_relationship *, ecs_entity from, uint32_t to);
int relationship_target_count(const ecs_relationship *, uint32_t slot);
ecs_entity relationship_target(const ecs_relationship *, uint32_t slot, int index);
void relationship_link(ecs_relationship *, uint32_t source, ecs_entity target);
void relationship_unlink(ecs_relationship *, uint32_t source, ecs_entity target);
/* Drops every pair the slot holds as a source and every pair aimed at it. */
void relationship_detach(ecs_world *, uint32_t slot);
uint64_t relationship_hash(const ecs_relationship *, uint32_t slot, uint64_t hash);

/* The slot's name, or NULL when it has none. */
const char *hierarchy_name(const ecs_world *, uint32_t slot);
uint32_t hierarchy_parent(const ecs_world *, uint32_t slot);
/* Whether another child of parent (a root when zero) already uses name. */
bool hierarchy_name_taken(const ecs_world *, uint32_t parent, const char *name, uint32_t self);
/* Recomputes depth for the slot and its descendants after a reparent. */
void hierarchy_update_depth(ecs_world *, uint32_t slot);
void hierarchy_move_child(ecs_world *, uint32_t child, int index);

#endif
