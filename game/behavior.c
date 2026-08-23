#include "behavior.h"

#include "../core/array.h"
#include "../core/xalloc.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *name;
    blueprint *definition; /* borrowed by the behavior world */
    blueprint_instance **pool;
} behavior_asset;

typedef struct
{
    ecs_entity entity;
    behavior_asset *asset;
    blueprint_instance *instance;
    uint64_t sequence;
} behavior_attachment;

struct game_behavior_world
{
    ecs_world *ecs;
    behavior_asset *assets;
    behavior_attachment *attachments;
    uint64_t next_sequence;
};

static behavior_asset *find_asset(game_behavior_world *world, const char *name)
{
    for (int i = 0; i < acount(world->assets); i++) {
        if (strcmp(world->assets[i].name, name) == 0) {
            return &world->assets[i];
        }
    }
    return NULL;
}

static behavior_attachment *find_attachment(
    game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name
)
{
    behavior_asset *asset = find_asset(world, asset_name);
    if (!asset) {
        return NULL;
    }
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_attachment *attachment = &world->attachments[i];
        if (attachment->asset == asset && attachment->entity.slot == entity.slot &&
            attachment->entity.generation == entity.generation) {
            return attachment;
        }
    }
    return NULL;
}

static blueprint_instance *acquire_instance(behavior_asset *asset, ecs_entity entity)
{
    pin_value owner = {
        .type = PIN_HANDLE,
        .u64 = ecs_entity_to_u64(entity),
        .type_name = "ecs.entity",
    };
    blueprint_instance *instance;
    if (acount(asset->pool)) {
        instance = blueprint_instance_pool_reuse(apop(asset->pool), asset->definition, &owner, NULL, NULL);
    } else {
        instance = blueprint_instance_create(asset->definition, &owner, NULL, NULL);
    }
    if (instance && blueprint_instance_set_activation_pool(instance, 1) != 0) {
        blueprint_instance_destroy(instance);
        return NULL;
    }
    return instance;
}

static void release_instance(behavior_asset *asset, blueprint_instance *instance)
{
    if (blueprint_instance_pool_release(instance) == 0) {
        apush(asset->pool, instance);
        return;
    }
    blueprint_instance_destroy(instance);
}

static void reap_instance(blueprint_instance *instance)
{
    for (int i = 0; i < acount(instance->activations);) {
        flow_run *run = instance->activations[i];
        flow_state state = flow_run_status(run);
        if (state != FLOW_COMPLETED && state != FLOW_FAILED) {
            i++;
            continue;
        }
        blueprint_instance_release_activation(instance, run);
    }
}

game_behavior_world *game_behavior_world_new(ecs_world *world)
{
    if (!world) {
        return NULL;
    }
    game_behavior_world *result = (game_behavior_world *)xcalloc(1, sizeof *result);
    result->ecs = world;
    result->next_sequence = 1;
    return result;
}

void game_behavior_world_free(game_behavior_world *world)
{
    if (!world) {
        return;
    }
    while (acount(world->attachments)) {
        behavior_attachment *attachment = &alast(world->attachments);
        release_instance(attachment->asset, attachment->instance);
        adel(world->attachments, acount(world->attachments) - 1);
    }
    for (int i = 0; i < acount(world->assets); i++) {
        behavior_asset *asset = &world->assets[i];
        for (int j = 0; j < acount(asset->pool); j++) {
            blueprint_instance_destroy(asset->pool[j]);
        }
        afree(asset->pool);
        free(asset->name);
    }
    afree(world->assets);
    afree(world->attachments);
    free(world);
}

int game_behavior_register_asset(game_behavior_world *world, const char *asset_name, blueprint *definition)
{
    if (!world || !asset_name || !asset_name[0] || !definition ||
        find_asset(world, asset_name) || acount(world->attachments)) {
        return -1;
    }
    apush(
        world->assets,
        (behavior_asset){
            .name = xstrdup(asset_name),
            .definition = definition,
        }
    );
    return 0;
}

static int detach_asset_entity(game_behavior_world *world, behavior_asset *asset, ecs_entity entity);

static void destroy_asset_pool(behavior_asset *asset)
{
    for (int i = 0; i < acount(asset->pool); i++) {
        blueprint_instance_destroy(asset->pool[i]);
    }
    afree(asset->pool);
}

int game_behavior_replace_asset(
    game_behavior_world *world,
    const char *asset_name,
    blueprint *definition,
    blueprint **old_definition
)
{
    behavior_asset *asset = world && asset_name ? find_asset(world, asset_name) : NULL;
    if (!asset || !definition || asset->definition == definition) {
        return -1;
    }
    ecs_entity *entities = NULL;
    for (int i = 0; i < acount(world->attachments); i++) {
        if (world->attachments[i].asset == asset) {
            apush(entities, world->attachments[i].entity);
        }
    }
    for (int i = acount(world->attachments) - 1; i >= 0; i--) {
        if (world->attachments[i].asset != asset) {
            continue;
        }
        release_instance(asset, world->attachments[i].instance);
        adel(world->attachments, i);
    }
    destroy_asset_pool(asset);
    blueprint *old = asset->definition;
    asset->definition = definition;
    for (int i = 0; i < acount(entities); i++) {
        if (game_behavior_attach(world, entities[i], asset_name) != 0) {
            detach_asset_entity(world, asset, entities[i]);
            destroy_asset_pool(asset);
            asset->definition = old;
            for (int j = 0; j < acount(entities); j++) {
                game_behavior_attach(world, entities[j], asset_name);
            }
            afree(entities);
            return -1;
        }
        flow_error begin_error = { 0 };
        flow_run *begin = blueprint_instance_dispatch(
            game_behavior_instance(world, entities[i], asset_name),
            "Begin",
            (flow_inputs){ 0 },
            &begin_error
        );
        if (!begin && begin_error.code != FLOW_ERR_UNKNOWN_EVENT) {
            detach_asset_entity(world, asset, entities[i]);
            destroy_asset_pool(asset);
            asset->definition = old;
            for (int j = 0; j < acount(entities); j++) {
                game_behavior_attach(world, entities[j], asset_name);
            }
            afree(entities);
            return -1;
        }
        if (begin && (flow_run_status(begin) == FLOW_COMPLETED ||
                      flow_run_status(begin) == FLOW_FAILED)) {
            blueprint_instance_release_activation(
                game_behavior_instance(world, entities[i], asset_name),
                begin
            );
        }
    }
    afree(entities);
    if (old_definition) {
        *old_definition = old;
    }
    return 0;
}

