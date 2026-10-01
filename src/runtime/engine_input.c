#include "runtime/engine_input.h"

#include "graph/document/events.h"
#include "runtime/generated.h"
#include "runtime/scenes/gameplay.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *action;
    bool pressed;
} input_transition;
typedef input_transition input_button;

struct engine_input
{
    ecs_world *world;
    game_behavior_world *behaviors;
    CF_V2 movement;
    bool movement_sampled;
    CF_V2 aim;
    bool aim_sampled, aim_point, firing;
    uint64_t controls;
    input_button *buttons;
    input_transition *transitions;
    ecs_entity *controlled;
    ecs_entity *released;
};

engine_input *engine_input_new(ecs_world *world, game_behavior_world *behaviors)
{
    engine_input *input = xcalloc(1, sizeof *input);
    input->world = world;
    input->behaviors = behaviors;
    return input;
}

void engine_input_free(engine_input *input)
{
    for (int i = 0; i < acount(input->transitions); i++) {
        free(input->transitions[i].action);
    }
    for (int i = 0; i < acount(input->buttons); i++) {
        free(input->buttons[i].action);
    }
    afree(input->transitions);
    afree(input->buttons);
    afree(input->controlled);
    afree(input->released);
    free(input);
}

static bool has_entity(const ecs_entity *entities, ecs_entity entity)
{
    for (int i = 0; i < acount(entities); i++) {
        if (ecs_entity_to_u64(entities[i]) == ecs_entity_to_u64(entity)) {
            return true;
        }
    }
    return false;
}

static int button_index(const engine_input *input, const char *action)
{
    for (int i = 0; i < acount(input->buttons); i++) {
        if (!strcmp(input->buttons[i].action, action)) {
            return i;
        }
    }
    return -1;
}

static void free_transitions(engine_input *input)
{
    for (int i = 0; i < acount(input->transitions); i++) {
        free(input->transitions[i].action);
    }
    aclear(input->transitions);
}

static void apply_action_state(engine_input *input);

static void clear_actions(engine_input *input)
{
    free_transitions(input);
    for (int i = 0; i < acount(input->buttons); i++) {
        free(input->buttons[i].action);
    }
    aclear(input->buttons);
    apply_action_state(input);
}

static void clear_owner(engine_input *input, ecs_entity entity)
{
    if (!ecs_entity_is_alive(input->world, entity)) {
        return;
    }
    GameInput *controls = GAME_GameInput_get(input->world, entity);
    if (controls) {
        if ((controls->move.x != 0 || controls->move.y != 0) &&
            !has_entity(input->released, entity)) {
            apush(input->released, entity);
        }
        controls->move = (CF_V2){ 0 };
    }
    GameAim *aim = GAME_GameAim_get(input->world, entity);
    if (aim) {
        aim->firing = false;
        aim->manual = false;
    }
}

void engine_input_clear(engine_input *input)
{
    input->movement = (CF_V2){ 0 };
    input->movement_sampled = true;
    input->aim = (CF_V2){ 0 };
    input->aim_point = false;
    input->aim_sampled = true;
    input->firing = false;
    input->controls = 0;
    clear_actions(input);
    /* Pause may never run another fixed tick. Clear reflected local input now,
     * without dispatching authored gameplay while transport is stopped. */
    ecs_component_id component = GAME_COMPONENT_GameInput.id;
    if (ecs_world_component(input->world, component)) {
        ecs_query query;
        int result = ecs_query_begin(input->world, &query, &component, 1);
        assert(result == ECS_OK);
        (void)result;
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            clear_owner(input, entity);
        }
        ecs_query_end(&query);
    }
    for (int i = 0; i < acount(input->controlled); i++) {
        clear_owner(input, input->controlled[i]);
    }
}

void engine_input_reset(engine_input *input)
{
    engine_input_clear(input);
    aclear(input->controlled);
    aclear(input->released);
}

static bool update_button(engine_input *input, const char *action, bool down)
{
    int index = button_index(input, action);
    if (index < 0) {
        apush(input->buttons, ((input_button){ xstrdup(action), down }));
        return down;
    }
    if (input->buttons[index].pressed == down) {
        return false;
    }
    input->buttons[index].pressed = down;
    return true;
}

int engine_input_action(engine_input *input, const char *action, bool pressed)
{
    if (!input || !action || !action[0]) {
        return FLOW_ERR_ARGUMENT;
    }
    if (!game_behavior_has_event(input->behaviors, action)) {
        return FLOW_ERR_UNKNOWN_EVENT;
    }
    if (!update_button(input, action, pressed)) {
        return 0;
    }
    apush(input->transitions, ((input_transition){ xstrdup(action), pressed }));
    return 1;
}

