#ifndef CHIBI_NATIVE_VECTORS_DEFINITION_H
#define CHIBI_NATIVE_VECTORS_DEFINITION_H

#include "runtime/native_definitions.h"

#include <cute_math3d.h>

extern const CF_V3 NATIVE_VECTOR_MIN, NATIVE_VECTOR_MAX;
#define NATIVE_VECTOR_PORT(X, NAME, TYPE, NOMINAL, VALUE)                      \
    X(NAME, NATIVE_PARAMETER_DATA, TYPE, NOMINAL, PIN_CONTAINER_SINGLE, 0, 0, SCRIPT_REFERENCE_NONE, false, VALUE)
#define NATIVE_VECTOR_V2(X, NAME)                                              \
    NATIVE_VECTOR_PORT(X, NAME, PIN_STRUCT, "vec2", ((pin_value){ 0 }))
#define NATIVE_VECTOR_VALUE_INPUTS(X) NATIVE_VECTOR_V2(X, "Value")
#define NATIVE_VECTOR_VECTOR_OUTPUTS(X) NATIVE_VECTOR_V2(X, "Result")
#define NATIVE_VECTOR_SCALAR_OUTPUTS(X)                                        \
    NATIVE_VECTOR_PORT(X, "Result", PIN_F32, "number", ((pin_value){ 0 }))
#define NATIVE_VECTOR_PAIR_INPUTS(X)                                           \
    NATIVE_VECTOR_V2(X, "A") NATIVE_VECTOR_V2(X, "B")
#define NATIVE_VECTOR_DIRECTION_INPUTS(X)                                      \
    NATIVE_VECTOR_V2(X, "Origin") NATIVE_VECTOR_V2(X, "Target")
#define NATIVE_VECTOR_DIRECTION_OUTPUTS(X) NATIVE_VECTOR_V2(X, "Direction")
#define NATIVE_VECTOR_ANGLE_INPUTS(X)                                          \
    NATIVE_VECTOR_PORT(X, "Angle", PIN_F64, "f64", ((pin_value){ 0 }))
#define NATIVE_VECTOR_RANDOM_INPUTS(X)                                                              \
    NATIVE_VECTOR_PORT(                                                                             \
        X,                                                                                          \
        "Min",                                                                                      \
        PIN_STRUCT,                                                                                 \
        "vec3",                                                                                     \
        ((pin_value){ .type = PIN_STRUCT, .type_name = "vec3", .ptr = (void *)&NATIVE_VECTOR_MIN }) \
    )                                                                                               \
    NATIVE_VECTOR_PORT(                                                                             \
        X,                                                                                          \
        "Max",                                                                                      \
        PIN_STRUCT,                                                                                 \
        "vec3",                                                                                     \
        ((pin_value){ .type = PIN_STRUCT, .type_name = "vec3", .ptr = (void *)&NATIVE_VECTOR_MAX }) \
    )
#define NATIVE_VECTOR_RANDOM_OUTPUTS(X)                                        \
    NATIVE_VECTOR_PORT(X, "Result", PIN_STRUCT, "vec3", ((pin_value){ 0 }))

#define NATIVE_NORMALIZE_NAME "Normalize Vector 2"
#define NATIVE_NORMALIZE_SOURCE "vec2_norm"
#define NATIVE_NORMALIZE_METHOD "norm"
#define NATIVE_VECTOR_LENGTH_NAME "Vector Length"
#define NATIVE_VECTOR_LENGTH_SOURCE "length"
#define NATIVE_VECTOR_DOT_NAME "Vector Dot"
#define NATIVE_VECTOR_DOT_SOURCE "dot"
#define NATIVE_VECTOR_DISTANCE_NAME "Vector Distance"
#define NATIVE_VECTOR_DISTANCE_SOURCE "distance"
#define NATIVE_DIRECTION_TO_NAME "Direction To"
#define NATIVE_DIRECTION_TO_SOURCE "direction_to"
#define NATIVE_DIRECTION_FROM_ANGLE_NAME "Direction From Angle"
#define NATIVE_DIRECTION_FROM_ANGLE_SOURCE "direction"
#define NATIVE_RANDOM_RANGE_VECTOR_NAME "Random Range Vector"
#define NATIVE_RANDOM_RANGE_VECTOR_SOURCE "random_range_vector"

/* Kernel, canonical name, source name, method, input/output lists, random. */
#define NATIVE_VECTOR_OPERATIONS(X)                                            \
    X(normalize,                                                               \
      NATIVE_NORMALIZE_NAME,                                                   \
      NATIVE_NORMALIZE_SOURCE,                                                 \
      NATIVE_NORMALIZE_METHOD,                                                 \
      NATIVE_VECTOR_VALUE_INPUTS,                                              \
      NATIVE_VECTOR_VECTOR_OUTPUTS,                                            \
      false)                                                                   \
    X(vector_length,                                                           \
      NATIVE_VECTOR_LENGTH_NAME,                                               \
      NATIVE_VECTOR_LENGTH_SOURCE,                                             \
      NULL,                                                                    \
      NATIVE_VECTOR_VALUE_INPUTS,                                              \
      NATIVE_VECTOR_SCALAR_OUTPUTS,                                            \
      false)                                                                   \
    X(vector_dot,                                                              \
      NATIVE_VECTOR_DOT_NAME,                                                  \
      NATIVE_VECTOR_DOT_SOURCE,                                                \
      NULL,                                                                    \
      NATIVE_VECTOR_PAIR_INPUTS,                                               \
      NATIVE_VECTOR_SCALAR_OUTPUTS,                                            \
      false)                                                                   \
    X(vector_distance,                                                         \
      NATIVE_VECTOR_DISTANCE_NAME,                                             \
      NATIVE_VECTOR_DISTANCE_SOURCE,                                           \
      NULL,                                                                    \
      NATIVE_VECTOR_PAIR_INPUTS,                                               \
      NATIVE_VECTOR_SCALAR_OUTPUTS,                                            \
      false)                                                                   \
    X(direction_to,                                                            \
      NATIVE_DIRECTION_TO_NAME,                                                \
      NATIVE_DIRECTION_TO_SOURCE,                                              \
      NULL,                                                                    \
      NATIVE_VECTOR_DIRECTION_INPUTS,                                          \
      NATIVE_VECTOR_DIRECTION_OUTPUTS,                                         \
      false)                                                                   \
    X(direction_from_angle,                                                    \
      NATIVE_DIRECTION_FROM_ANGLE_NAME,                                        \
      NATIVE_DIRECTION_FROM_ANGLE_SOURCE,                                      \
      NULL,                                                                    \
      NATIVE_VECTOR_ANGLE_INPUTS,                                              \
      NATIVE_VECTOR_DIRECTION_OUTPUTS,                                         \
      false)                                                                   \
    X(random_range_vector,                                                     \
      NATIVE_RANDOM_RANGE_VECTOR_NAME,                                         \
      NATIVE_RANDOM_RANGE_VECTOR_SOURCE,                                       \
      NULL,                                                                    \
      NATIVE_VECTOR_RANDOM_INPUTS,                                             \
      NATIVE_VECTOR_RANDOM_OUTPUTS,                                            \
      true)

extern const native_definition_pack NATIVE_VECTOR_DEFINITION_PACK;
#endif
