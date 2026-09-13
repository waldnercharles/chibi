#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/runtime.h"
#include "test/suite_internal.h"
#include "test/whole_aggregate_fixture.h"
#include "test/whole_array_op_fixture.h"
#include "test/whole_batch_fixture.h"

#include <dlfcn.h>
#include <time.h>

static const char *workloads[] = { "counter",      "component",  "int_array",
                                   "string_array", "name_array", "struct_array" };
static behavior_asset *fixture(int workload)
{
    return workload < 2 ? whole_batch_fixture(workload == 1)
                        : whole_owned_tick_fixture(workload - 2, false);
}
static behavior_function *tick(behavior_asset *asset)
{
    assert(!behavior_asset_set_owner_type(asset, "ecs.entity"));
    bool duplicate;
    behavior_program *p = behavior_program_publish(asset);
    node *entry = behavior_program_event(p, "Tick", &duplicate);
    assert(entry && !duplicate && !behavior_program_diagnostic(p));
    return (
        behavior_function *
    )behavior_program_function(behavior_program_graph(p, asset->graph), entry);
}
void emit_batch_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int workload = 0; workload < 6; workload++) {
        behavior_asset *asset = fixture(workload);
        behavior_function *f = tick(asset);
        assert(behavior_function_batchable(f));
        char name[64];
        snprintf(name, sizeof name, "generated_tick%d", workload);
        behavior_function_write_c(f, out, name);
        behavior_asset_free(asset);
    }
    assert(!fclose(out));
}
static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static uint64_t measure(int mode, int workload, int count, int frames, void *library)
{
    bool components = workload == 1;
    behavior_asset *asset = fixture(workload);
    char symbol[64];
    snprintf(symbol, sizeof symbol, "generated_tick%d", workload);
    behavior_function *f = tick(asset);
    assert(behavior_function_batchable(f));
    void *code = f->code, *dispatch = f->dispatch;
    /* Benchmark-only replacement on a fresh, quiescent program. This runs C
     * emitted from the same IR through the identical engine batch wrapper.
     * Restore owned JIT pointers before freeing the program. */
    if (mode == 4 || mode == 8) {
        f->code = dlsym(library, symbol);
        assert(f->code);
        f->dispatch = NULL;
    }
    ecs_world *ecs = ecs_world_new();
    game_behavior_world *world = game_behavior_world_new(ecs);
    const ecs_component_desc *component = WHOLE_NODE_GetRecord.component;
    if (components) {
        assert(!ecs_world_register_component(ecs, component, NULL));
    }
    game_behavior_set_profiling(world, false);
    game_behavior_set_batch_enabled(world, mode >= 2);
    game_behavior_set_native_update(world, mode != 8 && mode != 9);
    assert(!game_behavior_register_asset(world, asset->name, asset));
    behavior_backend backend = mode == 5 ? BEHAVIOR_ORACLE
                             : mode == 1 || mode == 3 || mode == 7
                                 ? BEHAVIOR_BYTECODE
                                 : BEHAVIOR_SLJIT;
    behavior_backend previous = behavior_debug_backend(backend);
    behavior_instance **instances = NULL;
    ecs_entity *entities = NULL;
    for (int i = 0; i < count; i++) {
        ecs_entity entity = ecs_world_create_entity(ecs);
        apush(entities, entity);
        if (components) {
            whole_record record = { .weight = i * .5 };
            assert(!ecs_world_record_add(ecs, entity, component->id, &record, sizeof record));
        }
    }
    assert(!ecs_world_apply_commands(ecs));
    for (int i = 0; i < count; i++) {
        assert(!game_behavior_attach(world, entities[i], asset->name));
        game_behavior_attachment_info info;
        assert(!game_behavior_attachment_info_at(world, entities[i], 0, &info));
        apush(instances, info.instance);
        if (components) {
            pin_value target = { .type = PIN_HANDLE,
                                 .type_name = "ecs.entity",
                                 .u64 = ecs_entity_to_u64(entities[i]) };
            assert(!behavior_instance_set_variable(
                info.instance,
                asset->variables[1].id,
                &target
            ));
        }
    }
    behavior_debug_backend(previous);
    bool duplicate;
    assert(
        f == behavior_program_function(
                 behavior_program_graph(asset->program, asset->graph),
                 behavior_program_event(asset->program, "Tick", &duplicate)
             )
    );
    if (mode == 4 || mode == 8) {
        assert(f->code == dlsym(library, symbol));
    }
    assert(!game_behavior_pump(world, 0.));
    game_behavior_set_delta_seconds(world, .125);
    uint64_t batches = 0, prepared = 0, vm = 0, stores = 0, blocks = 0;
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    double start = 0;
    for (int frame = -8; frame < frames; frame++) {
        if (!frame) {
            batches = behavior_batch_dispatches();
            vm = behavior_function_vm_allocations();
            stores = runtime_store_creations();
            blocks = arena_block_allocations();
            start = seconds();
        }
        game_behavior_set_time(world, (frame + 8) * .125);
        int ran = mode >= 6
                    ? game_behavior_update(world, NULL)
                    : game_behavior_dispatch_all(world, "Tick", (flow_inputs){ 0 }, NULL);
        assert(ran == count);
        if (mode >= 6) {
            game_behavior_update_stats updates = game_behavior_last_update(world);
            assert(updates.prepared == count && !updates.scheduled);
            assert(updates.native_loop == (mode == 6 ? count : 0));
            if (frame >= 0) {
                prepared += updates.prepared;
            }
        }
        assert(!game_behavior_pump(world, (frame + 8) * .125));
    }
    double elapsed = seconds() - start;
    batches = behavior_batch_dispatches() - batches + prepared;
    vm = behavior_function_vm_allocations() - vm;
    stores = runtime_store_creations() - stores;
    blocks = arena_block_allocations() - blocks;
    uint64_t checksum = 0;
    for (int i = 0; i < count; i++) {
        if (workload < 2) {
            assert(instances[i]->variables[0].d == 10. + (frames + 8) * .125);
            checksum += (uint64_t)(instances[i]->variables[0].d * 8);
        } else {
            const behavior_instance *bi = instances[i];
            assert(bi->variables[3].i == frames + 8);
            const pin_array *source = bi->variables[0].ptr,
                            *saved = bi->variables[1].ptr,
                            *final = bi->variables[2].ptr;
            assert(source->count == 3 && saved->count == 5 && final->count == 5);
            assert(pin_array_equal(saved, final));
            for (int j = 0; j < 3; j++) {
                assert(pin_value_equal(&source->items[j], &final->items[j]));
            }
            checksum += (uint64_t)bi->variables[3].i + (uint64_t) final->count;
        }
        if (components) {
            const whole_record
                *record = ecs_component_data_const(ecs, entities[i], component->id);
            assert(record->weight == i * .5 + (frames + 8) * .125);
            checksum += (uint64_t)(record->weight * 8);
        }
    }
    assert(batches == ((mode >= 2 && mode <= 4) || mode >= 6 ? (uint64_t)count * frames : 0));
    assert(mode < 2 || mode == 5 || !vm);
    assert(mode < 2 || mode == 5 || !blocks);
    if (mode != 5) {
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    }
    printf(
        "%s %s entities=%d ns=%.3f checksum=%llu batches=%llu "
        "vm_allocations=%llu runtime_stores=%llu arena_blocks=%llu\n",
        (const char *[]){ "SLJIT_ACTIVATION",
                          "BYTECODE_ACTIVATION",
                          "SLJIT_BATCH",
                          "BYTECODE_BATCH",
                          "GENERATED_C_BATCH",
                          "ORACLE",
                          "SLJIT_UPDATE",
                          "BYTECODE_UPDATE",
                          "GENERATED_C_UPDATE",
                          "SLJIT_C_UPDATE" }[mode],
        workloads[workload],
        count,
        elapsed * 1e9 / (frames * (double)count),
        (unsigned long long)checksum,
        (unsigned long long)batches,
        (unsigned long long)vm,
        (unsigned long long)stores,
        (unsigned long long)blocks
    );
    game_behavior_world_free(world);
    ecs_world_free(ecs);
    f->code = code;
    f->dispatch = dispatch;
    behavior_asset_free(asset);
    afree(instances);
    afree(entities);
    return checksum;
}
int bench_batch(const char *path, int count, int frames, int rotation)
{
    assert(count > 0 && frames > 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int workload = 0; workload < 6; workload++) {
        uint64_t expected = 0;
        for (int i = 0; i < 10; i++) {
            int mode = (i + rotation) % 10;
            uint64_t checksum = measure(mode, workload, count, frames, library);
            if (!i) {
                expected = checksum;
            }
            assert(checksum == expected);
        }
    }
    dlclose(library);
    return 0;
}
