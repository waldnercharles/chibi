#include "runtime/shaders/program.h"

#include "runtime/shaders/detail.h"
#include "runtime/shaders/effects.h"
#include "runtime/shaders/image.h"
#include "runtime/shaders/volume.h"
#include "support/array.h"
#include "support/xalloc.h"

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
                                     "WorldPosition",
                                     "Vector3",
                                     "Normalize",
                                     "Clamp",
                                     "Saturate",
                                     "Step",
                                     "Smoothstep",
                                     "Noise",
                                     "UV",
                                     "ObjectPosition",
                                     "Normal",
                                     "ViewDirection",
                                     "Mapping",
                                     "Mapping3D",
                                     "Fresnel",
                                     "Remap",
                                     "Gradient",
                                     "Fractal Noise",
                                     "Voronoi",
                                     "Voronoi Color",
                                     "Color Ramp",
                                     "Color Ramp Alpha",
                                     "Image Texture",
                                     "Image Texture Alpha",
                                     "Surface Influence",
                                     "Floor",
                                     "Sqrt",
                                     "Sign",
                                     "PartPosition",
                                     "Planar Mapping",
                                     "Cylindrical Mapping",
                                     "Normal Map",
                                     "Bump",
                                     "Triplanar Texture",
                                     "Triplanar Texture Alpha",
                                     "Decal Mask",
                                     "Sampled SDF",
                                     "Star Field" };
