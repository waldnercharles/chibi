#include "authoring/workspace_documents.h"
#include "authoring/workspace_documents_definition.h"
#include "execution/function.h"
#include "types/array.h"

static const char *copy(const behavior_function_context *context, const char *text)
{
    return context->copy_string(context->value_storage, text ? text : "");
}
static void result(
    const behavior_function_context *context,
    workspace_documents *service,
    int handle,
    workspace_document_status status,
    pin_value *out,
    bool include_text
)
{
    workspace_document_view view = { 0 };
    workspace_document_read(service, handle, &view);
    out[0].b = status == WORKSPACE_DOCUMENT_OK;
    out[1].i = status;
    out[2].str = copy(context, workspace_document_status_text(status));
    out[3].i = view.document;
    out[4].i = view.revision;
    out[5].str = copy(context, include_text ? view.text : "");
    out[6].str = copy(context, view.path);
    out[7].b = view.dirty;
    out[8].b = view.transaction;
    out[9].b = view.can_undo;
    out[10].b = view.can_redo;
    out[11].i = (int)view.bytes;
    out[12].b = view.new_file;
}
static const char *open_call(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    int handle = 0;
    workspace_document_status status = workspace_document_open(service, in[0].str, &handle);
    result(ctx, service, handle, status, out, true);
    return NULL;
}
static const char *memory_call(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    int handle = 0;
    workspace_document_status
        status = workspace_document_memory(service, in[0].str ? in[0].str : "", &handle);
    result(ctx, service, handle, status, out, true);
    return NULL;
}
static const char *read_call(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(service, in[0].i, &view);
    result(ctx, service, in[0].i, status, out, true);
    return NULL;
}
static const char *info_call(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(service, in[0].i, &view);
    result(ctx, service, in[0].i, status, out, false);
    return NULL;
}
static const char *close_call(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    workspace_document_status status = workspace_document_close(service, in[0].i);
    result(ctx, service, in[0].i, status, out, true);
    return NULL;
}
static const char *edit_call(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    workspace_document_status status = workspace_document_edit(
        service,
        in[0].i,
        in[1].i,
        in[2].i,
        in[3].i,
        in[4].str ? in[4].str : ""
    );
    result(ctx, service, in[0].i, status, out, true);
    return NULL;
}
static const char *create_call(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    int handle = 0;
    workspace_document_status status = workspace_document_create(
        service,
        in[0].str,
        in[1].str ? in[1].str : "",
        &handle
    );
    result(ctx, service, handle, status, out, true);
    return NULL;
}
static const char *draft_call(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    int handle = 0;
    workspace_document_status status = workspace_document_draft(
        service,
        in[0].str,
        in[1].str ? in[1].str : "",
        &handle
    );
    result(ctx, service, handle, status, out, true);
    return NULL;
}
#define PATH_CALL(ID)                                                               \
    static const char *ID##_call(                                                   \
        const behavior_function_context *ctx,                                       \
        void *service,                                                              \
        const pin_value *in,                                                        \
        pin_value *out                                                              \
    )                                                                               \
    {                                                                               \
        workspace_document_status                                                   \
            status = workspace_document_##ID(service, in[0].i, in[1].i, in[2].str); \
        result(ctx, service, in[0].i, status, out, true);                           \
        return NULL;                                                                \
    }
PATH_CALL(save_as)
PATH_CALL(rename)
#undef PATH_CALL
#define REVISION_CALL(ID, NAME)                                                \
    static const char *ID##_call(                                              \
        const behavior_function_context *ctx,                                  \
        void *service,                                                         \
        const pin_value *in,                                                   \
        pin_value *out                                                         \
    )                                                                          \
    {                                                                          \
        workspace_document_status                                              \
            status = workspace_document_##NAME(service, in[0].i, in[1].i);     \
        result(ctx, service, in[0].i, status, out, true);                      \
        return NULL;                                                           \
    }
REVISION_CALL(begin, begin)
REVISION_CALL(commit, commit)
REVISION_CALL(cancel, cancel)
REVISION_CALL(undo, undo)
REVISION_CALL(redo, redo)
REVISION_CALL(save, save)
REVISION_CALL(remove, remove)
REVISION_CALL(reload, reload)
#undef REVISION_CALL
#define CALL(ID, FN)                                                           \
    const node_host_call WORKSPACE_DOCUMENT_##ID##_CALL = {                    \
        .service = "workspace_documents",                                      \
        .context = FN##_call,                                                  \
        .optional_service = true                                               \
    };
CALL(OPEN, open)
CALL(MEMORY, memory)
CALL(READ, read)
CALL(INFO, info)
CALL(CLOSE, close)
CALL(EDIT, edit)
CALL(BEGIN, begin)
CALL(COMMIT, commit)
CALL(CANCEL, cancel)
CALL(UNDO, undo)
CALL(REDO, redo)
CALL(SAVE, save)
CALL(CREATE, create)
CALL(DRAFT, draft)
CALL(SAVE_AS, save_as)
CALL(RENAME, rename)
CALL(REMOVE, remove)
CALL(RELOAD, reload)
#undef CALL
static const char *handles_call(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *,
    pin_value *out
)
{
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    int count = workspace_document_handles(service, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    workspace_document_status status = service ? WORKSPACE_DOCUMENT_OK
                                               : WORKSPACE_DOCUMENT_UNAVAILABLE;
    out[0].b = status == WORKSPACE_DOCUMENT_OK;
    out[1].i = status;
    out[2].str = copy(ctx, workspace_document_status_text(status));
    pin_array *array = pin_array_snapshot_allocate(ctx->value_storage, PIN_I32, "int", count);
    for (int i = 0; i < count; i++) {
        array->items[i] = (pin_value){ .type = PIN_I32, .i = handles[i] };
    }
    out[3].ptr = array;
    return NULL;
}
const node_host_call WORKSPACE_DOCUMENT_HANDLES_CALL = {
    .service = "workspace_documents",
    .context = handles_call,
    .optional_service = true
};
const node_access WORKSPACE_DOCUMENT_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };

