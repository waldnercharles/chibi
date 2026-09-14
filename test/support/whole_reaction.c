#include "graph/model/registry.h"
#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "runtime/generated.h"
#include "runtime/visual_motion.h"
#include "test/whole_reaction_fixture.h"

#include <assert.h>

behavior_asset *whole_reaction_fixture(bool waits, bool target)
{
    behavior_asset *a = behavior_asset_new("Visual reaction");
    assert(!behavior_asset_set_owner_type(a, "ecs.entity"));
    assert(behavior_asset_add_variable(a, "Done", PIN_BOOL, NULL, PIN_CONTAINER_SINGLE, NULL, ""));
    graph *g = behavior_asset_add_function(a, "Apply", false);
    node *effect = graph_add_node(g, &NODE_Set_Visual_Reaction);
    node *event = graph_add_node(a->graph, &NODE_Event);
    assert(!graph_set_event_name(a->graph, event, "Run"));
    for (int i = 1; i < 4; i++) {
        pin_spec pin = NODE_Set_Visual_Reaction.inputs[i];
        pin.flags = 0;
        assert(graph_add_pin(g, g->nodes[0], 1, &pin) == i);
        assert(graph_add_pin(a->graph, event, 1, &pin) == i);
        if (target || i != 3) {
            assert(!graph_connect_data(g, g->nodes[0], i, effect, i));
        }
    }
    node *tail = g->nodes[0];
    if (waits) {
        node *delay = graph_add_node(g, &NODE_Delay);
        assert(
            !graph_set_pin_default(g, delay, 0, 1, &(pin_value){ .type = PIN_F64, .d = .125 })
        );
        assert(!graph_connect_exec(g, tail, 0, delay, 0, 0));
        tail = delay;
    }
    assert(!graph_connect_exec(g, tail, 0, effect, 0, 0));
    assert(!graph_connect_exec(g, effect, 0, g->nodes[1], 0, 0));
    node *call = behavior_asset_add_function_call_node(a, g);
    for (int i = 1; i < 4; i++) {
        assert(!graph_connect_data(a->graph, event, i, call, i));
    }
    node *done = behavior_asset_add_set_node(a, a->variables[0].id);
    assert(
        !graph_set_pin_default(a->graph, done, 0, 1, &(pin_value){ .type = PIN_BOOL, .b = true })
    );
    assert(!graph_connect_exec(a->graph, event, 0, call, 0, 0));
    assert(!graph_connect_exec(a->graph, call, 0, done, 0, 0));
    return a;
}
void whole_reaction_smoke()
{
    for (int target = 0; target < 2; target++) {
        behavior_asset *a = whole_reaction_fixture(true, target);
        ecs_world *world = ecs_world_new();
        assert(!ecs_world_register_component(world, &GAME_COMPONENT_GameVisualMotion, NULL));
        ecs_entity owner = ecs_world_create_entity(world),
                   other = ecs_world_create_entity(world);
        assert(!GAME_GameVisualMotion_add(world, owner, 0));
        assert(!GAME_GameVisualMotion_add(world, other, 0));
        assert(!ecs_world_apply_commands(world));
        GameVisualMotion initial = { .speed = 2,
                                     .reaction_age = 7,
                                     .reaction_duration = .25f };
        *GAME_GameVisualMotion_get(world, owner) = initial;
        *GAME_GameVisualMotion_get(world, other) = initial;
        pin_value self = { .type = PIN_HANDLE,
                           .type_name = "ecs.entity",
                           .u64 = ecs_entity_to_u64(owner) };
        behavior_instance *bi = behavior_instance_create(a, &self, NULL, NULL);
        behavior_instance_set_trace_enabled(bi, false);
        assert(!behavior_instance_bind_service(bi, "ecs.world", world));
        CF_V3 rotation = { -1, 2, 3 };
        pin_value args[] = { { .type = PIN_STRUCT, .type_name = "CF_V3", .ptr = &rotation },
                             { .type = PIN_F32, .f = 4 },
                             { .type = PIN_HANDLE,
                               .type_name = "ecs.entity",
                               .u64 = ecs_entity_to_u64(other) } };
        uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
        flow_run *run = behavior_instance_dispatch(bi, "Run", (flow_inputs){ args, 3 }, NULL);
        assert(run && run->whole_function_executed && flow_run_status(run) == FLOW_SUSPENDED);
        rotation.x = 99;
        assert(!behavior_instance_tick(bi, .125));
        assert(flow_run_status(run) == FLOW_COMPLETED && bi->variables[0].b);
        const GameVisualMotion *actual = GAME_GameVisualMotion_get_const(world, target ? other : owner);
        assert(
            actual->reaction_rotation.x == -1 &&
            actual->reaction_rotation.y == 2 && actual->reaction_rotation.z == 3
        );
        assert(actual->reaction_lift == 4 && actual->reaction_age == 0 && actual->speed == 2);
        assert(GAME_GameVisualMotion_get_const(world, target ? owner : other)->reaction_age == 7);
        assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
        behavior_instance_destroy(bi);
        behavior_asset_free(a);
        ecs_world_free(world);
    }
    printf(
        "PASS: %s whole visual reaction, native component writes, nested wait "
        "and explicit/default target; zero oracle calls\n",
        behavior_backend_name(behavior_default_backend())
    );
}
