#include "graph/model/registry.h"
#include "graph/nodes/math_nodes.h"
#include "graph/nodes/particle_nodes.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/random.h"
#include "test/whole_particle_fixture.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

behavior_asset *whole_particle_fixture(bool directed, bool waits, bool random)
{
    behavior_asset *a = behavior_asset_new("Compiled particles");
    graph *g = behavior_asset_add_function(a, "Emit twice", false);
    node *entry = g->nodes[0], *ret = g->nodes[1];
    const node_desc *desc = directed ? &NODE_Emit_Directed_Particles
                                     : &NODE_Emit_Particles;
    node *first = graph_add_node(g, desc), *second = graph_add_node(g, desc);
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(!graph_set_event_name(a->graph, event, "Run"));
    static const float vector[2] = { 1, -2 };
    for (int i = 1; i < desc->input_count; i++) {
        pin_spec p = first->input_pins[i];
        p.id = p.flags = p.group = 0;
        if (p.type == PIN_STRUCT) {
            p.default_value = (pin_value){ .type = PIN_STRUCT,
                                           .type_name = "CF_V2",
                                           .ptr = (void *)vector };
        }
        assert(graph_add_pin(g, entry, 1, &p) == i);
        assert(graph_add_pin(a->graph, event, 1, &p) == i);
        assert(!graph_connect_data(g, entry, i, first, i));
        assert(!graph_connect_data(g, entry, i, second, i));
    }
    if (random) {
        node *sample = graph_add_node(g, &NODE_RandomReal);
        int speed = directed ? 6 : 4;
        assert(!graph_connect_data(g, sample, 0, first, speed));
        assert(!graph_connect_data(g, sample, 0, second, speed));
    }
    assert(!graph_connect_exec(g, entry, 0, first, 0, 0));
    if (waits) {
        node *delay = graph_add_node(g, &NODE_Delay);
        assert(
            !graph_set_pin_default(g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = .125 })
        );
        assert(!graph_connect_exec(g, first, 0, delay, 0, 0));
        assert(!graph_connect_exec(g, delay, 0, second, 0, 0));
    } else {
        assert(!graph_connect_exec(g, first, 0, second, 0, 0));
    }
    assert(!graph_connect_exec(g, second, 0, ret, 0, 0));
    node *call = behavior_asset_add_function_call_node(a, g);
    assert(!graph_connect_exec(a->graph, event, 0, call, 0, 0));
    for (int i = 1; i < desc->input_count; i++) {
        assert(!graph_connect_data(a->graph, event, i, call, i));
    }
    return a;
}
void whole_particle_smoke()
{
    for (int directed = 0; directed < 2; directed++) {
        behavior_asset *a = whole_particle_fixture(directed, true, true);
        game_particles *expected = game_particles_new(),
                       *actual = game_particles_new();
        int next = 0;
        for (int mode = 0; mode < 2; mode++) {
            game_particles *particles = mode ? actual : expected;
            behavior_instance *bi = behavior_instance_create(a, NULL, NULL, NULL);
            assert(
                !behavior_instance_set_backend(bi, mode ? behavior_default_backend() : BEHAVIOR_ORACLE)
            );
            behavior_instance_set_trace_enabled(bi, false);
            assert(!behavior_instance_bind_service(bi, "game.particles", particles));
            behavior_srand(317);
            uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
            flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ 0 }, NULL);
            assert(run && flow_run_status(run) == FLOW_SUSPENDED);
            assert(!behavior_instance_tick(bi, .125));
            assert(flow_run_status(run) == FLOW_COMPLETED);
            if (!mode) {
                next = behavior_rand();
            } else {
                assert(run->whole_function_executed && behavior_rand() == next);
                assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
                int count = game_particles_count(actual);
                assert(count > 0 && count == game_particles_count(expected));
                assert(!memcmp(
                    game_particles_data(actual),
                    game_particles_data(expected),
                    (size_t)count * sizeof(game_particle)
                ));
            }
            behavior_instance_destroy(bi);
        }
        game_particles_free(expected);
        game_particles_free(actual);
        behavior_asset_free(a);
    }
    printf(
        "PASS: %s whole particle bursts and sprays, nested waits and "
        "deterministic state; zero compiled oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
