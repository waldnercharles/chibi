#include "authoring/workspace_assets_definition.h"
#include "authoring/workspace_assets_scope.h"
#include "execution/function.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "types/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *copy(arena *storage, const char *text)
{
    if (!text) {
        text = "";
    }
    char *out = arena_alloc(storage, strlen(text) + 1);
    strcpy(out, text);
    return out;
}
static const char *template_call(
    const behavior_function_context *context,
    void *,
    const pin_value *in,
    pin_value *out
)
{
    workspace_asset_template_result result = { 0 };
    char error[256] = { 0 };
    out[0].b = workspace_asset_template(in[0].str, in[1].str, &result, error, sizeof error);
    out[1].str = copy(context->value_storage, error);
    out[2].str = copy(context->value_storage, result.path);
    out[3].str = copy(context->value_storage, result.text);
    workspace_asset_template_free(&result);
    return NULL;
}
static const char *kinds_call(
    const behavior_function_context *context,
    void *,
    const pin_value *,
    pin_value *out
)
{
    arena *storage = context->value_storage;
    pin_array *array = pin_array_snapshot_allocate(
        storage,
        PIN_STRUCT,
        "WorkspaceAssetKind",
        workspace_asset_kind_count()
    );
    for (int i = 0; i < workspace_asset_kind_count(); i++) {
        const char *kind = workspace_asset_kind_name(i);
        const asset_schema *schema = asset_schema_find(kind);
        workspace_asset_kind_row *row = arena_alloc(storage, sizeof *row);
        *row = (workspace_asset_kind_row){
            copy(storage, kind),
            copy(storage, workspace_asset_kind_folder(i)),
            copy(storage, workspace_asset_kind_extension(i)),
            copy(
                storage,
                schema ? schema->description
                : i == NEW_ASSET_KIND_COUNT
                    ? "Reusable RML widget with an authored content slot."
                : i == NEW_ASSET_KIND_COUNT + 1 ? "Root RML document."
                                                : "RCSS stylesheet."
            ),
            schema && schema->executable,
            schema && schema->complete
        };
        array->items[i] = (pin_value){ .type = PIN_STRUCT,
                                       .type_name = "WorkspaceAssetKind",
                                       .ptr = row };
    }
    out[0].ptr = array;
    return NULL;
}
const node_host_call WORKSPACE_ASSET_TEMPLATE_CALL = { .context = template_call };
const node_host_call WORKSPACE_ASSET_KINDS_CALL = { .context = kinds_call };
const node_access WORKSPACE_ASSETS_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };

