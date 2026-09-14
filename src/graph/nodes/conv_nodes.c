/* graph/nodes/conv_nodes.c -- CAT-26 explicit conversion descriptors, registered
 * as EXTRA_NODES. Scalar conversions remain available to the graph editor;
 * struct conversions are carried by data edges and evaluated at the edge.
 */
#include "graph/nodes/conv_nodes.h"

#include "graph/model/pin_conv.h"
#include "graph/model/pin_fmt.h"
#include "graph/model/registry.h"
#include "support/arena.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
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

static pin_value conv_to_name(const pin_value *inputs, int count, arena *storage)
{
    assert(count == 1 && inputs[0].type == PIN_STR);
    const char *text = inputs[0].str ? inputs[0].str : "";
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(storage, size);
    memcpy(copy, text, size);
    return (pin_value){ .type = PIN_NAME, .type_name = "name", .str = copy };
}

static double real_value(const pin_value *value)
{
    assert(value->type == PIN_F32 || value->type == PIN_F64);
    return value->type == PIN_F32 ? value->f : value->d;
}

/* Test bounds before casting: converting an out-of-range real is undefined.
 * In particular, INT64_MAX rounds up to 2^63 when represented as double. */
static int64_t real_integer(double value, int64_t minimum, int64_t maximum)
{
    if (isnan(value)) {
        return 0;
    }
    if (value <= (double)minimum) {
        return minimum;
    }
    if (value >= (double)maximum) {
        return maximum;
    }
    return (int64_t)value;
}

static pin_value conv_to_byte(const pin_value *inputs, int, arena *)
{
    return (pin_value){ .type = PIN_U8,
                        .type_name = "byte",
                        .u8 = (uint8_t)real_integer(real_value(inputs), 0, UINT8_MAX) };
}

static pin_value conv_to_integer(const pin_value *inputs, int, arena *)
{
    return (pin_value){ .type = PIN_I32,
                        .type_name = "int",
                        .i = (int32_t)real_integer(real_value(inputs), INT32_MIN, INT32_MAX) };
}

static pin_value conv_to_i64(const pin_value *inputs, int, arena *)
{
    int64_t value = inputs->type == PIN_I32
                      ? inputs->i
                      : real_integer(real_value(inputs), INT64_MIN, INT64_MAX);
    return (pin_value){ .type = PIN_I64, .type_name = "int64", .i64 = value };
}

static pin_spec real_integer_inputs[] = {
    { "Value", PIN_F64, "double", PIN_DATA },
};
static pin_spec to_byte_outputs[] = {
    { "Byte", PIN_U8, "byte", PIN_DATA },
};
static pin_spec to_integer_outputs[] = {
    { "Integer", PIN_I32, "int", PIN_DATA },
};
node_desc NODE_To_Byte = {
    .name = "To Byte",
    .input_count = 1,
    .output_count = 1,
    .inputs = real_integer_inputs,
    .outputs = to_byte_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = conv_to_byte,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Conversion",
    .keywords = "convert cast truncate saturate byte uint8",
};
node_desc NODE_To_Integer = {
    .name = "To Integer",
    .input_count = 1,
    .output_count = 1,
    .inputs = real_integer_inputs,
    .outputs = to_integer_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = conv_to_integer,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Conversion",
    .keywords = "convert cast truncate saturate integer int32",
};

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

