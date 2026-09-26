#ifndef CHIBI_EDITOR_STUDIO_H
#define CHIBI_EDITOR_STUDIO_H

#include "editor/workspace/scene_authoring.h"

/* The RmlUi Studio workspace: a full-window retained document. It reads and
 * edits the scene authoring model directly and shows the Scene render target
 * inside its viewport area. */
typedef struct studio studio;

/* `project_root` is the open project's directory; the caller keeps it alive. */
studio *studio_new(app_scene_authoring *scene, engine_viewport *viewport, const char *project_root);
void studio_free(studio *s);
/* Process input, emit the UI, draw the viewport and queue the composited frame.
 * Call after the scene frame; the host presents with cf_app_draw_onto_screen. */
void studio_frame(studio *s, double delta_seconds);
/* The project root the user chose to open, owned by the caller, or NULL. */
char *studio_take_project(studio *s);
/* Shows an error that stays until dismissed. */
void studio_error(studio *s, const char *title, const char *message);
/* Test and inspection access to element rectangles by kit path. */
bool studio_item_rect(const studio *s, const char *path, float rect[4]);

#endif
