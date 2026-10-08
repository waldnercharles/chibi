#include "runtime/fields/program.h"

#include "support/array.h"
#include "support/cos_sin.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct field_program
{
    field_ir *code;
    field_parameter *parameters;
    int output;
};
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
static field_program *new_program(
    const field_ir *code,
    int count,
    int output,
    const field_parameter *parameters,
    int parameter_count,
    size_t budget,
    char *error,
    size_t size
)
{
    assert(!size || error);
    if (size) {
        error[0] = 0;
    }
    if (!code || count <= 0 || count > FIELD_INSTRUCTION_MAX || output < 0 ||
        output >= count || parameter_count < 0 ||
        parameter_count > FIELD_INSTRUCTION_MAX || (parameter_count && !parameters)) {
        fail(error, size, "Invalid prepared field bounds");
        return NULL;
    }
    static const unsigned char arity[FIELD_OP_COUNT] = { 0, 0, 0, 0, 2, 2, 2, 2,
                                                         2, 2, 1, 1, 1, 1, 1, 3,
                                                         5, 3, 6, 3, 3, 4, 7, 3,
                                                         3, 3, 3, 3, 1 };
    for (int i = 0; i < count; i++) {
        field_ir ir = code[i];
        if (ir.op < 0 || ir.op >= FIELD_OP_COUNT || ir.count != arity[ir.op] ||
            (ir.op == FIELD_CONSTANT && !isfinite(ir.value)) ||
            (ir.op == FIELD_PARAMETER &&
             (ir.parameter < 0 || ir.parameter >= parameter_count))) {
            fail(error, size, "Invalid prepared field operation");
            return NULL;
        }
        for (int j = 0; j < ir.count; j++) {
            if (ir.inputs[j] < 0 || ir.inputs[j] >= i) {
                fail(error, size, "Prepared field operands must precede their use");
                return NULL;
            }
        }
    }
    for (int i = 0; i < parameter_count; i++) {
        float value;
        if (!parameters[i].name || !parameters[i].name[0] ||
            strlen(parameters[i].name) > FIELD_PARAMETER_NAME_MAX ||
            (parameters[i].value.type_name &&
             strlen(parameters[i].value.type_name) > FIELD_PARAMETER_NAME_MAX) ||
            !scalar(parameters[i].value, &value)) {
            fail(error, size, "Prepared field parameters require named finite scalars");
            return NULL;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(parameters[i].name, parameters[j].name)) {
                fail(error, size, "Prepared field parameter name is duplicated");
                return NULL;
            }
        }
    }
    int code_capacity = count < 16 ? 16 : count;
    int parameter_capacity = parameter_count && parameter_count < 16 ? 16 : parameter_count;
    size_t retained = sizeof(field_program) + sizeof(dyna_hdr) +
                      (size_t)code_capacity * sizeof(field_ir);
    if (parameter_count) {
        retained += sizeof(dyna_hdr) +
                    (size_t)parameter_capacity * sizeof(field_parameter);
    }
    for (int i = 0; i < parameter_count; i++) {
        retained += strlen(parameters[i].name) + 1;
        if (parameters[i].value.type_name) {
            retained += strlen(parameters[i].value.type_name) + 1;
        }
    }
    if (retained > budget) {
        fail(error, size, "Prepared field retained storage exceeds budget");
        return NULL;
    }
    field_program *p = xcalloc(1, sizeof *p);
    asetcap(p->code, code_capacity);
    if (parameter_count) {
        asetcap(p->parameters, parameter_capacity);
    }
    p->output = output;
    for (int i = 0; i < count; i++) {
        field_ir ir = code[i];
        ir.spatial = ir.op == FIELD_X ? 1 : ir.op == FIELD_Y ? 2 : 0;
        for (int j = 0; j < FIELD_INPUT_MAX; j++) {
            if (j < ir.count) {
                ir.spatial |= p->code[ir.inputs[j]].spatial;
            } else {
                ir.inputs[j] = 0;
            }
        }
        apush(p->code, ir);
    }
    for (int i = 0; i < parameter_count; i++) {
        field_parameter parameter = parameters[i];
        parameter.name = xstrdup(parameter.name);
        if (parameter.value.type_name) {
            parameter.value.type_name = xstrdup(parameter.value.type_name);
        }
        apush(p->parameters, parameter);
    }
    return p;
}
field_program *field_program_new_budget(
    const field_ir *code,
    int count,
    int output,
    const field_parameter *parameters,
    int parameter_count,
    size_t *remaining,
    char *error,
    size_t size
)
{
    assert(remaining);
    field_program *p = new_program(code, count, output, parameters, parameter_count, *remaining, error, size);
    if (p) {
        *remaining -= field_program_storage_bytes(p);
    }
    return p;
}
field_program *field_program_new(
    const field_ir *code,
    int count,
    int output,
    const field_parameter *parameters,
    int parameter_count,
    char *error,
    size_t size
)
{
    return new_program(code, count, output, parameters, parameter_count, SIZE_MAX, error, size);
}
field_ir field_program_instruction_at(const field_program *p, int index)
{
    assert(p && index >= 0 && index < acount(p->code));
    return p->code[index];
}
int field_program_output(const field_program *p)
{
    return p->output;
}
int field_program_parameter_count(const field_program *p)
{
    return acount(p->parameters);
}
field_parameter field_program_parameter_at(const field_program *p, int index)
{
    assert(p && index >= 0 && index < acount(p->parameters));
    return p->parameters[index];
}

