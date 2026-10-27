#include "graph/compiler/field_program.h"

#include "graph/model/value_copy.h"
#include "graph/nodes/core_nodes.h"
#include "graph/nodes/field_nodes.h"
#include "runtime/fields/ir.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    field_ir *code;
    field_parameter *parameters;
    int output;
} field_plan;
struct call_result
{
    const node *call;
    int output, slot;
};
typedef struct compiler
{
    const behavior_asset *asset;
    const struct compiler *caller;
    const int *arguments;
    int argument_count;
    const graph *graph;
    field_plan *program;
    int *slots, *state;
    struct call_result *calls; /* one slot per call output compiled */
    int depth;
    char *error;
    size_t size;
} compiler;

static void plan_free(field_plan *p)
{
    for (int i = 0; i < acount(p->parameters); i++) {
        free((char *)p->parameters[i].name);
    }
    afree(p->parameters);
    afree(p->code);
    free(p);
}

static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}

static bool scalar(pin_value v, float *out)
{
    switch (v.type) {
        case PIN_F32: *out = v.f; break;
        case PIN_I32: *out = (float)v.i; break;
        case PIN_U32: *out = (float)v.u; break;
        default: return false;
    }
    return v.container == PIN_CONTAINER_SINGLE && isfinite(*out);
}

/* A binding other than a local: a local names a wire the graph still holds,
   so it reads exactly like that wire. */
static bool state_binding(const pin_spec *pin)
{
    return pin->binding && pin->binding->kind != PIN_BINDING_LOCAL;
}

static int instruction(compiler *c, field_ir ir, int inputs)
{
    if (acount(c->program->code) >= FIELD_INSTRUCTION_MAX) {
        fail(c->error, c->size, "Field exceeds 4096 instructions");
        return -1;
    }
    ir.spatial = ir.op == FIELD_X ? 1 : ir.op == FIELD_Y ? 2 : 0;
    ir.count = inputs;
    for (int i = 0; i < inputs; i++) {
        ir.spatial |= c->program->code[ir.inputs[i]].spatial;
    }
    int slot = acount(c->program->code);
    apush(c->program->code, ir);
    return slot;
}

static int compile_node(compiler *c, node *n, int output);

/* The value a function returns at one of its outputs. */
static int compile_return(compiler *c, node *ret, int input)
{
    node *from = NULL;
    int pin = -1;
    if (graph_input_source(c->graph, ret, input, &from, &pin)) {
        return compile_node(c, from, pin);
    }
    float value;
    if (!scalar(ret->input_pins[input].default_value, &value)) {
        fail(c->error, c->size, "Field inputs require finite scalar values");
        return -1;
    }
    return instruction(c, (field_ir){ .op = FIELD_CONSTANT, .value = value }, 0);
}

