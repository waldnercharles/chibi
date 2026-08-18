/* core/const_nodes.c -- hand-written constant nodes, registered as EXTRA_NODES.
 *
 * A constant node has no inputs and one pre-set output; node_eval_const is a
 * no-op, so the caller (or the serializer) sets output[0] directly. Typed
 * const nodes keep the pin_spec concrete so connections type-check normally.
 */
#include "nodes.h"

static pin_spec const_f32_out[] = {
    { "value", PIN_F32, "float" },
};
node_desc NODE_const_f32 = {
    .name = "const_f32",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = const_f32_out,
    .eval = node_eval_const,
};

static pin_spec const_bool_out[] = {
    { "value", PIN_BOOL, "bool" },
};
node_desc NODE_const_bool = {
    .name = "const_bool",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = const_bool_out,
    .eval = node_eval_const,
};

static pin_spec const_str_out[] = {
    { "value", PIN_STR, "const char *" },
};
node_desc NODE_const_str = {
    .name = "const_str",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = const_str_out,
    .eval = node_eval_const,
};

node_desc *const EXTRA_NODES[] = {
    &NODE_const_f32,
    &NODE_const_bool,
    &NODE_const_str,
};
const int EXTRA_NODE_COUNT = 3;
