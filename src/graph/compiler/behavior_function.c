#include "graph/compiler/behavior_function_internal.h"
#include "graph/compiler/behavior_jit.h"
#include "graph/model/registry.h"
#include "graph/nodes/compare_nodes.h"
#include "graph/runtime/flow.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const node *node;
    int pin, slot;
} output;
typedef struct
{
    node *node;
    int after, pc;
} task;
typedef struct
{
    behavior_function *function;
    const behavior_asset *asset;
    graph *graph;
    task *tasks;
    output *outputs;
    const node **evaluating;
    unsigned call_depth;
    behavior_function_diagnostic *diagnostic;
} builder;
static int unsupported(builder *b, const node *n, int pin, bool output, const char *reason)
{
    if (!b->diagnostic->reason) {
        *b->diagnostic = (behavior_function_diagnostic){ b->graph, n, pin, output, reason };
    }
    return -1;
}
static bool scalar(pin_type type)
{
    return type == PIN_F32 || type == PIN_F64 || type == PIN_BOOL;
}
static int emit(builder *b, behavior_function_instruction i)
{
    apush(b->function->instructions, i);
    return acount(b->function->instructions) - 1;
}
static int value(builder *b, behavior_function_instruction i)
{
    if (!scalar(i.type)) {
        return -1;
    }
    if (i.op == BF_CONSTANT) {
        size_t size = i.type == PIN_BOOL ? sizeof(bool)
                    : i.type == PIN_F32  ? sizeof(float)
                                         : sizeof(double);
        for (int pc = acount(b->function->instructions) - 1; pc >= 0; pc--) {
            const behavior_function_instruction *previous = &b->function->instructions[pc];
            if (previous->op == BF_CHECK) {
                break;
            }
            if (previous->op == BF_CONSTANT && previous->type == i.type &&
                !memcmp(&previous->value.d, &i.value.d, size)) {
                return previous->output;
            }
        }
    }
    i.output = acount(b->function->slots);
    apush(b->function->slots, i.type);
    emit(b, i);
    return i.output;
}
static int variable(const builder *b, uint64_t id, pin_type type)
{
    for (int i = 0; i < acount(b->asset->variables); i++) {
        const behavior_variable *v = &b->asset->variables[i];
        if (v->id == id && v->type == type && v->container == PIN_CONTAINER_SINGLE) {
            return i;
        }
    }
    return -1;
}
static int input(builder *b, node *n, int pin);
static int pure_call(builder *b, node *n, int pin)
{
    graph *target = behavior_asset_find_graph(b->asset, n->reference_id);
    if (!target || target->kind != GRAPH_FUNCTION ||
        (target->call_depth_limit && b->call_depth >= (unsigned)target->call_depth_limit)) {
        return -1;
    }
    node *entry = graph_node_by_id(target, target->entry_id),
         *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret) {
        return -1;
    }
    int *args = NULL;
    for (int i = 0; i < n->input_count; i++) {
        int arg = input(b, n, i);
        if (arg < 0) {
            afree(args);
            return -1;
        }
        apush(args, arg);
    }
    graph *parent = b->graph;
    output *saved = b->outputs;
    b->call_depth++;
    b->graph = target;
    b->outputs = NULL;
    int at = 0, result = -1;
    for (int i = 0; i < entry->output_count; i++) {
        const pin_spec *spec = &entry->output_pins[i];
        if (spec->kind != PIN_DATA) {
            continue;
        }
        if (at >= acount(args) || spec->field_source ||
            spec->type != b->function->slots[args[at]]) {
            goto done;
        }
        apush(b->outputs, ((output){ entry, i, args[at++] }));
    }
    at = 0;
    for (int i = 0; i < ret->input_count; i++) {
        if (ret->input_pins[i].kind == PIN_DATA && at++ == pin) {
            result = input(b, ret, i);
            break;
        }
    }
