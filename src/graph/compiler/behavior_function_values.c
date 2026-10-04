#include "graph/compiler/behavior_jit.h"
#include "graph/compiler/behavior_lower_internal.h"
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/compare_nodes.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/nodes/math_nodes.h"
#include "graph/runtime/flow.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

int behavior_lower_unsupported(builder *b, const node *n, int pin, bool output, const char *reason)
{
    if (!b->diagnostic->reason) {
        *b->diagnostic = (behavior_function_diagnostic){ b->graph, n, pin, output, reason };
    }
    return -1;
}
bool behavior_lower_scalar(pin_type type)
{
#if CHIBI_HAVE_SJLIT && INTPTR_MAX < INT64_MAX
    /* Native 32-bit pair lowering is not implemented yet. The general
     * compiled backend still handles wide values on those targets. */
    if (type == PIN_I64 || type == PIN_U64 || type == PIN_HANDLE) {
        return false;
    }
#endif
    return (type >= PIN_BOOL && type <= PIN_F64) || type == PIN_ENUM ||
           type == PIN_HANDLE;
}
static int data_stack(builder *b, int source, int base)
{
    if (source == b->function->frames[b->frame].source_index) {
        return base;
    }
    behavior_function_source call = b->function->sources[source];
    int parent = data_stack(b, call.parent, base);
    int index = acount(b->function->stacks);
    apush(
        b->function->stacks,
        ((behavior_function_source){ b->function->sources[call.parent].graph, call.call, parent })
    );
    return index;
}
int behavior_lower_emit(builder *b, behavior_function_instruction i)
{
    i.frame = b->frame;
    i.source = b->source;
    i.stack = b->stack;
    if (i.node_id && b->source != b->function->frames[b->frame].source_index &&
        (i.op == BF_DIV || i.op == BF_MOD || i.op == BF_REQUIRE ||
         i.op == BF_ASSERT || i.op == BF_COMPONENT || i.op == BF_COLUMN ||
         i.op == BF_QUERY || i.op == BF_HOST_CALL || i.op == BF_CALL_CHECK)) {
        int parent = data_stack(b, b->source, b->stack);
        i.stack = acount(b->function->stacks);
        apush(
            b->function->stacks,
            ((behavior_function_source){ b->graph, graph_node_by_id(b->graph, i.node_id), parent })
        );
    }
    apush(b->function->instructions, i);
    return acount(b->function->instructions) - 1;
}
/* Negative targets below -1 identify expression-local labels; -1 remains the
 * end-of-function target and nonnegative targets belong to flow tasks. */
