#include "graph/authoring/graph_edit.h"
#include "graph/compiler/behavior_function_internal.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "runtime/component_catalog.h"
#include "test/suite_internal.h"
#include "test/whole_material_fixture.h"

#include <dlfcn.h>
#include <time.h>

static behavior_asset *fixture(int kind)
{
    behavior_asset *a = whole_material_fixture(kind, false, false);
    assert(!behavior_asset_set_owner_type(a, "ecs.entity"));
    node *self = graph_add_node(a->graph, &NODE_Self);
    assert(!graph_set_pin_type(a->graph, self, 1, 0, PIN_HANDLE, "ecs.entity"));
    assert(!graph_connect_data(a->graph, self, 0, a->graph->nodes[1], 1));
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
void emit_material_bench_c(FILE *out)
{
    for (int vector = 0; vector < 2; vector++) {
        behavior_asset *a = fixture(vector ? 4 : 0);
        behavior_function_write_c(entry(a), out, vector ? "generated_material_vec3" : "generated_material_float");
        behavior_asset_free(a);
    }
}
static double seconds()
{
    struct timespec t;
    assert(!clock_gettime(CLOCK_MONOTONIC, &t));
    return t.tv_sec + t.tv_nsec * 1e-9;
}
int bench_material(const char *path, int count, int rotation)
{
    assert(count > 0 && rotation >= 0);
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    const node_host_call *helper = registry_node_by_name("Set Material Parameter")
                                       ->host_call;
    for (int vector = 0; vector < 2; vector++) {
        int kind = vector ? 4 : 0, material = whole_material_publish(vector ? 3 : 1);
        for (int size = 0; size < 2; size++) {
            int entities = size ? 64 : 1;
            ecs_entity entity;
            ecs_world *world = whole_material_world(&entity);
            for (int i = 0; i < entities; i++) {
                if (i) {
                    entity = ecs_world_create_entity(world);
                    GameShader shader = { .material = "Typed host material" };
                    assert(!ecs_world_record_add(
                        world,
                        entity,
                        GAME_COMPONENT_GameShader.id,
                        &shader,
                        0
                    ));
                    assert(!ecs_world_apply_commands(world));
                }
                for (int name = 0; name < 2; name++) {
                    pin_value args[] = { { .type = PIN_HANDLE,
                                           .type_name = "ecs.entity",
                                           .u64 = ecs_entity_to_u64(entity) },
                                         { .type = PIN_STR,
                                           .str = name ? "Second" : "Value" },
                                         whole_material_value(kind, false) };
                    assert(!helper->invoke(world, args, NULL));
                }
            }
            pin_value owner = { .type = PIN_HANDLE,
                                .type_name = "ecs.entity",
                                .u64 = ecs_entity_to_u64(entity) };
            for (int order = 0; order < 6; order++) {
                int mode = (order + rotation) % 6;
                behavior_asset *a = fixture(kind);
                behavior_function *f = entry(a);
                void *code = f->artifact->code,
                     *inspection = f->artifact->inspection_code,
                     *dispatch = f->artifact->dispatch;
                if (mode % 3 == 2) {
                    f->artifact->code = dlsym(library, vector ? "generated_material_vec3" : "generated_material_float");
                    assert(f->artifact->code);
                    f->artifact->inspection_code = f->artifact->code;
                    f->artifact->dispatch = NULL;
                }
                behavior_instance *bi = behavior_instance_create(a, &owner, NULL, NULL);
                assert(
                    !behavior_instance_set_backend(bi, mode % 3 == 1 ? BEHAVIOR_BYTECODE : BEHAVIOR_SLJIT)
                );
                assert(!behavior_instance_bind_service(bi, "ecs.world", world));
                assert(!behavior_instance_set_activation_pool(bi, true));
                behavior_instance_set_trace_enabled(bi, false);
                behavior_batch *batch = NULL;
                uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls(),
                         allocations = 0;
                double start = 0;
                for (int i = -32; i < count; i++) {
                    if (!i) {
                        allocations = behavior_function_data_allocations();
                        start = seconds();
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
                        assert(!behavior_instance_release_activation(bi, run));
                    }
                }
                double elapsed = seconds() - start;
                allocations = behavior_function_data_allocations() - allocations;
                assert(!allocations && eval == eval_oracle_calls() && flow == flow_oracle_calls());
                float values[32][4];
                render_shader_parameters(material, world, entity, NULL, values);
                for (int i = 0; i < 2; i++) {
                    assert(values[i][0] == .25f);
                    assert(
                        values[i][1] == (vector ? -.5f : 0) &&
                        values[i][2] == (vector ? .75f : 0)
                    );
                }
                printf(
                    "%s material_%s_%d_%s ns=%.3f checksum=%d "
                    "data_allocations=%llu\n",
                    (const char *[]){ "SLJIT", "BYTECODE", "GENERATED_C" }[mode % 3],
                    vector ? "vec3" : "float",
                    entities,
                    mode >= 3 ? "batch" : "activation",
                    elapsed * 1e9 / count,
                    (int)(values[0][0] * 1000 + values[1][0] * 1000),
                    (unsigned long long)allocations
                );
                behavior_batch_free(batch);
                behavior_instance_destroy(bi);
                f->artifact->code = code;
                f->artifact->inspection_code = inspection;
                f->artifact->dispatch = dispatch;
                behavior_asset_free(a);
            }
            render_shader_clear_world(world);
            ecs_world_free(world);
        }
    }
    assert(!dlclose(library));
    render_shader_catalog_clear();
    return 0;
}
