#ifndef CHIBI_EDITOR_STUDIO_H
#define CHIBI_EDITOR_STUDIO_H

#include "editor/workspace/scene_authoring.h"

/* The RmlUi Studio workspace: a full-window retained document. It reads and
 * edits the scene authoring model directly and shows the Scene render target
 * inside its viewport area. */
typedef struct studio studio;

studio *studio_new(app_scene_authoring *scene, engine_viewport *viewport);
void studio_free(studio *s);
/* Process input, emit the UI, draw the viewport and queue the composited frame.
 * Call after the scene frame; the host presents with cf_app_draw_onto_screen. */
void studio_frame(studio *s, double delta_seconds);

#endif
