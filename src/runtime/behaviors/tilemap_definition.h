#ifndef CHIBI_TILEMAP_DEFINITION_H
#define CHIBI_TILEMAP_DEFINITION_H

#include "runtime/native_definitions.h"

#define TILEMAP_FRAME_NAME "Tilemap Frame At"
#define TILEMAP_BOX_NAME "Tilemap Occupied In Box"
#define TILEMAP_ENTITY(X)                                                      \
    X("Tilemap",                                                               \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_HANDLE,                                                              \
      "ecs.entity",                                                            \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define TILEMAP_VECTOR(X, NAME)                                                \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_STRUCT,                                                              \
      "vec2",                                                                  \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define TILEMAP_FRAME_INPUTS(X) TILEMAP_ENTITY(X) TILEMAP_VECTOR(X, "Point")
#define TILEMAP_BOX_INPUTS(X)                                                  \
    TILEMAP_ENTITY(X)                                                          \
    TILEMAP_VECTOR(X, "Minimum") TILEMAP_VECTOR(X, "Maximum")
#define TILEMAP_OUTPUT(X, NAME)                                                \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_I32,                                                                 \
      "int",                                                                   \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))

extern const native_definition_pack TILEMAP_DEFINITION_PACK;
#endif
