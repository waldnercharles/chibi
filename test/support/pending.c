#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/instance.h"
#include "runtime/behaviors/behavior.h"
#include "runtime/behaviors/entities.h"
#include "support/array.h"
#include "test/pending_fixture.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

static behavior_asset *counter()
{
    behavior_asset *a = behavior_asset_new("Pending counter");
    assert(behavior_asset_add_variable(a, "Calls", PIN_I32, NULL, PIN_CONTAINER_SINGLE, NULL, ""));
    graph *g = a->graph;
    node *work = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, work, "Work"));
    node *wait = graph_add_node(g, &NODE_Event);
    assert(!graph_set_event_name(g, wait, "Wait"));
    node *delay = graph_add_node(g, &NODE_Delay);
    assert(
        !graph_set_pin_default(g, delay, false, 1, &(pin_value){ .type = PIN_F64, .d = .25 })
    );
    node *get = behavior_asset_add_get_node(a, a->variables[0].id);
    node *add = graph_add_node(g, &NODE_Add);
    for (int p = 0; p < 2; p++) {
        assert(!graph_set_pin_type(g, add, false, p, PIN_I32, NULL));
    }
    assert(!graph_set_pin_type(g, add, true, 0, PIN_I32, NULL));
    assert(!graph_set_pin_default(g, add, false, 1, &(pin_value){ .type = PIN_I32, .i = 1 }));
    node *set = behavior_asset_add_set_node(a, a->variables[0].id);
    assert(!graph_connect_data(g, get, 0, add, 0));
    assert(!graph_connect_data(g, add, 0, set, 1));
    assert(!graph_connect_exec(g, work, 0, set, 0, 0));
    assert(!graph_connect_exec(g, wait, 0, delay, 0, 0));
    assert(!graph_connect_exec(g, delay, 0, set, 0, 0));
    return a;
}
static behavior_asset *emitter()
{
    behavior_asset *a = behavior_asset_new("Enqueue from sink");
    node *entry = graph_add_node(a->graph, &NODE_Event);
    assert(!graph_set_event_name(a->graph, entry, "Emit"));
    node *print = graph_add_node(a->graph, &NODE_PrintString);
    assert(!graph_connect_exec(a->graph, entry, 0, print, 0, 0));
    return a;
}
void bench_behavior_destroy(int count)
{
    assert(count > 0 && count <= 20000);
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    game_entities *host = game_behavior_entities_new(ecs, world);
    behavior_asset *assets[5];
    const char *names[] = { "A", "B", "C", "D", "E" };
    for (int i = 0; i < 5; i++) {
        assets[i] = counter();
        assert(!game_behavior_register_asset(world, names[i], assets[i]));
    }
    for (int i = 0; i < count; i++) {
        ecs_entity entity = ecs_world_create_entity(ecs);
        for (int j = 0; j < 5; j++) {
            assert(!game_behavior_attach(world, entity, names[j]));
        }
        game_entities_request_destroy(host, entity);
    }
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    assert(!game_entities_flush(host));
    clock_gettime(CLOCK_MONOTONIC, &end);
    double ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                (end.tv_nsec - start.tv_nsec) / 1e6;
    assert(!ecs_world_live_count(ecs) && !game_behavior_attachment_count(world));
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    printf("behavior_destroy entities=%d attachments=%d ms=%.6f oracle_calls=0\n", count, count * 5, ms);
    game_entities_free(host);
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    for (int i = 0; i < 5; i++) {
        behavior_asset_free(assets[i]);
    }
}
static flow_run *enqueue(behavior_instance *bi, bool wait)
{
    flow_run *run = behavior_instance_enqueue(bi, bi->asset->graph->nodes[wait], (flow_inputs){ 0 }, NULL);
    assert(run);
    return run;
}
typedef struct
{
    game_behavior_world *behaviors;
    ecs_entity *entities;
} destroy_trace;
static void destroyed(ecs_world *world, ecs_entity entity, void *user)
{
    destroy_trace *trace = user;
    assert(ecs_entity_is_alive(world, entity));
    assert(!game_behavior_attachment_count_for_entity(trace->behaviors, entity));
    apush(trace->entities, entity);
}
static void test_bulk_destroy(behavior_backend backend)
{
    enum
    {
        COUNT = 137
    };
    behavior_asset *a = counter(), *signal = emitter();
    node *set = alast(a->graph->nodes);
    node *tick = graph_add_node(a->graph, &NODE_Event);
    assert(!graph_set_event_name(a->graph, tick, "Tick"));
    assert(!graph_connect_exec(a->graph, tick, 0, set, 0, 0));
    assert(!graph_set_event_name(signal->graph, signal->graph->nodes[0], "Work"));
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    game_entities *host = game_behavior_entities_new(ecs, world);
    assert(!game_behavior_register_asset(world, a->name, a));
    assert(!game_behavior_register_asset(world, signal->name, signal));
    ecs_entity stale = ecs_world_create_entity(ecs);
    assert(!ecs_world_destroy_entity(ecs, stale));
    ecs_entity entities[COUNT];
    behavior_instance *instances[COUNT];
    behavior_backend previous = behavior_debug_backend(backend);
    for (int i = 0; i < COUNT; i++) {
        entities[i] = ecs_world_create_entity(ecs);
        const char *names[] = { i % 2 ? a->name : signal->name,
                                i % 2 ? signal->name : a->name };
        assert(!game_behavior_set_attachments(world, entities[i], names, 2));
        instances[i] = game_behavior_instance(world, entities[i], a->name);
        assert(!game_entities_request_event(host, entities[i], "Work", (execution_arguments){ 0 }));
    }
    assert(entities[0].slot == stale.slot && entities[0].generation != stale.generation);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    assert(game_behavior_update(world, NULL) == COUNT);
    for (int i = 0; i < COUNT; i++) {
        assert(instances[i]->execution.variables[0].i == 1);
        enqueue(instances[i], true);
    }
    assert(!game_behavior_pump(world, 0));
    /* Remove odd rows across multiple pending words, leaving the replacement
     * for a stale handle alive. Queued events must not reach pooled instances. */
    for (int i = COUNT - 2; i >= 1; i -= 2) {
        game_entities_request_destroy(host, entities[i]);
    }
    game_entities_request_destroy(host, entities[63]);
    game_entities_request_destroy(host, stale);
    game_entities_request_destroy(host, (ecs_entity){ 0 });
    destroy_trace trace = { .behaviors = world };
    ecs_world_set_destroy_observer(ecs, destroyed, &trace);
    assert(!game_entities_flush(host));
    assert(acount(trace.entities) == COUNT / 2);
    for (int i = 0; i < acount(trace.entities); i++) {
        assert(
            ecs_entity_to_u64(trace.entities[i]) ==
            ecs_entity_to_u64(entities[COUNT - 2 - i * 2])
        );
    }
    ecs_world_set_destroy_observer(ecs, NULL, NULL);
    afree(trace.entities);
    assert(ecs_world_live_count(ecs) == (COUNT + 1) / 2);
    assert(game_behavior_attachment_count(world) == COUNT + 1);
    for (int i = 0; i < COUNT; i++) {
        assert(ecs_entity_is_alive(ecs, entities[i]) == !(i % 2));
        assert(instances[i]->execution.alive == !(i % 2));
        if (i % 2) {
            assert(!acount(instances[i]->activations));
            continue;
        }
        assert(game_behavior_instance(world, entities[i], a->name) == instances[i]);
        game_behavior_attachment_info info;
        assert(!game_behavior_attachment_info_at(world, entities[i], 0, &info));
        assert(!strcmp(info.asset_name, signal->name));
    }
    assert(!game_behavior_pump(world, .25));
    assert(game_behavior_last_pump_count(world) == (COUNT + 1) / 2);
    while (game_entities_pending_events(host)) {
        assert(!game_entities_dispatch_events(host));
    }
    assert(!game_entities_pending_events(host));
    for (int i = 0; i < COUNT; i += 2) {
        assert(instances[i]->execution.variables[0].i == 3);
    }
    assert(game_behavior_update(world, NULL) == (COUNT + 1) / 2);
    for (int i = 0; i < COUNT; i += 2) {
        assert(instances[i]->execution.variables[0].i == 4);
    }
    assert(game_behavior_log_count(world) == (COUNT + 1) / 2);
    for (int i = 0; i < game_behavior_log_count(world); i++) {
        game_behavior_log_info log;
        assert(!game_behavior_log_info_at(world, i, &log));
        assert(ecs_entity_to_u64(log.entity) == ecs_entity_to_u64(entities[i * 2]));
    }
    ecs_entity replacement = ecs_world_create_entity(ecs);
    assert(!game_behavior_attach(world, replacement, a->name));
    behavior_instance *reused = game_behavior_instance(world, replacement, a->name);
    assert(!reused->execution.variables[0].i && !acount(reused->activations));
    enqueue(reused, false);
    assert(!game_behavior_pump(world, 1) && game_behavior_last_pump_count(world) == 1);
    assert(reused->execution.variables[0].i == 1);
    /* A destruction already in the ECS command buffer is applied once and its
     * behavior is still detached, even when the host queues the same handle. */
    assert(!ecs_world_record_destroy(ecs, replacement, 0));
    game_entities_request_destroy(host, replacement);
    game_entities_request_destroy(host, replacement);
    assert(!game_entities_flush(host));
    assert(!ecs_entity_is_alive(ecs, replacement));
    assert(!game_behavior_instance(world, replacement, a->name));
    if (backend != BEHAVIOR_ORACLE) {
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    }
    behavior_debug_backend(previous);
    game_entities_free(host);
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    behavior_asset_free(a);
    behavior_asset_free(signal);
}
static int parallel_count;
static void reverse_parallel(int count, void (*worker)(int, void *), void *user)
{
    parallel_count = count;
    for (int i = count - 1; i >= 0; i--) {
        worker(i, user);
    }
}
typedef struct
{
    behavior_instance *early, *late;
    int calls;
} sink_state;
static void enqueue_sink(const node *, const char *, void *user)
{
    sink_state *state = user;
    assert(!state->calls++);
    enqueue(state->early, false);
    enqueue(state->late, false);
}
typedef struct
{
    game_behavior_world *world;
    ecs_entity entity;
    const char *asset;
    behavior_instance *created;
} grow_state;
static void grow_sink(const node *, const char *, void *user)
{
    grow_state *state = user;
    assert(!state->created);
    assert(!game_behavior_attach(state->world, state->entity, state->asset));
    state->created = game_behavior_instance(state->world, state->entity, state->asset);
    enqueue(state->created, false);
}
static void test_growth(behavior_backend backend, int mode)
{
    behavior_asset *a = counter(), *signal = emitter();
    if (mode) {
        graph *g = signal->graph;
        assert(!graph_set_event_name(g, g->nodes[0], "Begin"));
        uint64_t id = behavior_asset_add_variable(signal, "Invalid", PIN_I32, NULL, PIN_CONTAINER_SINGLE, NULL, "")
                          ->id;
        node *divide = graph_add_node(g, &NODE_Divide);
        for (int p = 0; p < 2; p++) {
            assert(!graph_set_pin_type(g, divide, false, p, PIN_I32, NULL));
        }
        assert(!graph_set_pin_type(g, divide, true, 0, PIN_I32, NULL));
        assert(!graph_set_pin_default(g, divide, false, 1, &(pin_value){ .type = PIN_I32 }));
        node *set = behavior_asset_add_set_node(signal, id);
        assert(!graph_connect_data(g, divide, 0, set, 1));
        assert(!graph_connect_exec(g, g->nodes[1], 0, set, 0, 0));
    }
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    assert(!game_behavior_register_asset(world, a->name, a));
    assert(!game_behavior_register_asset(world, signal->name, signal));
    ecs_entity owner = ecs_world_create_entity(ecs);
    grow_state state = { world, ecs_world_create_entity(ecs), a->name, NULL };
    game_behavior_set_sink(world, grow_sink, &state);
    behavior_backend previous = behavior_debug_backend(backend);
    if (mode == 2) {
        const char *names[] = { signal->name };
        assert(game_behavior_set_attachments(world, owner, names, 1) == -1);
    } else {
        assert(game_behavior_attach(world, owner, signal->name) == (mode ? -1 : 0));
    }
    if (!mode) {
        behavior_instance *source = game_behavior_instance(world, owner, signal->name);
        enqueue(source, false);
        assert(!game_behavior_pump(world, 0) && game_behavior_last_pump_count(world) == 1);
    }
    behavior_debug_backend(previous);
    assert(state.created && !state.created->execution.variables[0].i);
    assert(!game_behavior_pump(world, .5) && game_behavior_last_pump_count(world) == 1);
    assert(state.created->execution.variables[0].i == 1);
    assert(!game_behavior_pump(world, 1) && !game_behavior_last_pump_count(world));
    assert(state.created->execution.clock->current_time == 1);
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    behavior_asset_free(a);
    behavior_asset_free(signal);
}
static void test_targeted_growth(behavior_backend backend)
{
    behavior_asset *a = counter(), *signal = emitter();
    assert(!graph_set_event_name(signal->graph, signal->graph->nodes[0], "Work"));
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    assert(!game_behavior_register_asset(world, a->name, a));
    assert(!game_behavior_register_asset(world, signal->name, signal));
    behavior_backend previous = behavior_debug_backend(backend);
    ecs_entity owner = ecs_world_create_entity(ecs);
    assert(!game_behavior_attach(world, owner, signal->name));
    behavior_instance *others[127];
    for (int i = 0; i < 127; i++) {
        ecs_entity entity = ecs_world_create_entity(ecs);
        assert(!game_behavior_attach(world, entity, a->name));
        others[i] = game_behavior_instance(world, entity, a->name);
    }
    grow_state state = { world, owner, a->name, NULL };
    game_behavior_set_sink(world, grow_sink, &state);
    assert(game_behavior_dispatch(world, owner, "Work", (flow_inputs){ 0 }, NULL) == 2);
    assert(state.created && state.created->execution.variables[0].i == 2);
    for (int i = 0; i < 127; i++) {
        assert(!others[i]->execution.variables[0].i);
    }
    behavior_debug_backend(previous);
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    behavior_asset_free(a);
    behavior_asset_free(signal);
}

