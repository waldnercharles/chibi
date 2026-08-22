/* core/conv_nodes.c -- CAT-26 explicit conversion nodes, registered as
 * EXTRA_NODES.
 *
 * TYPE-7 permits integer widening within one signedness, integer to float,
 * float to double, and any scalar to string, always through a visible
 * conversion node rather than silent coercion. Each conversion node's input
 * is retypeable (PIN-3): one node type serves every source type in its set,
 * and the editor retypes the input when it inserts the node (UI-22). The
 * output type is fixed by the node.
 */
#include "conv_nodes.h"

#include "arena.h"
#include "pin_fmt.h"

#include <string.h>

static pin_value conv_to_string(const pin_value *inputs, int input_count, arena *a)
{
    (void)input_count;
    pin_value r = { .type = PIN_STR, .type_name = "const char *", .str = "" };
    /* pin_value_to_string returns a static ring buffer: copy into the arena,
     * never hand the ring out. */
    const char *text = pin_value_to_string(&inputs[0]);
    size_t len = strlen(text);
    char *copy = arena_alloc(a, len + 1);
    if (!copy) {
        return r;
    }
    memcpy(copy, text, len + 1);
    r.str = copy;
    return r;
}

static pin_value conv_to_real(const pin_value *inputs, int input_count, arena *a)
{
    (void)input_count;
    (void)a;
    pin_value r = { .type = PIN_F64, .type_name = "double", .d = 0.0 };
    switch (inputs[0].type) {
        case PIN_U8: r.d = (double)inputs[0].u8; break;
        case PIN_I32: r.d = (double)inputs[0].i; break;
        case PIN_I64: r.d = (double)inputs[0].i64; break;
        default: break; /* input is retyped to one of these before use */
    }
    return r;
}

static pin_value conv_to_i64(const pin_value *inputs, int input_count, arena *a)
{
    (void)input_count;
    (void)a;
    pin_value r = { .type = PIN_I64, .type_name = "int64", .i64 = 0 };
    if (inputs[0].type == PIN_I32) {
        r.i64 = (int64_t)inputs[0].i;
    }
    return r;
}

static pin_spec to_string_inputs[] = {
    { "Value", PIN_STR, "const char *", PIN_DATA },
};
static pin_spec to_string_outputs[] = {
    { "String", PIN_STR, "const char *", PIN_DATA },
};
node_desc NODE_To_String = {
    .name = "To String",
    .input_count = 1,
    .output_count = 1,
    .inputs = to_string_inputs,
    .outputs = to_string_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = conv_to_string,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Conversion",
    .keywords = "convert cast string text str",
};

static pin_spec to_real_inputs[] = {
    { "Value", PIN_I32, "int", PIN_DATA },
};
static pin_spec to_real_outputs[] = {
    { "Real", PIN_F64, "double", PIN_DATA },
};
node_desc NODE_To_Real = {
    .name = "To Real",
    .input_count = 1,
    .output_count = 1,
    .inputs = to_real_inputs,
    .outputs = to_real_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = conv_to_real,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Conversion",
    .keywords = "convert cast real float double",
};

static pin_spec to_i64_inputs[] = {
    { "Value", PIN_I32, "int", PIN_DATA },
};
static pin_spec to_i64_outputs[] = {
    { "Integer64", PIN_I64, "int64", PIN_DATA },
};
node_desc NODE_To_Integer64 = {
    .name = "To Integer64",
    .input_count = 1,
    .output_count = 1,
    .inputs = to_i64_inputs,
    .outputs = to_i64_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = conv_to_i64,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Conversion",
    .keywords = "convert cast integer int64 widen",
};

/* Sources the To String node accepts. TYPE-7 says "any scalar to string";
 * this is the core scalar set that has a literal node (CAT-30), with F32 in
 * the Real category (TYPE-10). */
static int to_string_source(pin_type t)
{
    switch (t) {
        case PIN_BOOL:
        case PIN_U8:
        case PIN_I32:
        case PIN_I64:
        case PIN_F32:
        case PIN_F64:
        case PIN_NAME: return 1;
        default: return 0;
    }
}

/* TYPE-7: integer to float, integer widening within one signedness, and any
 * scalar to string. Real to Integer is lossy and deliberately absent.
 * TYPE-10 makes F32 and F64 one Real category, so an integer feeds either
 * width through To Real; the output is F64 and the boundary coerces it. */
const node_desc *conversion_for(pin_type from, pin_type to)
{
    int real_target = to == PIN_F32 || to == PIN_F64;
    if ((from == PIN_U8 || from == PIN_I32 || from == PIN_I64) && real_target) {
        return &NODE_To_Real;
    }
    if (from == PIN_I32 && to == PIN_I64) {
        return &NODE_To_Integer64;
    }
    if (to_string_source(from) && to == PIN_STR) {
        return &NODE_To_String;
    }
    return NULL;
}