static pin_spec to_name_outputs[] = {
    { "Name", PIN_NAME, "name", PIN_DATA },
};
node_desc NODE_To_Name = {
    .name = "To Name",
    .input_count = 1,
    .output_count = 1,
    .inputs = to_string_inputs,
    .outputs = to_name_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = conv_to_name,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Conversion",
    .keywords = "convert string name prototype asset",
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
    { "Value", PIN_F64, "double", PIN_DATA },
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

static pin_value vector_splat(const pin_value *input, arena *storage, const char *type, int width)
{
    float value = 0;
    switch (input->type) {
        case PIN_F32: value = input->f; break;
        case PIN_F64: value = (float)input->d; break;
        case PIN_U8: value = (float)input->u8; break;
        case PIN_I32: value = (float)input->i; break;
        case PIN_I64: value = (float)input->i64; break;
        default: assert(false); break;
    }
    float *result = arena_alloc(storage, (size_t)width * sizeof *result);
    for (int i = 0; i < width; i++) {
        result[i] = value;
    }
    return (pin_value){ .type = PIN_STRUCT, .type_name = type, .ptr = result };
}

#define VECTOR_SPLAT(vector_type, width)                                                    \
    static pin_value splat_##vector_type(const pin_value *input, int count, arena *storage) \
    {                                                                                       \
        assert(count == 1);                                                                 \
        return vector_splat(input, storage, #vector_type, width);                           \
    }                                                                                       \
    static pin_spec splat_##vector_type##_out[] = {                                         \
        { .name = "Vector", .type = PIN_STRUCT, .type_name = #vector_type }                 \
    };                                                                                      \
    node_desc NODE_Splat_##vector_type = { .name = "Scalar to " #vector_type,               \
                                           .input_count = 1,                                \
                                           .inputs = real_integer_inputs,                   \
                                           .output_count = 1,                               \
                                           .outputs = splat_##vector_type##_out,            \
                                           .pure_kind = NODE_PURE_CALL,                     \
                                           .fn = splat_##vector_type,                       \
                                           .category = "Conversion",                        \
                                           .display_class = NODE_DISPLAY_PURE,              \
                                           .conversion_from = "double",                     \
                                           .conversion_to = #vector_type };
VECTOR_SPLAT(CF_V2, 2)
VECTOR_SPLAT(CF_V3, 3)
VECTOR_SPLAT(ShaderVec2, 2)
VECTOR_SPLAT(ShaderVec3, 3)
VECTOR_SPLAT(ShaderVec4, 4)
#undef VECTOR_SPLAT

static const node_desc *const vector_splats[] = {
    &NODE_Splat_CF_V2,      &NODE_Splat_CF_V3,      &NODE_Splat_ShaderVec2,
    &NODE_Splat_ShaderVec3, &NODE_Splat_ShaderVec4,
};

bool conversion_is_vector_splat(const node_desc *conversion)
{
    for (int i = 0; i < (int)(sizeof vector_splats / sizeof *vector_splats); i++) {
        if (conversion == vector_splats[i]) {
            return true;
        }
    }
    return false;
}

static bool splat_source(pin_type type)
{
    return type == PIN_F32 || type == PIN_F64 || type == PIN_U8 ||
           type == PIN_I32 || type == PIN_I64;
}

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

/* TYPE-7: explicit numeric conversions include saturating real-to-integer
 * truncation, integer-to-real, integer widening, and scalar-to-string.
 * TYPE-10 makes F32 and F64 one Real category, so an integer feeds either
 * width through To Real; the output is F64 and the boundary coerces it. */
const node_desc *conversion_for(pin_type from, pin_type to)
{
    if (from == PIN_STR && to == PIN_NAME) {
        return &NODE_To_Name;
    }
    if (from == PIN_F32 || from == PIN_F64) {
        switch (to) {
            case PIN_U8: return &NODE_To_Byte;
            case PIN_I32: return &NODE_To_Integer;
            case PIN_I64: return &NODE_To_Integer64;
            default: break;
        }
    }
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

static int conversion_scalar_pins(const pin_spec *from, const pin_spec *to)
{
    return from && to && from->kind == PIN_DATA && to->kind == PIN_DATA &&
           from->container == PIN_CONTAINER_SINGLE &&
           to->container == PIN_CONTAINER_SINGLE;
}

int conversion_matches_pins(const pin_spec *from, const pin_spec *to, const node_desc *conversion)
{
    if (!conversion_scalar_pins(from, to) || !conversion ||
        conversion->input_count != 1 || conversion->output_count != 1 ||
        conversion->pure_kind != NODE_PURE_CALL || !conversion->fn) {
        return 0;
    }
    if (conversion_is_vector_splat(conversion)) {
        return splat_source(from->type) &&
               pin_compatible(&conversion->outputs[0], to);
    }
    if (conversion == conversion_for(from->type, to->type)) {
        return 1;
    }
    if (!conversion->conversion_from || !conversion->conversion_to ||
        !conversion_scalar_pins(&conversion->inputs[0], &conversion->outputs[0]) ||
        !pin_compatible(from, &conversion->inputs[0]) ||
        !pin_compatible(&conversion->outputs[0], to)) {
        return 0;
    }
    for (int i = 0; i < registry_node_count(); i++) {
        if (registry_node(i) == conversion) {
            return 1;
        }
    }
    return 0;
}

const node_desc *conversion_for_pins(const pin_spec *from, const pin_spec *to)
{
    if (!conversion_scalar_pins(from, to)) {
        return NULL;
    }
    const node_desc *builtin = conversion_for(from->type, to->type);
    if (builtin) {
        return builtin;
    }
    for (int i = 0; i < registry_node_count(); i++) {
        const node_desc *conversion = registry_node(i);
        if (conversion_matches_pins(from, to, conversion)) {
            return conversion;
        }
    }
    return NULL;
}