static void test_clock_inheritance(behavior_backend backend)
{
    behavior_asset *a = counter();
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    assert(!game_behavior_register_asset(world, a->name, a));
    ecs_entity owner = ecs_world_create_entity(ecs);
    behavior_backend previous = behavior_debug_backend(backend);
    for (int reuse = 0; reuse < 2; reuse++) {
        double now = 7 + reuse;
        game_behavior_set_time(world, now);
        game_behavior_set_delta_seconds(world, -0.0);
        assert(!game_behavior_attach(world, owner, a->name));
        behavior_instance *bi = game_behavior_instance(world, owner, a->name);
        assert(
            bi->execution.clock->current_time == now &&
            signbit(bi->execution.clock->delta_seconds)
        );
        flow_run *run = behavior_instance_dispatch(bi, "Wait", (flow_inputs){ 0 }, NULL);
        assert(run && flow_run_wake_time(run) == now + .25);
        assert(!game_behavior_pump(world, now) && !bi->execution.variables[0].i);
        game_behavior_set_delta_seconds(world, 0.0);
        assert(!signbit(bi->execution.clock->delta_seconds));
        assert(
            !game_behavior_pump(world, now + .25) && bi->execution.variables[0].i == 1
        );
        assert(
            !game_behavior_pump(world, now + .25) && bi->execution.variables[0].i == 1
        );
        assert(game_behavior_detach_entity(world, owner) == 1);
    }
    behavior_debug_backend(previous);
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    behavior_asset_free(a);
}

