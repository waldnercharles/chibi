#include "authoring/workspace_resources_definition.h"
#include "execution/function.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>
#define FIELD(OWNER, FIELD, NAME, STORAGE, TYPE)                               \
    { .name = NAME,                                                            \
      .type = STORAGE,                                                         \
      .type_name = TYPE,                                                       \
      .offset = offsetof(OWNER, FIELD),                                        \
      .size = sizeof(((OWNER *)0)->FIELD) }
static const type_field ROW_FIELDS[] = {
    FIELD(workspace_resource_row, key, "Key", PIN_STR, "const char *"),
    FIELD(workspace_resource_row, kind, "Kind", PIN_STR, "const char *"),
    FIELD(workspace_resource_row, name, "Name", PIN_STR, "const char *"),
    FIELD(workspace_resource_row, path, "Path", PIN_STR, "const char *"),
    FIELD(workspace_resource_row, reference, "Reference", PIN_STR, "const char *"),
    FIELD(workspace_resource_row, status, "Status", PIN_STR, "const char *"),
    FIELD(workspace_resource_row, message, "Message", PIN_STR, "const char *"),
    FIELD(workspace_resource_row, bytes, "Bytes", PIN_F64, "double"),
    FIELD(workspace_resource_row, width, "Width", PIN_I32, "int"),
    FIELD(workspace_resource_row, height, "Height", PIN_I32, "int"),
    FIELD(workspace_resource_row, vertices, "Vertices", PIN_I32, "int"),
    FIELD(workspace_resource_row, triangles, "Triangles", PIN_I32, "int"),
    FIELD(workspace_resource_row, metadata, "Metadata", PIN_STR, "const char *"),
    FIELD(workspace_resource_row, unresolved, "Unresolved", PIN_BOOL, "bool"),
};
static const type_field RESULT_FIELDS[] = {
    FIELD(workspace_resource_result, ok, "Ok", PIN_BOOL, "bool"),
    FIELD(workspace_resource_result, code, "Code", PIN_I32, "int"),
    FIELD(workspace_resource_result, message, "Message", PIN_STR, "const char *"),
    FIELD(workspace_resource_result, status, "Status", PIN_STR, "const char *"),
    FIELD(workspace_resource_result, revision, "Revision", PIN_I32, "int"),
    FIELD(workspace_resource_result, total, "Total", PIN_I32, "int"),
    FIELD(workspace_resource_result, complete, "Complete", PIN_BOOL, "bool"),
};
const type_desc WORKSPACE_RESOURCE_ROW_LAYOUT = {
    .name = "WorkspaceResourceRow",
    .size = sizeof(workspace_resource_row),
    .fields = ROW_FIELDS,
    .field_count = sizeof ROW_FIELDS / sizeof *ROW_FIELDS
};
const type_desc WORKSPACE_RESOURCE_RESULT_LAYOUT = {
    .name = "WorkspaceResourceResult",
    .size = sizeof(workspace_resource_result),
    .fields = RESULT_FIELDS,
    .field_count = sizeof RESULT_FIELDS / sizeof *RESULT_FIELDS
};
static const type_desc *const TYPES[] = { &WORKSPACE_RESOURCE_ROW_LAYOUT,
                                          &WORKSPACE_RESOURCE_RESULT_LAYOUT };
const type_catalog_pack WORKSPACE_RESOURCE_TYPE_PACK = {
    .types = TYPES,
    .type_count = sizeof TYPES / sizeof *TYPES
};
const native_type_definition WORKSPACE_RESOURCE_TYPE_DEFINITIONS[2] = {
    { .layout = &WORKSPACE_RESOURCE_ROW_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &WORKSPACE_RESOURCE_RESULT_LAYOUT, .storage = PIN_STRUCT },
};
static void return_status(arena *a, pin_value *out, workspace_resource_result result)
{
    assert(out->type == PIN_STRUCT && out->ptr);
    size_t size = strlen(result.message) + 1;
    char *message = arena_alloc(a, size);
    memcpy(message, result.message, size);
    result.message = message;
    memcpy(out->ptr, &result, sizeof result);
}
static const char *query(
    const behavior_function_context *context,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    pin_array *rows;
    workspace_resource_result result = workspace_resources_query(
        service,
        in[0].i,
        in[1].i,
        in[2].str,
        in[3].str,
        context->value_storage,
        &rows
    );
    return_status(context->value_storage, &out[0], result);
    out[1].ptr = rows;
    return NULL;
}
static const char *refresh(
    const behavior_function_context *context,
    void *service,
    const pin_value *,
    pin_value *out
)
{
    return_status(context->value_storage, &out[0], workspace_resources_refresh(service));
    return NULL;
}
static const char *lookup(
    const behavior_function_context *context,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    workspace_resource_row row;
    workspace_resource_result
        result = workspace_resources_lookup(service, in[0].str, context->value_storage, &row);
    return_status(context->value_storage, &out[0], result);
    assert(out[1].type == PIN_STRUCT && out[1].ptr);
    memcpy(out[1].ptr, &row, sizeof row);
    return NULL;
}
const node_host_call WORKSPACE_RESOURCE_QUERY_CALL = {
    .service = "workspace_resources",
    .optional_service = true,
    .missing_service = "Workspace resources host service is unavailable",
    .context = query
};
const node_host_call WORKSPACE_RESOURCE_REFRESH_CALL = {
    .service = "workspace_resources",
    .optional_service = true,
    .missing_service = "Workspace resources host service is unavailable",
    .context = refresh
};
const node_host_call WORKSPACE_RESOURCE_LOOKUP_CALL = {
    .service = "workspace_resources",
    .optional_service = true,
    .missing_service = "Workspace resources host service is unavailable",
    .context = lookup
};
const node_access WORKSPACE_RESOURCE_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
const node_access WORKSPACE_RESOURCE_REFRESH_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
