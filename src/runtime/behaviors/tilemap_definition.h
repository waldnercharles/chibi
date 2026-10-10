#ifndef CHIBI_TILEMAP_DEFINITION_H
#define CHIBI_TILEMAP_DEFINITION_H

#include "runtime/native_definitions.h"

#define GAME_TILEMAP_FRAME_NAME "Tilemap Frame At"
#define GAME_TILEMAP_BOX_NAME "Tilemap Occupied In Box"
#define GAME_TILEMAP_ENTITY(X)                                                 \
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
#define GAME_TILEMAP_VECTOR(X, NAME)                                           \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_STRUCT,                                                              \
      "CF_V2",                                                                 \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define GAME_TILEMAP_FRAME_INPUTS(X)                                           \
    GAME_TILEMAP_ENTITY(X) GAME_TILEMAP_VECTOR(X, "Point")
#define GAME_TILEMAP_BOX_INPUTS(X)                                             \
    GAME_TILEMAP_ENTITY(X)                                                     \
    GAME_TILEMAP_VECTOR(X, "Minimum") GAME_TILEMAP_VECTOR(X, "Maximum")
#define GAME_TILEMAP_OUTPUT(X, NAME)                                           \
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

extern const native_definition_pack GAME_TILEMAP_DEFINITION_PACK;
#endif
