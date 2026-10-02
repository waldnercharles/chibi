#include "graph/runtime/flow_internal.h"
#include "graph/runtime/update_internal.h"
#include "support/text_file.h"
#include "test/suite_internal.h"
#include "test/whole_aggregate_fixture.h"
#include "test/whole_batch_fixture.h"
#include "test/whole_command_fixture.h"

static int run_tick(game_behavior_world *world, bool update)
{
    return update
             ? game_behavior_update(world, NULL)
             : game_behavior_dispatch_all(world, "Tick", (flow_inputs){ 0 }, NULL);
}

static void world_parity(behavior_backend backend, bool batch, bool components, bool profile, bool update)
{
    enum
    {
        COUNT = 8
    };
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    game_behavior_set_batch_enabled(world, batch);
    game_behavior_set_profiling(world, profile);
    behavior_asset *asset = whole_batch_fixture(components);
    assert(!game_behavior_register_asset(world, asset->name, asset));
    const ecs_component_desc *component = WHOLE_NODE_GetRecord.component;
    if (components) {
        assert(!ecs_world_register_component(ecs, component, NULL));
    }
    ecs_entity entities[COUNT];
    behavior_instance *instances[COUNT];
    double weights[COUNT];
    for (int i = 0; i < COUNT; i++) {
        entities[i] = ecs_world_create_entity(ecs);
        weights[i] = i * .5;
        if (components) {
            whole_record record = { .weight = weights[i] };
            assert(!ecs_world_record_add(ecs, entities[i], component->id, &record, sizeof record));
        }
    }
    assert(!ecs_world_apply_commands(ecs));
    behavior_backend previous = behavior_debug_backend(backend);
    for (int i = 0; i < COUNT; i++) {
        assert(!game_behavior_attach(world, entities[i], asset->name));
        game_behavior_attachment_info info;
        assert(!game_behavior_attachment_info_at(world, entities[i], 0, &info));
        instances[i] = info.instance;
        assert(instances[i]->variables[0].d == 10.);
        if (components) {
            pin_value target = { .type = PIN_HANDLE,
                                 .type_name = "ecs.entity",
                                 .u64 = ecs_entity_to_u64(entities[(i + 1) % COUNT]) };
            assert(!behavior_instance_set_variable(
                instances[i],
                asset->variables[1].id,
                &target
            ));
        }
    }
    behavior_debug_backend(previous);
    assert(!game_behavior_pump(world, 0.));
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int frame = 0; frame < 24; frame++) {
        /* Inspected frames use the stable activation/watch path. Releasing a
         * hold must also drain the completed queue before batching resumes. */
        if (frame == 8 || frame == 12) {
            assert(!game_behavior_set_debug_hold(world, entities[0], asset->name, frame == 8));
        }
        int eligible = 0;
        for (int i = 0; i < COUNT; i++) {
            eligible += !instances[i]->trace_enabled &&
                        !acount(instances[i]->activations);
        }
        double delta = frame % 2 ? .125 : .25;
        game_behavior_set_delta_seconds(world, delta);
        game_behavior_profile_begin(world);
        uint64_t dispatched = behavior_batch_dispatches();
        assert(run_tick(world, update) == COUNT);
        if (update && batch && !profile && backend == BEHAVIOR_SLJIT) {
            assert(game_behavior_last_update(world).native_loop == eligible);
            assert(game_behavior_last_update(world).system_loop == eligible);
        }
        assert(
            behavior_batch_dispatches() - dispatched +
                (update ? game_behavior_last_update(world).prepared : 0) ==
            (uint64_t)(batch && backend != BEHAVIOR_ORACLE ? eligible : 0)
        );
        for (int i = 0; i < COUNT; i++) {
            assert(
                instances[i]->variables[0].d ==
                10. + (frame / 2 + 1) * .25 + ((frame + 1) / 2) * .125
            );
            if (components) {
                weights[(i + 1) % COUNT] = weights[i] + delta;
            }
        }
        for (int i = 0; components && i < COUNT; i++) {
            const whole_record
                *record = ecs_component_data_const(ecs, entities[i], component->id);
            assert(record->weight == weights[i]);
        }
        if (profile) {
            const game_behavior_profile_frame *p = game_behavior_profile(world);
            assert(p->count == 1 && !strcmp(p->rows[0].event, "Tick"));
            assert(
                p->rows[0].dispatches == COUNT && p->rows[0].starts == COUNT &&
                !p->rows[0].resumes
            );
        }
        assert(!game_behavior_pump(world, (frame + 1) * .25));
    }
    behavior_asset *next = whole_batch_fixture(components), *old;
    graph *updated = next->graphs[1];
    for (int i = 0; i < acount(updated->nodes); i++) {
        node *n = updated->nodes[i], *source;
        int pin;
        if (n->desc != &NODE_Add || !graph_input_source(updated, n, 1, &source, &pin)) {
            continue;
        }
        assert(source->desc == &NODE_DeltaTime);
        assert(!graph_remove_edge(updated, source, pin, n, 1));
        assert(
            !graph_set_pin_default(updated, n, 0, 1, &(pin_value){ .type = PIN_F64, .d = .25 })
        );
    }
    assert(
        game_behavior_preview_asset(world, asset->name, next, &old) == INSTANCE_UPDATE_READY &&
        old == asset
    );
    behavior_asset_free(old);
    asset = next;
    uint64_t dispatched = behavior_batch_dispatches();
    assert(run_tick(world, update) == COUNT);
    assert(
        behavior_batch_dispatches() - dispatched +
            (update ? game_behavior_last_update(world).prepared : 0) ==
        (uint64_t)(batch && backend != BEHAVIOR_ORACLE ? COUNT : 0)
    );
    for (int i = 0; i < COUNT; i++) {
        assert(instances[i]->variables[0].d == 14.75);
        if (components) {
            weights[(i + 1) % COUNT] = weights[i] + .25;
        }
    }
    for (int i = 0; components && i < COUNT; i++) {
        const whole_record
            *record = ecs_component_data_const(ecs, entities[i], component->id);
        assert(record->weight == weights[i]);
    }
    assert(!game_behavior_pump(world, 7.));
    if (!components) {
        assert(game_behavior_detach_entity(world, entities[0]) == 1);
        assert(!game_behavior_attach(world, entities[0], asset->name));
        assert(!game_behavior_pump(world, 7.));
        assert(run_tick(world, update) == COUNT);
        game_behavior_attachment_info info;
        assert(!game_behavior_attachment_info_at(world, entities[0], 0, &info));
        assert(info.instance->variables[0].d == 10.25);
    }
    if (backend != BEHAVIOR_ORACLE) {
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    }
    game_behavior_world_free(world);
    behavior_asset_free(asset);
    ecs_world_free(ecs);
}

