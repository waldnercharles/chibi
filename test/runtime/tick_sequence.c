#include "graph/authoring/graph_edit.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/runtime.h"
#include "graph/runtime/update_internal.h"
#include "test/suite_internal.h"
#include "test/whole_aggregate_fixture.h"
#include "test/whole_array_op_fixture.h"
#include "test/whole_batch_fixture.h"

#include <dlfcn.h>
#include <time.h>

/* Two distinct graphs with order-sensitive cross-entity reads/writes, then a
 * managed-value graph with host calls and a different register/stack layout. */
static behavior_asset *sequence_asset(int part, double delta)
{
    behavior_asset *asset = part == 2 ? whole_owned_tick_fixture(1, false)
                                      : whole_batch_fixture(true);
    assert(
        !behavior_asset_set_name(asset, (const char *[]){ "Transfer", "Advance", "Snapshot" }[part])
    );
    if (part == 1) {
        graph *g = asset->graphs[1];
        for (int i = 0; i < acount(g->nodes); i++) {
            node *n = g->nodes[i], *source;
            int pin;
            if (n->desc != &NODE_Add || !graph_input_source(g, n, 1, &source, &pin)) {
                continue;
            }
            assert(source->desc == &NODE_DeltaTime);
            assert(!graph_remove_edge(g, source, pin, n, 1));
            assert(
                !graph_set_pin_default(g, n, 0, 1, &(pin_value){ .type = PIN_F64, .d = delta })
            );
        }
    }
    if (part == 1) {
        node *sleep = graph_add_node(asset->graph, &NODE_Event),
             *delay = graph_add_node(asset->graph, &NODE_Delay);
        assert(!graph_set_event_name(asset->graph, sleep, "Sleep"));
        assert(
            !graph_set_pin_default(asset->graph, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = 10. })
        );
        assert(!graph_connect_exec(asset->graph, sleep, 0, delay, 0, 0));
    }
    return asset;
}
static void sequence_parity(behavior_backend backend)
{
    enum
    {
        COUNT = 8,
        PARTS = 3
    };
    ecs_world *ecs = ecs_world_new();
    const ecs_component_desc *component = WHOLE_NODE_GetRecord.component;
    assert(!ecs_world_register_component(ecs, component, NULL));
    game_behavior_world *world = game_behavior_world_new(ecs);
    behavior_asset *assets[PARTS];
    for (int j = 0; j < PARTS; j++) {
        assets[j] = sequence_asset(j, .25);
        assert(!game_behavior_register_asset(world, assets[j]->name, assets[j]));
    }
    ecs_entity entities[COUNT];
    double weights[COUNT];
    for (int i = 0; i < COUNT; i++) {
        entities[i] = ecs_world_create_entity(ecs);
        whole_record record = { .weight = weights[i] = i * .5 };
        assert(!ecs_world_record_add(ecs, entities[i], component->id, &record, sizeof record));
    }
    assert(!ecs_world_apply_commands(ecs));
    behavior_instance *instances[COUNT][PARTS];
    behavior_backend previous = behavior_debug_backend(backend);
    for (int i = 0; i < COUNT; i++) {
        for (int j = 0; j < PARTS; j++) {
            assert(!game_behavior_attach(world, entities[i], assets[j]->name));
            instances[i]
                     [j] = game_behavior_instance(world, entities[i], assets[j]->name);
            if (j == 2) {
                continue;
            }
            pin_value target = {
                .type = PIN_HANDLE,
                .type_name = "ecs.entity",
                .u64 = ecs_entity_to_u64(entities[j ? i : (i + 1) % COUNT])
            };
            assert(!behavior_instance_set_variable(
                instances[i][j],
                assets[j]->variables[1].id,
                &target
            ));
        }
    }
    behavior_debug_backend(previous);
    assert(!game_behavior_pump(world, 0.));
    game_behavior_set_delta_seconds(world, .125);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    flow_run *waiting = NULL;
    for (int frame = 0; frame < 20; frame++) {
        /* Stop in the middle of a cycle and resume at the next body. */
        if (frame == 4 || frame == 8) {
            assert(!game_behavior_set_debug_hold(world, entities[2], assets[1]->name, frame == 4));
        }
        if (frame == 9) {
            waiting = behavior_instance_dispatch(instances[3][1], "Sleep", (flow_inputs){ 0 }, NULL);
            assert(waiting && flow_run_status(waiting) == FLOW_SUSPENDED);
        }
        if (frame == 10) {
            assert(!behavior_instance_stop_activation(instances[3][1], waiting));
            assert(!behavior_instance_release_activation(instances[3][1], waiting));
        }
        /* A fresh immutable generation must never call the old sequence. */
        if (frame == 12) {
            behavior_asset *next = sequence_asset(1, .5), *old;
            assert(
                game_behavior_preview_asset(world, assets[1]->name, next, &old) == INSTANCE_UPDATE_READY
            );
            behavior_asset_free(old);
            assets[1] = next;
        }
        assert(game_behavior_update(world, NULL) == COUNT * PARTS);
        if (backend == BEHAVIOR_SLJIT) {
            game_behavior_update_stats stats = game_behavior_last_update(world);
            assert(stats.system_loop >= COUNT * PARTS - PARTS);
            if (frame < 4 || frame > 10) {
                assert(stats.system_loop == COUNT * PARTS);
            }
        }
        for (int i = 0; i < COUNT; i++) {
            weights[(i + 1) % COUNT] = weights[i] + .125;
            weights[i] += frame < 12 ? .25 : .5;
        }
        for (int i = 0; i < COUNT; i++) {
            const whole_record
                *record = ecs_component_data_const(ecs, entities[i], component->id);
            assert(record->weight == weights[i]);
            assert(instances[i][0]->execution.variables[0].d == 10. + (frame + 1) * .125);
            assert(
                instances[i][1]->execution.variables[0].d ==
                10. + (frame + 1) * .25 + (frame >= 12 ? (frame - 11) * .25 : 0.)
            );
            assert(instances[i][2]->execution.variables[3].i == frame + 1);
            assert(pin_array_equal(
                instances[i][2]->execution.variables[1].ptr,
                instances[i][2]->execution.variables[2].ptr
            ));
            for (int j = 0; j < PARTS; j++) {
                assert(!behavior_instance_last_failure(instances[i][j]));
            }
        }
        assert(!game_behavior_pump(world, (frame + 1) * .125));
    }
    if (backend != BEHAVIOR_ORACLE) {
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    }
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    for (int j = 0; j < PARTS; j++) {
        behavior_asset_free(assets[j]);
    }
}
void test_tick_sequences()
{
    sequence_parity(BEHAVIOR_SLJIT);
    sequence_parity(BEHAVIOR_BYTECODE);
    sequence_parity(BEHAVIOR_ORACLE);
    puts("PASS: interleaved Tick systems preserve cross-entity dependencies, managed values, inspection and hot apply");
}

