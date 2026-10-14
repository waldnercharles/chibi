#include "authoring/visual_authoring_preview_definition.h"

#include "execution/function.h"

#include <stddef.h>
#define FIELD(NAME, KIND, TYPE)                                                \
    { .name = #NAME,                                                           \
      .type = KIND,                                                            \
      .type_name = TYPE,                                                       \
      .offset = offsetof(visual_publication_row, NAME),                        \
      .size = sizeof(((visual_publication_row *)0)->NAME) }
static const type_field PUBLICATION_FIELDS[] = { FIELD(document, PIN_I32, "int"),
                                                 FIELD(revision, PIN_I32, "int"),
                                                 FIELD(valid, PIN_BOOL, "bool"),
                                                 FIELD(error, PIN_STR, "const char *") };
const type_desc VISUAL_PUBLICATION_LAYOUT = {
    .name = "VisualPublication",
    .size = sizeof(visual_publication_row),
    .fields = PUBLICATION_FIELDS,
    .field_count = NATIVE_COUNT(PUBLICATION_FIELDS)
};
const native_type_definition VISUAL_PREVIEW_TYPES[1] = {
    { .layout = &VISUAL_PUBLICATION_LAYOUT, .storage = PIN_STRUCT }
};
#define P(NAME, KIND, TYPE)                                                    \
    { .name = NAME, .kind = NATIVE_PARAMETER_DATA, .storage = KIND, .type_name = TYPE }
#define E(NAME)                                                                \
    { .name = NAME, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
#define I(NAME) P(NAME, PIN_I32, "int")
#define F(NAME) P(NAME, PIN_F32, "float")
#define S(NAME) P(NAME, PIN_STR, "const char *")
#define B(NAME) P(NAME, PIN_BOOL, "bool")
static const native_parameter_definition OPEN_IN[] = { E("Exec"),
                                                       I("Document"),
                                                       I("Width"),
                                                       I("Height") };
static const native_parameter_definition OPEN_OUT[] = { E("Then"),
                                                        I("Preview"),
                                                        S("Error") };
static const native_parameter_definition DRAW_IN[] = {
    E("Exec"), I("Preview"), I("Revision"), F("Time"),
    F("Yaw"),  F("Pitch"),   F("Zoom")
};
static const native_parameter_definition DRAW_OUT[] = { E("Then"),
                                                        B("Valid"),
                                                        S("Image"),
                                                        S("Error") };
static const native_parameter_definition CLOSE_IN[] = { E("Exec"), I("Preview") };
static const native_parameter_definition CLOSE_OUT[] = { E("Then"), B("Valid") };
static const native_parameter_definition EXPORT_IN[] = { E("Exec"),
                                                         I("Preview"),
                                                         S("Destination") };
static const native_parameter_definition EXPORT_OUT[] = { E("Then"), I("Job"), S("Error") };
static const native_parameter_definition JOB_IN[] = { E("Exec"), I("Job") };
static const native_parameter_definition JOB_OUT[] = { E("Then"),
                                                       I("State"),
                                                       S("Destination"),
                                                       S("Error") };
#define D(NAME, SOURCE, INPUT, OUTPUT)                                         \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = INPUT,                                                         \
      .input_count = NATIVE_COUNT(INPUT),                                      \
      .outputs = OUTPUT,                                                       \
      .output_count = NATIVE_COUNT(OUTPUT),                                    \
      .completion_output = 0 }
static const native_parameter_definition REFRESH_IN[] = { E("Exec") };
static const native_parameter_definition PUBLISH_IN[] = { E("Exec"),
                                                          I("Document"),
                                                          I("Revision") };
static const native_parameter_definition PUBLISH_OUT[] = { E("Then"),
                                                           B("Valid"),
                                                           S("Error") };