static node *tick_entry(behavior_asset *asset)
{
    bool duplicate;
    node *entry = behavior_program_event(behavior_program_publish(asset), "Tick", &duplicate);
    assert(entry && !duplicate);
    return entry;
}

static void failure_parity(behavior_backend backend, bool components, bool loop)
{
    behavior_asset *asset = whole_batch_fixture(components);
    graph *g = asset->graphs[1];
    if (loop) {
        node *first = g->nodes[0], *set = g->nodes[3];
        assert(set->desc == &NODE_SetVariable);
        assert(!graph_remove_edge(g, first, 0, set, 0));
        assert(!graph_remove_edge(g, set, 0, g->nodes[1], 0));
        node *range = graph_add_node(g, &NODE_ForLoop);
        assert(
            !graph_set_pin_default(g, range, 0, 2, &(pin_value){ .type = PIN_I32, .i = INT32_MAX })
        );
        assert(!graph_connect_exec(g, first, 0, range, 0, 0));
        assert(!graph_connect_exec(g, range, 0, set, 0, 0));
        assert(!graph_connect_exec(g, range, 2, g->nodes[1], 0, 0));
    } else if (!components) {
        /* A function with a result must reach Return to produce it. */
        assert(!graph_remove_edge(g, g->nodes[3], 0, g->nodes[1], 0));
        pin_spec result = { .name = "Total",
                            .type = PIN_F64,
                            .type_name = "double",
                            .kind = PIN_DATA };
        node *ret = g->nodes[1];
        assert(graph_add_pin(g, ret, 0, &result) >= 0);
        result.declaration_id = ret->input_pins[ret->input_count - 1].id;
        node *call = NULL;
        for (int i = 0; i < acount(asset->graph->nodes); i++) {
            call = asset->graph->nodes[i]->desc == &NODE_CallFunction
                     ? asset->graph->nodes[i]
                     : call;
        }
        assert(call && graph_add_pin(asset->graph, call, 1, &result) >= 0);
    }
    node *entry = tick_entry(asset);
    assert(behavior_function_batchable(
        behavior_program_function(behavior_program_graph(asset->program, asset->graph), entry)
    ));
    behavior_instance *ordinary = behavior_instance_create(asset, NULL, NULL, NULL);
    behavior_instance *batched = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(ordinary, backend));
    assert(!behavior_instance_set_backend(batched, backend));
    behavior_instance_set_trace_enabled(ordinary, false);
    behavior_instance_set_trace_enabled(batched, false);
    behavior_instance_set_delta_seconds(ordinary, .125);
    behavior_instance_set_delta_seconds(batched, .125);
    flow_run *reference = behavior_instance_dispatch(ordinary, "Tick", (flow_inputs){ 0 }, NULL);
    assert(reference && flow_run_status(reference) == FLOW_FAILED);
    /* Capture failures directly from both outer loops, after the earlier
     * variable write. Constructing the diagnostic must never repeat effects. */
    ecs_world *ecs = ecs_world_new();
    ecs_entity entity = ecs_world_create_entity(ecs);
    behavior_instance *prepared = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(prepared, backend));
    behavior_instance_set_trace_enabled(prepared, false);
    behavior_instance_set_delta_seconds(prepared, .125);
    behavior_update_binding **bindings = NULL;
    const behavior_function *function = behavior_program_batch_event(asset->program, "Tick", &entry);
    behavior_update_row rows[2] = {
        { .instance = prepared,
          .binding = behavior_update_prepare(&bindings, prepared, function, entry),
          .entity = ecs_entity_to_u64(entity) }
    };
    rows[1] = rows[0];
    behavior_update_row *row = rows;
    assert(row->binding);
    int result;
    void *code = backend == BEHAVIOR_SLJIT ? behavior_function_system_entry(function)
                                           : NULL;
    assert(backend != BEHAVIOR_SLJIT || code);
    behavior_update_row *
        stopped = code
                    ? ((
                          behavior_update_fn
                      )code)(rows, rows + 2, (const ecs_execution_view *)ecs, &result)
                    : behavior_update_run_c(rows, rows + 2, (const ecs_execution_view *)ecs, &result, NULL);
    assert(stopped == row && result < 0);
    flow_run *prepared_failure = behavior_update_failure(row, result);
    const flow_error *expected_error = flow_run_error(reference),
                     *prepared_error = flow_run_error(prepared_failure);
    assert(
        expected_error->code == prepared_error->code &&
        expected_error->node == prepared_error->node &&
        expected_error->pin == prepared_error->pin &&
        expected_error->steps == prepared_error->steps &&
        !strcmp(expected_error->message, prepared_error->message)
    );
    assert(prepared->variables[0].d == ordinary->variables[0].d);
    uint64_t failed_node = prepared_error->node->id;
    assert(!behavior_instance_release_activation(prepared, prepared_failure));
    const behavior_instance_failure *saved = behavior_instance_last_failure(prepared);
    assert(
        saved && saved->node_id == failed_node &&
        saved->code == expected_error->code && saved->pin == expected_error->pin &&
        saved->steps == expected_error->steps && !strcmp(saved->event_name, "Tick")
    );
    behavior_update_free_bindings(bindings);
    behavior_instance_destroy(prepared);
    ecs_world_free(ecs);
    behavior_batch *batch = NULL;
    flow_run *failed;
    assert(behavior_instance_dispatch_batch(batched, "Tick", &batch, &failed));
    assert(failed && flow_run_status(failed) == FLOW_FAILED && failed->whole_function_executed);
    const flow_error *a = flow_run_error(reference), *b = flow_run_error(failed);
    assert(a->code == b->code && a->node == b->node && a->pin == b->pin && a->steps == b->steps);
    assert(!strcmp(a->message, b->message));
    assert(flow_run_steps(reference) == flow_run_steps(failed));
    int n, m;
    const flow_call_frame *x = flow_run_call_stack(reference, &n),
                          *y = flow_run_call_stack(failed, &m);
    assert(n == m);
    for (int i = 0; i < n; i++) {
        assert(x[i].node_id == y[i].node_id && x[i].graph_id == y[i].graph_id);
    }
    assert(
        ordinary->variables[0].d == batched->variables[0].d &&
        batched->variables[0].d > 0.
    );
    behavior_batch_free(batch);
    behavior_instance_destroy(ordinary);
    behavior_instance_destroy(batched);
    behavior_asset_free(asset);
}

