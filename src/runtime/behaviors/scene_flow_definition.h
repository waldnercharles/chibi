#ifndef CHIBI_SCENE_FLOW_DEFINITION_H
#define CHIBI_SCENE_FLOW_DEFINITION_H
#include "runtime/native_definitions.h"

#define SCENE_LOAD_NAME "Load Scene"
#define SCENE_RELOAD_NAME "Reload Scene"
#define SCENE_CURRENT_NAME "Current Scene"
#define SCENE_PORT(X, NAME, KIND, STORAGE, TYPE_NAME)                          \
    X(NAME,                                                                    \
      KIND,                                                                    \
      STORAGE,                                                                 \
      TYPE_NAME,                                                               \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = STORAGE }))
#define SCENE_EXEC(X)                                                          \
    SCENE_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL)
#define SCENE_THEN(X)                                                          \
    SCENE_PORT(X, "Then", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL)
#define SCENE_LOAD_INPUTS(X)                                                   \
    SCENE_EXEC(X)                                                              \
    SCENE_PORT(X, "Scene", NATIVE_PARAMETER_DATA, PIN_STR, NULL)
#define SCENE_CURRENT_OUTPUTS(X)                                               \
    SCENE_PORT(X, "Scene", NATIVE_PARAMETER_DATA, PIN_STR, NULL)
extern const native_definition_pack SCENE_DEFINITION_PACK;

#define SCENE_LOAD_PRESENTATION                                                \
    .category = "Scenes", .keywords = "level change switch goto next",         \
    .description = "After this frame, destroys every entity and starts the "   \
                   "scene named Scene. Committed save data carries over."

#define SCENE_RELOAD_PRESENTATION                                              \
    .category = "Scenes", .keywords = "restart retry reset level",             \
    .description = "After this frame, destroys every entity and starts the "   \
                   "running scene again. Committed save data carries over."

#define SCENE_CURRENT_PRESENTATION                                             \
    .category = "Scenes", .keywords = "level name active",                     \
    .description = "The running scene's name."

#endif