static int compile_node(compiler *c, node *n, int output)
{
    if (n->desc == &NODE_FunctionEntry && !n->disabled && c->arguments &&
        output >= 0 && output < c->argument_count) {
        return c->arguments[output];
    }
    /* A function call may return several values; every other node one. */
    bool call = n->desc == &NODE_CallPureFunction || n->desc == &NODE_BehaviorExpression;
    if (output != 0 && !call) {
        fail(c->error, c->size, "Field nodes return one scalar");
        return -1;
    }
    int index = -1;
    for (int i = 0; i < acount(c->graph->nodes); i++) {
        if (c->graph->nodes[i] == n) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        return -1;
    }
    if (c->state[index] == 2 && !call) {
        return c->slots[index];
    }
    for (int i = 0; call && i < acount(c->calls); i++) {
        if (c->calls[i].call == n && c->calls[i].output == output) {
            return c->calls[i].slot;
        }
    }
    if (c->state[index] == 1) {
        fail(c->error, c->size, "Recursive field dependency");
        return -1;
    }
    if (++c->depth > 256) {
        fail(c->error, c->size, "Field dependency depth exceeds 256");
        return -1;
    }
    c->state[index] = 1;
    int op = field_node_opcode(n->desc);
    if (n->desc == &NODE_Return && c->caller) {
        op = FIELD_OUTPUT;
    }
    if ((op < 0 && !call) || n->disabled) {
        fail(c->error, c->size, "Procedural fields accept only enabled pure field nodes");
        return -1;
    }
    field_ir ir = { .op = call ? FIELD_OUTPUT : op };
    int arguments[FIELD_CALL_ARGUMENT_MAX];
    int *inputs = call ? arguments : ir.inputs;
    if (n->input_count > (call ? FIELD_CALL_ARGUMENT_MAX : FIELD_INPUT_MAX)) {
        fail(c->error, c->size, call ? "Field functions accept at most 32 scalar arguments" : "Field nodes accept at most seven inputs");
        return -1;
    }
    if (op == FIELD_PARAMETER) {
        node *from;
        int pin;
        if (n->input_pins[0].binding ||
            graph_input_source(c->graph, n, 0, &from, &pin)) {
            fail(c->error, c->size, "Field Parameter name must be a literal");
            return -1;
        }
        const char *name = graph_text_cstring(c->graph, n->input_pins[0].default_value);
        ir.parameter = -1;
        for (int i = 0; i < acount(c->program->parameters); i++) {
            if (name && !strcmp(c->program->parameters[i].name, name)) {
                ir.parameter = i;
                break;
            }
        }
        if (ir.parameter < 0) {
            fail(c->error, c->size, "Field Parameter must name a declared scalar parameter");
            return -1;
        }
    } else {
        for (int i = 0; i < n->input_count; i++) {
            node *from = NULL;
            int pin = -1;
            if (state_binding(&n->input_pins[i])) {
                fail(c->error, c->size, "Field inputs cannot read ECS, event, or mutable state bindings");
                return -1;
            }
            if (graph_input_source(c->graph, n, i, &from, &pin)) {
                if ((inputs[i] = compile_node(c, from, pin)) < 0) {
                    return -1;
                }
            } else {
                float value;
                if (!scalar(n->input_pins[i].default_value, &value)) {
                    fail(c->error, c->size, "Field inputs require finite scalar values");
                    return -1;
                }
                inputs[i] = instruction(c, (field_ir){ .op = FIELD_CONSTANT, .value = value }, 0);
                if (inputs[i] < 0) {
                    return -1;
                }
            }
        }
    }
    if (call) {
        graph *target = behavior_asset_find_graph(c->asset, n->reference_id);
        if (!target || target->kind != GRAPH_FUNCTION || output >= n->output_count) {
            fail(c->error, c->size, "Field call requires a resolved pure function");
            return -1;
        }
        for (const compiler *parent = c; parent; parent = parent->caller) {
            if (parent->graph == target) {
                fail(c->error, c->size, "Recursive field function");
                return -1;
            }
        }
        node *entry = graph_node_by_id(target, target->entry_id),
             *ret = graph_node_by_id(target, target->return_id);
        if (!entry || !ret || entry->output_count != n->input_count ||
            ret->input_count != n->output_count) {
            fail(c->error, c->size, "Field function signature mismatch");
            return -1;
        }
        int count = acount(target->nodes);
        compiler child = { .asset = c->asset,
                           .caller = c,
                           .graph = target,
                           .program = c->program,
                           .depth = c->depth,
                           .arguments = arguments,
                           .argument_count = n->input_count,
                           .slots = xcalloc((size_t)count, sizeof(int)),
                           .state = xcalloc((size_t)count, sizeof(int)),
                           .error = c->error,
                           .size = c->size };
        int result = compile_return(&child, ret, output);
        for (int i = 0; i < count && result >= 0; i++) {
            if (target->nodes[i] != entry &&
                compile_node(&child, target->nodes[i], 0) < 0) {
                result = -1;
            }
        }
        free(child.slots);
        free(child.state);
        afree(child.calls);
        if (result < 0) {
            return -1;
        }
        apush(c->calls, ((struct call_result){ n, output, result }));
        c->state[index] = 0;
        c->depth--;
        return result;
    }
    /* The Constant node copies its authored input; literal IR has no input. */
    if (op == FIELD_CONSTANT) {
        ir.op = FIELD_OUTPUT;
    }
    c->slots[index] = instruction(c, ir, op == FIELD_PARAMETER ? 0 : n->input_count);
    c->state[index] = 2;
    c->depth--;
    return c->slots[index];
}

static bool same_instruction(const field_ir *a, const field_ir *b)
{
    bool same = a->op == b->op && a->count == b->count &&
                a->parameter == b->parameter &&
                !memcmp(&a->value, &b->value, sizeof a->value);
    for (int k = 0; same && k < a->count; k++) {
        same = a->inputs[k] == b->inputs[k];
    }
    return same;
}

/* Copies read through to their source, repeated instructions merge, and
   instructions nothing reads drop out: a function's Return, a Constant node,
   a stage two calls both compute, and nodes the output never reads cost
   nothing extra per cell. */
static void prune(field_plan *p)
{
    int count = acount(p->code);
    int *index = xmalloc((size_t)count * sizeof *index);
    int *same = xmalloc((size_t)count * sizeof *same);
    bool *live = xcalloc((size_t)count, sizeof *live);
    for (int i = 0; i < count; i++) {
        field_ir *ir = &p->code[i];
        for (int k = 0; k < ir->count; k++) {
            int from = ir->inputs[k];
            while (p->code[from].op == FIELD_OUTPUT) {
                from = p->code[from].inputs[0];
            }
            ir->inputs[k] = same[from];
        }
        same[i] = i;
        for (int j = 0; j < i && ir->op != FIELD_OUTPUT; j++) {
            if (same[j] == j && same_instruction(&p->code[j], ir)) {
                same[i] = j;
                break;
            }
        }
    }
    int output = p->output;
    while (p->code[output].op == FIELD_OUTPUT) {
        output = p->code[output].inputs[0];
    }
    output = same[output];
    live[output] = true;
    for (int i = count - 1; i >= 0; i--) {
        for (int k = 0; live[i] && k < p->code[i].count; k++) {
            live[p->code[i].inputs[k]] = true;
        }
    }
    int kept = 0;
    for (int i = 0; i < count; i++) {
        if (!live[i]) {
            continue;
        }
        field_ir ir = p->code[i];
        for (int k = 0; k < FIELD_INPUT_MAX; k++) {
            ir.inputs[k] = k < ir.count ? index[ir.inputs[k]] : 0;
        }
        index[i] = kept;
        p->code[kept++] = ir;
    }
    asetlen(p->code, kept);
    p->output = index[output];
    free(index);
    free(same);
    free(live);
}