static void queued_wait(behavior_backend backend)
{
    behavior_asset *asset = whole_batch_fixture(false);
    graph *g = asset->graph;
    node *sleep = graph_add_node(g, &NODE_Event),
         *delay = graph_add_node(g, &NODE_Delay);
    assert(!graph_set_event_name(g, sleep, "Sleep"));
    assert(!graph_set_pin_default(g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1. }));
    assert(!graph_connect_exec(g, sleep, 0, delay, 0, 0));
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(instance, backend));
    behavior_instance_set_trace_enabled(instance, false);
    behavior_instance_set_delta_seconds(instance, .125);
    behavior_batch *batch = NULL;
    flow_run *failed;
    flow_run *wait = behavior_instance_dispatch(instance, "Sleep", (flow_inputs){ 0 }, NULL);
    assert(wait && flow_run_status(wait) == FLOW_SUSPENDED);
    assert(!behavior_instance_dispatch_batch(instance, "Tick", &batch, &failed));
    assert(instance->variables[0].d == 0.);
    assert(!behavior_instance_stop_activation(instance, wait));
    assert(!behavior_instance_release_activation(instance, wait));
    assert(behavior_instance_dispatch_batch(instance, "Tick", &batch, &failed) && !failed);
    assert(instance->variables[0].d == .125);
    behavior_batch_free(batch);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