int behavior_lower_label(builder *b)
{
    int label = -2 - acount(b->labels);
    apush(b->labels, -1);
    return label;
}
void behavior_lower_bind_label(builder *b, int label)
{
    assert(label <= -2 && -label - 2 < acount(b->labels));
    assert(b->labels[-label - 2] == -1);
    b->labels[-label - 2] = acount(b->function->instructions);
    b->constant_start = acount(b->function->instructions);
}
int behavior_lower_value(builder *b, behavior_function_instruction i)
{
    if (!behavior_lower_scalar(i.type) && !behavior_scalar_string(i.type)) {
        return -1;
    }
    /* String reads borrow their source until a retention boundary emits a copy.
     * Keep string pointers out of numeric operations. */
    if (behavior_scalar_string(i.type) && i.op != BF_CONSTANT &&
        i.op != BF_ARGUMENT && i.op != BF_LOCAL && i.op != BF_VARIABLE &&
        i.op != BF_COMMAND_RESULT && i.op != BF_LOAD && i.op != BF_READ &&
        i.op != BF_COPY_STRING && i.op != BF_INTERN_STRING && i.op != BF_SELECT) {
        return -1;
    }
    if (i.op == BF_CONSTANT) {
        size_t size = behavior_scalar_size(i.type);
        for (int pc = acount(b->function->instructions) - 1; pc >= b->constant_start;
             pc--) {
            const behavior_function_instruction *previous = &b->function->instructions[pc];
            if (previous->op == BF_CHECK || previous->op == BF_JUMP ||
                previous->op == BF_BRANCH) {
                break;
            }
            if (previous->op == BF_CONSTANT && previous->type == i.type &&
                !memcmp(&previous->value.d, &i.value.d, size)) {
                return previous->output;
            }
        }
    }
    b->function->managed |= behavior_scalar_string(i.type);
    i.output = acount(b->function->slots);
    apush(b->function->slots, i.type);
    behavior_lower_emit(b, i);
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
    return behavior_lower_value(b, i);
}
static int binary(builder *b, int op, int left, int right, const node *n)
{
    pin_type type = b->function->slots[left];
    behavior_function_instruction i = { .op = op,
                                        .type = type,
                                        .left = left,
                                        .right = right,
                                        .node_id = n->id };
    if (op >= BF_LT && op <= BF_NE) {
        i.type = PIN_BOOL;
    }
    if (op == BF_MOD && (type == PIN_F32 || type == PIN_F64)) {
        i.op = type == PIN_F32 ? BF_MOD_F32 : BF_MOD_F64;
    }
    return (op == BF_DIV || op == BF_MOD) && behavior_scalar_integer(type)
             ? divide(b, i)
             : behavior_lower_value(b, i);
}
static int select_value(builder *b, int condition, int yes, int no)
{
    assert(b->function->slots[yes] == b->function->slots[no]);
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_SELECT,
                                         .type = b->function->slots[yes],
                                         .left = condition,
                                         .right = yes,
                                         .target = no }
    );
}
static int scalar_binary(builder *b, int op, int left, int right, const node *n)
{
    pin_type a = b->function->slots[left], c = b->function->slots[right];
    if (a == c && (a == PIN_F32 || a == PIN_F64 || a == PIN_I32 || a == PIN_I64)) {
        return behavior_lower_inherit_name(b, binary(b, op, left, right, n), left);
    }
    if (a == PIN_F64 && c == PIN_F32) {
        return behavior_lower_inherit_name(
            b,
            binary(b, op, left, behavior_lower_convert(b, right, PIN_F64), n),
            left
        );
    }
    if (a != PIN_F32 || c != PIN_F64) {
        return -1;
    }
    /* numeric_op retains double precision when either mixed-width operand
     * is subnormal. Keep a widened representation until the consuming pin
     * applies its declared coercion. */
    int wide = behavior_lower_convert(b, left, PIN_F64);
    int zero = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .value = { .type = PIN_F64 } }
    );
    int minimum = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .value = { .type = PIN_F64, .d = FLT_MIN } }
    );
    int no = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .value = { .type = PIN_BOOL } }
    );
    int subnormal[2];
    for (int i = 0; i < 2; i++) {
        int magnitude = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_ABS,
                                             .type = PIN_F64,
                                             .left = i ? right : wide,
                                             .right = i ? right : wide }
        );
        subnormal[i] = select_value(
            b,
            binary(b, BF_GT, magnitude, zero, n),
            binary(b, BF_LT, magnitude, minimum, n),
            no
        );
    }
    int yes = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .value = { .type = PIN_BOOL, .b = true } }
    );
    int condition = select_value(b, subnormal[0], yes, subnormal[1]);
    int narrow = binary(b, op, left, behavior_lower_convert(b, right, PIN_F32), n);
    int result = behavior_lower_inherit_name(
        b,
        select_value(b, condition, binary(b, op, wide, right, n), behavior_lower_convert(b, narrow, PIN_F64)),
        left
    );
    scalar_value *meta = behavior_lower_scalar_value(b, result);
    if (meta) {
        meta->wide = condition;
        meta->logical_type = PIN_F32;
    }
    return result;
}
static int aggregate_binary(builder *b, int op, int left, int right, node *n)
{
    aggregate *lp = behavior_lower_aggregate(b, left),
              *rp = behavior_lower_aggregate(b, right);
    if (!lp && !rp) {
        return scalar_binary(b, op, left, right, n);
    }
    if (lp) {
        behavior_lower_require(b, lp, n, 0);
    }
    if (rp) {
        behavior_lower_require(b, rp, n, 1);
    }
    aggregate source = lp ? *lp : *rp;
    aggregate result = behavior_lower_shape(source.type->name);
    result.name = source.name;
    if (!result.type) {
        return -1;
    }
    result.present = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .value = { .type = PIN_BOOL, .b = true } }
    );
    for (int i = 0; i < source.type->field_count; i++) {
        const type_field *field = &source.type->fields[i];
        if (field->type == PIN_STRUCT) {
            afree(result.fields);
            return -1;
        }
        int a = left, c = right;
        for (int side = 0; side < 2; side++) {
            aggregate *operand = side ? rp : lp;
            if (!operand) {
                continue;
            }
            int slot = -1;
            for (int k = 0; k < operand->type->field_count; k++) {
                const type_field *peer = &operand->type->fields[k];
                if (!strcmp(peer->name, field->name)) {
                    slot = operand->fields[k].slot;
                    break;
                }
            }
            if (slot < 0) {
                afree(result.fields);
                return -1;
            }
            if (side) {
                c = slot;
            } else {
                a = slot;
            }
        }
        int slot = scalar_binary(b, op, a, c, n);
        result.fields[i].slot = behavior_lower_convert(b, slot, field->type);
        if (result.fields[i].slot < 0) {
            afree(result.fields);
            return -1;
        }
    }
    return behavior_lower_aggregate_value(b, result);
}
/* Vector Length, Dot and Distance read CF_V2 fields in double, as the nodes do. */
static int vector_scalar(builder *b, node *n)
{
    bool length = n->desc->jit_kind == NODE_JIT_V2_LENGTH;
    int x[2] = { -1, -1 }, y[2] = { -1, -1 };
    for (int i = 0; i < (length ? 1 : 2); i++) {
        aggregate *a = behavior_lower_aggregate(b, behavior_lower_input(b, n, i));
        if (!a || acount(a->fields) != 2 ||
            !pin_compatible(
                &(pin_spec){ .type = PIN_STRUCT, .type_name = a->type->name },
                &(pin_spec){ .type = PIN_STRUCT, .type_name = "CF_V2" }
            )) {
            return -1;
        }
        behavior_lower_require(b, a, n, i);
        x[i] = behavior_lower_convert(b, a->fields[0].slot, PIN_F64);
        y[i] = behavior_lower_convert(b, a->fields[1].slot, PIN_F64);
    }
    if (n->desc->jit_kind == NODE_JIT_V2_DOT) {
        return binary(
            b,
            BF_ADD,
            binary(b, BF_MUL, x[0], x[1], n),
            binary(b, BF_MUL, y[0], y[1], n),
            n
        );
    }
    int dx = length ? x[0] : binary(b, BF_SUB, x[0], x[1], n);
    int dy = length ? y[0] : binary(b, BF_SUB, y[0], y[1], n);
    return behavior_lower_value(
        b,
        (
            behavior_function_instruction
        ){ .op = BF_HYPOT, .type = PIN_F64, .left = dx, .right = dy }
    );
}