_Static_assert(sizeof names / sizeof *names == SHADER_OP_COUNT, "shader operation names");
const char *shader_op_name(shader_op op)
{
    return op >= 0 && op < SHADER_OP_COUNT ? names[op] : "Invalid";
}
int shader_op_inputs(shader_op op)
{
    if (shader_detail_op(op)) {
        return op == SHADER_NORMAL_MAP || op == SHADER_BUMP || op == SHADER_DECAL_MASK
                 ? 4
                 : 3;
    }
    if (op <= SHADER_TIME || op == SHADER_WORLD_POSITION || op == SHADER_PART_POSITION ||
        (op >= SHADER_UV && op <= SHADER_VIEW_DIRECTION)) {
        return 0;
    }
    if (op >= SHADER_MAPPING && op <= SHADER_FRACTAL_NOISE) {
        return 4;
    }
    if (op == SHADER_STAR_FIELD) {
        return 3;
    }
    if (op == SHADER_VORONOI || op == SHADER_VORONOI_COLOR) {
        return 3;
    }
    if (op == SHADER_COLOR_RAMP || op == SHADER_COLOR_RAMP_ALPHA) {
        return 2;
    }
    if ((op >= SHADER_ABS && op <= SHADER_LENGTH) ||
        (op >= SHADER_X && op <= SHADER_W) || op == SHADER_NORMALIZE ||
        op == SHADER_SAMPLED_SDF || op == SHADER_SATURATE ||
        op == SHADER_IMAGE_TEXTURE || op == SHADER_IMAGE_TEXTURE_ALPHA ||
        op == SHADER_SURFACE_INFLUENCE || (op >= SHADER_FLOOR && op <= SHADER_SIGN)) {
        return 1;
    }
    if (op == SHADER_MIX || op == SHADER_SELECT || op == SHADER_SMOOTH_UNION ||
        op == SHADER_VECTOR3 || op == SHADER_CLAMP || op == SHADER_SMOOTHSTEP) {
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
    for (int i = 0; i < acount(g->nodes); i++) {
        afree(g->nodes[i].ramp);
    }
    for (int i = 0; i < acount(g->images); i++) {
        free(g->images[i].key);
    }
    afree(g->images);
    afree(g->nodes);
    shader_graph_init(g);
}
bool shader_graph_clone(const shader_graph *source, shader_graph *result, shader_error *error)
{
    if (!shader_graph_validate(source, error)) {
        return false;
    }
    shader_graph fresh;
    shader_graph_init(&fresh);
    fresh.output_count = source->output_count;
    memcpy(fresh.output, source->output, sizeof fresh.output);
    for (int i = 0; i < acount(source->nodes); i++) {
        shader_node node = source->nodes[i];
        node.ramp = NULL;
        for (int j = 0; j < acount(source->nodes[i].ramp); j++) {
            apush(node.ramp, source->nodes[i].ramp[j]);
        }
        apush(fresh.nodes, node);
    }
    shader_graph_copy_images(&fresh, source);
    shader_graph_free(result);
    *result = fresh;
    return true;
}
int shader_graph_add(shader_graph *g, shader_op op, int width)
{
    shader_node n = { .op = op, .width = width, .input = { -1, -1, -1, -1 }, .image = -1 };
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
        if (shader_detail_op(n->op)) {
            if (!shader_detail_validate(g, n, error)) {
                return false;
            }
            continue;
        }
        int a = shader_op_inputs(n->op) > 0 ? g->nodes[n->input[0]].width : 0;
        int b = shader_op_inputs(n->op) > 1 ? g->nodes[n->input[1]].width : 0;
        int c = shader_op_inputs(n->op) > 2 ? g->nodes[n->input[2]].width : 0;
        int d = shader_op_inputs(n->op) > 3 ? g->nodes[n->input[3]].width : 0;
        bool valid = true;
        switch (n->op) {
            case SHADER_POSITION:
            case SHADER_UV: valid = n->width == 2; break;
            case SHADER_WORLD_POSITION:
            case SHADER_OBJECT_POSITION:
            case SHADER_PART_POSITION:
            case SHADER_NORMAL:
            case SHADER_VIEW_DIRECTION: valid = n->width == 3; break;
            case SHADER_SURFACE_INFLUENCE:
                valid = n->width == 3 && a == 3;
                break;
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
            case SHADER_VECTOR3:
                valid = n->width == 3 && a == 1 && b == 1 && c == 1;
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
            case SHADER_NORMALIZE:
            case SHADER_SATURATE: valid = n->width == a; break;
            case SHADER_CLAMP:
            case SHADER_SMOOTHSTEP:
                valid = n->width == a && b == 1 && c == 1;
                break;
            case SHADER_STEP: valid = n->width == a && b == 1; break;
            case SHADER_NOISE: valid = n->width == 1 && a <= 3 && b == 1; break;
            case SHADER_MAPPING:
                valid = n->width == 2 && a == 2 && b == 2 && c == 1 && d == 2;
                break;
            case SHADER_MAPPING3D:
                valid = n->width == 3 && a == 3 && b == 3 && c == 3 && d == 3;
                break;
            case SHADER_FRESNEL:
                valid = n->width == 1 && a == 3 && b == 3 && c == 1 && d == 1;
                break;
            case SHADER_REMAP:
                valid = n->width == a && b == 2 && c == 2 && d == 1;
                break;
            case SHADER_GRADIENT:
                valid = n->width == 1 && a == 2 && b == 1 && c == 1 && d == 1;
                break;
            case SHADER_FRACTAL_NOISE:
                valid = n->width == 1 && a <= 3 && b == 1 && c == 1 && d == 1;
                break;
            case SHADER_VORONOI:
            case SHADER_VORONOI_COLOR:
                valid = n->width == (n->op == SHADER_VORONOI ? 1 : 3) &&
                        a >= 2 && a <= 3 && b == 1 && c == 1;
                break;
            case SHADER_COLOR_RAMP:
            case SHADER_COLOR_RAMP_ALPHA:
                if (!shader_ramp_valid(n->ramp, acount(n->ramp))) {
                    return fail(error, n->source_id, "Color Ramp needs 2 to 16 ordered, finite stops in [0,1]");
                }
                valid = n->width == (n->op == SHADER_COLOR_RAMP ? 3 : 1) &&
                        a == 1 && b == 1;
                break;
            case SHADER_STAR_FIELD:
                valid = n->width == 3 && a == 3 && b == 1 && c == 1;
                break;
            case SHADER_SAMPLED_SDF:
                valid = n->width == 1 && a == 3 && shader_volume_contract(n) &&
                        n->image >= 0 && n->image < acount(g->images);
                if (valid) {
                    const shader_image_source *image = &g->images[n->image];
                    valid = shader_image_source_valid(image) &&
                            image->color_space == SHADER_IMAGE_LINEAR &&
                            image->filter == SHADER_IMAGE_NEAREST &&
                            image->wrap == SHADER_IMAGE_CLAMP;
                }
                break;
            case SHADER_IMAGE_TEXTURE:
            case SHADER_IMAGE_TEXTURE_ALPHA:
                if (n->image < 0 || n->image >= acount(g->images) ||
                    !shader_image_source_valid(&g->images[n->image])) {
                    return fail(error, n->source_id, "Image Texture needs an image and valid sampling settings");
                }
                valid = n->width == (n->op == SHADER_IMAGE_TEXTURE ? 3 : 1) && a == 2;
                break;
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
bool shader_graph_particle_validate(const shader_graph *g, shader_error *error)
{
    if (!shader_graph_validate(g, error)) {
        return false;
    }
    if (g->output_count != 3 || g->nodes[g->output[0]].width != 3 ||
        g->nodes[g->output[1]].width != 1 || g->nodes[g->output[2]].width != 4) {
        return fail(error, 0, "Particle function must return Position:ShaderVec3, Size:number, Color:ShaderVec4");
    }
    if (acount(g->images)) {
        return fail(error, 0, "Particle functions cannot sample images or volumes");
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        const shader_node *n = &g->nodes[i];
        switch (n->op) {
            case SHADER_CONSTANT:
            case SHADER_PARAMETER:
            case SHADER_TIME:
            case SHADER_ADD:
            case SHADER_SUBTRACT:
            case SHADER_MULTIPLY:
            case SHADER_DIVIDE:
            case SHADER_MIN:
            case SHADER_MAX:
            case SHADER_ABS:
            case SHADER_SIN:
            case SHADER_COS:
            case SHADER_FRACT:
            case SHADER_LENGTH:
            case SHADER_DOT:
            case SHADER_MIX:
            case SHADER_SELECT:
            case SHADER_CIRCLE:
            case SHADER_BOX:
            case SHADER_UNION:
            case SHADER_INTERSECTION:
            case SHADER_DIFFERENCE:
            case SHADER_SMOOTH_UNION:
            case SHADER_VECTOR2:
            case SHADER_VECTOR3:
            case SHADER_VECTOR4:
            case SHADER_X:
            case SHADER_Y:
            case SHADER_Z:
            case SHADER_W:
            case SHADER_NORMALIZE:
            case SHADER_CLAMP:
            case SHADER_SATURATE:
            case SHADER_STEP:
            case SHADER_SMOOTHSTEP:
            case SHADER_NOISE:
            case SHADER_MAPPING:
            case SHADER_MAPPING3D:
            case SHADER_FRESNEL:
            case SHADER_REMAP:
            case SHADER_GRADIENT:
            case SHADER_FRACTAL_NOISE:
            case SHADER_VORONOI:
            case SHADER_VORONOI_COLOR:
            case SHADER_FLOOR:
            case SHADER_SQRT:
            case SHADER_SIGN:
            case SHADER_PLANAR_MAPPING:
            case SHADER_CYLINDRICAL_MAPPING: break;
            default:
                if (error) {
                    error->node = n->source_id;
                    snprintf(
                        error->message,
                        sizeof error->message,
                        "Particle function cannot use %s",
                        shader_op_name(n->op)
                    );
                }
                return false;
        }
    }
    return true;
}
bool shader_particle_parameters_validate(
    char *const names[SHADER_PARAMETER_MAX],
    const int widths[SHADER_PARAMETER_MAX],
    shader_error *error
)
{
    static const char *const reserved[] = { "Index", "Count", "Seed" };
    for (int i = 0; i < 3; i++) {
        if (!names[i] || strcmp(names[i], reserved[i]) || widths[i] != 1) {
            return fail(error, 0, "Particle arguments must start with Index:number, Count:number, Seed:number");
        }
    }
    bool end = false;
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        if (!widths[i]) {
            if (names[i]) {
                return fail(error, 0, "Particle argument needs a scalar or vector width");
            }
            end = true;
            continue;
        }
        if (end || widths[i] < 1 || widths[i] > 4 || !names[i] || !*names[i]) {
            return fail(error, 0, "Particle arguments must be consecutive named scalar or vector values");
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(names[i], names[j])) {
                return fail(error, 0, "Particle argument names must be unique");
            }
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
static float smoothstep(float value, float low, float high)
{
    if (low == high) {
        return value >= low;
    }
    float t = fmaxf(0, fminf(1, (value - low) / (high - low)));
    return t * t * (3 - 2 * t);
}
static bool eval_values(
    const shader_graph *g,
    const shader_sample_context *sample,
    const float (*params)[4],
    float (*v)[4],
    const bool *selected,
    shader_error *error
)
{
    memset(v, 0, (size_t)acount(g->nodes) * sizeof *v);
    for (int i = 0; i < acount(g->nodes); i++) {
        if (selected && !selected[i]) {
            continue;
        }
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
        if (shader_detail_op(n->op)) {
            if (!shader_detail_eval(g, n, sample, params, (const float (*)[4])v, v[i], error)) {
                return false;
            }
            continue;
        }
        if (n->op == SHADER_SURFACE_INFLUENCE) {
            if (sample->resolve_influence) {
                sample->resolve_influence(sample->influence_userdata, a, v[i]);
            }
            continue;
        }
        if (n->op == SHADER_SAMPLED_SDF) {
            shader_image_view image = { 0 };
            if (!sample->resolve_image ||
                !sample->resolve_image(sample->image_userdata, &g->images[n->image], &image)) {
                return fail(error, n->source_id, "Sampled SDF image could not be resolved");
            }
            if (!shader_volume_eval(n, &image, a, &v[i][0], error)) {
                return false;
            }
            continue;
        }
        if (n->op == SHADER_IMAGE_TEXTURE || n->op == SHADER_IMAGE_TEXTURE_ALPHA) {
            const shader_image_source *source = &g->images[n->image];
            shader_image_view image = { 0 };
            float pixel[4];
            if (!sample->resolve_image ||
                !sample->resolve_image(sample->image_userdata, source, &image) ||
                !shader_image_sample(source, &image, a, pixel)) {
                char message[128];
                snprintf(
                    message,
                    sizeof message,
                    "Image Texture could not resolve '%s'",
                    source->key
                );
                return fail(error, n->source_id, message);
            }
            memcpy(
                v[i],
                pixel + (n->op == SHADER_IMAGE_TEXTURE_ALPHA ? 3 : 0),
                (size_t)n->width * sizeof(float)
            );
            continue;
        }
        if ((n->op >= SHADER_MAPPING && n->op <= SHADER_COLOR_RAMP_ALPHA) ||
            n->op == SHADER_STAR_FIELD) {
            shader_effect_eval(g, n, (const float (*)[4])v, v[i]);
            continue;
        }
        if (n->op == SHADER_NOISE) {
            v[i][0] = shader_noise(v[n->input[0]], g->nodes[n->input[0]].width, b[0]);
            continue;
        }
        if (n->op == SHADER_NORMALIZE) {
            float scale = 0, length = 0;
            for (int j = 0; j < n->width; j++) {
                scale = fmaxf(scale, fabsf(a[j]));
            }
            if (scale == 0) {
                continue;
            }
            for (int j = 0; j < n->width; j++) {
                a[j] /= scale;
                length += a[j] * a[j];
            }
            for (int j = 0; j < n->width; j++) {
                v[i][j] = a[j] / sqrtf(length);
            }
            continue;
        }
        for (int j = 0; j < n->width; j++) {
            switch (n->op) {
                case SHADER_CONSTANT: v[i][j] = n->value[j]; break;
                case SHADER_PARAMETER:
                    v[i][j] = params ? params[n->parameter][j] : n->value[j];
                    break;
                case SHADER_POSITION: v[i][j] = sample->position[j]; break;
                case SHADER_UV: v[i][j] = sample->uv[j]; break;
                case SHADER_TIME: v[i][j] = sample->time; break;
                case SHADER_WORLD_POSITION:
                    v[i][j] = sample->world_position[j];
                    break;
                case SHADER_PART_POSITION:
                    v[i][j] = sample->part_position[j];
                    break;
                case SHADER_OBJECT_POSITION:
                    v[i][j] = sample->object_position[j];
                    break;
                case SHADER_NORMAL: v[i][j] = sample->normal[j]; break;
                case SHADER_VIEW_DIRECTION:
                    v[i][j] = sample->view_direction[j];
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
                case SHADER_FLOOR: v[i][j] = floorf(a[j]); break;
                /* Total math: a negative square root reads as 0, as on the GPU. */
                case SHADER_SQRT: v[i][j] = sqrtf(fmaxf(a[j], 0)); break;
                case SHADER_SIGN:
                    v[i][j] = (float)((a[j] > 0) - (a[j] < 0));
                    break;
                case SHADER_MIX: v[i][j] = a[j] + (b[j] - a[j]) * c[j]; break;
                case SHADER_SELECT: v[i][j] = c[0] != 0 ? b[j] : a[j]; break;
                case SHADER_CLAMP:
                    v[i][j] = fmaxf(fminf(b[0], c[0]), fminf(a[j], fmaxf(b[0], c[0])));
                    break;
                case SHADER_SATURATE: v[i][j] = fmaxf(0, fminf(1, a[j])); break;
                case SHADER_STEP: v[i][j] = a[j] >= b[0]; break;
                case SHADER_SMOOTHSTEP:
                    v[i][j] = smoothstep(a[j], b[0], c[0]);
                    break;
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
                case SHADER_VECTOR3:
                case SHADER_VECTOR4: v[i][j] = v[n->input[j]][0]; break;
                case SHADER_X:
                case SHADER_Y:
                case SHADER_Z:
                case SHADER_W: v[i][j] = a[n->op - SHADER_X]; break;
                case SHADER_NORMALIZE:
                case SHADER_NOISE:
                default: assert(false);
            }
        }
    }
    return true;
}
bool shader_graph_eval_values_validated(
    const shader_graph *g,
    const shader_sample_context *sample,
    const float (*params)[4],
    float (*values)[4],
    shader_error *error
)
{
    return eval_values(g, sample, params, values, NULL, error);
}
bool shader_graph_eval_values(
    const shader_graph *g,
    const shader_sample_context *sample,
    const float (*params)[4],
    float (*values)[4],
    shader_error *error
)
{
    return shader_graph_validate(g, error) &&
           shader_graph_eval_values_validated(g, sample, params, values, error);
}
bool shader_graph_eval_node_scratch(
    const shader_graph *g,
    const shader_sample_context *sample,
    const float (*params)[4],
    int root,
    float (*values)[4],
    float result[4],
    shader_error *error
)
{
    assert(root >= 0 && root < acount(g->nodes));
    assert(acount(g->nodes) <= SHADER_NODE_MAX);
    bool selected[SHADER_NODE_MAX] = { false };
    selected[root] = true;
    for (int i = root; i >= 0; i--) {
        if (!selected[i]) {
            continue;
        }
        const shader_node *n = &g->nodes[i];
        for (int j = 0; j < shader_op_inputs(n->op); j++) {
            selected[n->input[j]] = true;
        }
    }
    if (!eval_values(g, sample, params, values, selected, error)) {
        return false;
    }
    memcpy(result, values[root], sizeof(float) * 4);
    return true;
}
bool shader_graph_eval_outputs(
    const shader_graph *g,
    const shader_sample_context *sample,
    const float (*params)[4],
    float result[SHADER_OUTPUT_MAX][4],
    shader_error *error
)
{
    if (!shader_graph_validate(g, error)) {
        return false;
    }
    float (*values)[4] = xcalloc((size_t)acount(g->nodes), sizeof *values);
    bool valid = shader_graph_eval_values(g, sample, params, values, error);
    if (valid) {
        memset(result, 0, sizeof(float) * SHADER_OUTPUT_MAX * 4);
        for (int i = 0; i < g->output_count; i++) {
            memcpy(result[i], values[g->output[i]], sizeof result[i]);
        }
    }
    free(values);
    return valid;
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
int shader_graph_ramp_vec4_count(const shader_graph *g)
{
    int count = 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i].op == SHADER_COLOR_RAMP ||
            g->nodes[i].op == SHADER_COLOR_RAMP_ALPHA) {
            count += 2 * acount(g->nodes[i].ramp);
        }
    }
    return count;
}

void shader_graph_ramp_payload(const shader_graph *g, float (*payload)[4])
{
    int offset = 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        const shader_node *n = &g->nodes[i];
        if (n->op != SHADER_COLOR_RAMP && n->op != SHADER_COLOR_RAMP_ALPHA) {
            continue;
        }
        for (int j = 0; j < acount(n->ramp); j++) {
            payload[offset][0] = n->ramp[j].position;
            payload[offset][1] = payload[offset][2] = payload[offset][3] = 0;
            memcpy(payload[offset + 1], n->ramp[j].color, sizeof n->ramp[j].color);
            offset += 2;
        }
    }
}

static int ramp_offset(const shader_graph *g, int node, int base)
{
    if (base < 0) {
        return -1;
    }
    for (int i = 0; i < node; i++) {
        if (g->nodes[i].op == SHADER_COLOR_RAMP ||
            g->nodes[i].op == SHADER_COLOR_RAMP_ALPHA) {
            base += 2 * acount(g->nodes[i].ramp);
        }
    }
    return base;
}

static void ramp_glsl(char **text, const shader_node *n, const char *prefix, int id, int offset)
{
    append(text, "vec4 %s_ramp_%d(float factor,float interpolation) {\n", prefix, id);
    const shader_ramp_stop *first = &n->ramp[0];
    if (offset < 0) {
        append(
            text,
            "if(factor<=%.9e)return vec4(%.9e,%.9e,%.9e,%.9e);\n",
            first->position,
            first->color[0],
            first->color[1],
            first->color[2],
            first->color[3]
        );
    } else {
        append(text, "if(factor<=sg_ramp(%d).x)return sg_ramp(%d);\n", offset, offset + 1);
    }
    for (int i = 1; i < acount(n->ramp); i++) {
        const shader_ramp_stop *a = &n->ramp[i - 1], *b = &n->ramp[i];
        if (offset < 0) {
            append(
                text,
                "if(factor<%.9e) { float t=clamp((factor-%.9e)/%.9e,0.0,1.0); "
                "int "
                "mode=int(clamp(interpolation,0.0,2.0)); if(mode==1)t=0.0; "
                "if(mode==2)t=t*t*(3.0-2.0*t); return "
                "mix(vec4(%.9e,%.9e,%.9e,%.9e),vec4(%.9e,%.9e,%.9e,%.9e),t); "
                "}\n",
                b->position,
                a->position,
                b->position - a->position,
                a->color[0],
                a->color[1],
                a->color[2],
                a->color[3],
                b->color[0],
                b->color[1],
                b->color[2],
                b->color[3]
            );
        } else {
            int left = offset + 2 * (i - 1), right = offset + 2 * i;
            append(
                text,
                "{ float l=sg_ramp(%d).x,r=sg_ramp(%d).x; if(factor<r) { "
                "float t=clamp((factor-l)/(r-l),0.0,1.0); int "
                "mode=int(clamp(interpolation,0.0,2.0)); if(mode==1)t=0.0; "
                "if(mode==2)t=t*t*(3.0-2.0*t); return "
                "mix(sg_ramp(%d),sg_ramp(%d),t); } }\n",
                left,
                right,
                left + 1,
                right + 1
            );
        }
    }
    const shader_ramp_stop *last = &n->ramp[acount(n->ramp) - 1];
    if (offset < 0) {
        append(
            text,
            "return vec4(%.9e,%.9e,%.9e,%.9e); }\n",
            last->color[0],
            last->color[1],
            last->color[2],
            last->color[3]
        );
    } else {
        append(text, "return sg_ramp(%d); }\n", offset + 2 * (acount(n->ramp) - 1) + 1);
    }
}
/* Whether nodes a and b compute the same value, given each earlier node's
 * first equal node in canon. Color ramps own their stops and stay distinct. */
static bool same_node(const shader_graph *g, const int *canon, int a, int b)
{
    const shader_node *x = &g->nodes[a], *y = &g->nodes[b];
    if (x->op != y->op || x->width != y->width || x->parameter != y->parameter ||
        x->image != y->image || memcmp(x->value, y->value, sizeof x->value) ||
        x->op == SHADER_COLOR_RAMP || x->op == SHADER_COLOR_RAMP_ALPHA) {
        return false;
    }
    for (int j = 0; j < shader_op_inputs(x->op); j++) {
        if (canon[x->input[j]] != canon[y->input[j]]) {
            return false;
        }
    }
    return true;
}

/* Each node's first equal node. Calls inline a copy of their function per
 * call, so graphs repeat work that GPU compilers do not always merge. */
static int *canonical_nodes(const shader_graph *g)
{
    int count = acount(g->nodes);
    int *canon = xmalloc(sizeof *canon * (size_t)(count ? count : 1));
    for (int i = 0; i < count; i++) {
        canon[i] = i;
        for (int j = 0; j < i; j++) {
            if (canon[j] == j && same_node(g, canon, i, j)) {
                canon[i] = j;
                break;
            }
        }
    }
    return canon;
}

/* Rows are derivatives of each value component with respect to object XYZ. */
static bool gradient_op(shader_op op)
{
    switch (op) {
        case SHADER_CONSTANT:
        case SHADER_PARAMETER:
        case SHADER_OBJECT_POSITION:
        case SHADER_TIME:
        case SHADER_ADD:
        case SHADER_SUBTRACT:
        case SHADER_MULTIPLY:
        case SHADER_DIVIDE:
        case SHADER_MIN:
        case SHADER_MAX:
        case SHADER_ABS:
        case SHADER_SIN:
        case SHADER_COS:
        case SHADER_LENGTH:
        case SHADER_DOT:
        case SHADER_MIX:
        case SHADER_SELECT:
        case SHADER_UNION:
        case SHADER_INTERSECTION:
        case SHADER_DIFFERENCE:
        case SHADER_SMOOTH_UNION:
        case SHADER_VECTOR2:
        case SHADER_VECTOR3:
        case SHADER_VECTOR4:
        case SHADER_X:
        case SHADER_Y:
        case SHADER_Z:
        case SHADER_W:
        case SHADER_CLAMP:
        case SHADER_SATURATE:
        case SHADER_SQRT:
        case SHADER_NORMALIZE: return true;
        default: return false;
    }
}

static void gradient_glsl(char **text, const shader_graph *g, int id, const bool *spatial)
{
    const shader_node *n = &g->nodes[id];
    append(text, "vec3 d%d[4];\n", id);
    for (int channel = 0; channel < n->width; channel++) {
        char a[32] = "", b[32] = "", c[32] = "";
        char da[32] = "", db[32] = "", dc[32] = "";
        char *value[] = { a, b, c }, *derivative[] = { da, db, dc };
        for (int input = 0; input < 3 && input < shader_op_inputs(n->op); input++) {
            int node = n->input[input],
                component = g->nodes[node].width == 1 ? 0 : channel;
            snprintf(value[input], 32, "v%d[%d]", node, component);
            snprintf(derivative[input], 32, "d%d[%d]", node, component);
        }
        append(text, "d%d[%d]=", id, channel);
        switch (n->op) {
            case SHADER_CONSTANT:
            case SHADER_PARAMETER:
            case SHADER_TIME: append(text, "vec3(0.0)"); break;
            case SHADER_OBJECT_POSITION:
                append(text, "vec3(%d,%d,%d)", channel == 0, channel == 1, channel == 2);
                break;
            case SHADER_ADD:
            case SHADER_SUBTRACT:
                append(text, "%s%s%s", da, n->op == SHADER_ADD ? "+" : "-", db);
                break;
            case SHADER_MULTIPLY:
                append(text, "%s*%s+%s*%s", da, b, db, a);
                break;
            case SHADER_DIVIDE:
                append(text, "%s==0.0?vec3(0.0):(%s*%s-%s*%s)/(%s*%s)", b, da, b, db, a, b, b);
                break;
            case SHADER_MIN:
            case SHADER_MAX:
            case SHADER_UNION:
            case SHADER_INTERSECTION:
            case SHADER_DIFFERENCE: {
                bool minimum = n->op == SHADER_MIN || n->op == SHADER_UNION;
                const char *negate = n->op == SHADER_DIFFERENCE ? "-" : "";
                append(text, "%s%s%s%s?%s:%s%s", a, minimum ? "<=" : ">=", negate, b, da, negate, db);
                break;
            }
            case SHADER_ABS: append(text, "sign(%s)*%s", a, da); break;
            case SHADER_SIN: append(text, "cos(%s)*%s", a, da); break;
            case SHADER_COS: append(text, "-sin(%s)*%s", a, da); break;
            case SHADER_SQRT:
                append(text, "%s>0.0?%s/(2.0*v%d[%d]):vec3(0.0)", a, da, id, channel);
                break;
            case SHADER_NORMALIZE: {
                int input = n->input[0];
                append(text, "scale%d>0.0?(%s-v%d[%d]*(vec3(0.0)", id, da, id, channel);
                for (int k = 0; k < n->width; k++) {
                    append(text, "+v%d[%d]*d%d[%d]", id, k, input, k);
                }
                const char *sw[] = { "", ".x", ".xy", ".xyz", "" };
                append(text, "))/scale%d/length(v%d%s/scale%d):vec3(0.0)", id, input, sw[n->width], id);
                break;
            }
            case SHADER_LENGTH:
            case SHADER_DOT: {
                int width = g->nodes[n->input[0]].width;
                append(text, "vec3(0.0)");
                for (int k = 0; k < width; k++) {
                    append(
                        text,
                        "+v%d[%d]*d%d[%d]",
                        n->input[0],
                        k,
                        n->input[n->op == SHADER_DOT ? 1 : 0],
                        k
                    );
                    if (n->op == SHADER_DOT) {
                        append(text, "+v%d[%d]*d%d[%d]", n->input[1], k, n->input[0], k);
                    }
                }
                if (n->op == SHADER_LENGTH) {
                    append(text, "; if(v%d.x>0.0)d%d[%d]/=v%d.x", id, id, channel, id);
                }
                break;
            }
            case SHADER_MIX:
                append(text, "mix(%s,%s,%s)+(%s-%s)*%s", da, db, c, b, a, dc);
                break;
            case SHADER_CLAMP:
                append(
                    text,
                    "%s<min(%s,%s)?(%s<=%s?%s:%s):(%s>max(%s,%s)?(%s>=%s?%s:%s)"
                    ":%s)",
                    a,
                    b,
                    c,
                    b,
                    c,
                    db,
                    dc,
                    a,
                    b,
                    c,
                    b,
                    c,
                    db,
                    dc,
                    da
                );
                break;
            case SHADER_SATURATE:
                append(text, "%s<0.0||%s>1.0?vec3(0.0):%s", a, a, da);
                break;
            case SHADER_SELECT:
                append(text, "v%d.x!=0.0?%s:%s", n->input[2], db, da);
                break;
            case SHADER_SMOOTH_UNION:
                append(
                    text,
                    "%s<=%s?%s:%s; if(v%d.x>0.0) { float "
                    "h=clamp(0.5+0.5*(v%d.x-v%d.x)/v%d.x,0.0,1.0); "
                    "d%d[%d]=mix(d%d[0],d%d[0],h)-h*(1.0-h)*d%d[0]; }",
                    a,
                    b,
                    da,
                    db,
                    n->input[2],
                    n->input[1],
                    n->input[0],
                    n->input[2],
                    id,
                    channel,
                    n->input[1],
                    n->input[0],
                    n->input[2]
                );
                break;
            case SHADER_VECTOR2:
            case SHADER_VECTOR3:
            case SHADER_VECTOR4:
                if (channel < n->width) {
                    append(text, "d%d[0]", n->input[channel]);
                } else {
                    append(text, "vec3(0.0)");
                }
                break;
            case SHADER_X:
            case SHADER_Y:
            case SHADER_Z:
            case SHADER_W:
                append(text, "d%d[%d]", n->input[0], g->nodes[n->input[0]].width == 1 ? 0 : n->op - SHADER_X);
                break;
            default: assert(false);
        }
        append(text, n->op == SHADER_SMOOTH_UNION ? "\n" : ";\n");
        if (n->op == SHADER_DIVIDE) {
            append(text, "if(%s==0.0)sg_gradient_valid=false;\n", b);
        }
        if (n->op == SHADER_ABS) {
            /* A ray-marched hit is only known within its object-space epsilon.
             * Treat an absolute-value crease inside that error bound as
             * singular when its input changes in space. */
            append(
                text,
                "if(any(notEqual(%s,vec3(0))) && "
                "abs(%s)<=sg_gradient_tolerance*length(%s))"
                "sg_gradient_valid=false;\n",
                da,
                a,
                da
            );
        }
        if (n->op == SHADER_MIN || n->op == SHADER_MAX ||
            n->op == SHADER_UNION || n->op == SHADER_INTERSECTION ||
            n->op == SHADER_DIFFERENCE || n->op == SHADER_SMOOTH_UNION) {
            const char *negate = n->op == SHADER_DIFFERENCE ? "-" : "";
            append(text, "if(%s==%s%s && any(notEqual(%s,%s%s))", a, negate, b, da, negate, db);
            if (n->op == SHADER_SMOOTH_UNION) {
                append(text, " && v%d.x<=0.0", n->input[2]);
            }
            append(text, ")sg_gradient_valid=false;\n");
        }
        if (n->op == SHADER_LENGTH) {
            append(text, "if(v%d.x==0.0 && (", id);
            int input = n->input[0];
            for (int component = 0; component < g->nodes[input].width; component++) {
                append(text, "%sany(notEqual(d%d[%d],vec3(0)))", component ? "||" : "", input, component);
            }
            append(text, "))sg_gradient_valid=false;\n");
        }
        if (n->op == SHADER_CLAMP) {
            append(text, "if(%s==%s && any(notEqual(%s,%s)))sg_gradient_valid=false;\n", b, c, db, dc);
            append(
                text,
                "if(%s==min(%s,%s) && "
                "any(notEqual(%s,(%s<=%s?%s:%s))))sg_gradient_valid=false;\n",
                a,
                b,
                c,
                da,
                b,
                c,
                db,
                dc
            );
            append(
                text,
                "if(%s==max(%s,%s) && "
                "any(notEqual(%s,(%s>=%s?%s:%s))))sg_gradient_valid=false;\n",
                a,
                b,
                c,
                da,
                b,
                c,
                db,
                dc
            );
        }
        if (n->op == SHADER_SATURATE) {
            append(text, "if((%s==0.0||%s==1.0) && any(notEqual(%s,vec3(0))))sg_gradient_valid=false;\n", a, a, da);
        }
        if (n->op == SHADER_SQRT && spatial[n->input[0]]) {
            append(text, "if(%s==0.0)sg_gradient_valid=false;\n", a);
        }
        if (n->op == SHADER_NORMALIZE && spatial[n->input[0]]) {
            append(text, "if(scale%d==0.0)sg_gradient_valid=false;\n", id);
        }
        if (n->op == SHADER_SELECT && spatial[n->input[2]]) {
            /* A condition can cross zero even with a zero derivative there. */
            append(text, "if(v%d.x==0.0)sg_gradient_valid=false;\n", n->input[2]);
        }
        if (n->op == SHADER_SMOOTH_UNION) {
            append(
                text,
                "if(v%d.x==0.0 && %s==%s && "
                "any(notEqual(d%d[0],vec3(0))))sg_gradient_valid=false;\n",
                n->input[2],
                a,
                b,
                n->input[2]
            );
        }
    }
}

static char *graph_glsl(const shader_graph *g, const char *prefix, int ramp_base, bool gradient, shader_error *error)
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
    bool reachable[SHADER_NODE_MAX] = { false }, spatial[SHADER_NODE_MAX] = { false };
    if (gradient) {
        if (!g->output_count) {
            return NULL;
        }
        reachable[g->output[0]] = true;
        for (int i = acount(g->nodes) - 1; i >= 0; i--) {
            if (!reachable[i]) {
                continue;
            }
            if (!gradient_op(g->nodes[i].op)) {
                return NULL;
            }
            for (int j = 0; j < shader_op_inputs(g->nodes[i].op); j++) {
                reachable[g->nodes[i].input[j]] = true;
            }
        }
    }
    if (gradient) {
        for (int i = 0; i < acount(g->nodes); i++) {
            spatial[i] = g->nodes[i].op == SHADER_OBJECT_POSITION;
            for (int j = 0; j < shader_op_inputs(g->nodes[i].op); j++) {
                spatial[i] |= spatial[g->nodes[i].input[j]];
            }
        }
    }
    char *s = NULL;
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i].op == SHADER_SAMPLED_SDF) {
            char *volume = shader_volume_glsl(&g->nodes[i], prefix, i);
            append(&s, "%s", volume);
            free(volume);
        }
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        if (gradient && !reachable[i]) {
            continue;
        }
        if (g->nodes[i].op == SHADER_NOISE ||
            (g->nodes[i].op >= SHADER_MAPPING &&
             g->nodes[i].op <= SHADER_COLOR_RAMP_ALPHA)) {
            append(&s, "%s", shader_effects_glsl());
            break;
        }
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i].op == SHADER_STAR_FIELD && (!gradient || reachable[i])) {
            append(&s, "%s", shader_star_field_glsl());
            break;
        }
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        if (gradient && !reachable[i]) {
            continue;
        }
        if (g->nodes[i].op == SHADER_COLOR_RAMP ||
            g->nodes[i].op == SHADER_COLOR_RAMP_ALPHA) {
            ramp_glsl(&s, &g->nodes[i], prefix, i, ramp_offset(g, i, ramp_base));
        }
    }
    append(&s, "#ifndef SG_INFLUENCE_DEFINED\n#define SG_INFLUENCE_DEFINED\n#ifndef SG_SCENE_INFLUENCE\nvec3 sg_scene_influence(vec3 position) { return vec3(0); }\n#endif\n#endif\n");
    append(&s, "#ifndef SG_SAMPLE_DEFINED\n#define SG_SAMPLE_DEFINED\nstruct SGSample { vec2 position; vec2 uv; vec3 object_position; vec3 part_position; vec3 object_from_world[3]; vec3 part_from_world[3]; vec3 uv_from_world[2]; vec3 world_position; vec3 normal; vec3 view_direction; float time; };\n#endif\n");
    bool details = false;
    for (int i = 0; i < acount(g->nodes); i++) {
        details = details || shader_detail_op(g->nodes[i].op);
    }
    if (details) {
        char *helpers = shader_detail_glsl_helpers(g, prefix, error);
        if (!helpers) {
            afree(s);
            return NULL;
        }
        append(&s, "%s", helpers);
        free(helpers);
    }
    if (gradient) {
        append(
            &s,
            "vec3 %s(SGSample sg_sample, vec4 sg_params[32], float "
            "sg_gradient_tolerance) {\n"
            "sg_gradient_tolerance=max(sg_gradient_tolerance,0.0);\n"
            "bool sg_gradient_valid=true;\n",
            prefix
        );
    } else {
        append(&s, "void %s(SGSample sg_sample, vec4 sg_params[32], out vec4 sg_out[%d]) {\n", prefix, SHADER_OUTPUT_MAX);
    }
    int *canon = gradient ? NULL : canonical_nodes(g);
    for (int i = 0; i < acount(g->nodes); i++) {
        const shader_node *n = &g->nodes[i];
        if (gradient && !reachable[i]) {
            continue;
        }
        if (!gradient && canon[i] != i) {
            append(&s, "vec4 v%d = v%d;\n", i, canon[i]);
            continue;
        }
        char a[32] = "", b[32] = "", c[32] = "", d[32] = "";
        char *args[] = { a, b, c, d };
        for (int j = 0; j < 4 && j < shader_op_inputs(n->op); j++) {
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
                append(&s, "vec4(sg_sample.position,0.0,0.0)");
                break;
            case SHADER_UV: append(&s, "vec4(sg_sample.uv,0.0,0.0)"); break;
            case SHADER_TIME: append(&s, "vec4(sg_sample.time)"); break;
            case SHADER_WORLD_POSITION:
                append(&s, "vec4(sg_sample.world_position,0.0)");
                break;
            case SHADER_SURFACE_INFLUENCE:
                append(&s, "vec4(sg_scene_influence(%s.xyz),0.0)", a);
                break;
            case SHADER_PART_POSITION:
                append(&s, "vec4(sg_sample.part_position,0.0)");
                break;
            case SHADER_OBJECT_POSITION:
                append(&s, "vec4(sg_sample.object_position,0.0)");
                break;
            case SHADER_NORMAL: append(&s, "vec4(sg_sample.normal,0.0)"); break;
            case SHADER_VIEW_DIRECTION:
                append(&s, "vec4(sg_sample.view_direction,0.0)");
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
            case SHADER_FLOOR: append(&s, "floor(%s)", a); break;
            case SHADER_SQRT: append(&s, "sqrt(max(%s,0.0))", a); break;
            case SHADER_SIGN: append(&s, "sign(%s)", a); break;
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
            case SHADER_CLAMP:
                append(&s, "clamp(%s,min(%s,%s),max(%s,%s))", a, b, c, b, c);
                break;
            case SHADER_SATURATE: append(&s, "clamp(%s,0.0,1.0)", a); break;
            /* Compared per component rather than with step(), which reads
             * NaN as reaching its edge: NaN gives 0 here, as on the CPU. */
            case SHADER_STEP:
                append(&s, "vec4(");
                for (int j = 0; j < 4; j++) {
                    append(&s, "%s(%s[%d] >= %s[%d] ? 1.0 : 0.0)", j ? "," : "", a, j, b, j);
                }
                append(&s, ")");
                break;
            case SHADER_SMOOTHSTEP:
                append(
                    &s,
                    "(%s.x==%s.x?step(%s,%s):clamp((%s-%s)/(%s-%s),0.0,1.0));\n"
                    "v%d=v%d*v%d*(3.0-2.0*v%d)",
                    b,
                    c,
                    b,
                    a,
                    a,
                    b,
                    c,
                    b,
                    i,
                    i,
                    i,
                    i
                );
                break;
            case SHADER_NORMALIZE: {
                const char *pad[] = { "", ".x,0.0,0.0,0.0", ".xy,0.0,0.0", ".xyz,0.0", "" };
                append(
                    &s,
                    "vec4(v%d%s);\n"
                    "float "
                    "scale%d=max(max(abs(v%d.x),abs(v%d.y)),max(abs(v%d.z),abs("
                    "v%d.w)));\n"
                    "if(scale%d>0.0) {v%d/=scale%d; v%d/=length(v%d);}\n",
                    n->input[0],
                    pad[n->width],
                    i,
                    i,
                    i,
                    i,
                    i,
                    i,
                    i,
                    i,
                    i,
                    i
                );
                if (gradient) {
                    gradient_glsl(&s, g, i, spatial);
                }
                continue;
            }
            case SHADER_NOISE: {
                const char *pad[] = { "", ".x,0.0,0.0", ".xy,0.0", ".xyz" };
                append(
                    &s,
                    "vec4(sgx_noise(vec3(v%d%s)*%s.x))",
                    n->input[0],
                    pad[g->nodes[n->input[0]].width],
                    b
                );
                break;
            }
            case SHADER_MAPPING:
                append(&s, "vec4(sgx_mapping(%s.xy,%s.xy,%s.x,%s.xy),0.0,0.0)", a, b, c, d);
                break;
            case SHADER_MAPPING3D:
                append(&s, "vec4(sgx_mapping3d(%s.xyz,%s.xyz,%s.xyz,%s.xyz),0.0)", a, b, c, d);
                break;
            case SHADER_FRESNEL:
                append(&s, "vec4(sgx_fresnel(%s.xyz,%s.xyz,%s.x,%s.x))", a, b, c, d);
                break;
            case SHADER_REMAP:
                append(&s, "sgx_remap(%s,%s.xy,%s.xy,%s.x)", a, b, c, d);
                break;
            case SHADER_GRADIENT:
                append(&s, "vec4(sgx_gradient(%s.xy,%s.x,%s.x,%s.x))", a, b, c, d);
                break;
            case SHADER_FRACTAL_NOISE: {
                const char *pad[] = { "", ".x,0.0,0.0", ".xy,0.0", ".xyz" };
                append(
                    &s,
                    "vec4(sgx_fbm(vec3(v%d%s),%s.x,%s.x,%s.x))",
                    n->input[0],
                    pad[g->nodes[n->input[0]].width],
                    b,
                    c,
                    d
                );
                break;
            }
            case SHADER_VORONOI:
            case SHADER_VORONOI_COLOR: {
                int width = g->nodes[n->input[0]].width;
                append(
                    &s,
                    "vec4(sgx_voronoi(vec3(v%d%s)*%s.x,%d,%s.x).%s%s)",
                    n->input[0],
                    width == 2 ? ".xy,0.0" : ".xyz",
                    b,
                    width,
                    c,
                    n->op == SHADER_VORONOI ? "x" : "yzw",
                    n->op == SHADER_VORONOI ? "" : ",0.0"
                );
                break;
            }
            case SHADER_COLOR_RAMP:
                append(&s, "vec4(%s_ramp_%d(%s.x,%s.x).rgb,0.0)", prefix, i, a, b);
                break;
            case SHADER_COLOR_RAMP_ALPHA:
                append(&s, "vec4(%s_ramp_%d(%s.x,%s.x).a)", prefix, i, a, b);
                break;
            case SHADER_SAMPLED_SDF:
                append(&s, "vec4(%s_volume_%d(%s.xyz))", prefix, i, a);
                break;
            case SHADER_STAR_FIELD:
                append(&s, "vec4(sgx_star_field(%s.xyz,%s.x,%s.x),0.0)", a, b, c);
                break;
            case SHADER_IMAGE_TEXTURE:
                append(&s, "vec4(%s_image_%d(%s.xy).rgb,0.0)", prefix, n->image, a);
                break;
            case SHADER_IMAGE_TEXTURE_ALPHA:
                append(&s, "vec4(%s_image_%d(%s.xy).a)", prefix, n->image, a);
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
                if (gradient) {
                    gradient_glsl(&s, g, i, spatial);
                }
                continue;
            case SHADER_VECTOR2:
                append(&s, "vec4(%s.x,%s.x,0.0,0.0)", a, b);
                break;
            case SHADER_VECTOR3:
                append(&s, "vec4(%s.x,%s.x,%s.x,0.0)", a, b, c);
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
            case SHADER_PLANAR_MAPPING:
            case SHADER_CYLINDRICAL_MAPPING:
            case SHADER_NORMAL_MAP:
            case SHADER_BUMP:
            case SHADER_TRIPLANAR_TEXTURE:
            case SHADER_TRIPLANAR_TEXTURE_ALPHA:
            case SHADER_DECAL_MASK: {
                char *expression = shader_detail_glsl_expression(g, n, prefix, i, a, b, c, d);
                append(&s, "%s", expression);
                free(expression);
                break;
            }
            case SHADER_OP_COUNT: assert(false);
        }
        append(&s, ";\n");
        if (gradient) {
            gradient_glsl(&s, g, i, spatial);
        }
    }
    if (gradient) {
        append(&s, "return sg_gradient_valid?d%d[0]:vec3(uintBitsToFloat(0x7fc00000u));\n", g->output[0]);
    }
    for (int i = 0; !gradient && i < SHADER_OUTPUT_MAX; i++) {
        if (i < g->output_count) {
            append(&s, "sg_out[%d]=v%d;\n", i, g->output[i]);
        } else {
            append(&s, "sg_out[%d]=vec4(0.0);\n", i);
        }
    }
    free(canon);
    append(&s, "}\n");
    if (gradient) {
        append(
            &s,
            "vec3 %s(SGSample sg_sample,vec4 sg_params[32]) {"
            "return %s(sg_sample,sg_params,0.0); }\n",
            prefix,
            prefix
        );
    }
    char *result = xmalloc((size_t)acount(s) + 1);
    memcpy(result, s, (size_t)acount(s) + 1);
    afree(s);
    return result;
}

