#include "graph/runtime/flow_internal.h"
#include "graph/runtime/update_internal.h"
#include "test/flow_snapshot.h"
#include "test/suite_internal.h"
#include "test/whole_array_op_fixture.h"
#include "test/whole_managed_fixture.h"

static void source_value(behavior_instance *bi, int kind, int id)
{
    arena storage = { 0 };
    pin_value args[5];
    whole_array_op_inputs(kind, args, &storage);
    pin_value *first = &((pin_array *)args[0].ptr)->items[0];
    char text[128];
    snprintf(text, sizeof text, "entity %d owns this temporary value", id);
    if (kind == ARRAY_OP_INTS) {
        first->i = id;
    } else if (kind == ARRAY_OP_STRUCTS) {
        whole_managed *record = first->ptr;
        record->label = text;
        record->nested.text = text;
        record->count = id;
    } else {
        first->str = text;
    }
    assert(!behavior_instance_set_variable(bi, bi->asset->variables[0].id, &args[0]));
    arena_free(&storage);
}

static void world_values(behavior_backend backend, int kind, bool update)
{
    enum
    {
        COUNT = 7
    };
    behavior_asset *a = whole_owned_tick_fixture(kind, false);
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    assert(!game_behavior_register_asset(world, a->name, a));
    behavior_instance *instances[COUNT];
    ecs_entity entities[COUNT];
    behavior_backend old_backend = behavior_debug_backend(backend);
    for (int i = 0; i < COUNT; i++) {
        entities[i] = ecs_world_create_entity(ecs);
        assert(!game_behavior_attach(world, entities[i], a->name));
        game_behavior_attachment_info info;
        assert(!game_behavior_attachment_info_at(world, entities[i], 0, &info));
        instances[i] = info.instance;
        source_value(instances[i], kind, i + 100);
    }
    behavior_debug_backend(old_backend);
    assert(!game_behavior_pump(world, 0));
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    for (int frame = 0; frame < 20; frame++) {
        if (frame == 5 || frame == 8) {
            assert(!game_behavior_set_debug_hold(world, entities[0], a->name, frame == 5));
        }
        int eligible = 0;
        for (int i = 0; i < COUNT; i++) {
            eligible += !instances[i]->trace_enabled &&
                        !acount(instances[i]->activations);
        }
        uint64_t batches = behavior_batch_dispatches();
        int ran = update
                    ? game_behavior_update(world, NULL)
                    : game_behavior_dispatch_all(world, "Tick", (flow_inputs){ 0 }, NULL);
        assert(ran == COUNT);
        if (backend != BEHAVIOR_ORACLE) {
            game_behavior_update_stats stats = game_behavior_last_update(world);
            assert(
                behavior_batch_dispatches() - batches + (update ? stats.prepared : 0) ==
                (uint64_t)eligible
            );
            if (update && backend == BEHAVIOR_SLJIT) {
                assert(stats.native_loop == eligible);
                assert(stats.system_loop == eligible);
            }
        }
        for (int i = 0; i < COUNT; i++) {
            behavior_instance *bi = instances[i];
            assert(bi->variables[3].i == frame + 1);
            const pin_array *source = bi->variables[0].ptr,
                            *saved = bi->variables[1].ptr,
                            *final = bi->variables[2].ptr;
            assert(source->count == 3 && saved->count == 5 && final->count == 5);
            assert(pin_array_equal(saved, final));
            for (int j = 0; j < 3; j++) {
                assert(pin_value_equal(&source->items[j], &final->items[j]));
            }
        }
        assert(!game_behavior_pump(world, frame + 1));
    }
    behavior_asset *next = whole_owned_tick_fixture(kind, false), *previous;
    int status = game_behavior_preview_asset(world, a->name, next, &previous);
    assert(status == INSTANCE_UPDATE_READY);
    assert(previous == a);
    behavior_asset_free(previous);
    a = next;
    assert(
        (update ? game_behavior_update(world, NULL)
                : game_behavior_dispatch_all(world, "Tick", (flow_inputs){ 0 }, NULL)) == COUNT
    );
    for (int i = 0; i < COUNT; i++) {
        assert(instances[i]->variables[3].i == 21);
        const pin_array *final = instances[i]->variables[2].ptr;
        assert(final->count == 5);
    }
    assert(!game_behavior_pump(world, 21));
    if (backend != BEHAVIOR_ORACLE) {
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    }
    game_behavior_world_free(world);
    behavior_asset_free(a);
    ecs_world_free(ecs);
}

