#include "host/workspace_overlays.h"

#include "authoring/workspace_documents.h"

#include <stdio.h>
#include <yyjson.h>

char *workspace_overlays_json(workspace_documents *documents, char *error, size_t size)
{
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    int count = workspace_document_handles(documents, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, array);
    bool valid = true;
    for (int i = 0; valid && i < count; i++) {
        workspace_document_view view;
        valid = workspace_document_read(documents, handles[i], &view) ==
                    WORKSPACE_DOCUMENT_OK &&
                !view.transaction;
        if (!valid) {
            snprintf(error, size, "Finish document gestures before freezing workspace inputs");
            break;
        }
        if (!view.path[0]) {
            if (view.dirty || view.bytes) {
                valid = false;
                snprintf(error, size, "Save As is required for unnamed workspace inputs");
            }
            continue;
        }
        yyjson_mut_val *row = yyjson_mut_arr_add_obj(doc, array);
        yyjson_mut_obj_add_strcpy(doc, row, "path", view.path);
        yyjson_mut_obj_add_strcpy(doc, row, "text", view.text);
    }
    char *json = valid ? yyjson_mut_write(doc, 0, NULL) : NULL;
    yyjson_mut_doc_free(doc);
    return json;
}
