#include "graph/nodes/conv_nodes.h"

#include "graph/model/native_definition.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "runtime/conversions.h"

#define CONVERSION_To_String_KEYWORDS "convert cast string text str"
#define CONVERSION_To_String_DESCRIPTION                                       \
    "Formats a bool, number or name Value as text."
#define CONVERSION_To_Name_KEYWORDS "convert string name prototype asset"
#define CONVERSION_To_Name_DESCRIPTION "Converts the text in Value to a name."
#define CONVERSION_To_Real_KEYWORDS                                            \
    "convert cast real number float f64 double bool"
#define CONVERSION_To_Real_DESCRIPTION                                         \
    "Converts a byte, integer or bool Value to a real, where a "               \
    "bool gives 1 or 0; very large 64-bit values lose "                        \
    "precision."
#define CONVERSION_To_Byte_KEYWORDS                                            \
    "convert cast truncate saturate byte uint8 bool"
#define CONVERSION_To_Byte_DESCRIPTION                                         \
    "Truncates Value toward zero and clamps it to 0 to 255; NaN "              \
    "gives 0. A bool gives 1 or 0."
#define CONVERSION_To_Integer_KEYWORDS                                         \
    "convert cast truncate saturate integer int32 bool"
#define CONVERSION_To_Integer_DESCRIPTION                                      \
    "Truncates Value toward zero and clamps it to the 32-bit "                 \
    "range; NaN gives 0. A bool gives 1 or 0."
#define CONVERSION_To_Integer64_KEYWORDS "convert cast integer i64 widen bool"
#define CONVERSION_To_Integer64_DESCRIPTION                                    \
    "Widens a 32-bit integer, or truncates a real toward zero "                \
    "and clamps it to the 64-bit range. A bool gives 1 or 0."
#define COUNT(A) (sizeof(A) / sizeof *(A))
#define SCALAR(KEY, NAME, SOURCE, INPUT, INPUT_NAME, RESULT, OUTPUT, OUTPUT_NAME) \
    static pin_spec KEY##_inputs[] = {                                            \
        NATIVE_CONVERSION_PORT(GRAPH_NATIVE_PORT, "Value", INPUT, INPUT_NAME)     \
    };                                                                            \
    static pin_spec KEY##_outputs[] = {                                           \
        NATIVE_CONVERSION_PORT(GRAPH_NATIVE_PORT, RESULT, OUTPUT, OUTPUT_NAME)    \
    };                                                                            \
    node_desc NODE_##KEY = { .name = NAME,                                        \
                             .source_name = SOURCE,                               \
                             .input_count = COUNT(KEY##_inputs),                  \
                             .output_count = COUNT(KEY##_outputs),                \
                             .inputs = KEY##_inputs,                              \
                             .outputs = KEY##_outputs,                            \
                             .pure_kind = NODE_PURE_CALL,                         \
                             .fn = native_conversion_##KEY,                       \
                             .execution_call = &NATIVE_CONVERSION_##KEY##_CALL,   \
                             .display_class = NODE_DISPLAY_PURE,                  \
                             .category = "Conversion",                            \
                             .keywords = CONVERSION_##KEY##_KEYWORDS,             \
                             .description = CONVERSION_##KEY##_DESCRIPTION };
NATIVE_CONVERSION_SCALARS(SCALAR)
#undef SCALAR
#define VECTOR(TYPE, WIDTH, NAME, SOURCE, DESCRIPTION)                         \
    static pin_spec Splat_##TYPE##_inputs[] = {                                \
        NATIVE_CONVERSION_PORT(GRAPH_NATIVE_PORT, "Value", PIN_F64, "f64")     \
    };                                                                         \
    static pin_spec Splat_##TYPE##_outputs[] = {                               \
        NATIVE_CONVERSION_PORT(GRAPH_NATIVE_PORT, "Vector", PIN_STRUCT, #TYPE) \
    };                                                                         \
    node_desc NODE_Splat_##TYPE = {                                            \
        .name = NAME,                                                          \
        .source_name = SOURCE,                                                 \
        .input_count = COUNT(Splat_##TYPE##_inputs),                           \
        .output_count = COUNT(Splat_##TYPE##_outputs),                         \
        .inputs = Splat_##TYPE##_inputs,                                       \
        .outputs = Splat_##TYPE##_outputs,                                     \
        .pure_kind = NODE_PURE_CALL,                                           \
        .fn = native_conversion_Splat_##TYPE,                                  \
        .execution_call = &NATIVE_CONVERSION_Splat_##TYPE##_CALL,              \
        .category = "Conversion",                                              \
        .description = DESCRIPTION,                                            \
        .display_class = NODE_DISPLAY_PURE,                                    \
        .conversion_from = "f64",                                              \
        .conversion_to = #TYPE                                                 \
    };
NATIVE_CONVERSION_VECTORS(VECTOR)
#undef VECTOR

static const node_desc *const vector_splats[] = {
    &NODE_Splat_vec2,       &NODE_Splat_vec3,       &NODE_Splat_ShaderVec2,
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
 * truncation, integer-to-real, bool-to-number, integer widening, and
 * scalar-to-string.
 * TYPE-10 makes F32 and F64 one Real category, so an integer feeds either
 * width through To Real, whose output is a number. A behavior wire becomes
 * source, where an integer widens to f64 exactly; graphs that compile their
 * edges reject numeric conversions. */
const node_desc *conversion_for(pin_type from, pin_type to)
{
    if (from == PIN_STR && to == PIN_NAME) {
        return &NODE_To_Name;
    }
    /* A bool is 1 or 0 to any number. */
    if (from == PIN_BOOL || from == PIN_F32 || from == PIN_F64) {
        switch (to) {
            case PIN_U8: return &NODE_To_Byte;
            case PIN_I32: return &NODE_To_Integer;
            case PIN_I64: return &NODE_To_Integer64;
            default: break;
        }
    }
    int real_target = to == PIN_F32 || to == PIN_F64;
    if ((from == PIN_BOOL || from == PIN_U8 || from == PIN_I32 || from == PIN_I64) &&
        real_target) {
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
    /* Pins that connect as they are need none: Transform is an alias of
     * CF_V2, so its conversion would otherwise match a CF_V2 pair. */
    if (!conversion_scalar_pins(from, to) || pin_compatible(from, to)) {
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

int wire_fit(const pin_spec *from, const pin_spec *to)
{
    if (from->kind != to->kind || ((from->flags | to->flags) & PIN_FLAG_SETTING)) {
        return 0;
    }
    if (from->kind == PIN_EXEC) {
        return 3;
    }
    /* A wildcard keeps its flag once a connection types it. */
    bool open = ((from->flags & PIN_FLAG_WILDCARD) && from->type == PIN_FLOW) ||
                ((to->flags & PIN_FLAG_WILDCARD) && to->type == PIN_FLOW);
    if (open) {
        return 1;
    }
    if (pin_compatible(from, to)) {
        return 3;
    }
    return conversion_for_pins(from, to) ? 2 : 0;
}
