#ifndef CHIBI_EDITOR_CONTROL_H
#define CHIBI_EDITOR_CONTROL_H

#include "editor/workspace/scene_authoring.h"

/* Optional local, live-editor transport, enabled by CHIBI_EDITOR_SOCKET.
 * Uses the same frame input path as the UI tests; no OS input injection. */
void editor_control_open();
void editor_control_close();
void editor_control_frame(editor *graph, app_scene_authoring *scene);

#endif