static void inspection_outputs(
    arena *storage,
    const workspace_asset_inspection *result,
    pin_value *fields_out,
    pin_value *choices_out
)
{
    pin_array *fields = pin_array_snapshot_allocate(
        storage,
        PIN_STRUCT,
        "WorkspaceAssetField",
        acount(result->fields)
    );
    for (int i = 0; i < acount(result->fields); i++) {
        workspace_asset_field *row = arena_alloc(storage, sizeof *row);
        *row = result->fields[i];
        row->path = copy(storage, row->path);
        row->name = copy(storage, row->name);
        row->type = copy(storage, row->type);
        row->text = copy(storage, row->text);
        row->json = copy(storage, row->json);
        row->description = copy(storage, row->description);
        row->reference = copy(storage, row->reference);
        row->type_name = copy(storage, row->type_name);
        row->unit = copy(storage, row->unit);
        row->relative = copy(storage, row->relative);
        row->component = copy(storage, row->component);
        row->presentation_category = copy(storage, row->presentation_category);
        row->icon = copy(storage, row->icon);
        fields->items[i] = (pin_value){ .type = PIN_STRUCT,
                                        .type_name = "WorkspaceAssetField",
                                        .ptr = row };
    }
    pin_array *choices = pin_array_snapshot_allocate(
        storage,
        PIN_STRUCT,
        "WorkspaceAssetChoice",
        acount(result->choices)
    );
    for (int i = 0; i < acount(result->choices); i++) {
        workspace_asset_choice *row = arena_alloc(storage, sizeof *row);
        *row = (workspace_asset_choice){ copy(storage, result->choices[i].path),
                                         copy(storage, result->choices[i].value) };
        choices->items[i] = (pin_value){ .type = PIN_STRUCT,
                                         .type_name = "WorkspaceAssetChoice",
                                         .ptr = row };
    }
    fields_out->ptr = fields;
    choices_out->ptr = choices;
}
static const char *inspect_common(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out,
    int object
)
{
    workspace_assets_service *service = host;
    arena *storage = context->value_storage;
    char error[256] = { 0 };
    if (!workspace_assets_service_sync(service, error, sizeof error)) {
        out[0].b = false;
        out[1].str = copy(context->value_storage, error);
        workspace_asset_inspection empty = { 0 };
        inspection_outputs(storage, &empty, &out[2], &out[3]);
        return NULL;
    }
    workspace_asset_inspection result = { 0 };
    const pin_array *targets = object == 2 ? in[3].ptr : NULL;
    const char *paths[256];
    if (object == 2 && (!targets || targets->count < 1 || targets->count > 256)) {
        out[0].b = false;
        out[1].str = copy(storage, "Object inspection requires1to256targets");
        inspection_outputs(storage, &result, &out[2], &out[3]);
        return NULL;
    }
    for (int i = 0; targets && i < targets->count; i++) {
        paths[i] = targets->items[i].str;
    }
    out[0].b = object == 2 ? workspace_objects_inspect(
                                 service->registry,
                                 service->documents,
                                 in[0].i,
                                 in[1].i,
                                 in[2].str,
                                 paths,
                                 targets->count,
                                 &result,
                                 error,
                                 sizeof error
                             )
             : object      ? workspace_object_inspect(
                                 service->registry,
                                 service->documents,
                                 in[0].i,
                                 in[1].i,
                                 in[2].str,
                                 in[3].str,
                                 &result,
                                 error,
                                 sizeof error
                             )
                           : workspace_asset_inspect_registry(
                                 service->registry,
                                 service->documents,
                                 in[0].i,
                                 in[1].i,
                                 in[2].str,
                                 &result,
                                 error,
                                 sizeof error
                             );
    out[1].str = copy(storage, error);
    inspection_outputs(storage, &result, &out[2], &out[3]);
    workspace_asset_inspection_free(&result);
    return NULL;
}
static const char *inspect_call(
    const behavior_function_context *c,
    void *h,
    const pin_value *in,
    pin_value *out
)
{
    return inspect_common(c, h, in, out, false);
}
static const char *object_call(
    const behavior_function_context *c,
    void *h,
    const pin_value *in,
    pin_value *out
)
{
    return inspect_common(c, h, in, out, true);
}
static const char *prepare_asset_call(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_assets_service *service = host;
    char error[512] = "", *text = NULL;
    out[0].b = workspace_assets_service_sync(service, error, sizeof error) &&
               workspace_asset_prepare_registry(
                   service->registry,
                   service->documents,
                   in[0].i,
                   in[1].i,
                   in[2].str,
                   in[3].str,
                   in[4].str,
                   in[5].str,
                   &text,
                   error,
                   sizeof error
               );
    out[1].str = copy(context->value_storage, text ? text : "");
    out[2].str = copy(context->value_storage, error);
    free(text);
    return NULL;
}
const node_host_call WORKSPACE_ASSET_PREPARE_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = prepare_asset_call
};
static const char *edit_call(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_assets_service *service = host;
    char error[256] = { 0 };
    if (!workspace_assets_service_sync(service, error, sizeof error)) {
        out[0].b = false;
        out[1].str = copy(context->value_storage, error);
        return NULL;
    }
    out[0].b = workspace_asset_edit_registry(
        service->registry,
        service->documents,
        in[0].i,
        in[1].i,
        in[2].str,
        in[3].str,
        in[4].str,
        in[5].str,
        error,
        sizeof error
    );
    out[1].str = copy(context->value_storage, error);
    return NULL;
}
const node_host_call WORKSPACE_ASSET_INSPECT_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = inspect_call
};
const node_host_call WORKSPACE_ASSET_EDIT_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = edit_call
};
const node_access WORKSPACE_ASSETS_WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };

