#include "ecs/ecs_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool exclusive(const ecs_relationship *relationship)
{
    return relationship->desc.traits & ECS_REL_EXCLUSIVE;
}

ecs_relationship *world_relationship(const ecs_world *world, ecs_component_id id)
{
    for (int i = 0; i < acount(world->relationships); i++) {
        if (world->relationships[i].desc.id == id) {
            return &world->relationships[i];
        }
    }
    return NULL;
}

void relationship_grow(ecs_relationship *relationship, uint32_t old_slots, uint32_t slots)
{
    if (exclusive(relationship)) {
        relationship->target = ecs_grow_storage(
            relationship->target,
            old_slots,
            slots,
            sizeof *relationship->target
        );
    } else {
        relationship->targets = ecs_grow_storage(
            relationship->targets,
            old_slots,
            slots,
            sizeof *relationship->targets
        );
    }
    relationship->sources = ecs_grow_storage(
        relationship->sources,
        old_slots,
        slots,
        sizeof *relationship->sources
    );
}

void relationship_free(ecs_relationship *relationship, uint32_t slots)
{
    for (uint32_t slot = 0; slot < slots; slot++) {
        if (relationship->targets) {
            afree(relationship->targets[slot]);
        }
        afree(relationship->sources[slot]);
    }
    free(relationship->target);
    free(relationship->targets);
    free(relationship->sources);
    free((char *)relationship->desc.name);
    free((char *)relationship->desc.key);
}

int relationship_target_count(const ecs_relationship *relationship, uint32_t slot)
{
    if (exclusive(relationship)) {
        return relationship->target[slot].slot != 0;
    }
    return acount(relationship->targets[slot]);
}

ecs_entity relationship_target(const ecs_relationship *relationship, uint32_t slot, int index)
{
    return exclusive(relationship) ? relationship->target[slot]
                                   : relationship->targets[slot][index];
}

bool relationship_reaches(
    const ecs_world *world,
    const ecs_relationship *relationship,
    ecs_entity from,
    uint32_t to
)
{
    if (exclusive(relationship)) {
        for (uint32_t slot = from.slot; slot; slot = relationship->target[slot].slot) {
            if (slot == to) {
                return true;
            }
        }
        return false;
    }
    /* Depth first with a visited set, so shared ancestors are walked once. */
    uint64_t *visited = xcalloc(occupancy_words(world->execution.slot_count), sizeof *visited);
    uint32_t *stack = NULL;
    apush(stack, from.slot);
    bit_set(visited, from.slot);
    bool found = false;
    while (!found && acount(stack)) {
        uint32_t slot = stack[acount(stack) - 1];
        asetlen(stack, acount(stack) - 1);
        found = slot == to;
        for (int i = 0; i < acount(relationship->targets[slot]); i++) {
            uint32_t next = relationship->targets[slot][i].slot;
            if (!bit_test(visited, next)) {
                bit_set(visited, next);
                apush(stack, next);
            }
        }
    }
    afree(stack);
    free(visited);
    return found;
}

static void remove_source(ecs_relationship *relationship, uint32_t target, uint32_t source)
{
    uint32_t *sources = relationship->sources[target];
    int count = acount(sources);
    int at = 0;
    while (sources[at] != source) {
        at++;
        assert(at < count);
    }
    memmove(&sources[at], &sources[at + 1], (size_t)(count - at - 1) * sizeof *sources);
    asetlen(relationship->sources[target], count - 1);
}

/* Drops target from the source's forward store only. */
static void remove_target(ecs_relationship *relationship, uint32_t source, uint32_t target)
{
    if (exclusive(relationship)) {
        assert(relationship->target[source].slot == target);
        relationship->target[source] = (ecs_entity){ 0, 0 };
        return;
    }
    ecs_entity *targets = relationship->targets[source];
    int count = acount(targets);
    int at = 0;
    while (targets[at].slot != target) {
        at++;
        assert(at < count);
    }
    memmove(&targets[at], &targets[at + 1], (size_t)(count - at - 1) * sizeof *targets);
    asetlen(relationship->targets[source], count - 1);
}

void relationship_link(ecs_relationship *relationship, uint32_t source, ecs_entity target)
{
    query_revision_change(&relationship->query_revision);
    if (exclusive(relationship)) {
        ecs_entity old = relationship->target[source];
        if (old.slot) {
            remove_source(relationship, old.slot, source);
        }
        relationship->target[source] = target;
    } else {
        ecs_entity **targets = &relationship->targets[source];
        uint64_t key = ecs_entity_to_u64(target);
        int count = acount(*targets);
        asetlen(*targets, count + 1);
        int at = count;
        while (at && ecs_entity_to_u64((*targets)[at - 1]) > key) {
            (*targets)[at] = (*targets)[at - 1];
            at--;
        }
        (*targets)[at] = target;
    }
    apush(relationship->sources[target.slot], source);
}

void relationship_unlink(ecs_relationship *relationship, uint32_t source, ecs_entity target)
{
    query_revision_change(&relationship->query_revision);
    remove_target(relationship, source, target.slot);
    remove_source(relationship, target.slot, source);
}

