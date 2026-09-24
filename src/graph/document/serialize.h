#ifndef CHIBI_SERIALIZE_H
#define CHIBI_SERIALIZE_H

#include "graph/model/behavior_asset.h"
#include "graph/model/graph.h"

char *graph_save_json(const graph *g);
graph *graph_load_json(const char *json);
const char *graph_last_error(void);
char *graph_save_nodes_json(const graph *g, node *const *subset, int count);
int graph_append_json(graph *g, const char *json, float dx, float dy);

/* FMT-1/FMT-2/FMT-16: the version-3 Behavior Asset document (metadata, ID
 * counter, optional owner type, variable list, graph list). Runtime state is
 * never serialized. */
char *behavior_asset_save_json(const behavior_asset *asset);
behavior_asset *behavior_asset_load_json(const char *json);
/* The pin a loaded node starts from before serialized fields apply. Documents
 * omit pin fields that match it. */
const pin_spec *serialize_pin_template(const node_desc *desc, int output, int index);
/* Documents refer to pins as <node id>.<key>. A key is the pin name, or
 * <source key>.<field> for an exposed struct field. */
#define SERIALIZE_KEY_SIZE 512
void serialize_pin_key(const node *n, int output, int index, char *out, size_t size);
/* Index of the pin with this key, or -1 when absent or ambiguous. */
int serialize_pin_by_key(const node *n, int output, const char *key);
/* False when the pin's key is ambiguous on its node side. */
bool serialize_pin_ref(const node *n, int output, int index, char *out, size_t size);
bool serialize_resolve_ref(const graph *g, const char *ref, int output, node **n, int *index);

#endif
