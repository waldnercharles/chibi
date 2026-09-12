#include "graph/compiler/behavior_function_internal.h"
#include "graph/compiler/behavior_jit.h"
#include "graph/model/registry.h"
#include "graph/nodes/compare_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/nodes/math_nodes.h"
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
    int after, pc, frame;
} task;
typedef struct
{
    behavior_function *function;
    const behavior_asset *asset;
    graph *graph;
    const node *entry;
    const node *root_entry;
    task *tasks;
    output *outputs;
    const node **evaluating;
    unsigned call_depth;
    int frame, source;
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
#if CHIBI_HAVE_SJLIT && INTPTR_MAX < INT64_MAX
    /* Native 32-bit pair lowering is not implemented yet. The general
     * compiled backend still handles wide values on those targets. */
    if (type == PIN_I64 || type == PIN_U64) {
        return false;
    }
#endif
    return (type >= PIN_BOOL && type <= PIN_F64) || type == PIN_ENUM;
}
static int emit(builder *b, behavior_function_instruction i)
{
    i.frame = b->frame;
    i.source = b->source;
    apush(b->function->instructions, i);
    return acount(b->function->instructions) - 1;
}
static int value(builder *b, behavior_function_instruction i)
{
    if (!scalar(i.type)) {
        return -1;
    }
    if (i.op == BF_CONSTANT) {
        size_t size = behavior_scalar_size(i.type);
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
/* Division and remainder of the same snapshots share one checked operation.
 * The second result uses a typed slot; no extra runtime instruction is needed. */
static int divide(builder *b, behavior_function_instruction i)
{
    for (int pc = acount(b->function->instructions) - 1; pc >= 0; pc--) {
        behavior_function_instruction *p = &b->function->instructions[pc];
        if (p->op == BF_CHECK) {
            break;
        }
        if ((p->op != BF_DIV && p->op != BF_MOD) || p->type != i.type ||
            p->left != i.left || p->right != i.right) {
            continue;
        }
        if (p->op == i.op) {
            return p->output;
        }
        if (p->target < 0) {
            p->target = acount(b->function->slots);
            apush(b->function->slots, i.type);
        }
        return p->target;
    }
    i.target = -1;
    return value(b, i);
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
static int local(builder *b, int frame, const node *n, int pin)
{
    const pin_spec *spec = &n->output_pins[pin];
    if (!scalar(spec->type) || spec->container != PIN_CONTAINER_SINGLE ||
        spec->kind != PIN_DATA || spec->field_source ||
        spec->default_value.type != spec->type) {
        builder at = *b;
        if (graph_node_by_id(b->graph, n->id) != n) {
            at.graph = b->function->frames[frame].source.graph;
        }
        return unsupported(&at, n, pin, true, "stored output type, default or projection");
    }
    for (int i = 0; i < acount(b->function->locals); i++) {
        const behavior_function_local *l = &b->function->locals[i];
        if (l->frame == frame && l->node == n && l->pin == pin) {
            return i;
        }
    }
    apush(
        b->function->locals,
        ((
            behavior_function_local
        ){ .initial = spec->default_value, .frame = frame, .node = n, .pin = pin })
    );
    return acount(b->function->locals) - 1;
}
static int convert(builder *b, int slot, pin_type type)
{
    if (slot < 0 || b->function->slots[slot] == type) {
        return slot;
    }
    if ((type != PIN_F32 && type != PIN_F64) ||
        (b->function->slots[slot] != PIN_F32 && b->function->slots[slot] != PIN_F64)) {
        return -1;
    }
    return value(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_CONVERT, .type = type, .left = slot, .right = slot }
    );
}
static int pure_call(builder *b, node *n, int pin)
{
    graph *target = behavior_asset_find_graph(b->asset, n->reference_id);
    if (!target || target->kind != GRAPH_FUNCTION ||
        (target->call_depth_limit && b->call_depth >= (unsigned)target->call_depth_limit)) {
        return -1;
    }
    node *entry = graph_node_by_id(target, target->entry_id),
         *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret ||
        (entry->output_count && entry->output_pins[0].kind == PIN_EXEC)) {
        return -1;
    }
    if (target->call_depth_limit) {
        unsigned limit = (unsigned)target->call_depth_limit - b->call_depth;
        if (!b->function->pure_depth_limit || limit < b->function->pure_depth_limit) {
            b->function->pure_depth_limit = limit;
        }
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
    int parent_source = b->source;
    b->source = acount(b->function->sources);
    apush(b->function->sources, ((behavior_function_source){ target, n, parent_source }));
    output *saved = b->outputs;
    b->call_depth++;
    b->graph = target;
    b->outputs = NULL;
    int at = 0, result = -1;
    output *returns = NULL;
    for (int i = 0; i < entry->output_count; i++) {
        const pin_spec *spec = &entry->output_pins[i];
        if (spec->kind != PIN_DATA) {
            continue;
        }
        if (at >= acount(args) || spec->field_source) {
            goto done;
        }
        int arg = convert(b, args[at++], spec->type);
        if (arg < 0) {
            goto done;
        }
        apush(b->outputs, ((output){ entry, i, arg }));
    }
    at = 0;
    for (int i = 0; i < ret->input_count; i++) {
        if (ret->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        /* Evaluation computes every return, even when the caller reads only
         * one. Cache the complete call so shared expressions run once. */
        int slot = input(b, ret, i);
        if (slot < 0 || at >= n->output_count || n->output_pins[at].field_source ||
            b->function->slots[slot] != n->output_pins[at].type) {
            goto done;
        }
        apush(returns, ((output){ n, at++, slot }));
    }
    if (pin < acount(returns)) {
        result = returns[pin].slot;
    }
done:
    afree(args);
    afree(b->outputs);
    b->outputs = saved;
    b->graph = parent;
    b->source = parent_source;
    b->call_depth--;
    if (result >= 0) {
        for (int i = 0; i < acount(returns); i++) {
            apush(b->outputs, returns[i]);
        }
    }
    afree(returns);
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
    if (n == b->entry && b->frame == 0) {
        int ordinal = 0;
        for (int i = 0; i < pin; i++) {
            ordinal += n->output_pins[i].kind == PIN_DATA &&
                       !n->output_pins[i].field_source;
        }
        if (n->output_pins[pin].default_value.type == type) {
            result = value(
                b,
                (behavior_function_instruction){ .op = BF_ARGUMENT,
                                                 .type = type,
                                                 .left = ordinal,
                                                 .value = n->output_pins[pin].default_value }
            );
        }
    } else if (n == b->entry || n->desc == &NODE_SetVariable || n->desc == &NODE_CallFunction) {
        int index = local(b, b->frame, n, pin);
        if (index >= 0) {
            result = value(
                b,
                (behavior_function_instruction){ .op = BF_LOCAL, .type = type, .left = index }
            );
        }
    } else if (n->desc->pure_kind == NODE_PURE_FUNCTION) {
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
            case NODE_JIT_MIN: op = BF_MIN; break;
            case NODE_JIT_MAX: op = BF_MAX; break;
            default: break;
        }
#define MATH_NODE(code, name, fn)                                              \
    if (n->desc == &NODE_##name) {                                             \
        op = BF_##code;                                                        \
    }
        BEHAVIOR_MATH_UNARY(MATH_NODE)
        BEHAVIOR_MATH_BINARY(MATH_NODE)
#undef MATH_NODE
        if (n->desc == &NODE_Less || n->desc == &NODE_Integer_Less) {
            op = BF_LT;
        }
        if (n->desc == &NODE_Less_Or_Equal || n->desc == &NODE_Integer_Less_Or_Equal) {
            op = BF_LE;
        }
        if (n->desc == &NODE_Greater || n->desc == &NODE_Integer_Greater) {
            op = BF_GT;
        }
        if (n->desc == &NODE_Greater_Or_Equal ||
            n->desc == &NODE_Integer_Greater_Or_Equal) {
            op = BF_GE;
        }
        if (n->desc == &NODE_Equal || n->desc == &NODE_Integer_Equal) {
            op = BF_EQ;
        }
        if (n->desc == &NODE_Not_Equal || n->desc == &NODE_Integer_Not_Equal) {
            op = BF_NE;
        }
        if (n->desc == &NODE_Modulo && (type == PIN_I32 || type == PIN_I64)) {
            op = BF_MOD;
        }
        if (n->desc == &NODE_Has_Any_Flags) {
            op = BF_FLAGS;
        }
        if (op >= 0 && n->input_count >= 1) {
            int left = input(b, n, 0), right = left;
            if (op != BF_NEG && op != BF_ABS && !(op >= BF_SIN && op <= BF_LOG10)) {
                right = n->input_count == 2 ? input(b, n, 1) : -1;
            }
            if (left >= 0 && right >= 0 && b->function->slots[left] != PIN_BOOL &&
                b->function->slots[left] == b->function->slots[right] &&
                ((op >= BF_LT && op <= BF_NE) || op == BF_FLAGS ||
                 b->function->slots[left] == type) &&
                (!behavior_function_math(op) || type == PIN_F64) &&
                (!behavior_scalar_integer(b->function->slots[left]) ||
                 b->function->slots[left] == PIN_I32 ||
                 b->function->slots[left] == PIN_I64 || op == BF_FLAGS)) {
                behavior_function_instruction i = { .op = op,
                                                    .type = type,
                                                    .left = left,
                                                    .right = right,
                                                    .node_id = n->id };
                result = (op == BF_DIV || op == BF_MOD) && behavior_scalar_integer(type)
                           ? divide(b, i)
                           : value(b, i);
            }
        }
        if (n->desc->jit_kind == NODE_JIT_CLAMP && n->input_count == 3 &&
            (type == PIN_F32 || type == PIN_F64 || type == PIN_I32 || type == PIN_I64)) {
            int x = input(b, n, 0), low = input(b, n, 1), high = input(b, n, 2);
            if (x >= 0 && low >= 0 && high >= 0 && b->function->slots[x] == type &&
                b->function->slots[low] == type && b->function->slots[high] == type) {
                /* The language defines Max(Min(value, high), low), including
                 * NaNs, signed zero and reversed bounds. */
                int bounded = value(
                    b,
                    (
                        behavior_function_instruction
                    ){ .op = BF_MIN, .type = type, .left = x, .right = high }
                );
                result = value(
                    b,
                    (
                        behavior_function_instruction
                    ){ .op = BF_MAX, .type = type, .left = bounded, .right = low }
                );
            }
        }
    }
    if (n->input_count == 1 && n->desc->pure_kind == NODE_PURE_CALL) {
        pin_type from = n->input_pins[0].type;
        bool to_real = n->desc == &NODE_To_Real && type == PIN_F64 &&
                       (from == PIN_U8 || from == PIN_I32 || from == PIN_I64);
        bool to_integer = ((n->desc == &NODE_To_Byte && type == PIN_U8) ||
                           (n->desc == &NODE_To_Integer && type == PIN_I32) ||
                           (n->desc == &NODE_To_Integer64 && type == PIN_I64)) &&
                          (from == PIN_F32 || from == PIN_F64 ||
                           (from == PIN_I32 && type == PIN_I64));
        if (to_real || to_integer) {
            int left = input(b, n, 0);
            if (left >= 0) {
                result = value(
                    b,
                    (
                        behavior_function_instruction
                    ){ .op = BF_CAST, .type = type, .left = left, .right = left }
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
    if (result >= 0 && n->desc->pure_kind != NODE_PURE_FUNCTION) {
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
        if (graph_input_conversion(b->graph, n, pin)) {
            return unsupported(b, n, pin, false, "input conversion");
        }
        return convert(b, get_output(b, source, source_pin), s->type);
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
        if (b->tasks[i].node == n && b->tasks[i].after == after &&
            b->tasks[i].frame == b->frame) {
            return i;
        }
    }
    if (acount(b->tasks) >= 1024) {
        return -2;
    }
    apush(b->tasks, ((task){ n, after, -1, b->frame }));
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
static int call(builder *b, node *n, int after)
{
    graph *target = behavior_asset_find_graph(b->asset, n->reference_id);
    if (!target || target->kind != GRAPH_FUNCTION) {
        return unsupported(b, n, -1, false, "function graph");
    }
    for (int frame = b->frame; frame >= 0;
         frame = b->function->frames[frame].source.parent) {
        if (b->function->frames[frame].source.graph == target) {
            return unsupported(b, n, -1, false, "recursive function call");
        }
    }
    if (acount(b->function->frames) >= 128) {
        return unsupported(b, n, -1, false, "function expansion limit");
    }
    node *entry = graph_node_by_id(target, target->entry_id);
    node *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret || entry->output_count == 0 ||
        entry->output_pins[0].kind != PIN_EXEC) {
        return unsupported(b, n, -1, false, "function entry or return");
    }
    int resume = successor(b, n, 0, after);
    if (resume < -1) {
        return -2;
    }
    int parent = b->frame, frame = acount(b->function->frames);
    int source = acount(b->function->sources);
    apush(b->function->sources, ((behavior_function_source){ target, n, b->source }));
    apush(
        b->function->frames,
        ((behavior_function_frame){ .source = { target, n, parent },
                                    .depth = b->function->frames[parent].depth + 1,
                                    .resume = resume,
                                    .source_index = source })
    );
    if (target->call_depth_limit) {
        emit(
            b,
            (behavior_function_instruction){ .op = BF_CALL_CHECK,
                                             .left = target->call_depth_limit,
                                             .node_id = n->id }
        );
    }
    int *args = NULL, *parameters = NULL;
    int at = 1;
    for (int i = 1; i < entry->output_count; i++) {
        if (entry->output_pins[i].kind != PIN_DATA || entry->output_pins[i].field_source) {
            continue;
        }
        while (at < n->input_count && n->input_pins[at].kind != PIN_DATA) {
            at++;
        }
        int arg = at < n->input_count ? input(b, n, at++) : -1;
        int parameter = local(b, frame, entry, i);
        if (arg >= 0) {
            arg = convert(b, arg, entry->output_pins[i].type);
        }
        if (arg < 0 || parameter < 0) {
            afree(args);
            afree(parameters);
            return -2;
        }
        apush(args, arg);
        apush(parameters, parameter);
    }
    emit(b, (behavior_function_instruction){ .op = BF_ENTER, .target = frame });
    for (int i = 0; i < acount(args); i++) {
        emit(
            b,
            (behavior_function_instruction){ .op = BF_SAVE,
                                             .type = b->function->slots[args[i]],
                                             .left = args[i],
                                             .target = parameters[i] }
        );
    }
    afree(args);
    afree(parameters);
    int end = acount(b->tasks);
    apush(b->tasks, ((task){ .pc = -1, .after = -1, .frame = frame }));
    b->frame = frame;
    int next = schedule(b, entry, end);
    b->frame = parent;
    return next;
}
static bool return_values(builder *b, node *n)
{
    behavior_function_frame frame = b->function->frames[b->frame];
    node *call = frame.source.call;
    int at = 0;
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        int slot = input(b, n, i), target = i;
        if (call) {
            while (at < call->output_count && (call->output_pins[at].kind != PIN_DATA ||
                                               call->output_pins[at].field_source)) {
                at++;
            }
            if (at == call->output_count) {
                return false;
            }
            slot = convert(b, slot, call->output_pins[at].type);
            target = local(b, frame.source.parent, call, at++);
        }
        if (slot < 0 || target < 0) {
            return false;
        }
        emit(
            b,
            (behavior_function_instruction){ .op = call ? BF_SAVE : BF_RESULT,
                                             .type = b->function->slots[slot],
                                             .left = slot,
                                             .target = target }
        );
    }
    if (call) {
        emit(b, (behavior_function_instruction){ .op = BF_LEAVE, .target = b->frame });
        emit(b, (behavior_function_instruction){ .op = BF_JUMP, .target = frame.resume });
        return true;
    }
    emit(b, (behavior_function_instruction){ .op = BF_RETURN, .node_id = n->id });
    return true;
}
static bool block(builder *b, int index)
{
    task t = b->tasks[index];
    node *n = t.node;
    b->frame = t.frame;
    behavior_function_frame frame = b->function->frames[b->frame];
    b->graph = frame.source.graph;
    b->source = frame.source_index;
    b->entry = b->frame ? graph_node_by_id(b->graph, b->graph->entry_id) : b->root_entry;
    b->tasks[index].pc = acount(b->function->instructions);
    aclear(b->outputs);
    if (!n) {
        emit(b, (behavior_function_instruction){ .op = BF_LEAVE, .target = b->frame });
        b->frame = frame.source.parent;
        b->source = b->function->frames[b->frame].source_index;
        emit(
            b,
            (behavior_function_instruction){ .op = BF_FAIL,
                                             .node_id = frame.source.call->id,
                                             .left = BEHAVIOR_FUNCTION_NO_RETURN }
        );
        return true;
    }
    if (n->disabled || n->breakpoint) {
        unsupported(b, n, -1, false, "disabled node or breakpoint");
        return false;
    }
    emit(b, (behavior_function_instruction){ .op = BF_CHECK, .node_id = n->id });
    int next = -2;
    if (n->desc == &NODE_Event || n->desc == &NODE_FunctionEntry) {
        next = successor(b, n, 0, t.after);
    } else if (n->desc == &NODE_Return && n->id == b->graph->return_id) {
        return return_values(b, n);
    } else if (n->desc == &NODE_CallFunction) {
        next = call(b, n, t.after);
        if (next < 0) {
            return false;
        }
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
        int stored = local(b, b->frame, n, 1);
        if (stored < 0) {
            return false;
        }
        emit(
            b,
            (behavior_function_instruction){ .op = BF_SAVE,
                                             .type = n->input_pins[1].type,
                                             .left = source,
                                             .target = stored }
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
/* Stored node outputs are observable only when another compiled expression
 * reads them. Remove unused storage before assigning native stack offsets. */
static void compact_locals(behavior_function *f)
{
    int *map = xcalloc((size_t)(acount(f->locals) + 1), sizeof *map);
    for (int i = 0; i < acount(f->instructions); i++) {
        if (f->instructions[i].op == BF_LOCAL) {
            map[f->instructions[i].left] = 1;
        }
    }
    int count = 0;
    for (int i = 0; i < acount(f->locals); i++) {
        if (map[i]) {
            f->locals[count] = f->locals[i];
            map[i] = count++;
        } else {
            map[i] = -1;
        }
    }
    asetlen(f->locals, count);
    int *pcs = xcalloc((size_t)acount(f->instructions), sizeof *pcs);
    behavior_function_instruction *instructions = NULL;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        behavior_function_instruction i = f->instructions[pc];
        pcs[pc] = acount(instructions);
        if (i.op == BF_SAVE) {
            i.target = map[i.target];
            if (i.target < 0) {
                continue;
            }
        }
        if (i.op == BF_LOCAL) {
            i.left = map[i.left];
        }
        apush(instructions, i);
    }
    for (int i = 0; i < acount(instructions); i++) {
        if (instructions[i].op == BF_JUMP || instructions[i].op == BF_BRANCH) {
            instructions[i].target = pcs[instructions[i].target];
        }
    }
    afree(f->instructions);
    f->instructions = instructions;
    free(pcs);
    free(map);
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
                  .entry = entry,
                  .root_entry = entry,
                  .function = xcalloc(1, sizeof(behavior_function)) };
    apush(b.function->sources, ((behavior_function_source){ g, NULL, -1 }));
    apush(b.function->frames, ((behavior_function_frame){ .source = { g, NULL, -1 } }));
    bool valid = true;
    bool pure = g->kind == GRAPH_FUNCTION &&
                (!entry->output_count || entry->output_pins[0].kind != PIN_EXEC);
    if (pure) {
        node *ret = graph_node_by_id(g, g->return_id);
        valid = ret && return_values(&b, ret);
    } else {
        schedule(&b, entry, -1);
        for (int i = 0; valid && i < acount(b.tasks); i++) {
            valid = block(&b, i);
        }
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
        compact_locals(b.function);
        behavior_function_optimize(b.function);
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
    behavior_jit_free(f->dispatch);
    behavior_function_free(f->fast);
    afree(f->instructions);
    afree(f->slots);
    afree(f->locals);
    afree(f->frames);
    afree(f->sources);
    free(f);
}
bool behavior_function_compile(behavior_function *f)
{
    f->code = behavior_function_jit(f);
    if (!f->code || !f->fast) {
        return f->code != NULL;
    }
    if (!behavior_function_compile(f->fast)) {
        return false;
    }
    f->dispatch = behavior_function_guard(f);
    return f->dispatch != NULL;
}
void *behavior_function_entry(const behavior_function *f)
{
    return f ? (f->dispatch ? f->dispatch : f->code) : NULL;
}
bool behavior_function_pure_depth(const behavior_function *f, unsigned depth)
{
    return !f->pure_depth_limit || depth < f->pure_depth_limit;
}
const behavior_function_source *behavior_function_source_at(const behavior_function *f, int source)
{
    return &f->sources[source];
}
