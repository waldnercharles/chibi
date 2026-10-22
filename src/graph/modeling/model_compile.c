#include "graph/modeling/model_compile.h"

#include "graph/model/value_copy.h"
#include "graph/modeling/model_internal.h"
#include "runtime/shaders/volume.h"
#include "support/xalloc.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int model_emit(compiler *c, const node *source, shader_op op, int width, int a, int b, int d)
{
    if (c->failed) {
        return 0;
    }
    if (op != SHADER_CONSTANT && op != SHADER_PARAMETER && op != SHADER_SAMPLED_SDF) {
        for (int i = 0; i < acount(c->shader->nodes); i++) {
            const shader_node *n = &c->shader->nodes[i];
            if (n->op == op && n->width == width && n->input[0] == a &&
                n->input[1] == b && n->input[2] == d && n->input[3] == -1) {
                return i;
            }
        }
    }
    if (acount(c->shader->nodes) == SHADER_NODE_MAX) {
        c->failed = true;
        model_fail(c->error, source->id, "Model expansion exceeds 1024 shader nodes");
        return 0;
    }
    int index = shader_graph_add(c->shader, op, width);
    shader_node *n = &c->shader->nodes[index];
    n->source_id = source->id;
    n->input[0] = a;
    n->input[1] = b;
    n->input[2] = d;
    return index;
}
int model_constant(compiler *c, const node *n, int width, const float *value)
{
    for (int i = 0; !c->failed && i < acount(c->shader->nodes); i++) {
        const shader_node *existing = &c->shader->nodes[i];
        if (existing->op == SHADER_CONSTANT && existing->width == width &&
            !memcmp(existing->value, value, width * sizeof(float))) {
            return i;
        }
    }
    int index = model_emit(c, n, SHADER_CONSTANT, width, -1, -1, -1);
    if (!c->failed) {
        memcpy(c->shader->nodes[index].value, value, width * sizeof(float));
    }
    return index;
}
int model_scalar(compiler *c, const node *n, float value)
{
    return model_constant(c, n, 1, &value);
}
int model_unary(compiler *c, const node *n, shader_op op, int width, int a)
{
    return model_emit(c, n, op, width, a, -1, -1);
}
int model_binary(compiler *c, const node *n, shader_op op, int width, int a, int b)
{
    return model_emit(c, n, op, width, a, b, -1);
}
int model_coordinate(compiler *c, const node *n, int p, int axis)
{
    return model_unary(c, n, SHADER_X + axis, 1, p);
}
static int make3(compiler *c, const node *n, int x, int y, int z)
{
    return model_emit(c, n, SHADER_VECTOR3, 3, x, y, z);
}
static int expression(const compiler *c, const node *n, int pin)
{
    const struct model_bounds_program *b = c->program->bounds_program;
    if (!b) {
        return -1;
    }
    for (int i = 0; i < acount(b->inputs); i++) {
        if (b->inputs[i].node == n->id && b->inputs[i].pin == pin) {
            return b->inputs[i].expression;
        }
    }
    return -1;
}
int model_field(compiler *c, const node *n, int pin)
{
    int value = expression(c, n, pin);
    if (value >= 0) {
        return value;
    }
    if (n->input_pins[pin].type == PIN_F32) {
        return model_scalar(c, n, model_number(n, pin));
    }
    float v[3];
    model_vector(n, pin, v);
    return model_constant(c, n, !strcmp(n->input_pins[pin].type_name, "ShaderVec2") ? 2 : 3, v);
}
static int rotation_rows(compiler *c, const node *n)
{
    struct model_bounds_program *b = c->program->bounds_program;
    if (!b || expression(c, n, 1) < 0) {
        return -1;
    }
    for (int i = 0; i < acount(b->rotations); i++) {
        if (b->rotations[i].node == n->id) {
            return b->rotations[i].slot;
        }
    }
    if (acount(c->program->parameters) + 3 > SHADER_PARAMETER_MAX) {
        c->failed = true;
        model_fail(c->error, n->id, "Model rotation exceeds 32 public and derived parameter slots");
        return -1;
    }
    int slot = acount(c->program->parameters);
    for (int i = 0; i < 3; i++) {
        model_parameter param = { .slot = slot + i,
                                  .width = 4,
                                  .node = n->id,
                                  .hidden = true,
                                  .asset = xstrdup(""),
                                  .name = xstrdup(""),
                                  .scope = xstrdup(""),
                                  .type = PIN_STRUCT,
                                  .type_name = xstrdup("ShaderVec4") };
        apush(c->program->parameters, param);
    }
    apush(b->rotations, ((model_rotation_uniform){ n->id, expression(c, n, 1), slot }));
    return slot;
}
static int uniform(compiler *c, const node *n, int slot)
{
    int value = model_emit(c, n, SHADER_PARAMETER, 4, -1, -1, -1);
    if (!c->failed) {
        c->shader->nodes[value].parameter = slot;
    }
    return value;
}
static int box_distance(compiler *c, const node *n, int p, int half)
{
    int q = model_binary(c, n, SHADER_SUBTRACT, 3, model_unary(c, n, SHADER_ABS, 3, p), half);
    int zero = model_scalar(c, n, 0);
    int outside = model_unary(c, n, SHADER_LENGTH, 1, model_binary(c, n, SHADER_MAX, 3, q, zero));
    int maxxy = model_binary(
        c,
        n,
        SHADER_MAX,
        1,
        model_coordinate(c, n, q, 0),
        model_coordinate(c, n, q, 1)
    );
    int inside = model_binary(
        c,
        n,
        SHADER_MIN,
        1,
        model_binary(c, n, SHADER_MAX, 1, maxxy, model_coordinate(c, n, q, 2)),
        zero
    );
    return model_binary(c, n, SHADER_ADD, 1, outside, inside);
}
int model_lower(compiler *c, const node *n, int p)
{
    if (c->failed) {
        return 0;
    }
    if (!model_enter(c, n)) {
        c->failed = true;
        return 0;
    }
    int op = model_operation(n), result = 0;
    if (op == MODEL_SAMPLED_SDF) {
        const char *key = graph_text_cstring(c->source, n->input_pins[0].default_value);
        if (!key) {
            model_fail(c->error, n->id, "Model resource contains embedded NUL");
            c->failed = true;
            return 0;
        }
        int image = 0;
        for (; image < acount(c->shader->images); image++) {
            if (!strcmp(c->shader->images[image].key, key)) {
                break;
            }
        }
        if (image == acount(c->shader->images)) {
            shader_image_source source = { .key = xstrdup(key),
                                           .color_space = SHADER_IMAGE_LINEAR,
                                           .filter = SHADER_IMAGE_NEAREST,
                                           .wrap = SHADER_IMAGE_CLAMP };
            apush(c->shader->images, source);
        }
        result = model_unary(c, n, SHADER_SAMPLED_SDF, 1, p);
        if (!c->failed) {
            shader_node *volume = &c->shader->nodes[result];
            volume->image = image;
            volume->parameter = n->input_pins[2].default_value.i;
            model_vector(n, 1, volume->value);
            volume->volume_range = model_number(n, 3);
            volume->volume_lipschitz = model_number(n, 4);
        }
    } else if (op == MODEL_OFFSET || op == MODEL_SHELL || op == MODEL_TUBE_SWEEP) {
        result = model_lower_constructive(c, n, p);
    } else if (op == MODEL_EXTRUDE || op == MODEL_REVOLVE || op == MODEL_ROUNDED_EXTRUDE || op == MODEL_BEVELED_EXTRUDE) {
        result = model_lower_extrusion(c, n, p);
    } else if (op == MODEL_PART) {
        result = model_lower(c, model_child(c->source, n, 0), p);
        if (n->reference_id > 0 &&
            n->reference_id <= (uint64_t)acount(c->program->parts)) {
            c->program->parts[n->reference_id - 1].distance = result;
            c->program->parts[n->reference_id - 1].position = p;
        }
    } else if (op >= MODEL_TRANSLATE && op <= MODEL_REFLECT) {
        float v[3] = { 0 };
        if (op != MODEL_REFLECT) {
            model_vector(n, 1, v);
        }
        int q = p, rotation_factor = -1;
        if (op == MODEL_TRANSLATE || op == MODEL_SCALE) {
            q = model_binary(
                c,
                n,
                op == MODEL_TRANSLATE ? SHADER_SUBTRACT : SHADER_DIVIDE,
                3,
                p,
                model_field(c, n, 1)
            );
        }
        if (op == MODEL_ROTATE) {
            int slots = rotation_rows(c, n);
            double m[3][3], forward[3][3];
            float factor;
            model_implemented_rotation(v, m, forward, &factor);
            int xyz[3];
            for (int axis = 0; axis < 3; axis++) {
                int row;
                if (slots >= 0) {
                    int u = uniform(c, n, slots + axis);
                    row = make3(
                        c,
                        n,
                        model_coordinate(c, n, u, 0),
                        model_coordinate(c, n, u, 1),
                        model_coordinate(c, n, u, 2)
                    );
                    if (axis == 0) {
                        rotation_factor = model_unary(c, n, SHADER_W, 1, u);
                    }
                } else {
                    float values[3];
                    for (int j = 0; j < 3; j++) {
                        values[j] = m[axis][j];
                    }
                    row = model_constant(c, n, 3, values);
                }
                xyz[axis] = model_binary(c, n, SHADER_DOT, 1, p, row);
            }
            q = make3(c, n, xyz[0], xyz[1], xyz[2]);
        }
        if (op == MODEL_REFLECT) {
            float factors[3] = { 1, 1, 1 };
            factors[n->input_pins[1].default_value.e] = -1;
            q = model_binary(c, n, SHADER_MULTIPLY, 3, p, model_constant(c, n, 3, factors));
        }
        result = model_lower(c, model_child(c->source, n, 0), q);
        if (op == MODEL_ROTATE) {
            double inverse[3][3], forward[3][3];
            float factor;
            model_implemented_rotation(v, inverse, forward, &factor);
            result = model_binary(
                c,
                n,
                SHADER_MULTIPLY,
                1,
                result,
                rotation_factor >= 0 ? rotation_factor : model_scalar(c, n, factor)
            );
        }
        if (op == MODEL_SCALE) {
            result = model_binary(
                c,
                n,
                SHADER_MULTIPLY,
                1,
                result,
                expression(c, n, 1) < 0
                    ? model_scalar(c, n, fminf(v[0], fminf(v[1], v[2])))
                    : model_binary(
                          c,
                          n,
                          SHADER_MIN,
                          1,
                          model_coordinate(c, n, model_field(c, n, 1), 0),
                          model_binary(
                              c,
                              n,
                              SHADER_MIN,
                              1,
                              model_coordinate(c, n, model_field(c, n, 1), 1),
                              model_coordinate(c, n, model_field(c, n, 1), 2)
                          )
                      )
            );
        }
    } else if (op >= MODEL_UNION && op <= MODEL_SMOOTH_UNION) {
        int a = model_lower(c, model_child(c->source, n, 0), p),
            b = model_lower(c, model_child(c->source, n, 1), p);
        shader_op operation = op == MODEL_UNION        ? SHADER_UNION
                            : op == MODEL_INTERSECTION ? SHADER_INTERSECTION
                            : op == MODEL_DIFFERENCE   ? SHADER_DIFFERENCE
                                                       : SHADER_SMOOTH_UNION;
        result = model_emit(
            c,
            n,
            operation,
            1,
            a,
            b,
            op == MODEL_SMOOTH_UNION ? model_field(c, n, 2) : -1
        );
    } else if (op == MODEL_SPHERE) {
        result = model_binary(
            c,
            n,
            SHADER_SUBTRACT,
            1,
            model_unary(c, n, SHADER_LENGTH, 1, p),
            model_field(c, n, 0)
        );
    } else if (op == MODEL_BOX || op == MODEL_ROUNDED_BOX) {
        int half = model_field(c, n, 0);
        int radius = op == MODEL_ROUNDED_BOX ? model_field(c, n, 1)
                                             : model_scalar(c, n, 0);
        if (op == MODEL_ROUNDED_BOX) {
            half = model_binary(c, n, SHADER_SUBTRACT, 3, half, radius);
        }
        result = box_distance(c, n, p, half);
        if (op == MODEL_ROUNDED_BOX) {
            result = model_binary(c, n, SHADER_SUBTRACT, 1, result, radius);
        }
    } else {
        int x = model_coordinate(c, n, p, 0), y = model_coordinate(c, n, p, 1),
            z = model_coordinate(c, n, p, 2);
        if (op == MODEL_CAPSULE) {
            int h = model_field(c, n, 1);
            int clamped = model_emit(
                c,
                n,
                SHADER_CLAMP,
                1,
                y,
                model_binary(c, n, SHADER_MULTIPLY, 1, h, model_scalar(c, n, -1)),
                h
            );
            int q = make3(c, n, x, model_binary(c, n, SHADER_SUBTRACT, 1, y, clamped), z);
            result = model_binary(
                c,
                n,
                SHADER_SUBTRACT,
                1,
                model_unary(c, n, SHADER_LENGTH, 1, q),
                model_field(c, n, 0)
            );
        } else {
            int xz = model_emit(c, n, SHADER_VECTOR2, 2, x, z, -1);
            int radial = model_binary(
                c,
                n,
                SHADER_SUBTRACT,
                1,
                model_unary(c, n, SHADER_LENGTH, 1, xz),
                model_field(c, n, 0)
            );
            if (op == MODEL_TORUS) {
                int q = model_emit(c, n, SHADER_VECTOR2, 2, radial, y, -1);
                result = model_binary(
                    c,
                    n,
                    SHADER_SUBTRACT,
                    1,
                    model_unary(c, n, SHADER_LENGTH, 1, q),
                    model_field(c, n, 1)
                );
            } else {
                int height = model_binary(
                    c,
                    n,
                    SHADER_SUBTRACT,
                    1,
                    model_unary(c, n, SHADER_ABS, 1, y),
                    model_field(c, n, 1)
                );
                int q = model_emit(c, n, SHADER_VECTOR2, 2, radial, height, -1),
                    zero = model_scalar(c, n, 0);
                int inside = model_binary(
                    c,
                    n,
                    SHADER_MIN,
                    1,
                    model_binary(c, n, SHADER_MAX, 1, radial, height),
                    zero
                );
                int outside = model_unary(
                    c,
                    n,
                    SHADER_LENGTH,
                    1,
                    model_binary(c, n, SHADER_MAX, 2, q, zero)
                );
                result = model_binary(c, n, SHADER_ADD, 1, inside, outside);
            }
        }
    }
    if (c->program->bounds_program) {
        apush(c->program->bounds_program->hit_nodes, ((model_hit_node){ n->id, result, p }));
    }
    c->depth--;
    return result;
}
static bool compile_graph(const graph *source, model_program *program, shader_error *error)
{
    if (!model_construction_bounds(source, &program->bounds, error)) {
        return false;
    }
    const node *output = NULL;
    for (int i = 0; i < acount(source->nodes); i++) {
        if (model_operation(source->nodes[i]) == MODEL_OUTPUT) {
            output = source->nodes[i];
        }
    }
    const node *root = model_child(source, output, 0);
    compiler c = { .source = source,
                   .shader = &program->shader,
                   .program = program,
                   .error = error };
    if (program->bounds_program &&
        acount(program->bounds_program->numeric.graph.nodes) &&
        !shader_graph_clone(&program->bounds_program->numeric.graph, &program->shader, error)) {
        return false;
    }
    int p = model_emit(&c, root, SHADER_OBJECT_POSITION, 3, -1, -1, -1);
    if (program->bounds_program) {
        program->bounds_program->root_position = p;
    }
    int distance = model_lower(&c, root, p);
    if (c.failed) {
        return false;
    }
    program->shader.output_count = 1;
    program->shader.output[0] = distance;
    float defaults[32][4] = { { 0 } };
    return shader_graph_validate(&program->shader, error) &&
           (program->bounds_program ||
            model_finite_program(&program->shader, &program->bounds, defaults, NULL, 0, error));
}
bool model_compile_with_catalog(
    const behavior_asset *asset,
    const behavior_asset *const *catalog,
    int count,
    model_program *result,
    shader_error *error
)
{
    if (asset->domain != BEHAVIOR_DOMAIN_MODEL) {
        return model_fail(error, 0, "Model compiler requires a model asset");
    }
    model_expansion expanded;
    if (!model_expand(asset, catalog, count, &expanded, error)) {
        return false;
    }
    model_program fresh;
    model_program_init(&fresh);
    fresh.dependencies = expanded.dependencies;
    expanded.dependencies = NULL;
    fresh.material_slots = expanded.materials.slots;
    expanded.materials.slots = NULL;
    fresh.material_dependencies = expanded.materials.dependencies;
    expanded.materials.dependencies = NULL;
    fresh.anchors = expanded.anchors;
    expanded.anchors = NULL;
    fresh.parts = expanded.parts;
    expanded.parts = NULL;
    fresh.bounds_program = xcalloc(1, sizeof *fresh.bounds_program);
    struct model_bounds_program *bp = fresh.bounds_program;
    const graph *construction = expanded.asset->graph;
    bp->plan = model_prepare_bounds(
        construction,
        expanded.inputs,
        acount(expanded.inputs),
        error
    );
    bp->numeric = expanded.numeric;
    model_numeric_init(&expanded.numeric);
    bp->inputs = expanded.inputs;
    expanded.inputs = NULL;
    bp->sources = expanded.sources;
    expanded.sources = NULL;
    fresh.parameters = bp->numeric.parameters;
    bp->numeric.parameters = NULL;
    bool valid = bp->plan && compile_graph(construction, &fresh, error);
    if (valid) {
        valid = model_build_hit(&fresh, construction, error);
    }
    if (!valid && error) {
        for (int i = 0; i < acount(bp->sources); i++) {
            if (bp->sources[i].generated == error->node) {
                error->node = bp->sources[i].authored;
                char message[sizeof error->message];
                snprintf(message, sizeof message, "%s", error->message);
                snprintf(
                    error->message,
                    sizeof error->message,
                    "%.24s [%.24s]: %.72s",
                    bp->sources[i].asset,
                    bp->sources[i].path,
                    message
                );
                break;
            }
        }
    }
    if (valid) {
        for (int i = acount(bp->numeric.graph.nodes); i < acount(fresh.shader.nodes);
             i++) {
            for (int j = 0; j < acount(bp->sources); j++) {
                if (bp->sources[j].generated == fresh.shader.nodes[i].source_id) {
                    fresh.shader.nodes[i].source_id = bp->sources[j].authored;
                    break;
                }
            }
        }
    }
    if (valid) {
        for (int i = bp->hit_numeric_count; i < acount(fresh.hit_shader.nodes); i++) {
            for (int j = 0; j < acount(bp->sources); j++) {
                if (fresh.hit_shader.nodes[i].source_id == bp->sources[j].generated) {
                    fresh.hit_shader.nodes[i].source_id = bp->sources[j].authored;
                    break;
                }
            }
        }
    }
    if (valid) {
        valid = shader_graph_clone(&fresh.shader, &bp->proof, error) &&
                shader_graph_clone(&fresh.hit_shader, &bp->hit_proof, error) &&
                model_build_part_shaders(&fresh, construction, error);
        float prepared[SHADER_PARAMETER_MAX][4];
        valid = valid &&
                model_program_prepare(&fresh, NULL, prepared, &fresh.bounds, error);
        if (valid) {
            for (int i = 0; i < acount(fresh.shader.nodes); i++) {
                if (fresh.shader.nodes[i].op == SHADER_PARAMETER) {
                    memcpy(
                        fresh.shader.nodes[i].value,
                        prepared[fresh.shader.nodes[i].parameter],
                        sizeof fresh.shader.nodes[i].value
                    );
                }
            }
            for (int i = 0; i < acount(fresh.hit_shader.nodes); i++) {
                if (fresh.hit_shader.nodes[i].op == SHADER_PARAMETER) {
                    memcpy(
                        fresh.hit_shader.nodes[i].value,
                        prepared[fresh.hit_shader.nodes[i].parameter],
                        sizeof fresh.hit_shader.nodes[i].value
                    );
                }
            }
        }
    }
    model_expansion_free(&expanded);
    if (!valid) {
        model_program_free(&fresh);
        return false;
    }
    model_program_free(result);
    *result = fresh;
    return true;
}
bool model_compile(const behavior_asset *asset, model_program *result, shader_error *error)
{
    return model_compile_with_catalog(asset, NULL, 0, result, error);
}
bool model_graph_validate(const graph *source, shader_error *error)
{
    model_program program;
    model_program_init(&program);
    bool valid = source->owner ? model_compile(source->owner, &program, error)
                               : compile_graph(source, &program, error);
    model_program_free(&program);
    return valid;
}