char *shader_graph_glsl_named_ramps(const shader_graph *g, const char *prefix, int ramp_base, shader_error *error)
{
    return graph_glsl(g, prefix, ramp_base, false, error);
}

/* Lower compound distance builders into the same arithmetic IR used by the
 * value emitter and differentiator. Only output[0]'s reachable nodes are copied. */
static int gradient_node(shader_graph *g, shader_op op, int width, int a, int b, int c, int d)
{
    int id = shader_graph_add(g, op, width);
    int inputs[] = { a, b, c, d };
    memcpy(g->nodes[id].input, inputs, sizeof inputs);
    return id;
}

static int gradient_constant(shader_graph *g, float value)
{
    int id = shader_graph_add(g, SHADER_CONSTANT, 1);
    g->nodes[id].value[0] = value;
    return id;
}

static int gradient_component(shader_graph *g, int node, int channel)
{
    return gradient_node(g, SHADER_X + channel, 1, node, -1, -1, -1);
}

static int gradient_mapping(shader_graph *g, const shader_node *n, const int *input)
{
    int scaled = gradient_node(g, SHADER_MULTIPLY, n->width, input[0], input[3], -1, -1);
    int p[3] = { -1, -1, -1 };
    for (int i = 0; i < n->width; i++) {
        p[i] = gradient_component(g, scaled, i);
    }
    int radians = gradient_constant(g, .017453292519943295f);
    for (int axis = n->width == 2 ? 2 : 0; axis < 3; axis++) {
        int angle = n->width == 2 ? input[2] : gradient_component(g, input[2], axis);
        angle = gradient_node(g, SHADER_MULTIPLY, 1, angle, radians, -1, -1);
        int cosine = gradient_node(g, SHADER_COS, 1, angle, -1, -1, -1);
        int sine = gradient_node(g, SHADER_SIN, 1, angle, -1, -1, -1);
        int a = (axis + 1) % 3, b = (axis + 2) % 3;
        int cx = gradient_node(g, SHADER_MULTIPLY, 1, cosine, p[a], -1, -1);
        int sy = gradient_node(g, SHADER_MULTIPLY, 1, sine, p[b], -1, -1);
        int sx = gradient_node(g, SHADER_MULTIPLY, 1, sine, p[a], -1, -1);
        int cy = gradient_node(g, SHADER_MULTIPLY, 1, cosine, p[b], -1, -1);
        p[a] = gradient_node(g, SHADER_SUBTRACT, 1, cx, sy, -1, -1);
        p[b] = gradient_node(g, SHADER_ADD, 1, sx, cy, -1, -1);
    }
    int rotated = gradient_node(
        g,
        n->width == 2 ? SHADER_VECTOR2 : SHADER_VECTOR3,
        n->width,
        p[0],
        p[1],
        p[2],
        -1
    );
    return gradient_node(g, SHADER_ADD, n->width, rotated, input[1], -1, -1);
}