static void exclusions(behavior_backend backend)
{
    for (int kind = 0; kind < 4; kind++) {
        behavior_asset *asset = whole_batch_fixture(false);
        graph *g = asset->graph;
        node *entry = tick_entry(asset), *call = g->nodes[acount(g->nodes) - 1];
        if (kind < 2) {
            node *tail = graph_add_node(g, kind ? whole_command_node() : &NODE_Delay);
            if (kind) {
                assert(!graph_set_node_reference(g, tail, 0, "Capture"));
            }
            assert(!graph_connect_exec(g, call, 0, tail, 0, 0));
        }
        if (kind == 2) {
            /* Batched runs pass no event arguments, so logic may not read one. */
            const behavior_variable *total = &asset->variables[0];
            pin_spec value = { .name = "Value",
                               .type = total->type,
                               .type_name = total->type_name };
            assert(graph_add_pin(g, entry, 1, &value) == 1);
            node *set = graph_add_node(g, &NODE_SetVariable);
            assert(!graph_set_node_reference(g, set, total->id, total->name));
            assert(!graph_set_pin_type(g, set, 0, 1, total->type, total->type_name));
            assert(!graph_set_pin_type(g, set, 1, 1, total->type, total->type_name));
            assert(!graph_connect_data(g, entry, 1, set, 1));
            assert(!graph_connect_exec(g, call, 0, set, 0, 0));
        }
        if (kind == 3) {
            assert(!graph_set_breakpoint(g, call, true));
        }
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(instance, backend));
        behavior_instance_set_trace_enabled(instance, false);
        const behavior_function *function = behavior_program_function(
            behavior_program_graph(asset->program, g),
            entry
        );
        assert(kind == 3 ? !function : function && !behavior_function_batchable(function));
        behavior_batch *batch = NULL;
        flow_run *failed;
        uint64_t dispatched = behavior_batch_dispatches();
        assert(!behavior_instance_dispatch_batch(instance, "Tick", &batch, &failed));
        assert(!failed && !batch && !instance->variables[0].d);
        assert(dispatched == behavior_batch_dispatches());
        behavior_instance_destroy(instance);
        behavior_asset_free(asset);
    }
}

