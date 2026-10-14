#ifndef CHIBI_DOCUMENT_GRAPH_SELECTION_DEFINITION_H
#define CHIBI_DOCUMENT_GRAPH_SELECTION_DEFINITION_H
#include "authoring/document_graph_selection.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
#include "types/catalog.h"
#define DOCUMENT_GRAPH_SELECTION_CAPABILITY_COUNT 9
typedef struct
{
    const char *id, *node, *title;
    float x, y, width, height;
    uint64_t color;
    bool hidden, box;
} document_graph_note;
typedef struct
{
    const char *from_node, *from_pin, *to_node, *to_pin;
    int index;
    float x, y;
} document_graph_route;
extern const type_desc DOCUMENT_GRAPH_NOTE_LAYOUT, DOCUMENT_GRAPH_ROUTE_LAYOUT;
extern const native_type_definition DOCUMENT_GRAPH_SELECTION_TYPES[2];
extern const type_catalog_pack DOCUMENT_GRAPH_SELECTION_TYPE_PACK;
extern const native_definition_pack DOCUMENT_GRAPH_SELECTION_DEFINITION_PACK;
extern const node_host_call DOCUMENT_GRAPH_SELECTION_CALLS[DOCUMENT_GRAPH_SELECTION_CAPABILITY_COUNT];
extern const node_access DOCUMENT_GRAPH_SELECTION_ACCESS;
#endif
