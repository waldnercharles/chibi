#include "authoring/visual_authoring_preview.h"
#include "authoring/visual_authoring_preview_definition.h"
#include "authoring/workspace_resources.h"
#include "execution/function.h"
#include "types/array.h"

#include <stdio.h>
static const char *model_reference_call(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_preview_service *s = host;
    char error[256] = "";
    out[0].b = visual_preview_model_reference(
        s->previews,
        in[0].i,
        in[1].i,
        in[2].ptr,
        in[3].b,
        in[4].f,
        error,
        sizeof error
    );
    out[1].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *model_pan(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_preview_service *s = host;
    char error[256] = "";
    out[0].b = visual_preview_model_pan(
        s->previews,
        in[0].i,
        in[1].i,
        in[2].f,
        in[3].f,
        error,
        sizeof error
    );
    out[1].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *model_inspection_call(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_preview_service *s = host;
    char error[256];
    out[0].b = visual_preview_model_inspection(
        s->previews,
        in[0].i,
        in[1].i,
        in[2].i,
        in[3].i,
        in[4].f,
        error,
        sizeof error
    );
    out[1].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *refresh_catalog(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    (void)in;
    visual_preview_service *service = host;
    char error[512] = "";
    workspace_resource_result inventory = service->resources ? workspace_resources_refresh(
                                                                   service->resources
                                                               )
                                                             : (workspace_resource_result){
                                                                   .message = "Visual catalog has no resource inventory"
                                                               };
    out[0].b = inventory.ok && inventory.complete &&
               visual_previews_refresh_catalog(service->previews, service->resources, error, sizeof error);
    if (!inventory.ok || !inventory.complete) {
        snprintf(error, sizeof error, "%s", inventory.message[0] ? inventory.message : "Visual resource inventory is incomplete");
    }
    out[1].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *publish_documents(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_preview_service *service = host;
    const pin_array *array = in[0].ptr;
    int count = array ? array->count : 0;
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    visual_publication_result results[WORKSPACE_DOCUMENT_COUNT_MAX];
    char error[256] = "";
    bool input_valid = count >= 0 && count <= WORKSPACE_DOCUMENT_COUNT_MAX;
    bool valid = input_valid;
    if (valid) {
        for (int i = 0; i < count; i++) {
            handles[i] = array->items[i].i;
        }
    }
    if (valid) {
        valid = visual_publish_documents(service->previews, handles, count, results, error, sizeof error);
    }
    if (count < 0 || count > WORKSPACE_DOCUMENT_COUNT_MAX) {
        snprintf(error, sizeof error, "Visual publication accepts at most 64 document handles");
    }
    pin_array *rows = arena_alloc(ctx->value_storage, sizeof *rows);
    *rows = (pin_array){ .element_type = PIN_STRUCT,
                         .type_name = VISUAL_PUBLICATION_LAYOUT.name,
                         .count = input_valid ? count : 0,
                         .capacity = input_valid ? count : 0 };
    rows->items = rows->count ? arena_alloc(
                                    ctx->value_storage,
                                    (size_t)rows->count * sizeof *rows->items
                                )
                              : NULL;
    int published = 0;
    for (int i = 0; i < rows->count; i++) {
        visual_publication_row *row = arena_alloc(ctx->value_storage, sizeof *row);
        *row = (visual_publication_row){
            results[i].document,
            results[i].revision,
            results[i].valid,
            ctx->copy_string(ctx->value_storage, results[i].error)
        };
        rows->items[i] = (pin_value){ .type = PIN_STRUCT,
                                      .type_name = VISUAL_PUBLICATION_LAYOUT.name,
                                      .ptr = row };
        published += results[i].valid;
    }
    out[0].b = valid;
    out[1].i = published;
    out[2].i = input_valid ? count - published : 0;
    out[3].ptr = rows;
    out[4].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *publish_document(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_preview_service *service = host;
    char error[512] = "";
    out[0].b = visual_preview_publish(service->previews, in[0].i, in[1].i, error, sizeof error);
    out[1].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *open_preview(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_preview_service *service = host;
    char error[256] = "";
    int handle = 0;
    visual_preview_open(service->previews, in[0].i, in[1].i, in[2].i, &handle, error, sizeof error);
    out[0].i = handle;
    out[1].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *draw_preview(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_preview_service *service = host;
    out[0].b = visual_preview_draw(
        service->previews,
        in[0].i,
        in[1].i,
        in[2].f,
        in[3].f,
        in[4].f,
        in[5].f
    );
    visual_preview_view view = { 0 };
    bool found = visual_preview_read(service->previews, in[0].i, &view);
    char image[64] = "";
    if (out[0].b) {
        snprintf(image, sizeof image, "kit:preview/%d", in[0].i);
    }
    out[1].str = ctx->copy_string(ctx->value_storage, image);
    out[2].str = ctx->copy_string(ctx->value_storage, found ? view.error : "Invalid preview handle");
    return NULL;
}
static const char *close_preview(
    const behavior_function_context *,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_preview_service *service = host;
    out[0].b = visual_preview_close(service->previews, in[0].i);
    return NULL;
}
static const char *export_preview(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_preview_service *service = host;
    char error[512] = "";
    int job = 0;
    visual_preview_export(
        service->previews,
        in[0].i,
        in[1].str,
        service->engine_version,
        service->build_id,
        service->manifest_hash,
        &job,
        error,
        sizeof error
    );
    out[0].i = job;
    out[1].str = ctx->copy_string(ctx->value_storage, error);
    return NULL;
}
static const char *job_status(
    const behavior_function_context *ctx,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_preview_service *service = host;
    visual_evidence_view view;
    bool found = visual_evidence_read(service->previews, in[0].i, &view);
    out[0].i = found ? (int)view.state : (int)VISUAL_EVIDENCE_FAILED;
    out[1].str = ctx->copy_string(ctx->value_storage, found ? view.destination : "");
    out[2].str = ctx->copy_string(ctx->value_storage, found ? view.error : "Invalid evidence job handle");
    return NULL;
}
static const char *close_job(
    const behavior_function_context *,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    visual_preview_service *service = host;
    out[0].b = visual_evidence_close(service->previews, in[0].i);
    return NULL;
}
#define OPS(X)                                                                 \
    X("Set Model Preview Reference", model_reference_call)                     \
    X("Set Model Preview Pan", model_pan)                                      \
    X("Set Visual Model Inspection", model_inspection_call)                    \
    X("Refresh Visual Catalog", refresh_catalog)                               \
    X("Publish Visual Documents", publish_documents)                           \
                                                                               \
    X("Publish Visual Document", publish_document)                             \
    X("Open Visual Preview", open_preview)                                     \
    X("Draw Visual Preview", draw_preview)                                     \
    X("Close Visual Preview", close_preview)                                   \
    X("Export Visual Evidence", export_preview)                                \
    X("Visual Evidence Status", job_status)                                    \
    X("Close Visual Evidence", close_job)
#define CALL(NAME, FN)                                                         \
    static const char *FN##_dispatch(                                          \
        const behavior_function_context *ctx,                                  \
        void *host,                                                            \
        const pin_value *in,                                                   \
        pin_value *out                                                         \
    )                                                                          \
    {                                                                          \
        if (!host) {                                                           \
            return "Visual previews require a GPU authoring host";             \
        }                                                                      \
        return FN(ctx, host, in, out);                                         \
    }                                                                          \
    static const node_host_call FN##_call = {                                  \
        .service = "visual.previews",                                          \
        .optional_service = true,                                              \
        .missing_service = "Visual previews require a GPU authoring host",     \
        .context = FN##_dispatch                                               \
    };
OPS(CALL)
static const node_access ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
#define BIND(NAME, FN)                                                         \
    { .canonical_name = NAME,                                                  \
      .implementation = { .call = &FN##_call, .access = &ACCESS } },
static const native_definition_binding BINDINGS[] = { OPS(BIND) };
static const type_desc *const LAYOUTS[] = { &VISUAL_PUBLICATION_LAYOUT };
const native_definition_pack VISUAL_PREVIEW_DEFINITION_PACK = {
    .definitions = VISUAL_PREVIEW_DEFINITIONS,
    .definition_count = VISUAL_PREVIEW_OPERATION_COUNT,
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
    .layouts = LAYOUTS,
    .layout_count = NATIVE_COUNT(LAYOUTS)
};
