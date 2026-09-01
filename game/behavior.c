#include "behavior.h"

#include "../core/array.h"
#include "../core/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAME_BEHAVIOR_LOG_MAX 256

typedef struct
{
    char *name;
    behavior_asset *definition; /* borrowed by the behavior world */
    behavior_instance **pool;
} behavior_asset_record;

typedef struct
{
    game_behavior_world *world;
    ecs_entity entity;
    const char *asset_name;
} behavior_sink_context;

typedef struct
{
    ecs_entity entity;
    behavior_asset_record *asset;
    behavior_instance *instance;
    uint64_t sequence;
    int debug_hold;
} behavior_attachment;

struct game_behavior_world
{
    ecs_world *ecs;
    behavior_asset_record *assets;
    behavior_attachment *attachments;
    game_behavior_log_info *logs;
    flow_parallel_for_fn parallel_for;
    void *parallel_user;
    flow_sink_fn sink;
    void *sink_user;
    int breakpoints_enabled;
    int *pump_results;
    int pump_result_capacity;
    uint64_t next_sequence;
};

static void destroy_asset_pool(behavior_asset_record *asset);

static behavior_asset_record *find_asset(game_behavior_world *world, const char *name)
{
    for (int i = 0; i < acount(world->assets); i++) {
        if (strcmp(world->assets[i].name, name) == 0) {
            return &world->assets[i];
        }
    }
    return NULL;
}

static void behavior_sink(const node *origin, const char *message, void *user)
{
    behavior_sink_context *context = (behavior_sink_context *)user;
    game_behavior_world *world = context->world;
    if (acount(world->logs) == GAME_BEHAVIOR_LOG_MAX) {
        adel(world->logs, 0);
    }
    game_behavior_log_info entry = {
        .entity = context->entity,
        .asset_name = context->asset_name,
        .origin = origin,
    };
    snprintf(entry.message, sizeof entry.message, "%s", message ? message : "");
    apush(world->logs, entry);
    if (world->sink) {
        world->sink(origin, message, world->sink_user);
    }
}