done:
    afree(args);
    afree(b->outputs);
    b->outputs = saved;
    b->graph = parent;
    b->call_depth--;
    if (result >= 0 && b->function->slots[result] != n->output_pins[pin].type) {
        return -1;
    }
    return result;
}
static int get_output(builder *b, node *n, int pin)
{
    if (pin < 0 || pin >= n->output_count || n->disabled ||
        !scalar(n->output_pins[pin].type) ||
        n->output_pins[pin].container != PIN_CONTAINER_SINGLE ||
        n->output_pins[pin].field_source) {
        return unsupported(b, n, pin, true, "output type, projection or disabled node");
    }
    for (int i = 0; i < acount(b->outputs); i++) {
        output *o = &b->outputs[i];
        if (o->node == n && o->pin == pin) {
            return o->slot;
        }
    }
    if (acount(b->evaluating) > 128) {
        return -1;
    }
    for (int i = 0; i < acount(b->evaluating); i++) {
        if (b->evaluating[i] == n) {
            return -1;
        }
    }
    apush(b->evaluating, n);
    pin_type type = n->output_pins[pin].type;
    int result = -1;
    if (n->desc->pure_kind == NODE_PURE_FUNCTION) {
        result = pure_call(b, n, pin);
    } else if (n->desc->pure_kind == NODE_PURE_GET) {
        int index = variable(b, n->reference_id, type);
        if (index >= 0) {
            result = value(
                b,
                (behavior_function_instruction){ .op = BF_VARIABLE, .type = type, .left = index }
            );
        }
    } else if (n->desc->pure_kind == NODE_PURE_DELTA && type == PIN_F32) {
        result = value(b, (behavior_function_instruction){ .op = BF_DELTA, .type = type });
    } else if (n->desc->pure_kind == NODE_PURE_COPY && n->input_count == 1) {
        result = input(b, n, 0);
    } else if (n->desc->display_class == NODE_DISPLAY_LITERAL && n->input_count == 1) {
        result = input(b, n, 0);
    } else if (
        n->desc->pure_kind == NODE_PURE_CONST && !n->desc->flow &&
        n->output_pins[pin].default_value.type == type
    ) {
        result = value(
            b,
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = type,
                                             .value = n->output_pins[pin].default_value }
        );
    } else if (n->desc->pure_kind == NODE_PURE_CALL) {
        int op = -1;
        switch (n->desc->jit_kind) {
            case NODE_JIT_ADD: op = BF_ADD; break;
            case NODE_JIT_SUB: op = BF_SUB; break;
            case NODE_JIT_MUL: op = BF_MUL; break;
            case NODE_JIT_DIV: op = BF_DIV; break;
            case NODE_JIT_NEG: op = BF_NEG; break;
            case NODE_JIT_ABS: op = BF_ABS; break;
            default: break;
        }
        if (n->desc == &NODE_Less) {
            op = BF_LT;
        }
        if (n->desc == &NODE_Less_Or_Equal) {
            op = BF_LE;
        }
        if (n->desc == &NODE_Greater) {
            op = BF_GT;
        }
        if (n->desc == &NODE_Greater_Or_Equal) {
            op = BF_GE;
        }
        if (n->desc == &NODE_Equal) {
            op = BF_EQ;
        }
        if (n->desc == &NODE_Not_Equal) {
            op = BF_NE;
        }
        if (op >= 0 && n->input_count >= 1) {
            int left = input(b, n, 0), right = left;
            if (op != BF_NEG && op != BF_ABS) {
                right = n->input_count == 2 ? input(b, n, 1) : -1;
            }
            if (left >= 0 && right >= 0 && b->function->slots[left] != PIN_BOOL &&
                b->function->slots[left] == b->function->slots[right] &&
                (op >= BF_LT || b->function->slots[left] == type)) {
                result = value(
                    b,
                    (
                        behavior_function_instruction
                    ){ .op = op, .type = type, .left = left, .right = right }
                );
            }
        }
    }
    apop(b->evaluating);
    if (result < 0) {
        return unsupported(b, n, pin, true, "data operation");
    }
    if (result >= 0 && b->function->slots[result] != type) {
        return -1;
    }
    if (result >= 0) {
        apush(b->outputs, ((output){ n, pin, result }));
    }
    return result;
}
static int input(builder *b, node *n, int pin)
{
    const pin_spec *s = &n->input_pins[pin];
    if (!scalar(s->type) || s->container != PIN_CONTAINER_SINGLE || s->kind != PIN_DATA) {
        return unsupported(b, n, pin, false, "input type or container");
    }
    node *source;
    int source_pin;
    if (graph_input_source(b->graph, n, pin, &source, &source_pin)) {
        if (graph_input_conversion(b->graph, n, pin) ||
            source->output_pins[source_pin].type != s->type) {
            return unsupported(b, n, pin, false, "input conversion");
        }
        return get_output(b, source, source_pin);
    }
    if (!s->binding) {
        if (s->default_value.type != s->type) {
            return -1;
        }
        return value(
            b,
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = s->type,
                                             .value = s->default_value }
        );
    }
    const pin_binding *binding = s->binding;
    if (binding->kind == PIN_BINDING_VARIABLE &&
        (!binding->source_asset || !strcmp(binding->source_asset, b->asset->name))) {
        int index = variable(b, binding->variable_id, s->type);
        if (index >= 0) {
            return value(
                b,
                (behavior_function_instruction){ .op = BF_VARIABLE,
                                                 .type = s->type,
                                                 .left = index }
            );
        }
    }
    if (binding->kind == PIN_BINDING_DELTA_TIME && s->type == PIN_F32) {
        return value(b, (behavior_function_instruction){ .op = BF_DELTA, .type = PIN_F32 });
    }
    return unsupported(b, n, pin, false, "input binding");
}
static int schedule(builder *b, node *n, int after)
{
    if (!n) {
        return after;
    }
    for (int i = 0; i < acount(b->tasks); i++) {
        if (b->tasks[i].node == n && b->tasks[i].after == after) {
            return i;
        }
    }
    if (acount(b->tasks) >= 1024) {
        return -2;
    }
    apush(b->tasks, ((task){ n, after, -1 }));
    return acount(b->tasks) - 1;
}
static int successor(builder *b, node *n, int pin, int after)
{
    const edge *e = NULL;
    int count = graph_exec_output_edges(b->graph, n, pin, &e);
    if (!count) {
        return after;
    }
    if (count != 1) {
        return -2;
    }
    node *target = graph_node_by_id(b->graph, e->to_id);
    int output, index;
    if (!target || !graph_pin_by_id(target, e->to_pin_id, &output, &index) ||
        output || index != 0) {
        return -2;
    }
    return schedule(b, target, after);
}
static bool block(builder *b, int index)
{
    task t = b->tasks[index];
    node *n = t.node;
    if (n->disabled || n->breakpoint) {
        unsupported(b, n, -1, false, "disabled node or breakpoint");
        return false;
    }
    b->tasks[index].pc = acount(b->function->instructions);
    aclear(b->outputs);
    emit(b, (behavior_function_instruction){ .op = BF_CHECK, .node_id = n->id });
    int next = -2;
    if (n->desc == &NODE_Event) {
        next = successor(b, n, 0, t.after);
    } else if (n->desc == &NODE_SetVariable) {
        int source = input(b, n, 1),
            target = variable(b, n->reference_id, n->input_pins[1].type);
        if (source < 0 || target < 0) {
            return false;
        }
        emit(
            b,
            (behavior_function_instruction){ .op = BF_STORE,
                                             .type = n->input_pins[1].type,
                                             .left = source,
                                             .target = target }
        );
        next = successor(b, n, 0, t.after);
    } else if (n->desc == &NODE_Branch || n->desc == &NODE_WhileLoop) {
        int condition = input(b, n, 1);
        if (condition < 0 || b->function->slots[condition] != PIN_BOOL) {
            return false;
        }
        bool loop = n->desc == &NODE_WhileLoop;
        int yes = successor(b, n, 0, loop ? index : t.after);
        int no = successor(b, n, 1, t.after);
        if (yes < -1 || no < -1) {
            return false;
        }
        emit(b, (behavior_function_instruction){ .op = BF_BRANCH, .left = condition, .target = yes });
        next = no;
    } else if (n->desc == &NODE_Sequence) {
        next = t.after;
        for (int i = n->output_count - 1; i >= 0; i--) {
            next = successor(b, n, i, next);
            if (next < -1) {
                return false;
            }
        }
    } else {
        unsupported(b, n, -1, false, "control operation");
        return false;
    }
    if (next < -1) {
        return false;
    }
    emit(b, (behavior_function_instruction){ .op = BF_JUMP, .target = next });
    return true;
}
behavior_function *behavior_function_lower(
    const behavior_asset *asset,
    graph *g,
    node *entry,
    behavior_function_diagnostic *diagnostic
)
{
    *diagnostic = (behavior_function_diagnostic){ 0 };
    builder b = { .diagnostic = diagnostic,
                  .asset = asset,
                  .graph = g,
                  .function = xcalloc(1, sizeof(behavior_function)) };
    schedule(&b, entry, -1);
    bool valid = true;
    for (int i = 0; valid && i < acount(b.tasks); i++) {
        valid = block(&b, i);
    }
    int end = emit(&b, (behavior_function_instruction){ .op = BF_END });
    for (int i = 0; valid && i < acount(b.function->instructions); i++) {
        behavior_function_instruction *op = &b.function->instructions[i];
        if (op->op == BF_JUMP || op->op == BF_BRANCH) {
            op->target = op->target < 0 ? end : b.tasks[op->target].pc;
        }
    }
    afree(b.tasks);
    afree(b.outputs);
    afree(b.evaluating);
    if (valid) {
        return b.function;
    }
    unsupported(&b, entry, -1, false, "control shape, breakpoint or compilation limit");
    behavior_function_free(b.function);
    return NULL;
}
void behavior_function_free(behavior_function *f)
{
    if (!f) {
        return;
    }
    behavior_jit_free(f->code);
    afree(f->instructions);
    afree(f->slots);
    free(f);
}
bool behavior_function_compile(behavior_function *f)
{
    f->code = behavior_function_jit(f);
    return f->code != NULL;
}
void *behavior_function_entry(const behavior_function *f)
{
    return f ? f->code : NULL;
}

