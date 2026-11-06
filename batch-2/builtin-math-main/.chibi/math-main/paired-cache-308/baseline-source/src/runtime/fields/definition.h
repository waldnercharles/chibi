#ifndef CHIBI_FIELD_DEFINITION_H
#define CHIBI_FIELD_DEFINITION_H
#include "runtime/native_definitions.h"

/* The field evaluator and graph views share every port name and scalar type. */
#define FIELD_PORT(X, NAME, STORAGE)                                           \
    X(NAME,                                                                    \
      NATIVE_PARAMETER_DATA,                                                   \
      STORAGE,                                                                 \
      NULL,                                                                    \
      PIN_CONTAINER_SINGLE,                                                    \
      0,                                                                       \
      0,                                                                       \
      SCRIPT_REFERENCE_NONE,                                                   \
      false,                                                                   \
      ((pin_value){ 0 }))
#define FIELD_VALUE_OUT_PORTS(X) FIELD_PORT(X, "Value", PIN_F32)
#define FIELD_IN_CONSTANT_PORTS(X) FIELD_PORT(X, "Value", PIN_F32)
#define FIELD_IN_PARAMETER_PORTS(X) FIELD_PORT(X, "Name", PIN_STR)
#define FIELD_IN_ADD_PORTS(X)                                                  \
    FIELD_PORT(X, "A", PIN_F32)                                                \
    FIELD_PORT(X, "B", PIN_F32)
#define FIELD_IN_SUBTRACT_PORTS(X)                                             \
    FIELD_PORT(X, "A", PIN_F32)                                                \
    FIELD_PORT(X, "B", PIN_F32)
#define FIELD_IN_MULTIPLY_PORTS(X)                                             \
    FIELD_PORT(X, "A", PIN_F32)                                                \
    FIELD_PORT(X, "B", PIN_F32)
#define FIELD_IN_DIVIDE_PORTS(X)                                               \
    FIELD_PORT(X, "A", PIN_F32)                                                \
    FIELD_PORT(X, "B", PIN_F32)
#define FIELD_IN_MIN_PORTS(X)                                                  \
    FIELD_PORT(X, "A", PIN_F32)                                                \
    FIELD_PORT(X, "B", PIN_F32)
#define FIELD_IN_MAX_PORTS(X)                                                  \
    FIELD_PORT(X, "A", PIN_F32)                                                \
    FIELD_PORT(X, "B", PIN_F32)
#define FIELD_IN_ABS_PORTS(X) FIELD_PORT(X, "Value", PIN_F32)
#define FIELD_IN_NEGATE_PORTS(X) FIELD_PORT(X, "Value", PIN_F32)
#define FIELD_IN_SIN_PORTS(X) FIELD_PORT(X, "Value", PIN_F32)
#define FIELD_IN_COS_PORTS(X) FIELD_PORT(X, "Value", PIN_F32)
#define FIELD_IN_FLOOR_PORTS(X) FIELD_PORT(X, "Value", PIN_F32)
#define FIELD_IN_CLAMP_PORTS(X)                                                \
    FIELD_PORT(X, "Value", PIN_F32)                                            \
    FIELD_PORT(X, "Min", PIN_F32)                                              \
    FIELD_PORT(X, "Max", PIN_F32)
#define FIELD_IN_REMAP_PORTS(X)                                                \
    FIELD_PORT(X, "Value", PIN_F32)                                            \
    FIELD_PORT(X, "In Min", PIN_F32)                                           \
    FIELD_PORT(X, "In Max", PIN_F32)                                           \
    FIELD_PORT(X, "Out Min", PIN_F32)                                          \
    FIELD_PORT(X, "Out Max", PIN_F32)
#define FIELD_IN_NOISE_PORTS(X)                                                \
    FIELD_PORT(X, "X", PIN_F32)                                                \
    FIELD_PORT(X, "Y", PIN_F32)                                                \
    FIELD_PORT(X, "Seed Offset", PIN_F32)
#define FIELD_IN_FRACTAL_PORTS(X)                                              \
    FIELD_PORT(X, "X", PIN_F32)                                                \
    FIELD_PORT(X, "Y", PIN_F32)                                                \
    FIELD_PORT(X, "Seed Offset", PIN_F32)                                      \
    FIELD_PORT(X, "Octaves", PIN_F32)                                          \
    FIELD_PORT(X, "Lacunarity", PIN_F32)                                       \
    FIELD_PORT(X, "Gain", PIN_F32)
#define FIELD_IN_HASH_PORTS(X)                                                 \
    FIELD_PORT(X, "X", PIN_F32)                                                \
    FIELD_PORT(X, "Y", PIN_F32)                                                \
    FIELD_PORT(X, "Seed Offset", PIN_F32)
#define FIELD_IN_CIRCLE_PORTS(X)                                               \
    FIELD_PORT(X, "X", PIN_F32)                                                \
    FIELD_PORT(X, "Y", PIN_F32)                                                \
    FIELD_PORT(X, "Radius", PIN_F32)