void relationship_detach(ecs_world *world, uint32_t slot)
{
    for (int r = 0; r < acount(world->relationships); r++) {
        ecs_relationship *relationship = &world->relationships[r];
        while (relationship_target_count(relationship, slot)) {
            relationship_unlink(relationship, slot, relationship_target(relationship, slot, 0));
        }
        uint32_t *sources = relationship->sources[slot];
        if (acount(sources)) {
            query_revision_change(&relationship->query_revision);
        }
        for (int i = 0; i < acount(sources); i++) {
            remove_target(relationship, sources[i], slot);
        }
        aclear(relationship->sources[slot]);
    }
}

static uint64_t hash_pair(uint64_t hash, ecs_component_id id, uint64_t other)
{
    uint64_t value[2] = { id, other };
    const unsigned char *bytes = (const unsigned char *)value;
    for (size_t b = 0; b < sizeof value; b++) {
        hash ^= bytes[b];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

/* Hashes the slot's targets and, in order, its sources, so child order counts. */
uint64_t relationship_hash(const ecs_relationship *relationship, uint32_t slot, uint64_t hash)
{
    for (int i = 0; i < relationship_target_count(relationship, slot); i++) {
        hash = hash_pair(
            hash,
            relationship->desc.id,
            ecs_entity_to_u64(relationship_target(relationship, slot, i))
        );
    }
    for (int i = 0; i < acount(relationship->sources[slot]); i++) {
        hash = hash_pair(hash, ~relationship->desc.id, relationship->sources[slot][i]);
    }
    return hash;
}

int ecs_world_register_relationship(ecs_world *world, const ecs_relationship_desc *desc, ecs_component_id *out_id)
{
    assert(world && desc);
    if (!desc->id || !desc->name || !desc->name[0] ||
        desc->on_delete_target > ECS_CLEANUP_PANIC) {
        return ECS_ERR_ARGUMENT;
    }
    if (world->iteration_depth || world->applying) {
        return ECS_ERR_ITERATING;
    }
    if (world_relationship(world, desc->id) || ecs_world_component(world, desc->id) ||
        (desc->key && world_key_taken(world, desc->key))) {
        return ECS_ERR_DUPLICATE;
    }
    ecs_relationship relationship = { .desc = *desc };
    if (desc->traits & ECS_REL_TRAVERSABLE) {
        relationship.desc.traits |= ECS_REL_ACYCLIC;
    }
    relationship.desc.name = xstrdup(desc->name);
    relationship.desc.key = desc->key ? xstrdup(desc->key) : NULL;
    relationship_grow(&relationship, 0, world->execution.slot_count);
    apush(world->relationships, relationship);
    if (out_id) {
        *out_id = desc->id;
    }
    return ECS_OK;
}

const ecs_relationship_desc *ecs_world_relationship(const ecs_world *world, ecs_component_id id)
{
    const ecs_relationship *relationship = world_relationship(world, id);
    return relationship ? &relationship->desc : NULL;
}

bool ecs_has_pair(const ecs_world *world, ecs_entity entity, ecs_component_id id, ecs_entity target)
{
    const ecs_relationship *relationship = world_relationship(world, id);
    if (!relationship || !entity_matches(world, entity)) {
        return false;
    }
    for (int i = 0; i < relationship_target_count(relationship, entity.slot); i++) {
        ecs_entity candidate = relationship_target(relationship, entity.slot, i);
        if (ecs_entity_to_u64(candidate) == ecs_entity_to_u64(target)) {
            return true;
        }
    }
    return false;
}

int ecs_pair_target_count(const ecs_world *world, ecs_entity entity, ecs_component_id id)
{
    const ecs_relationship *relationship = world_relationship(world, id);
    return relationship && entity_matches(world, entity)
             ? relationship_target_count(relationship, entity.slot)
             : 0;
}

ecs_entity ecs_pair_target(const ecs_world *world, ecs_entity entity, ecs_component_id id, int index)
{
    assert(index >= 0 && index < ecs_pair_target_count(world, entity, id));
    return relationship_target(world_relationship(world, id), entity.slot, index);
}

ecs_entity ecs_parent(const ecs_world *world, ecs_entity entity)
{
    return ecs_pair_target_count(world, entity, ECS_CHILD_OF)
             ? ecs_pair_target(world, entity, ECS_CHILD_OF, 0)
             : (ecs_entity){ 0 };
}

int ecs_pair_source_count(const ecs_world *world, ecs_component_id id, ecs_entity target)
{
    const ecs_relationship *relationship = world_relationship(world, id);
    return relationship && entity_matches(world, target)
             ? acount(relationship->sources[target.slot])
             : 0;
}

ecs_entity ecs_pair_source(const ecs_world *world, ecs_component_id id, ecs_entity target, int index)
{
    assert(index >= 0 && index < ecs_pair_source_count(world, id, target));
    uint32_t slot = world_relationship(world, id)->sources[target.slot][index];
    return (ecs_entity){ slot, world->generations[slot] };
}
