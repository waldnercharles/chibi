#include "authoring/workspace_info.h"
#include "authoring/workspace_info_definition.h"
#include "execution/function.h"

#include <string.h>
static const char *copy(arena *storage, const char *text)
{
    if (!text) {
        text = "";
    }
    char *out = arena_alloc(storage, strlen(text) + 1);
    strcpy(out, text);
    return out;
}
static const char *query(
    const behavior_function_context *context,
    void *service,
    const pin_value *,
    pin_value *out
)
{
    arena *storage = context->value_storage;
    const workspace_manifest *info = workspace_info_get(service);
    out[0].b = info != NULL;
    out[1].str = copy(storage, info ? info->root : "");
    out[2].str = copy(storage, info ? info->name : "");
    out[3].str = copy(storage, info ? info->content : "");
    out[4].str = copy(storage, info ? info->start_scene : "");
    out[5].i = info ? info->version : 0;
    out[6].str = copy(storage, info ? "" : "Workspace manifest service is unavailable");
    out[7].b = workspace_info_active(service);
    out[8].b = workspace_info_documents_restored(service);
    out[9].str = copy(storage, workspace_info_pending_root(service));
    out[10].i = workspace_info_pending_version(service);
    return NULL;
}
static const char *refresh(
    const behavior_function_context *context,
    void *service,
    const pin_value *,
    pin_value *out
)
{
    char error[256] = { 0 };
    out[0].b = service && workspace_info_refresh(service, error, sizeof error);
    out[1].str = copy(context->value_storage, service ? error : "Workspace manifest service is unavailable");
    return NULL;
}
const node_host_call WORKSPACE_INFO_CALLS[2] = {
    { .service = "workspace_info", .context = query, .optional_service = true },
    { .service = "workspace_info", .context = refresh, .optional_service = true }
};
const node_access WORKSPACE_INFO_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
const node_access WORKSPACE_INFO_WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
