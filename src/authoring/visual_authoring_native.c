#include "authoring/visual_authoring_definition.h"
#include "execution/function.h"
#include "support/arena.h"
#include "types/array.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIELD(OWNER, NAME, KIND, TYPE)                                         \
    { .name = #NAME,                                                           \
      .type = KIND,                                                            \
      .type_name = TYPE,                                                       \
      .offset = offsetof(OWNER, NAME),                                         \
      .size = sizeof(((OWNER *)0)->NAME) }
#define INT(OWNER, NAME) FIELD(OWNER, NAME, PIN_I32, "int")
#define FLOAT(OWNER, NAME) FIELD(OWNER, NAME, PIN_F32, "float")
static const type_field PARAMETER_FIELDS[] = {
    FIELD(visual_parameter, name, PIN_STR, "const char *"),
    INT(visual_parameter, slot),
    INT(visual_parameter, width),
    INT(visual_parameter, presentation),
    FIELD(visual_parameter, x, PIN_F64, "double"),
    FIELD(visual_parameter, y, PIN_F64, "double"),
    FIELD(visual_parameter, z, PIN_F64, "double"),
    FIELD(visual_parameter, w, PIN_F64, "double"),
    FIELD(visual_parameter, kind, PIN_STR, "const char *"),
    FIELD(visual_parameter, editable, PIN_BOOL, "bool"),
    FIELD(visual_parameter, bounded, PIN_BOOL, "bool"),
    FIELD(visual_parameter, minimum, PIN_F64, "double"),
    FIELD(visual_parameter, maximum, PIN_F64, "double")
};
static const type_field COST_FIELDS[] = {
    INT(visual_costs, nodes),           INT(visual_costs, uniforms),
    INT(visual_costs, images),          INT(visual_costs, ramp_vec4s),
    INT(visual_costs, texture_samples), INT(visual_costs, detail_expressions)
};
static const type_field SAMPLE_FIELDS[] = { INT(visual_sample, slot),
                                            FLOAT(visual_sample, x),
                                            FLOAT(visual_sample, y),
                                            FLOAT(visual_sample, z),
                                            FLOAT(visual_sample, w) };
#define LAYOUT(NAME, TYPE, FIELDS)                                             \
    const type_desc NAME = { .name = #TYPE,                                    \
                             .size = sizeof(TYPE),                             \
                             .fields = FIELDS,                                 \
                             .field_count = NATIVE_COUNT(FIELDS) }
LAYOUT(VISUAL_PARAMETER_LAYOUT, visual_parameter, PARAMETER_FIELDS);
LAYOUT(VISUAL_COSTS_LAYOUT, visual_costs, COST_FIELDS);
LAYOUT(VISUAL_SAMPLE_LAYOUT, visual_sample, SAMPLE_FIELDS);
#define CHANNEL(F, N, I)                                                       \
    { .name = N,                                                               \
      .type = PIN_F32,                                                         \
      .type_name = "float",                                                    \
      .offset = offsetof(visual_material_settings, F) + I * sizeof(float),     \
      .size = sizeof(float) }
