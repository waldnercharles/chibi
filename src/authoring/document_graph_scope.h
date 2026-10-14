#ifndef CHIBI_DOCUMENT_GRAPH_SCOPE_H
#define CHIBI_DOCUMENT_GRAPH_SCOPE_H
#include "authoring/workspace_documents.h"

#include <stdint.h>
/* Declarative function/signature operations. Source functions use validated
 * source edit requests. Indices are raw signature pin indices. */
typedef enum
{
    DOCUMENT_SCOPE_ADD,
    DOCUMENT_SCOPE_REMOVE,
    DOCUMENT_SCOPE_RENAME,
    DOCUMENT_SCOPE_ADD_PIN,
    DOCUMENT_SCOPE_REMOVE_PIN,
    DOCUMENT_SCOPE_RENAME_PIN,
    DOCUMENT_SCOPE_MOVE_PIN,
    DOCUMENT_SCOPE_RETYPE_PIN,
    DOCUMENT_SCOPE_DEFAULT,
    DOCUMENT_SCOPE_CALL
} document_scope_operation;
typedef struct
{
    int operation, storage, container, index, target;
    const char *graph, *caller, *name, *type_name, *value;
    bool result;
} document_scope_edit;
bool document_graph_scope_prepare(
    workspace_documents *,
    int document,
    int revision,
    const document_scope_edit *,
    uint64_t *identity,
    char **text,
    char *error,
    size_t size
);
bool document_graph_scope_edit(
    workspace_documents *,
    int document,
    int revision,
    const document_scope_edit *,
    uint64_t *identity,
    char *error,
    size_t size
);
#endif
