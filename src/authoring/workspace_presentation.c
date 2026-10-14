#include "authoring/workspace_presentation.h"

#include <stdio.h>
#include <stdlib.h>
bool workspace_presentation_load(
    const workspace_documents *documents,
    ecs_component_catalog catalog,
    component_presentation *out,
    char *error,
    size_t size
)
{
    bool exists;
    int handle, revision;
    workspace_document_status status = workspace_document_path(
        documents,
        COMPONENT_PRESENTATION_PATH,
        &exists,
        &handle,
        &revision
    );
    if (status == WORKSPACE_DOCUMENT_UNAVAILABLE ||
        (status == WORKSPACE_DOCUMENT_OK && !exists)) {
        component_presentation_free(out);
        return true;
    }
    char *text = NULL;
    if (!status) {
        status = workspace_document_read_path(documents, COMPONENT_PRESENTATION_PATH, &text);
    }
    if (status) {
        if (size) {
            snprintf(error, size, "%s", workspace_document_status_text(status));
        }
        return false;
    }
    bool ok = component_presentation_read(text, catalog, out, error, size);
    free(text);
    return ok;
}