int behavior_function_run(const behavior_function *f, behavior_function_context *ctx, bool native)
{
    if (native) {
        assert(f->code);
        return ((int (*)(behavior_function_context *))f->code)(ctx);
    }
    pin_value *slots = xcalloc((size_t)(acount(f->slots) + 1), sizeof *slots);
    int result = 0;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        pin_value *out = &slots[i->output], a = { 0 }, b = { 0 };
        if (i->op >= BF_ADD && i->op <= BF_STORE) {
            a = slots[i->left];
            b = slots[i->right];
        }
        if (i->op == BF_BRANCH) {
            a = slots[i->left];
        }
        if (i->op < BF_STORE) {
            out->type = i->type;
        }
        switch (i->op) {
            case BF_CONSTANT: *out = i->value; break;
            case BF_VARIABLE: *out = ctx->variables[i->left]; break;
            case BF_DELTA: out->f = (float)ctx->delta_seconds; break;
            case BF_STORE:
                if (i->type == PIN_F32) {
                    ctx->variables[i->target].f = a.f;
                }
                if (i->type == PIN_F64) {
                    ctx->variables[i->target].d = a.d;
                }
                if (i->type == PIN_BOOL) {
                    ctx->variables[i->target].b = a.b;
                }
                break;
            case BF_CHECK:
                ctx->node_id = i->node_id;
                if (ctx->steps >= ctx->budget) {
                    result = FLOW_ERR_STEP_BUDGET;
                    goto done;
                }
                ctx->steps++;
                break;
            case BF_JUMP: pc = i->target - 1; break;
            case BF_BRANCH:
                if (a.b) {
                    pc = i->target - 1;
                }
                break;
            case BF_END: goto done;
            default: {
                double x = a.type == PIN_F32 ? a.f : a.d,
                       y = b.type == PIN_F32 ? b.f : b.d, r = 0;
                switch (i->op) {
                    case BF_ADD:
                        r = i->type == PIN_F32 ? (float)(a.f + b.f) : x + y;
                        break;
                    case BF_SUB:
                        r = i->type == PIN_F32 ? (float)(a.f - b.f) : x - y;
                        break;
                    case BF_MUL:
                        r = i->type == PIN_F32 ? (float)(a.f * b.f) : x * y;
                        break;
                    case BF_DIV:
                        r = i->type == PIN_F32 ? (float)(a.f / b.f) : x / y;
                        break;
                    case BF_NEG: r = -x; break;
                    case BF_ABS: r = fabs(x); break;
                    case BF_LT: out->b = x < y; break;
                    case BF_LE: out->b = x <= y; break;
                    case BF_GT: out->b = x > y; break;
                    case BF_GE: out->b = x >= y; break;
                    case BF_EQ: out->b = x == y; break;
                    case BF_NE: out->b = x != y; break;
                    default: assert(false);
                }
                if (i->type == PIN_F32) {
                    out->f = (float)r;
                }
                if (i->type == PIN_F64) {
                    out->d = r;
                }
            }
        }
    }
done:
    free(slots);
    return result;
}
