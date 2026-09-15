#include "graph/runtime/update_internal.h"
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
                assert(instances[i]->variables[3].i == frame + 1);
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
    puts("preservation proof PASS: component sets remain independent across cached queries");
}

void test_parallel_ticks()
{
    preservation_proof_keys();
    cached_binding_lifecycle(BEHAVIOR_SLJIT);
    cached_binding_lifecycle(BEHAVIOR_BYTECODE);
    for (int kind = 0; kind < 3; kind++) {
        parallel_fixture(BEHAVIOR_SLJIT, kind, true);
        parallel_fixture(BEHAVIOR_SLJIT, kind, false);
        parallel_fixture(BEHAVIOR_BYTECODE, kind, true);
    }
    parallel_failures(false);
    parallel_failures(true);
    puts("parallel Tick PASS: native and bytecode values, managed scratch, ordered cross-entity writes and observation barriers");
}
