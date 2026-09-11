#include "graph/compiler/behavior_jit.h"

#include "graph/runtime/behavior_data.h"
#include "graph/runtime/flow.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#if CHIBI_HAVE_SJLIT
#include <sljitLir.h>
#endif

#if CHIBI_HAVE_SJLIT
static bool emit_scalar(struct sljit_compiler *compiler, const node *n)
{
    if (n->desc->pure_kind != NODE_PURE_CALL || n->output_count != 1 ||
        n->input_count != 2 || n->input_pins[0].container != PIN_CONTAINER_SINGLE ||
        n->input_pins[1].container != PIN_CONTAINER_SINGLE ||
        n->input_pins[0].type != n->input_pins[1].type) {
        return false;
    }
    pin_type type = n->input_pins[0].type;
    if (type != PIN_F32 && type != PIN_F64) {
        return false;
    }
    int op;
    switch (n->desc->jit_kind) {
        case NODE_JIT_ADD: op = SLJIT_ADD_F64; break;
        case NODE_JIT_SUB: op = SLJIT_SUB_F64; break;
        case NODE_JIT_MUL: op = SLJIT_MUL_F64; break;
        case NODE_JIT_DIV: op = SLJIT_DIV_F64; break;
        default: return false;
    }
    int width = type == PIN_F32 ? SLJIT_32 : 0;
    sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
    sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)n);
    sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2V(P, P), SLJIT_IMM, SLJIT_FUNC_ADDR(behavior_data_scalar));
    sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), offsetof(behavior_data_context, inputs));
    sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), offsetof(behavior_data_context, outputs));
    sljit_emit_fop1(compiler, SLJIT_MOV_F64 | width, SLJIT_FR0, 0, SLJIT_MEM1(SLJIT_R0), offsetof(pin_value, f));
    sljit_emit_fop1(
        compiler,
        SLJIT_MOV_F64 | width,
        SLJIT_FR1,
        0,
        SLJIT_MEM1(SLJIT_R0),
        sizeof(pin_value) + offsetof(pin_value, f)
    );
    sljit_emit_fop2(compiler, op | width, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1, 0);
    sljit_emit_fop1(
        compiler,
        SLJIT_MOV_F64 | width,
        SLJIT_MEM1(SLJIT_R1),
        offsetof(pin_value, f),
        SLJIT_FR0,
        0
    );
    return true;
}

void *behavior_jit_data(const graph_ir *ir, const graph_ir_block *block)
{
    struct sljit_compiler *compiler = sljit_create_compiler(NULL);
    if (!compiler) {
        return NULL;
    }
    sljit_emit_enter(compiler, 0, SLJIT_ARGS1(32, P), 2 | SLJIT_ENTER_FLOAT(2), 1, 0);
    struct sljit_label **labels = xcalloc((size_t)block->count + 1, sizeof *labels);
    struct sljit_jump **jumps = xcalloc((size_t)block->count, sizeof *jumps);
    struct sljit_jump **errors = NULL;
    for (int i = 0; i < block->count; i++) {
        const graph_ir_behavior_instruction *instruction = &ir->behavior[block->first + i];
        labels[i] = sljit_emit_label(compiler);
        if (instruction->op == GRAPH_IR_EVALUATE &&
            emit_scalar(compiler, instruction->node)) {
            continue;
        }
        sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R1, 0, SLJIT_IMM, (sljit_sw)instruction);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(32, P, P), SLJIT_IMM, SLJIT_FUNC_ADDR(behavior_data_instruction));
        if (instruction->op == GRAPH_IR_MEMO_BRANCH) {
            jumps[i] = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R0, 0, SLJIT_IMM, 0);
        } else {
            apush(errors, sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R0, 0, SLJIT_IMM, 0));
        }
    }
    labels[block->count] = sljit_emit_label(compiler);
    sljit_emit_return(compiler, SLJIT_MOV32, SLJIT_IMM, 0);
    struct sljit_label *failure = sljit_emit_label(compiler);
    sljit_emit_return(compiler, SLJIT_MOV32, SLJIT_R0, 0);
    if (sljit_get_compiler_error(compiler) != SLJIT_SUCCESS) {
        sljit_free_compiler(compiler);
        free(labels);
        free(jumps);
        afree(errors);
        return NULL;
    }
    for (int i = 0; i < block->count; i++) {
        if (jumps[i]) {
            int target = ir->behavior[block->first + i].target - block->first;
            assert(target > i && target <= block->count);
            sljit_set_label(jumps[i], labels[target]);
        }
    }
    for (int i = 0; i < acount(errors); i++) {
        sljit_set_label(errors[i], failure);
    }
    void *code = sljit_generate_code(compiler, 0, NULL);
    sljit_free_compiler(compiler);
    free(labels);
    free(jumps);
    afree(errors);
    return code;
}

