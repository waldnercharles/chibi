#pragma once

/* Deliberately independent of the behavior VM. All operands are scalar slots;
 * each instruction writes exactly one slot in topological order. */
typedef enum
{
    FIELD_CONSTANT,
    FIELD_X,
    FIELD_Y,
    FIELD_PARAMETER,
    FIELD_ADD,
    FIELD_SUBTRACT,
    FIELD_MULTIPLY,
    FIELD_DIVIDE,
    FIELD_MIN,
    FIELD_MAX,
    FIELD_ABS,
    FIELD_NEGATE,
    FIELD_SIN,
    FIELD_COS,
    FIELD_FLOOR,
    FIELD_CLAMP,
    FIELD_REMAP,
    FIELD_NOISE,
    FIELD_FRACTAL,
    FIELD_HASH,
    FIELD_CIRCLE,
    FIELD_BOX,
    FIELD_CAPSULE,
    FIELD_ROTATE_X,
    FIELD_ROTATE_Y,
    FIELD_SMOOTH_UNION,
    FIELD_SMOOTH_SUBTRACT,
    FIELD_SELECT,
    FIELD_OUTPUT,
    /* Appended so prepared opcodes keep their values. */
    FIELD_Z,
    FIELD_CELL_X,
    FIELD_CELL_Y,
    FIELD_CELL_EDGE,
    FIELD_OP_COUNT
} field_op;

#define FIELD_INPUT_MAX 7
#define FIELD_CALL_ARGUMENT_MAX 32
#define FIELD_INSTRUCTION_MAX 4096

typedef struct
{
    field_op op;
    unsigned spatial; /* X=1, Y=2; propagated once during compilation. */
    int inputs[FIELD_INPUT_MAX];
    int count; /* Inputs in use. */
    int parameter;
    float value;
} field_ir;
