#ifndef CHIBI_DOCUMENT_GRAPH_H
#define CHIBI_DOCUMENT_GRAPH_H
#include "authoring/document_semantics.h"

typedef enum
{
    DOCUMENT_GRAPH_ADD,
    DOCUMENT_GRAPH_CONNECT,
    DOCUMENT_GRAPH_DISCONNECT,
    DOCUMENT_GRAPH_DEFAULT,
    DOCUMENT_GRAPH_REMOVE,
    DOCUMENT_GRAPH_PLACE,
    DOCUMENT_GRAPH_ARRANGE,
    DOCUMENT_GRAPH_TEXT
} document_graph_operation;
typedef struct
{
    document_graph_operation operation;
    uint64_t graph, node, pin, from_node, from_pin;
    const char *type, *value;
    float x, y;
    bool placed;
} document_graph_request;
/* Typed structural edits to one owned declarative graph snapshot. An invalid
 * draft may remain invalid: compilation diagnostics are inspection results.
 * Failed operations preserve every source byte, revision and history entry.
 * Coordinates affect presentation only. Callers own document gestures. */
bool document_graph_prepare(
    workspace_documents *,
    int document,
    int revision,
    const document_graph_request *,
    uint64_t *created,
    char **text,
    char *error,
    size_t size
);
bool document_graph_edit(
    workspace_documents *,
    int document,
    int revision,
    const document_graph_request *,
    uint64_t *created,
    char *error,
    size_t size
);
bool document_graph_node_allowed(behavior_domain, const node_desc *);
#endif
