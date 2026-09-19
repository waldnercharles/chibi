#include "runtime/behaviors/entities.h"

#include "graph/model/registry.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/flow_internal.h"
#include "runtime/behaviors/live.h"
#include "runtime/engine.h"
#include "runtime/generated.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/simulation_fixture.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static node *add(graph *g, const char *name)
{
    const node_desc *desc = registry_node_by_name(name);
    assert(desc);
    return graph_add_node(g, desc);
}

static void data(graph *g, node *from, int output, node *to, int input)
{
    assert(graph_connect(g, from, output, to, input) == 0);
}

static void next(graph *g, node *from, node *to)
{
    assert(graph_connect_exec(g, from, 0, to, 0, 0) == 0);
}

static void input(graph *g, node *n, int index, pin_value value)
{
    assert(graph_set_pin_default(g, n, 0, index, &value) == 0);
}

static behavior_variable *variable(behavior_asset *asset, const char *name, pin_type type, const char *type_name)
{
    pin_value zero = { .type = type, .type_name = type_name };
    behavior_variable *v = behavior_asset_add_variable(
        asset,
        name,
        type,
        type_name,
        PIN_CONTAINER_SINGLE,
        &zero,
        "Test"
    );
    assert(v);
    return v;
}

static pin_value value(behavior_instance *instance, const char *name)
{
    for (int i = 0; i < acount(instance->asset->variables); i++) {
        behavior_variable *v = &instance->asset->variables[i];
        if (strcmp(v->name, name) == 0) {
            pin_value result;
            assert(behavior_instance_get_variable(instance, v->id, &result) == 0);
            return result;
        }
    }
    assert(!"missing test variable");
    return (pin_value){ 0 };
}

static behavior_asset *child_asset()
{
    behavior_asset *asset = behavior_asset_new("Child");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    uint64_t observed = variable(asset, "ObservedX", PIN_F32, "float")->id;
    graph *g = asset->graph;
    node *begin = add(g, "Event");
    assert(graph_set_event_name(g, begin, "Begin") == 0);
    node *get = add(g, "Get_GameTransform");
    node *split = add(g, "Break_GameTransform");
    node *set = behavior_asset_add_set_node(asset, observed);
    node *log = add(g, "Print String");
    input(g, log, 1, (pin_value){ .type = PIN_STR, .str = "child began" });
    data(g, get, 0, split, 0);
    data(g, split, 0, set, 1);
    next(g, begin, set);
    next(g, set, log);
    return asset;
}

static behavior_asset *parent_asset(bool pause, bool destroy)
{
    behavior_asset *asset = behavior_asset_new("Parent");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    uint64_t spawned = variable(asset, "Spawned", PIN_HANDLE, "ecs.entity")->id;
    uint64_t read_x = variable(asset, "ReadX", PIN_F32, "float")->id;
    graph *g = asset->graph;
    node *fire = add(g, "Event");
    assert(graph_set_event_name(g, fire, "Fire") == 0);
    node *spawn = add(g, "Spawn Prototype");
    input(g, spawn, 1, (pin_value){ .type = PIN_NAME, .str = "ChildPrototype" });
    node *make = add(g, "Make_GameTransform");
    input(g, make, 0, (pin_value){ .type = PIN_F32, .f = 73.0f });
    input(g, make, 1, (pin_value){ .type = PIN_F32, .f = -4.0f });
    node *initialize = add(g, "SetEntity_GameTransform");
    assert(graph_set_breakpoint(g, initialize, pause) == 0);
    node *remember = behavior_asset_add_set_node(asset, spawned);
    node *get = add(g, "GetEntity_GameTransform");
    node *split = add(g, "Break_GameTransform");
    node *read = behavior_asset_add_set_node(asset, read_x);
    next(g, fire, spawn);
    next(g, spawn, initialize);
    data(g, spawn, 1, initialize, 1);
    data(g, make, 0, initialize, 2);
    next(g, initialize, remember);
    data(g, spawn, 1, remember, 1);
    next(g, remember, read);
    data(g, spawn, 1, get, 0);
    data(g, get, 0, split, 0);
    data(g, split, 0, read, 1);
    if (destroy) {
        node *remove = add(g, "Destroy Entity");
        next(g, read, remove);
        data(g, spawn, 1, remove, 1);
    }
    return asset;
}

static int entity_count(ecs_world *world)
{
    ecs_query query;
    ecs_entity entity;
    assert(ecs_query_begin(world, &query, NULL, 0) == ECS_OK);
    int count = 0;
    while (ecs_query_next(&query, &entity)) {
        count++;
    }
    ecs_query_end(&query);
    return count;
}