void field_program_free(field_program *p)
{
    if (!p) {
        return;
    }
    for (int i = 0; i < acount(p->parameters); i++) {
        free((char *)p->parameters[i].name);
        free((char *)p->parameters[i].value.type_name);
    }
    afree(p->parameters);
    afree(p->code);
    free(p);
}
size_t field_program_storage_bytes(const field_program *p)
{
    size_t bytes = sizeof *p;
    if (p->code) {
        bytes += sizeof(dyna_hdr) + (size_t)acap(p->code) * sizeof *p->code;
    }
    if (p->parameters) {
        bytes += sizeof(dyna_hdr) + (size_t)acap(p->parameters) * sizeof *p->parameters;
    }
    for (int i = 0; i < acount(p->parameters); i++) {
        bytes += strlen(p->parameters[i].name) + 1;
        if (p->parameters[i].value.type_name) {
            bytes += strlen(p->parameters[i].value.type_name) + 1;
        }
    }
    return bytes;
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
/* Exactly rounded, unlike libm's hypotf, so every platform agrees. */
static float distance(float x, float y)
{
    return sqrtf(x * x + y * y);
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

/* The one-point API keeps a scalar interpreter. Besides fitting small
 * previews, it gives row baking an independent semantic reference in tests. */
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
        case FIELD_SIN: return cos_sin_compute(a).s;
        case FIELD_COS: return cos_sin_compute(a).c;
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
        case FIELD_CIRCLE: return c >= 0 ? distance(a, b) - c : NAN;
        case FIELD_BOX: {
            if (c < 0 || d < 0) {
                return NAN;
            }
            float qx = fabsf(a) - c, qy = fabsf(b) - d;
            return distance(fmaxf(qx, 0), fmaxf(qy, 0)) + fminf(fmaxf(qx, qy), 0);
        }
        case FIELD_CAPSULE: {
            float dx = e - c, dy = f - d, length = dx * dx + dy * dy;
            float t = length > 0
                        ? clamp(((a - c) * dx + (b - d) * dy) / length, 0, 1)
                        : 0;
            return g >= 0 ? distance(a - c - t * dx, b - d - t * dy) - g : NAN;
        }
        case FIELD_ROTATE_X: {
            cos_sin r = cos_sin_compute(c);
            return r.c * a + r.s * b;
        }
        case FIELD_ROTATE_Y: {
            cos_sin r = cos_sin_compute(c);
            return -r.s * a + r.c * b;
        }
        case FIELD_SMOOTH_UNION: return c >= 0 ? smooth_min(a, b, c) : NAN;
        case FIELD_SMOOTH_SUBTRACT: return c >= 0 ? -smooth_min(-a, b, c) : NAN;
        case FIELD_SELECT: return a > 0 ? b : c;
        case FIELD_OUTPUT: return a;
        case FIELD_OP_COUNT: break;
    }
    return NAN;
}

/* Dispatch once per instruction, then walk contiguous row values. The old
 * cell-major scratch layout called this switch once per cell and touched each
 * operand at a stride of the instruction count. */
