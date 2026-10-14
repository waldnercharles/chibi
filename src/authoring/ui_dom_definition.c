#include "authoring/ui_dom_definition.h"

#include "execution/function.h"
#define DATA(N, P, T, C, V)                                                    \
    { .name = N,                                                               \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = P,                                                            \
      .type_name = T,                                                          \
      .container = C,                                                          \
      .has_default = true,                                                     \
      .default_value = V }
#define INT(N, V)                                                              \
    DATA(N, PIN_I32, "int", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_I32, .i = V }))
#define TEXT(N, V)                                                             \
    DATA(N, PIN_STR, "const char *", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_STR, .str = V }))
#define NUMBER(N)                                                              \
    DATA(N, PIN_F64, "double", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_F64 }))
#define EXEC(N)                                                                \
    { .name = N, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
#define STATUS                                                                 \
    DATA("Status", PIN_STRUCT, "UiDomStatus", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_STRUCT }))
static const native_parameter_definition QUERY_IN[] = { EXEC("Exec"),
                                                        INT("First", 0),
                                                        INT("Count", 64) };
static const native_parameter_definition QUERY_OUT[] = {
    EXEC("Then"),
    STATUS,
    DATA(
        "Rows",
        PIN_STRUCT,
        "UiDomRow",
        PIN_CONTAINER_ARRAY,
        ((pin_value){ .type = PIN_STRUCT, .container = PIN_CONTAINER_ARRAY })
    )
};
static const native_parameter_definition INSPECT_IN[] = { EXEC("Exec"),
                                                          TEXT("Key", ""),
                                                          TEXT("Mode", "computed"),
                                                          INT("First", 0),
                                                          INT("Count", 64) };
static const native_parameter_definition INSPECT_OUT[] = {
    EXEC("Then"),
    STATUS,
    DATA(
        "Properties",
        PIN_STRUCT,
        "UiDomProperty",
        PIN_CONTAINER_ARRAY,
        ((pin_value){ .type = PIN_STRUCT, .container = PIN_CONTAINER_ARRAY })
    ),
    DATA("Row", PIN_STRUCT, "UiDomRow", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_STRUCT }))
};
static const native_parameter_definition PICK_IN[] = { EXEC("Exec"),
                                                       NUMBER("X"),
                                                       NUMBER("Y"),
                                                       TEXT("Ignore", "") };
static const native_parameter_definition PICK_OUT[] = {
    EXEC("Then"),
    STATUS,
    DATA("Row", PIN_STRUCT, "UiDomRow", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_STRUCT }))
};
static const native_parameter_definition PSEUDO_IN[] = {
    EXEC("Exec"),
    TEXT("Key", ""),
    TEXT("Pseudo", "hover"),
    DATA("On", PIN_BOOL, "bool", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_BOOL }))
};
static const native_parameter_definition PSEUDO_OUT[] = { EXEC("Then"), STATUS };
static const native_parameter_definition CONTEXTS_IN[] = { EXEC("Exec") };
static const native_parameter_definition CONTEXTS_OUT[] = {
    EXEC("Then"),
    STATUS,
    DATA(
        "Rows",
        PIN_STRUCT,
        "UiDomContext",
        PIN_CONTAINER_ARRAY,
        ((pin_value){ .type = PIN_STRUCT, .container = PIN_CONTAINER_ARRAY })
    )
};
static const native_parameter_definition SELECT_IN[] = { EXEC("Exec"),
                                                         TEXT("Key", "application") };
static const native_parameter_definition UNIT_IN[] = { EXEC("Exec"),
                                                       TEXT("Key", ""),
                                                       TEXT("Unit", "px"),
                                                       TEXT("Axis", "x") };
static const native_parameter_definition UNIT_OUT[] = { EXEC("Then"),
                                                        STATUS,
                                                        NUMBER("Pixels") };
static const native_parameter_definition MEASURE_IN[] = {
    TEXT("Element", ""),
    TEXT("Text", ""),
    DATA("Size", PIN_F32, "float", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_F32, .f = 12 })),
    INT("Weight", 400),
    TEXT("Context", "application")
};
static const native_parameter_definition MEASURE_OUT[] = {
    DATA("Known", PIN_BOOL, "bool", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_BOOL })),
    DATA("Width", PIN_F32, "float", PIN_CONTAINER_SINGLE, ((pin_value){ .type = PIN_F32 }))
};
#define COUNT(A) ((int)(sizeof A / sizeof *A))
#define DEF(N, S, I, O, D)                                                     \
    { .canonical_name = N,                                                     \
      .source_name = S,                                                        \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .category = "UI/Inspection",                                             \
      .description = D,                                                        \
      .inputs = I,                                                             \
      .input_count = COUNT(I),                                                 \
      .outputs = O,                                                            \
      .output_count = COUNT(O),                                                \
      .completion_output = 0,                                                  \
      .effects = SCRIPT_EFFECT_HOST }
