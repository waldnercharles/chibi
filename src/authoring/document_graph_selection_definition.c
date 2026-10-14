#include "authoring/document_graph_selection_definition.h"

#include <stddef.h>
#define FIELD(T, F, P, N)                                                      \
    { .name = #F,                                                              \
      .type = P,                                                               \
      .type_name = N,                                                          \
      .offset = offsetof(T, F),                                                \
      .size = sizeof(((T *)0)->F) }
#define S(T, F) FIELD(T, F, PIN_STR, "const char *")
#define F(T, N) FIELD(T, N, PIN_F32, "float")
#define B(T, F) FIELD(T, F, PIN_BOOL, "bool")
static const type_field NOTE_FIELDS[] = { S(document_graph_note, id),
                                          S(document_graph_note, node),
                                          S(document_graph_note, title),
                                          F(document_graph_note, x),
                                          F(document_graph_note, y),
                                          F(document_graph_note, width),
                                          F(document_graph_note, height),
                                          FIELD(document_graph_note, color, PIN_U64, "uint64"),
                                          B(document_graph_note, hidden),
                                          B(document_graph_note, box) };
static const type_field ROUTE_FIELDS[] = { S(document_graph_route, from_node),
                                           S(document_graph_route, from_pin),
                                           S(document_graph_route, to_node),
                                           S(document_graph_route, to_pin),
                                           FIELD(document_graph_route, index, PIN_I32, "int"),
                                           F(document_graph_route, x),
                                           F(document_graph_route, y) };
const type_desc DOCUMENT_GRAPH_NOTE_LAYOUT = { .name = "DocumentGraphNote",
                                               .size = sizeof(document_graph_note),
                                               .fields = NOTE_FIELDS,
                                               .field_count = NATIVE_COUNT(NOTE_FIELDS) };
const type_desc DOCUMENT_GRAPH_ROUTE_LAYOUT = { .name = "DocumentGraphRoute",
                                                .size = sizeof(document_graph_route),
                                                .fields = ROUTE_FIELDS,
                                                .field_count = NATIVE_COUNT(ROUTE_FIELDS) };
const native_type_definition DOCUMENT_GRAPH_SELECTION_TYPES[2] = {
    { .layout = &DOCUMENT_GRAPH_NOTE_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &DOCUMENT_GRAPH_ROUTE_LAYOUT, .storage = PIN_STRUCT }
};
static const type_desc *const LAYOUTS[] = { &DOCUMENT_GRAPH_NOTE_LAYOUT,
                                            &DOCUMENT_GRAPH_ROUTE_LAYOUT };
const type_catalog_pack DOCUMENT_GRAPH_SELECTION_TYPE_PACK = {
    .types = LAYOUTS,
    .type_count = NATIVE_COUNT(LAYOUTS)
};

#define D(N, T, S)                                                             \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = T, .type_name = S }
#define A(N, T, S)                                                             \
    { .name = N, .kind = NATIVE_PARAMETER_DATA, .storage = T, .type_name = S, .container = PIN_CONTAINER_ARRAY }
#define DOC D("Document", PIN_I32, "int"), D("Revision", PIN_I32, "int")
#define GRAPH DOC, D("Graph", PIN_STR, "const char *")
#define IDS A("Nodes", PIN_STR, "const char *")
#define POINTS A("X", PIN_F32, "float"), A("Y", PIN_F32, "float")
static const native_parameter_definition SELECTION[] = {
    GRAPH,  D("Operation", PIN_I32, "int"), IDS,
    POINTS, D("Dx", PIN_F32, "float"),      D("Dy", PIN_F32, "float")
};
static const native_parameter_definition COMMENT[] = {
    GRAPH,
    D("Node", PIN_STR, "const char *"),
    D("Text", PIN_STR, "const char *"),
    D("Hidden", PIN_BOOL, "bool")
};
static const native_parameter_definition BOX[] = { GRAPH,
                                                   D("Box", PIN_STR, "const char *"),
                                                   D("Remove", PIN_BOOL, "bool"),
                                                   D("Title", PIN_STR, "const char *"),
                                                   D("Color", PIN_U64, "uint64"),
                                                   D("X", PIN_F32, "float"),
                                                   D("Y", PIN_F32, "float"),
                                                   D("Width", PIN_F32, "float"),
                                                   D("Height", PIN_F32, "float") };
