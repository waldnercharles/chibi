#ifndef CHIBI_SERIALIZE_INTERNAL_H
#define CHIBI_SERIALIZE_INTERNAL_H

#include "graph/document/serialize.h"

#include <yyjson.h>

#define GRAPH_SCHEMA_VERSION 3

void serialize_error_set(const char *fmt, ...);
bool serialize_same_value(const pin_value *a, const pin_value *b);

/* Documents spell pin types by name; loading also accepts enum numbers. */
const char *serialize_pin_type_name(pin_type type);
bool serialize_read_pin_type(yyjson_val *value, pin_type *out);
/* A pin whose type differs from its template implies these unless written:
 * primitive types name themselves, and values start at zero. */
const char *serialize_implied_type_name(pin_type type, const char *template_name);
pin_value serialize_zero_value(pin_type type, pin_container container, const char *type_name);
void serialize_write_value(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const char *key,
    pin_type t,
    const char *type_name,
    const pin_value *v
);
int serialize_read_value(pin_type t, const char *type_name, yyjson_val *v, pin_value *out, graph *g);
int serialize_read_array(
    pin_type element_type,
    const char *type_name,
    yyjson_val *json,
    pin_value *out,
    graph *g
);
int serialize_read_locals(graph *g, yyjson_val *obj);
int serialize_resolve_locals(graph *g, yyjson_val *obj);
int serialize_validate_locals(graph *g);
void serialize_write_locals(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const graph *g,
    node *const *subset,
    int count
);
int serialize_migrate_component_reads(graph *g);
/* Loads one graph object's locals, nodes and edges into a new graph, or NULL. */
graph *serialize_load_graph_body(yyjson_val *obj, uint64_t id_counter);
/* Drops wildcard pin shapes that loading the written graph reproduces. */
void serialize_omit_implied_shapes(yyjson_mut_val *obj, const graph *g);
/* Asset loading marks pins whose document omits a declaration and then
 * derives it. */
#define SERIALIZE_DERIVED_DECLARATION UINT64_MAX
bool serialize_local_call(const node *n);
void serialize_derive_declarations(behavior_asset *asset);
void serialize_omit_derived_declarations(yyjson_mut_doc *doc, yyjson_mut_val *obj, const graph *g);
int serialize_repair_math_calls(behavior_asset *asset);

#endif
