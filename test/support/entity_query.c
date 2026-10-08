#include "runtime/behaviors/entity_query.h"

#include "graph/authoring/asset_edit.h"
#include "graph/authoring/graph_edit.h"
#include "graph/compiler/behavior_function_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/instance.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "types/array.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static behavior_asset *fixture(const EntityCondition *conditions, int count, bool dynamic)
{
    behavior_asset *a = behavior_asset_new("Reflected entity count");
    graph *g = behavior_asset_add_function(a, "Count", false);
    pin_spec entities = { .name = "Entities",
                          .type = PIN_HANDLE,
                          .type_name = "ecs.entity",
                          .container = PIN_CONTAINER_ARRAY };
    pin_spec result = { .name = "Count", .type = PIN_I32, .type_name = "int" };
    assert(graph_add_pin(g, g->nodes[0], true, &entities) == 1);
    assert(graph_add_pin(g, g->nodes[1], false, &result) == 1);
    node *n = graph_add_node(g, &NODE_Count_Matching_Entities);
    assert(!graph_connect_exec(g, g->nodes[0], 0, g->nodes[1], 0, 0));
    assert(!graph_connect_data(g, g->nodes[0], 1, n, 0));
    assert(!graph_connect_data(g, n, 0, g->nodes[1], 1));
    pin_array array = { .element_type = PIN_STRUCT,
                        .type_name = "EntityCondition",
                        .count = count };
    for (int i = 0; i < count; i++) {
        apush(
            array.items,
            ((pin_value){ .type = PIN_STRUCT,
                          .type_name = "EntityCondition",
                          .ptr = (void *)&conditions[i] })
        );
    }
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = "EntityCondition",
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = &array };
    if (dynamic) {
        behavior_variable *v = behavior_asset_add_variable(
            a,
            "Conditions",
            PIN_STRUCT,
            "EntityCondition",
            PIN_CONTAINER_ARRAY,
            &value,
            ""
        );
        assert(v);
        node *get = graph_add_node(g, &NODE_GetVariable);
        assert(!graph_set_node_reference(g, get, v->id, v->name));
        assert(!graph_set_pin_type(g, get, true, 0, PIN_STRUCT, "EntityCondition"));
        assert(!graph_set_pin_container(g, get, true, 0, PIN_CONTAINER_ARRAY));
        assert(get && !graph_connect_data(g, get, 0, n, 1));
    } else {
        assert(!graph_set_pin_default(g, n, false, 1, &value));
    }
    afree(array.items);
    return a;
}
void test_entity_query_diagnostics()
{
    behavior_asset *asset = fixture(NULL, 0, true);
    graph *g = asset->graphs[1];
    node *count = g->nodes[2];
    node *query = graph_add_node(g, registry_node_by_name("Find Entities With Component"));
    assert(!graph_remove_edge(g, g->nodes[0], 1, count, 0));
    assert(!graph_connect_data(g, query, 0, count, 0));
    /* An unused world query must not be reported as Tick work. */
    node *unused = graph_add_node(g, query->desc);
    node *event = graph_add_node(asset->graph, &NODE_Event);
    assert(!graph_set_event_name(asset->graph, event, "Tick"));
    node *call = behavior_asset_add_function_call_node_to_graph(asset, asset->graph, g);
    assert(call && !graph_connect_exec(asset->graph, event, 0, call, 0, 0));
    for (int reachable = 1; reachable >= 0; reachable--) {
        compile_report report;
        compile_report_init(&report);
        assert(!behavior_asset_compile(asset, &report));
        int scans = 0;
        assert(!report.note_count);
        for (int i = 0; i < acount(report.items); i++) {
            const compile_diagnostic *d = &report.items[i];
            if (d->rule != CMPL_RULE_QUERY_SCAN) {
                continue;
            }
            scans++;
            assert(d->severity == CMPL_WARNING);
            assert(d->graph_id == g->graph_id && d->node_id != unused->id);
            if (d->node_id == count->id) {
                assert(d->pin_id == count->input_pins[1].id && strstr(d->message, "Dynamic"));
            } else {
                assert(reachable && d->node_id == query->id);
                assert(d->pin_id == query->output_pins[0].id && strstr(d->message, "Tick"));
            }
        }
        assert(scans == 1 + reachable);
        compile_report_free(&report);
        if (reachable) {
            assert(!graph_remove_edge(asset->graph, event, 0, call, 0));
        }
    }
    behavior_asset_free(asset);
}
static void failure_lifetime(behavior_backend backend)
{
    behavior_asset *asset = fixture(NULL, 0, false);
    graph *g = asset->graphs[1];
    uint64_t source_graph = g->graph_id, source_node = g->nodes[2]->id;
    node *event = graph_add_node(asset->graph, &NODE_Event);
    assert(!graph_set_event_name(asset->graph, event, "Tick"));
    node *call = behavior_asset_add_function_call_node_to_graph(asset, asset->graph, g);
    assert(call && !graph_connect_exec(asset->graph, event, 0, call, 0, 0));
    behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(bi, backend));
    behavior_instance_set_trace_enabled(bi, false);
    assert(!behavior_instance_set_activation_pool(bi, true));
    assert(!behavior_instance_last_failure(bi));
    flow_run *run = behavior_instance_dispatch(bi, "Tick", (flow_inputs){ 0 }, NULL);
    assert(run && flow_run_status(run) == FLOW_FAILED);
    const behavior_instance_failure *f = behavior_instance_last_failure(bi);
    assert(
        f && f->code == flow_run_error(run)->code &&
        f->steps == flow_run_error(run)->steps
    );
    assert(f->node_id == source_node && f->graph_id == source_graph && f->backend == backend);
    assert(!strcmp(f->event_name, "Tick") && !strcmp(f->graph_name, "Count"));
    assert(strstr(f->message, "ecs.world"));
    assert(!behavior_instance_release_activation(bi, run));
    assert(!acount(bi->activations) && behavior_instance_last_failure(bi) == f);
    assert(!strcmp(f->event_name, "Tick"));
    ecs_world *world = ecs_world_new();
    assert(!behavior_instance_bind_service(bi, "ecs.world", world));
    run = behavior_instance_dispatch(bi, "Tick", (flow_inputs){ 0 }, NULL);
    assert(run && flow_run_status(run) == FLOW_COMPLETED);
    flow_run_free(run);
    assert(behavior_instance_last_failure(bi) == f);
    /* Failure snapshots must outlive replacement of the same native program.
     * Source/graph roundtrip tests cover source persistence separately. */
    behavior_asset *next = behavior_asset_clone(asset);
    assert(next && behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_READY);
    behavior_instance_update(bi, next);
    behavior_asset_free(asset);
    assert(behavior_instance_last_failure(bi) == f && !strcmp(f->graph_name, "Count"));
    assert(strstr(f->message, "ecs.world") && f->node_id == source_node);
    behavior_instance_clear_failure(bi);
    assert(!behavior_instance_last_failure(bi));
    assert(!behavior_instance_bind_service(bi, "ecs.world", NULL));
    run = behavior_instance_dispatch(bi, "Tick", (flow_inputs){ 0 }, NULL);
    assert(run && flow_run_status(run) == FLOW_FAILED);
    flow_run_free(run);
    assert(behavior_instance_last_failure(bi));
    assert(!behavior_instance_pool_release(bi));
    assert(behavior_instance_pool_reuse(bi, next, NULL, NULL, NULL) == bi);
    assert(!behavior_instance_last_failure(bi));
    behavior_instance_destroy(bi);
    behavior_asset_free(next);
    ecs_world_free(world);
}
behavior_asset *entity_query_fusion_fixture()
{
    const EntityCondition criteria[] = {
        { GAME_COMPONENT_GameHealth.key, "current", ">", 0 },
        { GAME_COMPONENT_GameMovement.key, "speed", ">=", 2 },
        { GAME_COMPONENT_GameHealth.key, "", "missing", 0 },
        { GAME_COMPONENT_GameCollider.key, "category", ">", INT_MAX }
    };
    behavior_asset *asset = fixture(criteria, 1, false);
    graph *g = asset->graphs[1];
    for (int j = 1; j < 4; j++) {
        node *n = graph_add_node(g, &NODE_Count_Matching_Entities);
        pin_spec result = {
            .name = (const char *[]){ "Living", "Moving", "Missing", "Category" }[j],
            .type = PIN_I32,
            .type_name = "int"
        };
        assert(graph_add_pin(g, g->nodes[1], false, &result) == j + 1);
        assert(!graph_connect_data(g, g->nodes[0], 1, n, 0));
        assert(!graph_connect_data(g, n, 0, g->nodes[1], j + 1));
        pin_value item = { .type = PIN_STRUCT,
                           .type_name = "EntityCondition",
                           .ptr = (void *)&criteria[j] };
        pin_array conditions = { .element_type = PIN_STRUCT,
                                 .type_name = "EntityCondition",
                                 .count = 1,
                                 .items = &item };
        pin_value value = { .type = PIN_STRUCT,
                            .type_name = "EntityCondition",
                            .container = PIN_CONTAINER_ARRAY,
                            .ptr = &conditions };
        assert(!graph_set_pin_default(g, n, false, 1, &value));
    }
    return asset;
}
static void fused_counts(ecs_world *world, pin_array *input, behavior_backend backend)
{
    const int expected[] = { 4, 3, 1, 2 };
    behavior_asset *asset = entity_query_fusion_fixture();
    graph *g = asset->graphs[1];
    compile_report report;
    compile_report_init(&report);
    assert(!behavior_asset_compile(asset, &report));
    if (report.note_count != 4 || report.warning_count) {
        for (int i = 0; i < acount(report.items); i++) {
            fprintf(
                stderr,
                "query diagnostic: %d %llu %s\n",
                report.items[i].severity,
                (unsigned long long)report.items[i].node_id,
                report.items[i].message
            );
        }
    }
    assert(report.note_count == 4 && !report.warning_count);
    for (int i = 0; i < acount(report.items); i++) {
        const compile_diagnostic *d = &report.items[i];
        assert(
            d->graph_id == g->graph_id && d->node_id && d->pin_id &&
            d->rule == CMPL_RULE_QUERY_FUSION && strstr(d->message, "4 compatible")
        );
    }
    compile_report_free(&report);
    behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(bi && !behavior_instance_set_backend(bi, backend));
    assert(!behavior_instance_bind_service(bi, "ecs.world", world));
    const behavior_function *compiled = behavior_program_function(
        behavior_program_graph(behavior_program_cached(asset), g),
        g->nodes[0]
    );
    assert(compiled);
    int rows = 0, columns = 0;
    for (int pc = 0; pc < acount(compiled->instructions); pc++) {
        rows += compiled->instructions[pc].op == BF_ROW;
        columns += compiled->instructions[pc].op == BF_COLUMN_TRY;
    }
    /* Four original rows remain for observation/failure fallback. The fused
     * traversal needs only three: living and missing share the health row. */
    assert(rows <= 7 && columns <= 3);
    for (int inspected = 0; inspected < 2; inspected++) {
        behavior_instance_set_trace_enabled(bi, inspected);
        pin_value args = { .type = PIN_HANDLE,
                           .type_name = "ecs.entity",
                           .container = PIN_CONTAINER_ARRAY,
                           .ptr = input };
        flow_options options = { .instance = bi };
        flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &args, 1 }, &options, NULL);
        assert(run && !flow_run_resume(run));
        for (int j = 0; j < 4; j++) {
            assert(flow_run_input_value(run, g->nodes[1], j + 1)->i == expected[j]);
        }
        flow_run_free(run);
    }
    ecs_world *missing = ecs_world_new();
    assert(!ecs_world_register_component(missing, &GAME_COMPONENT_GameHealth, NULL));
    for (int scenario = 0; scenario < 3; scenario++) {
        assert(!behavior_instance_bind_service(
            bi,
            "ecs.world",
            scenario == 0   ? world
            : scenario == 1 ? missing
                            : NULL
        ));
        behavior_instance_set_trace_enabled(bi, false);
        pin_value args = { .type = PIN_HANDLE,
                           .type_name = "ecs.entity",
                           .container = PIN_CONTAINER_ARRAY };
        flow_options options = { .instance = bi };
        flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &args, 1 }, &options, NULL);
        assert(run);
        int result = flow_run_resume(run);
        if (!scenario) {
            assert(!result);
            for (int j = 0; j < 4; j++) {
                assert(flow_run_input_value(run, g->nodes[1], j + 1)->i == 0);
            }
        } else {
            assert(result < 0 && flow_run_error(run)->node == g->nodes[scenario == 1 ? 3 : 2]);
        }
        flow_run_free(run);
    }
    ecs_world_free(missing);
    behavior_instance_destroy(bi);
    /* The same query is evaluated before a write and again at Return. Only
     * the four reads after the write may share a traversal. */
    node *set = graph_add_node(g, registry_node_by_name("Set_GameHealth_current"));
    node *negate = graph_add_node(g, registry_node_by_name("Negate"));
    assert(!graph_set_pin_type(g, negate, false, 0, PIN_I32, "int"));
    assert(!graph_set_pin_type(g, negate, true, 0, PIN_I32, "int"));
    assert(!graph_connect_data(g, g->nodes[2], 0, negate, 0));
    assert(!graph_connect_data(g, negate, 0, set, 1));
    assert(!graph_set_pin_default(g, set, false, 2, &input->items[0]));
    assert(!graph_remove_edge(g, g->nodes[0], 0, g->nodes[1], 0));
    assert(!graph_connect_exec(g, g->nodes[0], 0, set, 0, 0));
    assert(!graph_connect_exec(g, set, 0, g->nodes[1], 0, 0));
    bi = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(bi && !behavior_instance_set_backend(bi, backend));
    behavior_instance_set_trace_enabled(bi, false);
    assert(!behavior_instance_bind_service(bi, "ecs.world", world));
    GameHealth
        *health = GAME_GameHealth_get(world, ecs_entity_from_u64(input->items[0].u64));
    int before = health->current;
    pin_value args = { .type = PIN_HANDLE,
                       .type_name = "ecs.entity",
                       .container = PIN_CONTAINER_ARRAY,
                       .ptr = input };
    flow_options options = { .instance = bi };
    flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &args, 1 }, &options, NULL);
    assert(run && !flow_run_resume(run));
    assert(health->current == -4 && flow_run_input_value(run, g->nodes[1], 1)->i == 2);
    flow_run_free(run);
    health->current = before;
    const compile_report *published = behavior_program_report(
        behavior_program_cached(asset)
    );
    assert(published->note_count == 4 && published->warning_count >= 1);
    behavior_instance_destroy(bi);
    behavior_asset_free(asset);
}
static void shared_predicate_rows(ecs_world *world, pin_array *input, behavior_backend backend)
{
    const EntityCondition first[] = {
        { GAME_COMPONENT_GameHealth.key, "", "missing", 0 },
        { GAME_COMPONENT_GameMovement.key, "speed", ">=", 2 }
    };
    const EntityCondition second[] = {
        { GAME_COMPONENT_GameMovement.key, "speed", ">=", 2 },
        { GAME_COMPONENT_GameHealth.key, "current", ">", 0 }
    };
    behavior_asset *asset = fixture(first, 2, false);
    graph *g = asset->graphs[1];
    node *count = graph_add_node(g, &NODE_Count_Matching_Entities);
    pin_spec result = { .name = "Living and moving", .type = PIN_I32, .type_name = "int" };
    assert(graph_add_pin(g, g->nodes[1], false, &result) == 2);
    assert(!graph_connect_data(g, g->nodes[0], 1, count, 0));
    assert(!graph_connect_data(g, count, 0, g->nodes[1], 2));
    pin_value items[] = {
        { .type = PIN_STRUCT, .type_name = "EntityCondition", .ptr = (void *)&second[0] },
        { .type = PIN_STRUCT, .type_name = "EntityCondition", .ptr = (void *)&second[1] }
    };
    pin_array conditions = { .element_type = PIN_STRUCT,
                             .type_name = "EntityCondition",
                             .count = 2,
                             .items = items };
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = "EntityCondition",
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = &conditions };
    assert(!graph_set_pin_default(g, count, false, 1, &value));
    behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
    assert(bi && !behavior_instance_set_backend(bi, backend));
    assert(!behavior_instance_bind_service(bi, "ecs.world", world));
    for (int inspected = 0; inspected < 2; inspected++) {
        behavior_instance_set_trace_enabled(bi, inspected);
        pin_value args = { .type = PIN_HANDLE,
                           .type_name = "ecs.entity",
                           .container = PIN_CONTAINER_ARRAY,
                           .ptr = input };
        flow_options options = { .instance = bi };
        flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &args, 1 }, &options, NULL);
        assert(run && !flow_run_resume(run));
        assert(flow_run_input_value(run, g->nodes[1], 1)->i == 0);
        assert(flow_run_input_value(run, g->nodes[1], 2)->i == 3);
        flow_run_free(run);
    }
    const compile_report *report = behavior_program_report(behavior_program_cached(asset));
    assert(report->note_count == 2 && !report->warning_count);
    behavior_instance_destroy(bi);
    behavior_asset_free(asset);
}

