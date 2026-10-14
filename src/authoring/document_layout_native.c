#include "authoring/document_layout.h"
#include "authoring/document_layout_definition.h"
#include "execution/function.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
static char *copy(arena *storage, const char *text)
{
    if (!text) {
        text = "";
    }
    char *result = arena_alloc(storage, strlen(text) + 1);
    strcpy(result, text);
    return result;
}
static bool identity(const char *text, uint64_t *value)
{
    if (!text || !*text) {
        return false;
    }
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    errno = 0;
    char *end;
    *value = strtoull(text, &end, 10);
    return !errno && !*end;
}
static const char *run(
    const behavior_function_context *context,
    void *host,
    const pin_value *in,
    pin_value *out
)
{
    document_semantics_service *service = host;
    char error[256] = { 0 }, *metadata = NULL, *path = NULL;
    uint64_t graph = 0, node = 0;
    bool valid = identity(in[2].str, &graph) &&
                 (in[7].b || identity(in[3].str, &node));
    document_semantics *snapshot = NULL;
    if (!service || !service->documents) {
        strcpy(error, "Semantic document service is unavailable");
    } else if (valid) {
        snapshot = document_semantics_service_snapshot(service, in[0].i, in[1].i, error, sizeof error);
    } else {
        strcpy(error, "Source layout requires stable graph/node identities");
    }
    if (snapshot) {
        valid = document_layout_prepare(
            snapshot,
            graph,
            node,
            in[4].f,
            in[5].f,
            in[6].b,
            in[7].b,
            &metadata,
            error,
            sizeof error
        );
        workspace_document_view view;
        if (valid && !workspace_document_read(service->documents, in[0].i, &view) &&
            view.path && *view.path) {
            path = document_layout_path(service->documents, in[0].i, in[1].i, error, sizeof error);
            valid = path != NULL;
        }
    } else {
        valid = false;
    }
    out[0].b = valid;
    out[1].str = copy(context->value_storage, error);
    out[2].str = copy(context->value_storage, path);
    out[3].str = copy(context->value_storage, valid ? metadata : NULL);
    free(path);
    free(metadata);
    document_semantics_free(snapshot);
    return NULL;
}
const node_host_call DOCUMENT_LAYOUT_CALL = {
    .service = "document_semantics",
    .optional_service = true,
    .missing_service = "Semantic document service is unavailable",
    .context = run
};
const node_access DOCUMENT_LAYOUT_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