void engine_input_movement(engine_input *input, CF_V2 movement)
{
    input->movement = movement;
    input->movement_sampled = true;
}

void engine_input_aim(engine_input *input, CF_V2 aim, bool firing, bool point)
{
    input->aim = aim;
    input->aim_point = point;
    input->firing = firing;
    input->aim_sampled = true;
}

int engine_input_controls(engine_input *input, uint64_t controls, bool *firing)
{
    _Static_assert(GAME_CONTROL_COUNT < 64, "control bitset");
    *firing = false;
    input->controls = controls;
    input_button *sample = NULL;
    ecs_component_id component = GAME_COMPONENT_GameActionBinding.id;
    ecs_query query;
    int result = FLOW_OK;
    if (ecs_world_component(input->world, component) &&
        ecs_query_begin(input->world, &query, &component, 1) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            const GameActionBinding
                *binding = GAME_GameActionBinding_get_const(input->world, entity);
            if (binding->disabled || ecs_entity_is_retiring(input->world, entity)) {
                continue;
            }
            if (binding->source <= GAME_CONTROL_NONE ||
                binding->source >= GAME_CONTROL_COUNT || !binding->action ||
                !binding->action[0]) {
                result = FLOW_ERR_ARGUMENT;
                break;
            }
            bool down = (controls & (UINT64_C(1) << binding->source)) != 0;
            *firing |= down && binding->firing;
            int index = 0;
            while (index < acount(sample) &&
                   strcmp(sample[index].action, binding->action)) {
                index++;
            }
            if (index == acount(sample)) {
                apush(sample, ((input_button){ (char *)binding->action, down }));
            } else {
                sample[index].pressed |= down;
            }
        }
        ecs_query_end(&query);
    }
    if (result >= 0) {
        for (int i = 0; i < acount(input->buttons); i++) {
            int index = 0;
            while (index < acount(sample) &&
                   strcmp(sample[index].action, input->buttons[i].action)) {
                index++;
            }
            if (index == acount(sample)) {
                if (engine_input_action(input, input->buttons[i].action, false) ==
                    FLOW_ERR_UNKNOWN_EVENT) {
                    input->buttons[i].pressed = false;
                }
            }
        }
        for (int i = 0; i < acount(sample); i++) {
            int dispatched = engine_input_action(
                input,
                sample[i].action,
                sample[i].pressed
            );
            /* Held state is useful to Tick graphs without an event receiver. */
            if (dispatched == FLOW_ERR_UNKNOWN_EVENT) {
                update_button(input, sample[i].action, sample[i].pressed);
            } else if (dispatched < 0) {
                result = dispatched;
                break;
            }
        }
    }
    afree(sample);
    return result;
}

input_keyframe engine_input_held(const engine_input *input)
{
    return (input_keyframe){ .movement = input->movement,
                             .aim = input->aim,
                             .aim_point = input->aim_point,
                             .firing = input->firing,
                             .controls = input->controls };
}

int engine_input_hold(engine_input *input, const input_keyframe *key)
{
    bool firing;
    int result = engine_input_controls(input, key->controls, &firing);
    engine_input_movement(input, key->movement);
    engine_input_aim(input, key->aim, key->firing, key->aim_point);
    return result;
}

static int apply_aim_sample(engine_input *input)
{
    if (!input->aim_sampled ||
        !ecs_world_component(input->world, GAME_COMPONENT_GameAim.id)) {
        return FLOW_OK;
    }
    ecs_component_id required[] = { GAME_COMPONENT_GameInput.id,
                                    GAME_COMPONENT_GameAim.id,
                                    GAME_COMPONENT_GameTransform.id };
    ecs_query query;
    int result = ecs_query_begin(input->world, &query, required, 3);
    if (result != ECS_OK) {
        return result;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        GameAim *aim = GAME_GameAim_get(input->world, entity);
        aim->firing = input->firing;
        CF_V2 direction = input->aim;
        if (input->aim_point) {
            const GameTransform *position = GAME_GameTransform_get_const(input->world, entity);
            direction.x -= position->x;
            direction.y -= position->y;
        }
        /* Float products fit in double; sqrt rounds the same on native and WASM. */
        float length = (float)sqrt(
            (double)direction.x * direction.x + (double)direction.y * direction.y
        );
        if (length <= .1f) {
            /* Releasing aim ends manual control but keeps the last facing. */
            aim->manual = false;
            continue;
        }
        aim->manual = true;
        aim->direction = (CF_V2){ direction.x / length, direction.y / length };
    }
    ecs_query_end(&query);
    return FLOW_OK;
}

