#ifndef CHIBI_BEHAVIOR_STATE_H
#define CHIBI_BEHAVIOR_STATE_H

#include "types/state_machine.h"
#include "types/value.h"

enum
{
    NATIVE_STATE_SUPPRESSED = -1,
    NATIVE_STATE_INVALID = -2,
};

/* FLOW means uninitialized. Initialized rows are BOOL (Once/Flip/Gate),
 * I32 (Count), or STR (Multi). Multi masks belong to the instance and hold
 * one '0'/'1' byte per outcome, followed by a terminator. */
int native_state_input_count(native_state_kind);
int native_state_input_order(native_state_kind, int ordinal);
/* A DATA-ordinal demand mask, or INVALID. Evaluate only demanded inputs in
 * input_order. Reset of Once/Count/Multi does not evaluate authored inputs. */
int native_state_inputs(native_state_kind, int entry, const pin_value *state);
/* Inputs use the declaration's DATA ordinals; result is Counter/Is A for
 * Count/Flip. Returns a selected EXEC ordinal, SUPPRESSED, or INVALID.
 * Multi's owning adapter allocates its mask before a non-reset transition.
 * Retrigger is scheduled by the timer owner and is not a scalar transition. */
int native_state_transition(
    native_state_kind,
    int entry,
    pin_value *state,
    const pin_value *inputs,
    int outcomes,
    pin_value *result
);

typedef struct
{
    int index, outputs;
} behavior_state_selection;
enum
{
    BEHAVIOR_STATE_RESET = 1,
    BEHAVIOR_STATE_LOOP = 2,
    BEHAVIOR_STATE_RANDOM = 4
};
/* The caller owns a mask of outputs bytes containing '0' or '1'. Reset also
 * accepts a missing mask. Selection never allocates or changes ECS storage. */
int behavior_state_select(char *mask, int outputs, int start, int flags);
#endif