static const char *scene_call(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out,
    int operation
)
{
    workspace_assets_service *service = host;
    char error[256] = { 0 };
    if (!workspace_assets_service_sync(service, error, sizeof error)) {
        out[0].b = false;
        out[1].str = copy(context->value_storage, error);
        return NULL;
    }
    const char *entity = operation == WORKSPACE_SCENE_ADD ? "" : in[2].str;
    const char *parent = operation == WORKSPACE_SCENE_ADD  ? in[2].str
                       : operation == WORKSPACE_SCENE_MOVE ? in[3].str
                                                           : "";
    const char *before = operation == WORKSPACE_SCENE_MOVE ? in[4].str : "";
    const char *name = operation == WORKSPACE_SCENE_ADD ||
                               operation == WORKSPACE_SCENE_RENAME ||
                               operation == WORKSPACE_SCENE_DUPLICATE
                         ? in[3].str
                         : "";
    const char *prototype = operation == WORKSPACE_SCENE_ADD ? in[4].str : "";
    out[0].b = workspace_scene_edit_registry(
        service->registry,
        service->documents,
        in[0].i,
        in[1].i,
        operation,
        entity,
        parent,
        before,
        name,
        prototype,
        error,
        sizeof error
    );
    out[1].str = copy(context->value_storage, error);
    return NULL;
}
#define SCENE_CALLBACK(N)                                                                      \
    static const char *                                                                        \
    scene##N(const behavior_function_context *c, void *s, const pin_value *in, pin_value *out) \
    {                                                                                          \
        return scene_call(c, s, in, out, N);                                                   \
    }
SCENE_CALLBACK(0) SCENE_CALLBACK(1) SCENE_CALLBACK(2) SCENE_CALLBACK(3) SCENE_CALLBACK(4)
#define SCENE_CALL(N)                                                          \
    { .service = "workspace_assets",                                           \
      .optional_service = true,                                                \
      .missing_service = "Workspace asset service unavailable",                \
      .context = scene##N },
const node_host_call WORKSPACE_SCENE_CALLS[5]={SCENE_CALL(0) SCENE_CALL(1) SCENE_CALL(2) SCENE_CALL(3) SCENE_CALL(4)};

static const char *identity_call(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_assets_service *service = host;
    char *kind = NULL, *name = NULL, error[256] = { 0 };
    if (!service || !service->documents) {
        snprintf(error, sizeof error, "Workspace asset service unavailable");
        out[0].b = false;
    } else {
        out[0].b = workspace_asset_identity(service->documents, in[0].i, in[1].i, &kind, &name, error, sizeof error);
    }
    out[1].str = copy(context->value_storage, kind);
    out[2].str = copy(context->value_storage, name);
    out[3].str = copy(context->value_storage, error);
    free(kind);
    free(name);
    return NULL;
}
const node_host_call WORKSPACE_ASSET_IDENTITY_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = identity_call
};