/* An event may declare outputs that its logic never reads. */
static void unread_event_output(behavior_backend backend)
{
    behavior_asset *asset = whole_batch_fixture(false);
    node *entry = tick_entry(asset);
    pin_spec delta = { .name = "DeltaSeconds", .type = PIN_F32, .type_name = "float" };
    assert(graph_add_pin(asset->graph, entry, 1, &delta) == 1);
    behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(instance, backend));
    behavior_instance_set_trace_enabled(instance, false);
    behavior_instance_set_delta_seconds(instance, .125);
    behavior_batch *batch = NULL;
    flow_run *failed;
    assert(behavior_instance_dispatch_batch(instance, "Tick", &batch, &failed) && !failed);
    assert(instance->variables[0].d == .125);
    behavior_batch_free(batch);
    behavior_instance_destroy(instance);
    behavior_asset_free(asset);
}

/* Pursuit's Tick declares DeltaSeconds, which only its debugger watches read. */
static void watched_event_output()
{
    char *text = text_file_read("projects/arena2d/content/behaviors/Pursuit.kdl");
    behavior_asset *asset = behavior_asset_load(text);
    free(text);
    node *entry = tick_entry(asset);
    assert(entry->output_count == 2);
    assert(behavior_program_batch_event(asset->program, "Tick", &entry));
    behavior_asset_free(asset);
}

static void external_profile(const char *, uint64_t, uint64_t, behavior_profile_phase, void *user)
{
    (*(int *)user)++;
}

static void indexed_events(behavior_backend backend)
{
    behavior_asset *asset = whole_batch_fixture(false);
    node *entry = tick_entry(asset);
    behavior_batch *batch = NULL;
    for (int version = 0; version < 3; version++) {
        if (version == 1) {
            assert(!graph_set_event_name(asset->graph, entry, "Advance"));
        }
        if (version == 2) {
            node *duplicate = graph_add_node(asset->graph, &NODE_Event);
            assert(!graph_set_event_name(asset->graph, duplicate, "Advance"));
        }
        behavior_instance *instance = behavior_instance_create(asset, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(instance, backend));
        behavior_instance_set_trace_enabled(instance, false);
        behavior_instance_set_delta_seconds(instance, .125);
        flow_run *failed;
        if (version) {
            assert(!behavior_instance_dispatch_batch(instance, "Tick", &batch, &failed));
        }
        bool ran = behavior_instance_dispatch_batch(instance, version ? "Advance" : "Tick", &batch, &failed);
        assert(ran == (version != 2) && !failed);
        assert(instance->variables[0].d == (ran ? .125 : 0.));
        behavior_instance_destroy(instance);
    }
    behavior_batch_free(batch);
    behavior_asset_free(asset);
}

static void observed_profile()
{
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    behavior_asset *asset = whole_batch_fixture(false);
    assert(!game_behavior_register_asset(world, asset->name, asset));
    ecs_entity entity = ecs_world_create_entity(ecs);
    assert(!game_behavior_attach(world, entity, asset->name));
    assert(!game_behavior_pump(world, 0.));
    game_behavior_attachment_info info;
    assert(!game_behavior_attachment_info_at(world, entity, 0, &info));
    int callbacks = 0;
    info.instance->profile = external_profile;
    info.instance->profile_user = &callbacks;
    uint64_t dispatched = behavior_batch_dispatches();
    assert(game_behavior_update(world, NULL) == 1);
    assert(callbacks == 2 && dispatched == behavior_batch_dispatches());
    game_behavior_world_free(world);
    behavior_asset_free(asset);
    ecs_world_free(ecs);
}

