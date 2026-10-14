#ifndef CHIBI_DOCUMENT_GRAPH_SELECTION_H
#define CHIBI_DOCUMENT_GRAPH_SELECTION_H
#include "authoring/document_semantics.h"

#define DOCUMENT_GRAPH_SELECTION_MAX 256
typedef enum
{
    DOCUMENT_SELECTION_DELETE,
    DOCUMENT_SELECTION_CLONE,
    DOCUMENT_SELECTION_PLACE,
    DOCUMENT_SELECTION_COMMENT,
    DOCUMENT_SELECTION_BOX,
    DOCUMENT_SELECTION_REMOVE_BOX,
    DOCUMENT_SELECTION_REROUTE,
    DOCUMENT_SELECTION_COPY,
    DOCUMENT_SELECTION_PASTE,
    DOCUMENT_SELECTION_SOURCE_COPY,
    DOCUMENT_SELECTION_NODE_FLAG
} document_graph_selection_operation;
typedef struct
{
    document_graph_selection_operation operation;
    uint64_t graph, node, box, from_node, from_pin, to_node, to_pin;
    const uint64_t *nodes;
    int count;
    int flag_kind; /*0disabled,1breakpoint,2inline presentation,3advanced*/
    const float *x, *y;
    int points;
    float dx, dy, width, height;
    const char *text;
    uint32_t color;
    bool hidden;
} document_graph_selection_request;
typedef struct
{
    char *text, *metadata;
    uint64_t *created;
    uint64_t box;
} document_graph_selection_result;
/* Inputs are borrowed from an exact revision-bound semantic snapshot. Outputs
 * own their text, metadata and created-node identities; free after adoption.
 * Never mutates documents, history, disk, execution or the input presentation.
 * Source topology uses source-edit operations; only presentation edits become
 * source sidecar metadata. All selected identities are checked before changes. */
bool document_graph_selection_prepare(
    const document_semantics *,
    const document_graph_selection_request *,
    document_graph_selection_result *,
    char *error,
    size_t
);
void document_graph_selection_result_free(document_graph_selection_result *);
#endif
