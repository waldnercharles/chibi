#ifndef CBLUEPRINT_GAMEPLAY_INPUT_H
#define CBLUEPRINT_GAMEPLAY_INPUT_H

#include "editor/workspace/scene_authoring.h"
#include "runtime/engine.h"

/* Poll the host after its input update and before stepping the runtime. */
void app_gameplay_input(app_scene_authoring *scene, engine *runtime, engine_viewport *viewport);

#endif
