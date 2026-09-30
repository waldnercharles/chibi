#include "graph/model/registry.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/update_internal.h"
#include "runtime/behaviors/entities.h"
#include "runtime/task_pool.h"
#include "test/suite_internal.h"
#include "test/whole_aggregate_fixture.h"
#include "test/whole_array_op_fixture.h"
#include "test/whole_batch_fixture.h"

/* Independent instances, ordered attachments on each entity, and a shared
 * component target which must remain serial even on a large population. */
static void parallel_fixture(behavior_backend backend, int kind, bool parallel)
{
    enum
    {
        COUNT = 2049
    };
    behavior_backend previous = behavior_debug_backend(backend);
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    game_entities *queries = game_entities_new(ecs, world);
    game_behavior_bind_entities(world, queries);
    /* Query diagnostics must not serialize a batch that has no queries. */
    game_entities_profile_queries(queries, true);
    game_behavior_set_parallel_updates(world, parallel);
    behavior_asset *asset = kind == 2
                              ? whole_owned_tick_fixture(ARRAY_OP_STRINGS, false)
                              : whole_batch_fixture(kind == 1);
    const ecs_component_desc *component = WHOLE_NODE_GetRecord.component;
    assert(!ecs_world_register_component(ecs, component, NULL));
    assert(!game_behavior_register_asset(world, asset->name, asset));
    behavior_instance *instances[COUNT];
    ecs_entity entities[COUNT];
    for (int i = 0; i < COUNT; i++) {
        entities[i] = ecs_world_create_entity(ecs);
        whole_record record = { .weight = i * .5 };
        assert(!ecs_world_record_add(ecs, entities[i], component->id, &record, sizeof record));
    }
    assert(!ecs_world_apply_commands(ecs));
    for (int i = 0; i < COUNT; i++) {
        assert(!game_behavior_attach(world, entities[i], asset->name));
        instances[i] = game_behavior_instance(world, entities[i], asset->name);
        if (kind == 1) {
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
    assert(!game_behavior_pump(world, 0));
    game_task_pool *pool = game_task_pool_acquire();
    bool expected = parallel && backend == BEHAVIOR_SLJIT && kind == 2 &&
                    game_task_pool_tasks(pool)->workers > 1;
    game_task_pool_release(pool);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int frame = 0; frame < 8; frame++) {
        if (frame == 1 && kind == 2) {
            assert(game_behavior_detach_entity(world, entities[COUNT - 1]) == 1);
            assert(!game_behavior_attach(world, entities[COUNT - 1], asset->name));
            instances[COUNT - 1] = game_behavior_instance(
                world,
                entities[COUNT - 1],
                asset->name
            );
            assert(instances[COUNT - 1]);
        }
        if (frame == 3 || frame == 5) {
            assert(
                !game_behavior_set_debug_hold(world, entities[COUNT / 2], asset->name, frame == 3)
            );
        }
        if (frame == 6) {
            behavior_asset *next = kind == 2
                                     ? whole_owned_tick_fixture(ARRAY_OP_STRINGS, false)
                                     : whole_batch_fixture(kind == 1),
                           *old;
            assert(game_behavior_preview_asset(world, asset->name, next, &old) == INSTANCE_UPDATE_READY);
            behavior_asset_free(old);
            asset = next;
        }
        game_behavior_set_delta_seconds(world, .25);
        assert(game_behavior_update(world, NULL) == COUNT);
        game_behavior_update_stats stats = game_behavior_last_update(world);
        if (frame < 3 || frame > 5) {
            assert((stats.parallel == COUNT) == expected);
        } else {
            assert(!stats.parallel);
        }
        for (int i = 0; i < COUNT; i++) {
            if (kind == 2) {
                int expected = i == COUNT - 1 && frame > 0 ? frame : frame + 1;
                assert(instances[i]->variables[3].i == expected);
                assert(pin_array_equal(
                    instances[i]->variables[1].ptr,
                    instances[i]->variables[2].ptr
                ));
            } else {
                assert(instances[i]->variables[0].d == 10. + (frame + 1) * .25);
            }
            if (kind == 1) {
                const whole_record *record = ecs_component_data_const(
                    ecs,
                    entities[i],
                    component->id
                );
                double expected_weight = frame * COUNT * .25 + (i ? i : COUNT) * .25;
                assert(record->weight == expected_weight);
            }
        }
        assert(!game_behavior_pump(world, frame + 1));
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    assert(!game_entities_query_profile(queries).searches);
    game_entities_free(queries);
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    behavior_asset_free(asset);
    behavior_debug_backend(previous);
}
static void parallel_failures(bool parallel)
{
    enum
    {
        COUNT = 1025
    };
    behavior_backend previous = behavior_debug_backend(BEHAVIOR_SLJIT);
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    game_behavior_set_parallel_updates(world, parallel);
    behavior_asset *asset = whole_owned_tick_fixture(ARRAY_OP_STRINGS, true);
    assert(!game_behavior_register_asset(world, asset->name, asset));
    behavior_instance *instances[COUNT];
    for (int i = 0; i < COUNT; i++) {
        ecs_entity entity = ecs_world_create_entity(ecs);
        assert(!game_behavior_attach(world, entity, asset->name));
        instances[i] = game_behavior_instance(world, entity, asset->name);
    }
    assert(!game_behavior_pump(world, 0));
    for (int frame = 0; frame < 2; frame++) {
        assert(game_behavior_update(world, NULL) == COUNT);
        const behavior_instance_failure *first = behavior_instance_last_failure(
            instances[0]
        );
        assert(first && first->code < 0 && first->node_id);
        for (int i = 0; i < COUNT; i++) {
            const behavior_instance_failure *failure = behavior_instance_last_failure(
                instances[i]
            );
            assert(
                failure && failure->node_id == first->node_id &&
                failure->graph_id == first->graph_id
            );
            assert(
                failure->code == first->code && failure->steps == first->steps &&
                failure->pin == first->pin && !strcmp(failure->message, first->message)
            );
            assert(instances[i]->variables[3].i == frame + 1);
            assert(!acount(instances[i]->activations));
        }
    }
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    behavior_asset_free(asset);
    behavior_debug_backend(previous);
}

/* A blocked row in any chunk must reject the whole parallel attempt before
 * effects. The serial retry must execute every row exactly once. */
static void parallel_backend_barrier()
{
    enum
    {
        COUNT = 8193
    };
    behavior_backend previous = behavior_debug_backend(BEHAVIOR_SLJIT);
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    behavior_asset *asset = whole_owned_tick_fixture(ARRAY_OP_STRINGS, false);
    assert(!game_behavior_register_asset(world, asset->name, asset));
    behavior_instance *instances[COUNT];
    for (int i = 0; i < COUNT; i++) {
        ecs_entity entity = ecs_world_create_entity(ecs);
        assert(!game_behavior_attach(world, entity, asset->name));
        instances[i] = game_behavior_instance(world, entity, asset->name);
    }
    assert(!game_behavior_pump(world, 0));
    assert(game_behavior_update(world, NULL) == COUNT);
    game_task_pool *pool = game_task_pool_acquire();
    bool parallel = game_task_pool_tasks(pool)->workers > 1;
    game_task_pool_release(pool);
    int blocked[] = { 0, COUNT / 2, COUNT - 1 };
    for (int test = 0; test < 3; test++) {
        behavior_instance *instance = instances[blocked[test]];
        assert(!behavior_instance_set_backend(instance, BEHAVIOR_BYTECODE));
        assert(game_behavior_update(world, NULL) == COUNT);
        assert(!game_behavior_last_update(world).parallel);
        for (int i = 0; i < COUNT; i++) {
            pin_value runs;
            assert(!behavior_instance_get_variable(
                instances[i],
                asset->variables[3].id,
                &runs
            ));
            assert(runs.i == 2 + test * 2);
        }
        assert(!behavior_instance_set_backend(instance, BEHAVIOR_SLJIT));
        assert(game_behavior_update(world, NULL) == COUNT);
        assert((game_behavior_last_update(world).parallel == COUNT) == parallel);
        for (int i = 0; i < COUNT; i++) {
            pin_value runs;
            assert(!behavior_instance_get_variable(
                instances[i],
                asset->variables[3].id,
                &runs
            ));
            assert(runs.i == 3 + test * 2);
        }
    }
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    behavior_asset_free(asset);
    behavior_debug_backend(previous);
}

static void cached_binding_lifecycle(behavior_backend backend)
{
    behavior_backend previous = behavior_debug_backend(backend);
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    game_behavior_set_parallel_updates(world, false);
    behavior_asset *asset = whole_batch_fixture(false);
    assert(!game_behavior_register_asset(world, asset->name, asset));
    ecs_entity entities[3];
    behavior_instance *instances[3];
    for (int i = 0; i < 3; i++) {
        entities[i] = ecs_world_create_entity(ecs);
        assert(!game_behavior_attach(world, entities[i], asset->name));
        instances[i] = game_behavior_instance(world, entities[i], asset->name);
        assert(instances[i]);
    }
    assert(!game_behavior_pump(world, 0.));
    game_behavior_set_delta_seconds(world, .25);
    assert(game_behavior_update(world, NULL) == 3);
    for (int i = 0; i < 3; i++) {
        assert(instances[i]->variables[0].d == 10.25);
    }

    assert(game_behavior_detach_entity(world, entities[0]) == 1);
    entities[0] = ecs_world_create_entity(ecs);
    assert(!game_behavior_attach(world, entities[0], asset->name));
    instances[0] = game_behavior_instance(world, entities[0], asset->name);
    assert(instances[0]);
    assert(!game_behavior_pump(world, 1.));
    assert(game_behavior_update(world, NULL) == 3);
    assert(instances[0]->variables[0].d == 10.25);
    assert(instances[1]->variables[0].d == 10.5);
    assert(instances[2]->variables[0].d == 10.5);

    behavior_asset *next = whole_batch_fixture(false), *old;
    assert(game_behavior_preview_asset(world, asset->name, next, &old) == INSTANCE_UPDATE_READY);
    behavior_asset_free(old);
    asset = next;
    assert(!game_behavior_pump(world, 2.));
    assert(game_behavior_update(world, NULL) == 3);
    assert(instances[0]->variables[0].d == 10.5);
    assert(instances[1]->variables[0].d == 10.75);
    assert(instances[2]->variables[0].d == 10.75);
    for (int i = 0; i < 3; i++) {
        assert(!behavior_instance_last_failure(instances[i]));
    }
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    behavior_asset_free(asset);
    behavior_debug_backend(previous);
}

typedef struct service_swap service_swap;
struct service_swap
{
    behavior_instance *instance;
    service_swap *replacement;
    int value;
    bool replace;
};
static pin_spec service_swap_outputs[] = {
    { .name = "Value", .type = PIN_I32, .type_name = "int" }
};
static const node_access service_swap_access = { .flags = NODE_EFFECT_HOST_SINK };
static const char *service_swap_invoke(void *user, const pin_value *, pin_value *out)
{
    service_swap *service = user;
    out[0] = (pin_value){ .type = PIN_I32, .type_name = "int", .i = service->value };
    if (service->replace) {
        service->replace = false;
        assert(!behavior_instance_bind_service(
            service->instance,
            "test.service_swap",
            service->replacement
        ));
    }
    return NULL;
}
static const node_host_call service_swap_call = {
    .service = "test.service_swap",
    .invoke = service_swap_invoke
};
static node_desc service_swap_node = { .name = "Service Swap",
                                       .output_count = 1,
                                       .outputs = service_swap_outputs,
                                       .pure_kind = NODE_PURE_CALL,
                                       .access = &service_swap_access,
                                       .host_call = &service_swap_call };
static behavior_asset *service_swap_fixture()
{
    behavior_asset *asset = behavior_asset_new("Service Swap Tick");
    behavior_variable *value = behavior_asset_add_variable(
        asset,
        "Value",
        PIN_I32,
        "int",
        PIN_CONTAINER_SINGLE,
        NULL,
        ""
    );
    node *event = graph_add_node(asset->graph, &NODE_Event);
    node *swap = graph_add_node(asset->graph, &service_swap_node);
    node *set = behavior_asset_add_set_node(asset, value->id);
    assert(value && event && swap && set);
    assert(!graph_set_event_name(asset->graph, event, "Tick"));
    assert(!graph_connect_data(asset->graph, swap, 0, set, 1));
    assert(!graph_connect_exec(asset->graph, event, 0, set, 0, 0));
    return asset;
}
static void service_swap_during_tick(behavior_backend backend)
{
    behavior_backend previous = behavior_debug_backend(backend);
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    behavior_asset *asset = service_swap_fixture();
    assert(!game_behavior_register_asset(world, asset->name, asset));
    ecs_entity entity = ecs_world_create_entity(ecs);
    assert(!game_behavior_attach(world, entity, asset->name));
    behavior_instance *instance = game_behavior_instance(world, entity, asset->name);
    service_swap replacement = { .instance = instance, .value = 2 };
    service_swap current = { .instance = instance,
                             .replacement = &replacement,
                             .value = 1,
                             .replace = true };
    assert(!behavior_instance_bind_service(instance, "test.service_swap", &current));
    assert(!game_behavior_pump(world, 0.));
    assert(game_behavior_update(world, NULL) == 1);
    assert(instance->variables[0].i == 1);
    assert(!current.replace);
    assert(behavior_instance_service(instance, "test.service_swap") == &replacement);
    assert(game_behavior_update(world, NULL) == 1);
    assert(instance->variables[0].i == 2);
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    behavior_asset_free(asset);
    behavior_debug_backend(previous);
}

static void incremental_attachment_lifecycle(behavior_backend backend)
{
    behavior_backend previous = behavior_debug_backend(backend);
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    game_behavior_set_parallel_updates(world, false);
    behavior_asset *counter = whole_batch_fixture(false);
    behavior_asset *alternate = whole_batch_fixture(false);
    assert(!behavior_asset_set_name(alternate, "Alternate Tick batch"));
    assert(!game_behavior_register_asset(world, counter->name, counter));
    assert(!game_behavior_register_asset(world, alternate->name, alternate));

    ecs_entity entities[4];
    behavior_instance *instances[4];
    for (int i = 0; i < 4; i++) {
        entities[i] = ecs_world_create_entity(ecs);
        assert(!game_behavior_attach(world, entities[i], counter->name));
        instances[i] = game_behavior_instance(world, entities[i], counter->name);
        assert(instances[i]);
    }
    assert(!game_behavior_pump(world, 0.));
    game_behavior_set_delta_seconds(world, .25);
    assert(game_behavior_update(world, NULL) == 4);
    for (int i = 0; i < 4; i++) {
        assert(instances[i]->variables[0].d == 10.25);
    }

    const char *replacement[] = { alternate->name };
    assert(!game_behavior_set_attachments(world, entities[1], replacement, 1));
    assert(!game_behavior_pump(world, 1.));
    assert(game_behavior_update(world, NULL) == 4);
    assert(instances[0]->variables[0].d == 10.5);
    assert(instances[2]->variables[0].d == 10.5);
    assert(instances[3]->variables[0].d == 10.5);
    behavior_instance *alternate_instance = game_behavior_instance(
        world,
        entities[1],
        alternate->name
    );
    assert(alternate_instance && alternate_instance->variables[0].d == 10.25);

    const char *reordered[] = { alternate->name, counter->name };
    assert(!game_behavior_set_attachments(world, entities[0], reordered, 2));
    game_behavior_attachment_info first, second;
    assert(!game_behavior_attachment_info_at(world, entities[0], 0, &first));
    assert(!game_behavior_attachment_info_at(world, entities[0], 1, &second));
    assert(
        !strcmp(first.asset_name, alternate->name) &&
        !strcmp(second.asset_name, counter->name)
    );
    assert(!game_behavior_pump(world, 2.));
    assert(game_behavior_update(world, NULL) == 5);
    assert(instances[0]->variables[0].d == 10.75);
    assert(
        game_behavior_instance(world, entities[0], alternate->name)
            ->variables[0]
            .d == 10.25
    );
    assert(alternate_instance->variables[0].d == 10.5);
    assert(instances[2]->variables[0].d == 10.75);
    assert(instances[3]->variables[0].d == 10.75);

    assert(game_behavior_detach_entity(world, entities[2]) == 1);
    entities[2] = ecs_world_create_entity(ecs);
    assert(!game_behavior_attach(world, entities[2], counter->name));
    behavior_instance *replacement_instance = game_behavior_instance(
        world,
        entities[2],
        counter->name
    );
    assert(replacement_instance);
    assert(!game_behavior_pump(world, 3.));
    assert(game_behavior_update(world, NULL) == 5);
    assert(instances[0]->variables[0].d == 11.0);
    assert(alternate_instance->variables[0].d == 10.75);
    assert(instances[3]->variables[0].d == 11.0);
    assert(replacement_instance->variables[0].d == 10.25);

    game_behavior_world_free(world);
    ecs_world_free(ecs);
    behavior_asset_free(counter);
    behavior_asset_free(alternate);
    behavior_debug_backend(previous);
}

static void preservation_proof_keys()
{
    behavior_asset *asset = whole_batch_fixture(true);
    node *entry;
    const behavior_function *function = behavior_program_batch_event(
        behavior_program_publish(asset),
        "Tick",
        &entry
    );
    assert(function && entry);
    uint64_t preserved[] = { GAME_COMPONENT_GameTransform.id,
                             GAME_COMPONENT_GameCollider.id };
    uint64_t written[] = { WHOLE_NODE_GetRecord.component->id,
                           GAME_COMPONENT_GameCollider.id };
    assert(behavior_function_preserves_components(function, preserved, 2));
    assert(!behavior_function_preserves_components(function, written, 2));
    assert(behavior_function_preserves_components(function, preserved, 2));
    behavior_asset_free(asset);
    test_note("preservation proof PASS: component sets remain independent across cached queries\n");
}

void test_parallel_ticks()
{
    parallel_backend_barrier();
    preservation_proof_keys();
    service_swap_during_tick(BEHAVIOR_SLJIT);
    service_swap_during_tick(BEHAVIOR_BYTECODE);
    cached_binding_lifecycle(BEHAVIOR_SLJIT);
    cached_binding_lifecycle(BEHAVIOR_BYTECODE);
    incremental_attachment_lifecycle(BEHAVIOR_SLJIT);
    incremental_attachment_lifecycle(BEHAVIOR_BYTECODE);
    for (int kind = 0; kind < 3; kind++) {
        parallel_fixture(BEHAVIOR_SLJIT, kind, true);
        parallel_fixture(BEHAVIOR_SLJIT, kind, false);
        parallel_fixture(BEHAVIOR_BYTECODE, kind, true);
    }
    parallel_failures(false);
    parallel_failures(true);
    test_note("parallel Tick PASS: native and bytecode values, managed scratch, ordered cross-entity writes and observation barriers\n");
}
