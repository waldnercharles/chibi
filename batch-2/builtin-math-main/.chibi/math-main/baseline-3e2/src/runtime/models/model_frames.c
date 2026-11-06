#include "runtime/models/model_program_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void append(char **text, const char *format, ...)
{
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    int offset = acount(*text);
    asetlen(*text, offset + n + 1);
    vsnprintf(*text + offset, n + 1, format, args);
    asetlen(*text, offset + n);
    va_end(args);
}
static void require(const shader_graph *g, bool *used, int root)
{
    if (used[root]) {
        return;
    }
    used[root] = true;
    for (int i = 0; i < shader_op_inputs(g->nodes[root].op); i++) {
        require(g, used, g->nodes[root].input[i]);
    }
}
static bool affine_graph(const shader_graph *g, shader_error *error)
{
    bool *spatial = xcalloc(acount(g->nodes), sizeof *spatial);
    bool valid = true;
    for (int i = 0; valid && i < acount(g->nodes); i++) {
        const shader_node *n = &g->nodes[i];
        spatial[i] = n->op == SHADER_OBJECT_POSITION;
        for (int j = 0; j < shader_op_inputs(n->op); j++) {
            spatial[i] |= spatial[n->input[j]];
        }
        if (!spatial[i]) {
            continue;
        }
        switch (n->op) {
            case SHADER_OBJECT_POSITION:
            case SHADER_ADD:
            case SHADER_SUBTRACT:
            case SHADER_X:
            case SHADER_Y:
            case SHADER_Z:
            case SHADER_W:
            case SHADER_VECTOR2:
            case SHADER_VECTOR3:
            case SHADER_VECTOR4: break;
            case SHADER_MULTIPLY:
            case SHADER_DOT:
                if (spatial[n->input[0]] && spatial[n->input[1]]) {
                    valid = model_fail(error, n->source_id, "Part coordinate frame is not affine");
                }
                break;
            case SHADER_DIVIDE:
                if (spatial[n->input[1]]) {
                    valid = model_fail(error, n->source_id, "Part coordinate frame is not affine");
                }
                break;
            default:
                valid = model_fail(error, n->source_id, "Part coordinate frame is not affine");
                break;
        }
    }
    free(spatial);
    return valid;
}
bool model_build_frames(model_program *p, shader_error *error)
{
    const shader_graph *g = &p->shader;
    int count = acount(g->nodes);
    bool *used = xcalloc(count, sizeof *used);
    int *remap = xmalloc(count * sizeof *remap);
    for (int part = 0; part < acount(p->parts); part++) {
        shader_graph frame;
        shader_graph_init(&frame);
        int root = p->parts[part].position;
        memset(used, 0, count * sizeof *used);
        if (root >= 0) {
            require(g, used, root);
        }
        for (int i = 0; i < count; i++) {
            remap[i] = -1;
            if (!used[i]) {
                continue;
            }
            shader_node n = g->nodes[i];
            assert(!n.ramp);
            for (int j = 0; j < shader_op_inputs(n.op); j++) {
                n.input[j] = remap[n.input[j]];
            }
            remap[i] = acount(frame.nodes);
            apush(frame.nodes, n);
        }
        if (root >= 0) {
            frame.output_count = 1;
            frame.output[0] = remap[root];
        }
        apush(p->bounds_program->part_frames, frame);
        if (root >= 0 && (!shader_graph_validate(&frame, error) ||
                          !affine_graph(&frame, error))) {
            free(remap);
            free(used);
            return false;
        }
    }
    free(remap);
    free(used);
    return true;
}
static float component(const shader_graph *g, const float (*values)[4], int i, int j)
{
    return values[i][g->nodes[i].width == 1 ? 0 : j];
}
static bool derivative(const shader_graph *g, const float (*v)[4], int axis, float (*d)[4], shader_error *error)
{
    for (int i = 0; i < acount(g->nodes); i++) {
        const shader_node *n = &g->nodes[i];
        int a = n->input[0], b = n->input[1];
        if (n->op == SHADER_OBJECT_POSITION) {
            d[i][axis] = 1;
            continue;
        }
        bool active = false;
        for (int input = 0; input < shader_op_inputs(n->op); input++) {
            for (int j = 0; j < g->nodes[n->input[input]].width; j++) {
                active |= d[n->input[input]][j] != 0;
            }
        }
        if (!active) {
            continue;
        }
        for (int j = 0; j < n->width; j++) {
            float da = component(g, (const float (*)[4])d, a, j);
            float db = b >= 0 ? component(g, (const float (*)[4])d, b, j) : 0;
            float va = component(g, v, a, j), vb = b >= 0 ? component(g, v, b, j) : 0;
            switch (n->op) {
                case SHADER_ADD: d[i][j] = da + db; break;
                case SHADER_SUBTRACT: d[i][j] = da - db; break;
                case SHADER_MULTIPLY: d[i][j] = da * vb + va * db; break;
                case SHADER_DIVIDE:
                    /* Affine validation guarantees a nonspatial denominator. */
                    d[i][j] = da / vb;
                    break;
                case SHADER_X:
                case SHADER_Y:
                case SHADER_Z:
                case SHADER_W: d[i][j] = d[a][n->op - SHADER_X]; break;
                case SHADER_VECTOR2:
                case SHADER_VECTOR3:
                case SHADER_VECTOR4: d[i][j] = d[n->input[j]][0]; break;
                case SHADER_DOT:
                    for (int k = 0; k < g->nodes[a].width; k++) {
                        d[i][j] += d[a][k] * v[b][k] + v[a][k] * d[b][k];
                    }
                    break;
                default:
                    return model_fail(error, n->source_id, "Part coordinate frame is not affine");
            }
            if (!isfinite(d[i][j])) {
                return model_fail(error, n->source_id, "Part coordinate frame exceeds finite arithmetic");
            }
        }
    }
    return true;
}
bool model_program_part_point(
    const model_program *p,
    const float prepared[32][4],
    int part,
    const float point[3],
    float local[3],
    float rows[3][3],
    shader_error *error
)
{
    float defaults[32][4];
    model_bounds bounds;
    if (!prepared) {
        if (!model_program_prepare(p, NULL, defaults, &bounds, error)) {
            return false;
        }
        prepared = defaults;
    }
    for (int i = 0; i < 3; i++) {
        if (!isfinite(point[i])) {
            return model_fail(error, 0, "Part frame point must be finite");
        }
    }
    if (part < -1 || part >= acount(p->parts)) {
        return model_fail(error, 0, "Part frame owner is outside compiled table");
    }
    memset(rows, 0, 9 * sizeof(float));
    if (part < 0 || !p->bounds_program->part_frames[part].output_count) {
        memcpy(local, point, 3 * sizeof(float));
        for (int i = 0; i < 3; i++) {
            rows[i][i] = 1;
        }
        return true;
    }
    const shader_graph *g = &p->bounds_program->part_frames[part];
    int count = acount(g->nodes), root = g->output[0];
    float (*values)[4] = xcalloc(count, sizeof *values),
          (*d)[4] = xcalloc(count, sizeof *d);
    shader_sample_context sample = { 0 };
    memcpy(sample.object_position, point, sizeof sample.object_position);
    bool valid = shader_graph_eval_values(g, &sample, prepared, values, error);
    for (int axis = 0; valid && axis < 3; axis++) {
        memset(d, 0, count * sizeof *d);
        valid = derivative(g, (const float (*)[4])values, axis, d, error);
        for (int j = 0; j < 3; j++) {
            rows[j][axis] = d[root][j];
        }
    }
    if (valid) {
        memcpy(local, values[root], 3 * sizeof(float));
    }
    free(values);
    free(d);
    return valid;
}
bool model_program_frame(
    const model_program *p,
    int part,
    const float prepared[32][4],
    float rows[3][3],
    shader_error *error
)
{
    float point[3] = { 0 }, local[3];
    return model_program_part_point(p, prepared, part, point, local, rows, error);
}
static void argument(char out[32], const shader_graph *g, int input, char kind, int axis)
{
    if (kind == 'v') {
        snprintf(out, 32, "v%d%s", input, g->nodes[input].width == 1 ? ".xxxx" : "");
    } else {
        snprintf(out, 32, "d%d_%d%s", input, axis, g->nodes[input].width == 1 ? ".xxxx" : "");
    }
}
char *model_program_frame_glsl(const model_program *p, const char *prefix, shader_error *error)
{
    char *text = NULL;
    for (int part = 0; part < acount(p->parts); part++) {
        const shader_graph *g = &p->bounds_program->part_frames[part];
        if (!g->output_count) {
            continue;
        }
        char name[96];
        snprintf(name, sizeof name, "%s_part_%d", prefix, part);
        char *primal = shader_graph_glsl_named(g, name, error);
        if (!primal) {
            afree(text);
            return NULL;
        }
        primal[strlen(primal) - 2] = 0;
        append(&text, "%s", primal);
        free(primal);
        bool *active = xcalloc(acount(g->nodes), sizeof *active);
        for (int i = 0; i < acount(g->nodes); i++) {
            const shader_node *n = &g->nodes[i];
            active[i] = n->op == SHADER_OBJECT_POSITION;
            for (int j = 0; j < shader_op_inputs(n->op); j++) {
                active[i] |= active[n->input[j]];
            }
            for (int axis = 0; axis < 3; axis++) {
                append(&text, "vec4 d%d_%d=", i, axis);
                if (!active[i]) {
                    append(&text, "vec4(0);\n");
                    continue;
                }
                if (n->op == SHADER_OBJECT_POSITION) {
                    append(&text, "vec4(%d,%d,%d,0);\n", axis == 0, axis == 1, axis == 2);
                    continue;
                }
                char a[32], b[32], da[32], db[32];
                argument(a, g, n->input[0], 'v', axis);
                argument(da, g, n->input[0], 'd', axis);
                if (shader_op_inputs(n->op) > 1) {
                    argument(b, g, n->input[1], 'v', axis);
                    argument(db, g, n->input[1], 'd', axis);
                }
                switch (n->op) {
                    case SHADER_ADD: append(&text, "%s+%s", da, db); break;
                    case SHADER_SUBTRACT: append(&text, "%s-%s", da, db); break;
                    case SHADER_MULTIPLY:
                        append(&text, "%s*%s+%s*%s", da, b, a, db);
                        break;
                    case SHADER_DIVIDE: append(&text, "%s/%s", da, b); break;
                    case SHADER_DOT:
                        append(&text, "vec4(dot(%s.xyz,%s.xyz)+dot(%s.xyz,%s.xyz),0,0,0)", da, b, a, db);
                        break;
                    case SHADER_X:
                    case SHADER_Y:
                    case SHADER_Z:
                    case SHADER_W:
                        append(&text, "vec4(d%d_%d.%c,0,0,0)", n->input[0], axis, "xyzw"[n->op - SHADER_X]);
                        break;
                    case SHADER_VECTOR2:
                    case SHADER_VECTOR3:
                    case SHADER_VECTOR4:
                        append(&text, "vec4(");
                        for (int j = 0; j < 4; j++) {
                            if (j) {
                                append(&text, ",");
                            }
                            if (j < n->width) {
                                append(&text, "d%d_%d.x", n->input[j], axis);
                            } else {
                                append(&text, "0");
                            }
                        }
                        append(&text, ")");
                        break;
                    default:
                        free(active);
                        afree(text);
                        model_fail(error, n->source_id, "Part coordinate frame is not affine");
                        return NULL;
                }
                append(&text, ";\n");
            }
        }
        int root = g->output[0];
        for (int row = 0; row < 3; row++) {
            append(
                &text,
                "sg_out[%d]=vec4(d%d_0.%c,d%d_1.%c,d%d_2.%c,0);\n",
                row + 2,
                root,
                "xyz"[row],
                root,
                "xyz"[row],
                root,
                "xyz"[row]
            );
        }
        append(&text, "}\n");
        free(active);
    }
    append(
        &text,
        "void %s(int part,SGSample context,vec4 values[32],out vec3 rows[3]) { "
        "rows[0]=vec3(1,0,0); rows[1]=vec3(0,1,0); rows[2]=vec3(0,0,1); vec4 "
        "result[%d]; switch(part) {\n",
        prefix,
        SHADER_OUTPUT_MAX
    );
    for (int part = 0; part < acount(p->parts); part++) {
        if (p->bounds_program->part_frames[part].output_count) {
            append(
                &text,
                "case %d: %s_part_%d(context,values,result); "
                "rows[0]=result[2].xyz; rows[1]=result[3].xyz; "
                "rows[2]=result[4].xyz; break;\n",
                part,
                prefix,
                part
            );
        }
    }
    append(&text, "default: break; } }\n");
    char *result = xmalloc(acount(text) + 1);
    memcpy(result, text, acount(text) + 1);
    afree(text);
    return result;
}
