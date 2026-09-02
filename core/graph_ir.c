#include "graph_ir.h"

#include "array.h"

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

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= GRAPH_IR_HASH_PRIME;
    }
    return hash;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    return hash_bytes(hash, &value, sizeof value);
}

static uint64_t hash_constant(uint64_t hash, const graph_ir *ir, const graph_ir_instruction *instruction)
{
    const pin_value *value = &instruction->constant;
    hash = hash_u64(hash, (uint64_t)value->type);
    hash = hash_u64(hash, (uint64_t)value->container);
    hash = hash_u64(hash, graph_ir_name_id(value->type_name));
    if (instruction->output < 0 || !valid_value(ir, instruction->output)) {
        return hash;
    }
    size_t size = ir->values[instruction->output].size;
    switch (value->type) {
        case PIN_BOOL: return hash_bytes(hash, &value->b, sizeof value->b);
        case PIN_I8: return hash_bytes(hash, &value->i8, sizeof value->i8);
        case PIN_U8: return hash_bytes(hash, &value->u8, sizeof value->u8);
        case PIN_I16: return hash_bytes(hash, &value->i16, sizeof value->i16);
        case PIN_U16: return hash_bytes(hash, &value->u16, sizeof value->u16);
        case PIN_I32: return hash_bytes(hash, &value->i, sizeof value->i);
        case PIN_U32: return hash_bytes(hash, &value->u, sizeof value->u);
        case PIN_I64: return hash_bytes(hash, &value->i64, sizeof value->i64);
        case PIN_U64: return hash_bytes(hash, &value->u64, sizeof value->u64);
        case PIN_F32: return hash_bytes(hash, &value->f, sizeof value->f);
        case PIN_F64: return hash_bytes(hash, &value->d, sizeof value->d);
        case PIN_NAME:
        case PIN_STR: return hash_u64(hash, graph_ir_name_id(value->str));
        case PIN_HANDLE:
            return hash_bytes(hash, &value->u64, sizeof value->u64);
        case PIN_ENUM: return hash_bytes(hash, &value->e, sizeof value->e);
        case PIN_STRUCT:
            return value->ptr ? hash_bytes(hash, value->ptr, size)
                              : hash_u64(hash, 0);
        case PIN_FLOW: return hash;
    }
    return hash;
}

uint64_t graph_ir_hash(const graph_ir *ir)
{
    if (graph_ir_validate(ir, NULL) != 0) {
        return 0;
    }
    uint64_t hash = GRAPH_IR_HASH_OFFSET;
    hash = hash_u64(hash, (uint64_t)ir->event_delta);
    hash = hash_u64(hash, (uint64_t)acount(ir->values));
    for (int i = 0; i < acount(ir->values); i++) {
        const graph_ir_value *value = &ir->values[i];
        hash = hash_u64(hash, (uint64_t)value->type);
        hash = hash_u64(hash, (uint64_t)value->container);
        hash = hash_u64(hash, (uint64_t)value->kind);
        hash = hash_u64(hash, graph_ir_name_id(value->type_name));
        hash = hash_u64(hash, value->layout_hash);
        hash = hash_u64(hash, (uint64_t)value->size);
        hash = hash_u64(hash, (uint64_t)value->alignment);
        hash = hash_u64(hash, value->flags);
    }
    hash = hash_u64(hash, (uint64_t)acount(ir->instructions));
    for (int i = 0; i < acount(ir->instructions); i++) {
        const graph_ir_instruction *instruction = &ir->instructions[i];
        hash = hash_u64(hash, (uint64_t)instruction->op);
        hash = hash_u64(hash, instruction->operation_id);
        hash = hash_u64(hash, instruction->resource_id);
        hash = hash_u64(hash, (uint64_t)instruction->output);
        hash = hash_u64(hash, (uint64_t)instruction->input_count);
        for (int j = 0; j < instruction->input_count; j++) {
            hash = hash_u64(hash, (uint64_t)instruction->inputs[j]);
        }
        hash = hash_u64(hash, (uint64_t)instruction->output_count);
        for (int j = 0; j < instruction->output_count; j++) {
            hash = hash_u64(hash, (uint64_t)instruction->outputs[j]);
        }
        if (instruction->node && instruction->node->desc) {
            hash = hash_u64(hash, instruction->node->id);
            hash = hash_u64(hash, graph_ir_name_id(instruction->node->desc->name));
            hash = hash_u64(hash, instruction->node->desc->jit_kind);
            hash = hash_u64(hash, instruction->node->desc->jit_flags);
        }
        hash = hash_constant(hash, ir, instruction);
    }
    return hash ? hash : 1;
}