static const native_parameter_definition BULK_IN[] = {
    E("Exec"),
    { .name = "Documents",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_I32,
      .type_name = "int",
      .container = PIN_CONTAINER_ARRAY }
};
static const native_parameter_definition BULK_OUT[] = {
    E("Then"),
    B("Valid"),
    I("Published"),
    I("Pending"),
    { .name = "Results",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STRUCT,
      .type_name = "VisualPublication",
      .container = PIN_CONTAINER_ARRAY },
    S("Error")
};
static const native_parameter_definition MODEL_IN[] = {
    E("Exec"), I("Preview"),     I("Revision"),
    I("Part"), I("SectionAxis"), F("Offset")
};
static const native_parameter_definition MODEL_REFERENCE_IN[] = {
    E("Exec"),     I("Preview"),
    I("Revision"), P("Reference", PIN_STRUCT, "ModelReference"),
    B("Visible"),  F("Opacity")
};
static const native_parameter_definition MODEL_PAN_IN[] = { E("Exec"),
                                                            I("Preview"),
                                                            I("Revision"),
                                                            F("X"),
                                                            F("Y") };
const native_capability_definition VISUAL_PREVIEW_DEFINITIONS[VISUAL_PREVIEW_OPERATION_COUNT] = {
    D("Set Model Preview Reference", "set_model_preview_reference", MODEL_REFERENCE_IN, PUBLISH_OUT),
    D("Set Model Preview Pan", "set_model_preview_pan", MODEL_PAN_IN, PUBLISH_OUT),
    D("Set Visual Model Inspection", "set_visual_model_inspection", MODEL_IN, PUBLISH_OUT),
    D("Refresh Visual Catalog", "refresh_visual_catalog", REFRESH_IN, PUBLISH_OUT),
    D("Publish Visual Documents", "publish_visual_documents", BULK_IN, BULK_OUT),
    D("Publish Visual Document", "publish_visual_document", PUBLISH_IN, PUBLISH_OUT),
    D("Open Visual Preview", "open_visual_preview", OPEN_IN, OPEN_OUT),
    D("Draw Visual Preview", "draw_visual_preview", DRAW_IN, DRAW_OUT),
    D("Close Visual Preview", "close_visual_preview", CLOSE_IN, CLOSE_OUT),
    D("Export Visual Evidence", "export_visual_evidence", EXPORT_IN, EXPORT_OUT),
    D("Visual Evidence Status", "visual_evidence_status", JOB_IN, JOB_OUT),
    D("Close Visual Evidence", "close_visual_evidence", JOB_IN, CLOSE_OUT),
};
static const type_desc *const LAYOUTS[] = { &VISUAL_PUBLICATION_LAYOUT };
/* Offline source lowering needs the execution ABI as well as the signatures.
 * Prepared modules rebind these identities to the selected runtime pack; this
 * CPU-only contract deliberately cannot perform a graphics operation. */
static const char *requires_gpu(const behavior_function_context *, void *, const pin_value *, pin_value *)
{
    return "Visual previews require a GPU authoring host";
}
static const node_host_call PREVIEW_CONTRACT = {
    .service = "visual.previews",
    .optional_service = true,
    .missing_service = "Visual previews require a GPU authoring host",
    .context = requires_gpu
};
static const node_access PREVIEW_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
#undef B
#define CONTRACT(NAME)                                                         \
    {                                                                          \
        .canonical_name = NAME, .implementation = {                            \
            .call = &PREVIEW_CONTRACT,                                         \
            .access = &PREVIEW_ACCESS                                          \
        }                                                                      \
    }
static const native_definition_binding CONTRACTS[] = {
    CONTRACT("Set Model Preview Reference"),
    CONTRACT("Set Model Preview Pan"),
    CONTRACT("Set Visual Model Inspection"),
    CONTRACT("Refresh Visual Catalog"),
    CONTRACT("Publish Visual Documents"),
    CONTRACT("Publish Visual Document"),
    CONTRACT("Open Visual Preview"),
    CONTRACT("Draw Visual Preview"),
    CONTRACT("Close Visual Preview"),
    CONTRACT("Export Visual Evidence"),
    CONTRACT("Visual Evidence Status"),
    CONTRACT("Close Visual Evidence")
};
const native_definition_pack VISUAL_PREVIEW_DECLARATION_PACK = {
    .definitions = VISUAL_PREVIEW_DEFINITIONS,
    .definition_count = VISUAL_PREVIEW_OPERATION_COUNT,
    .bindings = CONTRACTS,
    .binding_count = NATIVE_COUNT(CONTRACTS),
    .layouts = LAYOUTS,
    .layout_count = NATIVE_COUNT(LAYOUTS)
};