static int apply_movement_sample(engine_input *input)
{
    if (!input->movement_sampled) {
        return FLOW_OK;
    }
    ecs_entity *controlled = NULL;
    ecs_component_id component = GAME_COMPONENT_GameInput.id;
    if (ecs_world_component(input->world, component)) {
        ecs_query query;
        int result = ecs_query_begin(input->world, &query, &component, 1);
        if (result != ECS_OK) {
            return result;
        }
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            apush(controlled, entity);
        }
        ecs_query_end(&query);
    }
    CF_V2 zero = { 0 };
    pin_value value = {
        .type = PIN_STRUCT,
        .type_name = GAME_T_CF_V2.name,
        .ptr = &zero,
    };
    int result = FLOW_OK;
    ecs_entity *released = input->released;
    input->released = NULL;
    /* Reflected input was cleared immediately when focus or transport changed.
     * Deliver its authored release only when simulation resumes. */
    for (int i = 0; i < acount(released); i++) {
        if (!ecs_entity_is_alive(input->world, released[i])) {
            continue;
        }
        result = game_behavior_dispatch(
            input->behaviors,
            released[i],
            graph_host_event_name(GRAPH_HOST_EVENT_INPUT),
            (flow_inputs){ &value, 1, NULL },
            NULL
        );
        if (result < 0) {
            break;
        }
    }
    for (int i = 0; i < acount(input->controlled) && result >= 0; i++) {
        ecs_entity entity = input->controlled[i];
        if (has_entity(controlled, entity) || has_entity(released, entity) ||
            !ecs_entity_is_alive(input->world, entity)) {
            continue;
        }
        GameAim *aim = GAME_GameAim_get(input->world, entity);
        if (aim) {
            aim->firing = false;
            aim->manual = false;
        }
        result = game_behavior_dispatch(
            input->behaviors,
            entity,
            graph_host_event_name(GRAPH_HOST_EVENT_INPUT),
            (flow_inputs){ &value, 1, NULL },
            NULL
        );
        if (result < 0) {
            break;
        }
    }
    afree(released);
    afree(input->controlled);
    input->controlled = controlled;
    return result < 0
             ? result
             : game_movement_dispatch(input->world, input->behaviors, input->movement);
}

static int dispatch_actions(engine_input *input)
{
    ecs_component_id component = GAME_COMPONENT_GameInput.id;
    if (!ecs_world_component(input->world, component)) {
        free_transitions(input);
        return FLOW_OK;
    }
    input_transition *transitions = input->transitions;
    input->transitions = NULL;
    ecs_entity *recipients = NULL;
    int result = FLOW_OK;
    for (int i = 0; i < acount(transitions) && result >= 0; i++) {
        if (!transitions[i].pressed) {
            continue;
        }
        ecs_query query;
        result = ecs_query_begin(input->world, &query, &component, 1);
        if (result != ECS_OK) {
            break;
        }
        aclear(recipients);
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            apush(recipients, entity);
        }
        ecs_query_end(&query);
        /* Actions may query contacts or spawn actors, so release ECS iteration
         * before running the authored behavior. */
        for (int j = 0; j < acount(recipients); j++) {
            entity = recipients[j];
            if (!GAME_GameInput_has(input->world, entity)) {
                continue;
            }
            result = game_behavior_dispatch(
                input->behaviors,
                entity,
                transitions[i].action,
                (flow_inputs){ 0 },
                NULL
            );
            if (result < 0) {
                break;
            }
        }
    }
    afree(recipients);
    for (int i = 0; i < acount(transitions); i++) {
        free(transitions[i].action);
    }
    afree(transitions);
    return result < 0 ? result : FLOW_OK;
}

static void apply_action_state(engine_input *input)
{
    ecs_component_id component = GAME_COMPONENT_GameActionBinding.id;
    if (!ecs_world_component(input->world, component)) {
        return;
    }
    ecs_query query;
    int result = ecs_query_begin(input->world, &query, &component, 1);
    assert(result == ECS_OK);
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        GameActionBinding *binding = GAME_GameActionBinding_get(input->world, entity);
        int index = binding->action ? button_index(input, binding->action) : -1;
        binding->pressed = !binding->disabled && index >= 0 &&
                           input->buttons[index].pressed;
    }
    ecs_query_end(&query);
}

int engine_input_apply(engine_input *input)
{
    apply_action_state(input);
    int result = apply_aim_sample(input);
    if (result >= 0) {
        result = apply_movement_sample(input);
    }
    return result < 0 ? result : dispatch_actions(input);
}