static int gradient_compound(shader_graph *g, const shader_node *n, const int *input)
{
    int a = input[0], b = input[1], c = input[2];
    switch (n->op) {
        case SHADER_MAPPING:
        case SHADER_MAPPING3D: return gradient_mapping(g, n, input);
        case SHADER_CIRCLE: {
            int length = gradient_node(g, SHADER_LENGTH, 1, a, -1, -1, -1);
            return gradient_node(g, SHADER_SUBTRACT, 1, length, b, -1, -1);
        }
        case SHADER_BOX: {
            int zero = gradient_constant(g, 0);
            int absolute = gradient_node(g, SHADER_ABS, 2, a, -1, -1, -1);
            int q = gradient_node(g, SHADER_SUBTRACT, 2, absolute, b, -1, -1);
            int positive = gradient_node(g, SHADER_MAX, 2, q, zero, -1, -1);
            int outside = gradient_node(g, SHADER_LENGTH, 1, positive, -1, -1, -1);
            int x = gradient_component(g, q, 0), y = gradient_component(g, q, 1);
            int maximum = gradient_node(g, SHADER_MAX, 1, x, y, -1, -1);
            int inside = gradient_node(g, SHADER_MIN, 1, maximum, zero, -1, -1);
            return gradient_node(g, SHADER_ADD, 1, outside, inside, -1, -1);
        }
        case SHADER_REMAP:
        case SHADER_SMOOTHSTEP: {
            int low = n->op == SHADER_REMAP ? gradient_component(g, b, 0) : b;
            int high = n->op == SHADER_REMAP ? gradient_component(g, b, 1) : c;
            int numerator = gradient_node(g, SHADER_SUBTRACT, n->width, a, low, -1, -1);
            int denominator = gradient_node(g, SHADER_SUBTRACT, 1, high, low, -1, -1);
            int t = gradient_node(g, SHADER_DIVIDE, n->width, numerator, denominator, -1, -1);
            int bounded = gradient_node(g, SHADER_SATURATE, n->width, t, -1, -1, -1);
            if (n->op == SHADER_SMOOTHSTEP) {
                int two = gradient_constant(g, 2), three = gradient_constant(g, 3);
                int twice = gradient_node(g, SHADER_MULTIPLY, n->width, bounded, two, -1, -1);
                int factor = gradient_node(g, SHADER_SUBTRACT, n->width, three, twice, -1, -1);
                int square = gradient_node(g, SHADER_MULTIPLY, n->width, bounded, bounded, -1, -1);
                return gradient_node(g, SHADER_MULTIPLY, n->width, square, factor, -1, -1);
            }
            t = gradient_node(g, SHADER_SELECT, n->width, t, bounded, input[3], -1);
            int to_low = gradient_component(g, c, 0),
                to_high = gradient_component(g, c, 1);
            return gradient_node(g, SHADER_MIX, n->width, to_low, to_high, t, -1);
        }
        default: return -1;
    }
}

