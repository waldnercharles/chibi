#ifndef CHIBI_VISIBILITY_DEFINITION_H
#define CHIBI_VISIBILITY_DEFINITION_H

#include "runtime/native_definitions.h"

#define VISIBILITY_SAMPLE_NAME "Visibility At"
#define VISIBILITY_SAMPLE_SOURCE "visibility_at"
#define VISIBILITY_SAMPLE_INPUTS(X)                                            \
    X("Position",                                                              \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_STRUCT,                                                              \
      "vec2",                                                                  \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_STRUCT, .type_name = "vec2" }))
#define VISIBILITY_SAMPLE_OUTPUTS(X)                                           \
    X("Visible",                                                               \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_BOOL,                                                                \
      "bool",                                                                  \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_BOOL, .type_name = "bool" }))                  \
    X("Explored",                                                              \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_BOOL,                                                                \
      "bool",                                                                  \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_BOOL, .type_name = "bool" }))                  \
    X("Obstacle",                                                              \
      NATIVE_PARAMETER_DATA,                                                   \
      PIN_BOOL,                                                                \
      "bool",                                                                  \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ .type = PIN_BOOL, .type_name = "bool" }))

extern const native_definition_pack VISIBILITY_DEFINITION_PACK;

#define VISIBILITY_SAMPLE_PRESENTATION                                         \
    .category = "Visibility", .keywords = "fog sight explored terrain memory", \
    .description = "Samples the fog-of-war cell at Position; Visible is "      \
                   "always "                                                   \
                   "true without fog and false outside its grid."

#endif
