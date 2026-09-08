#include "graph/compiler/graph_jit_sljit.h"

#include "graph/model/pin_conv.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef CBLUEPRINT_HAVE_SJLIT
#define CBLUEPRINT_HAVE_SJLIT 0
#endif

#if CBLUEPRINT_HAVE_SJLIT
#include <sljitLir.h>
#endif

struct graph_sljit_program
{
#if CBLUEPRINT_HAVE_SJLIT
    void *code;
    void *batch_code;
    size_t *offsets;
    int *column_indices;
    uint64_t *column_ids;
    size_t *column_sizes;
    int column_count;
    int register_mode;
    int simd_mode;
    uint64_t *operation_ids;
#else
    int disabled;
#endif
};

static graph_sljit_program *compile_fail(const char **reason, const char *message)
{
    if (reason) {
        *reason = message;
    }
    return NULL;
}

#if CBLUEPRINT_HAVE_SJLIT

typedef struct
{
    graph_sljit_program *program;
    const graph_ir *ir;
    struct sljit_compiler *compiler;
    size_t *offsets;
    size_t storage_size;
    sljit_s32 context_reg;
    sljit_sw delta_offset;
    sljit_sw column_stride;
    sljit_sw column_data_offset;
    int batch;
    int persistent_delta;
    int cached_column;
    int cached_valid;
} sljit_builder;

static void discard_builder(sljit_builder *builder)
{
    if (builder->compiler) {
        sljit_free_compiler(builder->compiler);
    }
    graph_sljit_free(builder->program);
}

static size_t align_offset(size_t offset, size_t alignment)
{
    size_t remainder = offset % alignment;
    return remainder ? offset + alignment - remainder : offset;
}

static int emit_op1(sljit_builder *builder, sljit_s32 op, sljit_s32 dst, sljit_sw dstw, sljit_s32 src, sljit_sw srcw)
{
    return sljit_emit_op1(builder->compiler, op, dst, dstw, src, srcw) != SLJIT_SUCCESS;
}

static int emit_op2(
    sljit_builder *builder,
    sljit_s32 op,
    sljit_s32 dst,
    sljit_sw dstw,
    sljit_s32 src1,
    sljit_sw src1w,
    sljit_s32 src2,
    sljit_sw src2w
);

static int emit_local_address(sljit_builder *builder, sljit_s32 reg, size_t offset)
{
    return sljit_get_local_base(builder->compiler, reg, 0, (sljit_sw)offset) !=
           SLJIT_SUCCESS;
}

static int copy_chunk_op(size_t size, size_t *chunk)
{
    if (size >= sizeof(sljit_sw)) {
        *chunk = sizeof(sljit_sw);
        return SLJIT_MOV;
    }
    if (size >= 4) {
        *chunk = 4;
        return SLJIT_MOV32;
    }
    if (size >= 2) {
        *chunk = 2;
        return SLJIT_MOV_U16;
    }
    *chunk = 1;
    return SLJIT_MOV_U8;
}

static int emit_copy(
    sljit_builder *builder,
    sljit_s32 dst_base,
    sljit_sw dst_offset,
    sljit_s32 src_base,
    sljit_sw src_offset,
    size_t size,
    sljit_s32 scratch
)
{
    while (size) {
        size_t chunk;
        int op = copy_chunk_op(size, &chunk);
        if (emit_op1(builder, op, scratch, 0, SLJIT_MEM1(src_base), src_offset) ||
            emit_op1(builder, op, SLJIT_MEM1(dst_base), dst_offset, scratch, 0)) {
            return -1;
        }
        size -= chunk;
        src_offset += (sljit_sw)chunk;
        dst_offset += (sljit_sw)chunk;
    }
    return 0;
}

static int emit_zero(sljit_builder *builder, size_t offset, size_t size)
{
    while (size) {
        size_t chunk;
        int op = copy_chunk_op(size, &chunk);
        if (emit_op1(builder, op, SLJIT_R0, 0, SLJIT_IMM, 0) ||
            emit_op1(builder, op, SLJIT_MEM1(SLJIT_SP), (sljit_sw)offset, SLJIT_R0, 0)) {
            return -1;
        }
        size -= chunk;
        offset += chunk;
    }
    return 0;
}

static uint64_t value_bits(pin_value value)
{
    switch (value.type) {
        case PIN_BOOL: return value.b;
        case PIN_I8: return (uint8_t)value.i8;
        case PIN_U8: return value.u8;
        case PIN_I16: return (uint16_t)value.i16;
        case PIN_U16: return value.u16;
        case PIN_I32: return (uint32_t)value.i;
        case PIN_U32: return value.u;
        case PIN_I64: return (uint64_t)value.i64;
        case PIN_U64: return value.u64;
        case PIN_F32: {
            uint32_t bits;
            memcpy(&bits, &value.f, sizeof bits);
            return bits;
        }
        case PIN_F64: {
            uint64_t bits;
            memcpy(&bits, &value.d, sizeof bits);
            return bits;
        }
        case PIN_HANDLE: return value.u64;
        case PIN_ENUM: return (uint32_t)value.e;
        case PIN_FLOW:
        case PIN_NAME:
        case PIN_STR:
        case PIN_STRUCT: break;
    }
    return 0;
}

static int emit_constant(sljit_builder *builder, const graph_ir_value *value, pin_value constant, size_t offset)
{
    if (value->type == PIN_FLOW) {
        return 0;
    }
    if (value->type == PIN_STRUCT) {
        if (!constant.ptr) {
            return -1;
        }
        if (emit_op1(
                builder,
                SLJIT_MOV_P,
                SLJIT_R0,
                0,
                SLJIT_IMM,
                (sljit_sw)(intptr_t)constant.ptr
            )) {
            return -1;
        }
        return emit_copy(builder, SLJIT_SP, (sljit_sw)offset, SLJIT_R0, 0, value->size, SLJIT_R1);
    }

    uint64_t bits = value_bits(constant);
    size_t size = value->size;
    while (size) {
        size_t chunk;
        int op = copy_chunk_op(size, &chunk);
        if (emit_op1(builder, op, SLJIT_MEM1(SLJIT_SP), (sljit_sw)offset, SLJIT_IMM, (sljit_sw)bits)) {
            return -1;
        }
        bits >>= chunk * 8;
        size -= chunk;
        offset += chunk;
    }
    return 0;
}

static sljit_sw report_error(const graph_jit_context *context, const uint64_t *operation_id)
{
    if (context->host && context->host->error) {
        context->host->error(context->host->user, GRAPH_JIT_ERROR, *operation_id);
    }
    return GRAPH_JIT_ERROR;
}

static int emit_column_address(sljit_builder *builder, int instruction)
{
    int column = builder->program->column_indices[instruction];
    if (column < 0) {
        return -1;
    }
    if (emit_op1(builder, SLJIT_MOV_P, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), (sljit_sw)offsetof(graph_jit_context, columns)) ||
        emit_op1(
            builder,
            SLJIT_MOV_P,
            SLJIT_R0,
            0,
            SLJIT_MEM1(SLJIT_R1),
            (sljit_sw)(column * (int)sizeof(graph_jit_column) +
                       (int)offsetof(graph_jit_column, data))
        )) {
        return -1;
    }
    return 0;
}

static int emit_register_column_address(sljit_builder *builder, int instruction)
{
    int column = builder->program->column_indices[instruction];
    if (column < 0) {
        return -1;
    }
    if (!builder->batch) {
        return emit_op1(
            builder,
            SLJIT_MOV_P,
            SLJIT_R0,
            0,
            SLJIT_MEM1(SLJIT_R2),
            column * builder->column_stride + builder->column_data_offset
        );
    }
    size_t size = builder->program->column_sizes[column];
    if (size > 1 &&
        (emit_op2(builder, SLJIT_MUL, SLJIT_R6, 0, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)size) ||
         emit_op2(builder, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R(7 + column), 0, SLJIT_R6, 0))) {
        return -1;
    }
    if (size == 1) {
        return emit_op1(builder, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_R(7 + column), 0);
    }
    return 0;
}

