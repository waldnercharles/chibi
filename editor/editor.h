#ifndef CBLUEPRINT_EDITOR_H
#define CBLUEPRINT_EDITOR_H

#include "../core/blueprint.h"
#include "../core/instance.h"

typedef struct editor editor;

/* The editor takes ownership of bp and frees it in editor_free(). */
editor *editor_new(blueprint *bp);
void editor_free(editor *e);

/* Renders the canvas and handles input. Call once per frame between
 * cf_app_update() and cf_app_draw_onto_screen(). */
void editor_draw(editor *e);

/* Advances the editor-owned preview instance with a host-supplied monotonic
 * timestamp, then advances editor state. Call once per host frame. */
void editor_tick(editor *e, double monotonic_time);

/* Selects and centers a node in the canvas. */
void editor_select_node(editor *e, node *n);
void editor_reveal_node(editor *e, node *n);

/* Adds a node of type desc to the graph at canvas (x, y) and returns the core
 * node for wiring, or NULL on allocation failure. */
node *editor_spawn(editor *e, const node_desc *desc, float x, float y);

/* Save the blueprint to `path` as JSON. Returns 0 on success, -1 on error. */
int editor_save(const editor *e, const char *path);

/* Replace the editor's blueprint with the JSON at `path`. Returns 0 on
 * success, -1 on error (the existing blueprint is left untouched). */
int editor_load(editor *e, const char *path);

/* Preview instance (UI-54/55/56). The editor owns one blueprint_instance;
 * any authoring mutation destroys it at the edit choke point and releases the
 * definition's mutation guard. The host recreates it and drives dispatch. */
blueprint_instance *editor_preview_instance(const editor *e);

/* Monotonic counter bumped on every instance (re)creation, so the host can
 * dispatch a once-per-instance event such as the demo Begin() convention. */
unsigned editor_preview_generation(const editor *e);

/* Create the preview runtime if missing. No-op when one is attached. */
void editor_preview_create(editor *e);

#endif