static const type_field MATERIAL_SETTINGS_FIELDS[] = {
    CHANNEL(base_rgb, "base_r", 0),
    CHANNEL(base_rgb, "base_g", 1),
    CHANNEL(base_rgb, "base_b", 2),
    FLOAT(visual_material_settings, opacity),
    FLOAT(visual_material_settings, roughness),
    FLOAT(visual_material_settings, metallic),
    CHANNEL(emission_rgb, "emission_r", 0),
    CHANNEL(emission_rgb, "emission_g", 1),
    CHANNEL(emission_rgb, "emission_b", 2),
    FLOAT(visual_material_settings, emission_gain),
    INT(visual_material_settings, alpha_mode),
    FLOAT(visual_material_settings, alpha_cutoff),
    FIELD(visual_material_settings, unlit, PIN_BOOL, "bool")
};
#undef CHANNEL
const type_desc VISUAL_MATERIAL_SETTINGS_LAYOUT = {
    .name = "MaterialAuthoringSettings",
    .size = sizeof(visual_material_settings),
    .fields = MATERIAL_SETTINGS_FIELDS,
    .field_count = NATIVE_COUNT(MATERIAL_SETTINGS_FIELDS)
};
const native_type_definition VISUAL_AUTHORING_TYPES[4] = {
    { .layout = &VISUAL_PARAMETER_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &VISUAL_COSTS_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &VISUAL_SAMPLE_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &VISUAL_MATERIAL_SETTINGS_LAYOUT, .storage = PIN_STRUCT }
};
static pin_array *array(arena *storage, const type_desc *type, int count)
{
    pin_array *result = arena_alloc(storage, sizeof *result);
    *result = (pin_array){ .element_type = PIN_STRUCT,
                           .type_name = type->name,
                           .count = count,
                           .capacity = count };
    result->items = count ? arena_alloc(storage, (size_t)count * sizeof *result->items)
                          : NULL;
    for (int i = 0; i < count; i++) {
        result->items[i] = (pin_value){ .type = PIN_STRUCT,
                                        .type_name = type->name,
                                        .ptr = arena_alloc(storage, type->size) };
    }
    return result;
}
static bool inspect_document(
    const visual_authoring_service *service,
    int document,
    int revision,
    visual_authoring_report *report
)
{
    return visual_authoring_inspect_linked(
        service->documents,
        document,
        revision,
        service->link_source,
        service->link_userdata,
        report
    );
}
static const char *inspect(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    const visual_authoring_service *service = host;
    visual_authoring_report report;
    visual_authoring_report_init(&report);
    bool ok = inspect_document(service, in[0].i, in[1].i, &report);
    int count = 0;
    if (ok) {
        for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
            count += report.shader.names[i] != NULL;
        }
    }
    pin_array *rows = array(ctx->value_storage, &VISUAL_PARAMETER_LAYOUT, count);
    int row = 0;
    if (ok) {
        for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
            if (!report.shader.names[i]) {
                continue;
            }
            double v[4] = { 0 };
            bool known = visual_authoring_parameter_values(&report, i, v);
            pin_type storage = visual_authoring_parameter_storage(&report, i);
            const char *kind = storage == PIN_BOOL ? "boolean"
                             : storage == PIN_I32  ? "integer"
                             : storage == PIN_F32 || storage == PIN_F64
                                 ? "number"
                             : storage == PIN_STRUCT ? "vector"
                                                     : "unavailable";
            *(visual_parameter *)rows->items[row++].ptr = (visual_parameter){
                .name = ctx->copy_string(ctx->value_storage, report.shader.names[i]),
                .slot = i,
                .width = report.shader.widths[i],
                .presentation = report.shader.presentation[i],
                .x = v[0],
                .y = v[1],
                .z = v[2],
                .w = v[3],
                .kind = ctx->copy_string(ctx->value_storage, kind),
                .editable = known && (storage == PIN_BOOL || storage == PIN_I32 ||
                                      storage == PIN_F32 || storage == PIN_F64 ||
                                      storage == PIN_STRUCT),
                .bounded = storage == PIN_BOOL || storage == PIN_I32,
                .minimum = storage == PIN_I32 ? INT32_MIN : 0,
                .maximum = storage == PIN_I32  ? INT32_MAX
                         : storage == PIN_BOOL ? 1
                                               : 0
            };
        }
    }
    shader_inspection c = report.shader.costs;
    *(visual_costs *)out[1].ptr = (visual_costs){
        c.nodes,      c.uniforms,        c.images,
        c.ramp_vec4s, c.texture_samples, c.detail_expressions
    };
    out[0].ptr = rows;
    out[2].b = ok;
    out[3].str = ctx->copy_string(ctx->value_storage, report.error.message);
    char graph[32] = "", node[32] = "";
    if (report.diagnostic_node) {
        snprintf(graph, sizeof graph, "%llu", (unsigned long long)report.diagnostic_graph);
        snprintf(node, sizeof node, "%llu", (unsigned long long)report.diagnostic_node);
    }
    out[4].str = ctx->copy_string(ctx->value_storage, graph);
    out[5].str = ctx->copy_string(ctx->value_storage, node);
    visual_authoring_report_free(&report);
    return NULL;
}
static const char *prepare_default(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_authoring_service *service = host;
    double values[4] = { in[4].d, in[5].d, in[6].d, in[7].d };
    shader_error error = { 0 };
    char *text = visual_authoring_prepare_default_exact(
        service->documents,
        in[0].i,
        in[1].i,
        in[2].i,
        in[3].i,
        values,
        &error
    );
    out[0].b = text != NULL;
    out[1].str = ctx->copy_string(ctx->value_storage, text ? text : "");
    out[2].str = ctx->copy_string(ctx->value_storage, error.message);
    free(text);
    return NULL;
}
static const char *set_default(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_authoring_service *service = host;
    double values[4] = { in[4].d, in[5].d, in[6].d, in[7].d };
    shader_error error = { 0 };
    int revision = in[1].i;
    out[0].b = visual_authoring_set_default_exact(
        service->documents,
        in[0].i,
        in[1].i,
        in[2].i,
        in[3].i,
        values,
        &revision,
        &error
    );
    out[1].i = revision;
    out[2].str = ctx->copy_string(ctx->value_storage, error.message);
    return NULL;
}
static const char *sample(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    const visual_authoring_service *service = host;
    visual_authoring_report report;
    visual_authoring_report_init(&report);
    bool ok = inspect_document(service, in[0].i, in[1].i, &report);
    float values[SHADER_OUTPUT_MAX][4] = { 0 };
    shader_error error = report.error;
    if (ok) {
        ok = visual_authoring_sample(
            &report,
            in[2].f,
            in[3].f,
            in[4].f,
            service->resolve_image,
            service->image_userdata,
            values,
            &error
        );
    }
    int count = ok ? report.shader.program.output_count : 0;
    pin_array *rows = array(ctx->value_storage, &VISUAL_SAMPLE_LAYOUT, count);
    for (int i = 0; i < count; i++) {
        *(visual_sample *)rows->items[i].ptr = (visual_sample){ i,
                                                                values[i][0],
                                                                values[i][1],
                                                                values[i][2],
                                                                values[i][3] };
    }
    out[0].ptr = rows;
    out[1].b = ok;
    out[2].str = ctx->copy_string(ctx->value_storage, error.message);
    visual_authoring_report_free(&report);
    return NULL;
}
static const char *create_material(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_authoring_service *service = host;
    char error[512] = "";
    char *path = visual_material_create(
        service->documents,
        service->content_root,
        service->workspace_root,
        in[0].str,
        in[1].ptr,
        error,
        sizeof error
    );
    out[0].b = path != NULL;
    out[1].str = ctx->copy_string(ctx->value_storage, path ? path : "");
    out[2].str = ctx->copy_string(ctx->value_storage, error);
    free(path);
    return NULL;
}
static const char *preset_settings(
    const behavior_function_context *ctx,
    void *,
    const pin_value *in,
    pin_value *out
)
{
    shader_error error = { 0 };
    visual_material_settings *settings = arena_alloc(ctx->value_storage, sizeof *settings);
    *settings = (visual_material_settings){ 0 };
    out[0].b = visual_material_preset_settings(in[0].i, settings, &error);
    out[1].ptr = settings;
    out[2].str = ctx->copy_string(ctx->value_storage, error.message);
    return NULL;
}
static const char *material_source(
    const behavior_function_context *ctx,
    void *,
    const pin_value *in,
    pin_value *out
)
{
    shader_error error = { 0 };
    char *source = visual_material_source(in[0].str, in[1].ptr, &error);
    out[0].b = source != NULL;
    out[1].str = ctx->copy_string(ctx->value_storage, source ? source : "");
    out[2].str = ctx->copy_string(ctx->value_storage, error.message);
    free(source);
    return NULL;
}
static const char *preset(const behavior_function_context *ctx, void *, const pin_value *in, pin_value *out)
{
    shader_error error = { 0 };
    char *source = visual_authoring_preset_source(in[0].str, in[1].i, &error);
    out[0].str = ctx->copy_string(ctx->value_storage, source ? source : "");
    out[1].str = ctx->copy_string(ctx->value_storage, error.message);
    free(source);
    return NULL;
}
#define PORT(NAME, KIND, TYPE)                                                 \
    { .name = NAME, .kind = NATIVE_PARAMETER_DATA, .storage = KIND, .type_name = TYPE }