static void separate_snapshots(ecs_world *world, pin_array *input, behavior_backend backend)
{
    behavior_asset *a = entity_query_fusion_fixture();
    graph *g = a->graphs[1];
    pin_spec second = g->nodes[0]->output_pins[1];
    second.name = "Other entities";
    second.id = 0;
    assert(graph_add_pin(g, g->nodes[0], true, &second) == 2);
    for (int i = 4; i < 6; i++) {
        assert(!graph_remove_edge(g, g->nodes[0], 1, g->nodes[i], 0));
        assert(!graph_connect_data(g, g->nodes[0], 2, g->nodes[i], 0));
    }
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(bi && !behavior_instance_set_backend(bi, backend));
    behavior_instance_set_trace_enabled(bi, false);
    assert(!behavior_instance_bind_service(bi, "ecs.world", world));
    pin_value args[] = { { .type = PIN_HANDLE,
                           .type_name = "ecs.entity",
                           .container = PIN_CONTAINER_ARRAY,
                           .ptr = input },
                         { .type = PIN_HANDLE,
                           .type_name = "ecs.entity",
                           .container = PIN_CONTAINER_ARRAY } };
    flow_options options = { .instance = bi };
    flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ args, 2 }, &options, NULL);
    assert(run && !flow_run_resume(run));
    const int expected[] = { 4, 3, 0, 0 };
    for (int i = 0; i < 4; i++) {
        assert(flow_run_input_value(run, g->nodes[1], i + 1)->i == expected[i]);
    }
    const compile_report *report = behavior_program_report(behavior_program_cached(a));
    assert(report->note_count == 4);
    for (int i = 0; i < acount(report->items); i++) {
        assert(strstr(report->items[i].message, "2 compatible"));
    }
    flow_run_free(run);
    behavior_instance_destroy(bi);
    behavior_asset_free(a);
}
static void invalid_conditions(behavior_backend backend)
{
    EntityCondition cases[] = {
        { "absent-component", "", "has", 0 },
        { GAME_COMPONENT_GameHealth.key, "absent-field", "==", 0 },
        { GAME_COMPONENT_GameHealth.key, "current", "has", 0 },
        { GAME_COMPONENT_GameHealth.key, "current", "approx", 0 }
    };
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL));
    for (int dynamic = 0; dynamic < 2; dynamic++) {
        for (int i = 0; i < 4; i++) {
            behavior_asset *asset = fixture(&cases[i], 1, dynamic);
            behavior_instance *bi = behavior_instance_create(asset, NULL, NULL, NULL);
            assert(!behavior_instance_set_backend(bi, backend));
            behavior_instance_set_trace_enabled(bi, false);
            assert(!behavior_instance_bind_service(bi, "ecs.world", world));
            graph *g = asset->graphs[1];
            flow_options options = { .instance = bi };
            flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ 0 }, &options, NULL);
            assert(run && flow_run_resume(run) < 0);
            assert(
                flow_run_error(run)->node == g->nodes[2] && flow_run_error(run)->message
            );
            flow_run_free(run);
            const behavior_instance_failure *failure = behavior_instance_last_failure(bi);
            assert(
                failure && failure->node_id == g->nodes[2]->id &&
                failure->graph_id == g->graph_id
            );
            behavior_instance_destroy(bi);
            behavior_asset_free(asset);
        }
    }
    ecs_world_free(world);
}

