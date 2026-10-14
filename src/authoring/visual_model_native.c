#include "assets/model_export_artifacts.h"
#include "authoring/visual_model_definition.h"
#include "authoring/workspace_file.h"
#include "execution/function.h"
#include "graph/document/serialize.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIELD(OWNER, NAME, KIND, TYPE)                                         \
    { .name = #NAME,                                                           \
      .type = KIND,                                                            \
      .type_name = TYPE,                                                       \
      .offset = offsetof(OWNER, NAME),                                         \
      .size = sizeof(((OWNER *)0)->NAME) }
#define I(O, N) FIELD(O, N, PIN_I32, "int")
#define F(O, N) FIELD(O, N, PIN_F32, "float")
#define S(O, N) FIELD(O, N, PIN_STR, "const char *")
#define U(O, N) FIELD(O, N, PIN_U64, "uint64")
#define B(O, N) FIELD(O, N, PIN_BOOL, "bool")
static const type_field PARAMETER_FIELDS[] = {
    S(visual_model_parameter, name),  S(visual_model_parameter, asset),
    S(visual_model_parameter, scope), U(visual_model_parameter, node),
    I(visual_model_parameter, slot),  I(visual_model_parameter, width),
    B(visual_model_parameter, local), F(visual_model_parameter, x),
    F(visual_model_parameter, y),     F(visual_model_parameter, z),
    F(visual_model_parameter, w),     F(visual_model_parameter, min_x),
    F(visual_model_parameter, min_y), F(visual_model_parameter, min_z),
    F(visual_model_parameter, min_w), F(visual_model_parameter, max_x),
    F(visual_model_parameter, max_y), F(visual_model_parameter, max_z),
    F(visual_model_parameter, max_w)
};
static const type_field INFO_FIELDS[] = {
    I(visual_model_info, distance_nodes), I(visual_model_info, hit_nodes),
    I(visual_model_info, public_slots),   I(visual_model_info, private_slots),
    I(visual_model_info, materials),      I(visual_model_info, images),
    I(visual_model_info, ramp_vec4s),     F(visual_model_info, min_x),
    F(visual_model_info, min_y),          F(visual_model_info, min_z),
    F(visual_model_info, max_x),          F(visual_model_info, max_y),
    F(visual_model_info, max_z),          B(visual_model_info, empty)
};
static const type_field PART_FIELDS[] = { I(visual_model_part, index),
                                          I(visual_model_part, material),
                                          I(visual_model_part, distance_nodes),
                                          U(visual_model_part, node),
                                          S(visual_model_part, name),
                                          S(visual_model_part, asset),
                                          S(visual_model_part, path) };
static const type_field ANCHOR_FIELDS[] = {
    I(visual_model_anchor, index), U(visual_model_anchor, node),
    S(visual_model_anchor, name),  S(visual_model_anchor, asset),
    S(visual_model_anchor, scope), F(visual_model_anchor, x),
    F(visual_model_anchor, y),     F(visual_model_anchor, z),
    F(visual_model_anchor, pitch), F(visual_model_anchor, yaw),
    F(visual_model_anchor, roll)
};
#define L(NAME, TYPE, FIELDS, KEY)                                             \
    const type_desc NAME = { .name = KEY,                                      \
                             .size = sizeof(TYPE),                             \
                             .fields = FIELDS,                                 \
                             .field_count = NATIVE_COUNT(FIELDS) }
