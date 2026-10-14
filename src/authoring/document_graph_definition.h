#ifndef CHIBI_DOCUMENT_GRAPH_DEFINITION_H
#define CHIBI_DOCUMENT_GRAPH_DEFINITION_H
#include "authoring/document_graph.h"
#include "authoring/document_graph_scope.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
#include "types/catalog.h"
typedef struct
{
    const char *type, *label, *category, *query, *description;
} document_graph_choice;
typedef struct
{
    const char *graph, *name, *kind, *origin, *body, *signature;
    int start, end;
    bool disabled, pure;
} document_graph_scope;
typedef struct
{
    int kind, result, binding;
    const char *origin, *producer, *previous, *capability, *symbol, *component, *member;
    const char *value, *expression, *fragment, *capability_name, *name,
        *reference, *field, *signature;
    bool disabled;
} document_source_edit;
typedef struct
{
    int operation;
    const char *graph, *node, *pin, *from_node, *from_pin, *type, *value;
    float x, y;
    bool placed;
} document_graph_edit_value;
#define DOCUMENT_GRAPH_TYPE_COUNT 5
extern const native_type_definition DOCUMENT_GRAPH_TYPE_DEFINITIONS[DOCUMENT_GRAPH_TYPE_COUNT];
extern const type_catalog_pack DOCUMENT_GRAPH_TYPE_PACK;
extern const native_definition_pack DOCUMENT_GRAPH_DEFINITION_PACK;
extern const node_host_call DOCUMENT_GRAPH_CALLS[15];
extern const node_access DOCUMENT_GRAPH_READ_ACCESS, DOCUMENT_GRAPH_WRITE_ACCESS;
#endif
