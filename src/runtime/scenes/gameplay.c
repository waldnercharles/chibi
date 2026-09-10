#include "runtime/scenes/gameplay.h"

#include "graph/document/events.h"
#include "runtime/behaviors/entities.h"
#include "runtime/generated.h"

int game_movement_dispatch(ecs_world *world, game_behavior_world *behaviors, CF_V2 direction)
{
    ecs_query query;
    ecs_component_id component = GAME_COMPONENT_GameInput.id;
    if (!ecs_world_component(world, component)) {
        return FLOW_OK;
    }
    int result = ecs_query_begin(world, &query, &component, 1);
    if (result != ECS_OK) {
        return result;
    }
    pin_value value = {
        .type = PIN_STRUCT,
        .type_name = GAME_T_CF_V2.name,
        .ptr = &direction,
    };
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        GameInput *input = GAME_GameInput_get(world, entity);
        bool active = direction.x != 0 || direction.y != 0;
        bool was_active = input->move.x != 0 || input->move.y != 0;
        input->move = direction;
        if (!active && !was_active) {
            continue;
        }
        result = game_behavior_dispatch(
            behaviors,
            entity,
            graph_host_event_name(GRAPH_HOST_EVENT_INPUT),
            (flow_inputs){ &value, 1, NULL },
            NULL
        );
        if (result < 0) {
            break;
        }
    }
    ecs_query_end(&query);
    return result < 0 ? result : FLOW_OK;
}