int game_behavior_attach(game_behavior_world *world, ecs_entity entity, const char *asset_name)
{
    if (!world || !ecs_entity_is_alive(world->ecs, entity) ||
        find_attachment(world, entity, asset_name)) {
        return -1;
    }
    behavior_asset *asset = find_asset(world, asset_name);
    if (!asset) {
        return -1;
    }
    if (!asset->definition->owner_type_name) {
        if (blueprint_set_owner_type(asset->definition, "ecs.entity") != 0) {
            return -1;
        }
    } else if (strcmp(asset->definition->owner_type_name, "ecs.entity") != 0) {
        return -1;
    }
    blueprint_instance *instance = acquire_instance(asset, entity);
    if (!instance) {
        return -1;
    }
    apush(
        world->attachments,
        (behavior_attachment){
            .entity = entity,
            .asset = asset,
            .instance = instance,
            .sequence = world->next_sequence++,
        }
    );
    return 0;
}

static int detach_asset_entity(game_behavior_world *world, behavior_asset *asset, ecs_entity entity)
{
    int detached = 0;
    for (int i = 0; i < acount(world->attachments);) {
        behavior_attachment *attachment = &world->attachments[i];
        if (attachment->asset != asset || attachment->entity.slot != entity.slot ||
            attachment->entity.generation != entity.generation) {
            i++;
            continue;
        }
        release_instance(asset, attachment->instance);
        adel(world->attachments, i);
        detached++;
    }
    return detached;
}

int game_behavior_detach_entity(game_behavior_world *world, ecs_entity entity)
{
    if (!world) {
        return -1;
    }
    int detached = 0;
    for (int i = 0; i < acount(world->assets); i++) {
        detached += detach_asset_entity(world, &world->assets[i], entity);
    }
    return detached;
}

int game_behavior_destroy_entity(game_behavior_world *world, ecs_entity entity)
{
    if (!world || !ecs_entity_is_alive(world->ecs, entity)) {
        return ECS_ERR_ENTITY;
    }
    game_behavior_detach_entity(world, entity);
    return ecs_world_destroy_entity(world->ecs, entity);
}

int game_behavior_dispatch(
    game_behavior_world *world,
    ecs_entity entity,
    const char *event_name,
    flow_inputs inputs,
    flow_error *error
)
{
    if (!world || !ecs_entity_is_alive(world->ecs, entity) || !event_name) {
        return FLOW_ERR_ARGUMENT;
    }
    if (error) {
        *error = (flow_error){ 0 };
    }
    int dispatched = 0;
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_attachment *attachment = &world->attachments[i];
        if (attachment->entity.slot != entity.slot ||
            attachment->entity.generation != entity.generation) {
            continue;
        }
        flow_error local = { 0 };
        flow_run *run = blueprint_instance_dispatch(attachment->instance, event_name, inputs, &local);
        if (!run) {
            if (local.code == FLOW_ERR_UNKNOWN_EVENT) {
                continue;
            }
            if (error) {
                *error = local;
            }
            return local.code;
        }
        dispatched++;
        if (flow_run_status(run) == FLOW_COMPLETED ||
            flow_run_status(run) == FLOW_FAILED) {
            blueprint_instance_release_activation(attachment->instance, run);
        }
    }
    return dispatched;
}

int game_behavior_pump(game_behavior_world *world, double monotonic_time)
{
    if (!world) {
        return FLOW_ERR_ARGUMENT;
    }
    int result = FLOW_OK;
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_attachment *attachment = &world->attachments[i];
        if (!ecs_entity_is_alive(world->ecs, attachment->entity)) {
            continue;
        }
        int tick = blueprint_instance_tick(attachment->instance, monotonic_time);
        if (tick < 0) {
            result = tick;
        }
        reap_instance(attachment->instance);
    }
    return result;
}

int game_behavior_attachment_count(const game_behavior_world *world)
{
    return world ? acount(world->attachments) : 0;
}

int game_behavior_pool_size(const game_behavior_world *world, const char *asset_name)
{
    behavior_asset *asset = world && asset_name
                              ? find_asset((game_behavior_world *)world, asset_name)
                              : NULL;
    return asset ? acount(asset->pool) : 0;
}

blueprint_instance *game_behavior_instance(game_behavior_world *world, ecs_entity entity, const char *asset_name)
{
    behavior_attachment *attachment = world ? find_attachment(world, entity, asset_name)
                                            : NULL;
    return attachment ? attachment->instance : NULL;
}

int game_behavior_promote_variable(
    game_behavior_world *world,
    const char *asset_name,
    uint64_t variable_id,
    ecs_component_id component_id,
    game_behavior_promote_fn promote,
    void *user
)
{
    behavior_asset *asset = world && asset_name ? find_asset(world, asset_name) : NULL;
    if (!asset || !promote ||
        !blueprint_find_variable_by_id(asset->definition, variable_id)) {
        return -1;
    }
    return promote(asset->definition, variable_id, component_id, user);
}