static void evaluate_row(
    field_ir ir,
    const float *restrict inputs[FIELD_INPUT_MAX],
    float *restrict out,
    int count,
    field_grid grid,
    float y,
    unsigned seed,
    const float *parameters
)
{
#define ROW_1(operation, expression)                                           \
    case operation:                                                            \
        for (int column = 0; column < count; column++) {                       \
            const float a = inputs[0][column];                                 \
            out[column] = expression;                                          \
        }                                                                      \
        break
#define ROW_2(operation, expression)                                           \
    case operation:                                                            \
        for (int column = 0; column < count; column++) {                       \
            const float a = inputs[0][column], b = inputs[1][column];          \
            out[column] = expression;                                          \
        }                                                                      \
        break
#define ROW_3(operation, expression)                                           \
    case operation:                                                            \
        for (int column = 0; column < count; column++) {                       \
            const float a = inputs[0][column], b = inputs[1][column],          \
                        c = inputs[2][column];                                 \
            out[column] = expression;                                          \
        }                                                                      \
        break
    switch (ir.op) {
        case FIELD_CONSTANT: out[0] = ir.value; break;
        case FIELD_X:
            for (int column = 0; column < count; column++) {
                out[column] = grid.center_x +
                              (column + .5f - grid.width * .5f) * grid.cell_size;
            }
            break;
        case FIELD_Y: out[0] = y; break;
        case FIELD_PARAMETER:
            out[0] = parameters[ir.parameter];
            break;
            ROW_2(FIELD_ADD, a + b);
            ROW_2(FIELD_SUBTRACT, a - b);
            ROW_2(FIELD_MULTIPLY, a * b);
            ROW_2(FIELD_DIVIDE, b != 0 ? a / b : NAN);
            ROW_2(FIELD_MIN, fminf(a, b));
            ROW_2(FIELD_MAX, fmaxf(a, b));
            ROW_1(FIELD_ABS, fabsf(a));
            ROW_1(FIELD_NEGATE, -a);
            ROW_1(FIELD_SIN, cos_sin_compute(a).s);
            ROW_1(FIELD_COS, cos_sin_compute(a).c);
            ROW_1(FIELD_FLOOR, floorf(a));
            ROW_3(FIELD_CLAMP, clamp(a, b, c));
        case FIELD_REMAP:
            for (int column = 0; column < count; column++) {
                const float a = inputs[0][column], b = inputs[1][column],
                            c = inputs[2][column], d = inputs[3][column],
                            e = inputs[4][column];
                out[column] = b != c ? mix(d, e, (a - b) / (c - b)) : NAN;
            }
            break;
        case FIELD_HASH:
            for (int column = 0; column < count; column++) {
                const float a = inputs[0][column], b = inputs[1][column],
                            c = inputs[2][column];
                out[column] = fabsf(a) >= 2147483520.f || fabsf(b) >= 2147483520.f ||
                                      fabsf(c) >= 2147483520.f
                                ? NAN
                                : hash_sample(seed + (unsigned)(int)c, (int)floorf(a), (int)floorf(b));
            }
            break;
        case FIELD_NOISE:
            for (int column = 0; column < count; column++) {
                const float a = inputs[0][column], b = inputs[1][column],
                            c = inputs[2][column];
                out[column] = fabsf(a) >= 2147483520.f || fabsf(b) >= 2147483520.f ||
                                      fabsf(c) >= 2147483520.f
                                ? NAN
                                : noise(seed + (unsigned)(int)c, a, b);
            }
            break;
        case FIELD_FRACTAL:
            for (int column = 0; column < count; column++) {
                float a = inputs[0][column], b = inputs[1][column],
                      c = inputs[2][column];
                const float d = inputs[3][column], e = inputs[4][column],
                            f = inputs[5][column];
                if (fabsf(a) >= 2147483520.f || fabsf(b) >= 2147483520.f ||
                    fabsf(c) >= 2147483520.f || d < 1 || d > 16 ||
                    floorf(d) != d || e <= 0 || f < 0 || f > 1) {
                    out[column] = NAN;
                    continue;
                }
                unsigned offset = seed + (unsigned)(int)c;
                float total = 0, amplitude = 1, weight = 0;
                for (int i = 0; i < (int)d; i++) {
                    total += amplitude * noise(offset + (unsigned)i, a, b);
                    weight += amplitude;
                    a *= e;
                    b *= e;
                    amplitude *= f;
                }
                out[column] = total / weight;
            }
            break;
        case FIELD_CIRCLE:
            for (int column = 0; column < count; column++) {
                const float a = inputs[0][column], b = inputs[1][column],
                            c = inputs[2][column];
                out[column] = c >= 0 ? distance(a, b) - c : NAN;
            }
            break;
        case FIELD_BOX: {
            for (int column = 0; column < count; column++) {
                const float a = inputs[0][column], b = inputs[1][column],
                            c = inputs[2][column], d = inputs[3][column];
                if (c < 0 || d < 0) {
                    out[column] = NAN;
                    continue;
                }
                float qx = fabsf(a) - c, qy = fabsf(b) - d;
                out[column] = distance(fmaxf(qx, 0), fmaxf(qy, 0)) +
                              fminf(fmaxf(qx, qy), 0);
            }
            break;
        }
        case FIELD_CAPSULE: {
            for (int column = 0; column < count; column++) {
                const float a = inputs[0][column], b = inputs[1][column],
                            c = inputs[2][column], d = inputs[3][column],
                            e = inputs[4][column], f = inputs[5][column],
                            g = inputs[6][column];
                float dx = e - c, dy = f - d, length = dx * dx + dy * dy;
                float t = length > 0
                            ? clamp(((a - c) * dx + (b - d) * dy) / length, 0, 1)
                            : 0;
                out[column] = g >= 0 ? distance(a - c - t * dx, b - d - t * dy) - g
                                     : NAN;
            }
            break;
        }
        case FIELD_ROTATE_X:
            for (int column = 0; column < count; column++) {
                const float a = inputs[0][column], b = inputs[1][column],
                            c = inputs[2][column];
                cos_sin r = cos_sin_compute(c);
                out[column] = r.c * a + r.s * b;
            }
            break;
        case FIELD_ROTATE_Y:
            for (int column = 0; column < count; column++) {
                const float a = inputs[0][column], b = inputs[1][column],
                            c = inputs[2][column];
                cos_sin r = cos_sin_compute(c);
                out[column] = -r.s * a + r.c * b;
            }
            break;
        case FIELD_SMOOTH_UNION:
            for (int column = 0; column < count; column++) {
                const float a = inputs[0][column], b = inputs[1][column],
                            c = inputs[2][column];
                out[column] = c >= 0 ? smooth_min(a, b, c) : NAN;
            }
            break;
        case FIELD_SMOOTH_SUBTRACT:
            for (int column = 0; column < count; column++) {
                const float a = inputs[0][column], b = inputs[1][column],
                            c = inputs[2][column];
                out[column] = c >= 0 ? -smooth_min(-a, b, c) : NAN;
            }
            break;
            ROW_3(FIELD_SELECT, a > 0 ? b : c);
            ROW_1(FIELD_OUTPUT, a);
        case FIELD_OP_COUNT: out[0] = NAN; break;
    }
#undef ROW_1
#undef ROW_2
#undef ROW_3
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
 * row a buffer sees. Values are instruction-major so each row kernel reads and
 * writes contiguous columns. */
static bool bake_row(
    const field_program *restrict p,
    float *restrict slots,
    const float *restrict parameters,
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
        const float *restrict inputs[FIELD_INPUT_MAX];
        for (int k = 0; k < ir.count; k++) {
            inputs[k] = slots + (size_t)ir.inputs[k] * grid.width;
        }
        float *restrict out = slots + (size_t)i * grid.width;
        evaluate_row(ir, inputs, out, count, grid, y, seed, parameters);
        for (int column = 0; column < count; column++) {
            if (!isfinite(out[column])) {
                return false;
            }
        }
        if (count == 1) {
            for (int column = 1; column < grid.width; column++) {
                out[column] = out[0];
            }
        }
    }
    return true;
}

