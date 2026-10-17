#include "runtime/shaders/detail.h"

#include "runtime/shaders/image.h"
#include "support/array.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool fail(shader_error *error, uint64_t node, const char *message)
{
    if (error) {
        error->node = node;
        snprintf(error->message, sizeof error->message, "%s", message);
    }
    return false;
}
bool shader_detail_op(shader_op op)
{
    return op >= SHADER_PLANAR_MAPPING && op <= SHADER_DECAL_MASK;
}
static bool derivative(shader_op op)
{
    return shader_detail_op(op) && op != SHADER_PLANAR_MAPPING &&
           op != SHADER_CYLINDRICAL_MAPPING;
}
static int sampled_root(const shader_node *n)
{
    return n->input[n->op == SHADER_NORMAL_MAP ? 1 : 0];
}
static void ancestors(const shader_graph *g, int root, bool reachable[SHADER_NODE_MAX])
{
    memset(reachable, 0, SHADER_NODE_MAX * sizeof *reachable);
    reachable[root] = true;
    for (int i = root; i >= 0; i--) {
        if (reachable[i]) {
            const shader_node *n = &g->nodes[i];
            for (int j = 0; j < shader_op_inputs(n->op); j++) {
                reachable[n->input[j]] = true;
            }
        }
    }
}
static int texture_samples(shader_op op)
{
    if (op == SHADER_IMAGE_TEXTURE || op == SHADER_IMAGE_TEXTURE_ALPHA) {
        return 1;
    }
    return op == SHADER_TRIPLANAR_TEXTURE || op == SHADER_TRIPLANAR_TEXTURE_ALPHA
             ? 3
             : 0;
}
static int sample_count(const shader_graph *g, bool textures)
{
    int cost[SHADER_NODE_MAX] = { 0 }, total = 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        const shader_node *n = &g->nodes[i];
        cost[i] = textures ? texture_samples(n->op) : derivative(n->op) ? 6 : 0;
        if (derivative(n->op)) {
            bool reachable[SHADER_NODE_MAX];
            ancestors(g, sampled_root(n), reachable);
            for (int j = 0; j <= sampled_root(n); j++) {
                if (reachable[j]) {
                    cost[i] += 6 * cost[j];
                }
            }
        }
        total += cost[i];
    }
    return total;
}
int shader_detail_texture_samples(const shader_graph *g)
{
    return sample_count(g, true);
}
int shader_detail_expression_samples(const shader_graph *g)
{
    return sample_count(g, false);
}
bool shader_detail_validate(const shader_graph *g, const shader_node *n, shader_error *error)
{
    int a = g->nodes[n->input[0]].width, b = g->nodes[n->input[1]].width,
        c = g->nodes[n->input[2]].width;
    bool valid;
    switch (n->op) {
        case SHADER_PLANAR_MAPPING:
        case SHADER_CYLINDRICAL_MAPPING:
            valid = n->width == 2 && a == 3 && b == 2 && c == 2 && n->value[0] >= 0 &&
                    n->value[0] <= 2 && floorf(n->value[0]) == n->value[0];
            break;
        case SHADER_NORMAL_MAP:
            valid = n->width == 3 && a == 3 && b == 2 && c == 1 &&
                    g->nodes[n->input[3]].width == 1 &&
                    (n->value[0] == SHADER_NORMAL_DIRECTX ||
                     n->value[0] == SHADER_NORMAL_OPENGL ||
                     n->value[0] == SHADER_NORMAL_OPENGL_TANGENT);
            break;
        case SHADER_BUMP:
            valid = n->width == 3 && a == 1 && b == 1 && c == 1 &&
                    g->nodes[n->input[3]].width == 3;
            break;
        case SHADER_TRIPLANAR_TEXTURE:
        case SHADER_TRIPLANAR_TEXTURE_ALPHA:
            valid = n->width == (n->op == SHADER_TRIPLANAR_TEXTURE ? 3 : 1) &&
                    a == 3 && b == 1 && c == 1 && n->image >= 0 &&
                    n->image < acount(g->images) &&
                    shader_image_source_valid(&g->images[n->image]);
            break;
        case SHADER_DECAL_MASK:
            valid = n->width == 1 && a == 3 && b == 3 && c == 1 &&
                    g->nodes[n->input[3]].width == 1;
            break;
        default: return true;
    }
    if (!valid) {
        return fail(error, n->source_id, "Surface detail has invalid input widths or settings");
    }
    if (!derivative(n->op)) {
        return true;
    }
    bool reachable[SHADER_NODE_MAX];
    ancestors(g, sampled_root(n), reachable);
    for (int i = 0; i <= sampled_root(n); i++) {
        bool texture_height = n->op == SHADER_BUMP &&
                              (g->nodes[i].op == SHADER_TRIPLANAR_TEXTURE ||
                               g->nodes[i].op == SHADER_TRIPLANAR_TEXTURE_ALPHA);
        if (reachable[i] && derivative(g->nodes[i].op) && !texture_height) {
            return fail(error, n->source_id, "Surface detail cannot sample a nested derivative expression");
        }
    }
    int spacing = n->op == SHADER_NORMAL_MAP ? 3
                : n->op == SHADER_DECAL_MASK ? -1
                                             : 2;
    if (spacing >= 0 && g->nodes[n->input[spacing]].op == SHADER_CONSTANT &&
        g->nodes[n->input[spacing]].value[0] <= 0) {
        return fail(error, n->source_id, "Surface detail Distance must be positive world units");
    }
    if (n->op == SHADER_NORMAL_MAP) {
        for (int i = 0; i <= sampled_root(n); i++) {
            if (reachable[i] && g->nodes[i].op == SHADER_CYLINDRICAL_MAPPING &&
                i != sampled_root(n)) {
                return fail(error, n->source_id, "Normal Map angular UV must connect directly from Cylindrical Mapping; use its Scale and Offset pins");
            }
        }
        ancestors(g, n->input[0], reachable);
        for (int i = 0; i <= n->input[0]; i++) {
            const shader_node *source = &g->nodes[i];
            if (reachable[i] &&
                (source->op == SHADER_IMAGE_TEXTURE ||
                 source->op == SHADER_TRIPLANAR_TEXTURE) &&
                g->images[source->image].color_space != SHADER_IMAGE_LINEAR) {
                return fail(error, n->source_id, "Normal Map image data must use Linear color space");
            }
        }
    }
    return true;
}
static float dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
static void cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
static void normalize(const float value[3], const float fallback[3], float out[3])
{
    float scale = fmaxf(fabsf(value[0]), fmaxf(fabsf(value[1]), fabsf(value[2])));
    if (!isfinite(scale) || scale == 0) {
        if (out != fallback) {
            memcpy(out, fallback, 3 * sizeof(float));
        }
        return;
    }
    float scaled[3] = { value[0] / scale, value[1] / scale, value[2] / scale };
    float length = sqrtf(dot(scaled, scaled));
    for (int i = 0; i < 3; i++) {
        out[i] = scaled[i] / length;
    }
}
static void shift(shader_sample_context *out, const shader_sample_context *sample, int axis, float distance)
{
    *out = *sample;
    out->world_position[axis] += distance;
    for (int i = 0; i < 3; i++) {
        out->object_position[i] += sample->object_from_world[i][axis] * distance;
        out->part_position[i] += sample->part_from_world[i][axis] * distance;
    }
    out->position[0] = out->object_position[0];
    out->position[1] = out->object_position[2];
    for (int i = 0; i < 2; i++) {
        out->uv[i] += sample->uv_from_world[i][axis] * distance;
    }
}
static bool gradient(
    const shader_graph *g,
    const shader_node *n,
    const shader_sample_context *sample,
    const float (*parameters)[4],
    float distance,
    float period,
    float result[3][3],
    shader_error *error
)
{
    if (!isfinite(distance) || distance <= 0) {
        return fail(error, n->source_id, "Surface detail Distance must be finite and positive");
    }
    bool borrowed = sample->derivative_scratch &&
                    sample->derivative_capacity >= acount(g->nodes);
    float (*scratch)[4] = borrowed
                            ? sample->derivative_scratch
                            : xmalloc((size_t)acount(g->nodes) * sizeof *scratch);
    bool ok = true;
    int root = sampled_root(n), width = g->nodes[root].width;
    for (int axis = 0; axis < 3 && ok; axis++) {
        shader_sample_context positive, negative;
        shift(&positive, sample, axis, distance);
        shift(&negative, sample, axis, -distance);
        if (borrowed) {
            positive.derivative_scratch = negative.derivative_scratch = scratch +
                                                                        acount(g->nodes);
            positive.derivative_capacity = negative.derivative_capacity = sample->derivative_capacity -
                                                                          acount(g->nodes);
        }
        float a[4], b[4];
        ok = shader_graph_eval_node_scratch(g, &positive, parameters, root, scratch, a, error) &&
             shader_graph_eval_node_scratch(g, &negative, parameters, root, scratch, b, error);
        for (int j = 0; j < width && ok; j++) {
            float delta = a[j] - b[j];
            /* Repeating UVs cross cylindrical seams without a false giant slope. */
            if (n->op == SHADER_NORMAL_MAP && j == 0 && period > 0 &&
                fabsf(delta) > .5f * period) {
                delta -= roundf(delta / period) * period;
            }
            result[j][axis] = delta / (2 * distance);
            if (!isfinite(result[j][axis])) {
                ok = fail(error, n->source_id, "Surface detail derivative is not finite");
            }
        }
    }
    if (!borrowed) {
        free(scratch);
    }
    return ok;
}
static float smooth(float x)
{
    x = fminf(1, fmaxf(0, x));
    return x * x * (3 - 2 * x);
}
bool shader_detail_eval(
    const shader_graph *g,
    const shader_node *n,
    const shader_sample_context *sample,
    const float (*parameters)[4],
    const float (*values)[4],
    float out[4],
    shader_error *error
)
{
    const float *a = values[n->input[0]], *b = values[n->input[1]],
                *c = values[n->input[2]];
    const float *d = shader_op_inputs(n->op) == 4 ? values[n->input[3]] : NULL;
    if (n->op == SHADER_PLANAR_MAPPING || n->op == SHADER_CYLINDRICAL_MAPPING) {
        int axis = (int)n->value[0];
        float u = axis == 0 ? a[2] : a[0], v = axis == 1 ? a[2] : -a[1];
        if (n->op == SHADER_CYLINDRICAL_MAPPING) {
            int first = (axis + 1) % 3, second = (axis + 2) % 3;
            u = atan2f(a[second], a[first]) / 6.28318530718f + .5f;
            v = a[axis];
        }
        out[0] = u * b[0] + c[0];
        out[1] = v * b[1] + c[1];
        return true;
    }
    const float up[3] = { 0, 1, 0 };
    float normal[3];
    normalize(n->op == SHADER_BUMP ? d : sample->normal, up, normal);
    float spacing = n->op == SHADER_NORMAL_MAP ? d[0]
                  : n->op == SHADER_DECAL_MASK ? .001f
                                               : c[0];
    if ((n->op == SHADER_NORMAL_MAP && c[0] == 0) ||
        (n->op == SHADER_BUMP && b[0] == 0)) {
        memcpy(out, normal, sizeof normal);
        return true;
    }
    float jacobian[3][3] = { 0 };
    const shader_node *root = &g->nodes[sampled_root(n)];
    float period = n->op == SHADER_NORMAL_MAP && root->op == SHADER_CYLINDRICAL_MAPPING
                     ? fabsf(values[root->input[1]][0])
                     : 0;
    if (!gradient(g, n, sample, parameters, spacing, period, jacobian, error)) {
        return false;
    }
    if (n->op == SHADER_BUMP) {
        float tangent[3], along = dot(jacobian[0], normal);
        for (int i = 0; i < 3; i++) {
            tangent[i] = normal[i] - b[0] * (jacobian[0][i] - normal[i] * along);
        }
        normalize(tangent, normal, out);
        return true;
    }
    if (n->op == SHADER_NORMAL_MAP) {
        float tangent[3], du[3], dv[3];
        float along_u = dot(jacobian[0], normal), along_v = dot(jacobian[1], normal);
        for (int i = 0; i < 3; i++) {
            du[i] = jacobian[0][i] - along_u * normal[i];
            dv[i] = jacobian[1][i] - along_v * normal[i];
        }
        float largest = 0;
        for (int i = 0; i < 3; i++) {
            largest = fmaxf(largest, fmaxf(fabsf(du[i]), fabsf(dv[i])));
        }
        if (largest == 0) {
            memcpy(out, normal, sizeof normal);
            return true;
        }
        for (int i = 0; i < 3; i++) {
            du[i] /= largest;
            dv[i] /= largest;
        }
        if (n->value[0] == SHADER_NORMAL_OPENGL_TANGENT) {
            const float zero[3] = { 0 };
            float bitangent[3];
            normalize(du, zero, du);
            cross(normal, du, bitangent);
            float sign = dot(bitangent, dv) < 0 ? -1 : 1;
            for (int i = 0; i < 3; i++) {
                dv[i] = bitangent[i] * sign;
            }
        }
        float scale = sqrtf(fmaxf(dot(du, du), dot(dv, dv)));
        if (scale == 0) {
            memcpy(out, normal, sizeof normal);
            return true;
        }
        float x = (2 * a[0] - 1) * c[0],
              y = (2 * a[1] - 1) * c[0] *
                  (n->value[0] != SHADER_NORMAL_DIRECTX ? -1 : 1);
        for (int i = 0; i < 3; i++) {
            tangent[i] = (du[i] * x + dv[i] * y) / scale + normal[i] * (2 * a[2] - 1);
        }
        normalize(tangent, normal, out);
        return true;
    }
    if (n->op == SHADER_DECAL_MASK) {
        if (b[0] <= 0 || b[1] <= 0 || b[2] <= 0 || c[0] < 0 || d[0] < -1 || d[0] > 1) {
            return fail(error, n->source_id, "Decal Mask requires positive Half Size, nonnegative Feather and Facing in [-1,1]");
        }
        float edge = fmaxf(fabsf(a[0]) - b[0], fmaxf(fabsf(a[1]) - b[1], fabsf(a[2]) - b[2]));
        out[0] = c[0] > 0 ? 1 - smooth((edge + c[0]) / c[0]) : edge <= 0;
        if (d[0] > -1) {
            float facing[3] = { 0 };
            normalize(jacobian[2], facing, facing);
            if (dot(normal, facing) < d[0]) {
                out[0] = 0;
            }
        }
        return true;
    }
    float matrix_scale = 0;
    for (int row = 0; row < 3; row++) {
        for (int axis = 0; axis < 3; axis++) {
            matrix_scale = fmaxf(matrix_scale, fabsf(jacobian[row][axis]));
        }
    }
    if (matrix_scale == 0 || !isfinite(b[0]) || b[0] <= 0) {
        return fail(error, n->source_id, "Triplanar Texture requires nonsingular Position mapping and positive Sharpness");
    }
    for (int row = 0; row < 3; row++) {
        for (int axis = 0; axis < 3; axis++) {
            jacobian[row][axis] /= matrix_scale;
        }
    }
    float cofactors[3][3];
    cross(jacobian[1], jacobian[2], cofactors[0]);
    cross(jacobian[2], jacobian[0], cofactors[1]);
    cross(jacobian[0], jacobian[1], cofactors[2]);
    float det = dot(jacobian[0], cofactors[0]), weights[3] = { 0 }, sum = 0,
          largest = 0;
    if (det == 0) {
        return fail(error, n->source_id, "Triplanar Texture requires nonsingular Position mapping");
    }
    for (int i = 0; i < 3; i++) {
        weights[i] = fabsf(dot(normal, cofactors[i]));
        largest = fmaxf(largest, weights[i]);
    }
    if (largest == 0) {
        return fail(error, n->source_id, "Triplanar Texture has an unavailable surface frame");
    }
    for (int i = 0; i < 3; i++) {
        weights[i] = powf(weights[i] / largest, b[0]);
        sum += weights[i];
    }
    shader_image_view view = { 0 };
    const shader_image_source *source = &g->images[n->image];
    if (!sample->resolve_image ||
        !sample->resolve_image(sample->image_userdata, source, &view)) {
        return fail(error, n->source_id, "Triplanar Texture could not resolve its image");
    }
    float uv[3][2] = { { a[2], -a[1] }, { a[0], a[2] }, { a[0], -a[1] } };
    for (int i = 0; i < 3; i++) {
        float pixel[4];
        if (!shader_image_sample(source, &view, uv[i], pixel)) {
            return fail(error, n->source_id, "Triplanar Texture image sampling failed");
        }
        for (int channel = 0; channel < n->width; channel++) {
            out[channel] += pixel[n->op == SHADER_TRIPLANAR_TEXTURE_ALPHA ? 3 : channel] *
                            weights[i] / sum;
        }
    }
    return true;
}

