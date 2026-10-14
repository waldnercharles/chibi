#include "runtime/color_value_definition.h"

#include "execution/function.h"
#include "runtime/color_value.h"
static const char *to_hex(const behavior_function_context *ctx, void *, const pin_value *in, pin_value *out)
{
    float rgba[4] = { in[0].f, in[1].f, in[2].f, in[3].f };
    char text[10] = "", error[128] = "";
    out[0].b = color_value_to_hex(rgba, text, error, sizeof error);
    out[1].str = ctx->copy_string(ctx->value_storage, text);
    out[2].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *from_hex(const behavior_function_context *ctx, void *, const pin_value *in, pin_value *out)
{
    float rgba[4] = { 0 };
    char error[128] = "";
    out[0].b = color_value_from_hex(in[0].str, rgba, error, sizeof error);
    for (int i = 0; i < 4; i++) {
        out[i + 1].f = rgba[i];
    }
    out[5].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *packed_to_hex(
    const behavior_function_context *ctx,
    void *,
    const pin_value *in,
    pin_value *out
)
{
    char text[10] = "", error[128] = "";
    out[0].b = color_value_packed_to_hex(in[0].d, text, error, sizeof error);
    out[1].str = ctx->copy_string(ctx->value_storage, text);
    out[2].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *packed_from_hex(
    const behavior_function_context *ctx,
    void *,
    const pin_value *in,
    pin_value *out
)
{
    uint32_t packed = 0;
    char error[128] = "";
    out[0].b = color_value_packed_from_hex(in[0].str, &packed, error, sizeof error);
    out[1].u64 = packed;
    out[2].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
#define P(N, K, T)                                                             \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = K, .type_name = T }
#define F(N) P(N, PIN_F32, "float")
#define B(N) P(N, PIN_BOOL, "bool")
#define S(N) P(N, PIN_STR, "const char *")
static const native_parameter_definition TO_IN[] = { F("R"), F("G"), F("B"), F("A") };
static const native_parameter_definition TO_OUT[] = { B("Valid"), S("Text"), S("Error") };
static const native_parameter_definition FROM_IN[] = { S("Text") };
static const native_parameter_definition FROM_OUT[] = { B("Valid"), F("R"),
                                                        F("G"),     F("B"),
                                                        F("A"),     S("Error") };
static const native_parameter_definition PACKED_TO_IN[] = { P("Packed", PIN_F64, "double") };
static const native_parameter_definition PACKED_FROM_OUT[] = { B("Valid"),
                                                               P("Packed", PIN_U64, "uint64"),
                                                               S("Error") };
static const node_host_call PACKED_TO_CALL = { .context = packed_to_hex },
                            PACKED_FROM_CALL = { .context = packed_from_hex };
static const node_host_call TO_CALL = { .context = to_hex },
                            FROM_CALL = { .context = from_hex };
static const node_access ACCESS = { 0 };
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "Packed Color to Hex",
      .source_name = "packed_color_to_hex",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = PACKED_TO_IN,
      .input_count = NATIVE_COUNT(PACKED_TO_IN),
      .outputs = TO_OUT,
      .output_count = NATIVE_COUNT(TO_OUT),
      .completion_output = -1 },
    { .canonical_name = "Packed Color from Hex",
      .source_name = "packed_color_from_hex",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = FROM_IN,
      .input_count = NATIVE_COUNT(FROM_IN),
      .outputs = PACKED_FROM_OUT,
      .output_count = NATIVE_COUNT(PACKED_FROM_OUT),
      .completion_output = -1 },
    { .canonical_name = "Linear Color to Hex",
      .source_name = "linear_color_to_hex",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = TO_IN,
      .input_count = NATIVE_COUNT(TO_IN),
      .outputs = TO_OUT,
      .output_count = NATIVE_COUNT(TO_OUT),
      .completion_output = -1 },
    { .canonical_name = "Linear Color from Hex",
      .source_name = "linear_color_from_hex",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = FROM_IN,
      .input_count = NATIVE_COUNT(FROM_IN),
      .outputs = FROM_OUT,
      .output_count = NATIVE_COUNT(FROM_OUT),
      .completion_output = -1 }
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Packed Color to Hex",
      .implementation = { .call = &PACKED_TO_CALL, .access = &ACCESS } },
    { .canonical_name = "Packed Color from Hex",
      .implementation = { .call = &PACKED_FROM_CALL, .access = &ACCESS } },
    { .canonical_name = "Linear Color to Hex",
      .implementation = { .call = &TO_CALL, .access = &ACCESS } },
    { .canonical_name = "Linear Color from Hex",
      .implementation = { .call = &FROM_CALL, .access = &ACCESS } }
};
const native_definition_pack COLOR_VALUE_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