static const char *components_call(
    const behavior_function_context *context,
    void *host,
    const pin_value *,
    pin_value *out
)
{
    arena *storage = context->value_storage;
    workspace_assets_service *service = host;
    char error[256] = { 0 };
    bool valid = workspace_assets_service_sync(service, error, sizeof error);
    out[1].b = valid;
    out[2].str = copy(storage, error);
    if (!valid) {
        out[0].ptr = pin_array_snapshot_allocate(storage, PIN_STRUCT, "WorkspaceComponent", 0);
        return NULL;
    }
    prototype_registry native = { .components = REGISTRY_COMPONENTS,
                                  .component_count = REGISTRY_COMPONENT_COUNT };
    const prototype_registry *registry = service->registry ? service->registry : &native;
    int count = 0;
    for (int i = 0; i < registry->component_count; i++) {
        if (!registry->components[i]->derived_from) {
            count++;
        }
    }
    pin_array *array = pin_array_snapshot_allocate(storage, PIN_STRUCT, "WorkspaceComponent", count);
    int at = 0;
    for (int i = 0; i < registry->component_count; i++) {
        const ecs_component_desc *desc = registry->components[i];
        if (desc->derived_from) {
            continue;
        }
        workspace_component_row *row = arena_alloc(storage, sizeof *row);
        *row = (workspace_component_row){ copy(storage, desc->key),
                                          copy(storage, desc->name),
                                          desc->type ? desc->type->field_count : 0,
                                          desc->type && desc->type->field_count == 0 };
        array->items[at++] = (pin_value){ .type = PIN_STRUCT,
                                          .type_name = "WorkspaceComponent",
                                          .ptr = row };
    }
    out[0].ptr = array;
    return NULL;
}
static const char *component_call(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_assets_service *service = host;
    char error[256] = { 0 };
    if (!workspace_assets_service_sync(service, error, sizeof error)) {
        out[0].b = false;
        out[1].str = copy(context->value_storage, error);
        return NULL;
    }
    out[0].b = workspace_asset_component_registry(
        service->registry,
        service->documents,
        in[0].i,
        in[1].i,
        in[2].str,
        in[3].str,
        in[4].str,
        in[5].b,
        error,
        sizeof error
    );
    out[1].str = copy(context->value_storage, error);
    return NULL;
}
const node_host_call WORKSPACE_COMPONENT_CATALOG_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = components_call
};
const node_host_call WORKSPACE_ASSET_COMPONENT_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = component_call
};

static const char *objects_edit_call(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_assets_service *service = host;
    const pin_array *array = in[3].ptr;
    const char *paths[256];
    char error[256] = { 0 };
    if (!workspace_assets_service_sync(service, error, sizeof error)) {
        out[0].b = false;
        out[1].str = copy(context->value_storage, error);
        return NULL;
    }
    if (!array || array->count < 1 || array->count > 256) {
        out[0].b = false;
        out[1].str = copy(context->value_storage, "Object edits require1to256targets");
        return NULL;
    }
    for (int i = 0; i < array->count; i++) {
        paths[i] = array->items[i].str;
    }
    out[0].b = workspace_objects_edit(
        service->registry,
        service->documents,
        in[0].i,
        in[1].i,
        in[2].str,
        paths,
        array->count,
        in[4].str,
        in[5].str,
        in[6].str,
        error,
        sizeof error
    );
    out[1].str = copy(context->value_storage, error);
    return NULL;
}
const node_host_call WORKSPACE_OBJECT_INSPECT_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = object_call
};
const node_host_call WORKSPACE_OBJECTS_EDIT_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = objects_edit_call
};

static const char *objects_call(
    const behavior_function_context *c,
    void *h,
    const pin_value *in,
    pin_value *out
)
{
    return inspect_common(c, h, in, out, 2);
}
const node_host_call WORKSPACE_OBJECTS_INSPECT_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = objects_call
};

static const char *objects_component_call(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_assets_service *service = host;
    const pin_array *array = in[3].ptr;
    const char *paths[256];
    char error[256] = { 0 };
    if (!workspace_assets_service_sync(service, error, sizeof error)) {
        out[0].b = false;
        out[1].str = copy(context->value_storage, error);
        return NULL;
    }
    if (!array || array->count < 1 || array->count > 256) {
        out[0].b = false;
        out[1].str = copy(context->value_storage, "Component edits require1to256targets");
        return NULL;
    }
    for (int i = 0; i < array->count; i++) {
        paths[i] = array->items[i].str;
    }
    out[0].b = workspace_objects_component(
        service->registry,
        service->documents,
        in[0].i,
        in[1].i,
        in[2].str,
        paths,
        array->count,
        in[4].str,
        in[5].b,
        error,
        sizeof error
    );
    out[1].str = copy(context->value_storage, error);
    return NULL;
}
const node_host_call WORKSPACE_OBJECTS_COMPONENT_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = objects_component_call
};

