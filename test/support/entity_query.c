#include "runtime/behaviors/entity_query.h"

#include "graph/compiler/behavior_function_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/value_array.h"
#include "runtime/generated.h"
#include "support/array.h"

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
    char *json = behavior_asset_save_json(asset);
    behavior_asset *next = behavior_asset_load_json(json);
    free(json);
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
        { { GAME_COMPONENT_GameCollider.key, "sensor", "==", 1 } }
    };
    int expected[] = { 4, 6, 1, 1, 3, 5, 1, 3, 7, 2, 2 };
    for (int variant = 0; variant < 4; variant++) {
        int dynamic = variant % 2;
        for (int test = 0; test < 11; test++) {
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
                behavior_program_graph(a->program, g),
                g->nodes[0]
            );
            if (!f || acount(f->host_calls) != dynamic) {
                const compile_diagnostic *d = behavior_program_diagnostic(a->program);
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
    afree(input.items);
    ecs_world_free(world);
    printf(
        "PASS: %s reflected entity reductions, static native loops, dynamic "
        "helper, AND/presence, NaN, empty filters, duplicate and stale "
        "handles\n",
        behavior_backend_name(backend)
    );
}
