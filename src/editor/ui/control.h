#ifndef CHIBI_EDITOR_CONTROL_H
#define CHIBI_EDITOR_CONTROL_H

#include "editor/workspace/scene_authoring.h"

/* Optional local control of a live editor through the command API, enabled by
 * CHIBI_EDITOR_SOCKET. A client sends one JSON request line and receives one
 * JSON response line. Native builds only; elsewhere these do nothing. */
void editor_control_open();
void editor_control_close();
void editor_control_frame(app_scene_authoring *scene);

#endif