static const behavior_function *sequence_tick(behavior_asset *asset, node **entry)
{
    assert(!behavior_asset_set_owner_type(asset, "ecs.entity"));
    behavior_program *program = behavior_program_publish(asset);
    assert(!behavior_program_diagnostic(program));
    const behavior_function *f = behavior_program_batch_event(program, "Tick", entry);
    assert(f);
    return f;
}
void emit_sequence_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    behavior_asset *assets[3];
    const behavior_function *functions[3];
    for (int i = 0; i < 3; i++) {
        assets[i] = sequence_asset(i, .25);
        node *entry;
        functions[i] = sequence_tick(assets[i], &entry);
    }
    behavior_function_write_sequence_c(functions, 2, out, "generated_sequence2");
    behavior_function_write_sequence_c(functions, 3, out, "generated_sequence3");
    assert(!fclose(out));
    for (int i = 0; i < 3; i++) {
        behavior_asset_free(assets[i]);
    }
}
static double sequence_seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static uint64_t measure_sequence(int mode, int parts, int count, int frames, void *library)
{
    ecs_world *ecs = ecs_world_new();
    const ecs_component_desc *component = WHOLE_NODE_GetRecord.component;
    assert(!ecs_world_register_component(ecs, component, NULL));
    behavior_asset *assets[3];
    const behavior_function *functions[3];
    node *entries[3];
    for (int j = 0; j < parts; j++) {
        assets[j] = sequence_asset(j, .25);
        functions[j] = sequence_tick(assets[j], &entries[j]);
    }
    behavior_update_binding **bindings = NULL;
    behavior_update_row *rows = NULL;
    for (int i = 0; i < count; i++) {
        ecs_entity entity = ecs_world_create_entity(ecs);
        whole_record record = { .weight = i * .5 };
        assert(!ecs_world_record_add(ecs, entity, component->id, &record, sizeof record));
        pin_value owner = { .type = PIN_HANDLE,
                            .type_name = "ecs.entity",
                            .u64 = ecs_entity_to_u64(entity) };
        for (int j = 0; j < parts; j++) {
            behavior_instance *bi = behavior_instance_create(assets[j], &owner, NULL, NULL);
            assert(!behavior_instance_set_backend(bi, mode == 3 ? BEHAVIOR_BYTECODE : BEHAVIOR_SLJIT));
            assert(!behavior_instance_bind_service(bi, "ecs.world", ecs));
            behavior_instance_set_trace_enabled(bi, false);
            behavior_instance_set_delta_seconds(bi, .125);
            if (j < 2) {
                assert(
                    !behavior_instance_set_variable(bi, assets[j]->variables[1].id, &owner)
                );
            }
            behavior_update_row row = behavior_update_row_make(
                bi,
                behavior_update_prepare(&bindings, bi, functions[j], entries[j]),
                owner.u64,
                acount(rows)
            );
            assert(row.binding);
            apush(rows, row);
        }
    }
    assert(!ecs_world_apply_commands(ecs));
    behavior_update_refresh(bindings);
    behavior_update_span *spans = NULL;
    behavior_update_kernel **kernels = NULL;
    double start = sequence_seconds();
    void *code = NULL;
    if (mode == 0) {
        code = behavior_update_compile();
    }
    if (mode == 1) {
        behavior_update_system_spans(rows, acount(rows), &spans, &kernels);
        assert(acount(spans) == 1 && spans[0].period == parts && acount(kernels) == 1);
        code = spans[0].code;
    }
    if (mode == 2) {
        char name[64];
        snprintf(name, sizeof name, "generated_sequence%d", parts);
        code = dlsym(library, name);
    }
    assert(mode == 3 || code);
    double compile_us = (sequence_seconds() - start) * 1e6;
    /* Rebuilding after attachment churn must keep the existing generation's
     * kernel. This separately measures plan rebuilding without compilation. */
    double rebuild_us = 0.;
    if (mode == 1) {
        start = sequence_seconds();
        for (int i = 0; i < 32; i++) {
            behavior_update_system_spans(rows, acount(rows), &spans, &kernels);
            assert(acount(spans) == 1 && spans[0].code == code && acount(kernels) == 1);
        }
        rebuild_us = (sequence_seconds() - start) * 1e6 / 32;
    }
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    uint64_t vm = 0, stores = 0, blocks = 0;
    start = 0.;
    for (int frame = -8; frame < frames; frame++) {
        if (!frame) {
            vm = behavior_function_vm_allocations();
            stores = runtime_store_creations();
            blocks = arena_block_allocations();
            start = sequence_seconds();
        }
        int result;
        behavior_update_row *end = code ? ((behavior_update_fn)code)(
                                              rows,
                                              rows + acount(rows),
                                              (const ecs_execution_view *)ecs,
                                              &result
                                          )
                                        : behavior_update_run_c(
                                              rows,
                                              rows + acount(rows),
                                              (const ecs_execution_view *)ecs,
                                              &result,
                                              NULL
                                          );
        assert(end == rows + acount(rows) && result == FLOW_OK);
    }
    double elapsed = sequence_seconds() - start;
    assert(
        vm == behavior_function_vm_allocations() &&
        stores == runtime_store_creations() && blocks == arena_block_allocations()
    );
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    uint64_t checksum = 0;
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < parts; j++) {
            behavior_instance *bi = behavior_update_instance(&rows[i * parts + j]);
            if (j < 2) {
                assert(bi->execution.variables[0].d == (frames + 8) * (j ? .25 : .125));
                checksum += (uint64_t)(bi->execution.variables[0].d * 8);
            } else {
                assert(bi->execution.variables[3].i == frames + 8);
                assert(pin_array_equal(
                    bi->execution.variables[1].ptr,
                    bi->execution.variables[2].ptr
                ));
                checksum += (uint64_t)bi->execution.variables[3].i;
            }
        }
        const whole_record *record = ecs_component_data_const(
            ecs,
            ecs_entity_from_u64(rows[i * parts].entity),
            component->id
        );
        assert(record->weight == i * .5 + (frames + 8) * .375);
        checksum += (uint64_t)(record->weight * 8);
    }
    printf(
        "sequence mode=%s parts=%d entities=%d ns=%.3f checksum=%llu "
        "compile_us=%.3f rebuild_us=%.3f oracle_calls=0 allocations=0\n",
        (const char *[]){ "PREPARED", "SLJIT", "GENERATED_C", "BYTECODE" }[mode],
        parts,
        count,
        elapsed * 1e9 / (count * (double)frames),
        (unsigned long long)checksum,
        compile_us,
        rebuild_us
    );
    if (mode == 0) {
        behavior_update_free_code(code);
    }
    behavior_update_free_kernels(kernels);
    afree(spans);
    behavior_update_free_bindings(bindings);
    for (int i = 0; i < acount(rows); i++) {
        behavior_instance_destroy(behavior_update_instance(&rows[i]));
    }
    afree(rows);
    for (int j = 0; j < parts; j++) {
        behavior_asset_free(assets[j]);
    }
    ecs_world_free(ecs);
    return checksum;
}
int bench_sequences(const char *path, int count, int frames, int rotation)
{
    assert(count >= 2 && frames > 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int parts = 2; parts <= 3; parts++) {
        uint64_t expected = 0;
        for (int i = 0; i < 4; i++) {
            uint64_t checksum = measure_sequence((i + rotation) % 4, parts, count, frames, library);
            if (i) {
                assert(checksum == expected);
            }
            expected = checksum;
        }
    }
    dlclose(library);
    return 0;
}