static const native_parameter_definition REROUTE[] = {
    GRAPH,
    D("FromNode", PIN_STR, "const char *"),
    D("FromPin", PIN_STR, "const char *"),
    D("ToNode", PIN_STR, "const char *"),
    D("ToPin", PIN_STR, "const char *"),
    POINTS
};
static const native_parameter_definition SOURCE_COPY[] = {
    DOC,
    A("Origins", PIN_STR, "const char *")
};
static const native_parameter_definition GRAPH_COPY[] = { GRAPH, IDS };
static const native_parameter_definition PASTE[] = { GRAPH,
                                                     D("Text", PIN_STR, "const char *"),
                                                     D("Dx", PIN_F32, "float"),
                                                     D("Dy", PIN_F32, "float") };
static const native_parameter_definition OUTPUT[] = {
    D("Ok", PIN_BOOL, "bool"),
    D("Error", PIN_STR, "const char *"),
    D("Text", PIN_STR, "const char *"),
    D("Metadata", PIN_STR, "const char *"),
    A("Created", PIN_STR, "const char *"),
    D("Box", PIN_STR, "const char *")
};
static const native_parameter_definition FLAG[] = { GRAPH,
                                                    IDS,
                                                    D("Kind", PIN_I32, "int"),
                                                    D("Flag", PIN_BOOL, "bool") };
static const native_parameter_definition INSPECT[] = { GRAPH };
static const native_parameter_definition PRESENTATION[] = {
    D("Ok", PIN_BOOL, "bool"),
    D("Error", PIN_STR, "const char *"),
    A("Notes", PIN_STRUCT, "DocumentGraphNote"),
    A("Routes", PIN_STRUCT, "DocumentGraphRoute")
};
#define OPS(X)                                                                 \
    X(0, "prepare_graph_selection", SELECTION)                                 \
    X(1, "prepare_graph_comment", COMMENT)                                     \
    X(2, "prepare_graph_box", BOX)                                             \
    X(3, "prepare_graph_reroute", REROUTE)                                     \
    X(4, "copy_source_selection", SOURCE_COPY)                                 \
    X(5, "copy_declarative_selection", GRAPH_COPY)                             \
    X(6, "prepare_declarative_paste", PASTE)
#define DEF(I, N, IN)                                                          \
    { .canonical_name = "Authoring." N,                                        \
      .source_name = N,                                                        \
      .kind = SCRIPT_CAPABILITY_FUNCTION,                                      \
      .pure = true,                                                            \
      .inputs = IN,                                                            \
      .input_count = NATIVE_COUNT(IN),                                         \
      .outputs = OUTPUT,                                                       \
      .output_count = NATIVE_COUNT(OUTPUT),                                    \
      .completion_output = -1 },
static const native_capability_definition DEFINITIONS[] = {
    OPS(DEF) DEF(8, "prepare_graph_node_flag", FLAG){
        .canonical_name = "Authoring.inspect_graph_presentation",
        .source_name = "inspect_graph_presentation",
        .kind = SCRIPT_CAPABILITY_FUNCTION,
        .pure = true,
        .inputs = INSPECT,
        .input_count = NATIVE_COUNT(INSPECT),
        .outputs = PRESENTATION,
        .output_count = NATIVE_COUNT(PRESENTATION),
        .completion_output = -1 }
};
#define BIND(I, N, IN)                                                         \
    { .canonical_name = "Authoring." N,                                        \
      .implementation = { .call = &DOCUMENT_GRAPH_SELECTION_CALLS[I],          \
                          .access = &DOCUMENT_GRAPH_SELECTION_ACCESS } },
static const native_definition_binding BINDINGS[] = {
    OPS(BIND) BIND(8, "prepare_graph_node_flag", FLAG){
        .canonical_name = "Authoring.inspect_graph_presentation",
        .implementation = { .call = &DOCUMENT_GRAPH_SELECTION_CALLS[7],
                            .access = &DOCUMENT_GRAPH_SELECTION_ACCESS } }
};
const native_definition_pack DOCUMENT_GRAPH_SELECTION_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = NATIVE_COUNT(DEFINITIONS),
    .bindings = BINDINGS,
    .binding_count = NATIVE_COUNT(BINDINGS),
    .layouts = LAYOUTS,
    .layout_count = NATIVE_COUNT(LAYOUTS)
};
