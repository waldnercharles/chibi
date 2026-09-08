#include "graph/model/behavior_asset.h"
#include "graph/shader/shader_graph.h"
#include "support/array.h"

#include <stdio.h>
#include <string.h>

/* Each call gets an independent lowering scope; argument values are caller IR indices. */
typedef struct scope
{
    const graph *source;
    struct scope *parent;
    int *arguments;
    const node **visiting;
    struct
    {
        uint64_t node;
        int pin, value;
    } *cache;
} scope;
typedef struct
{
    shader_graph *ir;
    shader_error *error;
    int depth;
} lowering;
static int error(lowering *l, uint64_t id, const char *message)
{
    if (l->error) {
        l->error->node = id;
        snprintf(l->error->message, sizeof l->error->message, "%s", message);
    }
    return -1;
}
static int width(const pin_spec *pin)
{
    if (pin->kind != PIN_DATA || pin->container != PIN_CONTAINER_SINGLE) {
        return 0;
    }
    if (pin->type == PIN_F32 || pin->type == PIN_F64 || pin->type == PIN_I32 ||
        pin->type == PIN_BOOL) {
        return 1;
    }
    if (pin->type != PIN_STRUCT || !pin->type_name) {
        return 0;
    }
    if (!strcmp(pin->type_name, "ShaderVec2") || !strcmp(pin->type_name, "v2") ||
        !strcmp(pin->type_name, "vec2") || !strcmp(pin->type_name, "CF_V2")) {
        return 2;
    }
    if (!strcmp(pin->type_name, "ShaderVec3")) {
        return 3;
    }
    if (!strcmp(pin->type_name, "ShaderVec4")) {
        return 4;
    }
    return 0;
}
static int literal(lowering *l, const pin_spec *pin, uint64_t id)
{
    int w = width(pin);
    if (!w) {
        return error(l, id, "Shader pins require scalar or vector values");
    }
    int index = shader_graph_add(l->ir, SHADER_CONSTANT, w);
    shader_node *n = &l->ir->nodes[index];
    n->source_id = id;
    const pin_value *v = &pin->default_value;
    if (w > 1) {
        if (v->ptr) {
            memcpy(n->value, v->ptr, (size_t)w * sizeof(float));
        }
        return index;
    }
    switch (pin->type) {
        case PIN_F32: n->value[0] = v->f; break;
        case PIN_F64: n->value[0] = (float)v->d; break;
        case PIN_I32: n->value[0] = (float)v->i; break;
        case PIN_BOOL: n->value[0] = v->b; break;
        default: break;
    }
    return index;
}
static int output(lowering *l, scope *s, const node *n, int pin);
static int input(lowering *l, scope *s, const node *n, int pin)
{
    node *from = NULL;
    int from_pin = 0;
    if (graph_input_source(s->source, n, pin, &from, &from_pin)) {
        if (graph_input_conversion(s->source, n, pin)) {
            return error(l, n->id, "Use shader math or vector nodes for GPU conversions");
        }
        return output(l, s, from, from_pin);
    }
    return literal(l, &n->input_pins[pin], n->id);
}
static int call(lowering *l, scope *s, const node *n, int pin)
{
    if (!s->source->owner) {
        return error(l, n->id, "Shader function has no owning asset");
    }
    graph *function = behavior_asset_find_graph(s->source->owner, n->reference_id);
    if (!function || function->kind != GRAPH_FUNCTION) {
        return error(l, n->id, "Missing shader function");
    }
    for (scope *p = s; p; p = p->parent) {
        if (p->source == function) {
            return error(l, n->id, "Recursive shader functions are not supported");
        }
    }
    node *ret = graph_node_by_id(function, function->return_id);
    if (!ret) {
        return error(l, n->id, "Shader function needs a Return node");
    }
    int result_pin = -1, count = 0;
    for (int i = 0; i < ret->input_count; i++) {
        if (ret->input_pins[i].kind == PIN_DATA) {
            if (count++ == pin) {
                result_pin = i;
            }
        }
    }
    if (result_pin < 0) {
        return error(l, n->id, "Missing shader function result");
    }
    scope child = { .source = function, .parent = s };
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        int value = input(l, s, n, i);
        if (value < 0) {
            afree(child.arguments);
            return -1;
        }
        apush(child.arguments, value);
    }
    int result = input(l, &child, ret, result_pin);
    afree(child.arguments);
    afree(child.cache);
    afree(child.visiting);
    return result;
}
static int operation(const node *n)
{
    if (!strncmp(n->desc->name, "Shader ", 7)) {
        for (int i = 0; i < SHADER_OP_COUNT; i++) {
            if (!strcmp(n->desc->name + 7, shader_op_name(i))) {
                return i;
            }
        }
    }
    /* Existing scalar arithmetic retains the same authored spelling in functions. */
    if (n->desc == &NODE_Add) {
        return SHADER_ADD;
    }
    if (n->desc == &NODE_Subtract) {
        return SHADER_SUBTRACT;
    }
    if (n->desc == &NODE_Multiply) {
        return SHADER_MULTIPLY;
    }
    if (n->desc == &NODE_Divide) {
        return SHADER_DIVIDE;
    }
    if (n->desc == &NODE_Min) {
        return SHADER_MIN;
    }
    if (n->desc == &NODE_Max) {
        return SHADER_MAX;
    }
    if (n->desc == &NODE_Abs) {
        return SHADER_ABS;
    }
    return -1;
}
static int lower_node(lowering *l, scope *s, const node *n, int pin)
{
    if (n->disabled) {
        return error(l, n->id, "Disabled node cannot supply shader data");
    }
    if (pin < 0 || pin >= n->output_count) {
        return error(l, n->id, "Invalid shader output pin");
    }
    if (n->desc == &NODE_CallPureFunction) {
        return call(l, s, n, pin);
    }
    if (n->desc == &NODE_FunctionEntry) {
        int ordinal = 0;
        for (int i = 0; i < pin; i++) {
            ordinal += n->output_pins[i].kind == PIN_DATA;
        }
        if (ordinal >= acount(s->arguments)) {
            return error(l, n->id, "Missing shader function argument");
        }
        return s->arguments[ordinal];
    }
    if (n->desc == &NODE_RerouteData) {
        return input(l, s, n, 0);
    }
    int op = operation(n);
    if (op < 0) {
        if (n->desc->pure_kind == NODE_PURE_CONST &&
            n->desc->flow_kind == NODE_FLOW_NONE && !n->input_count) {
            return literal(l, &n->output_pins[pin], n->id);
        }
        return error(l, n->id, "Node is not supported in shader graphs");
    }
    int args[4] = { -1, -1, -1, -1 }, w = width(&n->output_pins[pin]);
    if (!w) {
        return error(l, n->id, "Unsupported shader output type");
    }
    for (int j = 0; j < shader_op_inputs(op); j++) {
        if (j >= n->input_count) {
            return error(l, n->id, "Missing shader input");
        }
        args[j] = input(l, s, n, j);
        if (args[j] < 0) {
            return -1;
        }
    }
    int index = shader_graph_add(l->ir, op, w);
    shader_node *ir = &l->ir->nodes[index];
    ir->source_id = n->id;
    memcpy(ir->input, args, sizeof args);
    if (op == SHADER_CONSTANT || op == SHADER_PARAMETER) {
        /* literal creates one node; copy its value, then drop the temporary. */
        int value = literal(l, &n->output_pins[pin], n->id);
        if (value < 0) {
            return -1;
        }
        memcpy(l->ir->nodes[index].value, l->ir->nodes[value].value, sizeof ir->value);
        asetlen(l->ir->nodes, value);
        l->ir->nodes[index].parameter = (int)n->reference_id;
    }
    return index;
}
static int output(lowering *l, scope *s, const node *n, int pin)
{
    for (int i = 0; i < acount(s->cache); i++) {
        if (s->cache[i].node == n->id && s->cache[i].pin == pin) {
            return s->cache[i].value;
        }
    }
    for (int i = 0; i < acount(s->visiting); i++) {
        if (s->visiting[i] == n) {
            return error(l, n->id, "Shader data graph contains a cycle");
        }
    }
    if (l->depth >= SHADER_NODE_MAX || acount(l->ir->nodes) >= SHADER_NODE_MAX) {
        return error(l, n->id, "Shader graph is too complex");
    }
    l->depth++;
    apush(s->visiting, n);
    int result = lower_node(l, s, n, pin);
    apop(s->visiting);
    l->depth--;
    if (result >= 0) {
        apush(s->cache, ((typeof(*s->cache)){ n->id, pin, result }));
    }
    return result;
}
bool shader_graph_from_graph(const graph *source, shader_graph *result, shader_error *err)
{
    shader_graph ir;
    shader_graph_init(&ir);
    lowering l = { .ir = &ir, .error = err };
    scope s = { .source = source };
    const node *root = NULL;
    for (int i = 0; i < acount(source->nodes); i++) {
        node *n = source->nodes[i];
        if (strcmp(n->desc->name, "Shader Material Output") &&
            strcmp(n->desc->name, "Shader Shape Output")) {
            continue;
        }
        if (root) {
            error(&l, n->id, "Shader graph must have exactly one output node");
            goto failed;
        }
        root = n;
    }
    if (!root) {
        error(&l, 0, "Shader graph needs a Material or Shape Output node");
        goto failed;
    }
    ir.output_count = root->input_count;
    if (ir.output_count > SHADER_OUTPUT_MAX) {
        error(&l, root->id, "Too many shader outputs");
        goto failed;
    }
    for (int i = 0; i < ir.output_count; i++) {
        ir.output[i] = input(&l, &s, root, i);
        if (ir.output[i] < 0) {
            goto failed;
        }
    }
    for (int i = 0; i < ir.output_count; i++) {
        int expected = ir.output_count == 1 ? 1 : 4;
        if (ir.nodes[ir.output[i]].width != expected) {
            error(&l, root->id, "Shader output has the wrong type");
            goto failed;
        }
    }
    if (!shader_graph_validate(&ir, err)) {
        goto failed;
    }
    afree(s.cache);
    afree(s.visiting);
    shader_graph_free(result);
    *result = ir;
    return true;
failed:
    afree(s.cache);
    afree(s.visiting);
    shader_graph_free(&ir);
    return false;
}