void *behavior_jit_control(const graph_ir_control *instruction)
{
    struct sljit_compiler *compiler = sljit_create_compiler(NULL);
    if (!compiler) {
        return NULL;
    }
    sljit_emit_enter(compiler, 0, SLJIT_ARGS1(32, P), 2, 1, 0);
    struct sljit_jump *error = NULL;
    struct sljit_jump *otherwise = NULL;
    struct sljit_jump *done = NULL;
    if (instruction->op == GRAPH_IR_BRANCH) {
        sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS1(32, P), SLJIT_IMM, SLJIT_FUNC_ADDR(behavior_control_branch));
        error = sljit_emit_cmp(compiler, SLJIT_SIG_LESS | SLJIT_32, SLJIT_R0, 0, SLJIT_IMM, 0);
        otherwise = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R0, 0, SLJIT_IMM, 0);
        sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_IMM, 0);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(32, P, 32), SLJIT_IMM, SLJIT_FUNC_ADDR(flow_context_continue));
        done = sljit_emit_jump(compiler, SLJIT_JUMP);
        struct sljit_label *false_target = sljit_emit_label(compiler);
        if (sljit_get_compiler_error(compiler) != SLJIT_SUCCESS) {
            sljit_free_compiler(compiler);
            return NULL;
        }
        sljit_set_label(otherwise, false_target);
        sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_S0, 0);
        sljit_emit_op1(compiler, SLJIT_MOV32, SLJIT_R1, 0, SLJIT_IMM, 1);
        sljit_emit_icall(compiler, SLJIT_CALL, SLJIT_ARGS2(32, P, 32), SLJIT_IMM, SLJIT_FUNC_ADDR(flow_context_continue));
    } else {
        sljit_emit_op1(
            compiler,
            SLJIT_MOV_P,
            SLJIT_R0,
            0,
            SLJIT_IMM,
            (sljit_sw)instruction->node
        );
        sljit_emit_op1(compiler, SLJIT_MOV_P, SLJIT_R1, 0, SLJIT_S0, 0);
        sljit_emit_icall(
            compiler,
            SLJIT_CALL,
            SLJIT_ARGS2(32, P, P),
            SLJIT_IMM,
            SLJIT_FUNC_ADDR(instruction->host)
        );
    }
    struct sljit_label *exit = sljit_emit_label(compiler);
    if (sljit_get_compiler_error(compiler) != SLJIT_SUCCESS) {
        sljit_free_compiler(compiler);
        return NULL;
    }
    if (error) {
        sljit_set_label(error, exit);
    }
    if (done) {
        sljit_set_label(done, exit);
    }
    sljit_emit_return(compiler, SLJIT_MOV32, SLJIT_R0, 0);
    void *code = sljit_generate_code(compiler, 0, NULL);
    sljit_free_compiler(compiler);
    return code;
}
#else
void *behavior_jit_data(const graph_ir *, const graph_ir_block *)
{
    return NULL;
}
void *behavior_jit_control(const graph_ir_control *)
{
    return NULL;
}
#endif

void behavior_jit_free(void *code)
{
#if CHIBI_HAVE_SJLIT
    if (code) {
        sljit_free_code(code, NULL);
    }
#else
    (void)code;
#endif
}
