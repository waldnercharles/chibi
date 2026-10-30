#include "authoring/scene_snapshot.h"

#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>

struct authoring_scene_owner
{
    uint64_t generation;
    int references;
    bool alive;
};
struct authoring_scene_snapshot
{
    authoring_scene_owner *owner;
    uint64_t generation;
    authoring_scene_node *nodes;
    int count;
};

static void release(authoring_scene_owner *owner)
{
    assert(owner && owner->references > 0);
    if (!--owner->references) {
        free(owner);
    }
}

authoring_scene_owner *authoring_scene_owner_new()
{
    authoring_scene_owner *owner = xcalloc(1, sizeof *owner);
    *owner = (authoring_scene_owner){ .generation = 1, .references = 1, .alive = true };
    return owner;
}

void authoring_scene_owner_changed(authoring_scene_owner *owner)
{
    assert(owner && owner->alive);
    /* Exhaustion must not make old snapshots current again in Release. */
    if (owner->generation == UINT64_MAX) {
        abort();
    }
    owner->generation++;
}

void authoring_scene_owner_free(authoring_scene_owner *owner)
{
    assert(owner && owner->alive);
    owner->alive = false;
    release(owner);
}

authoring_scene_snapshot *authoring_scene_snapshot_new(
    authoring_scene_owner *owner,
    const authoring_scene_node *nodes,
    int count,
    char *error,
    size_t error_size
)
{
    assert(owner && owner->alive);
    if (authoring_scene_validate(nodes, count, error, error_size)) {
        return NULL;
    }
    if (owner->references == INT_MAX) {
        abort();
    }
    authoring_scene_snapshot *snapshot = xcalloc(1, sizeof *snapshot);
    *snapshot = (authoring_scene_snapshot){ .owner = owner,
                                            .generation = owner->generation,
                                            .count = count };
    owner->references++;
    if (count) {
        snapshot->nodes = xcalloc((size_t)count, sizeof *snapshot->nodes);
    }
    for (int i = 0; i < count; i++) {
        snapshot->nodes[i] = nodes[i];
        snapshot->nodes[i].name = xstrdup(nodes[i].name);
    }
    return snapshot;
}

void authoring_scene_snapshot_free(authoring_scene_snapshot *snapshot)
{
    assert(snapshot);
    for (int i = 0; i < snapshot->count; i++) {
        free((char *)snapshot->nodes[i].name);
    }
    free(snapshot->nodes);
    release(snapshot->owner);
    free(snapshot);
}

const authoring_scene_node *authoring_scene_snapshot_nodes(
    const authoring_scene_snapshot *snapshot,
    int *count
)
{
    assert(snapshot && count);
    *count = snapshot->count;
    return snapshot->nodes;
}

bool authoring_scene_snapshot_current(
    const authoring_scene_snapshot *snapshot,
    const authoring_scene_owner *owner
)
{
    assert(snapshot);
    return snapshot->owner == owner && snapshot->owner->alive &&
           snapshot->generation == snapshot->owner->generation;
}
