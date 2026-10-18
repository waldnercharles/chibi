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
static const unsigned char ARITY[] = {
    [FIELD_CONSTANT] = 0,
    [FIELD_X] = 0,
    [FIELD_Y] = 0,
    [FIELD_PARAMETER] = 0,
    [FIELD_ADD] = 2,
    [FIELD_SUBTRACT] = 2,
    [FIELD_MULTIPLY] = 2,
    [FIELD_DIVIDE] = 2,
    [FIELD_MIN] = 2,
    [FIELD_MAX] = 2,
    [FIELD_ABS] = 1,
    [FIELD_NEGATE] = 1,
    [FIELD_SIN] = 1,
    [FIELD_COS] = 1,
    [FIELD_FLOOR] = 1,
    [FIELD_CLAMP] = 3,
    [FIELD_REMAP] = 5,
    [FIELD_NOISE] = 3,
    [FIELD_FRACTAL] = 6,
    [FIELD_HASH] = 3,
    [FIELD_CIRCLE] = 3,
    [FIELD_BOX] = 4,
    [FIELD_CAPSULE] = 7,
    [FIELD_ROTATE_X] = 3,
    [FIELD_ROTATE_Y] = 3,
    [FIELD_SMOOTH_UNION] = 3,
    [FIELD_SMOOTH_SUBTRACT] = 3,
    [FIELD_SELECT] = 3,
    [FIELD_OUTPUT] = 1,
    [FIELD_Z] = 0,
    [FIELD_CELL_X] = 4,
    [FIELD_CELL_Y] = 4,
    [FIELD_CELL_EDGE] = 4,
};
_Static_assert(sizeof ARITY == FIELD_OP_COUNT, "every field op has an arity");
int field_op_arity(field_op op)
{
    assert(op >= 0 && op < FIELD_OP_COUNT);
    return ARITY[op];
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
    for (int i = 0; i < count; i++) {
        field_ir ir = code[i];
        if (ir.op < 0 || ir.op >= FIELD_OP_COUNT || ir.count != ARITY[ir.op] ||
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
/* Columnar cells: the Voronoi diagram of a unit triangular lattice whose
 * sites each move at most CELL_JITTER_MAX. Then the nearest site is the
 * nearest lattice point's or one of its six neighbors', and every cell
 * borders only sites within two lattice rings of its own, so both searches
 * stay local and exact. */
#define CELL_JITTER_MAX .25f
#define CELL_LIMIT 1e6f
static const int CELL_RING[18][2] = {
    { 1, 0 }, { -1, 0 },  { 0, 1 },  { 0, -1 }, { 1, -1 }, { -1, 1 },
    { 1, 1 }, { -1, -1 }, { 2, -1 }, { -2, 1 }, { 1, -2 }, { -1, 2 },
    { 2, 0 }, { -2, 0 },  { 0, 2 },  { 0, -2 }, { 2, -2 }, { -2, 2 },
};
static const float CELL_ROW = .8660254f; /* sqrt(3) / 2 */

static void cell_site(unsigned seed, int i, int j, float jitter, float out[2])
{
    /* Square jitter scaled so no site moves farther than jitter. */
    float reach = jitter * .70710678f;
    out[0] = i + .5f * j + reach * hash_sample(seed, i, j);
    out[1] = j * CELL_ROW + reach * hash_sample(seed ^ 0x68e31da4u, i, j);
}

/* The site of the cell holding (x, y) and its lattice point, or false for
 * an invalid jitter or out-of-range position. */
static bool nearest_cell(unsigned seed, float x, float y, float jitter, int lattice[2], float site[2])
{
    if (!(jitter >= 0 && jitter <= CELL_JITTER_MAX) ||
        !(fabsf(x) < CELL_LIMIT) || !(fabsf(y) < CELL_LIMIT)) {
        return false;
    }
    /* Round the axial lattice coordinates through cube coordinates. */
    float r = y / CELL_ROW, q = x - .5f * r, s = -q - r;
    float rq = roundf(q), rr = roundf(r), rs = roundf(s);
    float dq = fabsf(rq - q), dr = fabsf(rr - r), ds = fabsf(rs - s);
    if (dq > dr && dq > ds) {
        rq = -rr - rs;
    } else if (dr > ds) {
        rr = -rq - rs;
    }
    float best = INFINITY;
    for (int n = -1; n < 6; n++) {
        int i = (int)rq + (n < 0 ? 0 : CELL_RING[n][0]),
            j = (int)rr + (n < 0 ? 0 : CELL_RING[n][1]);
        float at[2];
        cell_site(seed, i, j, jitter, at);
        float dx = at[0] - x, dy = at[1] - y, d = dx * dx + dy * dy;
        if (d < best) {
            best = d;
            lattice[0] = i;
            lattice[1] = j;
            site[0] = at[0];
            site[1] = at[1];
        }
    }
    return true;
}

/* Cell X and Y return the site; Cell Edge the distance from (x, y) to its
 * cell's boundary, the nearest bisector with a neighboring site [Quilez]. */
static float cell(field_op op, unsigned seed, float x, float y, float jitter, float offset)
{
    int lattice[2];
    float site[2];
    if (fabsf(offset) >= 2147483520.f ||
        !nearest_cell(seed + (unsigned)(int)offset, x, y, jitter, lattice, site)) {
        return NAN;
    }
    if (op != FIELD_CELL_EDGE) {
        return site[op == FIELD_CELL_Y];
    }
    float edge = INFINITY;
    for (int n = 0; n < 18; n++) {
        float other[2];
        cell_site(
            seed + (unsigned)(int)offset,
            lattice[0] + CELL_RING[n][0],
            lattice[1] + CELL_RING[n][1],
            jitter,
            other
        );
        float dx = other[0] - site[0], dy = other[1] - site[1];
        float mx = .5f * (other[0] + site[0]) - x,
              my = .5f * (other[1] + site[1]) - y;
        edge = fminf(edge, (mx * dx + my * dy) / distance(dx, dy));
    }
    return edge;
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
 * previews, it gives row baking an independent semantic reference in tests,
 * and library host calls evaluate through it. */
float field_op_value(field_op op, const float *operands, unsigned seed)
{
    assert(field_op_arity(op) > 0);
    float v[FIELD_INPUT_MAX] = { 0 };
    memcpy(v, operands, (size_t)field_op_arity(op) * sizeof *v);
    float a = v[0], b = v[1], c = v[2], d = v[3], e = v[4], f = v[5], g = v[6];
    switch (op) {
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
            if (op == FIELD_HASH) {
                return hash_sample(offset, (int)floorf(a), (int)floorf(b));
            }
            if (op == FIELD_NOISE) {
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
        case FIELD_CELL_X:
        case FIELD_CELL_Y:
        case FIELD_CELL_EDGE: return cell(op, seed, a, b, c, d);
        case FIELD_CONSTANT:
        case FIELD_X:
        case FIELD_Y:
        case FIELD_Z:
        case FIELD_PARAMETER:
        case FIELD_OP_COUNT: break;
    }
    return NAN;
}
static float evaluate(field_ir ir, const float *v, float x, float y, float z, unsigned seed, const float *parameters)
{
    switch (ir.op) {
        case FIELD_CONSTANT: return ir.value;
        case FIELD_X: return x;
        case FIELD_Y: return y;
        case FIELD_Z: return z;
        case FIELD_PARAMETER: return parameters[ir.parameter];
        default: break;
    }
    float operands[FIELD_INPUT_MAX];
    for (int i = 0; i < ir.count; i++) {
        operands[i] = v[ir.inputs[i]];
    }
    return field_op_value(ir.op, operands, seed);
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
        case FIELD_Z: out[0] = grid.depth; break;
        case FIELD_CELL_X:
        case FIELD_CELL_Y:
        case FIELD_CELL_EDGE:
            for (int column = 0; column < count; column++) {
                out[column] = cell(
                    ir.op,
                    seed,
                    inputs[0][column],
                    inputs[1][column],
                    inputs[2][column],
                    inputs[3][column]
                );
            }
            break;
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
    if (!p || workers < 1 || workers > TASK_WORKER_MAX ||
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

enum
{
    /* Points evaluated together, sharing one instruction dispatch. */
    POINT_BATCH = 256
};
typedef struct
{
    field_program_baker *baker;
    const float *x, *y, *z;
    int count;
    float *out;
    atomic_bool invalid;
} point_job;

/* Evaluates points [first, first + count) of a job. Every instruction runs
 * across the batch, so arbitrary positions need no row structure. */
static bool bake_point_batch(
    const field_program_baker *baker,
    float *slots,
    const point_job *job,
    int first,
    int count
)
{
    const field_program *p = baker->program;
    for (int i = 0; i < acount(p->code); i++) {
        field_ir ir = p->code[i];
        float *out = slots + (size_t)i * POINT_BATCH;
        switch (ir.op) {
            case FIELD_X:
                memcpy(out, job->x + first, (size_t)count * sizeof *out);
                break;
            case FIELD_Y:
                memcpy(out, job->y + first, (size_t)count * sizeof *out);
                break;
            case FIELD_Z:
                memcpy(out, job->z + first, (size_t)count * sizeof *out);
                break;
            case FIELD_CONSTANT:
            case FIELD_PARAMETER: {
                float value = ir.op == FIELD_CONSTANT
                                ? ir.value
                                : baker->parameters[ir.parameter];
                for (int c = 0; c < count; c++) {
                    out[c] = value;
                }
                break;
            }
            default: {
                const float *inputs[FIELD_INPUT_MAX];
                for (int k = 0; k < ir.count; k++) {
                    inputs[k] = slots + (size_t)ir.inputs[k] * POINT_BATCH;
                }
                evaluate_row(
                    ir,
                    inputs,
                    out,
                    count,
                    (field_grid){ 0 },
                    0,
                    baker->seed,
                    baker->parameters
                );
            }
        }
        for (int c = 0; c < count; c++) {
            if (!isfinite(out[c])) {
                return false;
            }
        }
    }
    memcpy(
        job->out + first,
        slots + (size_t)p->output * POINT_BATCH,
        (size_t)count * sizeof *job->out
    );
    return true;
}

static void bake_point_bands(int start, int end, uint32_t worker, void *context)
{
    point_job *job = context;
    float *slots = workspace_for_worker(job->baker, worker, POINT_BATCH);
    for (int batch = start; batch < end; batch++) {
        int first = batch * POINT_BATCH;
        int count = job->count - first < POINT_BATCH ? job->count - first : POINT_BATCH;
        if (!bake_point_batch(job->baker, slots, job, first, count)) {
            atomic_store_explicit(&job->invalid, true, memory_order_relaxed);
            return;
        }
    }
}

bool field_program_baker_bake_points(
    field_program_baker *baker,
    const float *x,
    const float *y,
    const float *z,
    int count,
    float *out,
    const task_system *tasks,
    char *error,
    size_t size
)
{
    if (!baker || count < 0 || (count && (!x || !y || !z || !out)) ||
        (tasks && tasks->workers != baker->worker_count)) {
        return fail(error, size, "Invalid field points");
    }
    point_job job = { baker, x, y, z, count, out, false };
    int batches = (count + POINT_BATCH - 1) / POINT_BATCH;
    if (tasks && tasks->workers > 1 && batches > 1) {
        void *task = tasks->enqueue(bake_point_bands, batches, 1, &job, tasks->user);
        if (task) {
            tasks->finish(task, tasks->user);
        }
        /* A NULL handle means enqueue already completed the range inline. */
    } else {
        bake_point_bands(0, batches, 0, &job);
    }
    bool invalid = atomic_load_explicit(&job.invalid, memory_order_relaxed);
    if (invalid) {
        fail(error, size, "Field evaluation produced an invalid scalar");
    }
    return !invalid;
}

static bool valid_grid(field_grid grid)
{
    return grid.width >= 1 && grid.height >= 1 && grid.width <= 4096 &&
           grid.height <= 4096 && isfinite(grid.cell_size) &&
           grid.cell_size > 0 && isfinite(grid.center_x) &&
           isfinite(grid.center_y) && isfinite(grid.depth);
}

bool field_program_baker_bake_rows(
    field_program_baker *baker,
    field_grid grid,
    int first,
    int count,
    float *out,
    const task_system *tasks,
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
    const task_system *tasks,
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
    const task_system *tasks,
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
        values[i] = evaluate(p->code[i], values, x, y, 0, ctx.seed, parameters);
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
