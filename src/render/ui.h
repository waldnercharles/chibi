#ifndef CHIBI_RENDER_UI_H
#define CHIBI_RENDER_UI_H

#include "ecs/ecs.h"
#include "platform/cute.h"
#include "runtime/ui.h"

/* Presents a world's GameUi documents: loads content/ui/<document>.yaml for
 * each, re-reads it within a second of an edit (keeping the last valid one),
 * applies what behaviors set in the game_ui model, and draws the result over
 * a canvas. RCSS dp units scale with the canvas height, 720 pixels being 1. */
typedef struct render_ui render_ui;

render_ui *render_ui_new(const char *content_root);
void render_ui_free(render_ui *ui);
/* `model` may be NULL to show documents as authored. */
void render_ui_update(render_ui *ui, ecs_world *world, game_ui *model, int width, int height, double dt);
/* Draws the documents over `target` without clearing it. */
void render_ui_composite(render_ui *ui, CF_Canvas target);
/* The last document error, or "" when every shown document loaded. */
const char *render_ui_error(const render_ui *ui);

#endif