static void append(char **text, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    int old = acount(*text);
    asetlen(*text, old + length + 1);
    vsnprintf(*text + old, (size_t)length + 1, format, args);
    va_end(args);
    asetlen(*text, old + length);
}
static char *finish(char *text)
{
    char *result = xmalloc((size_t)acount(text) + 1);
    memcpy(result, text, (size_t)acount(text) + 1);
    afree(text);
    return result;
}
static const char *common_glsl =
    "#ifndef SG_DETAIL_DEFINED\n#define SG_DETAIL_DEFINED\n"
    "vec3 sgd_normal(vec3 p,vec3 fallback) { float "
    "scale=max(abs(p.x),max(abs(p.y),abs(p.z))); "
    "if(scale==0.0||isnan(scale)||isinf(scale))return fallback;p/=scale;return "
    "p*inversesqrt(dot(p,p)); }\n"
    "SGSample sgd_shift(SGSample p,int axis,float distance) { vec3 "
    "delta=vec3(0); delta[axis]=distance; p.world_position+=delta; "
    "for(int "
    "i=0;i<3;i++){p.object_position[i]+=dot(p.object_from_world[i],delta);p."
    "part_position[i]+=dot(p.part_from_world[i],delta);} "
    "p.position=p.object_position.xz;for(int "
    "i=0;i<2;i++)p.uv[i]+=dot(p.uv_from_world[i],delta);return p;}\n"
    "vec2 sgd_planar(vec3 p,vec2 scale,vec2 offset,int axis) { vec2 "
    "uv=axis==0?vec2(p.z,-p.y):axis==1?p.xz:vec2(p.x,-p.y);return "
    "uv*scale+offset;}\n"
    "vec2 sgd_cylinder(vec3 p,vec2 scale,vec2 offset,int axis) { int "
    "a=(axis+1)%3,b=(axis+2)%3; float "
    "u=p[a]*p[a]+p[b]*p[b]>0?atan(p[b],p[a])/6.28318530718+0.5:0.5;return "
    "vec2(u,p[axis])*scale+offset;}\n"
    "vec3 sgd_map_normal(vec3 color,vec3 normal,vec3 du,vec3 dv,float "
    "strength,float convention) { "
    "du-=normal*dot(normal,du);dv-=normal*dot(normal,dv); "
    "float "
    "largest=max(max(abs(du.x),max(abs(du.y),abs(du.z))),max(abs(dv.x),max(abs("
    "dv.y),abs(dv.z)))); "
    "if(largest==0.0)return normal;du/=largest;dv/=largest;"
    "if(convention==2.0){du=sgd_normal(du,vec3(0));vec3 "
    "bitangent=cross(normal,du);"
    "dv=bitangent*(dot(bitangent,dv)<0.0?-1.0:1.0);}float "
    "scale=sqrt(max(dot(du,du),dot(dv,dv))); "
    "if(scale==0.0)return normal;"
    "vec3 p=color*2.0-1.0;p.xy*=strength;if(convention>0.5)p.y=-p.y; "
    "return sgd_normal((du*p.x+dv*p.y)/scale+normal*p.z,normal); }\n"
    "#endif\n";
