#ifndef CBLUEPRINT_EDITOR_H
#define CBLUEPRINT_EDITOR_H

#include "../core/flow.h"
#include "../core/graph.h"

typedef struct editor editor;

/* The editor borrows g; call graph_free yourself. */
editor *editor_new(graph *g);
void editor_free(editor *e);

/* Renders the canvas and handles input. Call once per frame between
 * cf_app_update() and cf_app_draw_onto_screen(). */
void editor_draw(editor *e);

/* Advances the editor-owned run with a host-supplied monotonic timestamp. */
void editor_tick(editor *e, double monotonic_time);

/* Selects and centers a node in the canvas. */
void editor_select_node(editor *e, node *n);
void editor_reveal_node(editor *e, node *n);

/* Adds a node of type desc to the graph at canvas (x, y) and returns the core
 * node for wiring, or NULL on allocation failure. */
node *editor_spawn(editor *e, const node_desc *desc, float x, float y);

/* Save the graph to `path` as JSON. Returns 0 on success, -1 on error. */
int editor_save(const editor *e, const char *path);

/* Replace the editor's graph with the JSON at `path`. Returns 0 on success,
 * -1 on error (the existing graph is left untouched). */
int editor_load(editor *e, const char *path);

#endif