static void run_spawn_case(bool cancel_before, int pause, bool destroy)
{
    ecs_world *ecs = ecs_world_new();
    assert(ecs_world_register_component(ecs, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
    game_live_world *live = game_live_world_new(ecs);
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char error[256];
    game_prototype *prototype = game_prototype_load(
        "{\"name\":\"ChildPrototype\",\"components\":{\"GameTransform\":{\"x\":"
        "1,\"y\":2}},\"behaviors\":[\"child\"]}",
        &registry,
        error,
        sizeof error
    );
    assert(prototype);
    assert(game_entities_register(live->entities, prototype) == 0);
    assert(game_live_add_definition(live, game_live_definition_new("child", child_asset())) == 0);
    assert(
        game_live_add_definition(live, game_live_definition_new("parent", parent_asset(pause, destroy))) == 0
    );
    ecs_entity owner = ecs_world_create_entity(ecs);
    assert(game_behavior_attach(live->behaviors, owner, "parent") == 0);
    behavior_instance *parent = game_behavior_instance(live->behaviors, owner, "parent");
    behavior_instance_set_breakpoints_enabled(parent, pause);
    uint64_t eval_before = eval_oracle_calls(), flow_before = flow_oracle_calls();
    flow_error dispatch_error = { 0 };
    int dispatched = game_behavior_dispatch(live->behaviors, owner, "Fire", (flow_inputs){ 0 }, &dispatch_error);
    if (dispatched != 1) {
        fprintf(
            stderr,
            "entity test Fire failed: %d %s\n",
            dispatched,
            dispatch_error.message ? dispatch_error.message : "no diagnostic"
        );
    }
    assert(dispatched == 1);
    assert(acount(parent->activations) == 1);
    flow_run *run = parent->activations[0];
    if (behavior_instance_backend(parent) != BEHAVIOR_ORACLE) {
        if (!run->whole_function_executed) {
            bool duplicate;
            node *entry = behavior_program_event(parent->asset->program, "Fire", &duplicate);
            const behavior_function_diagnostic *d = behavior_program_function_diagnostic(
                behavior_program_graph(parent->asset->program, parent->asset->graph),
                entry
            );
            fprintf(
                stderr,
                "Spawn whole exclusion: %s node=%s pin=%d\n",
                d ? d->reason : "none",
                d ? d->node->desc->name : "none",
                d ? d->pin : -1
            );
        }
        assert(run->whole_function_executed && run->compiled);
    }
    assert(flow_run_waiting_for_host(run));
    assert(entity_count(ecs) == 1);
    assert(game_behavior_attachment_count(live->behaviors) == 1);
    if (cancel_before) {
        assert(behavior_instance_stop_activation(parent, run) == 0);
    }
    int blocked = game_entities_flush(live->entities);
    if (behavior_instance_backend(parent) != BEHAVIOR_ORACLE) {
        assert(eval_before == eval_oracle_calls() && flow_before == flow_oracle_calls());
    }
    if (cancel_before || destroy) {
        assert(blocked == 0);
        assert(entity_count(ecs) == 1);
        assert(game_behavior_attachment_count(live->behaviors) == 1);
        assert(game_behavior_log_count(live->behaviors) == 0);
    } else if (pause) {
        assert(blocked > 0 && game_entities_initializing(live->entities));
        assert(entity_count(ecs) == 2);
        assert(flow_run_status(run) == FLOW_PAUSED);
        assert(game_behavior_attachment_count(live->behaviors) == 1);
        assert(game_behavior_log_count(live->behaviors) == 0);
        if (pause == 2) {
            assert(flow_run_resume(run) == FLOW_OK);
            assert(game_entities_flush(live->entities) == 0);
            assert(game_behavior_attachment_count(live->behaviors) == 2);
            assert(game_behavior_log_count(live->behaviors) == 1);
            game_entities_reset(live->entities);
        } else if (pause == 3) {
            game_entities_reset(live->entities);
        } else {
            assert(behavior_instance_stop_activation(parent, run) == 0);
        }
        assert(game_entities_flush(live->entities) == 0);
        assert(entity_count(ecs) == 1);
        assert(game_behavior_attachment_count(live->behaviors) == 1);
        assert(!game_entities_initializing(live->entities));
    } else {
        assert(blocked == 0 && entity_count(ecs) == 2);
        ecs_entity child = ecs_entity_from_u64(value(parent, "Spawned").u64);
        assert(ecs_entity_is_alive(ecs, child));
        ecs_entity *owned = NULL;
        game_entities_copy_live(live->entities, &owned);
        assert(acount(owned) == 1 && ecs_entity_to_u64(owned[0]) == ecs_entity_to_u64(child));
        assert(game_entities_owns(live->entities, child));
        assert(!game_entities_owns(live->entities, owner));
        behavior_instance *instance = game_behavior_instance(live->behaviors, child, "child");
        assert(instance && value(instance, "ObservedX").f == 73.0f);
        assert(value(parent, "ReadX").f == 73.0f);
        assert(GAME_GameTransform_get(ecs, child)->y == -4.0f);
        assert(game_behavior_log_count(live->behaviors) == 1);
        assert(game_entities_flush(live->entities) == 0);
        assert(game_behavior_log_count(live->behaviors) == 1);
        game_entities_request_destroy(live->entities, child);
        assert(ecs_entity_is_alive(ecs, child));
        assert(game_behavior_attachment_count(live->behaviors) == 2);
        assert(game_entities_flush(live->entities) == 0);
        assert(!ecs_entity_is_alive(ecs, child));
        game_entities_copy_live(live->entities, &owned);
        assert(acount(owned) == 0 && !game_entities_owns(live->entities, child));
        afree(owned);
        assert(game_behavior_attachment_count(live->behaviors) == 1);
        graph g = { 0 };
        node *get = add(&g, "GetEntity_GameTransform");
        input(
            &g,
            get,
            0,
            (pin_value){ .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(child) }
        );
        arena a;
        arena_init(&a, 1024);
        eval_context ctx;
        eval_context_init(&ctx);
        ctx.instance = parent;
        ctx.services = parent->services;
        pin_value result;
        assert(graph_eval_output(&g, get, 0, &a, &result, &ctx) < 0);
        assert(strstr(eval_context_status(&ctx)->message, "live entity"));
        node *has = add(&g, "HasEntity_GameTransform");
        input(
            &g,
            has,
            0,
            (pin_value){ .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(owner) }
        );
        eval_context_reset_runtime(&ctx);
        assert(graph_eval_output(&g, has, 0, &a, &result, &ctx) == 0 && !result.b);
        input(
            &g,
            get,
            0,
            (pin_value){ .type = PIN_HANDLE,
                         .type_name = "ecs.entity",
                         .u64 = ecs_entity_to_u64(owner) }
        );
        eval_context_reset_runtime(&ctx);
        assert(graph_eval_output(&g, get, 0, &a, &result, &ctx) < 0);
        assert(strstr(eval_context_status(&ctx)->message, "lacks component"));
        eval_context_free(&ctx);
        arena_free(&a);
        graph_free(&g);
        assert(game_entities_register(live->entities, prototype) == 0);
        for (int i = 0; i < 65; i++) {
            assert(
                game_behavior_dispatch(live->behaviors, owner, "Fire", (flow_inputs){ 0 }, NULL) == 1
            );
        }
        assert(entity_count(ecs) == 1);
        assert(game_entities_flush(live->entities) == 0);
        assert(entity_count(ecs) == 65);
        assert(game_entities_flush(live->entities) == 0);
        assert(entity_count(ecs) == 66);
        game_entities_reset(live->entities);
        assert(entity_count(ecs) == 1);
        assert(game_behavior_attachment_count(live->behaviors) == 1);
        assert(
            game_behavior_dispatch(live->behaviors, owner, "Fire", (flow_inputs){ 0 }, NULL) == 1
        );
        game_entities_reset(live->entities);
        assert(game_entities_flush(live->entities) == 0 && entity_count(ecs) == 1);
        assert(
            game_behavior_dispatch(live->behaviors, owner, "Fire", (flow_inputs){ 0 }, NULL) == 1
        );
        assert(game_entities_flush(live->entities) == 0 && entity_count(ecs) == 2);
        owned = NULL;
        game_entities_copy_live(live->entities, &owned);
        assert(acount(owned) == 1);
        ecs_entity retired = owned[0];
        assert(game_behavior_destroy_entity(live->behaviors, retired) == ECS_OK);
        ecs_entity replacement = ecs_world_create_entity(ecs);
        assert(
            replacement.slot == retired.slot &&
            replacement.generation != retired.generation
        );
        assert(!game_entities_owns(live->entities, replacement));
        game_entities_copy_live(live->entities, &owned);
        assert(acount(owned) == 0);
        afree(owned);
        game_entities_reset(live->entities);
        assert(ecs_entity_is_alive(ecs, replacement));
        assert(game_behavior_destroy_entity(live->behaviors, replacement) == ECS_OK);
        assert(entity_count(ecs) == 1);
    }
    game_prototype_free(prototype);
    game_live_world_free(live);
    ecs_world_free(ecs);
}

typedef struct event_observer
{
    game_entities *entities;
    ecs_entity target;
    int count;
    int sequence[128];
    bool recurse;
} event_observer;

static int observe_event(node *, flow_context *ctx)
{
    event_observer *observer = flow_context_service(ctx, "test.event_observer");
    assert(observer);
    flow_inputs inputs = flow_context_inputs(ctx);
    assert(inputs.count == 3);
    assert(inputs.values[0].type == PIN_STR);
    assert(strcmp(inputs.values[0].str, "owned message") == 0);
    assert(strcmp(inputs.values[0].type_name, "string") == 0);
    assert(inputs.values[1].type == PIN_HANDLE);
    assert(strcmp(inputs.values[1].type_name, "ecs.entity") == 0);
    assert(inputs.values[1].u64 == ecs_entity_to_u64(observer->target));
    assert(inputs.values[2].type == PIN_I32);
    assert(observer->count < 128);
    observer->sequence[observer->count++] = inputs.values[2].i;
    if (observer->recurse) {
        observer->recurse = false;
        pin_value followup[] = { inputs.values[0],
                                 inputs.values[1],
                                 { .type = PIN_I32, .type_name = "int", .i = 99 } };
        assert(
            game_entities_request_event(
                observer->entities,
                observer->target,
                "Queued",
                (flow_inputs){ .values = followup, .count = 3 }
            ) == FLOW_OK
        );
    }
    return FLOW_OK;
}

static pin_spec observe_inputs[] = { { .name = "Exec", .kind = PIN_EXEC } };
static const node_desc observe_desc = {
    .name = "ObserveQueuedEventTest",
    .inputs = observe_inputs,
    .input_count = 1,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = observe_event,
};

static behavior_asset *event_asset(bool pause)
{
    behavior_asset *asset = behavior_asset_new("QueuedEvents");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    graph *g = asset->graph;
    node *event = add(g, "Event");
    assert(graph_set_event_name(g, event, "Queued") == 0);
    pin_spec parameters[] = {
        { .name = "Text", .type = PIN_STR, .type_name = "string", .kind = PIN_DATA },
        { .name = "Source", .type = PIN_HANDLE, .type_name = "ecs.entity", .kind = PIN_DATA },
        { .name = "Sequence", .type = PIN_I32, .type_name = "int", .kind = PIN_DATA },
    };
    for (int i = 0; i < 3; i++) {
        assert(graph_add_pin(g, event, 1, &parameters[i]) >= 0);
    }
    node *observe = graph_add_node(g, &observe_desc);
    assert(graph_set_breakpoint(g, observe, pause) == 0);
    next(g, event, observe);
    return asset;
}

static void queue_owned_event(game_entities *entities, ecs_entity target, int sequence)
{
    char *name = xstrdup("Queued");
    char *text = xstrdup("owned message");
    char *text_type = xstrdup("string");
    char *entity_type = xstrdup("ecs.entity");
    pin_value values[] = {
        { .type = PIN_STR, .type_name = text_type, .str = text },
        { .type = PIN_HANDLE, .type_name = entity_type, .u64 = ecs_entity_to_u64(target) },
        { .type = PIN_I32, .type_name = "int", .i = sequence },
    };
    assert(
        game_entities_request_event(entities, target, name, (flow_inputs){ .values = values, .count = 3 }) ==
        FLOW_OK
    );
    memset(name, '?', strlen(name));
    memset(text, '?', strlen(text));
    memset(text_type, '?', strlen(text_type));
    memset(entity_type, '?', strlen(entity_type));
    memset(values, 0, sizeof values);
    free(name);
    free(text);
    free(text_type);
    free(entity_type);
}

static void test_queued_events(bool pause)
{
    ecs_world *ecs = ecs_world_new();
    game_live_world *live = game_live_world_new(ecs);
    behavior_asset *asset = event_asset(pause);
    assert(game_behavior_register_asset(live->behaviors, "events", asset) == 0);
    ecs_entity target = ecs_world_create_entity(ecs);
    assert(game_behavior_attach(live->behaviors, target, "events") == 0);
    behavior_instance *instance = game_behavior_instance(live->behaviors, target, "events");
    event_observer observer = { .entities = live->entities, .target = target };
    assert(behavior_instance_bind_service(instance, "test.event_observer", &observer) == 0);
    behavior_instance_set_breakpoints_enabled(instance, pause);
    queue_owned_event(live->entities, target, 0);
    assert(observer.count == 0);
    assert(game_entities_dispatch_events(live->entities) == FLOW_OK);
    if (pause) {
        assert(observer.count == 0 && acount(instance->activations) == 1);
        flow_run *run = instance->activations[0];
        assert(flow_run_status(run) == FLOW_PAUSED);
        assert(strcmp(flow_run_event_name(run), "Queued") == 0);
        /* The queue has freed its request; activation inputs must own all metadata. */
        assert(flow_run_resume(run) == FLOW_OK);
        assert(observer.count == 1 && observer.sequence[0] == 0);
        game_live_world_free(live);
        behavior_asset_free(asset);
        ecs_world_free(ecs);
        return;
    }
    assert(observer.count == 1 && observer.sequence[0] == 0);
    observer.count = 0;
    observer.recurse = true;
    for (int i = 0; i < 65; i++) {
        queue_owned_event(live->entities, target, i);
    }
    assert(game_entities_dispatch_events(live->entities) == FLOW_OK);
    assert(observer.count == 64);
    for (int i = 0; i < 64; i++) {
        assert(observer.sequence[i] == i);
    }
    assert(game_entities_dispatch_events(live->entities) == FLOW_OK);
    assert(observer.count == 66);
    assert(observer.sequence[64] == 64 && observer.sequence[65] == 99);
    queue_owned_event(live->entities, target, 100);
    game_entities_reset(live->entities);
    assert(game_entities_dispatch_events(live->entities) == FLOW_OK && observer.count == 66);
    queue_owned_event(live->entities, target, 101);
    game_entities_request_destroy(live->entities, target);
    assert(game_entities_dispatch_events(live->entities) == FLOW_OK);
    assert(observer.count == 66);
    assert(game_entities_flush(live->entities) == FLOW_OK);
    assert(!ecs_entity_is_alive(ecs, target));
    queue_owned_event(live->entities, target, 102);
    assert(game_entities_dispatch_events(live->entities) == FLOW_OK && observer.count == 66);
    game_live_world_free(live);
    behavior_asset_free(asset);
    ecs_world_free(ecs);
}

static behavior_asset *delayed_spawn_parent()
{
    behavior_asset *asset = behavior_asset_new("DelayedParent");
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    graph *function = behavior_asset_add_function(asset, "Create Later", 0);
    node *entry = graph_node_by_id(function, function->entry_id);
    node *ret = graph_node_by_id(function, function->return_id);
    pin_spec entity = { .name = "Entity",
                        .type = PIN_HANDLE,
                        .type_name = "ecs.entity",
                        .kind = PIN_DATA };
    assert(graph_add_pin(function, ret, 0, &entity) == 1);
    node *spawn = add(function, "Spawn Prototype");
    input(function, spawn, 1, (pin_value){ .type = PIN_NAME, .str = "ChildPrototype" });
    node *delay = add(function, "Delay");
    input(function, delay, 1, (pin_value){ .type = PIN_F64, .d = 0.5 });
    next(function, entry, spawn);
    next(function, spawn, delay);
    next(function, delay, ret);
    data(function, spawn, 1, ret, 1);
    graph *g = asset->graph;
    node *fire = add(g, "Event");
    assert(graph_set_event_name(g, fire, "Fire") == 0);
    node *call = behavior_asset_add_function_call_node(asset, function);
    node *make = add(g, "Make_GameTransform");
    input(g, make, 0, (pin_value){ .type = PIN_F32, .f = 73 });
    node *initialize = add(g, "SetEntity_GameTransform");
    assert(graph_set_breakpoint(g, initialize, true) == 0);
    next(g, fire, call);
    next(g, call, initialize);
    data(g, call, 1, initialize, 1);
    data(g, make, 0, initialize, 2);
    return asset;
}

static int attach_spawn_parent(ecs_world *, ecs_entity entity, const char *name, void *world)
{
    return game_behavior_attach(world, entity, name);
}

static void delayed_spawn_case(bool isolated, int mode)
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char error[256];
    game_prototype *child = game_prototype_load(
        "{\"name\":\"ChildPrototype\",\"components\":{\"GameTransform\":{\"x\":"
        "1,\"y\":2}},\"behaviors\":[\"child\"]}",
        &registry,
        error,
        sizeof error
    );
    game_prototype *prototype = game_prototype_load(
        "{\"name\":\"ParentPrototype\",\"components\":{},\"behaviors\":["
        "\"parent\"]}",
        &registry,
        error,
        sizeof error
    );
    assert(child && prototype);
    game_prototype_preview *preview = isolated ? game_prototype_preview_new(&registry)
                                               : NULL;
    ecs_world *world = isolated ? game_prototype_preview_world(preview)
                                : ecs_world_new();
    game_live_world *live = NULL;
    engine *runtime = NULL;
    behavior_asset *parent_asset = delayed_spawn_parent();
    behavior_asset *child_behavior = child_asset();
    ecs_entity owner;
    if (isolated) {
        assert(game_prototype_preview_register_behavior(preview, "parent", parent_asset) == 0);
        assert(game_prototype_preview_register_behavior(preview, "child", child_behavior) == 0);
        behavior_asset_free(parent_asset);
        behavior_asset_free(child_behavior);
        assert(game_prototype_preview_register_prototype(preview, child) == 0);
        assert(game_prototype_preview_launch(preview, prototype, error, sizeof error) == 0);
        owner = game_prototype_preview_entity(preview);
    } else {
        assert(ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL) == ECS_OK);
        live = game_live_world_new(world);
        runtime = engine_new(world, live);
        assert(game_live_add_definition(live, game_live_definition_new("parent", parent_asset)) == 0);
        assert(
            game_live_add_definition(live, game_live_definition_new("child", child_behavior)) == 0
        );
        assert(game_entities_register(live->entities, child) == 0);
        assert(
            game_prototype_spawn(
                prototype,
                world,
                &(game_prototype_spawn_hooks){ attach_spawn_parent, live->behaviors },
                &owner,
                error,
                sizeof error
            ) == 0
        );
        assert(engine_set_playing(runtime, true) == 0);
    }
    game_behavior_world *behaviors = isolated
                                       ? game_prototype_preview_behaviors(preview)
                                       : live->behaviors;
    behavior_instance *parent = game_behavior_instance(behaviors, owner, "parent");
    assert(parent);
    behavior_instance_set_breakpoints_enabled(parent, mode == 1);
    simulation_fixture simulation = simulation_fixture_new(runtime, preview);
    uint64_t eval_before = eval_oracle_calls(), flow_before = flow_oracle_calls();
    assert(game_behavior_dispatch(behaviors, owner, "Fire", (flow_inputs){ 0 }, NULL) == 1);
    assert(acount(parent->activations) == 1);
    flow_run *run = parent->activations[0];
    simulation_frame(&simulation, 0);
    if (behavior_instance_backend(parent) != BEHAVIOR_ORACLE) {
        assert(run->whole_function_executed && run->compiled);
    }
    ecs_query query;
    ecs_component_id transform = GAME_COMPONENT_GameTransform.id;
    ecs_entity spawned;
    assert(ecs_query_begin(world, &query, &transform, 1) == ECS_OK);
    assert(ecs_query_next(&query, &spawned));
    ecs_query_end(&query);
    assert(GAME_GameTransform_get(world, spawned)->x == 1);
    assert(
        game_behavior_attachment_count(behaviors) == 1 &&
        game_behavior_log_count(behaviors) == 0
    );
    simulation_advance(&simulation, .25);
    assert(
        game_behavior_attachment_count(behaviors) == 1 &&
        game_behavior_log_count(behaviors) == 0
    );
    if (mode == 2) {
        assert(behavior_instance_stop_activation(parent, run) == 0);
        simulation_advance(&simulation, simulation.time + .5);
        assert(!ecs_entity_is_alive(world, spawned));
        assert(
            game_behavior_attachment_count(behaviors) == 1 &&
            game_behavior_log_count(behaviors) == 0
        );
    } else {
        if (!isolated) {
            assert(engine_set_playing(runtime, false) == 0);
            simulation_advance(&simulation, 1);
            assert(
                game_behavior_attachment_count(behaviors) == 1 &&
                game_behavior_log_count(behaviors) == 0
            );
            assert(GAME_GameTransform_get(world, spawned)->x == 1);
            assert(engine_set_playing(runtime, true) == 0);
        }
        simulation_advance(&simulation, simulation.time + .5);
        if (mode == 1) {
            assert(flow_run_status(run) == FLOW_PAUSED);
            assert(
                game_behavior_attachment_count(behaviors) == 1 &&
                game_behavior_log_count(behaviors) == 0
            );
            assert(GAME_GameTransform_get(world, spawned)->x == 1);
            double held_time = engine_last_timing(simulation.runtime)->simulation_time;
            simulation_advance(&simulation, simulation.time + 1);
            assert(engine_last_timing(simulation.runtime)->simulation_time == held_time);
            assert(
                flow_run_status(run) == FLOW_PAUSED &&
                game_behavior_log_count(behaviors) == 0
            );
            assert(flow_run_resume(run) == FLOW_OK);
            simulation_frame(&simulation, simulation.time);
        }
        assert(
            game_behavior_attachment_count(behaviors) == 2 &&
            game_behavior_log_count(behaviors) == 1
        );
        behavior_instance *instance = game_behavior_instance(behaviors, spawned, "child");
        assert(instance && value(instance, "ObservedX").f == 73);
    }
    if (behavior_instance_backend(parent) != BEHAVIOR_ORACLE) {
        assert(eval_before == eval_oracle_calls() && flow_before == flow_oracle_calls());
    }
    if (isolated) {
        game_prototype_preview_free(preview);
    } else {
        engine_free(runtime);
        game_live_world_free(live);
        ecs_world_free(world);
    }
    game_prototype_free(child);
    game_prototype_free(prototype);
}