static void test_borrowed_clock_wait(behavior_backend backend)
{
    behavior_asset *asset = counter();
    behavior_clock clock = { .current_time = 7 };
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(instance, backend));
    behavior_instance_bind_clock(instance, &clock);
    flow_run *run = behavior_instance_dispatch(instance, "Wait", (flow_inputs){ 0 }, NULL);
    assert(run && flow_run_wake_time(run) == 7.25);
    clock.current_time = 7.125;
    assert(!behavior_instance_pump(instance));
    assert(flow_run_status(run) == FLOW_SUSPENDED);
    clock.current_time = 7.25;
    assert(!behavior_instance_pump(instance));
    assert(flow_run_status(run) == FLOW_COMPLETED);
    pin_value value;
    assert(!behavior_instance_get_variable(instance, asset->variables[0].id, &value));
    assert(value.i == 1);
    assert(!behavior_instance_pool_release(instance));
    clock.current_time = 100;
    assert(behavior_instance_pool_reuse(instance, asset, NULL, NULL, NULL) == instance);
    run = behavior_instance_dispatch(instance, "Wait", (flow_inputs){ 0 }, NULL);
    assert(run && flow_run_wake_time(run) == 7.5);
    assert(!behavior_instance_tick(instance, 7.5));
    assert(flow_run_status(run) == FLOW_COMPLETED);
    assert(!behavior_instance_get_variable(instance, asset->variables[0].id, &value));
    assert(value.i == 1);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

