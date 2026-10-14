#include "authoring/resource_previews_definition.h"

#include "execution/function.h"
#define P(N, T, S)                                                             \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = T, .type_name = S }
#define E(N) { .name = N, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
#define I(N) P(N, PIN_I32, "int")
#define S(N) P(N, PIN_STR, "const char *")
#define B(N) P(N, PIN_BOOL, "bool")
static const native_parameter_definition OPEN_IN[] = { E("Exec"),
                                                       S("Path"),
                                                       I("Width"),
                                                       I("Height") };
static const native_parameter_definition OPEN_OUT[] = { E("Then"),
                                                        I("Preview"),
                                                        S("Error") };
static const native_parameter_definition HANDLE_IN[] = { E("Exec"), I("Preview") };
static const native_parameter_definition READ_OUT[] = { E("Then"),
                                                        B("Ready"),
                                                        S("Status"),
                                                        S("Image"),
                                                        S("Error") };
static const native_parameter_definition CLOSE_OUT[] = { E("Then"), B("Closed") };
#define D(N, SRC, IN, OUT)                                                     \
    { .canonical_name = N,                                                     \
      .source_name = SRC,                                                      \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = IN,                                                            \
      .input_count = NATIVE_COUNT(IN),                                         \
      .outputs = OUT,                                                          \
      .output_count = NATIVE_COUNT(OUT),                                       \
      .completion_output = 0 }
const native_capability_definition RESOURCE_PREVIEW_DEFINITIONS[3] = {
    D("Open Resource Preview", "open_resource_preview", OPEN_IN, OPEN_OUT),
    D("Read Resource Preview", "read_resource_preview", HANDLE_IN, READ_OUT),
    D("Close Resource Preview", "close_resource_preview", HANDLE_IN, CLOSE_OUT)
};
static const char *unavailable(const behavior_function_context *, void *, const pin_value *, pin_value *)
{
    return "Resource previews require a graphics host";
}
static const node_host_call UNAVAILABLE = {
    .service = "resource_previews",
    .optional_service = true,
    .missing_service = "Resource previews require a graphics host",
    .context = unavailable
};
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Open Resource Preview",
      .implementation = { .call = &UNAVAILABLE, .access = &ACCESS } },
    { .canonical_name = "Read Resource Preview",
      .implementation = { .call = &UNAVAILABLE, .access = &ACCESS } },
    { .canonical_name = "Close Resource Preview",
      .implementation = { .call = &UNAVAILABLE, .access = &ACCESS } }
};
const native_definition_pack RESOURCE_PREVIEWS_DECLARATION_PACK = {
    .definitions = RESOURCE_PREVIEW_DEFINITIONS,
    .definition_count = 3,
    .bindings = BINDINGS,
    .binding_count = 3
};