static void update_guards(behavior_backend backend)
{
    behavior_asset *asset = whole_batch_fixture(true);
    graph *g = asset->graph;
    node *sleep = graph_add_node(g, &NODE_Event),
         *delay = graph_add_node(g, &NODE_Delay);
    assert(!graph_set_event_name(g, sleep, "Sleep"));
    assert(!graph_set_pin_default(g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 1. }));
    assert(!graph_connect_exec(g, sleep, 0, delay, 0, 0));
    ecs_world *ecs = ecs_world_new(), *alternate = ecs_world_new();
    const ecs_component_desc *component = WHOLE_NODE_GetRecord.component;
    assert(!ecs_world_register_component(ecs, component, NULL));
    assert(!ecs_world_register_component(alternate, component, NULL));
    game_behavior_world *world = game_behavior_world_new(ecs);
    game_behavior_set_delta_seconds(world, .125);
    assert(!game_behavior_register_asset(world, asset->name, asset));
    behavior_backend previous = behavior_debug_backend(backend);
    behavior_instance *instances[3];
    ecs_entity entities[3];
    for (int i = 0; i < 3; i++) {
        entities[i] = ecs_world_create_entity(ecs);
        assert(
            ecs_entity_to_u64(ecs_world_create_entity(alternate)) ==
            ecs_entity_to_u64(entities[i])
        );
        whole_record record = { .weight = i }, other = { .weight = 100 + i };
        assert(!ecs_world_record_add(ecs, entities[i], component->id, &record, sizeof record));
        assert(!ecs_world_record_add(alternate, entities[i], component->id, &other, sizeof other));
        assert(!game_behavior_attach(world, entities[i], asset->name));
        instances[i] = game_behavior_instance(world, entities[i], asset->name);
        pin_value target = { .type = PIN_HANDLE,
                             .type_name = "ecs.entity",
                             .u64 = ecs_entity_to_u64(entities[i]) };
        assert(!behavior_instance_set_variable(instances[i], asset->variables[1].id, &target));
    }
    behavior_debug_backend(previous);
    assert(!ecs_world_apply_commands(ecs) && !ecs_world_apply_commands(alternate));
    assert(!game_behavior_pump(world, 0.));
    assert(game_behavior_update(world, NULL) == 3);
    assert(game_behavior_last_update(world).prepared == 3);
    assert(game_behavior_last_update(world).native_loop == (backend == BEHAVIOR_SLJIT ? 3 : 0));
    assert(!game_behavior_profile(world)->count);
    assert(!behavior_instance_bind_service(instances[1], "ecs.world", alternate));
    assert(game_behavior_update(world, NULL) == 3);
    assert(game_behavior_last_update(world).prepared == 3);
    for (int i = 0; i < 3; i++) {
        const whole_record
            *record = ecs_component_data_const(ecs, entities[i], component->id),
            *other = ecs_component_data_const(alternate, entities[i], component->id);
        assert(record->weight == i + (i == 1 ? .125 : .25));
        assert(other->weight == 100 + i + (i == 1 ? .125 : 0));
    }
    /* Rebinding invalidates every cached use before the old world is freed. */
    assert(!behavior_instance_bind_service(instances[1], "ecs.world", ecs));
    ecs_world_free(alternate);
    flow_run *wait = behavior_instance_dispatch(instances[1], "Sleep", (flow_inputs){ 0 }, NULL);
    assert(wait && flow_run_status(wait) == FLOW_SUSPENDED);
    assert(game_behavior_update(world, NULL) == 3);
    assert(game_behavior_last_update(world).prepared == 2);
    assert(!behavior_instance_stop_activation(instances[1], wait));
    assert(!behavior_instance_release_activation(instances[1], wait));
    assert(game_behavior_update(world, NULL) == 3);
    assert(game_behavior_last_update(world).prepared == 3);
    /* Storage can grow without invalidating the schedule. A dead handle must
     * still fail its native guard if the slot is reused by a new generation. */
    assert(!ecs_world_reserve_entities(ecs, 20000));
    assert(!ecs_world_destroy_entity(ecs, entities[1]));
    assert(!ecs_world_apply_commands(ecs));
    ecs_entity reused = ecs_world_create_entity(ecs);
    assert(reused.slot == entities[1].slot && reused.generation != entities[1].generation);
    assert(game_behavior_update(world, NULL) == 2);
    assert(instances[1]->variables[0].d == 10.5);
    assert(instances[0]->variables[0].d == 10.625 && instances[2]->variables[0].d == 10.625);
    game_behavior_world_free(world);
    behavior_asset_free(asset);
    ecs_world_free(ecs);
}

/* A failed middle row must retain its one partial write, stop later rows,
 * and preserve the same source error as an ordinary compiled activation. */
