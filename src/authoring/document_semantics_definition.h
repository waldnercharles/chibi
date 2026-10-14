#ifndef CHIBI_DOCUMENT_SEMANTICS_DEFINITION_H
#define CHIBI_DOCUMENT_SEMANTICS_DEFINITION_H
#include "authoring/document_semantics.h"
#include "runtime/native_definitions.h"
#include "runtime/type_definitions.h"
#include "types/catalog.h"

typedef struct
{
    int start, end, line, column;
    const char *message;
} document_diagnostic_row;
typedef struct
{
    const char *graph, *node, *origin, *label;
    const char *symbol, *source, *category;
    int source_kind, operation;
    const char *reference_name, *called_graph;
    bool call;
    int start, end, kind;
    float x, y;
    uint64_t execution_module, execution_function, execution_occurrence;
    bool disabled, breakpoint, advanced;
    int presentation;
} document_node_row;
typedef struct
{
    const char *graph, *node, *pin, *origin, *label, *type, *value, *text, *value_json;
    bool output, flow, editable, color, boolean;
    int index, container;
    double number;
    int start, end; /* Exact source expression bytes; -1 without an expression. */
    bool advanced;
    bool connected, visible, named, bound;
    const char *source;
    int vector_width;
} document_port_row;
typedef struct
{
    const char *graph, *from, *from_pin, *to, *to_pin;
    bool flow;
} document_edge_row;
typedef struct
{
    int start, end;
    const char *kind;
} document_token_row;
typedef struct
{
    const char *label, *detail, *insert;
    int kind;
} document_completion_row;
typedef struct
{
    const char *name;
    int start, end, kind;
} document_reference_row;
#define DOCUMENT_SEMANTICS_TYPE_COUNT 7
extern const native_type_definition DOCUMENT_SEMANTICS_TYPE_DEFINITIONS[DOCUMENT_SEMANTICS_TYPE_COUNT];
extern const native_definition_pack DOCUMENT_SEMANTICS_DEFINITION_PACK;
extern const type_catalog_pack DOCUMENT_SEMANTICS_TYPE_PACK;
extern const node_host_call DOCUMENT_SEMANTICS_CALLS[8];
extern const node_access DOCUMENT_SEMANTICS_READ_ACCESS, DOCUMENT_SEMANTICS_WRITE_ACCESS;
#endif
