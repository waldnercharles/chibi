#ifndef CHIBI_AUTHORING_SOURCE_PRESENTATION_INTERNAL_H
#define CHIBI_AUTHORING_SOURCE_PRESENTATION_INTERNAL_H
#include "authoring/source_presentation.h"
#include "language/identity.h"

struct authoring_source_presentation
{
    behavior_asset *view;
    char *text, *identity;
    source_presentation_origin *origins;
    int references;
    uint64_t semantic_generation;
};
typedef struct
{
    authoring_source_presentation *out;
    const authoring_source_presentation *previous;
    const script_analysis *analysis;
    const script_catalog *catalog;
    script_occurrences *occurrences;
    graph *graph;
    int function;
    node **expressions, **statements;
} source_projection;

char *source_view_own(graph *, const char *);
const script_catalog_type *source_view_type(source_projection *, uint64_t);
const script_catalog_capability *source_view_capability(source_projection *, uint64_t);
pin_spec source_view_data_pin(source_projection *, const char *, script_type);
pin_spec source_view_flow_pin(source_projection *, const char *);
void source_view_origin(source_projection *, source_presentation_kind, int, script_range, node *, uint64_t, uint64_t);
node *source_view_box(source_projection *, const char *, node_display_class, pin_spec *, pin_spec *);
void source_view_describe(source_projection *, node *, const char *);
void source_view_describe_variable(source_projection *, node *, const script_symbol *, bool);
void source_view_wire(source_projection *, node *, int, node *, int, graph_edge_kind, int);
const script_expression *source_view_tuple(source_projection *, int);
const char *source_view_result_name(source_projection *, const script_expression *, int);
node *source_view_expression(source_projection *, int);
bool source_view_expression_action(source_projection *, int);
node *source_view_query(source_projection *, int);
void source_view_declaration(source_projection *, int);
void source_view_function_graph(source_projection *, int);
#endif