static behavior_asset *attachment_asset(const char *name)
{
    behavior_asset *asset = behavior_asset_new(name);
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    uint64_t state = variable(asset, "State", PIN_I32, "int")->id;
    node *begin = add(asset->graph, "Event");
    assert(graph_set_event_name(asset->graph, begin, "Begin") == 0);
    node *set = behavior_asset_add_set_node(asset, state);
    input(asset->graph, set, 1, (pin_value){ .type = PIN_I32, .i = 9, .type_name = "int" });
    next(asset->graph, begin, set);
    return asset;
}

static void test_attachment_lists()
{
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    behavior_asset *a = attachment_asset("A"), *b = attachment_asset("B");
    assert(game_behavior_register_asset(world, "A", a) == 0);
    assert(game_behavior_register_asset(world, "B", b) == 0);
    ecs_entity entity = ecs_world_create_entity(ecs);
    const char *ab[] = { "A", "B" }, *ba[] = { "B", "A" };
    assert(game_behavior_set_attachments(world, entity, ab, 2) == 0);
    behavior_instance *original = game_behavior_instance(world, entity, "A");
    assert(original && value(original, "State").i == 9);
    pin_value changed = { .type = PIN_I32, .i = 77, .type_name = "int" };
    assert(behavior_instance_set_variable(original, a->variables[0].id, &changed) == 0);
    assert(game_behavior_begin_pending(world) == 0);
    assert(value(original, "State").i == 77);
    assert(game_behavior_set_attachments(world, entity, ba, 2) == 0);
    assert(game_behavior_begin_pending(world) == 0);
    assert(value(original, "State").i == 77);
    game_behavior_attachment_info info;
    assert(game_behavior_attachment_info_at(world, entity, 0, &info) == 0);
    assert(strcmp(info.asset_name, "B") == 0);
    assert(game_behavior_attachment_info_at(world, entity, 1, &info) == 0);
    assert(info.instance == original && value(original, "State").i == 77);
    /* Child Begin reads a required Transform that this entity does not have.
     * Dispatch returns a failed run, not NULL; old A/B must survive unchanged. */
    behavior_asset *broken = child_asset();
    assert(game_behavior_register_asset(world, "Broken", broken) == 0);
    const char *failing[] = { "Broken" };
    assert(game_behavior_set_attachments(world, entity, failing, 1) == -1);
    assert(game_behavior_instance(world, entity, "Broken") == NULL);
    const char *invalid[] = { "A", "Missing" }, *duplicate[] = { "A", "A" };
    assert(game_behavior_set_attachments(world, entity, invalid, 2) == -1);
    assert(game_behavior_set_attachments(world, entity, duplicate, 2) == -1);
    assert(game_behavior_attachment_count_for_entity(world, entity) == 2);
    assert(game_behavior_attachment_info_at(world, entity, 0, &info) == 0);
    assert(strcmp(info.asset_name, "B") == 0);
    assert(game_behavior_instance(world, entity, "A") == original);
    assert(value(original, "State").i == 77);
    assert(game_behavior_set_attachments(world, entity, ab, 1) == 0);
    assert(game_behavior_attachment_count_for_entity(world, entity) == 1);
    assert(game_behavior_instance(world, entity, "A") == original);
    assert(game_behavior_set_attachments(world, entity, NULL, 0) == 0);
    assert(game_behavior_attachment_count_for_entity(world, entity) == 0);
    assert(ecs_world_destroy_entity(ecs, entity) == ECS_OK);
    assert(game_behavior_set_attachments(world, entity, ab, 1) == -1);
    game_behavior_world_free(world);
    behavior_asset_free(a);
    behavior_asset_free(b);
    behavior_asset_free(broken);
    ecs_world_free(ecs);
    puts("attachment lists PASS: ordered replay retains state and rejects invalid sets atomically");
}

