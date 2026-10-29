#pragma once
#include "graph/model/behavior_asset.h"
typedef struct
{
    char *path;
    behavior_asset *asset;
} authoring_graph_source;
/* Bounded native graph document snapshot; owns each path and parsed asset.
 * No preview, runtime world or execution callbacks. Output initially NULL. */
/* The linkable definition of an event record, or NULL when it is invalid. */
behavior_asset *authoring_event_definition(const char *text);
bool authoring_graph_sources_load(const char *root, authoring_graph_source **out);
void authoring_graph_sources_free(authoring_graph_source *);