void test_entity_query(behavior_backend backend)
{
    invalid_conditions(backend);
    failure_lifetime(backend);
    ecs_world *world = ecs_world_new();
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameHealth, NULL));
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameMovement, NULL));
    assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameCollider, NULL));
    int health[] = { 7, 0, 0, -2, INT_MAX };
    float movement[] = { 2.5f, -0.f, NAN, 1.5f, INFINITY };
    pin_array input = { .element_type = PIN_HANDLE, .type_name = "ecs.entity", .count = 8 };
    for (int i = 0; i < 5; i++) {
        ecs_entity entity = ecs_world_create_entity(world);
        if (i != 2) {
            assert(!ecs_world_record_add(
                world,
                entity,
                GAME_COMPONENT_GameHealth.id,
                &(GameHealth){ .current = health[i] },
                0
            ));
        }
        assert(!ecs_world_record_add(
            world,
            entity,
            GAME_COMPONENT_GameMovement.id,
            &(GameMovement){ .speed = movement[i] },
            0
        ));
        assert(!ecs_world_record_add(
            world,
            entity,
            GAME_COMPONENT_GameCollider.id,
            &(GameCollider){ .category = i ? 1 : UINT_MAX, .sensor = i == 0 },
            0
        ));
        assert(!ecs_world_apply_commands(world));
        apush(
            input.items,
            ((pin_value){ .type = PIN_HANDLE,
                          .type_name = "ecs.entity",
                          .u64 = ecs_entity_to_u64(entity) })
        );
    }
    ecs_entity stale = ecs_world_create_entity(world);
    assert(!ecs_world_destroy_entity(world, stale));
    ecs_entity replacement = ecs_world_create_entity(world);
    assert(replacement.slot == stale.slot && replacement.generation != stale.generation);
    assert(!ecs_world_record_add(
        world,
        replacement,
        GAME_COMPONENT_GameHealth.id,
        &(GameHealth){ .current = 10 },
        0
    ));
    assert(!ecs_world_apply_commands(world));
    apush(
        input.items,
        ((pin_value){ .type = PIN_HANDLE,
                      .type_name = "ecs.entity",
                      .u64 = ecs_entity_to_u64(stale) })
    );
    apush(
        input.items,
        ((pin_value){ .type = PIN_HANDLE,
                      .type_name = "ecs.entity",
                      .u64 = ecs_entity_to_u64(replacement) })
    );
    apush(input.items, input.items[0]);
    EntityCondition conditions[][2] = {
        { { GAME_COMPONENT_GameHealth.key, "current", ">", 0 } },
        { { GAME_COMPONENT_GameHealth.key, "", "has", 0 } },
        { { GAME_COMPONENT_GameHealth.key, "", "missing", 0 } },
        { { GAME_COMPONENT_GameHealth.key, "current", "<", 0 } },
        { { GAME_COMPONENT_GameMovement.key, "speed", ">=", 2 } },
        { { GAME_COMPONENT_GameMovement.key, "speed", "!=", 0 } },
        { { GAME_COMPONENT_GameMovement.key, "", "missing", 0 } },
        { { GAME_COMPONENT_GameHealth.key, "current", ">", 0 },
          { GAME_COMPONENT_GameMovement.key, "speed", ">=", 2 } },
        { { 0 } },
        { { GAME_COMPONENT_GameCollider.key, "category", ">", INT_MAX } },
        { { GAME_COMPONENT_GameCollider.key, "sensor", "==", 1 } },
        { { GAME_COMPONENT_GameMovement.key, "speed", "<", 2 } },
        { { GAME_COMPONENT_GameMovement.key, "speed", "<=", 2 } },
        { { GAME_COMPONENT_GameMovement.key, "speed", ">", 2 } },
        { { GAME_COMPONENT_GameMovement.key, "speed", ">=", 2 } },
        { { GAME_COMPONENT_GameMovement.key, "speed", "==", 2 } },
        { { GAME_COMPONENT_GameMovement.key, "speed", "!=", 2 } }
    };
    int expected[] = { 4, 6, 1, 1, 3, 5, 1, 3, 7, 2, 2, 2, 2, 3, 3, 0, 6 };
    for (int variant = 0; variant < 4; variant++) {
        int dynamic = variant % 2;
        for (int test = 0; test < (int)(sizeof expected / sizeof *expected); test++) {
            behavior_asset *a = fixture(
                conditions[test],
                test == 8   ? 0
                : test == 7 ? 2
                            : 1,
                dynamic
            );
            behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
            assert(bi && !behavior_instance_set_backend(bi, backend));
            behavior_instance_set_trace_enabled(bi, variant >= 2);
            assert(!behavior_instance_bind_service(bi, "ecs.world", world));
            graph *g = a->graphs[1];
            const behavior_function *f = behavior_program_function(
                behavior_program_graph(behavior_program_cached(a), g),
                g->nodes[0]
            );
            if (!f || acount(f->host_calls) != dynamic) {
                const compile_diagnostic *d = behavior_program_diagnostic(
                    behavior_program_cached(a)
                );
                fprintf(
                    stderr,
                    "query test %d dynamic %d calls %d: %s\n",
                    test,
                    dynamic,
                    f ? acount(f->host_calls) : -1,
                    d ? d->message : "no diagnostic"
                );
            }
            assert(f && acount(f->host_calls) == dynamic);
            pin_value args = { .type = PIN_HANDLE,
                               .type_name = "ecs.entity",
                               .container = PIN_CONTAINER_ARRAY,
                               .ptr = &input };
            flow_options options = { .instance = bi };
            uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
            flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ &args, 1 }, &options, NULL);
            assert(run && !flow_run_resume(run));
            int actual = flow_run_input_value(run, g->nodes[1], 1)->i;
            if (actual != expected[test]) {
                fprintf(
                    stderr,
                    "query backend=%s test=%d dynamic=%d actual=%d "
                    "expected=%d\n",
                    behavior_backend_name(backend),
                    test,
                    dynamic,
                    actual,
                    expected[test]
                );
            }
            assert(actual == expected[test]);
            if (backend != BEHAVIOR_ORACLE) {
                assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
            }
            flow_run_free(run);
            behavior_instance_destroy(bi);
            behavior_asset_free(a);
        }
    }
    fused_counts(world, &input, backend);
    shared_predicate_rows(world, &input, backend);
    separate_snapshots(world, &input, backend);
    afree(input.items);
    ecs_world_free(world);
    printf(
        "PASS: %s reflected entity reductions, static native loops, dynamic "
        "helper, AND/presence, NaN, empty filters, duplicate and stale "
        "handles\n",
        behavior_backend_name(backend)
    );
}