/* Step and Mix in double, as the nodes compute them. */
static int step_or_mix(builder *b, node *n)
{
    int in[3] = { -1, -1, -1 };
    for (int i = 0; i < n->input_count && i < 3; i++) {
        in[i] = behavior_lower_convert(b, behavior_lower_input(b, n, i), PIN_F64);
        if (in[i] < 0) {
            return -1;
        }
    }
    if (n->desc == &NODE_Mix) {
        int span = binary(b, BF_SUB, in[1], in[0], n);
        return binary(b, BF_ADD, in[0], binary(b, BF_MUL, span, in[2], n), n);
    }
    int zero = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .value = { .type = PIN_F64 } }
    );
    int one = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .value = { .type = PIN_F64, .d = 1 } }
    );
    return select_value(b, binary(b, BF_LT, in[0], in[1], n), zero, one);
}

static int normalize_aggregate(builder *b, node *n, int slot)
{
    aggregate *source = behavior_lower_aggregate(b, slot);
    if (!source || acount(source->fields) != 2 ||
        !pin_compatible(
            &(pin_spec){ .type = PIN_STRUCT, .type_name = source->type->name },
            &(pin_spec){ .type = PIN_STRUCT, .type_name = "CF_V2" }
        )) {
        return -1;
    }
    behavior_lower_require(b, source, n, 0);
    int x = behavior_lower_convert(b, source->fields[0].slot, PIN_F64),
        y = behavior_lower_convert(b, source->fields[1].slot, PIN_F64);
    int length = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_HYPOT, .type = PIN_F64, .left = x, .right = y }
    );
    int zero = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .value = { .type = PIN_F64 } }
    );
    int infinity = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_F64,
                                         .value = { .type = PIN_F64, .d = INFINITY } }
    );
    int no = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .value = { .type = PIN_BOOL } }
    );
    int valid = select_value(
        b,
        binary(b, BF_GT, length, zero, n),
        binary(b, BF_LT, length, infinity, n),
        no
    );
    aggregate result = behavior_lower_shape("CF_V2");
    result.present = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .value = { .type = PIN_BOOL, .b = true } }
    );
    for (int i = 0; i < 2; i++) {
        int normalized = binary(b, BF_DIV, i ? y : x, length, n);
        result.fields[i]
            .slot = behavior_lower_convert(b, select_value(b, valid, normalized, zero), PIN_F32);
    }
    return behavior_lower_aggregate_value(b, result);
}
/* (cos, sin) of the angle, computed in double as the node does. */
static int from_angle(builder *b, node *n)
{
    int angle = behavior_lower_convert(b, behavior_lower_input(b, n, 0), PIN_F64);
    if (angle < 0) {
        return -1;
    }
    aggregate result = behavior_lower_shape("CF_V2");
    result.present = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .value = { .type = PIN_BOOL, .b = true } }
    );
    const behavior_function_op ops[] = { BF_COS, BF_SIN };
    for (int i = 0; i < 2; i++) {
        int value = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = ops[i],
                                             .type = PIN_F64,
                                             .left = angle,
                                             .right = angle,
                                             .node_id = n->id }
        );
        result.fields[i].slot = behavior_lower_convert(b, value, PIN_F32);
    }
    return behavior_lower_aggregate_value(b, result);
}
int behavior_lower_variable(const builder *b, uint64_t id, pin_type type, pin_container container)
{
    for (int i = 0; i < acount(b->asset->variables); i++) {
        const behavior_variable *v = &b->asset->variables[i];
        if (v->id == id && v->type == type && v->container == container) {
            return i;
        }
    }
    return -1;
}
int behavior_lower_convert(builder *b, int slot, pin_type type)
{
    array_value *array = behavior_lower_array(b, slot);
    if (array) {
        return array->type == type ? slot : -1;
    }
    if (slot < 0 || b->function->slots[slot] == type) {
        return slot;
    }
    if ((type != PIN_F32 && type != PIN_F64) ||
        (b->function->slots[slot] != PIN_F32 && b->function->slots[slot] != PIN_F64)) {
        return -1;
    }
    return behavior_lower_inherit_name(
        b,
        behavior_lower_value(
            b,
            (
                behavior_function_instruction
            ){ .op = BF_CONVERT, .type = type, .left = slot, .right = slot }
        ),
        slot
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
        if (n->input_pins[i].kind != PIN_DATA || n->input_pins[i].field_source) {
            continue;
        }
        int arg = behavior_lower_input(b, n, i);
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
    const node **traced = b->traced;
    b->call_depth++;
    b->graph = target;
    b->outputs = NULL;
    b->traced = NULL;
    int at = 0, result = -1;
    output *returns = NULL;
    for (int i = 0; i < entry->output_count; i++) {
        const pin_spec *spec = &entry->output_pins[i];
        if (spec->kind != PIN_DATA || spec->field_source) {
            continue;
        }
        if (at >= acount(args)) {
            goto done;
        }
        int arg = behavior_lower_convert(b, args[at++], spec->type);
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
        int slot = behavior_lower_input(b, ret, i);
        while (at < n->output_count && (n->output_pins[at].kind != PIN_DATA ||
                                        n->output_pins[at].field_source)) {
            at++;
        }
        if (slot < 0 || at >= n->output_count ||
            b->function->slots[slot] != n->output_pins[at].type) {
            goto done;
        }
        apush(returns, ((output){ n, at++, slot }));
    }
    for (int i = 0; i < acount(returns); i++) {
        if (returns[i].pin == pin) {
            result = returns[i].slot;
        }
    }
done:
    afree(args);
    afree(b->outputs);
    b->outputs = saved;
    afree(b->traced);
    b->traced = traced;
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
/* A number of type `to` holding 1 or 0. */
static int bit_constant(builder *b, pin_type to, bool one)
{
    pin_value value = { .type = to };
    switch (to) {
        case PIN_U8: value.u8 = one; break;
        case PIN_I32: value.i = one; break;
        case PIN_I64: value.i64 = one; break;
        default: value.d = one; break;
    }
    return behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT, .type = to, .value = value }
    );
}
static int numeric_cast(builder *b, int slot, const node_desc *conversion, pin_type to)
{
    if (slot < 0) {
        return -1;
    }
    pin_type from = b->function->slots[slot];
    bool real = conversion == &NODE_To_Real && to == PIN_F64 &&
                (from == PIN_U8 || from == PIN_I32 || from == PIN_I64);
    bool integer = ((conversion == &NODE_To_Byte && to == PIN_U8) ||
                    (conversion == &NODE_To_Integer && to == PIN_I32) ||
                    (conversion == &NODE_To_Integer64 && to == PIN_I64)) &&
                   (from == PIN_F32 || from == PIN_F64 ||
                    (from == PIN_I32 && to == PIN_I64));
    bool flag = from == PIN_BOOL && conversion == conversion_for(from, to);
    if (!real && !integer && !flag) {
        return -1;
    }
    const char *name = to == PIN_F64 ? "double"
                     : to == PIN_U8  ? "byte"
                     : to == PIN_I32 ? "int"
                                     : "int64";
    int value = flag
                  ? select_value(b, slot, bit_constant(b, to, true), bit_constant(b, to, false))
                  : behavior_lower_value(
                        b,
                        (
                            behavior_function_instruction
                        ){ .op = BF_CAST, .type = to, .left = slot, .right = slot }
                    );
    return behavior_lower_named_constant(b, value, name);
}
static int to_name(builder *b, int source)
{
    if (source < 0 || b->function->slots[source] != PIN_STR) {
        return -1;
    }
    return behavior_lower_named_constant(
        b,
        behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_COPY_STRING,
                                             .type = PIN_NAME,
                                             .left = source,
                                             .value = { .str = "" } }
        ),
        "name"
    );
}
static int vector_splat(builder *b, int slot, const node_desc *conversion)
{
    if (slot < 0) {
        return -1;
    }
    aggregate a = behavior_lower_shape(conversion->outputs[0].type_name);
    if (!a.type) {
        return -1;
    }
    int value = behavior_lower_convert(b, slot, PIN_F32);
    if (value < 0) {
        afree(a.fields);
        return -1;
    }
    a.present = behavior_lower_value(
        b,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_BOOL,
                                         .value = { .type = PIN_BOOL, .b = true } }
    );
    for (int i = 0; i < acount(a.fields); i++) {
        assert(a.fields[i].type == PIN_F32);
        a.fields[i].slot = value;
    }
    return behavior_lower_aggregate_value(b, a);
}
static int output_value(builder *b, node *n, int pin)
{
    if (pin < 0 || pin >= n->output_count || n->disabled ||
        (!behavior_lower_scalar(n->output_pins[pin].type) &&
         !behavior_scalar_string(n->output_pins[pin].type) &&
         n->output_pins[pin].type != PIN_STRUCT &&
         n->output_pins[pin].container < PIN_CONTAINER_ARRAY) ||
        n->output_pins[pin].container > PIN_CONTAINER_SET) {
        return behavior_lower_unsupported(b, n, pin, true, "output type, projection or disabled node");
    }
    for (int i = 0; i < acount(b->outputs); i++) {
        output *o = &b->outputs[i];
        if (o->node == n && o->pin == pin) {
            return o->slot;
        }
    }
    if (n->output_pins[pin].field_source) {
        int source = -1;
        const type_field *field = graph_output_field(n, pin, &source);
        /* The outer call already marked this node as traced. Read its base
         * output without suppressing the first evaluation's dependencies. */
        int aggregate = field ? output_value(b, n, source) : -1;
        return aggregate < 0 ? -1 : behavior_lower_field(b, aggregate, field, n, pin);
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
            result = behavior_lower_source(b, BF_ARGUMENT, ordinal, &n->output_pins[pin], n, pin);
        }
    } else if (
        n == b->entry || n->desc == &NODE_SetVariable || n->desc == &NODE_CallFunction ||
        behavior_expression_mode_for(b, n) == GRAPH_EXPRESSION_ACTION ||
        behavior_lower_is_state(n->desc) ||
        (n->desc->command && n->desc->command->begin) ||
        n->desc == &NODE_ForLoop || n->desc == &NODE_ForLoopWithBreak ||
        n->desc == &NODE_ForEach || n->desc == &NODE_ForEachWithBreak ||
        n->desc->component_kind == NODE_COMPONENT_SET_SELF ||
        n->desc->component_kind == NODE_COMPONENT_SET_ENTITY ||
        n->desc->component_kind == NODE_COMPONENT_SET_FIELD
    ) {
        if (behavior_lower_is_state(n->desc) && !b->capture_outputs) {
            /* The data evaluator refreshes inputs before exposing a stored
             * state-node output. Preserve those effects, but never repeat the
             * flow operation. Dependency traversal was already recorded. */
            bool suppressed = b->suppress_trace;
            b->suppress_trace = true;
            bool valid = behavior_lower_state_inputs(b, n);
            b->suppress_trace = suppressed;
            if (!valid) {
                return -1;
            }
        }
        int index = behavior_lower_local(b, b->frame, n, pin);
        if (index >= 0) {
            result = behavior_lower_source(b, BF_LOCAL, index, &n->output_pins[pin], n, pin);
        }
    } else if (n->desc->lower) {
        b->function->custom_lowering = true;
        result = n->desc->lower(b, n, pin);
    } else if (n->desc->component_kind) {
        result = behavior_lower_component_read(b, n);
    } else if (n->desc->pure_kind == NODE_PURE_SELF && type == PIN_HANDLE) {
        result = behavior_lower_owner(b);
    } else if (n->desc->pure_kind == NODE_PURE_FUNCTION) {
        result = pure_call(b, n, pin);
    } else if (n->desc->pure_kind == NODE_PURE_GET) {
        int index = behavior_lower_variable(
            b,
            n->reference_id,
            type,
            n->output_pins[pin].container
        );
        if (index >= 0) {
            result = behavior_lower_source(b, BF_VARIABLE, index, &n->output_pins[pin], n, pin);
        }
    } else if (n->desc->pure_kind == NODE_PURE_DELTA && type == PIN_F32) {
        result = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_DELTA, .type = type }
        );
        result = behavior_lower_named_constant(b, result, NULL);
    } else if (n->desc->pure_kind == NODE_PURE_MAKE) {
        result = behavior_lower_make(b, n);
    } else if (n->desc->pure_kind == NODE_PURE_BREAK) {
        int aggregate = behavior_lower_input(b, n, 0);
        if (aggregate >= 0 && pin < n->desc->type->field_count) {
            result = behavior_lower_field(b, aggregate, &n->desc->type->fields[pin], n, 0);
        }
    } else if (n->desc->pure_kind == NODE_PURE_COPY && n->input_count == 1) {
        result = behavior_lower_input(b, n, 0);
    } else if (n->desc->display_class == NODE_DISPLAY_LITERAL && n->input_count == 1) {
        result = behavior_lower_input(b, n, 0);
    } else if (
        n->desc->pure_kind == NODE_PURE_CONST && !n->desc->flow &&
        n->output_pins[pin].default_value.type == type
    ) {
        result = behavior_lower_source(b, BF_CONSTANT, 0, &n->output_pins[pin], n, pin);
    } else if (conversion_is_vector_splat(n->desc)) {
        result = vector_splat(b, behavior_lower_input(b, n, 0), n->desc);
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
        if (n->desc == &NODE_Modulo) {
            op = BF_MOD;
        }
        if (n->desc == &NODE_Has_Any_Flags) {
            op = BF_FLAGS;
        }
        if (n->desc == &NODE_And || n->desc == &NODE_Or || n->desc == &NODE_Not) {
            int condition = behavior_lower_input(b, n, 0);
            int yes = -1;
            int no = -1;
            if (n->desc == &NODE_And) {
                yes = behavior_lower_input(b, n, 1);
                no = behavior_lower_value(
                    b,
                    (behavior_function_instruction){ .op = BF_CONSTANT,
                                                     .type = PIN_BOOL,
                                                     .value = { .type = PIN_BOOL } }
                );
            } else if (n->desc == &NODE_Or) {
                yes = behavior_lower_value(
                    b,
                    (behavior_function_instruction){
                        .op = BF_CONSTANT,
                        .type = PIN_BOOL,
                        .value = { .type = PIN_BOOL, .b = true } }
                );
                no = behavior_lower_input(b, n, 1);
            } else {
                yes = behavior_lower_value(
                    b,
                    (behavior_function_instruction){ .op = BF_CONSTANT,
                                                     .type = PIN_BOOL,
                                                     .value = { .type = PIN_BOOL } }
                );
                no = behavior_lower_value(
                    b,
                    (behavior_function_instruction){
                        .op = BF_CONSTANT,
                        .type = PIN_BOOL,
                        .value = { .type = PIN_BOOL, .b = true } }
                );
            }
            if (condition >= 0 && yes >= 0 && no >= 0 &&
                b->function->slots[condition] == PIN_BOOL &&
                b->function->slots[yes] == PIN_BOOL &&
                b->function->slots[no] == PIN_BOOL) {
                result = behavior_lower_value(
                    b,
                    (behavior_function_instruction){ .op = BF_SELECT,
                                                     .type = PIN_BOOL,
                                                     .left = condition,
                                                     .right = yes,
                                                     .target = no }
                );
                result = behavior_lower_named_constant(b, result, NULL);
            }
        }
        bool numeric = (op >= BF_ADD && op <= BF_MOD) || op == BF_MIN || op == BF_MAX;
        if (numeric && n->input_count == 2) {
            int left = behavior_lower_input(b, n, 0),
                right = behavior_lower_input(b, n, 1);
            if (left >= 0 && right >= 0) {
                result = aggregate_binary(b, op, left, right, n);
            }
        }
        if (!numeric && op >= 0 && n->input_count >= 1) {
            int left = behavior_lower_input(b, n, 0), right = left;
            if (op != BF_NEG && op != BF_ABS && !(op >= BF_SIN && op <= BF_ROUND)) {
                right = n->input_count == 2 ? behavior_lower_input(b, n, 1) : -1;
            }
            if (left >= 0 && right >= 0 &&
                behavior_lower_scalar(b->function->slots[left]) &&
                b->function->slots[left] != PIN_BOOL &&
                b->function->slots[left] != PIN_HANDLE &&
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
                           : behavior_lower_value(b, i);
                result = op == BF_NEG || op == BF_ABS
                           ? behavior_lower_inherit_name(b, result, left)
                           : behavior_lower_named_constant(b, result, NULL);
            }
        }
        if (n->desc->jit_kind == NODE_JIT_CLAMP && n->input_count == 3 &&
            (type == PIN_F32 || type == PIN_F64 || type == PIN_I32 || type == PIN_I64)) {
            /* A float operand of a double clamp widens first. */
            int x = behavior_lower_convert(b, behavior_lower_input(b, n, 0), type),
                low = behavior_lower_convert(b, behavior_lower_input(b, n, 1), type),
                high = behavior_lower_convert(b, behavior_lower_input(b, n, 2), type);
            if (x >= 0 && low >= 0 && high >= 0 && b->function->slots[x] == type &&
                b->function->slots[low] == type && b->function->slots[high] == type) {
                /* The language defines Max(Min(value, high), low), including
                 * NaNs, signed zero and reversed bounds. */
                int bounded = behavior_lower_value(
                    b,
                    (
                        behavior_function_instruction
                    ){ .op = BF_MIN, .type = type, .left = x, .right = high }
                );
                result = behavior_lower_value(
                    b,
                    (
                        behavior_function_instruction
                    ){ .op = BF_MAX, .type = type, .left = bounded, .right = low }
                );
                result = behavior_lower_inherit_name(b, result, x);
            }
        }
    }
    if (n->desc->jit_kind == NODE_JIT_V2_NORMALIZE ||
        n->desc->jit_kind == NODE_JIT_V2_DIRECTION) {
        int source = behavior_lower_input(b, n, 0);
        if (n->desc->jit_kind == NODE_JIT_V2_DIRECTION) {
            int target = behavior_lower_input(b, n, 1);
            aggregate *origin = behavior_lower_aggregate(b, source),
                      *destination = behavior_lower_aggregate(b, target);
            if (origin && destination) {
                behavior_lower_require(b, origin, n, 0);
                behavior_lower_require(b, destination, n, 1);
            }
            source = !origin || !destination
                       ? -1
                       : aggregate_binary(b, BF_SUB, target, source, n);
        }
        if (source >= 0) {
            result = normalize_aggregate(b, n, source);
        }
    }
    if (n->desc->jit_kind == NODE_JIT_V2_FROM_ANGLE) {
        result = from_angle(b, n);
    }
    if (n->desc->jit_kind == NODE_JIT_UNWRAP) {
        int source = behavior_lower_input(b, n, 0);
        aggregate *a = behavior_lower_aggregate(b, source);
        if (a && a->type->field_count == 1) {
            result = behavior_lower_field(b, source, &a->type->fields[0], n, 0);
        }
    }
    if (n->desc->jit_kind == NODE_JIT_IDENTITY) {
        result = behavior_lower_identity(b, behavior_lower_input(b, n, 0), n->desc, n, 0, false);
    }
    if (n->desc->jit_kind == NODE_JIT_V2_LENGTH || n->desc->jit_kind == NODE_JIT_V2_DOT ||
        n->desc->jit_kind == NODE_JIT_V2_DISTANCE) {
        result = vector_scalar(b, n);
    }
    if (n->desc == &NODE_Step || n->desc == &NODE_Mix) {
        result = step_or_mix(b, n);
    }
    if (n->desc == &NODE_To_Name && n->input_count == 1) {
        result = to_name(b, behavior_lower_input(b, n, 0));
    }
    if (n->input_count == 1 &&
        (n->desc == &NODE_To_Real || n->desc == &NODE_To_Byte ||
         n->desc == &NODE_To_Integer || n->desc == &NODE_To_Integer64)) {
        result = numeric_cast(b, behavior_lower_input(b, n, 0), n->desc, type);
    }
    if (result < 0) {
        result = n->desc->host_call ? behavior_lower_host_call(b, n, pin)
                                    : behavior_lower_motion(b, n, pin);
    }
    if (result < 0 && n->desc->query) {
        result = behavior_lower_query(b, n);
    }
    if (result < 0) {
        result = behavior_lower_random(b, n, pin);
    }
    if (result < 0) {
        result = behavior_lower_array_operation(b, n);
    }
    apop(b->evaluating);
    if (result < 0) {
        return behavior_lower_unsupported(b, n, pin, true, "data operation");
    }
    array_value *array = behavior_lower_array(b, result);
    if ((array != NULL) != (n->output_pins[pin].container >= PIN_CONTAINER_ARRAY) ||
        (array && array->container != n->output_pins[pin].container)) {
        return -1;
    }
    if (!array && result >= 0 && b->function->slots[result] != type &&
        !(type == PIN_F32 && b->function->slots[result] == PIN_F64)) {
        return -1;
    }
    if (result >= 0 && n->desc->pure_kind != NODE_PURE_FUNCTION) {
        apush(b->outputs, ((output){ n, pin, result }));
    }
    return result;
}
int behavior_lower_output(builder *b, node *n, int pin)
{
    bool seen = false;
    for (int i = 0; i < acount(b->traced); i++) {
        seen |= b->traced[i] == n;
    }
    bool suppressed = b->suppress_trace;
    if (b->recording && !b->capture_outputs && !seen) {
        if (n->desc->flow) {
            behavior_lower_trace_dependencies(b, n);
        } else {
            apush(b->traced, n);
        }
    }
    b->suppress_trace |= seen || b->capture_outputs;
    int value = output_value(b, n, pin);
    b->suppress_trace = suppressed;
    return value;
}
int behavior_lower_apply_conversion(builder *b, int slot, const node_desc *conversion, node *n, int pin)
{
    if (!conversion || slot < 0) {
        return slot;
    }
    if (conversion_is_vector_splat(conversion)) {
        return vector_splat(b, slot, conversion);
    }
    if (conversion == &NODE_To_Name) {
        return to_name(b, slot);
    }
    aggregate *a = behavior_lower_aggregate(b, slot);
    if (conversion->jit_kind == NODE_JIT_IDENTITY) {
        int converted = behavior_lower_identity(b, slot, conversion, n, pin, true);
        if (converted >= 0) {
            return converted;
        }
    }
    if (conversion->jit_kind == NODE_JIT_UNWRAP && a && a->type->field_count == 1) {
        return behavior_lower_field(b, slot, &a->type->fields[0], n, pin);
    }
    if (conversion->output_count == 1) {
        int converted = numeric_cast(b, slot, conversion, conversion->outputs[0].type);
        if (converted >= 0) {
            return converted;
        }
    }
    return behavior_lower_unsupported(b, n, pin, false, "input conversion");
}