#define EXEC(NAME)                                                             \
    { .name = NAME, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
#define I(NAME) PORT(NAME, PIN_I32, "int")
#define F(NAME) PORT(NAME, PIN_F32, "float")
#define S(NAME) PORT(NAME, PIN_STR, "const char *")
#define B(NAME) PORT(NAME, PIN_BOOL, "bool")
#define A(NAME, TYPE)                                                          \
    { .name = NAME,                                                            \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = PIN_STRUCT,                                                   \
      .type_name = TYPE,                                                       \
      .container = PIN_CONTAINER_ARRAY }
static const native_parameter_definition INSPECT_IN[] = { EXEC("Exec"),
                                                          I("Document"),
                                                          I("Revision") };
static const native_parameter_definition INSPECT_OUT[] = {
    EXEC("Then"),
    A("Parameters", "visual_parameter"),
    PORT("Costs", PIN_STRUCT, "visual_costs"),
    B("Valid"),
    S("Error"),
    S("DiagnosticGraph"),
    S("DiagnosticNode")
};
static const native_parameter_definition DEFAULT_IN[] = {
    EXEC("Exec"),
    I("Document"),
    I("Revision"),
    I("Slot"),
    I("Width"),
    PORT("X", PIN_F64, "double"),
    PORT("Y", PIN_F64, "double"),
    PORT("Z", PIN_F64, "double"),
    PORT("W", PIN_F64, "double")
};
static const native_parameter_definition PREPARE_OUT[] = { EXEC("Then"),
                                                           B("Valid"),
                                                           S("Text"),
                                                           S("Error") };
static const native_parameter_definition DEFAULT_OUT[] = { EXEC("Then"),
                                                           B("Valid"),
                                                           I("Revision"),
                                                           S("Error") };
static const native_parameter_definition SAMPLE_IN[] = {
    EXEC("Exec"), I("Document"), I("Revision"), F("U"), F("V"), F("Time")
};
static const native_parameter_definition SAMPLE_OUT[] = { EXEC("Then"),
                                                          A("Samples", "visual_sample"),
                                                          B("Valid"),
                                                          S("Error") };
static const native_parameter_definition PRESET_IN[] = { EXEC("Exec"),
                                                         S("Name"),
                                                         I("Preset") };
static const native_parameter_definition PRESET_OUT[] = { EXEC("Then"),
                                                          S("Source"),
                                                          S("Error") };
static const native_parameter_definition MATERIAL_SETTINGS_IN[] = { EXEC("Exec"),
                                                                    I("Preset") };
static const native_parameter_definition MATERIAL_SETTINGS_OUT[] = {
    EXEC("Then"),
    B("Valid"),
    PORT("Settings", PIN_STRUCT, "MaterialAuthoringSettings"),
    S("Error")
};
static const native_parameter_definition MATERIAL_SOURCE_IN[] = {
    EXEC("Exec"),
    S("Name"),
    PORT("Settings", PIN_STRUCT, "MaterialAuthoringSettings")
};
static const native_parameter_definition MATERIAL_SOURCE_OUT[] = { EXEC("Then"),
                                                                   B("Valid"),
                                                                   S("Source"),
                                                                   S("Error") };
static const native_parameter_definition MATERIAL_CREATE_OUT[] = { EXEC("Then"),
                                                                   B("Valid"),
                                                                   S("Path"),
                                                                   S("Error") };
#define OPS(X)                                                                                        \
    X("Create Material", "create_material", create_material, MATERIAL_SOURCE_IN, MATERIAL_CREATE_OUT) \
    X("Material Preset Settings",                                                                     \
      "material_preset_settings",                                                                     \
      preset_settings,                                                                                \
      MATERIAL_SETTINGS_IN,                                                                           \
      MATERIAL_SETTINGS_OUT)                                                                          \
    X("Material Source", "material_source", material_source, MATERIAL_SOURCE_IN, MATERIAL_SOURCE_OUT) \
    X("Inspect Visual", "inspect_visual", inspect, INSPECT_IN, INSPECT_OUT)                           \
    X("Prepare Visual Default", "prepare_visual_default", prepare_default, DEFAULT_IN, PREPARE_OUT)   \
    X("Set Visual Default", "set_visual_default", set_default, DEFAULT_IN, DEFAULT_OUT)               \
    X("Sample Visual", "sample_visual", sample, SAMPLE_IN, SAMPLE_OUT)                                \
    X("Material Preset Source", "material_preset_source", preset, PRESET_IN, PRESET_OUT)
#define CALL(NAME, SOURCE, FN, INPUT, OUTPUT)                                  \
    static const char *FN##_dispatch(                                          \
        const behavior_function_context *ctx,                                  \
        void *host,                                                            \
        const pin_value *in,                                                   \
        pin_value *out                                                         \
    )                                                                          \
    {                                                                          \
        if (!host) {                                                           \
            return "Visual authoring requires an authoring host";              \
        }                                                                      \
        return FN(ctx, host, in, out);                                         \
    }                                                                          \
    static const node_host_call FN##_call = {                                  \
        .service = "visual.authoring",                                         \
        .optional_service = true,                                              \
        .missing_service = "Visual authoring requires an authoring host",      \
        .context = FN##_dispatch                                               \
    };
OPS(CALL)
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
#define DEF(NAME, SOURCE, FN, INPUT, OUTPUT)                                   \
    { .canonical_name = NAME,                                                  \
      .source_name = SOURCE,                                                   \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = INPUT,                                                         \
      .input_count = NATIVE_COUNT(INPUT),                                      \
      .outputs = OUTPUT,                                                       \
      .output_count = NATIVE_COUNT(OUTPUT),                                    \
      .completion_output = 0 },
static const native_capability_definition DEFINITIONS[] = { OPS(DEF) };
#define BIND(NAME, SOURCE, FN, INPUT, OUTPUT)                                  \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &FN##_call, .access = &ACCESS } },
static const native_definition_binding BINDINGS[] = { OPS(BIND) };
static const type_desc *const LAYOUTS[] = { &VISUAL_PARAMETER_LAYOUT,
                                            &VISUAL_COSTS_LAYOUT,
                                            &VISUAL_SAMPLE_LAYOUT,
                                            &VISUAL_MATERIAL_SETTINGS_LAYOUT };
const native_definition_pack VISUAL_AUTHORING_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
    .layouts = LAYOUTS,
    .layout_count = NATIVE_COUNT(LAYOUTS)
};
