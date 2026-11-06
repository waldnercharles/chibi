#include "execution/state.h"

#include "types/text.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

int behavior_state_select(char *used, int count, int start, int flags)
{
    assert(count >= 0);
    if (flags & BEHAVIOR_STATE_RESET) {
        if (used) {
            memset(used, '0', (size_t)count);
        }
        return -1;
    }
    if (!count) {
        return -1;
    }
    assert(used);
    int remaining = 0;
    for (int i = 0; i < count; i++) {
        remaining += used[i] == '0';
    }
    if (!remaining && !(flags & BEHAVIOR_STATE_LOOP)) {
        return -1;
    }
    if (!remaining) {
        memset(used, '0', (size_t)count);
        remaining = count;
    }
    bool valid = start >= 0 && start < count;
    int output = valid ? start : 0;
    int choice = flags & BEHAVIOR_STATE_RANDOM
                   ? (int)((double)rand() / ((double)RAND_MAX + 1) * remaining)
                   : 0;
    /* Preserve the random draw even when the first output is forced. */
    if (remaining == count && valid) {
        choice = 0;
    }
    for (int i = 0; i < count; i++) {
        if (used[output] == '0' && choice-- == 0) {
            used[output] = '1';
            return output;
        }
        if (++output == count) {
            output = 0;
        }
    }
    assert(false);
    return -1;
}

static bool entry_valid(native_state_kind kind, int entry)
{
    int count = kind == NATIVE_STATE_GATE ? 4
              : kind == NATIVE_STATE_ONCE || kind == NATIVE_STATE_COUNT ||
                        kind == NATIVE_STATE_MULTI
                  ? 2
                  : 1;
    return kind > NATIVE_STATE_NONE && kind <= NATIVE_STATE_RETRIGGER &&
           entry >= 0 && entry < count;
}
static bool state_valid(native_state_kind kind, const pin_value *state)
{
    if (!state || state->container != PIN_CONTAINER_SINGLE) {
        return false;
    }
    pin_type type = kind == NATIVE_STATE_COUNT     ? PIN_I32
                  : kind == NATIVE_STATE_MULTI     ? PIN_STR
                  : kind == NATIVE_STATE_RETRIGGER ? PIN_FLOW
                                                   : PIN_BOOL;
    return state->type == PIN_FLOW || state->type == type;
}
int native_state_input_count(native_state_kind kind)
{
    if (kind <= NATIVE_STATE_NONE || kind > NATIVE_STATE_RETRIGGER) {
        return NATIVE_STATE_INVALID;
    }
    return kind == NATIVE_STATE_FLIP ? 0 : kind == NATIVE_STATE_MULTI ? 3 : 1;
}
int native_state_input_order(native_state_kind kind, int ordinal)
{
    int count = native_state_input_count(kind);
    if (ordinal < 0 || ordinal >= count) {
        return NATIVE_STATE_INVALID;
    }
    return kind == NATIVE_STATE_MULTI ? 2 - ordinal : ordinal;
}
int native_state_inputs(native_state_kind kind, int entry, const pin_value *state)
{
    if (!entry_valid(kind, entry) || !state_valid(kind, state)) {
        return NATIVE_STATE_INVALID;
    }
    switch (kind) {
        case NATIVE_STATE_ONCE: return entry || state->type != PIN_FLOW ? 0 : 1;
        case NATIVE_STATE_COUNT: return entry ? 0 : 1;
        case NATIVE_STATE_FLIP: return 0;
        case NATIVE_STATE_GATE: return state->type == PIN_BOOL ? 0 : 1;
        case NATIVE_STATE_MULTI: return entry ? 0 : 7;
        case NATIVE_STATE_RETRIGGER: return 1;
        default: return NATIVE_STATE_INVALID;
    }
}
static bool input(const pin_value *inputs, int index, pin_type type)
{
    return inputs && inputs[index].container == PIN_CONTAINER_SINGLE &&
           inputs[index].type == type;
}
static void boolean(pin_value *state, bool value)
{
    *state = (pin_value){ .type = PIN_BOOL, .b = value };
}
int native_state_transition(
    native_state_kind kind,
    int entry,
    pin_value *state,
    const pin_value *inputs,
    int outcomes,
    pin_value *result
)
{
    int demand = native_state_inputs(kind, entry, state);
    if (demand < 0 || kind == NATIVE_STATE_RETRIGGER ||
        (kind == NATIVE_STATE_FLIP    ? outcomes != 2
         : kind == NATIVE_STATE_MULTI ? outcomes < 1
                                      : outcomes != 1)) {
        return NATIVE_STATE_INVALID;
    }
    if ((demand & 1) &&
        !input(inputs, 0, kind == NATIVE_STATE_COUNT ? PIN_I32 : PIN_BOOL)) {
        return NATIVE_STATE_INVALID;
    }
    if (kind == NATIVE_STATE_MULTI && demand &&
        (!input(inputs, 1, PIN_BOOL) || !input(inputs, 2, PIN_I32))) {
        return NATIVE_STATE_INVALID;
    }
    switch (kind) {
        case NATIVE_STATE_ONCE:
            if (entry) {
                boolean(state, false);
                return NATIVE_STATE_SUPPRESSED;
            }
            if (state->type == PIN_FLOW) {
                boolean(state, inputs[0].b);
            }
            if (state->b) {
                return NATIVE_STATE_SUPPRESSED;
            }
            state->b = true;
            return 0;
        case NATIVE_STATE_COUNT:
            if (!result && !entry) {
                return NATIVE_STATE_INVALID;
            }
            if (entry || state->type == PIN_FLOW) {
                *state = (pin_value){ .type = PIN_I32 };
            }
            if (entry) {
                return NATIVE_STATE_SUPPRESSED;
            }
            *result = (pin_value){ .type = PIN_I32,
                                   .type_name = "int",
                                   .i = state->i };
            if (state->i >= inputs[0].i) {
                return NATIVE_STATE_SUPPRESSED;
            }
            state->i++;
            return 0;
        case NATIVE_STATE_FLIP:
            if (!result) {
                return NATIVE_STATE_INVALID;
            }
            boolean(state, state->type == PIN_FLOW || !state->b);
            *result = *state;
            return state->b ? 0 : 1;
        case NATIVE_STATE_GATE:
            if (state->type == PIN_FLOW) {
                boolean(state, !inputs[0].b);
            }
            if (entry) {
                state->b = entry == 3 ? !state->b : entry == 1;
                return NATIVE_STATE_SUPPRESSED;
            }
            return state->b ? 0 : NATIVE_STATE_SUPPRESSED;
        case NATIVE_STATE_MULTI:
            if (state->type == PIN_FLOW && entry) {
                return NATIVE_STATE_SUPPRESSED;
            }
            if (state->type != PIN_STR || pin_text(*state).length != (uint32_t)outcomes) {
                return NATIVE_STATE_INVALID;
            }
            for (int i = 0; i < outcomes; i++) {
                if (pin_text(*state).bytes[i] != '0' &&
                    pin_text(*state).bytes[i] != '1') {
                    return NATIVE_STATE_INVALID;
                }
            }
            return behavior_state_select(
                (char *)pin_text(*state).bytes,
                outcomes,
                entry ? 0 : inputs[2].i,
                entry ? BEHAVIOR_STATE_RESET
                      : (inputs[0].b ? BEHAVIOR_STATE_LOOP : 0) |
                            (inputs[1].b ? BEHAVIOR_STATE_RANDOM : 0)
            );
        default: return NATIVE_STATE_INVALID;
    }
}