field_program *field_program_compile(const behavior_asset *asset, char *error, size_t size)
{
    if (size) {
        error[0] = 0;
    }
    if (!asset || asset->domain != BEHAVIOR_DOMAIN_FIELD || acount(asset->graphs) < 1 ||
        (asset->owner_type_name && asset->owner_type_name[0])) {
        fail(error, size, "ProceduralField requires a pure graph without an entity owner");
        return NULL;
    }
    for (int g = 0; g < acount(asset->graphs); g++) {
        const graph *graph = asset->graphs[g];
        if (g && graph->kind != GRAPH_FUNCTION) {
            fail(error, size, "Fields contain pure functions only");
            return NULL;
        }
        for (int i = 0; i < acount(graph->nodes); i++) {
            const node *n = graph->nodes[i];
            bool signature = n->desc == &NODE_FunctionEntry ||
                             n->desc == &NODE_Return ||
                             n->desc == &NODE_CallPureFunction ||
                             n->desc == &NODE_BehaviorExpression;
            if (n->disabled || (field_node_opcode(n->desc) < 0 && !signature) ||
                (g && field_node_opcode(n->desc) == FIELD_OUTPUT) ||
                (!g && (n->desc == &NODE_FunctionEntry || n->desc == &NODE_Return))) {
                fail(error, size, "Procedural fields accept only enabled pure field nodes and scalar functions");
                return NULL;
            }
            if (signature) {
                for (int side = 0; side < 2; side++) {
                    const pin_spec *pins = side ? n->output_pins : n->input_pins;
                    int count = side ? n->output_count : n->input_count;
                    for (int p = 0; p < count; p++) {
                        if (pins[p].kind != PIN_DATA || pins[p].type != PIN_F32 ||
                            pins[p].container != PIN_CONTAINER_SINGLE ||
                            state_binding(&pins[p])) {
                            fail(error, size, "Field functions require pure scalar pins");
                            return NULL;
                        }
                    }
                }
            }
        }
        for (int i = 0; i < acount(graph->edges); i++) {
            if (graph->edges[i].kind != GRAPH_EDGE_DATA || graph->edges[i].conversion) {
                fail(error, size, "Field graphs cannot contain execution or conversion edges");
                return NULL;
            }
        }
    }
    field_plan *p = xcalloc(1, sizeof *p);
    for (int i = 0; i < acount(asset->variables); i++) {
        behavior_variable v = asset->variables[i];
        float value;
        if (!scalar(v.default_value, &value)) {
            fail(error, size, "Field parameters must be finite number, int, or u32 scalars");
            plan_free(p);
            return NULL;
        }
        apush(p->parameters, ((field_parameter){ xstrdup(v.name), v.default_value }));
    }
    int count = acount(asset->graph->nodes);
    compiler c = { .asset = asset,
                   .graph = asset->graph,
                   .program = p,
                   .slots = xcalloc((size_t)count, sizeof(int)),
                   .state = xcalloc((size_t)count, sizeof(int)),
                   .error = error,
                   .size = size };
    int outputs = 0;
    bool valid = count > 0 && count <= FIELD_INSTRUCTION_MAX;
    for (int i = 0; i < count && valid; i++) {
        node *n = asset->graph->nodes[i];
        int slot = compile_node(&c, n, 0);
        valid = slot >= 0;
        if (field_node_opcode(n->desc) == FIELD_OUTPUT) {
            outputs++;
            p->output = slot;
        }
    }
    for (int i = 0; i < acount(asset->graph->edges) && valid; i++) {
        const edge *e = &asset->graph->edges[i];
        valid = e->kind == GRAPH_EDGE_DATA && !e->conversion;
        if (!valid) {
            fail(error, size, "Field graphs cannot contain execution or conversion edges");
        }
    }
    free(c.slots);
    free(c.state);
    afree(c.calls);
    if (!valid || outputs != 1) {
        if (size && !error[0]) {
            fail(error, size, "Field requires exactly one Field Output");
        }
        plan_free(p);
        return NULL;
    }
    prune(p);
    field_program *result = field_program_new(
        p->code,
        acount(p->code),
        p->output,
        p->parameters,
        acount(p->parameters),
        error,
        size
    );
    plan_free(p);
    return result;
}