#define FIELD_IN_BOX_PORTS(X)                                                  \
    FIELD_PORT(X, "X", PIN_F32)                                                \
    FIELD_PORT(X, "Y", PIN_F32)                                                \
    FIELD_PORT(X, "Half Width", PIN_F32)                                       \
    FIELD_PORT(X, "Half Height", PIN_F32)
#define FIELD_IN_CAPSULE_PORTS(X)                                              \
    FIELD_PORT(X, "X", PIN_F32)                                                \
    FIELD_PORT(X, "Y", PIN_F32)                                                \
    FIELD_PORT(X, "AX", PIN_F32)                                               \
    FIELD_PORT(X, "AY", PIN_F32)                                               \
    FIELD_PORT(X, "BX", PIN_F32)                                               \
    FIELD_PORT(X, "BY", PIN_F32)                                               \
    FIELD_PORT(X, "Radius", PIN_F32)
#define FIELD_IN_ROTATE_X_PORTS(X)                                             \
    FIELD_PORT(X, "X", PIN_F32)                                                \
    FIELD_PORT(X, "Y", PIN_F32)                                                \
    FIELD_PORT(X, "Angle", PIN_F32)
#define FIELD_IN_ROTATE_Y_PORTS(X)                                             \
    FIELD_PORT(X, "X", PIN_F32)                                                \
    FIELD_PORT(X, "Y", PIN_F32)                                                \
    FIELD_PORT(X, "Angle", PIN_F32)
#define FIELD_IN_SMOOTH_UNION_PORTS(X)                                         \
    FIELD_PORT(X, "A", PIN_F32)                                                \
    FIELD_PORT(X, "B", PIN_F32)                                                \
    FIELD_PORT(X, "Radius", PIN_F32)
#define FIELD_IN_SMOOTH_SUBTRACT_PORTS(X)                                      \
    FIELD_PORT(X, "A", PIN_F32)                                                \
    FIELD_PORT(X, "B", PIN_F32)                                                \
    FIELD_PORT(X, "Radius", PIN_F32)
#define FIELD_IN_SELECT_PORTS(X)                                               \
    FIELD_PORT(X, "Condition", PIN_F32)                                        \
    FIELD_PORT(X, "Positive", PIN_F32)                                         \
    FIELD_PORT(X, "Negative", PIN_F32)
#define FIELD_IN_OUTPUT_PORTS(X) FIELD_PORT(X, "Value", PIN_F32)
#define FIELD_IN_CELL_PORTS(X)                                                 \
    FIELD_PORT(X, "X", PIN_F32)                                                \
    FIELD_PORT(X, "Y", PIN_F32)                                                \
    FIELD_PORT(X, "Jitter", PIN_F32)                                           \
    FIELD_PORT(X, "Seed Offset", PIN_F32)
#define FIELD_NODE_CONSTANT_NAME "Field Constant"
#define FIELD_NODE_X_NAME "Field X"
#define FIELD_NODE_Y_NAME "Field Y"
#define FIELD_NODE_Z_NAME "Field Z"
#define FIELD_NODE_PARAMETER_NAME "Field Parameter"
#define FIELD_NODE_ADD_NAME "Field Add"
#define FIELD_NODE_SUBTRACT_NAME "Field Subtract"
#define FIELD_NODE_MULTIPLY_NAME "Field Multiply"
#define FIELD_NODE_DIVIDE_NAME "Field Divide"
#define FIELD_NODE_MIN_NAME "Field Min"
#define FIELD_NODE_MAX_NAME "Field Max"
#define FIELD_NODE_ABS_NAME "Field Abs"
#define FIELD_NODE_NEGATE_NAME "Field Negate"
#define FIELD_NODE_SIN_NAME "Field Sin"
#define FIELD_NODE_COS_NAME "Field Cos"
#define FIELD_NODE_FLOOR_NAME "Field Floor"
#define FIELD_NODE_CLAMP_NAME "Field Clamp"
#define FIELD_NODE_REMAP_NAME "Field Remap"
#define FIELD_NODE_NOISE_NAME "Field Noise"
#define FIELD_NODE_FRACTAL_NAME "Field Fractal"
#define FIELD_NODE_HASH_NAME "Field Hash"
#define FIELD_NODE_CIRCLE_NAME "Field Circle"
#define FIELD_NODE_BOX_NAME "Field Box"
#define FIELD_NODE_CAPSULE_NAME "Field Capsule"
#define FIELD_NODE_ROTATE_X_NAME "Field Rotate X"
#define FIELD_NODE_ROTATE_Y_NAME "Field Rotate Y"
#define FIELD_NODE_SMOOTH_UNION_NAME "Field Smooth Union"
#define FIELD_NODE_SMOOTH_SUBTRACT_NAME "Field Smooth Subtract"
#define FIELD_NODE_SELECT_NAME "Field Select"
#define FIELD_NODE_OUTPUT_NAME "Field Output"
#define FIELD_NODE_CELL_X_NAME "Field Cell X"
#define FIELD_NODE_CELL_Y_NAME "Field Cell Y"
#define FIELD_NODE_CELL_EDGE_NAME "Field Cell Edge"

extern const native_definition_pack FIELD_DEFINITION_PACK;
#endif
