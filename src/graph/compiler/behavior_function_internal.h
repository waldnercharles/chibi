#ifndef CHIBI_BEHAVIOR_FUNCTION_INTERNAL_H
#define CHIBI_BEHAVIOR_FUNCTION_INTERNAL_H
#include "graph/compiler/behavior_function.h"

typedef enum
{
    BF_CONSTANT,
    BF_VARIABLE,
    BF_DELTA,
    BF_ADD,
    BF_SUB,
    BF_MUL,
    BF_DIV,
    BF_NEG,
    BF_ABS,
    BF_LT,
    BF_LE,
    BF_GT,
    BF_GE,
    BF_EQ,
    BF_NE,
    BF_STORE,
    BF_CHECK,
    BF_JUMP,
    BF_BRANCH,
    BF_END
} behavior_function_op;
typedef struct
{
    behavior_function_op op;
    pin_type type;
    int output, left, right, target;
    uint64_t node_id;
    pin_value value;
} behavior_function_instruction;
struct behavior_function
{
    behavior_function_instruction *instructions;
    pin_type *slots;
    void *code;
};
void *behavior_function_jit(const behavior_function *function);
#endif
