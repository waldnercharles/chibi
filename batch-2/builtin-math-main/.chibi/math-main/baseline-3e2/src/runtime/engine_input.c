#include "runtime/engine_input.h"

#include "execution/function.h"
#include "execution/status.h"
#include "runtime/component_catalog.h"
#include "runtime/events.h"
#include "runtime/transform_propagation.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/text.h"

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

struct engine_input
{
    ecs_world *world;
    engine_input_host host;
    input_action *actions;
    CF_V2 movement;
    bool movement_sampled;
    CF_V2 aim;
    bool aim_sampled, aim_point, firing;
    input_pointer_ray pointer;
    uint64_t controls;
    /* Device snapshots after each change since the last step, oldest first. */
    input_keyframe *trail;
    input_controller_state controllers[INPUT_CONTROLLER_MAX];
    input_button *buttons;
    input_transition *transitions;
    ecs_entity *controlled;
    ecs_entity *released;
    char message[256];
};

engine_input *engine_input_new(ecs_world *world, const engine_input_host *host)
{
    engine_input *input = xcalloc(1, sizeof *input);
    input->world = world;
    if (host) {
        input->host = *host;
    }
    return input;
}

static void free_actions(engine_input *input)
{
    for (int i = 0; i < acount(input->actions); i++) {
        free((char *)input->actions[i].name);
    }
    afree(input->actions);
}