static int emit_batch_column_bases(sljit_builder *builder)
{
    for (int i = 0; i < builder->program->column_count; i++) {
        if (emit_op1(
                builder,
                SLJIT_MOV_P,
                SLJIT_R(7 + i),
                0,
                SLJIT_MEM1(SLJIT_R2),
                i * (int)sizeof(graph_jit_column) + (int)offsetof(graph_jit_column, data)
            )) {
            return -1;
        }
    }
    return 0;
}

static int emit_column_base(sljit_builder *builder, sljit_sw offset)
{
    return emit_op1(builder, SLJIT_MOV_P, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R0), offset);
}

static int emit_error(sljit_builder *builder, int instruction)
{
    if (emit_op1(builder, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0) ||
        emit_op1(
            builder,
            SLJIT_MOV_P,
            SLJIT_R1,
            0,
            SLJIT_IMM,
            (sljit_sw)(intptr_t)&builder->program->operation_ids[instruction]
        ) ||
        sljit_emit_icall(builder->compiler, SLJIT_CALL, SLJIT_ARGS2(W, P, P), SLJIT_IMM, SLJIT_FUNC_ADDR(report_error)) !=
            SLJIT_SUCCESS ||
        sljit_emit_return(builder->compiler, SLJIT_MOV, SLJIT_R0, 0) != SLJIT_SUCCESS) {
        return -1;
    }
    return 0;
}

