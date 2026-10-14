#include "authoring/ui_authoring_definition.h"
#include "execution/function.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>
#define FIELD(T, F, N, P, S)                                                   \
    { .name = N,                                                               \
      .type = P,                                                               \
      .type_name = S,                                                          \
      .offset = offsetof(T, F),                                                \
      .size = sizeof(((T *)0)->F) }
#define R(F, N, P, S) FIELD(ui_authoring_row, F, N, P, S)
static const type_field ROW_FIELDS[] = { R(index, "Index", PIN_I32, "int"),
                                         R(parent, "Parent", PIN_I32, "int"),
                                         R(depth, "Depth", PIN_I32, "int"),
                                         R(start, "Start", PIN_I32, "int"),
                                         R(end, "End", PIN_I32, "int"),
                                         R(line, "Line", PIN_I32, "int"),
                                         R(name, "Name", PIN_STR, "const char *"),
                                         R(value, "Value", PIN_STR, "const char *"),
                                         R(extra, "Extra", PIN_STR, "const char *"),
                                         R(binding, "Binding", PIN_BOOL, "bool") };
#define S(F, N, P, T) FIELD(ui_authoring_status, F, N, P, T)
static const type_field STATUS_FIELDS[] = { S(ok, "Ok", PIN_BOOL, "bool"),
                                            S(valid, "Valid", PIN_BOOL, "bool"),
                                            S(code, "Code", PIN_I32, "int"),
                                            S(document, "Document", PIN_I32, "int"),
                                            S(revision, "Revision", PIN_I32, "int"),
                                            S(total, "Total", PIN_I32, "int"),
                                            S(error, "Error", PIN_STR, "const char *") };
#define COUNT(A) ((int)(sizeof A / sizeof *A))
const type_desc UI_AUTHORING_ROW_LAYOUT = { .name = "UiSourceRow",
                                            .size = sizeof(ui_authoring_row),
                                            .fields = ROW_FIELDS,
                                            .field_count = COUNT(ROW_FIELDS) };
const type_desc UI_AUTHORING_STATUS_LAYOUT = { .name = "UiSourceStatus",
                                               .size = sizeof(ui_authoring_status),
                                               .fields = STATUS_FIELDS,
                                               .field_count = COUNT(STATUS_FIELDS) };
static const type_desc *const TYPES[] = { &UI_AUTHORING_ROW_LAYOUT,
                                          &UI_AUTHORING_STATUS_LAYOUT };
const type_catalog_pack UI_AUTHORING_TYPE_PACK = { .types = TYPES,
                                                   .type_count = COUNT(TYPES) };
const native_type_definition UI_AUTHORING_TYPE_DEFINITIONS[2] = {
    { .layout = &UI_AUTHORING_ROW_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &UI_AUTHORING_STATUS_LAYOUT, .storage = PIN_STRUCT }
};
static void returned(const behavior_function_context *ctx, pin_value *out, ui_authoring_status result)
{
    assert(out->type == PIN_STRUCT && out->ptr);
    result.error = ctx->copy_string(ctx->value_storage, result.error);
    memcpy(out->ptr, &result, sizeof result);
}
static const char *query(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    if (!service) {
        returned(
            ctx,
            out,
            (ui_authoring_status){
                .code = WORKSPACE_DOCUMENT_UNAVAILABLE,
                .error = "UI source authoring requires an active workspace" }
        );
        out[1].ptr = pin_array_snapshot_allocate(ctx->value_storage, PIN_STRUCT, "UiSourceRow", 0);
        return NULL;
    }
    pin_array *rows;
    ui_authoring_status result = ui_authoring_query(
        service,
        in[0].i,
        in[1].i,
        in[2].str,
        in[3].str,
        in[4].i,
        in[5].i,
        in[6].i,
        ctx->value_storage,
        &rows
    );
    returned(ctx, out, result);
    out[1].ptr = rows;
    return NULL;
}
static const char *edit(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    if (!service) {
        returned(
            ctx,
            out,
            (ui_authoring_status){
                .code = WORKSPACE_DOCUMENT_UNAVAILABLE,
                .error = "UI source authoring requires an active workspace" }
        );
        return NULL;
    }
    returned(
        ctx,
        out,
        ui_authoring_edit(
            service,
            in[0].i,
            in[1].i,
            in[2].str,
            in[3].i,
            in[4].i,
            in[5].str,
            in[6].str,
            in[7].str,
            in[8].b
        )
    );
    return NULL;
}
const node_host_call
    UI_AUTHORING_QUERY_CALL = { .service = "ui_authoring",
                                .optional_service = true,
                                .missing_service = "UI source authoring "
                                                   "service unavailable",
                                .context = query },
    UI_AUTHORING_EDIT_CALL = {
        .service = "ui_authoring",
        .optional_service = true,
        .missing_service = "UI source authoring service unavailable",
        .context = edit
    };
const node_access UI_AUTHORING_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