static void test_attachment_index()
{
    behavior_backend modes[] = { BEHAVIOR_SLJIT, BEHAVIOR_BYTECODE, BEHAVIOR_ORACLE };
    for (int mode = 0; mode < 3; mode++) {
        behavior_backend previous = behavior_debug_backend(modes[mode]);
        ecs_world *ecs = ecs_world_new();
        game_behavior_world *world = game_behavior_world_new(ecs);
        behavior_asset *assets[] = { attachment_asset("A"), attachment_asset("B") };
        for (int i = 0; i < 2; i++) {
            graph *g = assets[i]->graph;
            node *event = add(g, "Event"), *print = add(g, "Print String");
            assert(!graph_set_event_name(g, event, "Log"));
            input(g, print, 1, (pin_value){ .type = PIN_STR, .str = assets[i]->name });
            next(g, event, print);
            assert(!graph_set_event_name(g, add(g, "Event"), assets[i]->name));
            assert(!game_behavior_register_asset(world, assets[i]->name, assets[i]));
        }
        assert(!game_behavior_has_event(world, "Log"));
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        ecs_entity first = ecs_world_create_entity(ecs),
                   second = ecs_world_create_entity(ecs);
        assert(!game_behavior_attach(world, first, "A"));
        assert(game_behavior_has_event(world, "A") && !game_behavior_has_event(world, "B"));
        assert(!game_behavior_attach(world, second, "A"));
        assert(!game_behavior_attach(world, first, "B"));
        assert(game_behavior_has_event(world, "Log") && !game_behavior_has_event(world, "Missing"));
        behavior_instance *original = game_behavior_instance(world, first, "A");
        const char *ab[] = { "A", "B" };
        /* Same per-entity names in an interleaved world still need grouping. */
        assert(!game_behavior_set_attachments(world, first, ab, 2));
        assert(game_behavior_dispatch_all(world, "Log", (flow_inputs){ 0 }, NULL) == 3);
        ecs_entity order[] = { first, first, second };
        const char *names[] = { "A", "B", "A" };
        for (int i = 0; i < 3; i++) {
            game_behavior_log_info info;
            assert(!game_behavior_log_info_at(world, i, &info));
            assert(ecs_entity_to_u64(info.entity) == ecs_entity_to_u64(order[i]));
            assert(!strcmp(info.asset_name, names[i]));
        }
        ecs_entity *crowd = NULL;
        for (int i = 0; i < 600; i++) {
            if (i == 300) {
                game_behavior_set_deferred(world, true);
            }
            ecs_entity entity = ecs_world_create_entity(ecs);
            assert(!game_behavior_set_attachments(world, entity, ab, 2));
            apush(crowd, entity);
            assert(
                value(game_behavior_instance(world, entity, "A"), "State").i ==
                (i < 300 ? 9 : 0)
            );
        }
        game_behavior_set_deferred(world, false);
        assert(!game_behavior_begin_pending(world));
        for (int i = 0; i < acount(crowd); i++) {
            assert(game_behavior_attachment_count_for_entity(world, crowd[i]) == 2);
            game_behavior_attachment_info info;
            assert(!game_behavior_attachment_info_at(world, crowd[i], 1, &info));
            assert(
                !strcmp(info.asset_name, "B") && value(info.instance, "State").i == 9
            );
        }
        assert(game_behavior_instance(world, first, "A") == original);
        assert(!game_behavior_destroy_entity(world, second));
        ecs_entity reused = ecs_world_create_entity(ecs);
        assert(reused.slot == second.slot && reused.generation != second.generation);
        assert(!game_behavior_set_attachments(world, reused, ab, 2));
        assert(!game_behavior_instance(world, second, "A"));
        behavior_asset *replacement = attachment_asset("A"), *old = NULL;
        assert(!game_behavior_replace_asset(world, "A", replacement, &old) && old == assets[0]);
        assert(!game_behavior_has_event(world, "A") && game_behavior_has_event(world, "B"));
        behavior_asset *updated = attachment_asset("B");
        assert(!graph_set_event_name(updated->graph, add(updated->graph, "Event"), "Changed"));
        assert(!game_behavior_pump(world, 0.1));
        assert(game_behavior_preview_asset(world, "B", updated, &old) == INSTANCE_UPDATE_READY);
        assert(old == assets[1] && !game_behavior_has_event(world, "B"));
        assert(!game_behavior_has_event(world, "Log") && game_behavior_has_event(world, "Changed"));
        for (int i = 0; i < acount(crowd); i++) {
            behavior_instance *instance = game_behavior_instance(world, crowd[i], "A");
            assert(
                instance && instance->asset == replacement &&
                value(instance, "State").i == 9
            );
            assert(game_behavior_instance(world, crowd[i], "B"));
        }
        assert(game_behavior_detach_asset(world, first, "A") == 1);
        assert(!game_behavior_instance(world, first, "A"));
        assert(game_behavior_instance(world, first, "B"));
        assert(game_behavior_detach_entity(world, first) == 1);
        assert(!game_behavior_attachment_count_for_entity(world, first));
        for (int i = 0; i < acount(crowd); i++) {
            assert(game_behavior_detach_asset(world, crowd[i], "B") == 1);
        }
        assert(game_behavior_has_event(world, "Changed"));
        assert(game_behavior_detach_asset(world, reused, "B") == 1);
        assert(
            !game_behavior_has_event(world, "Changed") &&
            game_behavior_has_event(world, "Begin")
        );
        if (mode < 2) {
            assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        }
        afree(crowd);
        game_behavior_world_free(world);
        ecs_world_free(ecs);
        behavior_asset_free(assets[0]);
        behavior_asset_free(assets[1]);
        behavior_asset_free(replacement);
        behavior_asset_free(updated);
        behavior_debug_backend(previous);
    }
    puts("attachment index PASS: growth, grouping, Begin, replacement, deletion and stale handles across all backends");
}

