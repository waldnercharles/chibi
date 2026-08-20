/* core/const_nodes.c -- hand-written constant nodes, registered as EXTRA_NODES.
 *
 * A constant node has no inputs and one pre-set output; node_eval_const is a
 * no-op, so the caller (or the serializer) sets output[0] directly. Typed
 * const nodes keep the pin_spec concrete so connections type-check normally.
 */
#include "node.h"

static pin_spec const_f32_out[] = {
    { "value", PIN_F32, "float" },
};
node_desc NODE_const_f32 = {
    .name = "const_f32",
    .input_count = 0,
    .output_count = 1,
    .inputs = NULL,
    .outputs = const_f32_out,
    .pure_kind = NODE_PURE_CONST,
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
    .pure_kind = NODE_PURE_CONST,
};

static pin_spec const_byte_out[] = {
    { "value", PIN_U8, "byte" },
};
node_desc NODE_const_byte = {
    .name = "const_byte",
    .output_count = 1,
    .outputs = const_byte_out,
    .pure_kind = NODE_PURE_CONST,
};

static pin_spec const_int_out[] = {
    { "value", PIN_I32, "int" },
};
node_desc NODE_const_int = {
    .name = "const_int",
    .output_count = 1,
    .outputs = const_int_out,
    .pure_kind = NODE_PURE_CONST,
};

static pin_spec const_int64_out[] = {
    { "value", PIN_I64, "int64" },
};
node_desc NODE_const_int64 = {
    .name = "const_int64",
    .output_count = 1,
    .outputs = const_int64_out,
    .pure_kind = NODE_PURE_CONST,
};

static pin_spec const_real_out[] = {
    { "value", PIN_F64, "double" },
};
node_desc NODE_const_real = {
    .name = "const_real",
    .output_count = 1,
    .outputs = const_real_out,
    .pure_kind = NODE_PURE_CONST,
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
    .pure_kind = NODE_PURE_CONST,
};

static pin_spec all_types_outputs[] = {
    { "bool", PIN_BOOL, "bool" },
    { "i8", PIN_I8, "int8" },
    { "u8", PIN_U8, "byte" },
    { "i16", PIN_I16, "int16" },
    { "u16", PIN_U16, "uint16" },
    { "i32", PIN_I32, "int" },
    { "u32", PIN_U32, "uint" },
    { "i64", PIN_I64, "int64" },
    { "u64", PIN_U64, "uint64" },
    { "f32", PIN_F32, "float" },
    { "f64", PIN_F64, "double" },
    { "str", PIN_STR, "const char *" },
    { "struct", PIN_STRUCT, "Vec3" },
    { "enum", PIN_ENUM, "TestEnum" },
    { "flow", PIN_FLOW, NULL, PIN_EXEC },
};
node_desc NODE_all_types_literal = {
    .name = "all_types_literal",
    .input_count = 0,
    .output_count = 15,
    .outputs = all_types_outputs,
    .pure_kind = NODE_PURE_CONST,
};

node_desc *const EXTRA_NODES[] = {
    &NODE_const_f32, &NODE_const_bool,        &NODE_const_byte,
    &NODE_const_int, &NODE_const_int64,       &NODE_const_real,
    &NODE_const_str, &NODE_all_types_literal,
};
const int EXTRA_NODE_COUNT = 8;