static behavior_attachment *find_attachment(
    game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name
)
{
    behavior_asset_record *asset = find_asset(world, asset_name);
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

static behavior_instance *acquire_instance(
    behavior_asset_record *asset,
    game_behavior_world *world,
    ecs_entity entity
)
{
    pin_value owner = {
        .type = PIN_HANDLE,
        .u64 = ecs_entity_to_u64(entity),
        .type_name = "ecs.entity",
    };
    behavior_instance *instance;
    if (acount(asset->pool)) {
        instance = behavior_instance_pool_reuse(apop(asset->pool), asset->definition, &owner, NULL, NULL);
    } else {
        instance = behavior_instance_create(asset->definition, &owner, NULL, NULL);
    }
    if (instance &&
        (behavior_instance_set_activation_pool(instance, 1) != 0 ||
         behavior_instance_bind_service(instance, "ecs.world", world->ecs) != 0)) {
        behavior_instance_destroy(instance);
        return NULL;
    }
    behavior_sink_context *context = (behavior_sink_context *)xcalloc(1, sizeof *context);
    context->world = world;
    context->entity = entity;
    context->asset_name = asset->name;
    instance->sink = behavior_sink;
    instance->sink_user = context;
    return instance;
}

static void release_instance(behavior_asset_record *asset, behavior_instance *instance)
{
    free(instance->sink_user);
    instance->sink = NULL;
    instance->sink_user = NULL;
    if (behavior_instance_pool_release(instance) == 0) {
        apush(asset->pool, instance);
        return;
    }
    behavior_instance_destroy(instance);
}

static void reap_instance(behavior_instance *instance)
{
    for (int i = 0; i < acount(instance->activations);) {
        flow_run *run = instance->activations[i];
        flow_state state = flow_run_status(run);
        if (state != FLOW_COMPLETED && state != FLOW_FAILED) {
            i++;
            continue;
        }
        behavior_instance_release_activation(instance, run);
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
        behavior_asset_record *asset = &world->assets[i];
        for (int j = 0; j < acount(asset->pool); j++) {
            behavior_instance_destroy(asset->pool[j]);
        }
        afree(asset->pool);
        free(asset->name);
    }
    afree(world->assets);
    afree(world->attachments);
    afree(world->logs);
    free(world->pump_results);
    free(world);
}

int game_behavior_register_asset(game_behavior_world *world, const char *asset_name, behavior_asset *definition)
{
    if (!world || !asset_name || !asset_name[0] || !definition ||
        find_asset(world, asset_name) || acount(world->attachments)) {
        return -1;
    }
    apush(
        world->assets,
        (behavior_asset_record){
            .name = xstrdup(asset_name),
            .definition = definition,
        }
    );
    return 0;
}

int game_behavior_unregister_asset(
    game_behavior_world *world,
    const char *asset_name,
    behavior_asset **definition
)
{
    if (!world || !asset_name || !asset_name[0] || !definition) {
        return -1;
    }
    for (int i = 0; i < acount(world->assets); i++) {
        behavior_asset_record *asset = &world->assets[i];
        if (strcmp(asset->name, asset_name) != 0) {
            continue;
        }
        if (acount(world->attachments)) {
            return -1;
        }
        *definition = asset->definition;
        free(asset->name);
        destroy_asset_pool(asset);
        adel(world->assets, i);
        return 0;
    }
    return -1;
}

static int detach_asset_entity(game_behavior_world *world, behavior_asset_record *asset, ecs_entity entity);

static int begin_instance(behavior_attachment *attachment)
{
    flow_error error = { 0 };
    flow_run *begin = behavior_instance_dispatch(attachment->instance, "Begin", (flow_inputs){ 0 }, &error);
    if (!begin) {
        return error.code == FLOW_ERR_UNKNOWN_EVENT ? 0 : -1;
    }
    return 0;
}

static void destroy_asset_pool(behavior_asset_record *asset)
{
    for (int i = 0; i < acount(asset->pool); i++) {
        behavior_instance_destroy(asset->pool[i]);
    }
    afree(asset->pool);
}

int game_behavior_replace_asset(
    game_behavior_world *world,
    const char *asset_name,
    behavior_asset *definition,
    behavior_asset **old_definition
)
{
    behavior_asset_record *asset = world && asset_name ? find_asset(world, asset_name)
                                                       : NULL;
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
    behavior_asset *old = asset->definition;
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
    behavior_asset_record *asset = find_asset(world, asset_name);
    if (!asset) {
        return -1;
    }
    if (!asset->definition->owner_type_name) {
        if (behavior_asset_set_owner_type(asset->definition, "ecs.entity") != 0) {
            return -1;
        }
    } else if (strcmp(asset->definition->owner_type_name, "ecs.entity") != 0) {
        return -1;
    }
    behavior_instance *instance = acquire_instance(asset, world, entity);
    if (!instance) {
        return -1;
    }
    behavior_instance_set_parallel_for(instance, world->parallel_for, world->parallel_user);
    apush(
        world->attachments,
        (behavior_attachment){
            .entity = entity,
            .asset = asset,
            .instance = instance,
            .sequence = world->next_sequence++,
        }
    );
    behavior_attachment *attachment = &alast(world->attachments);
    if (begin_instance(attachment) != 0) {
        release_instance(asset, instance);
        adel(world->attachments, acount(world->attachments) - 1);
        return -1;
    }
    return 0;
}

static int detach_asset_entity(game_behavior_world *world, behavior_asset_record *asset, ecs_entity entity)
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

int game_behavior_detach_asset(game_behavior_world *world, ecs_entity entity, const char *asset_name)
{
    behavior_asset_record *asset = world && asset_name ? find_asset(world, asset_name)
                                                       : NULL;
    return asset ? detach_asset_entity(world, asset, entity) : -1;
}

int game_behavior_set_breakpoints_enabled_for_asset(
    game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name,
    int enabled
)
{
    behavior_instance *instance = game_behavior_instance(world, entity, asset_name);
    if (!instance) {
        return -1;
    }
    behavior_instance_set_breakpoints_enabled(instance, enabled);
    return 0;
}

int game_behavior_breakpoints_enabled_for_asset(
    const game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name
)
{
    behavior_instance *instance = game_behavior_instance((game_behavior_world *)world, entity, asset_name);
    return instance ? behavior_instance_breakpoints_enabled(instance) : 0;
}

int game_behavior_set_debug_hold(
    game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name,
    int enabled
)
{
    behavior_asset_record *asset = world && asset_name ? find_asset(world, asset_name)
                                                       : NULL;
    behavior_attachment *attachment = asset ? find_attachment(world, entity, asset_name)
                                            : NULL;
    if (!attachment) {
        return -1;
    }
    attachment->debug_hold = enabled != 0;
    return 0;
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

static int dispatch_attachment(
    behavior_attachment *attachment,
    const char *event_name,
    flow_inputs inputs,
    flow_error *error
)
{
    flow_error local = { 0 };
    flow_run *run = behavior_instance_dispatch(attachment->instance, event_name, inputs, &local);
    if (!run) {
        if (local.code == FLOW_ERR_UNKNOWN_EVENT) {
            return 0;
        }
        if (error) {
            *error = local;
        }
        return local.code;
    }
    if (!attachment->debug_hold && (flow_run_status(run) == FLOW_COMPLETED ||
                                    flow_run_status(run) == FLOW_FAILED)) {
        behavior_instance_release_activation(attachment->instance, run);
    }
    return 1;
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
        int result = dispatch_attachment(attachment, event_name, inputs, error);
        if (result < 0) {
            return result;
        }
        dispatched += result;
    }
    return dispatched;
}

int game_behavior_dispatch_all(
    game_behavior_world *world,
    const char *event_name,
    flow_inputs inputs,
    flow_error *error
)
{
    if (!world || !event_name) {
        return FLOW_ERR_ARGUMENT;
    }
    if (error) {
        *error = (flow_error){ 0 };
    }
    int dispatched = 0;
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_attachment *attachment = &world->attachments[i];
        if (!ecs_entity_is_alive(world->ecs, attachment->entity)) {
            continue;
        }
        int result = dispatch_attachment(attachment, event_name, inputs, error);
        if (result < 0) {
            return result;
        }
        dispatched += result;
    }
    return dispatched;
}