static void system_failure(bool interleaved)
{
    behavior_asset *asset = whole_batch_fixture(true);
    behavior_asset *other = interleaved ? whole_batch_fixture(true) : asset;
    if (interleaved) {
        assert(!behavior_asset_set_name(other, "Second Tick"));
    }
    ecs_world *ecs = ecs_world_new();
    const ecs_component_desc *component = WHOLE_NODE_GetRecord.component;
    assert(!ecs_world_register_component(ecs, component, NULL));
    behavior_instance *instances[3];
    behavior_update_row rows[3];
    behavior_update_binding **bindings = NULL;
    node *entry = tick_entry(asset);
    const behavior_function *f = behavior_program_batch_event(asset->program, "Tick", &entry);
    const behavior_function *functions[2] = { f, NULL };
    node *other_entry = interleaved ? tick_entry(other) : entry;
    functions[1] = behavior_program_batch_event(other->program, "Tick", &other_entry);
    for (int i = 0; i < 3; i++) {
        ecs_entity entity = ecs_world_create_entity(ecs);
        whole_record record = { .weight = i };
        assert(!ecs_world_record_add(ecs, entity, component->id, &record, sizeof record));
        pin_value owner = { .type = PIN_HANDLE,
                            .type_name = "ecs.entity",
                            .u64 = ecs_entity_to_u64(entity) };
        instances[i] = behavior_instance_create(i == 1 ? other : asset, &owner, NULL, NULL);
        assert(!behavior_instance_bind_service(instances[i], "ecs.world", ecs));
        behavior_instance_set_trace_enabled(instances[i], false);
        behavior_instance_set_delta_seconds(instances[i], .125);
        pin_value target = owner;
        if (i == 1) {
            target.u64 = 0;
        }
        assert(!behavior_instance_set_variable(instances[i], asset->variables[1].id, &target));
        rows[i] = (behavior_update_row){ .instance = instances[i],
                                         .binding = behavior_update_prepare(
                                             &bindings,
                                             instances[i],
                                             functions[i % 2],
                                             i == 1 ? other_entry : entry
                                         ),
                                         .services_revision = instances[i]->services_revision,
                                         .entity = owner.u64 };
        assert(rows[i].binding);
    }
    assert(!ecs_world_apply_commands(ecs));
    behavior_update_refresh(bindings);
    void *code = interleaved ? behavior_function_compile_sequence(functions, 2)
                             : behavior_function_system_entry(f);
    int result;
    if (interleaved) {
        behavior_update_row *wrong = ((
            behavior_update_fn
        )code)(rows + 1, rows + 3, (const ecs_execution_view *)ecs, &result);
        assert(
            wrong == rows + 1 && result == FLOW_OK && instances[1]->variables[0].d == 0.
        );
    }
    behavior_update_row *stopped = ((
        behavior_update_fn
    )code)(rows, rows + 3, (const ecs_execution_view *)ecs, &result);
    assert(stopped == rows + 1 && result < 0);
    assert(
        instances[0]->variables[0].d == .125 &&
        instances[1]->variables[0].d == .125 && instances[2]->variables[0].d == 0
    );
    flow_run *failed = behavior_update_failure(stopped, result);
    assert(flow_run_status(failed) == FLOW_FAILED && flow_run_error(failed)->node);
    assert(!behavior_instance_release_activation(instances[1], failed));
    if (interleaved) {
        stopped = ((
            behavior_update_fn
        )code)(rows + 2, rows + 3, (const ecs_execution_view *)ecs, &result);
        assert(
            stopped == rows + 3 && result == FLOW_OK &&
            instances[2]->variables[0].d == .125
        );
    }
    /* Budget rejection, in either the kernel or its checked fallback, must
     * stop before a variable write and retain the ordinary diagnostic. */
    rows[0].binding->context.budget = 0;
    stopped = ((
        behavior_update_fn
    )code)(rows, rows + 3, (const ecs_execution_view *)ecs, &result);
    assert(stopped == rows && result == FLOW_ERR_STEP_BUDGET);
    assert(instances[0]->variables[0].d == .125);
    if (interleaved) {
        behavior_update_free_code(code);
    }
    behavior_update_free_bindings(bindings);
    for (int i = 0; i < 3; i++) {
        behavior_instance_destroy(instances[i]);
    }
    ecs_world_free(ecs);
    if (interleaved) {
        behavior_asset_free(other);
    }
    behavior_asset_free(asset);
}

