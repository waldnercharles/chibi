#ifndef CHIBI_SERIALIZE_H
#define CHIBI_SERIALIZE_H

#include "graph/model/behavior_asset.h"
#include "graph/model/graph.h"
#include "support/asset_value.h"

char *graph_save(const graph *g);
graph *graph_load(const char *document);
const char *graph_last_error(void);
char *graph_save_nodes(const graph *g, node *const *subset, int count);
int graph_append(graph *g, const char *document, float dx, float dy);

/* FMT-1/FMT-2/FMT-16: the version-3 Behavior Asset document (metadata, ID
 * counter, optional owner type, variable list, graph list). Runtime state is
 * never serialized. */
char *behavior_asset_save(const behavior_asset *asset);
behavior_asset *behavior_asset_load(const char *document);
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
/* A local call's pin declares the signature pin with its name; documents
 * record a declaration only when it differs. */
uint64_t serialize_call_declaration(const behavior_asset *asset, const node *call, int output, int index);

/* Native asset tree values are owned by the caller's document. */
av_value *graph_to_value(av_doc *doc, const graph *g);
graph *graph_from_value(av_value *value);
av_value *graph_nodes_to_value(av_doc *doc, const graph *g, node *const *subset, int count);
int graph_append_value(graph *g, av_value *value, float dx, float dy);
av_value *behavior_asset_to_value(av_doc *doc, const behavior_asset *asset);
/* File documents use existing unique local, variable and function names. */
av_value *behavior_asset_to_disk_value(av_doc *doc, const behavior_asset *asset);
behavior_asset *behavior_asset_from_value(av_value *value);
behavior_asset *behavior_asset_clone(const behavior_asset *asset);
bool behavior_asset_equal(const behavior_asset *a, const behavior_asset *b);

#endif
