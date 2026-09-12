#ifndef CHIBI_GRAPH_IR_H
#define CHIBI_GRAPH_IR_H

#include "graph/model/node.h"

#include <stddef.h>
#include <stdint.h>

#define GRAPH_IR_MAX_OPERANDS 16

typedef enum
{
    GRAPH_IR_OP_CONST,
    GRAPH_IR_OP_COPY,
    GRAPH_IR_OP_GET_COMPONENT,
    GRAPH_IR_OP_SET_COMPONENT,
    GRAPH_IR_OP_BREAK_STRUCT,
    GRAPH_IR_OP_MAKE_STRUCT,
    GRAPH_IR_OP_CALL,
} graph_ir_op;

typedef enum
{
    GRAPH_IR_VALUE_FLOW,
    GRAPH_IR_VALUE_FIXED,
    GRAPH_IR_VALUE_MANAGED,
} graph_ir_value_kind;

enum
{
    GRAPH_IR_VALUE_POD = 1u << 0,
    GRAPH_IR_VALUE_BORROWED = 1u << 1,
    GRAPH_IR_VALUE_SNAPSHOT = 1u << 2,
};

typedef struct
{
    pin_type type;
    pin_container container;
    graph_ir_value_kind kind;
    const char *type_name;
    const type_desc *type_desc;
    size_t size;
    size_t alignment;
    uint64_t layout_hash;
    unsigned flags;
} graph_ir_value;

typedef struct
{
    graph_ir_op op;
    const node *node;
    const type_desc *type;
    const node_access *access;
    node_fn fn;
    node_fast_fn fast;
    node_fast_direct_fn direct;
    int direct_count;
    uint64_t operation_id;
    uint64_t resource_id;
    int output;
    int input_count;
    int inputs[GRAPH_IR_MAX_OPERANDS];
    int output_count;
    int outputs[GRAPH_IR_MAX_OPERANDS];
    pin_value constant;
} graph_ir_instruction;

/* Behavior blocks use slot indices and resolved edges. They are independent of
 * the straight-line System instructions above. Both behavior backends consume
 * these same operations; nodes and pin IDs are retained only as source maps. */
typedef enum
{
    GRAPH_IR_MEMO_BRANCH,
    GRAPH_IR_BIND_INPUT,
    GRAPH_IR_READ_SLOT,
    GRAPH_IR_EVALUATE,
} graph_ir_behavior_op;

typedef struct
{
    graph_ir_behavior_op op;
    const node *node;
    int pin;
    const node *source;
    int source_pin;
    const type_field *field;
    node_fn conversion;
    int target;
} graph_ir_behavior_instruction;

typedef struct
{
    int first;
    int count;
    bool output;
    uint64_t graph_id;
    uint64_t node_id;
    uint64_t pin_id;
} graph_ir_block;

typedef enum
{
    GRAPH_IR_ENTRY,
    GRAPH_IR_BRANCH,
    GRAPH_IR_SEQUENCE,
    GRAPH_IR_LOOP,
    GRAPH_IR_FOREACH,
    GRAPH_IR_WHILE,
    GRAPH_IR_BREAK,
    GRAPH_IR_YIELD,
    GRAPH_IR_FUNCTION_CALL,
    GRAPH_IR_FUNCTION_RETURN,
    GRAPH_IR_HOST_EFFECT,
} graph_ir_control_op;

typedef struct
{
    graph_ir_control_op op;
    const node *node;
    node_flow_fn host;
    uint64_t graph_id;
    uint64_t callee_id;
    unsigned effects;
} graph_ir_control;

typedef struct graph_ir
{
    graph_ir_value *values;
    graph_ir_instruction *instructions;
    int event_delta;
    graph_ir_behavior_instruction *behavior;
    graph_ir_block *blocks;
    graph_ir_control *control;
} graph_ir;

void graph_ir_init(graph_ir *ir);
void graph_ir_free(graph_ir *ir);
int graph_ir_add_value(graph_ir *ir, graph_ir_value value);
int graph_ir_add_instruction(graph_ir *ir, const graph_ir_instruction *instruction);
int graph_ir_validate(const graph_ir *ir, const char **reason);
uint64_t graph_ir_name_id(const char *name);
uint64_t graph_ir_hash(const graph_ir *ir);

#endif
