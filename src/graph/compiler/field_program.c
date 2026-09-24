#include "graph/compiler/field_program.h"

#include "graph/compiler/field_ir.h"
#include "graph/nodes/field_nodes.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <float.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *name;
    pin_value value;
} parameter;
struct field_program
{
    field_ir *code;
    parameter *parameters;
    int output;
};
typedef struct compiler
{
    const behavior_asset *asset;
    const struct compiler *caller;
    const int *arguments;
    int argument_count;
    const graph *graph;
    field_program *program;
    int *slots, *state;
    int depth;
    char *error;
    size_t size;
} compiler;

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

static int instruction(compiler *c, field_ir ir, int inputs)
{
    if (acount(c->program->code) >= FIELD_INSTRUCTION_MAX) {
        fail(c->error, c->size, "Field exceeds 4096 instructions");
        return -1;
    }
    ir.spatial = ir.op == FIELD_X ? 1 : ir.op == FIELD_Y ? 2 : 0;
    for (int i = 0; i < inputs; i++) {
        ir.spatial |= c->program->code[ir.inputs[i]].spatial;
    }
    int slot = acount(c->program->code);
    apush(c->program->code, ir);
    return slot;
}

static int compile_node(compiler *c, node *n, int output)
{
    if (n->desc == &NODE_FunctionEntry && !n->disabled && c->arguments &&
        output >= 0 && output < c->argument_count) {
        return c->arguments[output];
    }
    if (output != 0) {
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
    if (c->state[index] == 2) {
        return c->slots[index];
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
    bool call = n->desc == &NODE_CallPureFunction;
    if (n->desc == &NODE_Return && c->caller) {
        op = FIELD_OUTPUT;
    }
    if ((op < 0 && !call) || n->disabled) {
        fail(c->error, c->size, "Procedural fields accept only enabled pure field nodes");
        return -1;
    }
    field_ir ir = { .op = call ? FIELD_OUTPUT : op };
    if (n->input_count > 7) {
        fail(c->error, c->size, "Field functions accept at most seven scalar arguments");
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
        const char *name = n->input_pins[0].default_value.str;
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
            if (n->input_pins[i].binding) {
                fail(c->error, c->size, "Field inputs cannot read ECS, event, or mutable state bindings");
                return -1;
            }
            if (graph_input_source(c->graph, n, i, &from, &pin)) {
                if ((ir.inputs[i] = compile_node(c, from, pin)) < 0) {
                    return -1;
                }
            } else {
                float value;
                if (!scalar(n->input_pins[i].default_value, &value)) {
                    fail(c->error, c->size, "Field inputs require finite scalar values");
                    return -1;
                }
                ir.inputs[i] = instruction(c, (field_ir){ .op = FIELD_CONSTANT, .value = value }, 0);
                if (ir.inputs[i] < 0) {
                    return -1;
                }
            }
        }
    }
    if (call) {
        graph *target = behavior_asset_find_graph(c->asset, n->reference_id);
        if (!target || target->kind != GRAPH_FUNCTION || n->output_count != 1) {
            fail(c->error, c->size, "Field call requires a resolved pure function returning one scalar");
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
            ret->input_count != 1) {
            fail(c->error, c->size, "Field function signature mismatch");
            return -1;
        }
        int count = acount(target->nodes);
        compiler child = { .asset = c->asset,
                           .caller = c,
                           .graph = target,
                           .program = c->program,
                           .depth = c->depth,
                           .arguments = ir.inputs,
                           .argument_count = n->input_count,
                           .slots = xcalloc((size_t)count, sizeof(int)),
                           .state = xcalloc((size_t)count, sizeof(int)),
                           .error = c->error,
                           .size = c->size };
        int result = compile_node(&child, ret, 0);
        for (int i = 0; i < count && result >= 0; i++) {
            if (target->nodes[i] != entry &&
                compile_node(&child, target->nodes[i], 0) < 0) {
                result = -1;
            }
        }
        free(child.slots);
        free(child.state);
        if (result < 0) {
            return -1;
        }
        c->slots[index] = result;
        c->state[index] = 2;
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
                             n->desc == &NODE_CallPureFunction;
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
                            pins[p].binding) {
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
    field_program *p = xcalloc(1, sizeof *p);
    for (int i = 0; i < acount(asset->variables); i++) {
        behavior_variable v = asset->variables[i];
        float value;
        if (!scalar(v.default_value, &value)) {
            fail(error, size, "Field parameters must be finite float, int, or uint scalars");
            field_program_free(p);
            return NULL;
        }
        apush(p->parameters, ((parameter){ xstrdup(v.name), v.default_value }));
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
    if (!valid || outputs != 1) {
        if (size && !error[0]) {
            fail(error, size, "Field requires exactly one Field Output");
        }
        field_program_free(p);
        return NULL;
    }
    return p;
}

void field_program_free(field_program *p)
{
    if (!p) {
        return;
    }
    for (int i = 0; i < acount(p->parameters); i++) {
        free(p->parameters[i].name);
    }
    afree(p->parameters);
    afree(p->code);
    free(p);
}

int field_program_instruction_count(const field_program *p)
{
    return acount(p->code);
}

static float hash_sample(unsigned seed, int x, int y)
{
    unsigned hash = seed ^ (unsigned)x * 0x9e3779b9u ^ (unsigned)y * 0x85ebca6bu;
    hash = (hash ^ (hash >> 16)) * 0x7feb352du;
    hash = (hash ^ (hash >> 15)) * 0x846ca68bu;
    return ((hash ^ (hash >> 16)) & 65535) / 32767.5f - 1;
}
static float mix(float a, float b, float t)
{
    return a + (b - a) * t;
}
static float noise(unsigned seed, float x, float y)
{
    if (!isfinite(x) || !isfinite(y) || fabsf(x) >= 2147483520.f ||
        fabsf(y) >= 2147483520.f) {
        return NAN;
    }
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float tx = x - ix, ty = y - iy;
    tx = tx * tx * (3 - 2 * tx);
    ty = ty * ty * (3 - 2 * ty);
    return mix(
        mix(hash_sample(seed, ix, iy), hash_sample(seed, ix + 1, iy), tx),
        mix(hash_sample(seed, ix, iy + 1), hash_sample(seed, ix + 1, iy + 1), tx),
        ty
    );
}
static float clamp(float x, float lo, float hi)
{
    return lo <= hi ? fminf(hi, fmaxf(lo, x)) : NAN;
}
static float smooth_min(float a, float b, float radius)
{
    if (radius <= 0) {
        return fminf(a, b);
    }
    float h = fmaxf(radius - fabsf(a - b), 0) / radius;
    return fminf(a, b) - h * h * radius * .25f;
}

static float evaluate(field_ir ir, const float *v, float x, float y, unsigned seed, const float *parameters)
{
    float a = v[ir.inputs[0]], b = v[ir.inputs[1]], c = v[ir.inputs[2]],
          d = v[ir.inputs[3]];
    float e = v[ir.inputs[4]], f = v[ir.inputs[5]], g = v[ir.inputs[6]];
    switch (ir.op) {
        case FIELD_CONSTANT: return ir.value;
        case FIELD_X: return x;
        case FIELD_Y: return y;
        case FIELD_PARAMETER: return parameters[ir.parameter];
        case FIELD_ADD: return a + b;
        case FIELD_SUBTRACT: return a - b;
        case FIELD_MULTIPLY: return a * b;
        case FIELD_DIVIDE: return b != 0 ? a / b : NAN;
        case FIELD_MIN: return fminf(a, b);
        case FIELD_MAX: return fmaxf(a, b);
        case FIELD_ABS: return fabsf(a);
        case FIELD_NEGATE: return -a;
        case FIELD_SIN: return sinf(a);
        case FIELD_COS: return cosf(a);
        case FIELD_FLOOR: return floorf(a);
        case FIELD_CLAMP: return clamp(a, b, c);
        case FIELD_REMAP: return b != c ? mix(d, e, (a - b) / (c - b)) : NAN;
        case FIELD_HASH:
        case FIELD_NOISE:
        case FIELD_FRACTAL: {
            if (fabsf(a) >= 2147483520.f || fabsf(b) >= 2147483520.f ||
                fabsf(c) >= 2147483520.f) {
                return NAN;
            }
            unsigned offset = seed + (unsigned)(int)c;
            if (ir.op == FIELD_HASH) {
                return hash_sample(offset, (int)floorf(a), (int)floorf(b));
            }
            if (ir.op == FIELD_NOISE) {
                return noise(offset, a, b);
            }
            if (d < 1 || d > 16 || floorf(d) != d || e <= 0 || f < 0 || f > 1) {
                return NAN;
            }
            float total = 0, amplitude = 1, weight = 0;
            for (int i = 0; i < (int)d; i++) {
                total += amplitude * noise(offset + (unsigned)i, a, b);
                weight += amplitude;
                a *= e;
                b *= e;
                amplitude *= f;
            }
            return total / weight;
        }
        case FIELD_CIRCLE: return c >= 0 ? hypotf(a, b) - c : NAN;
        case FIELD_BOX: {
            if (c < 0 || d < 0) {
                return NAN;
            }
            float qx = fabsf(a) - c, qy = fabsf(b) - d;
            return hypotf(fmaxf(qx, 0), fmaxf(qy, 0)) + fminf(fmaxf(qx, qy), 0);
        }
        case FIELD_CAPSULE: {
            float dx = e - c, dy = f - d, length = dx * dx + dy * dy;
            float t = length > 0
                        ? clamp(((a - c) * dx + (b - d) * dy) / length, 0, 1)
                        : 0;
            return g >= 0 ? hypotf(a - c - t * dx, b - d - t * dy) - g : NAN;
        }
        case FIELD_ROTATE_X: return cosf(c) * a + sinf(c) * b;
        case FIELD_ROTATE_Y: return -sinf(c) * a + cosf(c) * b;
        case FIELD_SMOOTH_UNION: return c >= 0 ? smooth_min(a, b, c) : NAN;
        case FIELD_SMOOTH_SUBTRACT: return c >= 0 ? -smooth_min(-a, b, c) : NAN;
        case FIELD_SELECT: return a > 0 ? b : c;
        case FIELD_OUTPUT: return a;
        case FIELD_OP_COUNT: break;
    }
    return NAN;
}

static float *bind_parameters(const field_program *p, field_context ctx, char *error, size_t size)
{
    float *values = xcalloc((size_t)acount(p->parameters) + 1, sizeof(float));
    for (int i = 0; i < acount(p->parameters); i++) {
        scalar(p->parameters[i].value, &values[i]);
    }
    for (int i = 0; i < ctx.parameter_count; i++) {
        int index = -1;
        for (int j = 0; j < acount(p->parameters); j++) {
            if (ctx.parameters[i].name &&
                !strcmp(ctx.parameters[i].name, p->parameters[j].name)) {
                index = j;
                break;
            }
        }
        if (index < 0 ||
            ctx.parameters[i].value.type != p->parameters[index].value.type ||
            !scalar(ctx.parameters[i].value, &values[index])) {
            fail(error, size, "Unknown, nonfinite, or incorrectly typed field parameter");
            free(values);
            return NULL;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(ctx.parameters[j].name, ctx.parameters[i].name)) {
                fail(error, size, "Duplicate field parameter");
                free(values);
                return NULL;
            }
        }
    }
    return values;
}

/* Keep X-only and constant instructions across rows; reuse is zero on the first
 * row a buffer sees. A row's working set is bounded by width * instruction
 * count, independent of grid height. */
static bool bake_row(
    const field_program *p,
    float *slots,
    const float *parameters,
    unsigned seed,
    field_grid grid,
    int reuse,
    int row
)
{
    int stride = acount(p->code);
    float y = grid.center_y + (row + .5f - grid.height * .5f) * grid.cell_size;
    for (int i = 0; i < stride; i++) {
        field_ir ir = p->code[i];
        if (reuse && !(ir.spatial & 2)) {
            continue;
        }
        int count = ir.spatial & 1 ? grid.width : 1;
        for (int column = 0; column < count; column++) {
            float x = grid.center_x + (column + .5f - grid.width * .5f) * grid.cell_size;
            float *cell = slots + column * stride;
            cell[i] = evaluate(ir, cell, x, y, seed, parameters);
            if (!isfinite(cell[i])) {
                return false;
            }
        }
        if (!(ir.spatial & 1)) {
            for (int column = 1; column < grid.width; column++) {
                slots[column * stride + i] = slots[i];
            }
        }
    }
    return true;
}

typedef struct
{
    const field_program *program;
    const float *parameters;
    unsigned seed;
    field_grid grid;
    float *out;
    int rows_per_band;
    /* Set by any failing band, possibly from several workers at once; bands
     * are independent otherwise. */
    atomic_bool invalid;
} bake_job;

static void bake_bands(int start, int end, uint32_t, void *context)
{
    bake_job *job = context;
    int stride = acount(job->program->code);
    float *slots = xcalloc((size_t)stride * job->grid.width, sizeof(float));
    for (int band = start; band < end; band++) {
        int first = band * job->rows_per_band;
        int last = first + job->rows_per_band < job->grid.height
                     ? first + job->rows_per_band
                     : job->grid.height;
        /* Each band recomputes its row-invariant instructions on its first row. */
        for (int y = first; y < last; y++) {
            if (!bake_row(
                    job->program,
                    slots,
                    job->parameters,
                    job->seed,
                    job->grid,
                    y - first == 0 ? 0 : y,
                    y
                )) {
                atomic_store_explicit(&job->invalid, true, memory_order_relaxed);
                break;
            }
            for (int x = 0; x < job->grid.width; x++) {
                job->out[y * job->grid.width + x] = slots
                    [x * stride + job->program->output];
            }
        }
    }
    free(slots);
}

bool field_program_bake(
    const field_program *p,
    field_context ctx,
    field_grid grid,
    float *out,
    const game_task_system *tasks,
    char *error,
    size_t size
)
{
    if (!p || !out || grid.width < 1 || grid.height < 1 || grid.width > 4096 ||
        grid.height > 4096 || !isfinite(grid.cell_size) || grid.cell_size <= 0 ||
        !isfinite(grid.center_x) || !isfinite(grid.center_y) ||
        ctx.parameter_count < 0 || (ctx.parameter_count && !ctx.parameters)) {
        return fail(error, size, "Invalid field grid or parameters");
    }
    float *parameters = bind_parameters(p, ctx, error, size);
    if (!parameters) {
        return false;
    }
    bool parallel = tasks && tasks->workers > 1 && grid.height >= 32;
    bake_job job = { p,    parameters, ctx.seed,
                     grid, out,        parallel ? 8 : grid.height,
                     false };
    int bands = (grid.height + job.rows_per_band - 1) / job.rows_per_band;
    void *task = parallel ? tasks->enqueue(bake_bands, bands, 1, &job, tasks->user)
                          : NULL;
    if (task) {
        tasks->finish(task, tasks->user);
    } else {
        /* Serial grids, or a pool that could not take the job. */
        bake_bands(0, bands, 0, &job);
    }
    /* finish() orders every band's writes before this load. */
    bool invalid = atomic_load_explicit(&job.invalid, memory_order_relaxed);
    if (invalid) {
        fail(error, size, "Field evaluation produced an invalid scalar");
    }
    free(parameters);
    return !invalid;
}

bool field_program_sample(
    const field_program *p,
    field_context ctx,
    float x,
    float y,
    float *out,
    char *error,
    size_t size
)
{
    return field_program_bake(p, ctx, (field_grid){ 1, 1, 1, x, y }, out, NULL, error, size);
}
