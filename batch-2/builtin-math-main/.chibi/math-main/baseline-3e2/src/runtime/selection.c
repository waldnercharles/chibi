#include "runtime/selection.h"

#include "execution/function.h"
#include "types/array.h"
#include "types/text.h"

#include <assert.h>

pin_value native_select_value(const pin_value *inputs, int count, arena *)
{
    if (count < 2 || inputs[0].type != PIN_I32 || inputs[0].i < 0 ||
        inputs[0].i >= count - 1) {
        return (pin_value){ .type = PIN_FLOW };
    }
    return inputs[inputs[0].i + 1];
}

static const char *select_owned(arena *storage, const pin_value *inputs, int count, pin_value *result)
{
    pin_value value = native_select_value(inputs, count, storage);
    if (value.type == PIN_FLOW) {
        return "Select index is out of range";
    }
    return pin_value_snapshot_to_arena(storage, result, &value)
             ? "Cannot retain selected value"
             : NULL;
}

const node_host_call NATIVE_SELECT_CALL = { .value = select_owned };
const node_access NATIVE_SELECTION_ACCESS = { .flags = NODE_EFFECT_PURE };

int native_switch_index(int selection, int outcome_count)
{
    assert(outcome_count > 0);
    int cases = outcome_count - 1;
    return selection >= 0 && selection < cases ? selection : cases;
}

int native_switch_text(const char *selection, const uint64_t *outcomes, int outcome_count)
{
    assert(outcomes && outcome_count > 0);
    /* Hashes the same "outcome:Case:<value>" identity as an authored Case label,
     * without allocating a temporary concatenated name in each activation. */
    uint64_t id = script_catalog_id("outcome:Case", selection ? selection : "");
    for (int i = 0; i < outcome_count - 1; i++) {
        if (outcomes[i] == id) {
            return i;
        }
    }
    return outcome_count - 1;
}

static const char *switch_int(
    const struct behavior_function_context *,
    void *,
    const pin_value *inputs,
    int input_count,
    pin_value *,
    int result_count,
    const uint64_t *,
    int outcome_count,
    int *selected
)
{
    assert(input_count == 1 && result_count == 0 && outcome_count > 0);
    (void)input_count;
    (void)result_count;
    if (inputs[0].type != PIN_I32) {
        return "Switch selection is not int";
    }
    *selected = native_switch_index(inputs[0].i, outcome_count);
    return NULL;
}

static const char *switch_text(
    const struct behavior_function_context *ctx,
    void *,
    const pin_value *inputs,
    int input_count,
    pin_value *,
    int result_count,
    const uint64_t *outcomes,
    int outcome_count,
    int *selected
)
{
    assert(input_count == 1 && result_count == 0 && outcome_count > 0);
    (void)input_count;
    (void)result_count;
    if (inputs[0].type != PIN_STR && inputs[0].type != PIN_NAME) {
        return "Switch selection is not text";
    }
    const char *text;
    const char *error = pin_text_cstring(ctx->value_storage, inputs[0], &text, NULL);
    if (error) {
        return error;
    }
    *selected = native_switch_text(text, outcomes, outcome_count);
    return NULL;
}

const node_host_call NATIVE_SWITCH_INT_CALL = { .action = switch_int };
const node_host_call NATIVE_SWITCH_TEXT_CALL = { .action = switch_text };
