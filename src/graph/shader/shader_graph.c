#include "graph/shader/shader_graph.h"

#include "support/array.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *const names[] = { "Constant",
                                     "Parameter",
                                     "Position",
                                     "Time",
                                     "Add",
                                     "Subtract",
                                     "Multiply",
                                     "Divide",
                                     "Min",
                                     "Max",
                                     "Abs",
                                     "Sin",
                                     "Cos",
                                     "Fract",
                                     "Length",
                                     "Dot",
                                     "Mix",
                                     "Select",
                                     "Circle",
                                     "Box",
                                     "Union",
                                     "Intersection",
                                     "Difference",
                                     "SmoothUnion",
                                     "Vector2",
                                     "Vector4",
                                     "X",
                                     "Y",
                                     "Z",
                                     "W",
                                     "WorldPosition" };
const char *shader_op_name(shader_op op)
{
    return op >= 0 && op < SHADER_OP_COUNT ? names[op] : "Invalid";
}
int shader_op_inputs(shader_op op)
{
    if (op <= SHADER_TIME || op == SHADER_WORLD_POSITION) {
        return 0;
    }
    if ((op >= SHADER_ABS && op <= SHADER_LENGTH) || op >= SHADER_X) {
        return 1;
    }
    if (op == SHADER_MIX || op == SHADER_SELECT || op == SHADER_SMOOTH_UNION) {
        return 3;
    }
    if (op == SHADER_VECTOR4) {
        return 4;
    }
    return 2;
}
static bool fail(shader_error *error, uint64_t id, const char *message)
{
    if (error) {
        error->node = id;
        snprintf(error->message, sizeof error->message, "%s", message);
    }
    return false;
}
void shader_graph_init(shader_graph *g)
{
    *g = (shader_graph){ 0 };
    for (int i = 0; i < SHADER_OUTPUT_MAX; i++) {
        g->output[i] = -1;
    }
}
void shader_graph_free(shader_graph *g)
{
    afree(g->nodes);
    shader_graph_init(g);
}
int shader_graph_add(shader_graph *g, shader_op op, int width)
{
    shader_node n = { .op = op, .width = width, .input = { -1, -1, -1, -1 } };
    apush(g->nodes, n);
    return acount(g->nodes) - 1;
}
bool shader_graph_validate(const shader_graph *g, shader_error *error)
{
    if (acount(g->nodes) > SHADER_NODE_MAX || !acount(g->nodes)) {
        return fail(error, 0, "Invalid shader graph size");
    }
    if (g->output_count < 1 || g->output_count > SHADER_OUTPUT_MAX) {
        return fail(error, 0, "Shader needs an output");
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        const shader_node *n = &g->nodes[i];
        if (n->op < 0 || n->op >= SHADER_OP_COUNT || n->width < 1 || n->width > 4) {
            return fail(error, n->source_id, "Invalid shader operation or type");
        }
        if (n->op == SHADER_PARAMETER &&
            (n->parameter < 0 || n->parameter >= SHADER_PARAMETER_MAX)) {
            return fail(error, n->source_id, "Parameter slot must be between 0 and 31");
        }
        for (int j = 0; j < 4; j++) {
            if (!isfinite(n->value[j])) {
                return fail(error, n->source_id, "Shader values must be finite");
            }
        }
        for (int j = 0; j < shader_op_inputs(n->op); j++) {
            if (n->input[j] < 0 || n->input[j] >= i) {
                return fail(error, n->source_id, "Shader contains a cycle or missing input");
            }
        }
        int a = shader_op_inputs(n->op) > 0 ? g->nodes[n->input[0]].width : 0;
        int b = shader_op_inputs(n->op) > 1 ? g->nodes[n->input[1]].width : 0;
        int c = shader_op_inputs(n->op) > 2 ? g->nodes[n->input[2]].width : 0;
        bool valid = true;
        switch (n->op) {
            case SHADER_POSITION:
            case SHADER_WORLD_POSITION: valid = n->width == 2; break;
            case SHADER_TIME: valid = n->width == 1; break;
            case SHADER_LENGTH: valid = n->width == 1; break;
            case SHADER_DOT: valid = n->width == 1 && a == b; break;
            case SHADER_CIRCLE:
                valid = n->width == 1 && a == 2 && b == 1;
                break;
            case SHADER_BOX: valid = n->width == 1 && a == 2 && b == 2; break;
            case SHADER_UNION:
            case SHADER_INTERSECTION:
            case SHADER_DIFFERENCE:
                valid = n->width == 1 && a == 1 && b == 1;
                break;
            case SHADER_SMOOTH_UNION:
                valid = n->width == 1 && a == 1 && b == 1 && c == 1;
                break;
            case SHADER_VECTOR2:
                valid = n->width == 2 && a == 1 && b == 1;
                break;
            case SHADER_VECTOR4:
                valid = n->width == 4 && a == 1 && b == 1 && c == 1 &&
                        g->nodes[n->input[3]].width == 1;
                break;
            case SHADER_X:
            case SHADER_Y:
            case SHADER_Z:
            case SHADER_W:
                valid = n->width == 1 && a > (int)n->op - SHADER_X;
                break;
            case SHADER_CONSTANT:
            case SHADER_PARAMETER: break;
            default:
                valid = (a == 1 || a == n->width) &&
                        (!b || b == 1 || b == n->width) &&
                        (!c || c == 1 || (n->op != SHADER_SELECT && c == n->width));
                break;
        }
        if (!valid) {
            return fail(error, n->source_id, "Shader input and output types do not match");
        }
    }
    for (int i = 0; i < g->output_count; i++) {
        if (g->output[i] < 0 || g->output[i] >= acount(g->nodes)) {
            return fail(error, 0, "Missing shader output");
        }
    }
    return true;
}
static float component(const shader_graph *g, float (*v)[4], int index, int c)
{
    return v[index][g->nodes[index].width == 1 ? 0 : c];
}
static float smooth_union(float a, float b, float k)
{
    if (k <= 0) {
        return fminf(a, b);
    }
    float h = fmaxf(0, fminf(1, 0.5f + 0.5f * (b - a) / k));
    return b + (a - b) * h - k * h * (1 - h);
}
bool shader_graph_eval_outputs(
    const shader_graph *g,
    const float p[2],
    const float world[2],
    float time,
    const float (*params)[4],
    float result[SHADER_OUTPUT_MAX][4],
    shader_error *error
)
{
    if (!shader_graph_validate(g, error)) {
        return false;
    }
    float (*v)[4] = xcalloc((size_t)acount(g->nodes), sizeof *v);
    for (int i = 0; i < acount(g->nodes); i++) {
        const shader_node *n = &g->nodes[i];
        float a[4] = { 0 }, b[4] = { 0 }, c[4] = { 0 };
        for (int j = 0; j < 4; j++) {
            if (shader_op_inputs(n->op) > 0) {
                a[j] = component(g, v, n->input[0], j);
            }
            if (shader_op_inputs(n->op) > 1) {
                b[j] = component(g, v, n->input[1], j);
            }
            if (shader_op_inputs(n->op) > 2) {
                c[j] = component(g, v, n->input[2], j);
            }
        }
        for (int j = 0; j < n->width; j++) {
            switch (n->op) {
                case SHADER_CONSTANT: v[i][j] = n->value[j]; break;
                case SHADER_PARAMETER:
                    v[i][j] = params ? params[n->parameter][j] : n->value[j];
                    break;
                case SHADER_POSITION: v[i][j] = j < 2 ? p[j] : 0; break;
                case SHADER_TIME: v[i][j] = time; break;
                case SHADER_WORLD_POSITION:
                    v[i][j] = j < 2 ? world[j] : 0;
                    break;
                case SHADER_ADD: v[i][j] = a[j] + b[j]; break;
                case SHADER_SUBTRACT: v[i][j] = a[j] - b[j]; break;
                case SHADER_MULTIPLY: v[i][j] = a[j] * b[j]; break;
                case SHADER_DIVIDE:
                    v[i][j] = b[j] == 0 ? 0 : a[j] / b[j];
                    break;
                case SHADER_MIN:
                case SHADER_UNION: v[i][j] = fminf(a[j], b[j]); break;
                case SHADER_MAX:
                case SHADER_INTERSECTION: v[i][j] = fmaxf(a[j], b[j]); break;
                case SHADER_DIFFERENCE: v[i][j] = fmaxf(a[j], -b[j]); break;
                case SHADER_ABS: v[i][j] = fabsf(a[j]); break;
                case SHADER_SIN: v[i][j] = sinf(a[j]); break;
                case SHADER_COS: v[i][j] = cosf(a[j]); break;
                case SHADER_FRACT: v[i][j] = a[j] - floorf(a[j]); break;
                case SHADER_MIX: v[i][j] = a[j] + (b[j] - a[j]) * c[j]; break;
                case SHADER_SELECT: v[i][j] = c[0] != 0 ? b[j] : a[j]; break;
                case SHADER_LENGTH: {
                    float sum = 0;
                    for (int k = 0; k < g->nodes[n->input[0]].width; k++) {
                        sum += a[k] * a[k];
                    }
                    v[i][j] = sqrtf(sum);
                    break;
                }
                case SHADER_DOT: {
                    float sum = 0;
                    for (int k = 0; k < g->nodes[n->input[0]].width; k++) {
                        sum += a[k] * b[k];
                    }
                    v[i][j] = sum;
                    break;
                }
                case SHADER_CIRCLE: v[i][j] = hypotf(a[0], a[1]) - b[0]; break;
                case SHADER_BOX: {
                    float x = fabsf(a[0]) - b[0], y = fabsf(a[1]) - b[1];
                    v[i][j] = hypotf(fmaxf(x, 0), fmaxf(y, 0)) + fminf(fmaxf(x, y), 0);
                    break;
                }
                case SHADER_SMOOTH_UNION:
                    v[i][j] = smooth_union(a[0], b[0], c[0]);
                    break;
                case SHADER_VECTOR2: v[i][j] = j ? b[0] : a[0]; break;
                case SHADER_VECTOR4: v[i][j] = v[n->input[j]][0]; break;
                case SHADER_X:
                case SHADER_Y:
                case SHADER_Z:
                case SHADER_W: v[i][j] = a[n->op - SHADER_X]; break;
                case SHADER_OP_COUNT: assert(false);
            }
        }
    }
    memset(result, 0, sizeof(float) * SHADER_OUTPUT_MAX * 4);
    for (int i = 0; i < g->output_count; i++) {
        memcpy(result[i], v[g->output[i]], sizeof result[i]);
    }
    free(v);
    return true;
}
static void append(char **s, const char *fmt, ...)
{
    va_list a, b;
    va_start(a, fmt);
    va_copy(b, a);
    int n = vsnprintf(NULL, 0, fmt, a), old = acount(*s);
    va_end(a);
    asetlen(*s, old + n + 1);
    vsnprintf(*s + old, (size_t)n + 1, fmt, b);
    va_end(b);
    asetlen(*s, old + n);
}
char *shader_graph_glsl_named(const shader_graph *g, const char *prefix, shader_error *error)
{
    if (!shader_graph_validate(g, error)) {
        return NULL;
    }
    if (!prefix || (!isalpha((unsigned char)*prefix) && *prefix != '_')) {
        fail(error, 0, "Invalid GLSL function name");
        return NULL;
    }
    for (const char *p = prefix; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') {
            fail(error, 0, "Invalid GLSL function name");
            return NULL;
        }
    }
    char *s = NULL;
    append(&s, "void %s(vec2 sg_position, vec2 sg_world_position, float sg_time, vec4 sg_params[32], out vec4 sg_out[8]) {\n", prefix);
    for (int i = 0; i < acount(g->nodes); i++) {
        const shader_node *n = &g->nodes[i];
        char a[32] = "", b[32] = "", c[32] = "";
        char *args[] = { a, b, c };
        for (int j = 0; j < 3 && j < shader_op_inputs(n->op); j++) {
            snprintf(args[j], 32, "v%d%s", n->input[j], g->nodes[n->input[j]].width == 1 ? ".xxxx" : "");
        }
        append(&s, "vec4 v%d = ", i);
        switch (n->op) {
            case SHADER_CONSTANT:
                append(
                    &s,
                    "vec4(%.9e,%.9e,%.9e,%.9e)",
                    n->value[0],
                    n->value[1],
                    n->value[2],
                    n->value[3]
                );
                break;
            case SHADER_PARAMETER:
                append(&s, "sg_params[%d]", n->parameter);
                break;
            case SHADER_POSITION:
                append(&s, "vec4(sg_position,0.0,0.0)");
                break;
            case SHADER_TIME: append(&s, "vec4(sg_time)"); break;
            case SHADER_WORLD_POSITION:
                append(&s, "vec4(sg_world_position,0.0,0.0)");
                break;
            case SHADER_ADD:
            case SHADER_SUBTRACT:
            case SHADER_MULTIPLY:
                append(
                    &s,
                    "%s %s %s",
                    a,
                    n->op == SHADER_ADD        ? "+"
                    : n->op == SHADER_SUBTRACT ? "-"
                                               : "*",
                    b
                );
                break;
            case SHADER_DIVIDE:
                append(&s, "vec4(");
                for (int j = 0; j < 4; j++) {
                    append(&s, "%s(%s[%d] == 0.0 ? 0.0 : %s[%d] / %s[%d])", j ? "," : "", b, j, a, j, b, j);
                }
                append(&s, ")");
                break;
            case SHADER_MIN:
            case SHADER_UNION: append(&s, "min(%s,%s)", a, b); break;
            case SHADER_MAX:
            case SHADER_INTERSECTION: append(&s, "max(%s,%s)", a, b); break;
            case SHADER_DIFFERENCE: append(&s, "max(%s,-%s)", a, b); break;
            case SHADER_ABS:
            case SHADER_SIN:
            case SHADER_COS:
            case SHADER_FRACT:
                append(
                    &s,
                    "%s(%s)",
                    n->op == SHADER_ABS   ? "abs"
                    : n->op == SHADER_SIN ? "sin"
                    : n->op == SHADER_COS ? "cos"
                                          : "fract",
                    a
                );
                break;
            case SHADER_LENGTH:
            case SHADER_DOT: {
                const char *sw[] = { "", ".x", ".xy", ".xyz", "" };
                int w = g->nodes[n->input[0]].width;
                if (n->op == SHADER_LENGTH) {
                    append(&s, "vec4(length(v%d%s))", n->input[0], sw[w]);
                } else {
                    append(&s, "vec4(dot(v%d%s,v%d%s))", n->input[0], sw[w], n->input[1], sw[w]);
                }
                break;
            }
            case SHADER_MIX: append(&s, "mix(%s,%s,%s)", a, b, c); break;
            case SHADER_SELECT:
                append(&s, "(%s.x != 0.0 ? %s : %s)", c, b, a);
                break;
            case SHADER_CIRCLE:
                append(&s, "vec4(length(%s.xy)-%s.x)", a, b);
                break;
            case SHADER_BOX:
                append(
                    &s,
                    "vec4(length(max(abs(%s.xy)-%s.xy,vec2(0.0)))+min(max(abs(%"
                    "s.x)-%s.x,abs(%s.y)-%s.y),0.0))",
                    a,
                    b,
                    a,
                    b,
                    a,
                    b
                );
                break;
            case SHADER_SMOOTH_UNION:
                append(
                    &s,
                    "min(%s,%s);\nif (%s.x > 0.0) { float h = "
                    "clamp(0.5+0.5*(%s.x-%s.x)/%s.x,0.0,1.0); v%d = "
                    "vec4(mix(%s.x,%s.x,h)-%s.x*h*(1.0-h)); }\n",
                    a,
                    b,
                    c,
                    b,
                    a,
                    c,
                    i,
                    b,
                    a,
                    c
                );
                continue;
            case SHADER_VECTOR2:
                append(&s, "vec4(%s.x,%s.x,0.0,0.0)", a, b);
                break;
            case SHADER_VECTOR4:
                append(
                    &s,
                    "vec4(v%d.x,v%d.x,v%d.x,v%d.x)",
                    n->input[0],
                    n->input[1],
                    n->input[2],
                    n->input[3]
                );
                break;
            case SHADER_X:
            case SHADER_Y:
            case SHADER_Z:
            case SHADER_W:
                append(&s, "vec4(%s.%c)", a, "xyzw"[n->op - SHADER_X]);
                break;
            case SHADER_OP_COUNT: assert(false);
        }
        append(&s, ";\n");
    }
    for (int i = 0; i < SHADER_OUTPUT_MAX; i++) {
        if (i < g->output_count) {
            append(&s, "sg_out[%d]=v%d;\n", i, g->output[i]);
        } else {
            append(&s, "sg_out[%d]=vec4(0.0);\n", i);
        }
    }
    append(&s, "}\n");
    char *result = xmalloc((size_t)acount(s) + 1);
    memcpy(result, s, (size_t)acount(s) + 1);
    afree(s);
    return result;
}