typedef struct
{
    float *slots;
    int capacity;
} bake_workspace;
struct field_program_baker
{
    const field_program *program;
    float *parameters;
    unsigned seed;
    int worker_count;
    bake_workspace *workspaces;
};
typedef struct
{
    field_program_baker *baker;
    field_grid grid;
    int first_row, last_row;
    float *out;
    int rows_per_band;
    /* Set by any failing band, possibly from several workers at once; bands
     * are independent otherwise. */
    atomic_bool invalid;
} bake_job;

static float *workspace_for_worker(field_program_baker *baker, uint32_t worker, int width)
{
    assert(worker < (uint32_t)baker->worker_count);
    bake_workspace *workspace = &baker->workspaces[worker];
    /* The live width sets slot strides; each band's first row rewrites every
     * slot, so a high-water allocation can safely serve narrower edge grids. */
    if (width > workspace->capacity) {
        workspace->slots = xrealloc(
            workspace->slots,
            (size_t)acount(baker->program->code) * width * sizeof(float)
        );
        workspace->capacity = width;
    }
    return workspace->slots;
}

static void bake_bands(int start, int end, uint32_t worker, void *context)
{
    bake_job *job = context;
    field_program_baker *baker = job->baker;
    const field_program *program = baker->program;
    float *slots = workspace_for_worker(baker, worker, job->grid.width);
    for (int band = start; band < end; band++) {
        int first = job->first_row + band * job->rows_per_band;
        int last = first + job->rows_per_band < job->last_row ? first + job->rows_per_band
                                                              : job->last_row;
        /* Each band recomputes its row-invariant instructions on its first row. */
        for (int y = first; y < last; y++) {
            if (!bake_row(program, slots, baker->parameters, baker->seed, job->grid, y - first == 0 ? 0 : y, y)) {
                atomic_store_explicit(&job->invalid, true, memory_order_relaxed);
                break;
            }
            memcpy(
                job->out + y * job->grid.width,
                slots + (size_t)program->output * job->grid.width,
                (size_t)job->grid.width * sizeof(float)
            );
        }
    }
}