static bool gradient_lower(const shader_graph *source, shader_graph *result)
{
    bool reachable[SHADER_NODE_MAX] = { false };
    int mapping[SHADER_NODE_MAX];
    reachable[source->output[0]] = true;
    for (int i = acount(source->nodes) - 1; i >= 0; i--) {
        if (!reachable[i]) {
            continue;
        }
        for (int j = 0; j < shader_op_inputs(source->nodes[i].op); j++) {
            reachable[source->nodes[i].input[j]] = true;
        }
    }
    for (int i = 0; i < acount(source->nodes); i++) {
        if (!reachable[i]) {
            continue;
        }
        const shader_node *n = &source->nodes[i];
        int inputs[4] = { -1, -1, -1, -1 };
        for (int j = 0; j < shader_op_inputs(n->op); j++) {
            inputs[j] = mapping[n->input[j]];
        }
        int id = gradient_compound(result, n, inputs);
        if (id < 0) {
            if (!gradient_op(n->op)) {
                return false;
            }
            id = shader_graph_add(result, n->op, n->width);
            result->nodes[id] = *n;
            memcpy(result->nodes[id].input, inputs, sizeof inputs);
        }
        mapping[i] = id;
        if (acount(result->nodes) > SHADER_NODE_MAX) {
            return false;
        }
    }
    result->output[0] = mapping[source->output[0]];
    result->output_count = 1;
    return true;
}

char *shader_graph_glsl_gradient_named(const shader_graph *g, const char *prefix, shader_error *error)
{
    if (!shader_graph_validate(g, error)) {
        return NULL;
    }
    shader_graph lowered;
    shader_graph_init(&lowered);
    char *source = gradient_lower(g, &lowered)
                     ? graph_glsl(&lowered, prefix, -1, true, error)
                     : NULL;
    shader_graph_free(&lowered);
    return source;
}

char *shader_graph_glsl_named(const shader_graph *g, const char *prefix, shader_error *error)
{
    return shader_graph_glsl_named_ramps(g, prefix, -1, error);
}