static int emit_call_error_check(sljit_builder *builder, int instruction)
{
    struct sljit_jump
        *fail = sljit_emit_cmp(builder->compiler, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    struct sljit_jump *skip = sljit_emit_jump(builder->compiler, SLJIT_JUMP);
    struct sljit_label *error = sljit_emit_label(builder->compiler);
    if (!fail || !skip || !error || emit_error(builder, instruction)) {
        return -1;
    }
    struct sljit_label *next = sljit_emit_label(builder->compiler);
    if (!next) {
        return -1;
    }
    sljit_set_label(fail, error);
    sljit_set_label(skip, next);
    return 0;
}

static int direct_available(const graph_ir_instruction *instruction)
{
    switch (instruction->direct_count) {
        case 1: return instruction->direct.zero != NULL;
        case 2: return instruction->direct.one != NULL;
        case 3: return instruction->direct.two != NULL;
        case 4: return instruction->direct.three != NULL;
    }
    return 0;
}

static int emit_direct_call(sljit_builder *builder, const graph_ir_instruction *instruction, int index)
{
    if (!direct_available(instruction) ||
        instruction->input_count != instruction->direct_count - 1 ||
        instruction->output < 0) {
        return -1;
    }
    if (emit_local_address(builder, SLJIT_R0, builder->offsets[instruction->output])) {
        return -1;
    }
    for (int i = 0; i < instruction->input_count; i++) {
        if (emit_local_address(
                builder,
                SLJIT_R1 + i,
                builder->offsets[instruction->inputs[i]]
            )) {
            return -1;
        }
    }

    sljit_s32 args;
    sljit_sw address;
    switch (instruction->direct_count) {
        case 1:
            args = SLJIT_ARGS1(W, P);
            address = SLJIT_FUNC_ADDR(instruction->direct.zero);
            break;
        case 2:
            args = SLJIT_ARGS2(W, P, P);
            address = SLJIT_FUNC_ADDR(instruction->direct.one);
            break;
        case 3:
            args = SLJIT_ARGS3(W, P, P, P);
            address = SLJIT_FUNC_ADDR(instruction->direct.two);
            break;
        case 4:
            args = SLJIT_ARGS4(W, P, P, P, P);
            address = SLJIT_FUNC_ADDR(instruction->direct.three);
            break;
        default: return -1;
    }
    if (sljit_emit_icall(builder->compiler, SLJIT_CALL, args, SLJIT_IMM, address) !=
        SLJIT_SUCCESS) {
        return -1;
    }
    return emit_call_error_check(builder, index);
}

enum
{
    SJLIT_FLOAT_REGISTER_COUNT = 8,
};

typedef enum
{
    SJLIT_VALUE_NONE,
    SJLIT_VALUE_F32,
    SJLIT_VALUE_VEC2,
    SJLIT_VALUE_SIMD2,
} sljit_value_kind;

typedef struct
{
    sljit_value_kind kind;
    sljit_s32 regs[2];
} sljit_value_location;

typedef struct
{
    sljit_builder *builder;
    sljit_value_location *locations;
    int *refs;
    int *last_use;
    int *defined_at;
    int simd;
} sljit_register_builder;

static int value_named(const graph_ir_value *value, const char *name)
{
    return value->type_name && strcmp(value->type_name, name) == 0;
}

static sljit_value_kind value_location_kind(const graph_ir_value *value)
{
    if (value->type == PIN_F32 && value->size == sizeof(float)) {
        return SJLIT_VALUE_F32;
    }
    if (value->type != PIN_STRUCT) {
        return SJLIT_VALUE_NONE;
    }
    if (value->size == sizeof(float) && value_named(value, "GameMovement")) {
        return SJLIT_VALUE_F32;
    }
    if (value->size == 2 * sizeof(float) &&
        (value_named(value, "CF_V2") || value_named(value, "GameInput") ||
         value_named(value, "GameTransform"))) {
        return SJLIT_VALUE_VEC2;
    }
    return SJLIT_VALUE_NONE;
}

static int location_reg_count(sljit_value_kind kind)
{
    return kind == SJLIT_VALUE_VEC2 ? 2 : 1;
}

static int location_available(const sljit_value_location *location)
{
    return location->kind != SJLIT_VALUE_NONE;
}

static void retain_location(sljit_register_builder *registers, sljit_value_location location)
{
    for (int i = 0; i < location_reg_count(location.kind); i++) {
        int index = location.regs[i] - SLJIT_FR0;
        assert(index >= 0 && index < SJLIT_FLOAT_REGISTER_COUNT);
        registers->refs[index]++;
    }
}

static void release_location(sljit_register_builder *registers, int value)
{
    sljit_value_location *location = &registers->locations[value];
    for (int i = 0; i < location_reg_count(location->kind); i++) {
        int index = location->regs[i] - SLJIT_FR0;
        assert(index >= 0 && index < SJLIT_FLOAT_REGISTER_COUNT);
        assert(registers->refs[index] > 0);
        registers->refs[index]--;
    }
    *location = (sljit_value_location){ 0 };
}

static int alloc_location(sljit_register_builder *registers, sljit_value_kind kind, sljit_value_location *location)
{
    if (registers->simd && kind == SJLIT_VALUE_VEC2) {
        kind = SJLIT_VALUE_SIMD2;
    }
    int count = location_reg_count(kind);
    int found[2];
    int found_count = 0;
    for (int i = 0; i < SJLIT_FLOAT_REGISTER_COUNT && found_count < count; i++) {
        if (!registers->refs[i]) {
            found[found_count++] = i;
        }
    }
    if (found_count != count) {
        return -1;
    }
    *location = (sljit_value_location){ .kind = kind };
    for (int i = 0; i < count; i++) {
        location->regs[i] = kind == SJLIT_VALUE_SIMD2 ? SLJIT_VR(found[i])
                                                      : SLJIT_FR(found[i]);
        registers->refs[found[i]] = 1;
    }
    return 0;
}

static int alias_location(sljit_register_builder *registers, int output, int input)
{
    sljit_value_location location = registers->locations[input];
    if (!location_available(&location)) {
        return -1;
    }
    registers->locations[output] = location;
    retain_location(registers, location);
    return 0;
}

static int field_is(const type_field *field, size_t offset, size_t size)
{
    return field->offset == offset && field->size == size;
}

static int call_kind(const graph_ir_instruction *instruction)
{
    if (!instruction->node || !instruction->node->desc) {
        return 0;
    }
    const node_desc *desc = instruction->node->desc;
    if (desc->conversion_from && desc->conversion_to && desc->input_count == 1 &&
        desc->output_count == 1 && desc->inputs[0].type == PIN_STRUCT &&
        desc->outputs[0].type == PIN_STRUCT &&
        pin_compatible(&desc->inputs[0], &desc->outputs[0])) {
        return 1;
    }
    switch (desc->jit_kind) {
        case NODE_JIT_MUL: return 2;
        case NODE_JIT_ADD: return 3;
        case NODE_JIT_SUB: return 5;
        case NODE_JIT_DIV: return 6;
        case NODE_JIT_MIN: return 7;
        case NODE_JIT_MAX: return 8;
        case NODE_JIT_CLAMP: return 9;
        case NODE_JIT_NEG: return 10;
        case NODE_JIT_ABS: return 11;
        case NODE_JIT_V2_MOVE: return 4;
        default: return 0;
    }
}

/* Contract the reflected movement chain only when its nodes opt into
 * fast-math rounding. */
static int fma_pattern(
    const graph_ir *ir,
    const int *defined_at,
    const int *last_use,
    const graph_ir_instruction *add,
    int add_index,
    int *transform,
    int *scale
)
{
    if (ir->event_delta < 0 || add->op != GRAPH_IR_OP_CALL || call_kind(add) != 3 ||
        add->input_count != 2 || !add->node || !add->node->desc ||
        !(add->node->desc->jit_flags & NODE_JIT_FLAG_FAST_MATH)) {
        return 0;
    }
    for (int i = 0; i < 2; i++) {
        int product = add->inputs[i];
        int other = add->inputs[1 - i];
        int scale_index = defined_at[product];
        if (scale_index < 0 || scale_index >= add_index) {
            continue;
        }
        const graph_ir_instruction *scale_instruction = &ir->instructions[scale_index];
        if (scale_instruction->op != GRAPH_IR_OP_CALL ||
            call_kind(scale_instruction) != 2 || scale_instruction->output != product ||
            scale_instruction->input_count != 2 ||
            scale_instruction->inputs[1] != ir->event_delta ||
            !scale_instruction->node || !scale_instruction->node->desc ||
            !(scale_instruction->node->desc->jit_flags & NODE_JIT_FLAG_FAST_MATH)) {
            continue;
        }
        int first = scale_instruction->inputs[0];
        int first_index = defined_at[first];
        if (first_index < 0 || last_use[first] != scale_index ||
            last_use[product] != add_index || last_use[other] != add_index) {
            continue;
        }
        const graph_ir_instruction *first_instruction = &ir->instructions[first_index];
        if (first_instruction->op != GRAPH_IR_OP_CALL ||
            call_kind(first_instruction) != 2 || first_instruction->output != first ||
            first_instruction->input_count != 2 || !first_instruction->node ||
            !first_instruction->node->desc ||
            !(first_instruction->node->desc->jit_flags & NODE_JIT_FLAG_FAST_MATH)) {
            continue;
        }
        *transform = other;
        *scale = product;
        return 1;
    }
    return 0;
}

static int register_instruction_supported(
    const graph_ir *ir,
    const graph_ir_instruction *instruction,
    const graph_sljit_program *program,
    int index
)
{
    const graph_ir_value *values = ir->values;
    switch (instruction->op) {
        case GRAPH_IR_OP_CONST:
            return instruction->output >= 0 &&
                   value_location_kind(&values[instruction->output]) == SJLIT_VALUE_F32 &&
                   instruction->constant.type == PIN_F32;
        case GRAPH_IR_OP_COPY:
            return instruction->input_count == 1 && instruction->output >= 0 &&
                   value_location_kind(&values[instruction->output]) != SJLIT_VALUE_NONE &&
                   value_location_kind(&values[instruction->output]) ==
                       value_location_kind(&values[instruction->inputs[0]]);
        case GRAPH_IR_OP_GET_COMPONENT:
            return instruction->input_count == 0 && instruction->output >= 0 &&
                   program->column_indices[index] >= 0 &&
                   value_location_kind(&values[instruction->output]) != SJLIT_VALUE_NONE;
        case GRAPH_IR_OP_SET_COMPONENT:
            return instruction->input_count == 1 &&
                   program->column_indices[index] >= 0 &&
                   value_location_kind(&values[instruction->inputs[0]]) !=
                       SJLIT_VALUE_NONE;
        case GRAPH_IR_OP_BREAK_STRUCT: {
            if (instruction->input_count != 1 ||
                instruction->output_count != 1 || !instruction->type) {
                return 0;
            }
            const type_field *field = &instruction->type->fields[0];
            sljit_value_kind source = value_location_kind(&values[instruction->inputs[0]]);
            sljit_value_kind output = value_location_kind(
                &values[instruction->outputs[0]]
            );
            size_t size = source == SJLIT_VALUE_VEC2 ? 2 * sizeof(float)
                                                     : sizeof(float);
            return source != SJLIT_VALUE_NONE && source == output &&
                   field_is(field, 0, size);
        }
        case GRAPH_IR_OP_MAKE_STRUCT:
            if (instruction->input_count != 2 ||
                instruction->output_count != 0 || !instruction->type ||
                instruction->type->field_count != 2 || instruction->output < 0 ||
                value_location_kind(&values[instruction->output]) != SJLIT_VALUE_VEC2) {
                return 0;
            }
            return field_is(&instruction->type->fields[0], 0, sizeof(float)) &&
                   field_is(&instruction->type->fields[1], sizeof(float), sizeof(float)) &&
                   value_location_kind(&values[instruction->inputs[0]]) ==
                       SJLIT_VALUE_F32 &&
                   value_location_kind(&values[instruction->inputs[1]]) == SJLIT_VALUE_F32;
        case GRAPH_IR_OP_CALL: {
            int kind = call_kind(instruction);
            if (instruction->output < 0 || instruction->output_count != 0) {
                return 0;
            }
            sljit_value_kind output = value_location_kind(&values[instruction->output]);
            if (kind == 1) {
                return instruction->input_count == 1 && output == SJLIT_VALUE_VEC2 &&
                       value_location_kind(&values[instruction->inputs[0]]) ==
                           SJLIT_VALUE_VEC2;
            }
            if (kind == 2) {
                sljit_value_kind left = value_location_kind(
                    &values[instruction->inputs[0]]
                );
                sljit_value_kind right = value_location_kind(
                    &values[instruction->inputs[1]]
                );
                return instruction->input_count == 2 &&
                       ((output == SJLIT_VALUE_VEC2 &&
                         left == SJLIT_VALUE_VEC2 && right == SJLIT_VALUE_F32) ||
                        (output == SJLIT_VALUE_F32 && left == SJLIT_VALUE_F32 &&
                         right == SJLIT_VALUE_F32));
            }
            if (kind == 3 || kind == 5 || kind == 6 || kind == 7 || kind == 8) {
                sljit_value_kind left = value_location_kind(
                    &values[instruction->inputs[0]]
                );
                sljit_value_kind right = value_location_kind(
                    &values[instruction->inputs[1]]
                );
                return instruction->input_count == 2 &&
                       ((output == SJLIT_VALUE_VEC2 &&
                         left == SJLIT_VALUE_VEC2 && right == SJLIT_VALUE_VEC2) ||
                        (output == SJLIT_VALUE_F32 && left == SJLIT_VALUE_F32 &&
                         right == SJLIT_VALUE_F32));
            }
            if (kind == 9) {
                sljit_value_kind bound = value_location_kind(
                    &values[instruction->inputs[1]]
                );
                return instruction->input_count == 3 &&
                       (output == SJLIT_VALUE_F32 || output == SJLIT_VALUE_VEC2) &&
                       value_location_kind(&values[instruction->inputs[0]]) == output &&
                       bound == output &&
                       value_location_kind(&values[instruction->inputs[2]]) == output;
            }
            if (kind == 10 || kind == 11) {
                return instruction->input_count == 1 &&
                       (output == SJLIT_VALUE_F32 || output == SJLIT_VALUE_VEC2) &&
                       value_location_kind(&values[instruction->inputs[0]]) == output;
            }
            if (kind == 4) {
                return instruction->input_count == 3 && output == SJLIT_VALUE_VEC2 &&
                       value_location_kind(&values[instruction->inputs[0]]) ==
                           SJLIT_VALUE_VEC2 &&
                       value_location_kind(&values[instruction->inputs[1]]) ==
                           SJLIT_VALUE_F32 &&
                       value_location_kind(&values[instruction->inputs[2]]) ==
                           SJLIT_VALUE_F32;
            }
            return 0;
        }
    }
    return 0;
}

static int register_graph_supported(
    const graph_ir *ir,
    const graph_sljit_program *program,
    const char **reason
)
{
    if (SLJIT_NUMBER_OF_SCRATCH_FLOAT_REGISTERS < SJLIT_FLOAT_REGISTER_COUNT) {
        if (reason) {
            *reason = "SLJIT has too few floating-point registers";
        }
        return 0;
    }
    if (ir->event_delta >= 0 &&
        value_location_kind(&ir->values[ir->event_delta]) != SJLIT_VALUE_F32) {
        if (reason) {
            *reason = "SLJIT register path requires a float event delta";
        }
        return 0;
    }
    for (int i = 0; i < acount(ir->values); i++) {
        const graph_ir_value *value = &ir->values[i];
        if (value->kind != GRAPH_IR_VALUE_FLOW &&
            value_location_kind(value) == SJLIT_VALUE_NONE) {
            if (reason) {
                *reason = "SLJIT register path does not support this value "
                          "shape";
            }
            return 0;
        }
    }
    for (int i = 0; i < acount(ir->instructions); i++) {
        if (!register_instruction_supported(ir, &ir->instructions[i], program, i)) {
            if (reason) {
                *reason = "SLJIT register path does not support this "
                          "instruction shape";
            }
            return 0;
        }
    }
    return 1;
}

static int simd_graph_supported(const graph_ir *ir)
{
#if defined(__aarch64__)
    if (getenv("CB_DISABLE_SIMD")) {
        return 0;
    }
    if (!sljit_has_cpu_feature(SLJIT_HAS_SIMD)) {
        return 0;
    }
    for (int i = 0; i < acount(ir->instructions); i++) {
        const graph_ir_instruction *instruction = &ir->instructions[i];
        if (instruction->op == GRAPH_IR_OP_MAKE_STRUCT) {
            return 0;
        }
        if (instruction->op == GRAPH_IR_OP_CALL && call_kind(instruction) != 1 &&
            call_kind(instruction) != 2 && call_kind(instruction) != 3) {
            return 0;
        }
    }
    return 1;
#else
    return 0;
#endif
}

static int emit_op2(
    sljit_builder *builder,
    sljit_s32 op,
    sljit_s32 dst,
    sljit_sw dstw,
    sljit_s32 src1,
    sljit_sw src1w,
    sljit_s32 src2,
    sljit_sw src2w
)
{
    return sljit_emit_op2(builder->compiler, op, dst, dstw, src1, src1w, src2, src2w) !=
           SLJIT_SUCCESS;
}

static int emit_fop1(sljit_builder *builder, sljit_s32 op, sljit_s32 dst, sljit_sw dstw, sljit_s32 src, sljit_sw srcw)
{
    return sljit_emit_fop1(builder->compiler, op, dst, dstw, src, srcw) != SLJIT_SUCCESS;
}

static int emit_fop2(
    sljit_builder *builder,
    sljit_s32 op,
    sljit_s32 dst,
    sljit_sw dstw,
    sljit_s32 src1,
    sljit_sw src1w,
    sljit_s32 src2,
    sljit_sw src2w
)
{
    return sljit_emit_fop2(builder->compiler, op, dst, dstw, src1, src1w, src2, src2w) !=
           SLJIT_SUCCESS;
}

#if defined(__aarch64__)
#define SJLIT_SIMD_F32X2                                                       \
    (SLJIT_SIMD_FLOAT | SLJIT_SIMD_REG_64 | SLJIT_SIMD_ELEM_32)

/* SLJIT has SIMD loads but no portable floating-point vector arithmetic API.
 * These are AArch64 FMUL (vector-scalar) and FADD (two float lanes). */
enum
{
    SJLIT_AARCH64_FMUL_V2_SCALAR = 0x0f809000,
    SJLIT_AARCH64_FADD_V2 = 0x0e20d400,
    SJLIT_AARCH64_FMLA_V2_SCALAR = 0x0f801000,
};

static int emit_simd_load(sljit_builder *builder, sljit_s32 vreg)
{
    return sljit_emit_simd_mov(builder->compiler, SJLIT_SIMD_F32X2, vreg, SLJIT_MEM1(SLJIT_R0), 0) !=
           SLJIT_SUCCESS;
}

static int emit_simd_store(sljit_builder *builder, sljit_s32 vreg)
{
    return sljit_emit_simd_mov(
               builder->compiler,
               SJLIT_SIMD_F32X2 | SLJIT_SIMD_STORE,
               vreg,
               SLJIT_MEM1(SLJIT_R0),
               0
           ) != SLJIT_SUCCESS;
}

static int emit_simd_op(sljit_builder *builder, uint32_t base, sljit_s32 dst, sljit_s32 src1, sljit_s32 src2)
{
    int rd = sljit_get_register_index(SLJIT_SIMD_REG_64, dst);
    int rn = sljit_get_register_index(SLJIT_SIMD_REG_64, src1);
    int rm = sljit_get_register_index(SLJIT_SIMD_REG_64, src2);
    if (rd < 0 || rn < 0 || rm < 0) {
        return -1;
    }
    uint32_t instruction = base | (uint32_t)rd | ((uint32_t)rn << 5) |
                           ((uint32_t)rm << 16);
    return sljit_emit_op_custom(builder->compiler, &instruction, sizeof instruction) !=
           SLJIT_SUCCESS;
}

static int emit_simd_scalar_op(sljit_builder *builder, uint32_t base, sljit_s32 dst, sljit_s32 src1, sljit_s32 scalar)
{
    int rd = sljit_get_register_index(SLJIT_SIMD_REG_64, dst);
    int rn = sljit_get_register_index(SLJIT_SIMD_REG_64, src1);
    int rm = sljit_get_register_index(SLJIT_SIMD_REG_64, scalar);
    if (rd < 0 || rn < 0 || rm < 0) {
        return -1;
    }
    uint32_t instruction = base | (uint32_t)rd | ((uint32_t)rn << 5) |
                           ((uint32_t)rm << 16);
    return sljit_emit_op_custom(builder->compiler, &instruction, sizeof instruction) !=
           SLJIT_SUCCESS;
}
#else
static int emit_simd_load(sljit_builder *, sljit_s32)
{
    return -1;
}

static int emit_simd_store(sljit_builder *, sljit_s32)
{
    return -1;
}

static int emit_simd_op(sljit_builder *, uint32_t, sljit_s32, sljit_s32, sljit_s32)
{
    return -1;
}

static int emit_simd_scalar_op(sljit_builder *, uint32_t, sljit_s32, sljit_s32, sljit_s32)
{
    return -1;
}
#endif

static int emit_register_get(
    sljit_register_builder *registers,
    const graph_ir_instruction *instruction,
    int index
)
{
    sljit_value_kind kind = value_location_kind(
        &registers->builder->ir->values[instruction->output]
    );
    sljit_value_location location;
    if (alloc_location(registers, kind, &location) ||
        emit_register_column_address(registers->builder, index)) {
        return -1;
    }
    if (location.kind == SJLIT_VALUE_SIMD2) {
        if (emit_simd_load(registers->builder, location.regs[0])) {
            return -1;
        }
    } else if (emit_fop1(registers->builder, SLJIT_MOV_F32, location.regs[0], 0, SLJIT_MEM1(SLJIT_R0), 0)) {
        return -1;
    }
    if (location.kind == SJLIT_VALUE_VEC2 &&
        emit_fop1(registers->builder, SLJIT_MOV_F32, location.regs[1], 0, SLJIT_MEM1(SLJIT_R0), sizeof(float))) {
        return -1;
    }
    registers->locations[instruction->output] = location;
    if (registers->builder->batch) {
        registers->builder->cached_column = registers->builder->program->column_indices[index];
        registers->builder->cached_valid = 1;
    }
    return 0;
}

static int emit_register_set(
    sljit_register_builder *registers,
    const graph_ir_instruction *instruction,
    int index
)
{
    const sljit_value_location *location = &registers->locations[instruction->inputs[0]];
    int column = registers->builder->program->column_indices[index];
    if (!location_available(location)) {
        return -1;
    }
    if (!registers->builder->batch || !registers->builder->cached_valid ||
        registers->builder->cached_column != column) {
        if (emit_register_column_address(registers->builder, index)) {
            return -1;
        }
        if (registers->builder->batch) {
            registers->builder->cached_column = column;
            registers->builder->cached_valid = 1;
        }
    }
    if (location->kind == SJLIT_VALUE_SIMD2) {
        return emit_simd_store(registers->builder, location->regs[0]);
    }
    if (emit_fop1(
            registers->builder,
            SLJIT_MOV_F32,
            SLJIT_MEM1(SLJIT_R0),
            0,
            location->regs[0],
            0
        )) {
        return -1;
    }
    if (location->kind == SJLIT_VALUE_VEC2 && emit_fop1(
                                                  registers->builder,
                                                  SLJIT_MOV_F32,
                                                  SLJIT_MEM1(SLJIT_R0),
                                                  sizeof(float),
                                                  location->regs[1],
                                                  0
                                              )) {
        return -1;
    }
    return 0;
}

static int emit_minmax(sljit_builder *builder, int maximum, sljit_s32 output, sljit_s32 left, sljit_s32 right)
{
    struct sljit_jump *select_right = sljit_emit_fcmp(
        builder->compiler,
        (maximum ? SLJIT_F_LESS : SLJIT_F_GREATER),
        left,
        0,
        right,
        0
    );
    if (!select_right || emit_fop1(builder, SLJIT_MOV_F32, output, 0, left, 0)) {
        return -1;
    }
    struct sljit_jump *done = sljit_emit_jump(builder->compiler, SLJIT_JUMP);
    struct sljit_label *right_label = sljit_emit_label(builder->compiler);
    if (!done || !right_label ||
        emit_fop1(builder, SLJIT_MOV_F32, output, 0, right, 0)) {
        return -1;
    }
    struct sljit_label *done_label = sljit_emit_label(builder->compiler);
    if (!done_label) {
        return -1;
    }
    sljit_set_label(select_right, right_label);
    sljit_set_label(done, done_label);
    return 0;
}

static int emit_clamp(sljit_builder *builder, sljit_s32 output, sljit_s32 value, sljit_s32 low, sljit_s32 high)
{
    return emit_minmax(builder, 0, output, value, high) ||
           emit_minmax(builder, 1, output, output, low);
}

static int emit_register_instruction(
    sljit_register_builder *registers,
    const graph_ir_instruction *instruction,
    int index
)
{
    const graph_ir_value *values = registers->builder->ir->values;
    switch (instruction->op) {
        case GRAPH_IR_OP_CONST: {
            sljit_value_location location;
            if (alloc_location(registers, SJLIT_VALUE_F32, &location) ||
                sljit_emit_fset32(
                    registers->builder->compiler,
                    location.regs[0],
                    instruction->constant.f
                ) != SLJIT_SUCCESS) {
                return -1;
            }
            registers->locations[instruction->output] = location;
            return 0;
        }
        case GRAPH_IR_OP_COPY:
            return alias_location(registers, instruction->output, instruction->inputs[0]);
        case GRAPH_IR_OP_GET_COMPONENT:
            return emit_register_get(registers, instruction, index);
        case GRAPH_IR_OP_SET_COMPONENT:
            return emit_register_set(registers, instruction, index);
        case GRAPH_IR_OP_BREAK_STRUCT:
            return alias_location(
                registers,
                instruction->outputs[0],
                instruction->inputs[0]
            );
        case GRAPH_IR_OP_MAKE_STRUCT: {
            sljit_value_location location;
            if (alloc_location(registers, SJLIT_VALUE_VEC2, &location) ||
                emit_fop1(
                    registers->builder,
                    SLJIT_MOV_F32,
                    location.regs[0],
                    0,
                    registers->locations[instruction->inputs[0]].regs[0],
                    0
                ) ||
                emit_fop1(
                    registers->builder,
                    SLJIT_MOV_F32,
                    location.regs[1],
                    0,
                    registers->locations[instruction->inputs[1]].regs[0],
                    0
                )) {
                return -1;
            }
            registers->locations[instruction->output] = location;
            return 0;
        }
        case GRAPH_IR_OP_CALL: {
            int kind = call_kind(instruction);
            if (kind == 1) {
                return alias_location(
                    registers,
                    instruction->output,
                    instruction->inputs[0]
                );
            }
            if (kind == 10 || kind == 11) {
                sljit_value_location output;
                sljit_value_kind output_kind = value_location_kind(
                    &values[instruction->output]
                );
                if (alloc_location(registers, output_kind, &output)) {
                    return -1;
                }
                sljit_s32 op = kind == 10 ? SLJIT_NEG_F32 : SLJIT_ABS_F32;
                const sljit_value_location
                    *input = &registers->locations[instruction->inputs[0]];
                if (emit_fop1(registers->builder, op, output.regs[0], 0, input->regs[0], 0) ||
                    (output.kind == SJLIT_VALUE_VEC2 &&
                     emit_fop1(registers->builder, op, output.regs[1], 0, input->regs[1], 0))) {
                    return -1;
                }
                registers->locations[instruction->output] = output;
                return 0;
            }
            if (kind == 2 && registers->simd) {
                int add_index = registers->last_use[instruction->output];
                int transform;
                int scale;
                if (add_index > index &&
                    fma_pattern(
                        registers->builder->ir,
                        registers->defined_at,
                        registers->last_use,
                        &registers->builder->ir->instructions[add_index],
                        add_index,
                        &transform,
                        &scale
                    )) {
                    registers->last_use[registers->builder->ir->event_delta] = add_index;
                    return alias_location(
                        registers,
                        instruction->output,
                        instruction->inputs[0]
                    );
                }
            }
            if (kind == 3 && registers->simd) {
                int transform;
                int scale;
                if (fma_pattern(
                        registers->builder->ir,
                        registers->defined_at,
                        registers->last_use,
                        instruction,
                        index,
                        &transform,
                        &scale
                    )) {
                    const sljit_value_location *transform_location = &registers->locations[transform];
                    const sljit_value_location *scale_location = &registers->locations[scale];
                    const sljit_value_location *
                        delta_location = &registers
                                              ->locations[registers->builder->ir->event_delta];
                    if (!location_available(transform_location) ||
                        !location_available(scale_location) ||
                        !location_available(delta_location) ||
                        emit_simd_scalar_op(
                            registers->builder,
                            SJLIT_AARCH64_FMLA_V2_SCALAR,
                            transform_location->regs[0],
                            scale_location->regs[0],
                            delta_location->regs[0]
                        )) {
                        return -1;
                    }
                    return alias_location(registers, instruction->output, transform);
                }
            }
            sljit_value_location output;
            sljit_value_kind output_kind = value_location_kind(
                &values[instruction->output]
            );
            if (alloc_location(registers, output_kind, &output)) {
                return -1;
            }
            const sljit_value_location
                *input0 = &registers->locations[instruction->inputs[0]];
            const sljit_value_location
                *input1 = &registers->locations[instruction->inputs[1]];
            if (output.kind == SJLIT_VALUE_F32 && (kind == 7 || kind == 8)) {
                return emit_minmax(
                    registers->builder,
                    kind == 8,
                    output.regs[0],
                    input0->regs[0],
                    input1->regs[0]
                );
            }
            if (output.kind == SJLIT_VALUE_F32 && kind == 9) {
                const sljit_value_location
                    *input2 = &registers->locations[instruction->inputs[2]];
                return emit_clamp(
                    registers->builder,
                    output.regs[0],
                    input0->regs[0],
                    input1->regs[0],
                    input2->regs[0]
                );
            }
            if (output.kind == SJLIT_VALUE_F32) {
                sljit_s32 op = kind == 2 ? SLJIT_MUL_F32
                             : kind == 3 ? SLJIT_ADD_F32
                             : kind == 5 ? SLJIT_SUB_F32
                                         : SLJIT_DIV_F32;
                return emit_fop2(
                    registers->builder,
                    op,
                    output.regs[0],
                    0,
                    input0->regs[0],
                    0,
                    input1->regs[0],
                    0
                );
            }
            if (output.kind == SJLIT_VALUE_SIMD2) {
                if (kind == 2) {
                    if (emit_simd_scalar_op(
                            registers->builder,
                            SJLIT_AARCH64_FMUL_V2_SCALAR,
                            output.regs[0],
                            input0->regs[0],
                            input1->regs[0]
                        )) {
                        return -1;
                    }
                } else if (kind == 3) {
                    if (emit_simd_op(
                            registers->builder,
                            SJLIT_AARCH64_FADD_V2,
                            output.regs[0],
                            input0->regs[0],
                            input1->regs[0]
                        )) {
                        return -1;
                    }
                } else {
                    return -1;
                }
            } else if (kind == 2) {
                if (emit_fop2(
                        registers->builder,
                        SLJIT_MUL_F32,
                        output.regs[0],
                        0,
                        input0->regs[0],
                        0,
                        input1->regs[0],
                        0
                    ) ||
                    emit_fop2(
                        registers->builder,
                        SLJIT_MUL_F32,
                        output.regs[1],
                        0,
                        input0->regs[1],
                        0,
                        input1->regs[0],
                        0
                    )) {
                    return -1;
                }
            } else if (kind == 3 || kind == 5 || kind == 6) {
                sljit_s32 op = kind == 3 ? SLJIT_ADD_F32
                             : kind == 5 ? SLJIT_SUB_F32
                                         : SLJIT_DIV_F32;
                if (emit_fop2(
                        registers->builder,
                        op,
                        output.regs[0],
                        0,
                        input0->regs[0],
                        0,
                        input1->regs[0],
                        0
                    ) ||
                    emit_fop2(
                        registers->builder,
                        op,
                        output.regs[1],
                        0,
                        input0->regs[1],
                        0,
                        input1->regs[1],
                        0
                    )) {
                    return -1;
                }
            } else if (kind == 7 || kind == 8) {
                int maximum = kind == 8;
                if (emit_minmax(
                        registers->builder,
                        maximum,
                        output.regs[0],
                        input0->regs[0],
                        input1->regs[0]
                    ) ||
                    emit_minmax(
                        registers->builder,
                        maximum,
                        output.regs[1],
                        input0->regs[1],
                        input1->regs[1]
                    )) {
                    return -1;
                }
            } else if (kind == 9) {
                const sljit_value_location
                    *input2 = &registers->locations[instruction->inputs[2]];
                if (emit_clamp(
                        registers->builder,
                        output.regs[0],
                        input0->regs[0],
                        input1->regs[0],
                        input2->regs[0]
                    ) ||
                    emit_clamp(
                        registers->builder,
                        output.regs[1],
                        input0->regs[1],
                        input1->regs[1],
                        input2->regs[1]
                    )) {
                    return -1;
                }
            } else if (kind == 4) {
                const sljit_value_location
                    *input2 = &registers->locations[instruction->inputs[2]];
                if (emit_fop2(
                        registers->builder,
                        SLJIT_ADD_F32,
                        output.regs[0],
                        0,
                        input0->regs[0],
                        0,
                        input1->regs[0],
                        0
                    ) ||
                    emit_fop2(
                        registers->builder,
                        SLJIT_ADD_F32,
                        output.regs[1],
                        0,
                        input0->regs[1],
                        0,
                        input2->regs[0],
                        0
                    )) {
                    return -1;
                }
            } else {
                return -1;
            }
            registers->locations[instruction->output] = output;
            return 0;
        }
    }
    return -1;
}

static void release_dead_registers(sljit_register_builder *registers, int instruction)
{
    for (int i = 0; i < acount(registers->builder->ir->values); i++) {
        if (!location_available(&registers->locations[i])) {
            continue;
        }
        if (registers->last_use[i] == instruction ||
            (registers->defined_at[i] == instruction && registers->last_use[i] < 0)) {
            release_location(registers, i);
        }
    }
}

static void register_liveness(sljit_register_builder *registers)
{
    for (int i = 0; i < acount(registers->builder->ir->values); i++) {
        registers->last_use[i] = -1;
        registers->defined_at[i] = -1;
    }
    for (int i = 0; i < acount(registers->builder->ir->instructions); i++) {
        const graph_ir_instruction *instruction = &registers->builder->ir->instructions[i];
        if (instruction->output >= 0) {
            registers->defined_at[instruction->output] = i;
        }
        for (int j = 0; j < instruction->output_count; j++) {
            registers->defined_at[instruction->outputs[j]] = i;
        }
        for (int j = 0; j < instruction->input_count; j++) {
            registers->last_use[instruction->inputs[j]] = i;
        }
    }
}

static int emit_register_program(sljit_builder *builder, int emit_return)
{
    int value_count = acount(builder->ir->values);
    sljit_register_builder registers = {
        .builder = builder,
        .locations = (sljit_value_location *)
            xcalloc((size_t)value_count, sizeof *registers.locations),
        .refs = (int *)xcalloc(SJLIT_FLOAT_REGISTER_COUNT, sizeof *registers.refs),
        .last_use = (int *)xcalloc((size_t)value_count, sizeof *registers.last_use),
        .defined_at = (int *)xcalloc((size_t)value_count, sizeof *registers.defined_at),
        .simd = builder->program->simd_mode,
    };
    register_liveness(&registers);
    if (builder->ir->event_delta >= 0) {
        sljit_value_location location;
        if (builder->persistent_delta) {
            location = (sljit_value_location){
                .kind = SJLIT_VALUE_F32,
                .regs = { SLJIT_FR0 },
            };
            registers.refs[0] = 1;
            registers.last_use[builder->ir->event_delta] = acount(builder->ir->instructions);
        } else if (
            alloc_location(&registers, SJLIT_VALUE_F32, &location) ||
            emit_fop1(
                builder,
                SLJIT_MOV_F32,
                location.regs[0],
                0,
                SLJIT_MEM1(builder->context_reg),
                builder->delta_offset
            )
        ) {
            free(registers.locations);
            free(registers.refs);
            free(registers.last_use);
            free(registers.defined_at);
            return -1;
        }
        registers.locations[builder->ir->event_delta] = location;
        if (registers.last_use[builder->ir->event_delta] < 0) {
            release_location(&registers, builder->ir->event_delta);
        }
    }
    for (int i = 0; i < acount(builder->ir->instructions); i++) {
        if (emit_register_instruction(&registers, &builder->ir->instructions[i], i)) {
            free(registers.locations);
            free(registers.refs);
            free(registers.last_use);
            free(registers.defined_at);
            return -1;
        }
        release_dead_registers(&registers, i);
    }
    int result = emit_return &&
                 sljit_emit_return(builder->compiler, SLJIT_MOV, SLJIT_IMM, GRAPH_JIT_OK) !=
                     SLJIT_SUCCESS;
    free(registers.locations);
    free(registers.refs);
    free(registers.last_use);
    free(registers.defined_at);
    return result ? -1 : 0;
}

static int emit_instruction(sljit_builder *builder, const graph_ir_instruction *instruction, int index);

static int emit_memory_program(sljit_builder *builder)
{
    const graph_ir *ir = builder->ir;
    if (ir->event_delta >= 0 && emit_copy(
                                    builder,
                                    SLJIT_SP,
                                    (sljit_sw)builder->offsets[ir->event_delta],
                                    SLJIT_S0,
                                    (sljit_sw)offsetof(graph_jit_context, delta_seconds),
                                    sizeof(float),
                                    SLJIT_R0
                                )) {
        return -1;
    }
    for (int i = 0; i < acount(ir->instructions); i++) {
        if (emit_instruction(builder, &ir->instructions[i], i)) {
            return -1;
        }
    }
    return sljit_emit_return(builder->compiler, SLJIT_MOV, SLJIT_IMM, GRAPH_JIT_OK) != SLJIT_SUCCESS
             ? -1
             : 0;
}

static int emit_instruction(sljit_builder *builder, const graph_ir_instruction *instruction, int index)
{
    const graph_ir_value *values = builder->ir->values;
    switch (instruction->op) {
        case GRAPH_IR_OP_CONST:
            if (instruction->output < 0) {
                return -1;
            }
            return emit_constant(
                builder,
                &values[instruction->output],
                instruction->constant,
                builder->offsets[instruction->output]
            );
        case GRAPH_IR_OP_COPY:
            if (instruction->input_count != 1 || instruction->output < 0) {
                return -1;
            }
            return emit_copy(
                builder,
                SLJIT_SP,
                (sljit_sw)builder->offsets[instruction->output],
                SLJIT_SP,
                (sljit_sw)builder->offsets[instruction->inputs[0]],
                values[instruction->output].size,
                SLJIT_R0
            );
        case GRAPH_IR_OP_GET_COMPONENT:
            if (instruction->input_count != 0 || instruction->output < 0 ||
                emit_column_address(builder, index)) {
                return -1;
            }
            return emit_copy(
                builder,
                SLJIT_SP,
                (sljit_sw)builder->offsets[instruction->output],
                SLJIT_R0,
                0,
                values[instruction->output].size,
                SLJIT_R1
            );
        case GRAPH_IR_OP_SET_COMPONENT:
            if (instruction->input_count != 1 || emit_column_address(builder, index)) {
                return -1;
            }
            return emit_copy(
                builder,
                SLJIT_R0,
                0,
                SLJIT_SP,
                (sljit_sw)builder->offsets[instruction->inputs[0]],
                values[instruction->inputs[0]].size,
                SLJIT_R1
            );
        case GRAPH_IR_OP_BREAK_STRUCT:
            if (instruction->input_count != 1 || !instruction->type ||
                instruction->output_count != instruction->type->field_count) {
                return -1;
            }
            for (int i = 0; i < instruction->output_count; i++) {
                const type_field *field = &instruction->type->fields[i];
                if (emit_copy(
                        builder,
                        SLJIT_SP,
                        (sljit_sw)builder->offsets[instruction->outputs[i]],
                        SLJIT_SP,
                        (sljit_sw)builder->offsets[instruction->inputs[0]] +
                            (sljit_sw)field->offset,
                        field->size,
                        SLJIT_R0
                    )) {
                    return -1;
                }
            }
            return 0;
        case GRAPH_IR_OP_MAKE_STRUCT:
            if (instruction->output_count != 0 || !instruction->type ||
                instruction->output < 0 ||
                instruction->input_count != instruction->type->field_count ||
                emit_zero(
                    builder,
                    builder->offsets[instruction->output],
                    values[instruction->output].size
                )) {
                return -1;
            }
            for (int i = 0; i < instruction->input_count; i++) {
                const type_field *field = &instruction->type->fields[i];
                if (emit_copy(
                        builder,
                        SLJIT_SP,
                        (sljit_sw)builder->offsets[instruction->output] +
                            (sljit_sw)field->offset,
                        SLJIT_SP,
                        (sljit_sw)builder->offsets[instruction->inputs[i]],
                        field->size,
                        SLJIT_R0
                    )) {
                    return -1;
                }
            }
            return 0;
        case GRAPH_IR_OP_CALL:
            return emit_direct_call(builder, instruction, index);
    }
    return -1;
}

static int prepare_offsets(sljit_builder *builder, const char **reason)
{
    size_t offset = 0;
    for (int i = 0; i < acount(builder->ir->values); i++) {
        const graph_ir_value *value = &builder->ir->values[i];
        if (value->kind == GRAPH_IR_VALUE_FLOW) {
            builder->offsets[i] = 0;
            continue;
        }
        if (value->kind != GRAPH_IR_VALUE_FIXED || !value->size) {
            if (reason) {
                *reason = "SLJIT backend requires fixed-size POD values";
            }
            return -1;
        }
        size_t alignment = value->alignment ? value->alignment : 1;
        offset = align_offset(offset, alignment);
        builder->offsets[i] = offset;
        offset += value->size;
        if (offset > SLJIT_MAX_LOCAL_SIZE) {
            if (reason) {
                *reason = "SLJIT local storage limit exceeded";
            }
            return -1;
        }
    }
    builder->storage_size = offset;
    return 0;
}

static int validate_ir(const graph_ir *ir, const char **reason)
{
    if (graph_ir_validate(ir, reason)) {
        return -1;
    }
    for (int i = 0; i < acount(ir->values); i++) {
        const graph_ir_value *value = &ir->values[i];
        if (value->container != PIN_CONTAINER_SINGLE ||
            (value->kind != GRAPH_IR_VALUE_FLOW &&
             !(value->flags & GRAPH_IR_VALUE_POD))) {
            if (reason) {
                *reason = "SLJIT backend requires single POD values";
            }
            return -1;
        }
    }
    for (int i = 0; i < acount(ir->instructions); i++) {
        const graph_ir_instruction *instruction = &ir->instructions[i];
        if (instruction->op == GRAPH_IR_OP_CALL &&
            (!direct_available(instruction) ||
             instruction->input_count != instruction->direct_count - 1)) {
            if (reason) {
                *reason = "SLJIT backend requires a direct adapter with at "
                          "most three inputs";
            }
            return -1;
        }
    }
    return 0;
}

static void *compile_batch_code(const graph_ir *ir, graph_sljit_program *program)
{
    if (!program->register_mode || program->column_count > 3) {
        return NULL;
    }
    struct sljit_compiler *compiler = sljit_create_compiler(NULL);
    if (!compiler) {
        return NULL;
    }
    sljit_builder builder = {
        .program = program,
        .ir = ir,
        .compiler = compiler,
        .context_reg = SLJIT_R4,
        .delta_offset = offsetof(graph_sljit_batch_context, delta_seconds),
        .column_stride = sizeof(graph_jit_column),
        .column_data_offset = offsetof(graph_jit_column, data),
        .batch = 1,
        .persistent_delta = 1,
    };
    sljit_s32 scratch_count = 10 | SLJIT_ENTER_FLOAT(SJLIT_FLOAT_REGISTER_COUNT) |
                              (program->simd_mode
                                   ? SLJIT_ENTER_VECTOR(SJLIT_FLOAT_REGISTER_COUNT)
                                   : 0);
    if (sljit_emit_enter(compiler, SLJIT_ENTER_REG_ARG, SLJIT_ARGS1(W, P_R), scratch_count, 0, 0) !=
            SLJIT_SUCCESS ||
        emit_op1(&builder, SLJIT_MOV_P, SLJIT_R4, 0, SLJIT_R0, 0) ||
        (ir->event_delta >= 0 &&
         emit_fop1(&builder, SLJIT_MOV_F32, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_R4), offsetof(graph_sljit_batch_context, delta_seconds))) ||
        emit_column_base(&builder, offsetof(graph_sljit_batch_context, columns)) ||
        emit_batch_column_bases(&builder) ||
        emit_op1(&builder, SLJIT_MOV_P, SLJIT_R3, 0, SLJIT_MEM1(SLJIT_R0), offsetof(graph_sljit_batch_context, slots)) ||
        emit_op1(&builder, SLJIT_MOV32, SLJIT_R5, 0, SLJIT_MEM1(SLJIT_R0), offsetof(graph_sljit_batch_context, entity_count))) {
        sljit_free_compiler(compiler);
        return NULL;
    }
    struct sljit_jump *skip = sljit_emit_cmp(compiler, SLJIT_LESS_EQUAL, SLJIT_R5, 0, SLJIT_IMM, 0);
    struct sljit_label *loop = sljit_emit_label(compiler);
    if (!skip || !loop ||
        emit_op1(&builder, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_R3), 0) ||
        emit_register_program(&builder, 0) ||
        emit_op2(&builder, SLJIT_ADD, SLJIT_R3, 0, SLJIT_R3, 0, SLJIT_IMM, sizeof(uint32_t)) ||
        emit_op2(&builder, SLJIT_SUB32 | SLJIT_SET_Z, SLJIT_R5, 0, SLJIT_R5, 0, SLJIT_IMM, 1)) {
        sljit_free_compiler(compiler);
        return NULL;
    }
    struct sljit_jump *again = sljit_emit_jump(compiler, SLJIT_NOT_ZERO);
    struct sljit_label *done = sljit_emit_label(compiler);
    if (!again || !done ||
        sljit_emit_return(compiler, SLJIT_MOV, SLJIT_IMM, GRAPH_JIT_OK) != SLJIT_SUCCESS) {
        sljit_free_compiler(compiler);
        return NULL;
    }
    sljit_set_label(skip, done);
    sljit_set_label(again, loop);
    void *code = sljit_generate_code(compiler, 0, NULL);
    sljit_free_compiler(compiler);
    return code;
}