L(VISUAL_MODEL_PARAMETER_LAYOUT, visual_model_parameter, PARAMETER_FIELDS, "ModelAuthoringParameter");
L(VISUAL_MODEL_INFO_LAYOUT, visual_model_info, INFO_FIELDS, "ModelAuthoringInfo");
L(VISUAL_MODEL_PART_LAYOUT, visual_model_part, PART_FIELDS, "ModelAuthoringPart");
L(VISUAL_MODEL_ANCHOR_LAYOUT, visual_model_anchor, ANCHOR_FIELDS, "ModelAuthoringAnchor");
static const type_field REFERENCE_FIELDS[] = {
    S(visual_model_reference, image),    F(visual_model_reference, center_x),
    F(visual_model_reference, center_y), F(visual_model_reference, height),
    F(visual_model_reference, angle),    B(visual_model_reference, locked)
};
L(VISUAL_MODEL_REFERENCE_LAYOUT, visual_model_reference, REFERENCE_FIELDS, "ModelReference");
const native_type_definition VISUAL_MODEL_TYPES[5] = {
    { .layout = &VISUAL_MODEL_PARAMETER_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &VISUAL_MODEL_INFO_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &VISUAL_MODEL_PART_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &VISUAL_MODEL_ANCHOR_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &VISUAL_MODEL_REFERENCE_LAYOUT, .storage = PIN_STRUCT }
};
enum
{
    MODEL_BAKES_MAX = 4,
    MODEL_BAKE_BYTES_MAX = 32 * 1024 * 1024
};
typedef struct
{
    visual_model_bake_view view;
    unsigned char *data;
    model_export_artifacts bundle;
} model_artifact;
struct visual_model_service
{
    workspace_documents *documents;
    shader_image_resolver resolve_image;
    void *image_user;
    visual_model_catalog_reader read_catalog;
    void *catalog_user;
    void *(*enter_scope)(void *, const char **);
    void (*leave_scope)(void *, void *);
    void *scope_user;
    workspace_files *files;
    model_artifact bakes[MODEL_BAKES_MAX];
    int sequence;
    size_t bytes;
};
visual_model_service *visual_models_new(workspace_documents *documents, const char *root)
{
    visual_model_service *service = xcalloc(1, sizeof *service);
    service->documents = documents;
    char error[256];
    service->files = root ? workspace_files_new(root, error, sizeof error) : NULL;
    return service;
}
void visual_models_free(visual_model_service *service)
{
    if (!service) {
        return;
    }
    workspace_files_free(service->files);
    for (int i = 0; i < MODEL_BAKES_MAX; i++) {
        model_export_artifacts_free(&service->bakes[i].bundle);
    }
    free(service);
}
void visual_models_set_images(visual_model_service *service, shader_image_resolver resolver, void *user)
{
    service->resolve_image = resolver;
    service->image_user = user;
}
void visual_models_set_catalog(visual_model_service *service, visual_model_catalog_reader reader, void *user)
{
    service->read_catalog = reader;
    service->catalog_user = user;
}
void visual_models_set_scope(
    visual_model_service *service,
    void *(*enter)(void *, const char **),
    void (*leave)(void *, void *),
    void *user
)
{
    assert((enter != NULL) == (leave != NULL));
    service->enter_scope = enter;
    service->leave_scope = leave;
    service->scope_user = user;
}
static void *enter_scope(visual_model_service *service, const char **error)
{
    *error = NULL;
    return service->enter_scope ? service->enter_scope(service->scope_user, error)
                                : NULL;
}
static void leave_scope(visual_model_service *service, void *token)
{
    if (service->leave_scope) {
        service->leave_scope(service->scope_user, token);
    }
}
static bool read_report(
    visual_model_service *service,
    int document,
    int revision,
    visual_model_report *report,
    model_export_source **snapshots
)
{
    int count = 0;
    behavior_asset **catalog = service->read_catalog
                                 ? service->read_catalog(service->catalog_user, &count)
                                 : NULL;
    bool valid = visual_model_inspect_images(
        service->documents,
        document,
        revision,
        (const behavior_asset *const *)catalog,
        count,
        service->resolve_image,
        service->image_user,
        report
    );
    if (valid && snapshots) {
        workspace_document_view view;
        workspace_document_read(service->documents, document, &view);
        apush(
            *snapshots,
            ((model_export_source){ xstrdup(report->source->name),
                                    xstrdup(view.path ? view.path : ""),
                                    xstrdup(view.text) })
        );
        for (int i = 0; i < count; i++) {
            if (!strcmp(catalog[i]->name, report->source->name)) {
                continue;
            }
            char *text = behavior_asset_save(catalog[i]);
            if (!text) {
                valid = false;
                break;
            }
            apush(
                *snapshots,
                ((model_export_source){ xstrdup(catalog[i]->name),
                                        xstrdup("applied-catalog"),
                                        text })
            );
        }
    }
    for (int i = 0; i < count; i++) {
        behavior_asset_free(catalog[i]);
    }
    free(catalog);
    return valid;
}
static bool report_error(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static model_artifact *find_bake(const visual_model_service *service, int handle)
{
    for (int i = 0; i < MODEL_BAKES_MAX; i++) {
        if (handle > 0 && service->bakes[i].view.bake == handle) {
            return (model_artifact *)&service->bakes[i];
        }
    }
    return NULL;
}
static bool visual_models_bake_inner(
    visual_model_service *service,
    int document,
    int revision,
    const model_bake_options *options,
    visual_model_bake_view *view,
    char *error,
    size_t size
)
{
    model_artifact *slot = NULL;
    for (int i = 0; i < MODEL_BAKES_MAX; i++) {
        if (!service->bakes[i].view.bake) {
            slot = &service->bakes[i];
            break;
        }
    }
    if (!slot || service->sequence == INT_MAX) {
        return report_error(error, size, "Close a model bake before starting another");
    }
    visual_model_report report;
    visual_model_report_init(&report);
    model_export_source *snapshots = NULL;
    bool valid = read_report(service, document, revision, &report, &snapshots);
    model_bake_result bake = { 0 };
    shader_error diagnostic = report.error;
    if (valid) {
        valid = visual_model_bake(&report, options, &bake, &diagnostic);
    }
    model_export_artifacts bundle = { 0 };
    model_export_options export_options = { .bake = *options };
    if (!valid) {
        report_error(error, size, diagnostic.message);
    }
    if (valid) {
        valid = model_export_artifacts_build(
            report.source->name,
            &report.program,
            &bake,
            &export_options,
            snapshots,
            acount(snapshots),
            MODEL_BAKE_BYTES_MAX - service->bytes,
            &bundle,
            error,
            size
        );
    }
    for (int i = 0; valid && i < 4; i++) {
        if (bundle.files[i].size > WORKSPACE_FILE_BYTES_MAX) {
            valid = report_error(error, size, "A model artifact exceeds its16MiB file budget");
        }
    }
    size_t length = bundle.files[0].size;
    if (valid) {
        slot->view = (visual_model_bake_view){ .bake = ++service->sequence,
                                               .document = document,
                                               .revision = revision,
                                               .triangles = acount(bake.mesh.indices) / 3,
                                               .bytes = length,
                                               .samples = bake.samples,
                                               .work = bake.work,
                                               .cell_diagonal = bake.cell_diagonal,
                                               .max_residual = bake.max_residual };
        slot->bundle = bundle;
        bundle = (model_export_artifacts){ 0 };
        slot->data = slot->bundle.files[0].bytes;
        service->bytes += slot->bundle.size;
        *view = slot->view;
        if (size) {
            error[0] = 0;
        }
    }
    model_export_artifacts_free(&bundle);
    for (int i = 0; i < acount(snapshots); i++) {
        free((char *)snapshots[i].name);
        free((char *)snapshots[i].path);
        free((char *)snapshots[i].text);
    }
    afree(snapshots);
    model_bake_result_free(&bake);
    visual_model_report_free(&report);
    return valid;
}
bool visual_models_bake(
    visual_model_service *service,
    int document,
    int revision,
    const model_bake_options *options,
    visual_model_bake_view *view,
    char *error,
    size_t size
)
{
    const char *scope_error;
    void *token = enter_scope(service, &scope_error);
    if (scope_error) {
        return report_error(error, size, scope_error);
    }
    bool valid = visual_models_bake_inner(service, document, revision, options, view, error, size);
    leave_scope(service, token);
    return valid;
}
bool visual_model_bake_read(const visual_model_service *service, int handle, visual_model_bake_view *view)
{
    model_artifact *artifact = find_bake(service, handle);
    if (!artifact) {
        return false;
    }
    *view = artifact->view;
    return true;
}
bool visual_model_bake_export(
    visual_model_service *service,
    int handle,
    const char *path,
    char *error,
    size_t size
)
{
    model_artifact *artifact = find_bake(service, handle);
    if (!artifact) {
        return report_error(error, size, "Model bake handle is invalid");
    }
    size_t length = path ? strlen(path) : 0;
    if (length < 5 || strcmp(path + length - 5, ".mesh")) {
        return report_error(error, size, "Choose a new .mesh file inside the workspace");
    }
    return workspace_file_write_new(
        service->files,
        path,
        artifact->data,
        artifact->view.bytes,
        error,
        size
    );
}
bool visual_model_bake_export_bundle(
    visual_model_service *service,
    int handle,
    const char *directory,
    char *error,
    size_t size
)
{
    model_artifact *artifact = find_bake(service, handle);
    if (!artifact) {
        return report_error(error, size, "Model bake handle is invalid");
    }
    workspace_file_entry entries[4];
    for (int i = 0; i < 4; i++) {
        entries[i] = (workspace_file_entry){ artifact->bundle.files[i].name,
                                             artifact->bundle.files[i].bytes,
                                             artifact->bundle.files[i].size };
    }
    return workspace_file_bundle_write_atomic(service->files, directory, entries, 4, error, size);
}
bool visual_model_bake_close(visual_model_service *service, int handle)
{
    model_artifact *artifact = find_bake(service, handle);
    if (!artifact) {
        return false;
    }
    service->bytes -= artifact->bundle.size;
    model_export_artifacts_free(&artifact->bundle);
    *artifact = (model_artifact){ 0 };
    return true;
}
static pin_array *rows(arena *storage, const type_desc *type, int count)
{
    pin_array *array = arena_alloc(storage, sizeof *array);
    *array = (pin_array){ .element_type = PIN_STRUCT,
                          .type_name = type->name,
                          .count = count,
                          .capacity = count };
    array->items = count ? arena_alloc(storage, (size_t)count * sizeof *array->items)
                         : NULL;
    for (int i = 0; i < count; i++) {
        array->items[i] = (pin_value){ .type = PIN_STRUCT,
                                       .type_name = type->name,
                                       .ptr = arena_alloc(storage, type->size) };
        memset(array->items[i].ptr, 0, type->size);
    }
    return array;
}
static const char *inspect_model_inner(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_model_service *service = host;
    visual_model_report report;
    visual_model_report_init(&report);
    bool valid = read_report(service, in[0].i, in[1].i, &report, NULL);
    int parameter_count = 0;
    if (valid) {
        for (int i = 0; i < acount(report.program.parameters); i++) {
            parameter_count += !report.program.parameters[i].hidden;
        }
    }
    pin_array *parameters = rows(ctx->value_storage, &VISUAL_MODEL_PARAMETER_LAYOUT, parameter_count);
    int item = 0;
    for (int i = 0; valid && i < acount(report.program.parameters); i++) {
        const model_parameter *p = &report.program.parameters[i];
        if (p->hidden) {
            continue;
        }
        float *v = report.parameters[p->slot];
        visual_model_parameter *row = parameters->items[item++].ptr;
        *row = (visual_model_parameter){
            .name = ctx->copy_string(ctx->value_storage, p->name),
            .asset = ctx->copy_string(ctx->value_storage, p->asset),
            .scope = ctx->copy_string(ctx->value_storage, p->scope),
            .node = p->node,
            .slot = p->slot,
            .width = p->width,
            .local = !strcmp(p->asset, report.source->name) && !p->scope[0],
            .x = v[0],
            .y = v[1],
            .z = v[2],
            .w = v[3],
            .min_x = p->minimum[0],
            .min_y = p->minimum[1],
            .min_z = p->minimum[2],
            .min_w = p->minimum[3],
            .max_x = p->maximum[0],
            .max_y = p->maximum[1],
            .max_z = p->maximum[2],
            .max_w = p->maximum[3]
        };
    }
    pin_array *parts = rows(
        ctx->value_storage,
        &VISUAL_MODEL_PART_LAYOUT,
        valid ? acount(report.inspection.parts) : 0
    );
    for (int i = 0; i < parts->count; i++) {
        const model_part_inspection *p = &report.inspection.parts[i];
        *(visual_model_part *)parts->items[i].ptr = (visual_model_part){
            i,
            p->material,
            p->distance_nodes,
            p->node,
            ctx->copy_string(ctx->value_storage, p->name),
            ctx->copy_string(ctx->value_storage, p->asset),
            ctx->copy_string(ctx->value_storage, p->path)
        };
    }
    pin_array *anchors = rows(
        ctx->value_storage,
        &VISUAL_MODEL_ANCHOR_LAYOUT,
        valid ? acount(report.program.anchors) : 0
    );
    for (int i = 0; i < anchors->count; i++) {
        const model_anchor *p = &report.program.anchors[i];
        float position[3] = { 0 }, angles[3] = { 0 };
        shader_error error = { 0 };
        if (!model_program_anchor(&report.program, i, report.parameters, position, angles, &error)) {
            valid = false;
            report.error = error;
            break;
        }
        *(visual_model_anchor *)anchors->items[i].ptr = (visual_model_anchor){
            i,
            p->node,
            ctx->copy_string(ctx->value_storage, p->name),
            ctx->copy_string(ctx->value_storage, p->asset),
            ctx->copy_string(ctx->value_storage, p->scope),
            position[0],
            position[1],
            position[2],
            angles[0],
            angles[1],
            angles[2]
        };
    }
    model_inspection c = report.inspection;
    model_bounds b = report.bounds;
    *(visual_model_info *)out[1].ptr = (visual_model_info){
        c.distance_nodes, c.hit_nodes, c.public_slots, c.private_slots,
        c.material_slots, c.images,    c.ramp_vec4s,   b.min[0],
        b.min[1],         b.min[2],    b.max[0],       b.max[1],
        b.max[2],         b.empty
    };
    out[0].b = valid;
    out[2].ptr = parameters;
    out[3].ptr = parts;
    out[4].ptr = anchors;
    out[5].str = ctx->copy_string(ctx->value_storage, report.error.message);
    visual_model_report_free(&report);
    return NULL;
}
static const char *inspect_model(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_model_service *service = host;
    const char *error;
    void *token = enter_scope(service, &error);
    if (error) {
        return error;
    }
    const char *result = inspect_model_inner(ctx, host, in, out);
    leave_scope(service, token);
    return result;
}
static const char *export_bundle(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    char error[256] = "";
    out[0].b = visual_model_bake_export_bundle(host, in[0].i, in[1].str, error, sizeof error);
    out[1].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *inspect_reference(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_model_service *service = host;
    visual_model_reference value = { 0 };
    shader_error error = { 0 };
    out[0].b = visual_model_reference_read(service->documents, in[0].i, in[1].i, &value, &error);
    visual_model_reference *copy = arena_alloc(ctx->value_storage, sizeof *copy);
    *copy = value;
    copy->image = ctx->copy_string(ctx->value_storage, value.image ? value.image : "");
    out[1].ptr = copy;
    out[2].str = ctx->copy_string(ctx->value_storage, error.message);
    visual_model_reference_free(&value);
    return NULL;
}
static const char *prepare_reference(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_model_service *service = host;
    shader_error error = { 0 };
    char *text = visual_model_reference_prepare(
        service->documents,
        in[0].i,
        in[1].i,
        in[2].ptr,
        &error
    );
    out[0].b = text != NULL;
    out[1].str = ctx->copy_string(ctx->value_storage, text ? text : "");
    out[2].str = ctx->copy_string(ctx->value_storage, error.message);
    free(text);
    return NULL;
}
static const char *prepare_default(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_model_service *service = host;
    float values[4] = { in[4].f, in[5].f, in[6].f, in[7].f };
    shader_error error = { 0 };
    char *text = visual_model_prepare_default(
        service->documents,
        in[0].i,
        in[1].i,
        in[2].u64,
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
    visual_model_service *service = host;
    shader_error error = { 0 };
    float values[4] = { in[4].f, in[5].f, in[6].f, in[7].f };
    int revision = in[1].i;
    out[0].b = visual_model_set_default(
        service->documents,
        in[0].i,
        in[1].i,
        in[2].u64,
        in[3].i,
        values,
        &revision,
        &error
    );
    out[1].i = revision;
    out[2].str = ctx->copy_string(ctx->value_storage, error.message);
    return NULL;
}
static const char *bake_model(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    model_bake_options options = model_bake_defaults();
    options.cell_size = in[2].f;
    options.max_triangles = in[3].i;
    options.max_samples = in[4].u64;
    options.max_work = in[5].u64;
    visual_model_bake_view view = { 0 };
    char error[256] = "";
    out[0].b = visual_models_bake(host, in[0].i, in[1].i, &options, &view, error, sizeof error);
    out[1].i = view.bake;
    out[2].i = view.triangles;
    out[3].u64 = view.samples;
    out[4].u64 = view.work;
    out[5].f = view.cell_diagonal;
    out[6].f = view.max_residual;
    out[7].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *export_bake(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    char error[256] = "";
    out[0].b = visual_model_bake_export(host, in[0].i, in[1].str, error, sizeof error);
    out[1].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *close_bake(
    const behavior_function_context *,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    out[0].b = visual_model_bake_close(host, in[0].i);
    return NULL;
}
#define P(N, K, T)                                                             \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = K, .type_name = T }
#define EXEC(N)                                                                \
    { .name = N, .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW }
#define INT(N) P(N, PIN_I32, "int")
#define FLOAT(N) P(N, PIN_F32, "float")
#define BOOL(N) P(N, PIN_BOOL, "bool")
#define TEXT(N) P(N, PIN_STR, "const char *")
#define ARRAY(N, T)                                                            \
    { .name = N,                                                               \
      .kind = NATIVE_PARAMETER_DATA,                                           \
      .storage = PIN_STRUCT,                                                   \
      .type_name = T,                                                          \
      .container = PIN_CONTAINER_ARRAY }
static const native_parameter_definition INSPECT_IN[] = { EXEC("Exec"),
                                                          INT("Document"),
                                                          INT("Revision") };
static const native_parameter_definition INSPECT_OUT[] = {
    EXEC("Then"),
    BOOL("Valid"),
    P("Info", PIN_STRUCT, "ModelAuthoringInfo"),
    ARRAY("Parameters", "ModelAuthoringParameter"),
    ARRAY("Parts", "ModelAuthoringPart"),
    ARRAY("Anchors", "ModelAuthoringAnchor"),
    TEXT("Error")
};
static const native_parameter_definition DEFAULT_IN[] = {
    EXEC("Exec"),    INT("Document"),
    INT("Revision"), P("Node", PIN_U64, "uint64"),
    INT("Width"),    FLOAT("X"),
    FLOAT("Y"),      FLOAT("Z"),
    FLOAT("W")
};
static const native_parameter_definition PREPARE_OUT[] = { EXEC("Then"),
                                                           BOOL("Valid"),
                                                           TEXT("Text"),
                                                           TEXT("Error") };
static const native_parameter_definition DEFAULT_OUT[] = { EXEC("Then"),
                                                           BOOL("Valid"),
                                                           INT("Revision"),
                                                           TEXT("Error") };
static const native_parameter_definition BAKE_IN[] = { EXEC("Exec"),
                                                       INT("Document"),
                                                       INT("Revision"),
                                                       FLOAT("CellSize"),
                                                       INT("MaxTriangles"),
                                                       P("MaxSamples", PIN_U64, "uint64"),
                                                       P("MaxWork", PIN_U64, "uint64") };
static const native_parameter_definition BAKE_OUT[] = { EXEC("Then"),
                                                        BOOL("Valid"),
                                                        INT("Bake"),
                                                        INT("Triangles"),
                                                        P("Samples", PIN_U64, "uint64"),
                                                        P("Work", PIN_U64, "uint64"),
                                                        FLOAT("CellDiagonal"),
                                                        FLOAT("MaxResidual"),
                                                        TEXT("Error") };
static const native_parameter_definition EXPORT_IN[] = { EXEC("Exec"),
                                                         INT("Bake"),
                                                         TEXT("Path") };
static const native_parameter_definition EXPORT_OUT[] = { EXEC("Then"),
                                                          BOOL("Valid"),
                                                          TEXT("Error") };
static const native_parameter_definition CLOSE_IN[] = { EXEC("Exec"), INT("Bake") };
static const native_parameter_definition CLOSE_OUT[] = { EXEC("Then"), BOOL("Valid") };
static const native_parameter_definition REFERENCE_IN[] = { EXEC("Exec"),
                                                            INT("Document"),
                                                            INT("Revision") };
static const native_parameter_definition REFERENCE_OUT[] = {
    EXEC("Then"),
    BOOL("Valid"),
    P("Reference", PIN_STRUCT, "ModelReference"),
    TEXT("Error")
};
static const native_parameter_definition REFERENCE_EDIT_IN[] = {
    EXEC("Exec"),
    INT("Document"),
    INT("Revision"),
    P("Reference", PIN_STRUCT, "ModelReference")
};
#define OPS(X)                                                                                              \
    X("Export Model Bundle", "export_model_bundle", export_bundle, EXPORT_IN, EXPORT_OUT)                   \
    X("Inspect Model Reference", "inspect_model_reference", inspect_reference, REFERENCE_IN, REFERENCE_OUT) \
    X("Prepare Model Reference",                                                                            \
      "prepare_model_reference",                                                                            \
      prepare_reference,                                                                                    \
      REFERENCE_EDIT_IN,                                                                                    \
      PREPARE_OUT)                                                                                          \
    X("Bake Model", "bake_model", bake_model, BAKE_IN, BAKE_OUT)                                            \
    X("Export Model Bake", "export_model_bake", export_bake, EXPORT_IN, EXPORT_OUT)                         \
    X("Close Model Bake", "close_model_bake", close_bake, CLOSE_IN, CLOSE_OUT)                              \
                                                                                                            \
    X("Inspect Model", "inspect_model", inspect_model, INSPECT_IN, INSPECT_OUT)                             \
    X("Prepare Model Default", "prepare_model_default", prepare_default, DEFAULT_IN, PREPARE_OUT)           \
    X("Set Model Default", "set_model_default", set_default, DEFAULT_IN, DEFAULT_OUT)
#define CALL(N, S, F, IN, OUT)                                                 \
    static const char *F##_dispatch(                                           \
        const behavior_function_context *ctx,                                  \
        void *host,                                                            \
        const pin_value *in,                                                   \
        pin_value *out                                                         \
    )                                                                          \
    {                                                                          \
        if (!host) {                                                           \
            return "Model authoring requires an authoring host";               \
        }                                                                      \
        return F(ctx, host, in, out);                                          \
    }                                                                          \
    static const node_host_call F##_call = {                                   \
        .service = "visual.model",                                             \
        .optional_service = true,                                              \
        .missing_service = "Model authoring requires an authoring host",       \
        .context = F##_dispatch                                                \
    };
OPS(CALL)
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
#define DEF(N, S, F, IN, OUT)                                                  \
    { .canonical_name = N,                                                     \
      .source_name = S,                                                        \
      .kind = SCRIPT_CAPABILITY_ACTION,                                        \
      .inputs = IN,                                                            \
      .input_count = NATIVE_COUNT(IN),                                         \
      .outputs = OUT,                                                          \
      .output_count = NATIVE_COUNT(OUT),                                       \
      .completion_output = 0 },
static const native_capability_definition DEFINITIONS[] = { OPS(DEF) };
#define BIND(N, S, F, IN, OUT)                                                 \
    { .canonical_name = N,                                                     \
      .implementation = { .call = &F##_call, .access = &ACCESS } },
static const native_definition_binding BINDINGS[] = { OPS(BIND) };
static const type_desc *const LAYOUTS[] = { &VISUAL_MODEL_PARAMETER_LAYOUT,
                                            &VISUAL_MODEL_INFO_LAYOUT,
                                            &VISUAL_MODEL_PART_LAYOUT,
                                            &VISUAL_MODEL_ANCHOR_LAYOUT,
                                            &VISUAL_MODEL_REFERENCE_LAYOUT };
const native_definition_pack VISUAL_MODEL_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
    .layouts = LAYOUTS,
    .layout_count = NATIVE_COUNT(LAYOUTS)
};
