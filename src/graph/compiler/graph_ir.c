#include "graph/compiler/graph_ir.h"

#include "support/array.h"

#include <stdbool.h>
#include <string.h>

static const uint64_t GRAPH_IR_HASH_OFFSET = UINT64_C(1469598103934665603);
static const uint64_t GRAPH_IR_HASH_PRIME = UINT64_C(1099511628211);

void graph_ir_init(graph_ir *ir)
{
    *ir = (graph_ir){ .event_delta = -1 };
}

void graph_ir_free(graph_ir *ir)
{
    if (!ir) {
        return;
    }
    afree(ir->values);
    afree(ir->instructions);
    afree(ir->behavior);
    afree(ir->blocks);
    afree(ir->control);
    ir->event_delta = -1;
}

int graph_ir_add_value(graph_ir *ir, graph_ir_value value)
{
    if (!ir) {
        return -1;
    }
    apush(ir->values, value);
    return acount(ir->values) - 1;
}

int graph_ir_add_instruction(graph_ir *ir, const graph_ir_instruction *instruction)
{
    if (!ir || !instruction) {
        return -1;
    }
    apush(ir->instructions, *instruction);
    return acount(ir->instructions) - 1;
}

static int valid_value(const graph_ir *ir, int value)
{
    return value >= 0 && value < acount(ir->values);
}

static int valid_inputs(const graph_ir *ir, const graph_ir_instruction *instruction)
{
    if (instruction->input_count < 0 || instruction->input_count > GRAPH_IR_MAX_OPERANDS) {
        return 0;
    }
    for (int i = 0; i < instruction->input_count; i++) {
        if (!valid_value(ir, instruction->inputs[i])) {
            return 0;
        }
    }
    return 1;
}

static int valid_outputs(const graph_ir *ir, const graph_ir_instruction *instruction)
{
    if (instruction->output_count < 0 ||
        instruction->output_count > GRAPH_IR_MAX_OPERANDS) {
        return 0;
    }
    for (int i = 0; i < instruction->output_count; i++) {
        if (!valid_value(ir, instruction->outputs[i])) {
            return 0;
        }
    }
    return 1;
}

int graph_ir_validate(const graph_ir *ir, const char **reason)
{
    static const char *const invalid = "invalid graph IR";
    if (reason) {
        *reason = NULL;
    }
    if (!ir) {
        if (reason) {
            *reason = invalid;
        }
        return -1;
    }
    if (ir->event_delta >= 0 && !valid_value(ir, ir->event_delta)) {
        if (reason) {
            *reason = "event delta register is out of range";
        }
        return -1;
    }
    for (int i = 0; i < acount(ir->instructions); i++) {
        const graph_ir_instruction *instruction = &ir->instructions[i];
        if (instruction->output < -1 ||
            (instruction->output >= 0 && !valid_value(ir, instruction->output)) ||
            !valid_inputs(ir, instruction) || !valid_outputs(ir, instruction)) {
            if (reason) {
                *reason = invalid;
            }
            return -1;
        }
        switch (instruction->op) {
            case GRAPH_IR_OP_CONST:
            case GRAPH_IR_OP_COPY:
            case GRAPH_IR_OP_CALL:
            case GRAPH_IR_OP_GET_COMPONENT:
            case GRAPH_IR_OP_MAKE_STRUCT:
                if (instruction->output < 0) {
                    if (reason) {
                        *reason = "value instruction has no output";
                    }
                    return -1;
                }
                break;
            case GRAPH_IR_OP_SET_COMPONENT:
                if (instruction->output >= 0 || instruction->input_count != 1) {
                    if (reason) {
                        *reason = "component write has an invalid shape";
                    }
                    return -1;
                }
                break;
            case GRAPH_IR_OP_BREAK_STRUCT:
                if (instruction->output >= 0 || instruction->input_count != 1 ||
                    instruction->output_count == 0) {
                    if (reason) {
                        *reason = "struct break has an invalid shape";
                    }
                    return -1;
                }
                break;
            default:
                if (reason) {
                    *reason = "unknown graph IR operation";
                }
                return -1;
        }
    }
    return 0;
}

uint64_t graph_ir_name_id(const char *name)
{
    if (!name) {
        return 0;
    }
    uint64_t hash = GRAPH_IR_HASH_OFFSET;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        hash ^= *p;
        hash *= GRAPH_IR_HASH_PRIME;
    }
    return hash;
}