static graph_sljit_program *compile_ir(const graph_ir *ir, const char **reason)
{
    graph_sljit_program *program = (graph_sljit_program *)xcalloc(1, sizeof *program);
    program->offsets = (size_t *)
        xcalloc((size_t)acount(ir->values), sizeof *program->offsets);
    program->column_indices = (int *)
        xcalloc((size_t)acount(ir->instructions), sizeof *program->column_indices);
    for (int i = 0; i < acount(ir->instructions); i++) {
        program->column_indices[i] = -1;
    }
    program->operation_ids = (uint64_t *)
        xcalloc((size_t)acount(ir->instructions), sizeof *program->operation_ids);
    for (int i = 0; i < acount(ir->instructions); i++) {
        const graph_ir_instruction *instruction = &ir->instructions[i];
        program->operation_ids[i] = instruction->operation_id;
        if (instruction->op != GRAPH_IR_OP_GET_COMPONENT &&
            instruction->op != GRAPH_IR_OP_SET_COMPONENT) {
            continue;
        }
        size_t column_size = instruction->op == GRAPH_IR_OP_GET_COMPONENT
                               ? ir->values[instruction->output].size
                               : ir->values[instruction->inputs[0]].size;
        int column = -1;
        for (int j = 0; j < program->column_count; j++) {
            if (program->column_ids[j] == instruction->resource_id) {
                column = j;
                break;
            }
        }
        if (column < 0) {
            column = program->column_count++;
            program->column_ids = (uint64_t *)xrealloc(
                program->column_ids,
                (size_t)program->column_count * sizeof *program->column_ids
            );
            program->column_sizes = (size_t *)xrealloc(
                program->column_sizes,
                (size_t)program->column_count * sizeof *program->column_sizes
            );
            program->column_ids[column] = instruction->resource_id;
            program->column_sizes[column] = column_size;
        }
        assert(program->column_sizes[column] == column_size);
        program->column_indices[i] = column;
    }

    sljit_builder builder = {
        .program = program,
        .ir = ir,
        .compiler = sljit_create_compiler(NULL),
        .offsets = program->offsets,
    };
    int register_mode = register_graph_supported(ir, program, reason);
    int simd_mode = register_mode && simd_graph_supported(ir);
    program->register_mode = register_mode;
    program->simd_mode = simd_mode;
    builder.context_reg = register_mode ? SLJIT_R0 : SLJIT_S0;
    builder.delta_offset = offsetof(graph_jit_context, delta_seconds);
    builder.column_stride = sizeof(graph_jit_column);
    builder.column_data_offset = offsetof(graph_jit_column, data);
    if (!register_mode && prepare_offsets(&builder, reason)) {
        discard_builder(&builder);
        return NULL;
    }
    if (!builder.compiler) {
        if (reason) {
            *reason = "SLJIT compiler allocation failed";
        }
        graph_sljit_free(program);
        return NULL;
    }
    if (sljit_emit_enter(
            builder.compiler,
            register_mode ? SLJIT_ENTER_REG_ARG : 0,
            register_mode ? SLJIT_ARGS1(W, P_R) : SLJIT_ARGS1(W, P),
            (register_mode ? 3 : 4) |
                (register_mode ? SLJIT_ENTER_FLOAT(SJLIT_FLOAT_REGISTER_COUNT) : 0) |
                (simd_mode ? SLJIT_ENTER_VECTOR(SJLIT_FLOAT_REGISTER_COUNT) : 0),
            register_mode ? 0 : 1,
            (sljit_s32)(register_mode ? 0 : builder.storage_size)
        ) != SLJIT_SUCCESS) {
        if (reason) {
            *reason = "SLJIT entry generation failed";
        }
        discard_builder(&builder);
        return NULL;
    }
    if (register_mode &&
        emit_column_base(&builder, offsetof(graph_jit_context, columns))) {
        if (reason) {
            *reason = "SLJIT column base generation failed";
        }
        discard_builder(&builder);
        return NULL;
    }
    if ((register_mode ? emit_register_program(&builder, 1)
                       : emit_memory_program(&builder))) {
        if (reason) {
            *reason = register_mode
                        ? "SLJIT register instruction generation failed"
                        : "SLJIT instruction generation failed";
        }
        discard_builder(&builder);
        return NULL;
    }
    void *code = sljit_generate_code(builder.compiler, 0, NULL);
    sljit_free_compiler(builder.compiler);
    free(builder.offsets);
    program->offsets = NULL;
    if (!code) {
        if (reason) {
            *reason = "SLJIT executable memory allocation failed";
        }
        graph_sljit_free(program);
        return NULL;
    }
    program->code = code;
    program->batch_code = compile_batch_code(ir, program);
    return program;
}