static void test_detach_suffix(behavior_backend backend)
{
    enum
    {
        COUNT = 130
    };
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    behavior_asset *asset = counter();
    assert(!game_behavior_register_asset(world, asset->name, asset));
    ecs_entity entities[COUNT];
    behavior_instance *instances[COUNT];
    behavior_backend previous = behavior_debug_backend(backend);
    for (int i = 0; i < COUNT; i++) {
        entities[i] = ecs_world_create_entity(ecs);
        assert(!game_behavior_attach(world, entities[i], asset->name));
        instances[i] = game_behavior_instance(world, entities[i], asset->name);
    }
    behavior_debug_backend(previous);
    int waiting[] = { 0, 63, 64, 127, 129 };
    for (int i = 0; i < 5; i++) {
        enqueue(instances[waiting[i]], true);
    }
    assert(!game_behavior_pump(world, 0) && game_behavior_last_pump_count(world) == 5);
    /* Moving the last attachment must move its pending membership as well,
     * while retaining the pending prefix across word boundaries. */
    assert(game_behavior_detach_entity(world, entities[127]) == 1);
    assert(!game_behavior_pump(world, .25) && game_behavior_last_pump_count(world) == 4);
    assert(
        instances[0]->execution.variables[0].i == 1 &&
        instances[63]->execution.variables[0].i == 1
    );
    assert(
        instances[64]->execution.variables[0].i == 1 &&
        instances[129]->execution.variables[0].i == 1
    );
    assert(game_behavior_instance(world, entities[129], asset->name) == instances[129]);
    enqueue(instances[0], true);
    enqueue(instances[63], true);
    enqueue(instances[129], true);
    assert(!game_behavior_pump(world, .25) && game_behavior_last_pump_count(world) == 3);
    assert(game_behavior_detach_entity(world, entities[129]) == 1);
    assert(!game_behavior_pump(world, .5) && game_behavior_last_pump_count(world) == 2);
    assert(
        instances[0]->execution.variables[0].i == 2 &&
        instances[63]->execution.variables[0].i == 2
    );
    assert(!game_behavior_pump(world, .5) && !game_behavior_last_pump_count(world));
    assert(game_behavior_has_event(world, "Wait"));
    assert(game_behavior_attachment_count(world) == COUNT - 2);
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    behavior_asset_free(asset);
}

