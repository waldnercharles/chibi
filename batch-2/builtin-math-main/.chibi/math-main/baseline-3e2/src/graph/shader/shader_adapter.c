#include "graph/model/behavior_asset.h"
#include "graph/model/value_copy.h"
#include "graph/nodes/conv_nodes.h"
#include "graph/nodes/core_nodes.h"
#include "graph/shader/shader_graph.h"
#include "runtime/shaders/image.h"
#include "support/array.h"

#include <assert.h>
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
    const node *parameters[SHADER_PARAMETER_MAX];
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
    if (!strcmp(pin->type_name, "ShaderVec2") ||
        !strcmp(pin->type_name, "v2") || !strcmp(pin->type_name, "vec2")) {
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
int shader_node_input_context(const node *n, int pin, const char **label)
{
    if (label) {
        *label = NULL;
    }
    if (pin < 0 || pin >= n->input_count || n->input_pins[pin].type != PIN_STRUCT ||
        n->input_pins[pin].container != PIN_CONTAINER_SINGLE) {
        return -1;
    }
    int context = -1;
    const char *name = n->desc->name;
    if (!strcmp(name, "Shader Fresnel") && pin < 2) {
        context = pin == 0 ? SHADER_NORMAL : SHADER_VIEW_DIRECTION;
    }
    if ((!strcmp(name, "Shader Bump") && pin == 3)) {
        context = SHADER_NORMAL;
    }
    if (!strcmp(name, "Shader Normal Map") && pin == 1) {
        context = SHADER_UV;
    }
    if (pin == 0) {
        if (!strcmp(name, "Shader Mapping") || !strcmp(name, "Shader Gradient") ||
            !strcmp(name, "Shader Image Texture")) {
            context = SHADER_UV;
        }
        if (!strcmp(name, "Shader Planar Mapping") ||
            !strcmp(name, "Shader Cylindrical Mapping") ||
            !strcmp(name, "Shader Triplanar Texture") ||
            !strcmp(name, "Shader Decal Mask")) {
            context = SHADER_PART_POSITION;
        }
        if (!strcmp(name, "Shader Mapping3D")) {
            context = SHADER_OBJECT_POSITION;
        }
        if (!strcmp(name, "Shader Surface Influence") ||
            !strcmp(name, "Shader Star Field")) {
            context = SHADER_WORLD_POSITION;
        }
        if (!strcmp(name, "Shader Fractal Noise") || !strcmp(name, "Shader Voronoi")) {
            context = width(&n->input_pins[pin]) == 2 ? SHADER_UV
                                                      : SHADER_OBJECT_POSITION;
        }
    }
    if (label) {
        switch (context) {
            case SHADER_UV: *label = "Surface UV"; break;
            case SHADER_OBJECT_POSITION: *label = "Object Position"; break;
            case SHADER_PART_POSITION: *label = "Part Position"; break;
            case SHADER_WORLD_POSITION: *label = "World Position"; break;
            case SHADER_NORMAL: *label = "Surface Normal"; break;
            case SHADER_VIEW_DIRECTION: *label = "View Direction"; break;
            default: break;
        }
    }
    return context;
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
static int constant_settings(lowering *l, const scope *s, const node *n)
{
    for (int j = 0; j < n->input_count && j < n->desc->input_count; j++) {
        node *source = NULL;
        int source_pin = 0;
        if ((n->desc->inputs[j].flags & PIN_FLAG_SETTING) &&
            graph_input_source(s->source, n, j, &source, &source_pin)) {
            return error(l, n->id, "Shader settings must be authored constants");
        }
    }
    return 0;
}
static int input(lowering *l, scope *s, const node *n, int pin)
{
    node *from = NULL;
    int from_pin = 0;
    if (graph_input_source(s->source, n, pin, &from, &from_pin)) {
        const node_desc *conversion = graph_input_conversion(s->source, n, pin);
        if (conversion_is_vector_splat(conversion)) {
            int value = output(l, s, from, from_pin);
            if (value < 0) {
                return value;
            }
            int w = width(&n->input_pins[pin]);
            shader_op op = w == 2 ? SHADER_VECTOR2
                         : w == 3 ? SHADER_VECTOR3
                                  : SHADER_VECTOR4;
            int result = shader_graph_add(l->ir, op, w);
            l->ir->nodes[result].source_id = n->id;
            for (int axis = 0; axis < w; axis++) {
                l->ir->nodes[result].input[axis] = value;
            }
            return result;
        }
        if (conversion) {
            return error(l, n->id, "Use shader math or vector nodes for GPU conversions");
        }
        return output(l, s, from, from_pin);
    }
    const pin_spec *spec = &n->input_pins[pin];
    if (spec->binding) {
        return error(l, n->id, "Input bindings are unavailable in shader graphs");
    }
    if (spec->type == PIN_STRUCT && !spec->default_value.ptr) {
        int context = shader_node_input_context(n, pin, NULL);
        if (context >= 0) {
            int result = shader_graph_add(l->ir, context, width(spec));
            l->ir->nodes[result].source_id = n->id;
            return result;
        }
    }
    return literal(l, spec, n->id);
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
static int image_source(lowering *l, const scope *s, const node *n)
{
    int image = !strcmp(n->desc->name, "Shader Triplanar Texture")
                  ? 3
                  : SHADER_IMAGE_INPUT_IMAGE;
    int count = image + 4;
    if (n->input_count != count || n->input_pins[image].type != PIN_STR ||
        n->input_pins[image].container != PIN_CONTAINER_SINGLE) {
        return error(l, n->id, "Image Texture has an invalid image setting");
    }
    for (int i = image + 1; i < count; i++) {
        if (n->input_pins[i].type != PIN_I32 ||
            n->input_pins[i].container != PIN_CONTAINER_SINGLE) {
            return error(l, n->id, "Image Texture settings must be integer selections");
        }
    }
    const pin_spec *pins = n->input_pins;
    shader_image_source source = {
        .key = (char *)graph_text_cstring(s->source, pins[image].default_value),
        .color_space = pins[image + 1].default_value.i,
        .filter = pins[image + 2].default_value.i,
        .wrap = pins[image + 3].default_value.i
    };
    if (!source.key || !*source.key) {
        return error(l, n->id, "Choose an image for Image Texture");
    }
    if (!shader_image_source_valid(&source)) {
        return error(l, n->id, "Image Texture has an invalid color space, filter or wrap selection");
    }
    return shader_graph_add_image(l->ir, &source);
}
static int lower_node(lowering *l, scope *s, const node *n, int pin)
{
    if (n->disabled) {
        return error(l, n->id, "Disabled node cannot supply shader data");
    }
    if (pin < 0 || pin >= n->output_count) {
        return error(l, n->id, "Invalid shader output pin");
    }
    if (n->desc == &NODE_CallPureFunction || n->desc == &NODE_BehaviorExpression) {
        graph *target = n->desc == &NODE_BehaviorExpression && s->source->owner
                          ? behavior_asset_find_graph(s->source->owner, n->reference_id)
                          : NULL;
        if (n->desc == &NODE_BehaviorExpression &&
            (!target || target->expression_mode != GRAPH_EXPRESSION_VALUE)) {
            return error(l, n->id, "Only value expressions are supported in shader graphs");
        }
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
    if (constant_settings(l, s, n) < 0) {
        return -1;
    }
    if (op == SHADER_VORONOI && pin == 1) {
        op = SHADER_VORONOI_COLOR;
    }
    if (op == SHADER_COLOR_RAMP && pin == 1) {
        op = SHADER_COLOR_RAMP_ALPHA;
    }
    int image = -1;
    if (op == SHADER_IMAGE_TEXTURE || op == SHADER_TRIPLANAR_TEXTURE) {
        image = image_source(l, s, n);
        if (image < 0) {
            return -1;
        }
        if (pin == 1) {
            op = op == SHADER_IMAGE_TEXTURE ? SHADER_IMAGE_TEXTURE_ALPHA
                                            : SHADER_TRIPLANAR_TEXTURE_ALPHA;
        }
    }
    shader_ramp_stop stops[SHADER_RAMP_MAX];
    int stop_count = 0;
    if (op == SHADER_COLOR_RAMP || op == SHADER_COLOR_RAMP_ALPHA) {
        stop_count = shader_node_ramp_get(n, stops);
        if (!shader_ramp_valid(stops, stop_count)) {
            return error(l, n->id, "Color Ramp needs 2 to 16 ordered, finite stops in [0,1]");
        }
    }
    int detail_setting = op == SHADER_PLANAR_MAPPING || op == SHADER_CYLINDRICAL_MAPPING
                           ? 3
                       : op == SHADER_NORMAL_MAP ? 4
                                                 : -1;
    if (detail_setting >= 0 &&
        (n->input_pins[detail_setting].type != PIN_I32 ||
         n->input_pins[detail_setting].default_value.i < 0 ||
         n->input_pins[detail_setting].default_value.i >
             (op == SHADER_NORMAL_MAP ? SHADER_NORMAL_OPENGL_TANGENT : 2))) {
        return error(l, n->id, "Invalid surface detail setting selection");
    }
    int setting = op == SHADER_GRADIENT ? 1 : stop_count ? 2 : -1;
    if (setting >= 0 &&
        (n->input_pins[setting].type != PIN_I32 ||
         n->input_pins[setting].default_value.i < 0 ||
         n->input_pins[setting].default_value.i > (stop_count ? 2 : 3))) {
        return error(l, n->id, "Invalid shader setting selection");
    }
    int args[4] = { -1, -1, -1, -1 }, w = width(&n->output_pins[pin]);
    if (!w) {
        return error(l, n->id, "Unsupported shader output type");
    }
    for (int j = 0; j < shader_op_inputs(op); j++) {
        if (j >= n->input_count) {
            return error(l, n->id, "Missing shader input");
        }
        args[j] = input(l, s, n, stop_count && j == 1 ? 2 : j);
        if (args[j] < 0) {
            return -1;
        }
    }
    int index = shader_graph_add(l->ir, op, w);
    shader_node *ir = &l->ir->nodes[index];
    ir->source_id = n->id;
    ir->image = image;
    if (detail_setting >= 0) {
        ir->value[0] = n->input_pins[detail_setting].default_value.i;
    }
    memcpy(ir->input, args, sizeof args);
    for (int i = 0; i < stop_count; i++) {
        apush(ir->ramp, stops[i]);
    }
    if (op == SHADER_CONSTANT || op == SHADER_PARAMETER) {
        if (op == SHADER_PARAMETER) {
            if (n->reference_id >= SHADER_PARAMETER_MAX) {
                return error(l, n->id, "Parameter slot must be between 0 and 31");
            }
            const node *previous = l->parameters[n->reference_id];
            if (previous && previous != n) {
                return error(l, n->id, "Distinct Shader Parameters must use different slots");
            }
            l->parameters[n->reference_id] = n;
        }
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
bool shader_graph_from_function(const graph *source, shader_graph *result, shader_error *err)
{
    shader_graph ir;
    shader_graph_init(&ir);
    lowering l = { .ir = &ir, .error = err };
    scope s = { .source = source };
    if (!source || source->kind != GRAPH_FUNCTION) {
        error(&l, 0, "Shader function graph is invalid");
        goto failed;
    }
    const node *entry = graph_node_by_id(source, source->entry_id);
    const node *ret = graph_node_by_id(source, source->return_id);
    if (!entry || entry->desc != &NODE_FunctionEntry) {
        error(&l, source->entry_id, "Shader function needs a Function Entry node");
        goto failed;
    }
    if (!ret || ret->desc != &NODE_Return) {
        error(&l, source->return_id, "Shader function needs a Return node");
        goto failed;
    }
    int ordinal = 0;
    for (int i = 0; i < entry->output_count; i++) {
        const pin_spec *pin = &entry->output_pins[i];
        if (pin->kind != PIN_DATA) {
            continue;
        }
        int w = width(pin);
        if (!w) {
            error(&l, entry->id, "Shader function arguments require scalar or vector values");
            goto failed;
        }
        if (ordinal >= SHADER_PARAMETER_MAX) {
            error(&l, entry->id, "Shader function has too many arguments");
            goto failed;
        }
        int argument = literal(&l, pin, entry->id);
        ir.nodes[argument].op = SHADER_PARAMETER;
        ir.nodes[argument].parameter = ordinal++;
        apush(s.arguments, argument);
    }
    for (int i = 0; i < ret->input_count; i++) {
        if (ret->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        if (ir.output_count >= SHADER_OUTPUT_MAX) {
            error(&l, ret->id, "Shader function has too many results");
            goto failed;
        }
        int value = input(&l, &s, ret, i);
        if (value < 0) {
            goto failed;
        }
        if (ir.nodes[value].width != width(&ret->input_pins[i])) {
            error(&l, ret->id, "Shader function result has the wrong type");
            goto failed;
        }
        ir.output[ir.output_count++] = value;
    }
    if (!shader_graph_validate(&ir, err)) {
        goto failed;
    }
    afree(s.arguments);
    afree(s.cache);
    afree(s.visiting);
    shader_graph_free(result);
    *result = ir;
    return true;
failed:
    afree(s.arguments);
    afree(s.cache);
    afree(s.visiting);
    shader_graph_free(&ir);
    return false;
}
bool shader_graph_validate_function(const graph *source, shader_error *err)
{
    shader_graph ir;
    shader_graph_init(&ir);
    bool valid = shader_graph_from_function(source, &ir, err);
    shader_graph_free(&ir);
    return valid;
}
/* A Surface Output's Opacity under its Alpha Mode: Opaque keeps every pixel,
 * and Mask turns Opacity into 1 where it reaches Alpha Cutoff and 0 below. */
static int surface_opacity(lowering *l, scope *s, const node *root)
{
    const pin_spec *mode = &root->input_pins[SHADER_SURFACE_ALPHA_MODE];
    if (mode->type != PIN_I32 || mode->default_value.i < SHADER_ALPHA_BLEND ||
        mode->default_value.i > SHADER_ALPHA_MASK) {
        return error(l, root->id, "Invalid Alpha Mode");
    }
    if (mode->default_value.i == SHADER_ALPHA_OPAQUE) {
        int whole = shader_graph_add(l->ir, SHADER_CONSTANT, 1);
        l->ir->nodes[whole].source_id = root->id;
        l->ir->nodes[whole].value[0] = 1;
        return whole;
    }
    int value = input(l, s, root, SHADER_SURFACE_OPACITY);
    if (value < 0 || mode->default_value.i == SHADER_ALPHA_BLEND) {
        return value;
    }
    int cutoff = input(l, s, root, SHADER_SURFACE_ALPHA_CUTOFF);
    if (cutoff < 0) {
        return -1;
    }
    int mask = shader_graph_add(l->ir, SHADER_STEP, 1);
    l->ir->nodes[mask].source_id = root->id;
    l->ir->nodes[mask].input[0] = value;
    l->ir->nodes[mask].input[1] = cutoff;
    return mask;
}
shader_alpha_mode shader_graph_alpha_mode(const graph *source)
{
    for (int i = 0; i < acount(source->nodes); i++) {
        const node *n = source->nodes[i];
        if (!strcmp(n->desc->name, "Shader Surface Output")) {
            assert(n->input_count == SHADER_SURFACE_INPUT_COUNT);
            return n->input_pins[SHADER_SURFACE_ALPHA_MODE].default_value.i;
        }
    }
    return SHADER_ALPHA_BLEND;
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
        if (strcmp(n->desc->name, "Shader Surface Output") &&
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
        error(&l, 0, "Shader graph needs a Surface or Shape Output node");
        goto failed;
    }
    bool shape = !strcmp(root->desc->name, "Shader Shape Output");
    if (source->owner &&
        ((source->owner->domain == BEHAVIOR_DOMAIN_MATERIAL && shape) ||
         (source->owner->domain == BEHAVIOR_DOMAIN_SHAPE && !shape))) {
        error(&l, root->id, "Shader output does not match the asset domain");
        goto failed;
    }
    if (root->input_count != (shape ? 1 : SHADER_SURFACE_INPUT_COUNT)) {
        error(&l, root->id, "Shader output has the wrong number of inputs");
        goto failed;
    }
    if (constant_settings(&l, &s, root) < 0) {
        goto failed;
    }
    ir.output_count = shape ? 1 : SHADER_OUTPUT_MAX;
    for (int i = 0; i < ir.output_count; i++) {
        ir.output[i] = shape || i != SHADER_SURFACE_OPACITY
                         ? input(&l, &s, root, i)
                         : surface_opacity(&l, &s, root);
        if (ir.output[i] < 0) {
            goto failed;
        }
    }
    for (int i = 0; i < ir.output_count; i++) {
        int expected = shape || (i != SHADER_SURFACE_BASE_COLOR &&
                                 i != SHADER_SURFACE_EMISSION &&
                                 i != SHADER_SURFACE_NORMAL)
                         ? 1
                         : 3;
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