#endif

int graph_sljit_available()
{
    return CBLUEPRINT_HAVE_SJLIT;
}

graph_sljit_program *graph_sljit_compile(const graph_ir *ir, const char **reason)
{
#if CBLUEPRINT_HAVE_SJLIT
    if (!ir) {
        return compile_fail(reason, "graph IR is required");
    }
    if (validate_ir(ir, reason)) {
        return NULL;
    }
    return compile_ir(ir, reason);
#else
    return compile_fail(reason, "SLJIT backend is disabled");
#endif
}

void graph_sljit_free(graph_sljit_program *program)
{
#if CBLUEPRINT_HAVE_SJLIT
    if (!program) {
        return;
    }
    if (program->code) {
        sljit_free_code(program->code, NULL);
    }
    if (program->batch_code) {
        sljit_free_code(program->batch_code, NULL);
    }
    free(program->offsets);
    free(program->column_indices);
    free(program->column_ids);
    free(program->column_sizes);
    free(program->operation_ids);
#endif
    free(program);
}

graph_jit_entry_fn graph_sljit_entry(const graph_sljit_program *program)
{
#if CBLUEPRINT_HAVE_SJLIT
    return (graph_jit_entry_fn)program->code;
#else
    return NULL;
#endif
}

graph_sljit_batch_entry_fn graph_sljit_batch_entry(const graph_sljit_program *program)
{
#if CBLUEPRINT_HAVE_SJLIT
    return (graph_sljit_batch_entry_fn)program->batch_code;
#else
    return NULL;
#endif
}

int graph_sljit_uses_registers(const graph_sljit_program *program)
{
#if CBLUEPRINT_HAVE_SJLIT
    return program->register_mode;
#else
    return 0;
#endif
}

int graph_sljit_uses_batch(const graph_sljit_program *program)
{
#if CBLUEPRINT_HAVE_SJLIT
    return program->batch_code != NULL;
#else
    return 0;
#endif
}

int graph_sljit_uses_simd(const graph_sljit_program *program)
{
#if CBLUEPRINT_HAVE_SJLIT
    return program->simd_mode;
#else
    return 0;
#endif
}

int graph_sljit_column_count(const graph_sljit_program *program)
{
#if CBLUEPRINT_HAVE_SJLIT
    return program->column_count;
#else
    return 0;
#endif
}

uint64_t graph_sljit_column_id_at(const graph_sljit_program *program, int index)
{
#if CBLUEPRINT_HAVE_SJLIT
    assert(index >= 0 && index < program->column_count);
    return program->column_ids[index];
#else
    return 0;
#endif
}