static int behavior_lower_event_output(
    builder *b,
    node *n,
    int pin,
    const pin_binding *binding,
    const pin_spec *target
)
{
    if (!b->root_entry || b->root_entry->id != binding->event_node_id) {
        return behavior_lower_unsupported(b, n, pin, false, "event output belongs to another event");
    }
    int output = 0;
    int source_pin = -1;
    const pin_spec *source = graph_pin_by_id(b->root_entry, binding->event_pin_id, &output, &source_pin);
    if (!source || !output || source->kind != PIN_DATA) {
        return behavior_lower_unsupported(b, n, pin, false, "event output binding");
    }
    int aggregate_pin = source_pin;
    const type_field *field = graph_output_field(b->root_entry, source_pin, &aggregate_pin);
    if (source->field_source && !field) {
        return behavior_lower_unsupported(b, n, pin, false, "event output field");
    }
    const pin_spec *argument = &b->root_entry->output_pins[aggregate_pin];
    if (argument->kind != PIN_DATA || argument->field_source) {
        return behavior_lower_unsupported(b, n, pin, false, "event output aggregate");
    }
    int ordinal = 0;
    for (int i = 0; i < aggregate_pin; i++) {
        ordinal += b->root_entry->output_pins[i].kind == PIN_DATA &&
                   !b->root_entry->output_pins[i].field_source;
    }
    int value = behavior_lower_source(b, BF_ARGUMENT, ordinal, argument, n, pin);
    if (value < 0) {
        return value;
    }
    if (field) {
        value = behavior_lower_field(b, value, field, n, pin);
    }
    pin_spec actual = field ? (pin_spec){ .type = field->type,
                                          .type_name = field->type_name,
                                          .container = PIN_CONTAINER_SINGLE,
                                          .kind = PIN_DATA }
                            : *argument;
    if (!pin_compatible(&actual, target)) {
        const node_desc *conversion = conversion_for_pins(&actual, target);
        if (!conversion) {
            return behavior_lower_unsupported(b, n, pin, false, "event output binding type");
        }
        value = behavior_lower_apply_conversion(b, value, conversion, n, pin);
    }
    return behavior_lower_coerce(b, value, target, true);
}

