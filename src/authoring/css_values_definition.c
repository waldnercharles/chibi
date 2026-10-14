#include "authoring/css_values_definition.h"
#define P(N, T, S)                                                             \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = T, .type_name = S }
#define B(N) P(N, PIN_BOOL, "bool")
#define S(N) P(N, PIN_STR, "const char *")
#define D(N) P(N, PIN_F64, "double")
#define F(N) P(N, PIN_F32, "float")
static const native_parameter_definition QUERY_IN[] = { S("Property"), S("Value") };
static const native_parameter_definition QUERY_OUT[] = {
    B("Ok"),
    B("Known"),
    B("Simple"),
    S("Kind"),
    D("Number"),
    S("Unit"),
    S("Default"),
    S("Original"),
    B("Inherited"),
    B("Shorthand"),
    B("Length"),
    B("Unitless"),
    B("Percent"),
    B("Color"),
    B("Integer"),
    { .name = "Options",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STR,
      .type_name = "const char *",
      .container = PIN_CONTAINER_ARRAY },
    F("R"),
    F("G"),
    F("B"),
    F("A"),
    S("Error")
};
static const native_parameter_definition FORMAT_IN[] = {
    S("Property"), S("Current"), S("Operation"), D("Number"), S("Unit"),
    S("Keyword"),  F("R"),       F("G"),         F("B"),      F("A")
};
static const native_parameter_definition FORMAT_OUT[] = { B("Ok"), S("Text"), S("Error") };
#define DEF(N, S, I, O, D)                                                     \
    { .canonical_name = N,                                                     \
      .source_name = S,                                                        \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .pure = true,                                                            \
      .category = "UI/CSS",                                                    \
      .description = D,                                                        \
      .inputs = I,                                                             \
      .input_count = NATIVE_COUNT(I),                                          \
      .outputs = O,                                                            \
      .output_count = NATIVE_COUNT(O),                                         \
      .completion_output = -1 }
static const native_capability_definition DEFINITIONS[] = {
    DEF("CSS Query Value", "query_css_value", QUERY_IN, QUERY_OUT, "Bounded CPU RCSS scalar metadata and parsed simple literal. Original remains exact; unknown, shorthand and complex values return Simple=false. Options mirror pinned RCSS keywords. Color channels are linear RGB/straight alpha; hex is sRGB. <=127-byte property and4096-byte literal."),
    DEF("CSS Format Value", "format_css_value", FORMAT_IN, FORMAT_OUT, "Fixed operation number/color/keyword validated against a known simple current property value. Preserves numeric unit exactly (empty Unit preserves); never evaluates or converts relative units, percentages, var/calc or shorthands. Complex values require source editing. Color channels are linear RGB/straight alpha.")
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "CSS Query Value",
      .implementation = { .call = &CSS_VALUES_QUERY_CALL, .access = &CSS_VALUES_ACCESS } },
    { .canonical_name = "CSS Format Value",
      .implementation = { .call = &CSS_VALUES_FORMAT_CALL, .access = &CSS_VALUES_ACCESS } }
};
const native_definition_pack CSS_VALUES_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS)
};
