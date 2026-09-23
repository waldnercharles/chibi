#ifndef CHIBI_COMMAND_PALETTE_H
#define CHIBI_COMMAND_PALETTE_H
#include "editor/workspace/scene_authoring.h"

typedef struct editor_command_palette editor_command_palette;
void editor_command_palette_open(app_scene_authoring *scene);
void editor_command_palette_draw(app_scene_authoring *scene);
void editor_command_palette_frame(app_scene_authoring *scene);
void editor_command_palette_free(editor_command_palette *palette);
#endif