static bool root_glsl(
    const shader_graph *g,
    int root,
    const char *name,
    const char *prefix,
    char **text,
    shader_error *error
)
{
    bool reachable[SHADER_NODE_MAX];
    ancestors(g, root, reachable);
    int remap[SHADER_NODE_MAX];
    shader_graph sub;
    shader_graph_init(&sub);
    sub.images = g->images; /* Borrowed resource identities; original functions sample them. */
    for (int i = 0; i <= root; i++) {
        if (!reachable[i]) {
            continue;
        }
        shader_node copy = g->nodes[i];
        for (int input = 0; input < shader_op_inputs(copy.op); input++) {
            copy.input[input] = remap[copy.input[input]];
        }
        remap[i] = acount(sub.nodes);
        apush(sub.nodes, copy); /* Ramp pointers are borrowed for synchronous emission. */
    }
    sub.output_count = 1;
    sub.output[0] = remap[root];
    char *source = shader_graph_glsl_named(&sub, name, error);
    afree(sub.nodes);
    if (!source) {
        return false;
    }
    for (int i = 0; i < acount(g->images); i++) {
        append(text, "#define %s_image_%d %s_image_%d\n", name, i, prefix, i);
    }
    append(text, "%s", source);
    free(source);
    for (int i = 0; i < acount(g->images); i++) {
        append(text, "#undef %s_image_%d\n", name, i);
    }
    return true;
}
char *shader_detail_glsl_helpers(const shader_graph *g, const char *prefix, shader_error *error)
{
    char *text = NULL;
    append(&text, "%s", common_glsl);
    for (int i = 0; i < acount(g->nodes); i++) {
        const shader_node *n = &g->nodes[i];
        if (!derivative(n->op)) {
            continue;
        }
        char name[160];
        snprintf(name, sizeof name, "%s_detail_root_%d", prefix, i);
        if (!root_glsl(g, sampled_root(n), name, prefix, &text, error)) {
            afree(text);
            return NULL;
        }
        append(&text, "vec4 %s_detail_%d(SGSample context,vec4 parameters[32],vec4 a,vec4 b,vec4 c,vec4 d,float period) {\n", prefix, i);
        append(&text, "vec3 normal=sgd_normal(%s,vec3(0,1,0));\n", n->op == SHADER_BUMP ? "d.xyz" : "context.normal");
        if (n->op == SHADER_BUMP || n->op == SHADER_NORMAL_MAP) {
            append(&text, "if(%s.x==0.0)return vec4(normal,0);\n", n->op == SHADER_BUMP ? "b" : "c");
        }
        const char *spacing = n->op == SHADER_NORMAL_MAP ? "d.x"
                            : n->op == SHADER_DECAL_MASK ? "0.001"
                                                         : "c.x";
        append(&text, "float spacing=%s;if(spacing<=0.0||isnan(spacing)||isinf(spacing))return vec4(normal,0);\n", spacing);
        append(
            &text,
            "vec3 row0=vec3(0),row1=vec3(0),row2=vec3(0);for(int "
            "axis=0;axis<3;axis++){vec4 positive[%d],negative[%d];\n",
            SHADER_OUTPUT_MAX,
            SHADER_OUTPUT_MAX
        );
        append(
            &text,
            "%s(sgd_shift(context,axis,spacing),parameters,positive);%s(sgd_"
            "shift(context,axis,-spacing),parameters,negative);\n",
            name,
            name
        );
        append(&text, "vec3 delta=positive[0].xyz-negative[0].xyz;\n");
        if (n->op == SHADER_NORMAL_MAP) {
            append(&text, "if(period>0.0&&abs(delta.x)>0.5*period)delta.x-=round(delta.x/period)*period;\n");
        }
        append(&text, "row0[axis]=delta.x/(2.0*spacing);row1[axis]=delta.y/(2.0*spacing);row2[axis]=delta.z/(2.0*spacing);}\n");
        switch (n->op) {
            case SHADER_BUMP:
                append(&text, "return vec4(sgd_normal(normal-b.x*(row0-normal*dot(normal,row0)),normal),0);}\n");
                break;
            case SHADER_NORMAL_MAP:
                append(&text, "return vec4(sgd_map_normal(a.xyz,normal,row0,row1,c.x,%.9e),0);}\n", n->value[0]);
                break;
            case SHADER_DECAL_MASK:
                append(
                    &text,
                    "if(any(lessThanEqual(b.xyz,vec3(0)))||c.x<0.0||d.x < "
                    "-1.0||d.x>1.0)return vec4(0); "
                    "float "
                    "edge=max(abs(a.x)-b.x,max(abs(a.y)-b.y,abs(a.z)-b.z)); "
                    "float "
                    "mask=c.x>0.0?1.0-smoothstep(-c.x,0.0,edge):(edge<=0.0?1.0:"
                    "0.0); "
                    "if(d.x > -1.0 && "
                    "dot(normal,sgd_normal(row2,vec3(0)))<d.x)mask=0.0;return "
                    "vec4(mask);}\n"
                );
                break;
            case SHADER_TRIPLANAR_TEXTURE:
            case SHADER_TRIPLANAR_TEXTURE_ALPHA:
                append(
                    &text,
                    "float "
                    "matrix_scale=max(max(max(abs(row0.x),abs(row0.y)),abs("
                    "row0.z)),max(max(max(abs(row1.x),abs(row1.y)),abs(row1.z))"
                    ",max(max(abs(row2.x),abs(row2.y)),abs(row2.z)))); "
                    "if(matrix_scale==0.0||b.x<=0.0||isnan(b.x)||isinf(b.x))"
                    "return "
                    "vec4(0);row0/=matrix_scale;row1/=matrix_scale;row2/"
                    "=matrix_scale; "
                    "vec3 "
                    "co0=cross(row1,row2),co1=cross(row2,row0),co2=cross(row0,"
                    "row1);float det=dot(row0,co0); "
                    "if(det==0.0)return vec4(0);vec3 "
                    "weights=abs(vec3(dot(normal,co0),dot(normal,co1),dot("
                    "normal,co2))); "
                    "float "
                    "largest=max(weights.x,max(weights.y,weights.z));if("
                    "largest==0.0)return vec4(0); "
                    "weights=pow(weights/largest,vec3(b.x));weights/"
                    "=weights.x+weights.y+weights.z;\n"
                );
                append(
                    &text,
                    "vec4 "
                    "color=%s_image_%d(vec2(a.z,-a.y))*weights.x+%s_image_%d(a."
                    "xz)*weights.y+%s_image_%d(vec2(a.x,-a.y))*weights.z;\n",
                    prefix,
                    n->image,
                    prefix,
                    n->image,
                    prefix,
                    n->image
                );
                append(&text, "return %s;}\n", n->op == SHADER_TRIPLANAR_TEXTURE ? "vec4(color.rgb,0)" : "vec4(color.a)");
                break;
            default: break;
        }
    }
    return finish(text);
}
char *shader_detail_glsl_expression(
    const shader_graph *g,
    const shader_node *n,
    const char *prefix,
    int index,
    const char *a,
    const char *b,
    const char *c,
    const char *d
)
{
    char *text = NULL;
    if (n->op == SHADER_PLANAR_MAPPING || n->op == SHADER_CYLINDRICAL_MAPPING) {
        append(
            &text,
            "vec4(%s(%s.xyz,%s.xy,%s.xy,%d),0,0)",
            n->op == SHADER_PLANAR_MAPPING ? "sgd_planar" : "sgd_cylinder",
            a,
            b,
            c,
            (int)n->value[0]
        );
    } else {
        const shader_node *root = &g->nodes[sampled_root(n)];
        char period[48] = "0.0";
        if (n->op == SHADER_NORMAL_MAP && root->op == SHADER_CYLINDRICAL_MAPPING) {
            snprintf(period, sizeof period, "abs(v%d.x)", root->input[1]);
        }
        append(
            &text,
            "%s_detail_%d(sg_sample,sg_params,%s,%s,%s,%s,%s)",
            prefix,
            index,
            a,
            b,
            c,
            *d ? d : "vec4(0)",
            period
        );
    }
    return finish(text);
}
