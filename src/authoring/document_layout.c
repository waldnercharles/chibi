#include "authoring/document_layout.h"

#include "graph/authoring/layout.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
char *document_layout_path(
    const workspace_documents *documents,
    int document,
    int revision,
    char *error,
    size_t size
)
{
    workspace_document_view view;
    workspace_document_status status = workspace_document_read(documents, document, &view);
    if (status || view.revision != revision) {
        fail(error, size, workspace_document_status_text(status ? status : WORKSPACE_DOCUMENT_STALE));
        return NULL;
    }
    if (!view.path || !*view.path) {
        fail(error, size, "Memory documents have no sidecar path");
        return NULL;
    }
    size_t length = strlen(view.path);
    if (length + 5 > WORKSPACE_DOCUMENT_PATH_MAX) {
        fail(error, size, "Sidecar path exceeds the workspace path limit");
        return NULL;
    }
    char *path = xmalloc(length + 6);
    snprintf(path, length + 6, "%s.meta", view.path);
    if (size) {
        *error = 0;
    }
    return path;
}
bool document_layout_prepare(
    const document_semantics *snapshot,
    uint64_t graph_id,
    uint64_t node_id,
    float x,
    float y,
    bool placed,
    bool arrange,
    char **metadata,
    char *error,
    size_t size
)
{
    *metadata = NULL;
    const authoring_source_presentation *source = document_semantics_presentation(snapshot);
    if (!source) {
        return fail(error, size, "Layout requires an authored source graph");
    }
    if (!arrange && (!isfinite(x) || !isfinite(y) || fabsf(x) > 1000000 ||
                     fabsf(y) > 1000000)) {
        return fail(error, size, "Graph coordinates must be finite and within one million units");
    }
    authoring_source_presentation *copy = authoring_source_presentation_clone(source);
    behavior_asset *asset = authoring_source_presentation_view(copy);
    graph *g = behavior_asset_find_graph(asset, graph_id);
    bool valid = g != NULL;
    if (valid && arrange) {
        for (int i = 0; i < acount(g->nodes); i++) {
            g->nodes[i]->placed = false;
        }
        graph_layout(g);
    } else if (valid) {
        node *n = graph_node_by_id(g, node_id);
        valid = n != NULL;
        if (valid) {
            n->x = x;
            n->y = y;
            n->placed = placed;
            graph_place_unplaced(g);
        }
    }
    if (valid) {
        *metadata = authoring_source_presentation_metadata(copy);
        valid = *metadata != NULL;
    }
    authoring_source_presentation_free(copy);
    if (!valid) {
        return fail(error, size, "Graph or node identity does not belong to this source snapshot");
    }
    if (size) {
        *error = 0;
    }
    return true;
}