typedef struct
{
    game_behavior_world *world;
    double monotonic_time;
} behavior_pump_context;

static int instance_breakpoints_active(const behavior_instance *instance)
{
    if (!instance || !instance->bp || !instance->bp->graph) {
        return 0;
    }
    if (instance->breakpoints_override) {
        return instance->breakpoints_enabled;
    }
    const graph *graph = instance->bp->graph;
    if (!graph->breakpoints_enabled) {
        return 0;
    }
    for (int i = 0; i < acount(graph->nodes); i++) {
        if (graph->nodes[i]->breakpoint) {
            return 1;
        }
    }
    return 0;
}

static void pump_attachment(int index, void *user)
{
    behavior_pump_context *context = (behavior_pump_context *)user;
    behavior_attachment *attachment = &context->world->attachments[index];
    context->world->pump_results[index] = FLOW_OK;
    if (!ecs_entity_is_alive(context->world->ecs, attachment->entity)) {
        return;
    }
    int result = behavior_instance_tick(attachment->instance, context->monotonic_time);
    if (result < 0) {
        context->world->pump_results[index] = result;
    }
}

int game_behavior_pump(game_behavior_world *world, double monotonic_time)
{
    if (!world) {
        return FLOW_ERR_ARGUMENT;
    }
    int count = acount(world->attachments);
    int breakpoints_enabled = world->breakpoints_enabled;
    for (int i = 0; i < count && !breakpoints_enabled; i++) {
        breakpoints_enabled = instance_breakpoints_active(world->attachments[i].instance);
    }
    if (count > world->pump_result_capacity) {
        world->pump_results = (int *)
            xrealloc(world->pump_results, (size_t)count * sizeof *world->pump_results);
        world->pump_result_capacity = count;
    }
    behavior_pump_context context = { world, monotonic_time };
    if (world->parallel_for && !breakpoints_enabled) {
        world->parallel_for(count, pump_attachment, &context);
    } else {
        for (int i = 0; i < count; i++) {
            pump_attachment(i, &context);
        }
    }
    int result = FLOW_OK;
    for (int i = 0; i < count; i++) {
        if (world->pump_results[i] < 0) {
            result = world->pump_results[i];
        }
        if (!world->attachments[i].debug_hold) {
            reap_instance(world->attachments[i].instance);
        }
    }
    return result;
}

void game_behavior_set_delta_seconds(game_behavior_world *world, double delta_seconds)
{
    if (!world) {
        return;
    }
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_instance_set_delta_seconds(world->attachments[i].instance, delta_seconds);
    }
}

void game_behavior_set_parallel_for(
    game_behavior_world *world,
    flow_parallel_for_fn parallel_for,
    void *parallel_user
)
{
    if (!world) {
        return;
    }
    world->parallel_for = parallel_for;
    world->parallel_user = parallel_user;
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_instance_set_parallel_for(world->attachments[i].instance, parallel_for, parallel_user);
    }
}

