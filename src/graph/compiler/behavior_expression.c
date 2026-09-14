#include "graph/compiler/behavior_expression.h"

#include "graph/model/behavior_asset.h"
#include "graph/runtime/instance.h"
#include "support/array.h"

#include <stddef.h>
#include <string.h>
#if CHIBI_HAVE_SJLIT
#include <sljitLir.h>

typedef struct
{
    const node *node;
    int pin;
    bool output;
    int value;
} slot;
typedef struct
{
    int op; /* zero: literal; one: variable; otherwise SLJIT float opcode */
    pin_type type;
    const pin_value *literal;
    int variable;
    int left, right;
    int spill;
} expression;
typedef struct
{
    expression *values;
    slot *slots;
    const behavior_asset *asset;
} builder;

static bool real(const pin_spec *pin)
{
    return pin->container == PIN_CONTAINER_SINGLE &&
           (pin->type == PIN_F32 || pin->type == PIN_F64);
}
static int find(const builder *b, const node *n, int pin, bool output)
{
    for (int i = acount(b->slots) - 1; i >= 0; i--) {
        const slot *s = &b->slots[i];
        if (s->node == n && s->pin == pin && s->output == output) {
            return s->value;
        }
    }
    return -1;
}
static int add(builder *b, expression e)
{
    apush(b->values, e);
    return acount(b->values) - 1;
}
static int variable(builder *b, uint64_t id, const pin_spec *spec)
{
    for (int i = 0; i < acount(b->asset->variables); i++) {
        const behavior_variable *v = &b->asset->variables[i];
        if (v->id == id && v->type == spec->type && v->container == PIN_CONTAINER_SINGLE) {
            return add(b, (expression){ .op = 1, .type = v->type, .variable = i });
        }
    }
    return -1;
}
static int bind(builder *b, const pin_spec *spec)
{
    if (!real(spec)) {
        return -1;
    }
    if (!spec->binding) {
        if (spec->default_value.type != spec->type) {
            return -1;
        }
        return add(b, (expression){ .type = spec->type, .literal = &spec->default_value });
    }
    const pin_binding *binding = spec->binding;
    if (binding->kind != PIN_BINDING_VARIABLE ||
        (binding->source_asset && strcmp(binding->source_asset, b->asset->name))) {
        return -1;
    }
    return variable(b, binding->variable_id, spec);
}
static int evaluate(builder *b, const node *n)
{
    if (n->disabled || n->desc->flow || n->output_count != 1 ||
        !real(&n->output_pins[0])) {
        return -1;
    }
    if (n->desc->pure_kind == NODE_PURE_GET) {
        return variable(b, n->reference_id, &n->output_pins[0]);
    }
    if (n->desc->pure_kind == NODE_PURE_CONST) {
        const pin_spec *spec = &n->output_pins[0];
        if (spec->default_value.type != spec->type) {
            return -1;
        }
        return add(b, (expression){ .type = spec->type, .literal = &spec->default_value });
    }
    if (n->desc->pure_kind == NODE_PURE_COPY && n->input_count == 1 &&
        real(&n->input_pins[0]) && n->input_pins[0].type == n->output_pins[0].type) {
        return find(b, n, 0, false);
    }
    if (n->desc->pure_kind != NODE_PURE_CALL || n->input_count != 2 ||
        !real(&n->input_pins[0]) || !real(&n->input_pins[1]) ||
        n->input_pins[0].type != n->output_pins[0].type ||
        n->input_pins[1].type != n->output_pins[0].type) {
        return -1;
    }
    int op;
    switch (n->desc->jit_kind) {
        case NODE_JIT_ADD: op = SLJIT_ADD_F64; break;
        case NODE_JIT_SUB: op = SLJIT_SUB_F64; break;
        case NODE_JIT_MUL: op = SLJIT_MUL_F64; break;
        case NODE_JIT_DIV: op = SLJIT_DIV_F64; break;
        default: return -1;
    }
    int left = find(b, n, 0, false), right = find(b, n, 1, false);
    if (left < 0 || right < 0) {
        return -1;
    }
    return add(b, (expression){ .op = op, .type = n->output_pins[0].type, .left = left, .right = right });
}
static int lower(builder *b, const graph_ir *ir, const graph_ir_block *block)
{
    int result = -1;
    for (int i = block->first; i < block->first + block->count; i++) {
        const graph_ir_behavior_instruction *in = &ir->behavior[i];
        if (in->op == GRAPH_IR_MEMO_BRANCH) {
            continue;
        }
        bool output = in->op == GRAPH_IR_EVALUATE;
        int pin = output ? 0 : in->pin;
        if (in->op == GRAPH_IR_EVALUATE) {
            result = evaluate(b, in->node);
        }
        if (in->op == GRAPH_IR_BIND_INPUT) {
            result = bind(b, &in->node->input_pins[pin]);
        }
        if (in->op == GRAPH_IR_READ_SLOT) {
            const pin_spec *to = &in->node->input_pins[pin];
            const pin_spec *from = &in->source->output_pins[in->source_pin];
            if (in->field || in->conversion || !real(to) || to->type != from->type) {
                return -1;
            }
            result = find(b, in->source, in->source_pin, true);
        }
        if (result < 0) {
            return -1;
        }
        apush(b->slots, ((slot){ in->node, pin, output, result }));
    }
    return result;
}
static void load(struct sljit_compiler *c, const builder *b, int value, int reg)
{
    const expression *e = &b->values[value];
    int op = SLJIT_MOV_F64 | (e->type == PIN_F32 ? SLJIT_32 : 0);
    if (e->op == 0) {
        sljit_emit_fop1(c, op, reg, 0, SLJIT_MEM0(), (sljit_sw)&e->literal->d);
        return;
    }
    if (e->op == 1) {
        sljit_emit_fop1(
            c,
            op,
            reg,
            0,
            SLJIT_MEM1(SLJIT_R0),
            e->variable * sizeof(pin_value) + offsetof(pin_value, d)
        );
        return;
    }
    sljit_emit_fop1(c, op, reg, 0, SLJIT_MEM1(SLJIT_SP), e->spill * sizeof(double));
}
static void *emit(builder *b, int result)
{
    int spills = 0;
    for (int i = 0; i < acount(b->values); i++) {
        expression *e = &b->values[i];
        e->spill = -1;
        if (e->op <= 1) {
            continue;
        }
        bool next = true, needed = false;
        for (int j = i + 1; j < acount(b->values); j++) {
            const expression *use = &b->values[j];
            if (use->op <= 1) {
                continue;
            }
            if (use->right == i || (use->left == i && !next)) {
                needed = true;
            }
            next = false;
        }
        if (result == i && !next) {
            needed = true;
        }
        if (needed) {
            e->spill = spills++;
        }
    }
    struct sljit_compiler *c = sljit_create_compiler(NULL);
    if (!c) {
        return NULL;
    }
    sljit_emit_enter(c, 0, SLJIT_ARGS2(32, P_R, P_R), 2 | SLJIT_ENTER_FLOAT(2), 0, spills * sizeof(double));
    sljit_emit_op1(c, SLJIT_MOV_P, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_R0), offsetof(behavior_instance, variables));
    int resident = -1;
    for (int i = 0; i < acount(b->values); i++) {
        const expression *e = &b->values[i];
        if (e->op <= 1) {
            continue;
        }
        if (resident != e->left) {
            load(c, b, e->left, SLJIT_FR0);
        }
        load(c, b, e->right, SLJIT_FR1);
        int width = e->type == PIN_F32 ? SLJIT_32 : 0;
        sljit_emit_fop2(c, e->op | width, SLJIT_FR0, 0, SLJIT_FR0, 0, SLJIT_FR1, 0);
        if (e->spill >= 0) {
            sljit_emit_fop1(
                c,
                SLJIT_MOV_F64 | width,
                SLJIT_MEM1(SLJIT_SP),
                e->spill * sizeof(double),
                SLJIT_FR0,
                0
            );
        }
        resident = i;
    }
    if (resident != result) {
        load(c, b, result, SLJIT_FR0);
    }
    int width = b->values[result].type == PIN_F32 ? SLJIT_32 : 0;
    sljit_emit_fop1(c, SLJIT_MOV_F64 | width, SLJIT_MEM1(SLJIT_R1), offsetof(pin_value, d), SLJIT_FR0, 0);
    sljit_emit_return(c, SLJIT_MOV32, SLJIT_IMM, 0);
    void *code = sljit_get_compiler_error(c) == SLJIT_SUCCESS
                   ? sljit_generate_code(c, 0, NULL)
                   : NULL;
    sljit_free_compiler(c);
    return code;
}
#endif

bool behavior_expression_compile(
    const graph_ir *ir,
    const graph_ir_block *block,
    const behavior_asset *asset,
    void **code
)
{
    *code = NULL;
#if CHIBI_HAVE_SJLIT
    builder b = { .asset = asset };
    int result = lower(&b, ir, block);
    if (result >= 0) {
        *code = emit(&b, result);
    }
    afree(b.values);
    afree(b.slots);
    return result < 0 || *code;
#else
    (void)ir;
    (void)block;
    (void)asset;
    return true;
#endif
}