static const char *objects_candidate_call(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_assets_service *service = host;
    const pin_array *array = in[3].ptr;
    const char *paths[256];
    char *text = NULL, error[256] = { 0 };
    bool valid = workspace_assets_service_sync(service, error, sizeof error);
    if (valid && (!array || array->count < 1 || array->count > 256)) {
        valid = false;
        snprintf(error, sizeof error, "Object candidates require 1 to 256 targets");
    }
    if (valid) {
        for (int i = 0; i < array->count; i++) {
            paths[i] = array->items[i].str;
        }
        valid = workspace_objects_candidate(
            service->registry,
            service->documents,
            in[0].i,
            in[1].i,
            in[2].str,
            paths,
            array->count,
            in[4].str,
            in[5].str,
            in[6].str,
            in[7].str,
            &text,
            error,
            sizeof error
        );
    }
    workspace_asset_inspection effective = { 0 };
    char *replacement = NULL;
    int start = 0, end = 0;
    if (valid) {
        workspace_document_view original;
        valid = workspace_document_read(service->documents, in[0].i, &original) ==
                    WORKSPACE_DOCUMENT_OK &&
                workspace_candidate_difference(original.text, text, &start, &end, &replacement);
        workspace_documents *candidate = workspace_documents_new(NULL, NULL);
        int document = 0;
        if (valid) {
            valid = candidate &&
                    workspace_document_memory(candidate, text, &document) ==
                        WORKSPACE_DOCUMENT_OK &&
                    workspace_objects_inspect(
                        service->registry,
                        candidate,
                        document,
                        1,
                        in[2].str,
                        paths,
                        array->count,
                        &effective,
                        error,
                        sizeof error
                    );
        }
        workspace_documents_free(candidate);
    }
    out[3].i = start;
    out[4].i = end;
    out[5].str = copy(context->value_storage, replacement);
    inspection_outputs(context->value_storage, &effective, &out[6], &out[7]);
    workspace_asset_inspection_free(&effective);
    free(replacement);
    out[0].b = valid;
    out[1].str = copy(context->value_storage, text);
    out[2].str = copy(context->value_storage, error);
    free(text);
    return NULL;
}
const node_host_call WORKSPACE_OBJECTS_CANDIDATE_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = objects_candidate_call
};

static const char *prototype_source_call(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    const char *path = NULL, *text = NULL;
    char error[256] = { 0 };
    out[0].b = workspace_assets_service_prototype_source(host, in[0].str, &path, &text, error, sizeof error);
    out[1].str = copy(context->value_storage, path);
    out[2].str = copy(context->value_storage, text);
    out[3].str = copy(context->value_storage, error);
    return NULL;
}
static const char *prototype_candidate_call(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_assets_service *service = host;
    const pin_array *array = in[6].ptr;
    const char *paths[256];
    char *object = NULL, *prototype = NULL, *path = NULL, error[256] = { 0 };
    bool ok = workspace_assets_service_sync(service, error, sizeof error);
    if (ok && array && array->count > 256) {
        ok = false;
        snprintf(error, sizeof error, "Prototype actions accept at most 256 fields");
    }
    int count = array ? array->count : 0;
    if (ok) {
        for (int i = 0; i < count; i++) {
            paths[i] = array->items[i].str;
        }
        ok = workspace_prototype_candidate(
            service->registry,
            service->documents,
            in[0].i,
            in[1].i,
            in[2].str,
            in[3].str,
            in[4].i,
            in[5].i,
            paths,
            count,
            in[7].str,
            in[8].str,
            &object,
            &prototype,
            &path,
            error,
            sizeof error
        );
    }
    out[0].b = ok;
    out[1].str = copy(context->value_storage, error);
    out[2].str = copy(context->value_storage, object);
    out[3].str = copy(context->value_storage, prototype);
    out[4].str = copy(context->value_storage, path);
    free(object);
    free(prototype);
    free(path);
    return NULL;
}
const node_host_call WORKSPACE_PROTOTYPE_SOURCE_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = prototype_source_call
};
const node_host_call WORKSPACE_PROTOTYPE_CANDIDATE_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = prototype_candidate_call
};

