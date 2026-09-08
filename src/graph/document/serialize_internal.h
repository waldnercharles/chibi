#ifndef CBLUEPRINT_SERIALIZE_INTERNAL_H
#define CBLUEPRINT_SERIALIZE_INTERNAL_H

#include "graph/document/serialize.h"

#include <yyjson.h>

#define GRAPH_SCHEMA_VERSION 3

void serialize_error_set(const char *fmt, ...);
void serialize_write_value(
    yyjson_mut_doc *doc,
    yyjson_mut_val *obj,
    const char *key,
    pin_type t,
    const char *type_name,
    const pin_value *v
);
int serialize_read_value(pin_type t, yyjson_val *v, pin_value *out, graph *g);
int serialize_read_array(
    pin_type element_type,
    const char *type_name,
    yyjson_val *json,
    pin_value *out,
    graph *g
);
int serialize_migrate_component_reads(graph *g);
int serialize_repair_math_calls(behavior_asset *asset);

#endif
