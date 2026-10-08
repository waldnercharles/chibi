#include "graph/authoring/graph_edit.h"
#include "graph/compiler/behavior_function_internal.h"
#include "graph/model/registry.h"
#include "graph/runtime/flow_internal.h"
#include "runtime/behaviors/entities.h"
#include "runtime/component_catalog.h"
#include "test/suite_internal.h"
#include "test/whole_data_fixture.h"

#include <dlfcn.h>
#include <time.h>

static behavior_asset *fixture()
{
    behavior_asset *a = whole_data_fixture(registry_node_by_name("Find Nearest Entity"), false);
    CF_V2 origin = { 0 };
    pin_value args[] = { { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &origin },
                         { .type = PIN_F32, .f = 100000 },
                         { .type = PIN_U32, .u = 1 },
                         { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = 0 } };
    for (int i = 0; i < 4; i++) {
        assert(!graph_set_pin_default(a->graph, a->graph->nodes[1], 0, i + 1, &args[i]));
    }
    return a;
}
static behavior_function *entry(behavior_asset *a)
{
    behavior_program *p = behavior_program_publish(a);
    behavior_function *f = (behavior_function *)
        behavior_program_function(behavior_program_graph(p, a->graph), a->graph->nodes[0]);
    assert(f && behavior_function_batchable(f));
    return f;
}
void emit_nearest_c(const char *path)
{
    behavior_asset *a = fixture();
    FILE *out = fopen(path, "w");
    assert(out);
    behavior_function_write_c(entry(a), out, "generated_nearest");
    assert(!fclose(out));
    behavior_asset_free(a);
}
static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int bench_nearest(const char *path, int count, int rotation)
{
    assert(count > 0 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int size = 0; size < 2; size++) {
        int entities = size ? 512 : 32;
        ecs_world *world = ecs_world_new();
        assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameTransform, NULL));
        assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameCollider, NULL));
        game_entities *service = game_entities_new(world, NULL);
        uint64_t expected = 0;
        for (int i = 0; i < entities; i++) {
            ecs_entity e = ecs_world_create_entity(world);
            assert(!GAME_GameTransform_add(world, e, 0));
            assert(!GAME_GameCollider_add(world, e, 0));
            assert(!ecs_world_apply_commands(world));
            *GAME_GameTransform_get(world, e) = (GameTransform){ i + 1, 0 };
            *GAME_GameCollider_get(world, e) = (GameCollider){ .category = 1 };
            if (!i) {
                expected = ecs_entity_to_u64(e);
            }
        }
        for (int order = 0; order < 6; order++) {
            int mode = (order + rotation) % 6;
            behavior_asset *a = fixture();
            behavior_function *f = entry(a);
            void *code = f->artifact->code,
                 *inspection = f->artifact->inspection_code,
                 *dispatch = f->artifact->dispatch;
            if (mode % 3 == 2) {
                f->artifact->code = dlsym(library, "generated_nearest");
                assert(f->artifact->code);
                f->artifact->inspection_code = f->artifact->code;
                f->artifact->dispatch = NULL;
            }
            behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
            assert(entry(a) == f);
            assert(
                !behavior_instance_set_backend(bi, mode % 3 == 1 ? BEHAVIOR_BYTECODE : BEHAVIOR_SLJIT)
            );
            behavior_instance_set_trace_enabled(bi, false);
            assert(!behavior_instance_set_activation_pool(bi, true));
            assert(!behavior_instance_bind_service(bi, "game.entities", service));
            behavior_batch *batch = NULL;
            uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls(),
                     allocations = 0, checksum = 0;
            double start = 0;
            for (int i = -32; i < count; i++) {
                if (!i) {
                    start = seconds();
                    allocations = behavior_function_data_allocations();
                }
                flow_run *run = NULL;
                if (mode >= 3) {
                    assert(behavior_instance_dispatch_batch(bi, "Run", &batch, &run) && !run);
                } else {
                    run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, NULL);
                    assert(
                        run && run->whole_function_executed &&
                        flow_run_status(run) == FLOW_COMPLETED
                    );
                }
                assert(
                    bi->execution.variables[0].b &&
                    bi->execution.variables[1].u64 == expected &&
                    bi->execution.variables[3].f == 1
                );
                const CF_V2 *position = bi->execution.variables[2].ptr;
                assert(position && position->x == 1 && position->y == 0);
                if (i >= 0) {
                    checksum += bi->execution.variables[1].u64;
                }
                if (run) {
                    assert(!behavior_instance_release_activation(bi, run));
                }
            }
            double elapsed = seconds() - start;
            allocations = behavior_function_data_allocations() - allocations;
            assert(!allocations && eval == eval_oracle_calls() && flow == flow_oracle_calls());
            assert(checksum == (uint64_t)count * expected);
            printf(
                "%s nearest%d_%s ns=%.3f checksum=%llu data_allocations=%llu\n",
                (const char *[]){ "SLJIT", "BYTECODE", "GENERATED_C" }[mode % 3],
                entities,
                mode >= 3 ? "batch" : "activation",
                elapsed * 1e9 / count,
                (unsigned long long)checksum,
                (unsigned long long)allocations
            );
            behavior_batch_free(batch);
            behavior_instance_destroy(bi);
            f->artifact->code = code;
            f->artifact->inspection_code = inspection;
            f->artifact->dispatch = dispatch;
            behavior_asset_free(a);
        }
        game_entities_free(service);
        ecs_world_free(world);
    }
    assert(!dlclose(library));
    return 0;
}