field_program_baker *field_program_baker_new(
    const field_program *p,
    field_context ctx,
    int workers,
    char *error,
    size_t size
)
{
    if (!p || workers < 1 || workers > GAME_TASK_WORKER_MAX ||
        ctx.parameter_count < 0 || (ctx.parameter_count && !ctx.parameters)) {
        fail(error, size, "Invalid field grid or parameters");
        return NULL;
    }
    float *parameters = bind_parameters(p, ctx, error, size);
    if (!parameters) {
        return NULL;
    }
    field_program_baker *baker = xcalloc(1, sizeof *baker);
    *baker = (field_program_baker){ p,
                                    parameters,
                                    ctx.seed,
                                    workers,
                                    xcalloc((size_t)workers, sizeof(bake_workspace)) };
    return baker;
}

void field_program_baker_free(field_program_baker *baker)
{
    if (!baker) {
        return;
    }
    for (int i = 0; i < baker->worker_count; i++) {
        free(baker->workspaces[i].slots);
    }
    free(baker->workspaces);
    free(baker->parameters);
    free(baker);
}

static bool valid_grid(field_grid grid)
{
    return grid.width >= 1 && grid.height >= 1 && grid.width <= 4096 &&
           grid.height <= 4096 && isfinite(grid.cell_size) && grid.cell_size > 0 &&
           isfinite(grid.center_x) && isfinite(grid.center_y);
}

bool field_program_baker_bake_rows(
    field_program_baker *baker,
    field_grid grid,
    int first,
    int count,
    float *out,
    const game_task_system *tasks,
    char *error,
    size_t size
)
{
    if (!baker || !out || !valid_grid(grid) ||
        (tasks && tasks->workers != baker->worker_count)) {
        return fail(error, size, "Invalid field grid or parameters");
    }
    assert(first >= 0 && count > 0 && first + count <= grid.height);
    bool parallel = tasks && tasks->workers > 1 && count >= 32;
    bake_job job = { baker,         grid, first,
                     first + count, out,  parallel ? 8 : count,
                     false };
    int bands = (count + job.rows_per_band - 1) / job.rows_per_band;
    if (parallel) {
        void *task = tasks->enqueue(bake_bands, bands, 1, &job, tasks->user);
        if (task) {
            tasks->finish(task, tasks->user);
        }
        /* A NULL handle means enqueue already completed the range inline. */
    } else {
        bake_bands(0, bands, 0, &job);
    }
    /* finish() orders every band's writes before this load. */
    bool invalid = atomic_load_explicit(&job.invalid, memory_order_relaxed);
    if (invalid) {
        fail(error, size, "Field evaluation produced an invalid scalar");
    }
    return !invalid;
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
    return field_program_bake_rows(p, ctx, grid, 0, grid.height, out, tasks, error, size);
}

bool field_program_bake_rows(
    const field_program *p,
    field_context ctx,
    field_grid grid,
    int first,
    int count,
    float *out,
    const game_task_system *tasks,
    char *error,
    size_t size
)
{
    if (!out || !valid_grid(grid) || ctx.parameter_count < 0 ||
        (ctx.parameter_count && !ctx.parameters)) {
        return fail(error, size, "Invalid field grid or parameters");
    }
    field_program_baker *baker;
    baker = field_program_baker_new(p, ctx, tasks ? tasks->workers : 1, error, size);
    if (!baker) {
        return false;
    }
    bool ok = field_program_baker_bake_rows(baker, grid, first, count, out, tasks, error, size);
    field_program_baker_free(baker);
    return ok;
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
    if (!p || !out || !isfinite(x) || !isfinite(y) || ctx.parameter_count < 0 ||
        (ctx.parameter_count && !ctx.parameters)) {
        return fail(error, size, "Invalid field grid or parameters");
    }
    float *parameters = bind_parameters(p, ctx, error, size);
    if (!parameters) {
        return false;
    }
    int stride = acount(p->code);
    float *values = xcalloc((size_t)stride, sizeof(float));
    for (int i = 0; i < stride; i++) {
        values[i] = evaluate(p->code[i], values, x, y, ctx.seed, parameters);
        if (!isfinite(values[i])) {
            fail(error, size, "Field evaluation produced an invalid scalar");
            free(values);
            free(parameters);
            return false;
        }
    }
    *out = values[p->output];
    free(values);
    free(parameters);
    return true;
}
