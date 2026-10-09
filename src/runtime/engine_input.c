#include "runtime/engine_input.h"

#include "execution/status.h"
#include "runtime/component_catalog.h"
#include "runtime/events.h"
#include "runtime/systems/scheduler.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *action;
    bool pressed;
} input_transition;
typedef struct
{
    char *action;
    bool pressed;
    /* started: went down since the last step began; stepped: before this one. */
    bool started, stepped;
} input_button;
typedef struct
{
    ecs_entity entity;
    bool pressed;
} binding_change;

struct engine_input
{
    ecs_world *world;
    engine_input_host host;
    game_schedule *systems;
    CF_V2 movement;
    bool movement_sampled;
    CF_V2 aim;
    bool aim_sampled, aim_point, firing;
    uint64_t controls;
    /* Controls held after each change since the last step, oldest first. */
    uint64_t *trail;
    input_button *buttons;
    input_transition *transitions;
    ecs_entity *controlled;
    ecs_entity *released;
    binding_change *changes;
    char message[256];
};

engine_input *engine_input_new(ecs_world *world, const engine_input_host *host, game_schedule *systems)
{
    engine_input *input = xcalloc(1, sizeof *input);
    input->world = world;
    if (host) {
        input->host = *host;
    }
    input->systems = systems;
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
    afree(input->trail);
    afree(input->buttons);
    afree(input->controlled);
    afree(input->released);
    afree(input->changes);
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
    aclear(input->trail);
    clear_actions(input);
    /* Pause may never run another fixed tick. Clear reflected local input now,
     * without dispatching authored gameplay while transport is stopped. */
    ecs_component_id component = GAME_COMPONENT_GameInput.id;
    if (ecs_world_component(input->world, component)) {
        ecs_query query;
        int result = ecs_query_begin(input->world, &query, &component, 1);
        assert(result == ECS_OK);
        (void)result;
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
        apush(
            input->buttons,
            ((input_button){ .action = xstrdup(action), .pressed = down, .started = down })
        );
        return down;
    }
    input_button *button = &input->buttons[index];
    if (button->pressed == down) {
        return false;
    }
    button->pressed = down;
    button->started |= down;
    return true;
}

int engine_input_action(engine_input *input, const char *action, bool pressed)
{
    if (!input || !action || !action[0]) {
        return FLOW_ERR_ARGUMENT;
    }
    if (!input->host.has_event || !input->host.has_event(input->host.context, action)) {
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
    if (controls != input->controls) {
        apush(input->trail, controls);
    }
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
                apush(sample, ((input_button){ .action = (char *)binding->action, .pressed = down }));
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

int engine_input_trail(const engine_input *input, const uint64_t **controls)
{
    *controls = input->trail;
    return acount(input->trail);
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

static int dispatch(engine_input *input, ecs_entity entity, const char *event, execution_arguments arguments)
{
    if (!input->host.dispatch) {
        return FLOW_ERR_ARGUMENT;
    }
    return input->host.dispatch(
        input->host.context,
        entity,
        event,
        arguments,
        input->message,
        sizeof input->message
    );
}

static int dispatch_movement(engine_input *input)
{
    ecs_component_id component = GAME_COMPONENT_GameInput.id;
    if (!ecs_world_component(input->world, component)) {
        return FLOW_OK;
    }
    ecs_query query;
    int result = ecs_query_begin(input->world, &query, &component, 1);
    if (result != ECS_OK) {
        return result;
    }
    CF_V2 direction = input->movement;
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = GAME_T_CF_V2.name,
                        .ptr = &direction };
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        GameInput *member = GAME_GameInput_get(input->world, entity);
        bool active = direction.x != 0 || direction.y != 0;
        bool was_active = member->move.x != 0 || member->move.y != 0;
        member->move = direction;
        if (!active && !was_active) {
            continue;
        }
        result = dispatch(
            input,
            entity,
            game_host_event_name(GAME_HOST_EVENT_INPUT),
            (execution_arguments){ &value, 1 }
        );
        if (result < 0) {
            break;
        }
    }
    ecs_query_end(&query);
    return result < 0 ? result : FLOW_OK;
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
        result = dispatch(
            input,
            released[i],
            game_host_event_name(GAME_HOST_EVENT_INPUT),
            (execution_arguments){ &value, 1 }
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
        result = dispatch(
            input,
            entity,
            game_host_event_name(GAME_HOST_EVENT_INPUT),
            (execution_arguments){ &value, 1 }
        );
        if (result < 0) {
            break;
        }
    }
    afree(released);
    afree(input->controlled);
    input->controlled = controlled;
    return result < 0 ? result : dispatch_movement(input);
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
            result = dispatch(input, entity, transitions[i].action, (execution_arguments){ 0 });
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
    (void)result;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        GameActionBinding *binding = GAME_GameActionBinding_get(input->world, entity);
        int index = binding->action ? button_index(input, binding->action) : -1;
        bool pressed = !binding->disabled && index >= 0 && input->buttons[index].pressed;
        if (binding->pressed != pressed) {
            binding->pressed = pressed;
            apush(input->changes, ((binding_change){ entity, pressed }));
        }
    }
    ecs_query_end(&query);
}