static void shared_clock_dispatch(behavior_backend backend)
{
    behavior_asset *asset = whole_batch_fixture(false);
    behavior_clock clock = { .current_time = 7, .delta_seconds = .25 };
    behavior_instance *instances[2];
    for (int i = 0; i < 2; i++) {
        instances[i] = behavior_instance_create(asset, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(instances[i], backend));
        behavior_instance_bind_clock(instances[i], &clock);
        behavior_instance_set_trace_enabled(instances[i], false);
        flow_run *run = behavior_instance_dispatch(instances[i], "Begin", (flow_inputs){ 0 }, NULL);
        assert(run && flow_run_status(run) == FLOW_COMPLETED);
        assert(!behavior_instance_release_activation(instances[i], run));
    }
    behavior_instance *independent = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(independent, backend));
    behavior_instance_set_delta_seconds(independent, .125);
    flow_run *begin = behavior_instance_dispatch(independent, "Begin", (flow_inputs){ 0 }, NULL);
    assert(begin && flow_run_status(begin) == FLOW_COMPLETED);
    assert(!behavior_instance_release_activation(independent, begin));
    double expected[] = { 10.25, 10.75 };
    behavior_batch *batch = NULL;
    for (int frame = 0; frame < 2; frame++) {
        clock.delta_seconds = frame ? .5 : .25;
        for (int i = 0; i < 2; i++) {
            flow_run *run = NULL;
            if (i && backend != BEHAVIOR_ORACLE) {
                assert(behavior_instance_dispatch_batch(instances[i], "Tick", &batch, &run));
                assert(!run);
            } else {
                run = behavior_instance_dispatch(instances[i], "Tick", (flow_inputs){ 0 }, NULL);
                assert(run && flow_run_status(run) == FLOW_COMPLETED);
                assert(!behavior_instance_release_activation(instances[i], run));
            }
            pin_value value;
            assert(!behavior_instance_get_variable(
                instances[i],
                asset->variables[0].id,
                &value
            ));
            assert(value.d == expected[frame]);
        }
        flow_run *run = behavior_instance_dispatch(independent, "Tick", (flow_inputs){ 0 }, NULL);
        assert(run && flow_run_status(run) == FLOW_COMPLETED);
        pin_value value;
        assert(!behavior_instance_get_variable(independent, asset->variables[0].id, &value));
        assert(value.d == (frame ? 10.25 : 10.125));
        assert(!behavior_instance_release_activation(independent, run));
    }
    behavior_batch_free(batch);
    behavior_instance_destroy(independent);
    for (int i = 0; i < 2; i++) {
        behavior_instance_destroy(instances[i]);
    }
    behavior_asset_free(asset);
}

void test_behavior_batches()
{
    for (int backend = 0; backend < 3; backend++) {
        behavior_backend mode = (behavior_backend[]){ BEHAVIOR_SLJIT,
                                                      BEHAVIOR_BYTECODE,
                                                      BEHAVIOR_ORACLE }[backend];
        shared_clock_dispatch(mode);
        for (int batch = 0; batch < 2; batch++) {
            for (int component = 0; component < 2; component++) {
                for (int profile = 0; profile < 2; profile++) {
                    for (int update = 0; update < 2; update++) {
                        world_parity(mode, batch, component, profile, update);
                    }
                }
            }
        }
        if (mode == BEHAVIOR_ORACLE) {
            continue;
        }
        failure_parity(mode, false, false);
        failure_parity(mode, false, true);
        failure_parity(mode, true, false);
        queued_wait(mode);
        exclusions(mode);
        unread_event_output(mode);
        indexed_events(mode);
        update_guards(mode);
    }
    test_tick_sequences();
    test_parallel_ticks();
    system_failure(false);
    system_failure(true);
    observed_profile();
    watched_event_output();
    whole_batch_smoke();
    puts("PASS: ordered Tick batches, instance state, component dependencies, inspection, profiling, waits and failure snapshots");
}
