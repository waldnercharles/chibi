#include "ecs/ecs_internal.h"
#include "support/array.h"

#include <assert.h>
#include <string.h>

uint32_t hierarchy_parent(const ecs_world *world, uint32_t slot)
{
    return world->relationships[CHILD_OF_RELATIONSHIP].target[slot].slot;
}

const char *hierarchy_name(const ecs_world *world, uint32_t slot)
{
    const ecs_component *names = &world->components[NAME_COMPONENT];
    if (!bit_test(names->occupancy, slot)) {
        return NULL;
    }
    const ecs_name *row = (const ecs_name *)names->desc.column_base + slot;
    return row->value;
}

static bool name_matches(const char *name, const char *segment, size_t length)
{
    return name && !strncmp(name, segment, length) && !name[length];
}

/* Children are scanned in order; roots are the named slots with no parent. */
static uint32_t find_child(const ecs_world *world, uint32_t parent, const char *segment, size_t length)
{
    if (parent) {
        const uint32_t *children = world->relationships[CHILD_OF_RELATIONSHIP].sources[parent];
        for (int i = 0; i < acount(children); i++) {
            if (name_matches(hierarchy_name(world, children[i]), segment, length)) {
                return children[i];
            }
        }
        return 0;
    }
    const uint64_t *named = world->components[NAME_COMPONENT].occupancy;
    for (uint32_t word = 0; word <= world->high_water / 64u; word++) {
        for (uint64_t bits = named[word]; bits; bits &= bits - 1) {
            uint32_t slot = word * 64u + (uint32_t)__builtin_ctzll(bits);
            if (!hierarchy_parent(world, slot) &&
                name_matches(hierarchy_name(world, slot), segment, length)) {
                return slot;
            }
        }
    }
    return 0;
}

bool hierarchy_name_taken(const ecs_world *world, uint32_t parent, const char *name, uint32_t self)
{
    uint32_t found = find_child(world, parent, name, strlen(name));
    return found && found != self;
}

void hierarchy_update_depth(ecs_world *world, uint32_t slot)
{
    const ecs_relationship *child_of = &world->relationships[CHILD_OF_RELATIONSHIP];
    uint32_t *pending = NULL;
    apush(pending, slot);
    while (acount(pending)) {
        uint32_t next = pending[acount(pending) - 1];
        asetlen(pending, acount(pending) - 1);
        uint32_t parent = hierarchy_parent(world, next);
        world->depth[next] = parent ? world->depth[parent] + 1 : 0;
        for (int i = 0; i < acount(child_of->sources[next]); i++) {
            apush(pending, child_of->sources[next][i]);
        }
    }
    afree(pending);
}

void hierarchy_move_child(ecs_world *world, uint32_t child, int index)
{
    uint32_t *children = world->relationships[CHILD_OF_RELATIONSHIP]
                             .sources[hierarchy_parent(world, child)];
    int count = acount(children);
    int at = 0;
    while (children[at] != child) {
        at++;
        assert(at < count);
    }
    index = index < count ? index : count - 1;
    if (index == at) {
        return;
    }
    query_revision_change(&world->relationships[CHILD_OF_RELATIONSHIP].query_revision);
    if (index < at) {
        memmove(&children[index + 1], &children[index], (size_t)(at - index) * sizeof *children);
    } else {
        memmove(&children[at], &children[at + 1], (size_t)(index - at) * sizeof *children);
    }
    children[index] = child;
}

const char *ecs_entity_name(const ecs_world *world, ecs_entity entity)
{
    return entity_matches(world, entity) ? hierarchy_name(world, entity.slot) : NULL;
}

ecs_entity ecs_lookup(const ecs_world *world, ecs_entity scope, const char *path)
{
    assert(world && path);
    if (scope.slot && !entity_matches(world, scope)) {
        return (ecs_entity){ 0, 0 };
    }
    uint32_t slot = scope.slot;
    do {
        const char *end = strchr(path, '.');
        size_t length = end ? (size_t)(end - path) : strlen(path);
        slot = length ? find_child(world, slot, path, length) : 0;
        path = end ? end + 1 : NULL;
    } while (slot && path);
    return slot ? (ecs_entity){ slot, world->generations[slot] }
                : (ecs_entity){ 0, 0 };
}

int ecs_entity_depth(const ecs_world *world, ecs_entity entity)
{
    return entity_matches(world, entity) ? world->depth[entity.slot] : 0;
}