static const char *clipboard_call(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_assets_service *service = host;
    const pin_array *array = in[2].ptr;
    char error[256] = { 0 }, *payload = NULL, *text = NULL;
    const char *entities[256];
    bool ok = workspace_assets_service_sync(service, error, sizeof error);
    int count = array ? array->count : 0;
    if (ok && (count < 1 || count > 256)) {
        ok = false;
        snprintf(error, sizeof error, "Clipboard requires one to 256 selected objects");
    }
    if (ok) {
        for (int i = 0; i < count; i++) {
            entities[i] = array->items[i].str;
        }
        ok = workspace_scene_clipboard(
            service->registry,
            service->documents,
            in[0].i,
            in[1].i,
            entities,
            count,
            in[3].b,
            &payload,
            &text,
            error,
            sizeof error
        );
    }
    out[0].b = ok;
    out[1].str = copy(context->value_storage, error);
    out[2].str = copy(context->value_storage, payload);
    out[3].str = copy(context->value_storage, text);
    free(payload);
    free(text);
    return NULL;
}
static const char *paste_call(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_assets_service *service = host;
    char error[256] = { 0 }, *text = NULL, **paths = NULL;
    int count = 0;
    bool ok = workspace_assets_service_sync(service, error, sizeof error);
    if (ok) {
        ok = workspace_scene_paste(
            service->registry,
            service->documents,
            in[0].i,
            in[1].i,
            in[2].str,
            in[3].str,
            &text,
            &paths,
            &count,
            error,
            sizeof error
        );
    }
    out[0].b = ok;
    out[1].str = copy(context->value_storage, error);
    out[2].str = copy(context->value_storage, text);
    pin_array *array = pin_array_snapshot_allocate(context->value_storage, PIN_STR, "const char *", count);
    for (int i = 0; i < count; i++) {
        array->items[i].str = copy(context->value_storage, paths[i]);
        free(paths[i]);
    }
    out[3].ptr = array;
    free(paths);
    free(text);
    return NULL;
}
const node_host_call WORKSPACE_SCENE_CLIPBOARD_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = clipboard_call
};
const node_host_call WORKSPACE_SCENE_PASTE_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = paste_call
};

static const char *place_call(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    workspace_assets_service *service = host;
    char error[256] = { 0 }, *text = NULL, *replacement = NULL, **paths = NULL;
    int count = 0, start = 0, end = 0;
    bool ok = workspace_assets_service_sync(service, error, sizeof error);
    if (ok) {
        ok = workspace_scene_place_candidate(
            service->registry,
            service->documents,
            in[0].i,
            in[1].i,
            in[2].str,
            in[3].str,
            in[4].str,
            in[5].f,
            in[6].f,
            in[7].f,
            in[8].str,
            &text,
            &paths,
            &count,
            error,
            sizeof error
        );
    }
    workspace_document_view original;
    if (ok) {
        ok = workspace_document_read(service->documents, in[0].i, &original) ==
                 WORKSPACE_DOCUMENT_OK &&
             original.revision == in[1].i &&
             workspace_candidate_difference(original.text, text, &start, &end, &replacement);
        if (!ok) {
            snprintf(error, sizeof error, "Placement source changed before candidate difference");
        }
    }
    out[0].b = ok;
    out[1].str = copy(context->value_storage, ok ? text : "");
    out[2].i = start;
    out[3].i = end;
    out[4].str = copy(context->value_storage, replacement);
    pin_array *array = pin_array_snapshot_allocate(
        context->value_storage,
        PIN_STR,
        "const char *",
        ok ? count : 0
    );
    for (int i = 0; i < count; i++) {
        if (ok) {
            array->items[i].str = copy(context->value_storage, paths[i]);
        }
        free(paths[i]);
    }
    out[5].ptr = array;
    out[6].str = copy(context->value_storage, error);
    free(paths);
    free(text);
    free(replacement);
    return NULL;
}
const node_host_call WORKSPACE_SCENE_PLACE_CALL = {
    .service = "workspace_assets",
    .optional_service = true,
    .missing_service = "Workspace asset service unavailable",
    .context = place_call
};