const native_capability_definition UI_DOM_DEFINITIONS[8] = {
    { .canonical_name = "Measure UI Text",
      .source_name = "measure_ui_text",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .category = "UI",
      .description = "Measures one line in a live element's computed font "
                     "family/style at Size dp and Weight before transforms. "
                     "Known=false and Width=0 until context, element and font "
                     "are available. Does not change inspection context or "
                     "layout.",
      .inputs = MEASURE_IN,
      .input_count = COUNT(MEASURE_IN),
      .outputs = MEASURE_OUT,
      .output_count = COUNT(MEASURE_OUT),
      .completion_output = -1,
      .pure = true },
    DEF("Query UI DOM", "query_ui_dom", QUERY_IN, QUERY_OUT, "Reads a bounded page of the host's existing live UI context. Keys are monotonic service weak identities, resolved afresh and invalidated on destruction. <=4096 nodes/depth64/256 rows. Geometry is context pixels. Revision is an in-process observed tree/box change token, not a reproducibility fingerprint."),
    DEF("Inspect UI DOM", "inspect_ui_dom", INSPECT_IN, INSPECT_OUT, "Copies attributes, locally declared properties with source origins, or computed properties for a live element key. Mode attributes/declared/computed. Complete=false identifies bounded omissions."),
    DEF("Pick UI DOM", "pick_ui_dom", PICK_IN, PICK_OUT, "Hit-tests context pixel coordinates and copies the current element description; missing or excluded elements fail explicitly."),
    DEF("Force UI DOM Pseudo", "force_ui_dom_pseudo", PSEUDO_IN, PSEUDO_OUT, "Sets debug hover/active/focus/checked pseudo state on the element currently at Key. Changes no source document; caller owns toggling and clearing state."),
    DEF("Query UI DOM Contexts", "query_ui_dom_contexts", CONTEXTS_IN, CONTEXTS_OUT, "Copies the sorted named live context registry, at most 16 rows. Registration never changes existing selection."),
    DEF("Select UI DOM Context", "select_ui_dom_context", SELECT_IN, PSEUDO_OUT, "Selects a registered borrowed context, restoring forced states and invalidating old element identities on a context change. Missing keys refuse without changing selection."),
    DEF("Resolve UI DOM Unit", "resolve_ui_dom_unit", UNIT_IN, UNIT_OUT, "Resolves one literal length unit to physical pixels against a live element. Units px/dp/em/rem/vw/vh/in/cm/mm/pt/pc/%. Axis x/y chooses the percentage containing-block base. Variables, expressions, nonpositive scales and retired keys refuse without conversion.")
};
static const char *unavailable(const behavior_function_context *, void *, const pin_value *, pin_value *)
{
    return "Live UI inspection requires a runtime host context";
}
static const node_host_call DECLARE_CALL = {
    .service = "ui_dom",
    .missing_service = "Live UI inspection requires a runtime host context",
    .context = unavailable
};
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
static const node_access READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
#define BIND(N)                                                                \
    {                                                                          \
        .canonical_name = N, .implementation = {                               \
            .call = &DECLARE_CALL,                                             \
            .access = &ACCESS                                                  \
        }                                                                      \
    }
static const native_definition_binding BINDINGS[] = {
    BIND("Query UI DOM"),
    BIND("Inspect UI DOM"),
    BIND("Pick UI DOM"),
    BIND("Force UI DOM Pseudo"),
    BIND("Query UI DOM Contexts"),
    BIND("Select UI DOM Context"),
    BIND("Resolve UI DOM Unit"),
    { .canonical_name = "Measure UI Text",
      .implementation = { .call = &DECLARE_CALL, .access = &READ_ACCESS } }
};
static const type_desc *const TYPES[] = { &UI_DOM_ROW_LAYOUT,
                                          &UI_DOM_PROPERTY_LAYOUT,
                                          &UI_DOM_STATUS_LAYOUT,
                                          &UI_DOM_CONTEXT_LAYOUT };
const native_definition_pack UI_DOM_DECLARATION_PACK = { .definitions = UI_DOM_DEFINITIONS,
                                                         .definition_count = 8,
                                                         .bindings = BINDINGS,
                                                         .binding_count = 8,
                                                         .layouts = TYPES,
                                                         .layout_count = 4 };