static flow_run *failed_dispatch(
    behavior_instance *bi,
    int mode,
    behavior_batch **batch,
    behavior_update_binding ***bindings,
    ecs_world *world,
    ecs_entity entity,
    void *code
)
{
    flow_run *run;
    if (mode == 0) {
        return behavior_instance_dispatch(bi, "Tick", (flow_inputs){ 0 }, NULL);
    }
    if (mode == 1) {
        assert(behavior_instance_dispatch_batch(bi, "Tick", batch, &run) && run);
        return run;
    }
    node *entry;
    const behavior_function *f = behavior_program_batch_event(bi->asset->program, "Tick", &entry);
    assert(f);
    behavior_update_row row = { .instance = bi,
                                .binding = behavior_update_prepare(bindings, bi, f, entry),
                                .services_revision = bi->services_revision,
                                .entity = ecs_entity_to_u64(entity) };
    assert(row.binding);
    int result;
    behavior_update_row *
        stopped = mode == 3
                    ? ((
                          behavior_update_fn
                      )code)(&row, &row + 1, (const ecs_execution_view *)world, &result)
                    : behavior_update_run_c(&row, &row + 1, (const ecs_execution_view *)world, &result, NULL);
    assert(stopped == &row && result < 0);
    return behavior_update_failure(&row, result);
}

static void failure_values(behavior_backend backend, int kind, int mode)
{
    behavior_asset *a = whole_owned_tick_fixture(kind, true);
    behavior_instance *reference = behavior_instance_create(a, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(reference, BEHAVIOR_ORACLE));
    behavior_instance_set_trace_enabled(reference, false);
    source_value(reference, kind, 100);
    flow_run *expected_run = behavior_instance_dispatch(reference, "Tick", (flow_inputs){ 0 }, NULL);
    assert(expected_run && flow_run_status(expected_run) == FLOW_FAILED);
    test_flow_snapshot expected = test_flow_snapshot_take(
        expected_run,
        reference,
        flow_run_error(expected_run)->code
    );
    behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(bi, backend));
    behavior_instance_set_trace_enabled(bi, false);
    source_value(bi, kind, 100);
    ecs_world *ecs = ecs_world_new();
    ecs_entity entity = ecs_world_create_entity(ecs);
    behavior_batch *batch = NULL;
    behavior_update_binding **bindings = NULL;
    void *code = mode == 3 ? behavior_update_compile() : NULL;
    assert(mode != 3 || code);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    flow_run *run = failed_dispatch(bi, mode, &batch, &bindings, ecs, entity, code);
    assert(run && flow_run_status(run) == FLOW_FAILED && bi->variables[3].i == 1);
    /* Reuse and destroy the batch banks before the first inspection read.
     * The captured child must own its arrays, strings and nested fields. */
    for (int i = 0; i < 8; i++) {
        behavior_instance *other = behavior_instance_create(a, NULL, NULL, NULL);
        assert(!behavior_instance_set_backend(other, backend));
        behavior_instance_set_trace_enabled(other, false);
        source_value(other, kind, 200 + i);
        flow_run *later = failed_dispatch(other, mode, &batch, &bindings, ecs, entity, code);
        assert(
            later && flow_run_status(later) == FLOW_FAILED &&
            other->variables[3].i == 1
        );
        behavior_instance_destroy(other);
    }
    behavior_batch_free(batch);
    behavior_update_free_bindings(bindings);
    behavior_update_free_code(code);
    test_flow_snapshot
        actual = test_flow_snapshot_take(run, bi, flow_run_error(run)->code);
    test_flow_snapshot_same(actual, expected);
    test_flow_snapshot_free(&actual);
    test_flow_snapshot_free(&expected);
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    behavior_instance_destroy(bi);
    behavior_instance_destroy(reference);
    behavior_asset_free(a);
    ecs_world_free(ecs);
}

void test_owned_ticks()
{
    for (int kind = 0; kind < ARRAY_OP_KINDS; kind++) {
        for (int backend = 0; backend < 3; backend++) {
            behavior_backend mode = (behavior_backend[]){ BEHAVIOR_SLJIT,
                                                          BEHAVIOR_BYTECODE,
                                                          BEHAVIOR_ORACLE }[backend];
            for (int update = 0; update < 2; update++) {
                world_values(mode, kind, update);
            }
            for (int path = 0;
                 mode != BEHAVIOR_ORACLE && path < (mode == BEHAVIOR_SLJIT ? 4 : 3);
                 path++) {
                failure_values(mode, kind, path);
            }
        }
    }
    test_note("PASS: owned Tick arrays, strings and structs; batch/update/oracle state, inspection, hot apply and retained failures after bank reuse\n");
}