void engine_input_set_actions(engine_input *input, const input_action *actions, int count)
{
    free_actions(input);
    for (int i = 0; i < count; i++) {
        input_action action = actions[i];
        assert(action.controller >= 0 && action.controller < INPUT_CONTROLLER_MAX);
        for (int j = 0; j < i; j++) {
            assert(strcmp(actions[j].name, action.name));
        }
        action.name = xstrdup(action.name);
        apush(input->actions, action);
    }
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
    free_actions(input);
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

static int action_index(const engine_input *input, const char *name)
{
    for (int i = 0; i < acount(input->actions); i++) {
        if (!strcmp(input->actions[i].name, name)) {
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

static void clear_actions(engine_input *input)
{
    free_transitions(input);
    for (int i = 0; i < acount(input->buttons); i++) {
        free(input->buttons[i].action);
    }
    aclear(input->buttons);
}

static void clear_owner(engine_input *input, ecs_entity entity)
{
    if (!ecs_entity_is_alive(input->world, entity)) {
        return;
    }
    Input *controls = Input_get(input->world, entity);
    if (controls) {
        if ((controls->move.x != 0 || controls->move.y != 0) &&
            !has_entity(input->released, entity)) {
            apush(input->released, entity);
        }
        controls->move = (CF_V2){ 0 };
    }
    Aim *aim = Aim_get(input->world, entity);
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
    input->pointer = (input_pointer_ray){ 0 };
    /* Pause may never run another fixed tick. Clear reflected local input now,
     * without dispatching authored gameplay while transport is stopped. Held
     * controls and actions stay, so resuming with the same controls held
     * presses nothing again. */
    ecs_component_id component = COMPONENT_Input.id;
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
    input->controls = 0;
    memset(input->controllers, 0, sizeof input->controllers);
    aclear(input->trail);
    clear_actions(input);
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

void engine_input_pointer(engine_input *input, input_pointer_ray pointer)
{
    input->pointer = pointer.active ? pointer : (input_pointer_ray){ 0 };
}

int engine_input_devices(
    engine_input *input,
    uint64_t controls,
    const input_controller_state controllers[INPUT_CONTROLLER_MAX],
    bool *firing
)
{
    _Static_assert(CONTROL_COUNT < 64, "control bitset");
    _Static_assert(PAD_COUNT < 32, "controller bitset");
    uint32_t all_buttons = ((UINT32_C(1) << PAD_COUNT) - 1) & ~UINT32_C(1);
    for (int i = 0; i < INPUT_CONTROLLER_MAX; i++) {
        if (controllers[i].buttons & ~all_buttons) {
            return FLOW_ERR_ARGUMENT;
        }
    }
    input_keyframe previous = engine_input_held(input);
    input->controls = controls;
    for (int i = 0; i < INPUT_CONTROLLER_MAX; i++) {
        input->controllers[i] = controllers[i];
        if (!controllers[i].connected) {
            input->controllers[i].buttons = 0;
        }
    }
    input_keyframe held = engine_input_held(input);
    if (!input_keyframe_same(&previous, &held)) {
        apush(input->trail, held);
    }
    *firing = false;
    for (int i = 0; i < acount(input->buttons); i++) {
        if (action_index(input, input->buttons[i].action) < 0 &&
            engine_input_action(input, input->buttons[i].action, false) ==
                FLOW_ERR_UNKNOWN_EVENT) {
            input->buttons[i].pressed = false;
        }
    }
    for (int i = 0; i < acount(input->actions); i++) {
        const input_action *action = &input->actions[i];
        bool down = (controls & action->controls) != 0 ||
                    (input->controllers[action->controller].buttons &
                     action->buttons) != 0;
        *firing |= down && action->firing;
        int dispatched = engine_input_action(input, action->name, down);
        if (dispatched == FLOW_ERR_UNKNOWN_EVENT) {
            update_button(input, action->name, down);
        } else if (dispatched < 0) {
            return dispatched;
        }
    }
    return FLOW_OK;
}

int engine_input_controls(engine_input *input, uint64_t controls, bool *firing)
{
    return engine_input_devices(input, controls, input->controllers, firing);
}

int engine_input_controller(engine_input *input, int controller, input_controller_state state, bool *firing)
{
    if (controller < 0 || controller >= INPUT_CONTROLLER_MAX) {
        return FLOW_ERR_ARGUMENT;
    }
    input_controller_state controllers[INPUT_CONTROLLER_MAX];
    memcpy(controllers, input->controllers, sizeof controllers);
    controllers[controller] = state;
    return engine_input_devices(input, input->controls, controllers, firing);
}

int engine_input_trail(const engine_input *input, const input_keyframe **keys)
{
    *keys = input->trail;
    return acount(input->trail);
}

input_keyframe engine_input_held(const engine_input *input)
{
    input_keyframe held = { .movement = input->movement,
                            .aim = input->aim,
                            .aim_point = input->aim_point,
                            .firing = input->firing,
                            .controls = input->controls,
                            .pointer = input->pointer };
    memcpy(held.controllers, input->controllers, sizeof held.controllers);
    return held;
}

int engine_input_hold(engine_input *input, const input_keyframe *key)
{
    bool firing;
    int result = engine_input_devices(input, key->controls, key->controllers, &firing);
    engine_input_movement(input, key->movement);
    engine_input_aim(input, key->aim, key->firing, key->aim_point);
    engine_input_pointer(input, key->pointer);
    return result;
}

static int apply_aim_sample(engine_input *input)
{
    if (!input->aim_sampled || !ecs_world_component(input->world, COMPONENT_Aim.id)) {
        return FLOW_OK;
    }
    ecs_component_id required[] = { COMPONENT_Input.id,
                                    COMPONENT_Aim.id,
                                    COMPONENT_Transform.id };
    ecs_query query;
    int result = ecs_query_begin(input->world, &query, required, 3);
    if (result != ECS_OK) {
        return result;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        Aim *aim = Aim_get(input->world, entity);
        aim->firing = input->firing;
        CF_V2 direction = input->aim;
        if (input->aim_point) {
            CF_V2 position = transform_world_2d(input->world, entity).position;
            direction.x -= position.x;
            direction.y -= position.y;
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
    ecs_component_id component = COMPONENT_Input.id;
    if (!ecs_world_component(input->world, component)) {
        return FLOW_OK;
    }
    ecs_query query;
    int result = ecs_query_begin(input->world, &query, &component, 1);
    if (result != ECS_OK) {
        return result;
    }
    CF_V2 direction = input->movement;
    pin_value value = { .type = PIN_STRUCT, .type_name = T_CF_V2.name, .ptr = &direction };
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        Input *member = Input_get(input->world, entity);
        bool active = direction.x != 0 || direction.y != 0;
        bool was_active = member->move.x != 0 || member->move.y != 0;
        member->move = direction;
        if (!active && !was_active) {
            continue;
        }
        result = dispatch(
            input,
            entity,
            host_event_name(HOST_EVENT_INPUT),
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
    ecs_component_id component = COMPONENT_Input.id;
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
        .type_name = T_CF_V2.name,
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
            host_event_name(HOST_EVENT_INPUT),
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
        Aim *aim = Aim_get(input->world, entity);
        if (aim) {
            aim->firing = false;
            aim->manual = false;
        }
        result = dispatch(
            input,
            entity,
            host_event_name(HOST_EVENT_INPUT),
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
    ecs_component_id component = COMPONENT_Input.id;
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
            if (!Input_has(input->world, entity)) {
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

int engine_input_apply(engine_input *input)
{
    input->message[0] = 0;
    aclear(input->trail);
    for (int i = 0; i < acount(input->buttons); i++) {
        input->buttons[i].stepped = input->buttons[i].started;
        input->buttons[i].started = false;
    }
    int result = apply_aim_sample(input);
    if (result >= 0) {
        result = apply_movement_sample(input);
    }
    if (result >= 0) {
        result = dispatch_actions(input);
    }
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

static const char *action_held(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    const char *text;
    const char *error = pin_text_cstring(ctx->value_storage, in[0], &text, NULL);
    if (error) {
        return error;
    }

    if (!text || !text[0]) {
        return "Action Held requires an action name";
    }
    const input_button *button = named_button(service, text);
    out[0].b = button && button->pressed;
    return NULL;
}

static const char *action_pressed(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    const char *text;
    const char *error = pin_text_cstring(ctx->value_storage, in[0], &text, NULL);
    if (error) {
        return error;
    }

    if (!text || !text[0]) {
        return "Action Pressed requires an action name";
    }
    const input_button *button = named_button(service, text);
    out[0].b = button && button->stepped;
    return NULL;
}

const node_host_call INPUT_HELD_CALL = { .service = INPUT_SERVICE,
                                         .context = action_held,
                                         .optional_service = true };
const node_host_call INPUT_PRESSED_CALL = { .service = INPUT_SERVICE,
                                            .context = action_pressed,
                                            .optional_service = true };
static const char *controller_connected(void *service, const pin_value *in, pin_value *out)
{
    int controller = in[0].i;
    if (controller < 0 || controller >= INPUT_CONTROLLER_MAX) {
        return "Controller Connected requires a controller slot from 0 to 3";
    }
    const engine_input *input = service;
    out[0].b = input && input->controllers[controller].connected;
    return NULL;
}
const node_host_call INPUT_CONTROLLER_CONNECTED_CALL = {
    .service = INPUT_SERVICE,
    .invoke = controller_connected,
    .optional_service = true,
};
static const char *pointer_ray(void *service, const pin_value *, pin_value *out)
{
    assert(out[1].ptr && out[2].ptr);
    const engine_input *input = service;
    input_pointer_ray pointer = input ? input->pointer : (input_pointer_ray){ 0 };
    out[0].b = pointer.active;
    *(CF_V3 *)out[1].ptr = pointer.origin;
    *(CF_V3 *)out[2].ptr = pointer.direction;
    return NULL;
}
const node_host_call INPUT_POINTER_RAY_CALL = {
    .service = INPUT_SERVICE,
    .invoke = pointer_ray,
    .optional_service = true,
    .reusable = true,
};
const node_access INPUT_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
