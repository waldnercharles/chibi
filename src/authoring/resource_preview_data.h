#ifndef CHIBI_RESOURCE_PREVIEW_DATA_H
#define CHIBI_RESOURCE_PREVIEW_DATA_H
#include "graph/model/graph.h"

#include <stdbool.h>
#include <stddef.h>
typedef enum
{
    RESOURCE_PREVIEW_NONE,
    RESOURCE_PREVIEW_RASTER,
    RESOURCE_PREVIEW_STYLESHEET,
    RESOURCE_PREVIEW_UI,
    RESOURCE_PREVIEW_GRAPH,
    RESOURCE_PREVIEW_VISUAL,
    RESOURCE_PREVIEW_SCENE
} resource_preview_route_kind;
/* Pure dispatch over canonical inventory kinds and supported raw formats.
 * No filesystem access, execution or fallback to an unrelated provider. */
resource_preview_route_kind resource_preview_route(const char *kind, const char *path);
/* First 64 distinct literal CSS hex colors, in source order. Selectors,
 * strings, comments and nonliteral expressions are not swatches. Literal
 * fallbacks are included; variables are never evaluated.
 * sRGB bytes/straight alpha; input <=1MiB, <=4096 declaration ranges. */
bool resource_preview_palette(const char *, unsigned char colors[64][4], int *count, char *error, size_t);
/* Read-only mini-map of existing graph placement: <=512 nodes/2048 wires,
 * <=256x256 output. Does not execute, publish, arrange or mutate the graph. */
bool resource_preview_graph(const graph *, int width, int height, unsigned char *rgba, char *error, size_t);
#endif