void game_behavior_set_breakpoints_enabled(game_behavior_world *world, int enabled)
{
    if (world) {
        world->breakpoints_enabled = enabled != 0;
    }
}

void game_behavior_set_sink(game_behavior_world *world, flow_sink_fn sink, void *user)
{
    if (!world) {
        return;
    }
    world->sink = sink;
    world->sink_user = user;
}

int game_behavior_log_count(const game_behavior_world *world)
{
    return world ? acount(world->logs) : 0;
}

int game_behavior_log_info_at(const game_behavior_world *world, int index, game_behavior_log_info *out)
{
    if (!world || !out || index < 0 || index >= acount(world->logs)) {
        return -1;
    }
    *out = world->logs[index];
    return 0;
}

void game_behavior_clear_logs(game_behavior_world *world)
{
    if (world) {
        aclear(world->logs);
    }
}

int game_behavior_attachment_count(const game_behavior_world *world)
{
    return world ? acount(world->attachments) : 0;
}

int game_behavior_attachment_count_for_entity(const game_behavior_world *world, ecs_entity entity)
{
    if (!world) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < acount(world->attachments); i++) {
        const behavior_attachment *attachment = &world->attachments[i];
        if (attachment->entity.slot == entity.slot &&
            attachment->entity.generation == entity.generation) {
            count++;
        }
    }
    return count;
}

int game_behavior_attachment_info_at(
    game_behavior_world *world,
    ecs_entity entity,
    int index,
    game_behavior_attachment_info *out
)
{
    if (!world || !out || index < 0) {
        return -1;
    }
    for (int i = 0; i < acount(world->attachments); i++) {
        behavior_attachment *attachment = &world->attachments[i];
        if (attachment->entity.slot != entity.slot ||
            attachment->entity.generation != entity.generation) {
            continue;
        }
        if (index-- != 0) {
            continue;
        }
        *out = (game_behavior_attachment_info){
            .entity = attachment->entity,
            .asset_name = attachment->asset->name,
            .definition = attachment->asset->definition,
            .instance = attachment->instance,
            .sequence = attachment->sequence,
            .debug_hold = attachment->debug_hold,
        };
        return 0;
    }
    return -1;
}

int game_behavior_entity_state_at(
    const game_behavior_world *world,
    ecs_entity entity,
    const char *asset_name,
    game_behavior_entity_state *out
)
{
    behavior_attachment *attachment = world && asset_name
                                        ? find_attachment((game_behavior_world *)world, entity, asset_name)
                                        : NULL;
    if (!attachment || !out) {
        return -1;
    }
    *out = (game_behavior_entity_state){ .entity = entity };
    for (int i = 0; i < acount(attachment->instance->activations); i++) {
        const flow_run *run = attachment->instance->activations[i];
        flow_state state = flow_run_status(run);
        if (state == FLOW_COMPLETED || state == FLOW_FAILED || state == FLOW_IDLE) {
            continue;
        }
        out->active = 1;
        out->suspended = state == FLOW_SUSPENDED;
        if (out->suspended) {
            out->wake_time = flow_run_wake_time(run);
        }
        return 0;
    }
    return 0;
}

int game_behavior_asset_registered(const game_behavior_world *world, const char *asset_name)
{
    return world && asset_name &&
           find_asset((game_behavior_world *)world, asset_name) != NULL;
}

int game_behavior_asset_count(const game_behavior_world *world)
{
    return world ? acount(world->assets) : 0;
}

const char *game_behavior_asset_name_at(const game_behavior_world *world, int index)
{
    return world && index >= 0 && index < acount(world->assets)
             ? world->assets[index].name
             : NULL;
}

const behavior_asset *game_behavior_asset_definition_at(const game_behavior_world *world, int index)
{
    return world && index >= 0 && index < acount(world->assets)
             ? world->assets[index].definition
             : NULL;
}

int game_behavior_pool_size(const game_behavior_world *world, const char *asset_name)
{
    behavior_asset_record *asset = world && asset_name
                                     ? find_asset((game_behavior_world *)world, asset_name)
                                     : NULL;
    return asset ? acount(asset->pool) : 0;
}

behavior_instance *game_behavior_instance(game_behavior_world *world, ecs_entity entity, const char *asset_name)
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
    behavior_asset_record *asset = world && asset_name ? find_asset(world, asset_name)
                                                       : NULL;
    if (!asset || !promote ||
        !behavior_asset_find_variable_by_id(asset->definition, variable_id)) {
        return -1;
    }
    return promote(asset->definition, variable_id, component_id, user);
}