void test_pending_backend(behavior_backend backend)
{
    test_detach_suffix(backend);
    test_borrowed_clock_wait(backend);
    test_bulk_destroy(backend);
    enum
    {
        COUNT = 257,
        EMITTER = 128
    };
    behavior_asset *a = counter(), *signal = emitter();
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    assert(!game_behavior_register_asset(world, a->name, a));
    assert(!game_behavior_register_asset(world, signal->name, signal));
    ecs_entity entities[COUNT];
    behavior_instance *instances[COUNT];
    behavior_backend previous = behavior_debug_backend(backend);
    for (int i = 0; i < COUNT; i++) {
        entities[i] = ecs_world_create_entity(ecs);
        const char *name = i == EMITTER ? signal->name : a->name;
        assert(!game_behavior_attach(world, entities[i], name));
        instances[i] = game_behavior_instance(world, entities[i], name);
        assert(instances[i]);
    }
    behavior_debug_backend(previous);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    game_behavior_set_parallel_for(world, reverse_parallel, NULL);
    assert(!game_behavior_pump(world, 0) && !game_behavior_last_pump_count(world));
    enqueue(instances[63], true);
    enqueue(instances[63], true);
    enqueue(instances[64], true);
    enqueue(instances[256], true);
    parallel_count = 0;
    assert(
        !game_behavior_pump(world, 0) &&
        game_behavior_last_pump_count(world) == 3 && parallel_count == 3
    );
    assert(!game_behavior_pump(world, .25) && game_behavior_last_pump_count(world) == 3);
    assert(
        instances[63]->execution.variables[0].i == 2 &&
        instances[64]->execution.variables[0].i == 1 &&
        instances[256]->execution.variables[0].i == 1
    );
    assert(!game_behavior_pump(world, .5) && !game_behavior_last_pump_count(world));

    assert(!game_behavior_set_debug_hold(world, entities[64], a->name, true));
    flow_run *held = enqueue(instances[64], false);
    assert(flow_run_step(held) == FLOW_PAUSED_RESULT && game_behavior_paused(world));
    assert(
        !game_behavior_pump(world, .5) &&
        game_behavior_last_pump_count(world) == 1 && flow_run_status(held) == FLOW_PAUSED
    );
    assert(!flow_run_resume(held));
    assert(!game_behavior_pump(world, .5) && game_behavior_last_pump_count(world) == 1);
    assert(acount(instances[64]->activations) == 1 && !game_behavior_paused(world));
    assert(!game_behavior_set_debug_hold(world, entities[64], a->name, false));
    assert(!game_behavior_pump(world, .5));
    assert(!game_behavior_pump(world, .5) && !game_behavior_last_pump_count(world));

    flow_run *cancelled = enqueue(instances[0], true);
    assert(!flow_run_stop(cancelled));
    behavior_instance_release_activation(instances[0], cancelled);
    assert(!game_behavior_pump(world, .5) && !game_behavior_last_pump_count(world));
    enqueue(instances[63], true);
    enqueue(instances[256], true);
    assert(game_behavior_detach_entity(world, entities[63]) == 1);
    assert(!game_behavior_pump(world, .5) && game_behavior_last_pump_count(world) == 1);
    previous = behavior_debug_backend(backend);
    assert(!game_behavior_attach(world, entities[63], a->name));
    behavior_debug_backend(previous);
    instances[63] = game_behavior_instance(world, entities[63], a->name);
    assert(!instances[63]->execution.variables[0].i);
    enqueue(instances[63], false);
    assert(!game_behavior_pump(world, .75) && game_behavior_last_pump_count(world) == 2);
    assert(
        instances[63]->execution.variables[0].i == 1 &&
        instances[256]->execution.variables[0].i == 2
    );
    assert(!game_behavior_pump(world, .75) && !game_behavior_last_pump_count(world));

    /* Detach moved the last row into slot 63. Choose a row still after Emit. */
    sink_state state = { instances[0], instances[255], 0 };
    game_behavior_set_sink(world, enqueue_sink, &state);
    assert(
        behavior_instance_enqueue(instances[EMITTER], signal->graph->nodes[0], (flow_inputs){ 0 }, NULL)
    );
    parallel_count = 0;
    assert(!game_behavior_pump(world, 1));
    assert(state.calls == 1);
    assert(!parallel_count);
    assert(game_behavior_last_pump_count(world) == 2);
    assert(
        !instances[0]->execution.variables[0].i &&
        instances[255]->execution.variables[0].i == 1
    );
    assert(!game_behavior_pump(world, 1) && game_behavior_last_pump_count(world) == 1);
    assert(instances[0]->execution.variables[0].i == 1);
    assert(!game_behavior_pump(world, 1) && !game_behavior_last_pump_count(world));
    if (backend != BEHAVIOR_ORACLE) {
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    }
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    behavior_asset_free(a);
    behavior_asset_free(signal);
    for (int mode = 0; mode < 3; mode++) {
        test_growth(backend, mode);
    }
    test_clock_inheritance(backend);
    test_targeted_growth(backend);
    printf(
        "PASS: %s indexed pending queues, sparse parallel work, waits, debug "
        "holds, cancellation, pooling and same-pump enqueue order\n",
        behavior_backend_name(backend)
    );
}