static int input_value(builder *b, node *n, int pin)
{
    const pin_spec *s = &n->input_pins[pin];
    if ((!behavior_lower_scalar(s->type) && !behavior_scalar_string(s->type) &&
         s->type != PIN_STRUCT && s->container < PIN_CONTAINER_ARRAY) ||
        s->container > PIN_CONTAINER_SET || s->kind != PIN_DATA) {
        return behavior_lower_unsupported(b, n, pin, false, "input type or container");
    }
    node *source;
    int source_pin;
    if (graph_input_source(b->graph, n, pin, &source, &source_pin)) {
        behavior_lower_trace(b, source, source_pin, n, pin, GRAPH_EDGE_DATA);
        int slot = behavior_lower_output(b, source, source_pin);
        const array_value *array = behavior_lower_array(b, slot);
        if ((array != NULL) != (s->container >= PIN_CONTAINER_ARRAY) ||
            (array && array->container != s->container)) {
            return -1;
        }
        slot = behavior_lower_apply_conversion(b, slot, graph_input_conversion(b->graph, n, pin), n, pin);
        return behavior_lower_coerce(b, slot, s, false);
    }
    if (!s->binding) {
        if ((s->flags & PIN_FLAG_DEFAULT_SELF) && !s->default_value.u64 &&
            s->type == PIN_HANDLE) {
            return behavior_lower_coerce(b, behavior_lower_owner(b), s, true);
        }
        if (s->default_value.type != s->type) {
            return -1;
        }
        return behavior_lower_source(b, BF_CONSTANT, 0, s, n, pin);
    }
    const pin_binding *binding = s->binding;
    if (binding->kind == PIN_BINDING_COMPONENT ||
        binding->kind == PIN_BINDING_COMPONENT_FIELD) {
        return behavior_lower_coerce(b, behavior_lower_component_binding(b, n, pin), s, true);
    }
    if (binding->kind == PIN_BINDING_VARIABLE &&
        (!binding->source_asset || !strcmp(binding->source_asset, b->asset->name))) {
        const behavior_variable
            *variable = behavior_asset_find_variable_by_id(b->asset, binding->variable_id);
        if (variable && variable->container == s->container) {
            int index = (int)(variable - b->asset->variables);
            pin_spec source = *s;
            source.type = variable->type;
            source.type_name = variable->type_name;
            int value = behavior_lower_source(b, BF_VARIABLE, index, &source, n, pin);
            if (!pin_compatible(&source, s)) {
                const node_desc *conversion = conversion_for_pins(&source, s);
                if (!conversion) {
                    return behavior_lower_unsupported(b, n, pin, false, "variable binding type");
                }
                value = behavior_lower_apply_conversion(b, value, conversion, n, pin);
            }
            return behavior_lower_coerce(b, value, s, true);
        }
    }
    /* Delta time is a float; a double input widens it. */
    if (binding->kind == PIN_BINDING_DELTA_TIME &&
        (s->type == PIN_F32 || s->type == PIN_F64)) {
        int delta = behavior_lower_named_constant(
            b,
            behavior_lower_value(b, (behavior_function_instruction){ .op = BF_DELTA, .type = PIN_F32 }),
            s->type == PIN_F32 ? s->type_name : "float"
        );
        return behavior_lower_coerce(b, delta, s, true);
    }
    if (binding->kind == PIN_BINDING_EVENT_OUTPUT) {
        return behavior_lower_event_output(b, n, pin, binding, s);
    }
    return behavior_lower_unsupported(b, n, pin, false, "input binding");
}

int behavior_lower_input(builder *b, node *n, int pin)
{
    int slot = input_value(b, n, pin);
    if (b->recording && slot >= 0) {
        apush(b->inputs, ((output){ n, pin, slot }));
        node *source;
        int source_pin;
        if (n->desc->flow &&
            graph_input_source(b->graph, n, pin, &source, &source_pin) &&
            (source->output_pins[source_pin].flags & PIN_FLAG_WATCH)) {
            behavior_lower_watch(b, source, source_pin, true, slot);
        }
    }
    return slot;
}
