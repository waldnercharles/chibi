#include "runtime/behaviors/entities_source.h"

#include "execution/status.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

struct source_entities
{
    source_registry *sources;
    entities *entities;
    double now, delta;
    bool defer_begin;
};

/* Destroying an entity destroys its children too, so their behaviors stop
 * with its own rather than when the registry next reaps them. */
static void detach_tree(source_registry *sources, ecs_world *world, ecs_entity entity)
{
    source_detach_entity(sources, entity);
    for (int i = 0; i < ecs_pair_source_count(world, ECS_CHILD_OF, entity); i++) {
        detach_tree(sources, world, ecs_pair_source(world, ECS_CHILD_OF, entity, i));
    }
}
static int destroy(void *context, const ecs_entity *entities, int count)
{
    source_entities *host = context;
    ecs_world *world = source_world(host->sources);
    /* Pending reparenting decides which children go with them. */
    int status = ecs_world_apply_commands(world);
    for (int i = 0; !status && i < count; i++) {
        detach_tree(host->sources, world, entities[i]);
    }
    for (int i = 0; !status && i < count; i++) {
        if (ecs_entity_is_alive(world, entities[i]) &&
            !ecs_entity_is_retiring(world, entities[i])) {
            status = ecs_world_record_destroy(world, entities[i], (uint64_t)i);
        }
    }
    return status ? status : ecs_world_apply_commands(world);
}
static int attach(void *context, ecs_entity entity, const char *name)
{
    source_entities *host = context;
    return source_attach(host->sources, entity, name, host->defer_begin, host->now, host->delta);
}
static int dispatch(void *context, ecs_entity entity, const char *name, execution_arguments arguments)
{
    source_entities *host = context;
    return source_dispatch(
        host->sources,
        entity,
        name,
        arguments.values,
        arguments.count,
        host->now,
        host->delta
    );
}
source_entities *source_entities_new(source_registry *sources)
{
    assert(sources);
    source_entities *host = xcalloc(1, sizeof *host);
    host->sources = sources;
    entity_host callbacks = { host, destroy, attach, dispatch };
    host->entities = entities_new(source_world(sources), &callbacks);
    return host;
}
void source_entities_free(source_entities *host)
{
    assert(host);
    entities_free(host->entities);
    free(host);
}
entities *source_entities_host(const source_entities *host)
{
    return host->entities;
}
void source_entities_time(source_entities *host, double now, double delta, bool defer_begin)
{
    assert(isfinite(now) && isfinite(delta) && delta >= 0);
    host->now = now;
    host->delta = delta;
    host->defer_begin = defer_begin;
}