static void test_deferred_attachments()
{
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    behavior_asset *a = attachment_asset("Prepared");
    behavior_asset *broken = child_asset();
    assert(game_behavior_register_asset(world, "Prepared", a) == 0);
    assert(game_behavior_register_asset(world, "Broken", broken) == 0);
    ecs_entity entity = ecs_world_create_entity(ecs);
    game_behavior_set_deferred(world, true);
    const char *names[] = { "Prepared" };
    assert(game_behavior_set_attachments(world, entity, names, 1) == 0);
    behavior_instance *instance = game_behavior_instance(world, entity, "Prepared");
    assert(instance && value(instance, "State").i == 0);
    assert(game_behavior_begin_pending(world) == -1);
    assert(value(instance, "State").i == 0);
    game_behavior_set_deferred(world, false);
    assert(value(instance, "State").i == 0);
    assert(game_behavior_begin_pending(world) == 0);
    assert(value(instance, "State").i == 9);
    pin_value changed = { .type = PIN_I32, .i = 51, .type_name = "int" };
    assert(behavior_instance_set_variable(instance, a->variables[0].id, &changed) == 0);
    game_behavior_set_deferred(world, true);
    game_behavior_set_deferred(world, false);
    assert(game_behavior_begin_pending(world) == 0);
    assert(value(instance, "State").i == 51);
    /* Runtime spawning still starts immediately in the same world. */
    ecs_entity spawned = ecs_world_create_entity(ecs);
    assert(game_behavior_attach(world, spawned, "Prepared") == 0);
    assert(value(game_behavior_instance(world, spawned, "Prepared"), "State").i == 9);
    game_behavior_set_deferred(world, true);
    assert(game_behavior_attach(world, entity, "Broken") == 0);
    game_behavior_set_deferred(world, false);
    assert(game_behavior_begin_pending(world) == -1);
    assert(game_behavior_begin_pending(world) == -1);
    assert(game_behavior_instance(world, entity, "Prepared") == instance);
    assert(value(instance, "State").i == 51);
    game_behavior_world_free(world);
    behavior_asset_free(a);
    behavior_asset_free(broken);
    ecs_world_free(ecs);
    puts("deferred attachments PASS: setup is inert, Begin runs once, runtime spawn stays immediate, failures persist");
}