int engine_input_apply(engine_input *input)
{
    input->message[0] = 0;
    aclear(input->trail);
    for (int i = 0; i < acount(input->buttons); i++) {
        input->buttons[i].stepped = input->buttons[i].started;
        input->buttons[i].started = false;
    }
    apply_action_state(input);
    int result = apply_aim_sample(input);
    if (result >= 0) {
        result = apply_movement_sample(input);
    }
    /* Notify source field observers before an event boundary samples the
     * mutated component. Each explicit edge remains authoritative, including
     * repeated payloads; external actions still precede PressedChanged events. */
    binding_change *changes = input->changes;
    input->changes = NULL;
    for (int i = 0; result >= 0 && i < acount(changes); i++) {
        if (!input->host.field_changed ||
            !ecs_entity_is_alive(input->world, changes[i].entity) ||
            !GAME_GameActionBinding_has(input->world, changes[i].entity)) {
            continue;
        }
        result = input->host.field_changed(
            input->host.context,
            changes[i].entity,
            GAME_COMPONENT_GameActionBinding.id,
            offsetof(GameActionBinding, pressed),
            (pin_value){ .type = PIN_BOOL, .b = changes[i].pressed },
            input->message,
            sizeof input->message
        );
    }
    if (result >= 0) {
        result = dispatch_actions(input);
    }
    /* Gameplay runs after releasing the component query. Focus/transport
     * clears enqueue their release here and deliver it when simulation resumes. */
    for (int i = 0; result >= 0 && i < acount(changes); i++) {
        if (!ecs_entity_is_alive(input->world, changes[i].entity) ||
            !GAME_GameActionBinding_has(input->world, changes[i].entity)) {
            continue;
        }
        pin_value pressed = { .type = PIN_BOOL, .b = changes[i].pressed };
        result = dispatch(
            input,
            changes[i].entity,
            game_host_event_name(GAME_HOST_EVENT_PRESSED_CHANGED),
            (execution_arguments){ &pressed, 1 }
        );
        if (result >= 0) {
            game_schedule_error error = { 0 };
            result = game_schedule_field_changed(
                input->systems,
                changes[i].entity,
                GAME_COMPONENT_GameActionBinding.id,
                offsetof(GameActionBinding, pressed),
                pressed,
                &error
            );
            if (result < 0) {
                snprintf(input->message, sizeof input->message, "%s", error.message);
            }
        }
    }
    afree(changes);
    if (result < 0 && !input->message[0]) {
        snprintf(input->message, sizeof input->message, "Input action failed");
    }
    return result < 0 ? result : FLOW_OK;
}

const char *engine_input_error(const engine_input *input)
{
    return input->message;
}

static const input_button *named_button(const engine_input *input, const char *action)
{
    int index = input ? button_index(input, action) : -1;
    return index < 0 ? NULL : &input->buttons[index];
}

static const char *action_held(void *service, const pin_value *in, pin_value *out)
{
    if (!in[0].str || !in[0].str[0]) {
        return "Action Held requires an action name";
    }
    const input_button *button = named_button(service, in[0].str);
    out[0].b = button && button->pressed;
    return NULL;
}

static const char *action_pressed(void *service, const pin_value *in, pin_value *out)
{
    if (!in[0].str || !in[0].str[0]) {
        return "Action Pressed requires an action name";
    }
    const input_button *button = named_button(service, in[0].str);
    out[0].b = button && button->stepped;
    return NULL;
}

const node_host_call GAME_INPUT_HELD_CALL = { .service = GAME_INPUT_SERVICE,
                                              .invoke = action_held,
                                              .optional_service = true };
const node_host_call GAME_INPUT_PRESSED_CALL = { .service = GAME_INPUT_SERVICE,
                                                 .invoke = action_pressed,
                                                 .optional_service = true };
const node_access GAME_INPUT_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
