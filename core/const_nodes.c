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

static pin_spec all_types_outputs[] = {
    { "bool", PIN_BOOL, "bool" },         { "i8", PIN_I8, "int8_t" },
    { "u8", PIN_U8, "uint8_t" },          { "i16", PIN_I16, "int16_t" },
    { "u16", PIN_U16, "uint16_t" },       { "i32", PIN_I32, "int" },
    { "u32", PIN_U32, "unsigned" },       { "i64", PIN_I64, "int64_t" },
    { "u64", PIN_U64, "uint64_t" },       { "f32", PIN_F32, "float" },
    { "f64", PIN_F64, "double" },         { "str", PIN_STR, "const char *" },
    { "struct", PIN_STRUCT, "Vec3" },     { "enum", PIN_ENUM, "TestEnum" },
    { "flow", PIN_FLOW, NULL, PIN_EXEC },
};
node_desc NODE_all_types_literal = {
    .name = "all_types_literal",
    .input_count = 0,
    .output_count = 15,
    .outputs = all_types_outputs,
    .eval = node_eval_const,
    .pure = 1,
};

node_desc *const EXTRA_NODES[] = {
    &NODE_const_f32,
    &NODE_const_bool,
    &NODE_const_str,
    &NODE_all_types_literal,
};
const int EXTRA_NODE_COUNT = 4;