void test_compiled_spawns()
{
    behavior_backend modes[] = { BEHAVIOR_SLJIT, BEHAVIOR_BYTECODE, BEHAVIOR_ORACLE };
    for (int i = 0; i < 3; i++) {
        behavior_backend previous = behavior_debug_backend(modes[i]);
        run_spawn_case(false, false, false);
        run_spawn_case(true, false, false);
        run_spawn_case(false, 1, false);
        run_spawn_case(false, 2, false);
        run_spawn_case(false, 3, false);
        run_spawn_case(false, false, true);
        for (int isolated = 0; isolated < 2; isolated++) {
            for (int mode = 0; mode < 3; mode++) {
                delayed_spawn_case(isolated, mode);
            }
        }
        behavior_debug_backend(previous);
    }
    puts("PASS: whole compiled Spawn, initialization before Begin, breakpoints, cancellation and deferred destruction on SLJIT/bytecode/oracle");
}

static void prototype_registry_lifetime()
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char error[256];
    game_prototype *source = game_prototype_load(
        "{\"name\":\"Lifetime\",\"components\":{\"GameTransform\":{\"x\":12,"
        "\"y\":34}}}",
        &registry,
        error,
        sizeof error
    );
    assert(source);
    registry = (game_prototype_registry){ 0 };
    game_prototype *copy = game_prototype_clone(source, error, sizeof error);
    assert(copy);
    pin_value position;
    assert(game_prototype_read_field(copy, "GameTransform", "x", &position) == 0);
    assert(position.f == 12);
    game_prototype_free(copy);
    game_prototype_free(source);
}

void test_entities()
{
    prototype_registry_lifetime();
    test_attachment_lists();
    test_attachment_index();
    test_deferred_attachments();
    const node_desc *get = registry_node_by_name("GetEntity_GameTransform");
    const node_desc *set = registry_node_by_name("SetEntity_GameTransform");
    assert(get && get->access && (get->access->flags & NODE_EFFECT_WORLD_READ));
    assert(set && set->access && set->access->write_count == 1);
    assert(!(set->access->flags & NODE_EFFECT_ENTITY_SCOPED));
    test_compiled_spawns();
    puts("spawn function PASS: Delay and parent setup complete before Begin, with pause and cancellation in both hosts");
    test_queued_events(false);
    test_queued_events(true);
    puts("queued events PASS: copied payloads, bounded delivery, recursion, cancellation, and debugger lifetime");
    puts("entity host PASS: spawn initialization, targeted reads, cancellation, debugger hold, and deferred destruction");
}
