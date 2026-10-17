#ifndef CHIBI_TERRAIN_STORAGE_DEFINITION_H
#define CHIBI_TERRAIN_STORAGE_DEFINITION_H

#include "runtime/native_definitions.h"

#define TERRAIN_SAVE_NAME "Save Terrain"
#define TERRAIN_LOAD_NAME "Load Terrain"
#define TERRAIN_STORAGE_RESULT_NAME "Terrain Storage Result"
#define TERRAIN_STORAGE_PORT(X, NAME, KIND, STORAGE, TYPE)                     \
    X(NAME,                                                                    \
      KIND,                                                                    \
      STORAGE,                                                                 \
      TYPE,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = STORAGE }))
#define TERRAIN_STORAGE_REQUEST_INPUTS(X)                                              \
    TERRAIN_STORAGE_PORT(X, "Exec", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL)             \
    TERRAIN_STORAGE_PORT(X, "Entity", NATIVE_PARAMETER_DATA, PIN_HANDLE, "ecs.entity") \
    TERRAIN_STORAGE_PORT(X, "Identity", NATIVE_PARAMETER_DATA, PIN_STR, NULL)
#define TERRAIN_STORAGE_REQUEST_OUTPUTS(X)                                     \
    TERRAIN_STORAGE_PORT(X, "Then", NATIVE_PARAMETER_EXEC, PIN_FLOW, NULL)     \
    TERRAIN_STORAGE_PORT(X, "Request", NATIVE_PARAMETER_DATA, PIN_I32, "int")  \
    TERRAIN_STORAGE_PORT(X, "Error", NATIVE_PARAMETER_DATA, PIN_STR, NULL)
#define TERRAIN_STORAGE_RESULT_INPUTS(X)                                       \
    TERRAIN_STORAGE_PORT(X, "Request", NATIVE_PARAMETER_DATA, PIN_I32, "int")
#define TERRAIN_STORAGE_RESULT_OUTPUTS(X)                                         \
    TERRAIN_STORAGE_PORT(X, "Pending", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool")   \
    TERRAIN_STORAGE_PORT(X, "Succeeded", NATIVE_PARAMETER_DATA, PIN_BOOL, "bool") \
    TERRAIN_STORAGE_PORT(X, "Error", NATIVE_PARAMETER_DATA, PIN_STR, NULL)
#define TERRAIN_SAVE_PRESENTATION                                              \
    .category = "Storage", .keywords = "terrain checkpoint persistent save",   \
    .description =                                                             \
        "Queues a terrain snapshot under Identity in host save storage. "      \
        "Request is a positive token, or zero with Error when rejected. "      \
        "Poll Terrain Storage Result for completion. One request may "         \
        "be pending per world; Identity contains 1-64 bytes."
#define TERRAIN_LOAD_PRESENTATION                                              \
    .category = "Storage", .keywords = "terrain checkpoint persistent load",   \
    .description =                                                             \
        "Queues a terrain restore from Identity in host save storage. "        \
        "Request is a positive token, or zero with Error when rejected. "      \
        "Poll Terrain Storage Result for completion. Identity and "            \
        "the authored terrain recipe must match the saved snapshot."
#define TERRAIN_STORAGE_RESULT_PRESENTATION                                       \
    .category = "Storage", .keywords = "terrain checkpoint result pending error", \
    .description =                                                                \
        "Reads the latest terrain request result. Pending means queued; "         \
        "Succeeded means completed. A new accepted request expires "              \
        "the old token. Failed, unknown or expired requests return Error. "       \
        "Binary persistence requires native host-bound save storage."

extern const native_definition_pack TERRAIN_STORAGE_DEFINITION_PACK;

#endif