static const char *replace_many_call(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    if (!service) {
        out[0].b = false;
        out[1].i = WORKSPACE_DOCUMENT_UNAVAILABLE;
        out[2].str = copy(ctx, workspace_document_status_text(WORKSPACE_DOCUMENT_UNAVAILABLE));
        return NULL;
    }

    const pin_array *handles = in[0].ptr, *revisions = in[1].ptr, *texts = in[2].ptr;
    int count = handles ? handles->count : 0;
    bool valid = count >= 0 && count <= WORKSPACE_DOCUMENT_COUNT_MAX &&
                 (revisions ? revisions->count : 0) == count &&
                 (texts ? texts->count : 0) == count &&
                 (!count || (handles->items && revisions->items && texts->items));
    workspace_document_replacement requests[WORKSPACE_DOCUMENT_COUNT_MAX];
    for (int i = 0; valid && i < count; i++) {
        valid = handles->items[i].type == PIN_I32 &&
                revisions->items[i].type == PIN_I32 &&
                texts->items[i].type == PIN_STR &&
                handles->items[i].container == PIN_CONTAINER_SINGLE &&
                revisions->items[i].container == PIN_CONTAINER_SINGLE &&
                texts->items[i].container == PIN_CONTAINER_SINGLE;
        requests[i] = (workspace_document_replacement){ handles->items[i].i,
                                                        revisions->items[i].i,
                                                        texts->items[i].str };
    }
    workspace_document_status status = valid ? workspace_document_replace_anchored(
                                                   service,
                                                   requests,
                                                   count,
                                                   in[3].i
                                               )
                                             : WORKSPACE_DOCUMENT_INVALID;
    out[0].b = status == WORKSPACE_DOCUMENT_OK;
    out[1].i = status;
    out[2].str = copy(ctx, workspace_document_status_text(status));
    return NULL;
}
const node_host_call WORKSPACE_DOCUMENT_REPLACE_MANY_CALL = {
    .service = "workspace_documents",
    .context = replace_many_call,
    .optional_service = true
};

static const char *path_call(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    bool exists = false;
    int document = 0, revision = 0;
    workspace_document_status
        status = workspace_document_path(service, in[0].str, &exists, &document, &revision);
    out[0].b = !status;
    out[1].i = status;
    out[2].str = copy(ctx, workspace_document_status_text(status));
    out[3].b = exists;
    out[4].i = document;
    out[5].i = revision;
    return NULL;
}
const node_host_call WORKSPACE_DOCUMENT_PATH_CALL = {
    .service = "workspace_documents",
    .context = path_call,
    .optional_service = true
};

static const char *validate_edit_call(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    workspace_document_status status = workspace_document_validate_edit(
        service,
        in[0].i,
        in[1].i,
        in[2].i,
        in[3].i,
        in[4].str ? in[4].str : ""
    );
    out[0].b = !status;
    out[1].i = status;
    out[2].str = copy(ctx, workspace_document_status_text(status));
    return NULL;
}
const node_host_call WORKSPACE_DOCUMENT_VALIDATE_EDIT_CALL = {
    .service = "workspace_documents",
    .context = validate_edit_call,
    .optional_service = true
};
static const char *validate_candidates_call(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *in,
    pin_value *out
)
{
    if (!service) {
        out[0].b = false;
        out[1].i = WORKSPACE_DOCUMENT_UNAVAILABLE;
        out[2].str = copy(ctx, workspace_document_status_text(WORKSPACE_DOCUMENT_UNAVAILABLE));
        return NULL;
    }

    const pin_array *handles = in[0].ptr, *revisions = in[1].ptr,
                    *paths = in[2].ptr, *texts = in[3].ptr;
    int count = handles ? handles->count : 0;
    bool valid = count >= 0 && count <= WORKSPACE_DOCUMENT_COUNT_MAX &&
                 (revisions ? revisions->count : 0) == count &&
                 (paths ? paths->count : 0) == count &&
                 (texts ? texts->count : 0) == count &&
                 (!count || (handles->items && revisions->items &&
                             paths->items && texts->items));
    workspace_document_candidate requests[WORKSPACE_DOCUMENT_COUNT_MAX];
    for (int i = 0; valid && i < count; i++) {
        valid = handles->items[i].type == PIN_I32 &&
                revisions->items[i].type == PIN_I32 &&
                paths->items[i].type == PIN_STR && texts->items[i].type == PIN_STR &&
                handles->items[i].container == PIN_CONTAINER_SINGLE &&
                revisions->items[i].container == PIN_CONTAINER_SINGLE &&
                paths->items[i].container == PIN_CONTAINER_SINGLE &&
                texts->items[i].container == PIN_CONTAINER_SINGLE;
        requests[i] = (workspace_document_candidate){ handles->items[i].i,
                                                      revisions->items[i].i,
                                                      paths->items[i].str,
                                                      texts->items[i].str };
    }
    workspace_document_status status = valid ? workspace_document_validate_candidates(
                                                   service,
                                                   requests,
                                                   count,
                                                   in[4].i
                                               )
                                             : WORKSPACE_DOCUMENT_INVALID;
    out[0].b = !status;
    out[1].i = status;
    out[2].str = copy(ctx, workspace_document_status_text(status));
    return NULL;
}
const node_host_call WORKSPACE_DOCUMENT_VALIDATE_CANDIDATES_CALL = {
    .service = "workspace_documents",
    .context = validate_candidates_call,
    .optional_service = true
};
