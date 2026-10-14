#include "authoring/ui_source_preview_definition.h"

#include "execution/function.h"
#define DATA(N, T, S, V)                                                       \
    { .name = N,                                                               \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = T,                                                            \
      .type_name = S,                                                          \
      .container = PIN_CONTAINER_SINGLE,                                       \
      .has_default = true,                                                     \
      .default_value = V }
#define I(N, V)                                                                \
    DATA(N, PIN_I32, "int", ((pin_value){ .type = PIN_I32, .i = V }))
#define S(N, V)                                                                \
    DATA(N, PIN_STR, "const char *", ((pin_value){ .type = PIN_STR, .str = V }))
#define E(N) { .name = N, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
static const native_parameter_definition OPEN[] = {
    E("Exec"),
    I("Document", 0),
    I("Revision", 0),
    S("Asset", ""),
    I("Session", 0),
    DATA("Entity", PIN_U64, "uint64", ((pin_value){ .type = PIN_U64 }))
};
static const native_parameter_definition DRAW[] = {
    E("Exec"),
    I("Preview", 0),
    I("Revision", 0),
    I("Width", 640),
    I("Height", 360),
    DATA("Seconds", PIN_F64, "double", ((pin_value){ .type = PIN_F64 }))
};
static const native_parameter_definition CLOSE[] = { E("Exec"), I("Preview", 0) };
static const native_parameter_definition OUT[] = {
    E("Then"),
    DATA("Result", PIN_STRUCT, "UiSourcePreviewResult", ((pin_value){ .type = PIN_STRUCT }))
};
#define COUNT(A) ((int)(sizeof A / sizeof *A))
#define DEF(N, S, I, D)                                                        \
    { .canonical_name = N,                                                     \
      .source_name = S,                                                        \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .category = "UI/Source Preview",                                         \
      .description = D,                                                        \
      .inputs = I,                                                             \
      .input_count = COUNT(I),                                                 \
      .outputs = OUT,                                                          \
      .output_count = COUNT(OUT),                                              \
      .completion_output = 0,                                                  \
      .effects = SCRIPT_EFFECT_HOST }
const native_capability_definition UI_SOURCE_PREVIEW_DEFINITIONS[3] = {
    DEF("Open UI Source Preview", "open_ui_source_preview", OPEN, "Validates a revisioned rooted UI source snapshot, expanding unsaved widgets and styles without compiling behaviors. Keeps one retained surface; session0 uses an empty model, positive session borrows its Ui/save read models with event delivery disabled. Entity optionally binds self."),
    DEF("Draw UI Source Preview", "draw_ui_source_preview", DRAW, "Draws exact current source into a <=2048x2048 texture in the host graphics context. Stale/invalid input yields no texture. Context is registered for generic DOM inspection. Delta0..0.25 seconds."),
    DEF("Close UI Source Preview", "close_ui_source_preview", CLOSE, "Retires the source renderer, DOM weak identities and texture before borrowed model/resource lifetime ends.")
};
static const char *unavailable(const behavior_function_context *, void *, const pin_value *, pin_value *)
{
    return "UI source preview requires a graphics host";
}
static const node_host_call STUB = {
    .service = "ui_source_preview",
    .optional_service = true,
    .missing_service = "UI source preview requires a graphics host",
    .context = unavailable
};
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
#define B(N)                                                                   \
    {                                                                          \
        .canonical_name = N, .implementation = {                               \
            .call = &STUB,                                                     \
            .access = &ACCESS                                                  \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    B("Open UI Source Preview"),
    B("Draw UI Source Preview"),
    B("Close UI Source Preview")
};
static const type_desc *const TYPES[] = { &UI_SOURCE_PREVIEW_RESULT_LAYOUT };
const native_definition_pack UI_SOURCE_PREVIEW_DECLARATION_PACK = {
    .definitions = UI_SOURCE_PREVIEW_DEFINITIONS,
    .definition_count = 3,
    .bindings = BINDINGS,
    .binding_count = 3,
    .layouts = TYPES,
    .layout_count = 1
};
