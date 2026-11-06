#include "graph/authoring/graph_edit.h"
#include "graph/model/value_copy.h"
#include "graph/modeling/model_expand_internal.h"
#include "graph/modeling/model_profile_paths.h"
#include "graph/nodes/core_nodes.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool function_call(const node *n)
{
    return n->desc == &NODE_CallPureFunction || n->desc == &NODE_BehaviorExpression;
}
bool model_expand_error(expander *e, const scope *s, const node *n, const char *message)
{
    if (e->error) {
        e->error->node = n ? n->id : 0;
        snprintf(
            e->error->message,
            sizeof e->error->message,
            "%.28s [%.36s]: %.56s",
            s->asset->name,
            s->path,
            message
        );
    }
    return false;
}
bool model_expand_path(expander *e, const scope *source, const node *n, scope *target, char kind, int index)
{
    int length;
    if (index < 0) {
        length = snprintf(
            target->path,
            sizeof target->path,
            "%s/%c%llu",
            source->path,
            kind,
            (unsigned long long)n->id
        );
    } else {
        length = snprintf(
            target->path,
            sizeof target->path,
            "%s/%c%llu:%d",
            source->path,
            kind,
            (unsigned long long)n->id,
            index
        );
    }
    if (length < 0 || length >= MODEL_PATH_MAX) {
        return model_expand_error(e, source, n, "Model stable instance path exceeds its supported capacity");
    }
    return true;
}
/* Placement nodes change a geometry occurrence, not a reusable invocation's
 * controls or exported anchors. Keep function/array/reference components. */
void model_expand_identity(const scope *s, char out[MODEL_PATH_MAX])
{
    const char *start = s->path, *end = strchr(start, '/');
    int length = end ? (int)(end - start) : (int)strlen(start);
    memcpy(out, start, (size_t)length);
    out[length] = 0;
    while (end) {
        start = end + 1;
        end = strchr(start, '/');
        int count = end ? (int)(end - start) : (int)strlen(start);
        bool placement = count > 1 && start[0] == 't';
        for (int i = 1; i < count && placement; i++) {
            placement = start[i] >= '0' && start[i] <= '9';
        }
        if (placement) {
            continue;
        }
        out[length++] = '/';
        memcpy(out + length, start, (size_t)count);
        length += count;
        out[length] = 0;
    }
}
static int width(const pin_spec *pin)
{
    if (pin->kind != PIN_DATA || pin->container != PIN_CONTAINER_SINGLE) {
        return 0;
    }
    if (pin->type == PIN_F32 || pin->type == PIN_I32) {
        return 1;
    }
    if (pin->type != PIN_STRUCT || !pin->type_name) {
        return 0;
    }
    if (!strcmp(pin->type_name, "ShaderVec2")) {
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
static bool geometry(const pin_spec *pin)
{
    return pin->kind == PIN_DATA && pin->type == PIN_HANDLE && pin->type_name &&
           (!strcmp(pin->type_name, "Geometry3D") ||
            !strcmp(pin->type_name, "Profile2D")) &&
           pin->container == PIN_CONTAINER_SINGLE && !pin->binding &&
           pin->default_value.u64 == 0;
}
bool model_expand_enter(expander *e, const scope *s, const node *n)
{
    if (++e->work > SHADER_NODE_MAX * 8) {
        return model_expand_error(e, s, n, "Model expansion exceeds 1024 shader-node work budget");
    }
    for (int i = 0; i < e->depth; i++) {
        if (e->stack[i].graph == s->graph && e->stack[i].node == n &&
            e->stack[i].call == s->call && e->stack[i].instance == s->instance) {
            return model_expand_error(e, s, n, "Model construction contains a cycle");
        }
    }
    if (e->depth == MODEL_EXPAND_DEPTH) {
        return model_expand_error(e, s, n, "Model construction exceeds depth 128");
    }
    e->stack[e->depth].graph = s->graph;
    e->stack[e->depth].call = s->call;
    e->stack[e->depth].instance = s->instance;
    e->stack[e->depth++].node = n;
    bool found = false;
    for (int i = 0; i < acount(e->visited); i++) {
        found |= e->visited[i] == n;
    }
    if (!found) {
        apush(e->visited, n);
    }
    return true;
}
static bool literal(expander *e, const scope *s, const node *n, const pin_spec *pin, number_value *out)
{
    int w = width(pin);
    if (!w || pin->binding) {
        return model_expand_error(e, s, n, "Model numeric input requires a finite scalar/vector source");
    }
    *out = (number_value){ .width = w, .expression = -1 };
    if (pin->type == PIN_F32) {
        out->values[0] = pin->default_value.f;
    } else if (pin->type == PIN_I32) {
        out->values[0] = pin->default_value.i;
    } else if (pin->default_value.ptr) {
        memcpy(out->values, pin->default_value.ptr, w * sizeof(float));
    }
    for (int i = 0; i < w; i++) {
        if (!isfinite(out->values[i])) {
            return model_expand_error(e, s, n, "Model numeric values must be finite and positive when dimensions");
        }
    }
    return true;
}
bool model_expand_number_output(expander *, const scope *, const node *, int, number_value *);
bool model_expand_number_input(expander *e, const scope *s, const node *n, int pin, number_value *out)
{
    if (pin < 0 || pin >= n->input_count) {
        return model_expand_error(e, s, n, "Missing model numeric argument");
    }
    node *from;
    int output;
    if (!graph_input_source(s->graph, n, pin, &from, &output)) {
        return literal(e, s, n, &n->input_pins[pin], out);
    }
    if (graph_input_conversion(s->graph, n, pin)) {
        return model_expand_error(e, s, n, "Model numeric conversions require explicit numeric nodes");
    }
    return model_expand_number_output(e, s, from, output, out);
}
int model_expand_data_pin(const node *n, bool output, int ordinal)
{
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    for (int i = 0; i < count; i++) {
        if (pins[i].kind == PIN_DATA && ordinal-- == 0) {
            return i;
        }
    }
    return -1;
}
int model_expand_ordinal(const node *n, int pin)
{
    int result = 0;
    for (int i = 0; i < pin; i++) {
        result += n->output_pins[i].kind == PIN_DATA;
    }
    return result;
}
bool model_expand_function_scope(
    expander *e,
    const scope *s,
    const node *call,
    int output,
    scope *target,
    const node **ret,
    int *pin
)
{
    graph *g = behavior_asset_find_graph(s->asset, call->reference_id);
    if (!g || g->kind != GRAPH_FUNCTION) {
        return model_expand_error(e, s, call, "Missing model geometry function");
    }
    for (const scope *p = s; p; p = p->parent) {
        if (p->asset == s->asset && p->graph == g) {
            return model_expand_error(e, s, call, "Model function reference cycle");
        }
    }
    const node *entry = graph_node_by_id(g, g->entry_id);
    *ret = graph_node_by_id(g, g->return_id);
    if (!entry || entry->desc != &NODE_FunctionEntry || !*ret ||
        (*ret)->desc != &NODE_Return) {
        return model_expand_error(e, s, call, "Model function requires Entry and Return");
    }
    for (int i = 0; i < entry->output_count; i++) {
        if (entry->output_pins[i].kind != PIN_DATA) {
            return model_expand_error(e, s, call, "Model functions must be pure");
        }
    }
    for (int i = 0; i < (*ret)->input_count; i++) {
        if ((*ret)->input_pins[i].kind != PIN_DATA) {
            return model_expand_error(e, s, call, "Model functions must be pure");
        }
    }
    *pin = model_expand_data_pin(*ret, false, output);
    if (*pin < 0) {
        return model_expand_error(e, s, call, "Missing model function result");
    }
    *target = (scope){ .asset = s->asset, .graph = g, .parent = s, .call = call };
    if (!model_expand_path(e, s, call, target, 'f', -1)) {
        return false;
    }
    return true;
}
static bool parameter_value(expander *e, const scope *s, const node *n, number_value *out)
{
    model_parameter_decl decl;
    if (!model_parameter_get(n, &decl, e->error)) {
        return false;
    }
    *out = (number_value){ .width = decl.width, .expression = -1 };
    memcpy(out->values, decl.defaults, sizeof decl.defaults);
    char name[MODEL_PATH_MAX + 512];
    const char *prefix = (!strncmp(s->asset->name, "shape.", 6) ||
                          !strncmp(s->asset->name, "material.", 9))
                           ? "model."
                           : "";
    if (s->asset == e->root) {
        snprintf(name, sizeof name, "%s", decl.name);
    } else {
        snprintf(name, sizeof name, "%s%s.%s", prefix, s->asset->name, decl.name);
    }
    for (const scope *p = s; p; p = p->parent) {
        if (p->asset != s->asset || !p->instance) {
            continue;
        }
        for (int i = 0; i < p->instance->input_count; i++) {
            const pin_spec *pin = &p->instance->input_pins[i];
            if (pin->declaration_id != decl.id) {
                continue;
            }
            if (pin->type != decl.type || width(pin) != decl.width ||
                (decl.type_name &&
                 (!pin->type_name || strcmp(pin->type_name, decl.type_name)))) {
                return model_expand_error(e, p->parent, p->instance, "Model override type differs from its parameter declaration");
            }
            if (!model_expand_number_input(e, p->parent, p->instance, i, out)) {
                return false;
            }
            if (out->width != decl.width) {
                return model_expand_error(e, p->parent, p->instance, "Model override numeric width differs from its declaration");
            }
            for (int j = 0; j < decl.width; j++) {
                if (out->values[j] < decl.minimum[j] ||
                    out->values[j] > decl.maximum[j]) {
                    return model_expand_error(e, p->parent, p->instance, "Model override is outside the parameter range");
                }
            }
            if (out->expression >= 0) {
                model_numeric_constraint constraint = {
                    .node = p->instance->id,
                    .expression = out->expression,
                    .width = decl.width,
                    .asset = xstrdup(p->parent->asset->name)
                };
                memcpy(constraint.minimum, decl.minimum, sizeof decl.minimum);
                memcpy(constraint.maximum, decl.maximum, sizeof decl.maximum);
                apush(e->result->numeric.constraints, constraint);
                return true;
            }
            memcpy(decl.defaults, out->values, sizeof decl.defaults);
            char invocation[MODEL_PATH_MAX];
            model_expand_identity(p, invocation);
            snprintf(
                name,
                sizeof name,
                "%s%s.instance%llu.%s",
                (!strncmp(p->parent->asset->name, "shape.", 6) ||
                 !strncmp(p->parent->asset->name, "material.", 9))
                    ? "model."
                    : "",
                p->parent->asset->name,
                (unsigned long long)p->instance->id,
                decl.name
            );
            if (strchr(invocation + strlen(e->root->name) + 1, '/')) {
                snprintf(
                    name,
                    sizeof name,
                    "%s%s.%s",
                    (!strncmp(e->root->name, "shape.", 6) ||
                     !strncmp(e->root->name, "material.", 9))
                        ? "model."
                        : "",
                    invocation,
                    decl.name
                );
            }
            out->expression = model_numeric_parameter(
                &e->result->numeric,
                &decl,
                s->asset->name,
                name,
                invocation + strlen(e->root->name),
                e->error
            );
            return out->expression >= 0;
        }
        break;
    }
    out->expression = model_numeric_parameter(
        &e->result->numeric,
        &decl,
        s->asset->name,
        name,
        "",
        e->error
    );
    return out->expression >= 0;
}
bool model_expand_numeric_expression(
    expander *e,
    const node *n,
    shader_op operation,
    const number_value args[4],
    int count,
    number_value *out
)
{
    bool dynamic = false;
    for (int i = 0; i < count; i++) {
        dynamic |= args[i].expression >= 0;
    }
    if (!dynamic) {
        return true;
    }
    int inputs[4] = { -1, -1, -1, -1 };
    for (int i = 0; i < count; i++) {
        inputs[i] = args[i].expression;
        if (inputs[i] < 0) {
            inputs[i] = model_numeric_constant(
                &e->result->numeric,
                args[i].width,
                args[i].values,
                n->id,
                e->error
            );
        }
        if (inputs[i] < 0) {
            return false;
        }
    }
    out->expression = model_numeric_operation(
        &e->result->numeric,
        operation,
        out->width,
        inputs,
        n->id,
        e->error
    );
    return out->expression >= 0;
}
static bool numeric_node(expander *e, const scope *s, const node *n, int pin, number_value *out)
{
    if (pin < 0 || pin >= n->output_count || n->disabled) {
        return model_expand_error(e, s, n, "Invalid model numeric output");
    }
    if (n->desc == &NODE_ModelAnchor && pin >= 1 && pin <= 2) {
        model_anchor_decl declaration;
        return model_anchor_get(n, &declaration, e->error) &&
               model_expand_number_input(e, s, n, pin - 1, out);
    }
    if (n->desc == &NODE_ModelParameter) {
        return parameter_value(e, s, n, out);
    }
    if (n->desc == &NODE_FunctionEntry) {
        if (!s->call || !s->parent) {
            return model_expand_error(e, s, n, "Missing model function argument scope");
        }
        int argument = model_expand_data_pin(s->call, false, model_expand_ordinal(n, pin));
        return model_expand_number_input(e, s->parent, s->call, argument, out);
    }
    if (function_call(n)) {
        scope target;
        const node *ret;
        int result;
        if (!model_expand_function_scope(e, s, n, pin, &target, &ret, &result)) {
            return false;
        }
        return model_expand_number_input(e, &target, ret, result, out);
    }
    if (n->desc == &NODE_RerouteData) {
        return model_expand_number_input(e, s, n, 0, out);
    }
    const char *name = n->desc->name;
    if (!strcmp(name, "Shader Constant") ||
        (!n->input_count && n->desc->pure_kind == NODE_PURE_CONST)) {
        return literal(e, s, n, &n->output_pins[pin], out);
    }
    shader_op operation = SHADER_OP_COUNT;
    const struct
    {
        const char *name;
        shader_op op;
    } math[] = {
        { "Add", SHADER_ADD },           { "Subtract", SHADER_SUBTRACT },
        { "Multiply", SHADER_MULTIPLY }, { "Divide", SHADER_DIVIDE },
        { "Min", SHADER_MIN },           { "Max", SHADER_MAX },
        { "Abs", SHADER_ABS },           { "Sin", SHADER_SIN },
        { "Cos", SHADER_COS },           { "Sqrt", SHADER_SQRT },
        { "Clamp", SHADER_CLAMP },       { "Length", SHADER_LENGTH }
    };
    const char *short_name = !strncmp(name, "Shader ", 7) ? name + 7 : name;
    for (int i = 0; i < (int)(sizeof math / sizeof *math); i++) {
        if (!strcmp(short_name, math[i].name)) {
            operation = math[i].op;
            break;
        }
    }
    number_value args[4] = { 0 };
    if (n->input_count > 4) {
        return model_expand_error(e, s, n, "Unsupported model numeric operation");
    }
    for (int i = 0; i < n->input_count; i++) {
        if (!model_expand_number_input(e, s, n, i, &args[i])) {
            return false;
        }
    }
    int w = width(&n->output_pins[pin]);
    if (!w) {
        return model_expand_error(e, s, n, "Unsupported model numeric output type");
    }
    *out = (number_value){ .width = w, .expression = -1 };
    if (!strncmp(name, "Shader Vector", 13)) {
        for (int i = 0; i < w; i++) {
            if (i >= n->input_count || args[i].width != 1) {
                return model_expand_error(e, s, n, "Model vector construction requires scalar components");
            }
            out->values[i] = args[i].values[0];
        }
        return model_expand_numeric_expression(
            e,
            n,
            w == 2   ? SHADER_VECTOR2
            : w == 3 ? SHADER_VECTOR3
                     : SHADER_VECTOR4,
            args,
            w,
            out
        );
    }
    if (!strcmp(name, "Shader X") || !strcmp(name, "Shader Y") ||
        !strcmp(name, "Shader Z") || !strcmp(name, "Shader W")) {
        int axis = name[7] == 'X' ? 0
                 : name[7] == 'Y' ? 1
                 : name[7] == 'Z' ? 2
                                  : 3;
        if (axis >= args[0].width) {
            return model_expand_error(e, s, n, "Model numeric component is missing");
        }
        out->values[0] = args[0].values[axis];
        return model_expand_numeric_expression(e, n, SHADER_X + axis, args, 1, out);
    }
    if (!strcmp(name, "Shader Length")) {
        double squared = 0;
        for (int i = 0; i < args[0].width; i++) {
            squared += (double)args[0].values[i] * args[0].values[i];
        }
        out->values[0] = (float)sqrt(squared);
    } else {
        for (int axis = 0; axis < w; axis++) {
            double a = args[0].values[args[0].width == 1 ? 0 : axis],
                   b = args[1].values[args[1].width == 1 ? 0 : axis],
                   d = args[2].values[args[2].width == 1 ? 0 : axis], value = 0;
            if (!strcmp(name, "Shader Add") || !strcmp(name, "Add")) {
                value = a + b;
            } else if (!strcmp(name, "Shader Subtract") || !strcmp(name, "Subtract")) {
                value = a - b;
            } else if (!strcmp(name, "Shader Multiply") || !strcmp(name, "Multiply")) {
                value = a * b;
            } else if (!strcmp(name, "Shader Divide") || !strcmp(name, "Divide")) {
                if (b == 0) {
                    return model_expand_error(e, s, n, "Model numeric division by zero");
                }
                value = a / b;
            } else if (!strcmp(name, "Shader Min") || !strcmp(name, "Min")) {
                value = fmin(a, b);
            } else if (!strcmp(name, "Shader Max") || !strcmp(name, "Max")) {
                value = fmax(a, b);
            } else if (!strcmp(name, "Shader Abs") || !strcmp(name, "Abs")) {
                value = fabs(a);
            } else if (!strcmp(name, "Shader Sin")) {
                value = sin(a);
            } else if (!strcmp(name, "Shader Cos")) {
                value = cos(a);
            } else if (!strcmp(name, "Shader Sqrt")) {
                value = sqrt(a);
            } else if (!strcmp(name, "Shader Clamp") || !strcmp(name, "Clamp")) {
                value = fmax(b, fmin(d, a));
            } else {
                return model_expand_error(e, s, n, "Unsupported model numeric operation");
            }
            out->values[axis] = (float)value;
        }
    }
    for (int i = 0; i < w; i++) {
        if (!isfinite(out->values[i])) {
            return model_expand_error(e, s, n, "Model numeric expression is not finite");
        }
    }
    return model_expand_numeric_expression(e, n, operation, args, n->input_count, out);
}
bool model_expand_number_output(expander *e, const scope *s, const node *n, int pin, number_value *out)
{
    if (!model_expand_enter(e, s, n)) {
        return false;
    }
    bool valid = numeric_node(e, s, n, pin, out);
    e->depth--;
    return valid;
}
static node *geometry_output(expander *, const scope *, const node *, int);
node *model_expand_geometry_input(expander *e, const scope *s, const node *n, int pin)
{
    if (pin < 0 || pin >= n->input_count || !geometry(&n->input_pins[pin])) {
        model_expand_error(e, s, n, "Model Geometry is required as an unevaluated Geometry3D wire");
        return NULL;
    }
    node *from;
    int output;
    if (!graph_input_source(s->graph, n, pin, &from, &output) ||
        graph_input_conversion(s->graph, n, pin)) {
        model_expand_error(e, s, n, "Model Geometry is required as an unevaluated Geometry3D wire");
        return NULL;
    }
    if (!geometry(&from->output_pins[output]) ||
        strcmp(n->input_pins[pin].type_name, from->output_pins[output].type_name)) {
        model_expand_error(e, s, n, "Model construction requires an exact nominal Geometry3D or Profile2D wire");
        return NULL;
    }
    return geometry_output(e, s, from, output);
}
node *model_expand_add(expander *e, const scope *s, const node *source, node_desc *descriptor)
{
    if (acount(e->result->asset->graph->nodes) >= SHADER_NODE_MAX) {
        model_expand_error(e, s, source, "Model construction expansion exceeds 1024 nodes");
        return NULL;
    }
    node *n = graph_add_node(e->result->asset->graph, descriptor);
    apush(
        e->result->sources,
        ((model_source){ .generated = n->id,
                         .authored = source->id,
                         .asset = xstrdup(s->asset->name),
                         .path = xstrdup(s->path) })
    );
    return n;
}
bool model_expand_set_number(
    expander *e,
    const scope *s,
    const node *source,
    node *n,
    int pin,
    const number_value *value
)
{
    const pin_spec *p = &n->input_pins[pin];
    if (width(p) != value->width) {
        return model_expand_error(e, s, source, "Model numeric source type does not match construction input");
    }
    pin_value v = { .type = p->type, .type_name = p->type_name };
    if (p->type == PIN_F32) {
        v.f = value->values[0];
    } else if (p->type == PIN_I32) {
        v.i = (int)value->values[0];
    } else {
        v.ptr = (void *)value->values;
    }
    if (value->expression >= 0) {
        apush(e->result->inputs, ((model_numeric_input){ n->id, pin, value->expression }));
    }
    return graph_set_pin_default(e->result->asset->graph, n, 0, pin, &v) == 0;
}
static void dependency(expander *e, const char *name)
{
    for (int i = 0; i < acount(e->result->dependencies); i++) {
        if (!strcmp(e->result->dependencies[i], name)) {
            return;
        }
    }
    apush(e->result->dependencies, xstrdup(name));
}
static const node *model_root(expander *e, const scope *s)
{
    const node *root = NULL;
    for (int i = 0; i < acount(s->graph->nodes); i++) {
        const node *n = s->graph->nodes[i];
        if (n->desc != &NODE_ModelOutput) {
            continue;
        }
        if (n->disabled || n->input_count != 1 || n->output_count != 0) {
            model_expand_error(e, s, n, "Model Output must have its enabled construction contract");
            return NULL;
        }
        if (root) {
            model_expand_error(e, s, n, "Model requires exactly one Model Output");
            return NULL;
        }
        root = n;
    }
    if (!root) {
        model_expand_error(e, s, NULL, "Model requires exactly one Model Output");
    }
    return root;
}
typedef struct
{
    const graph *active[MODEL_EXPAND_DEPTH], *complete[SHADER_NODE_MAX];
    int depth, count, work;
} function_validator;
static bool function_dependencies(expander *e, const scope *s, const graph *g, function_validator *v)
{
    for (int i = 0; i < v->count; i++) {
        if (v->complete[i] == g) {
            return true;
        }
    }
    if (v->depth == MODEL_EXPAND_DEPTH || v->count == SHADER_NODE_MAX) {
        return model_expand_error(e, s, NULL, "Model function dependencies exceed supported limits");
    }
    v->active[v->depth++] = g;
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (++v->work > SHADER_NODE_MAX * 8) {
            return model_expand_error(e, s, n, "Model function dependency work exceeds supported limits");
        }
        if (!function_call(n)) {
            continue;
        }
        if (n->reference_asset && *n->reference_asset &&
            strcmp(n->reference_asset, s->asset->name)) {
            return model_expand_error(e, s, n, "Model functions must belong to their own model asset");
        }
        const graph *target = behavior_asset_find_graph(s->asset, n->reference_id);
        if (!target || target->kind != GRAPH_FUNCTION) {
            return model_expand_error(e, s, n, "Missing model geometry function");
        }
        for (int j = 0; j < v->depth; j++) {
            if (v->active[j] == target) {
                return model_expand_error(e, s, n, "Model function reference cycle");
            }
        }
        if (!function_dependencies(e, s, target, v)) {
            return false;
        }
    }
    v->depth--;
    v->complete[v->count++] = g;
    return true;
}
static bool validate_function_dependencies(expander *e, const scope *s)
{
    function_validator validator = { 0 };
    for (int i = 0; i < acount(s->asset->graphs); i++) {
        if (!function_dependencies(e, s, s->asset->graphs[i], &validator)) {
            return false;
        }
    }
    return true;
}
static bool material_dependency(expander *e, const scope *s, const node *n, const char *name)
{
    const behavior_asset *found = NULL;
    for (int i = 0; i < e->count; i++) {
        if (e->catalog[i]->domain == BEHAVIOR_DOMAIN_MATERIAL &&
            !strcmp(e->catalog[i]->name, name)) {
            if (found && found != e->catalog[i]) {
                return model_expand_error(e, s, n, "Ambiguous referenced Material asset");
            }
            found = e->catalog[i];
        }
    }
    if (!found) {
        char text[96];
        snprintf(text, sizeof text, "Missing Material asset '%.64s'", name);
        return model_expand_error(e, s, n, text);
    }
    char ***dependencies = &e->result->materials.dependencies;
    for (int i = 0; i < acount(*dependencies); i++) {
        if (!strcmp((*dependencies)[i], name)) {
            return true;
        }
    }
    apush(*dependencies, xstrdup(name));
    return true;
}
static int material_value(expander *e, const scope *s, const node *n)
{
    model_material_decl decl;
    if (!model_material_get(n, &decl, e->error) ||
        !material_dependency(e, s, n, decl.material)) {
        return -2;
    }
    const char *material = decl.material, *identity = "";
    char invocation[MODEL_PATH_MAX];
    for (const scope *p = s; p; p = p->parent) {
        if (p->asset != s->asset || !p->instance) {
            continue;
        }
        model_expand_identity(p, invocation);
        identity = invocation + strlen(e->root->name);
        for (int i = 0; i < p->instance->input_count; i++) {
            const pin_spec *pin = &p->instance->input_pins[i];
            if (pin->declaration_id != decl.id) {
                continue;
            }
            node *from;
            int from_pin;
            if (pin->type != PIN_STR || !pin->type_name ||
                strcmp(pin->type_name, "MaterialAsset") ||
                pin->container != PIN_CONTAINER_SINGLE || pin->binding ||
                graph_input_source(p->parent->graph, p->instance, i, &from, &from_pin) ||
                !graph_text_cstring(p->parent->graph, pin->default_value) ||
                !*graph_text_cstring(p->parent->graph, pin->default_value)) {
                model_expand_error(e, p->parent, p->instance, "Model material override requires a literal MaterialAsset name");
                return -2;
            }
            material = graph_text_cstring(p->parent->graph, pin->default_value);
            if (!material_dependency(e, p->parent, p->instance, material)) {
                return -2;
            }
        }
        break;
    }
    model_material_slot **slots = &e->result->materials.slots;
    for (int i = 0; i < acount(*slots); i++) {
        if ((*slots)[i].node == decl.id && !strcmp((*slots)[i].asset, s->asset->name) &&
            !strcmp((*slots)[i].scope, identity)) {
            return i;
        }
    }
    if (acount(*slots) == MODEL_MATERIAL_SLOT_MAX) {
        model_expand_error(e, s, n, "Model exceeds 32 material binding slots");
        return -2;
    }
    char name[MODEL_PATH_MAX + 512];
    if (!*identity && s->asset == e->root) {
        snprintf(name, sizeof name, "%s", decl.name);
    } else {
        snprintf(
            name,
            sizeof name,
            "%s%s%s.%s",
            (!strncmp(e->root->name, "shape.", 6) ||
             !strncmp(e->root->name, "material.", 9))
                ? "model."
                : "",
            e->root->name,
            identity,
            decl.name
        );
    }
    for (int i = 0; i < acount(*slots); i++) {
        if (!strcmp((*slots)[i].name, name)) {
            model_expand_error(e, s, n, "Model material slot lookup name is ambiguous");
            return -2;
        }
    }
    int index = acount(*slots);
    model_material_slot slot = { .slot = index,
                                 .node = decl.id,
                                 .asset = xstrdup(s->asset->name),
                                 .name = xstrdup(name),
                                 .scope = xstrdup(identity),
                                 .material = xstrdup(material) };
    apush(*slots, slot);
    return index;
}
static int material_output(expander *, const scope *, const node *, int);
static int material_input(expander *e, const scope *s, const node *n, int pin, bool optional)
{
    const pin_spec *p = &n->input_pins[pin];
    if (p->type != PIN_HANDLE || !p->type_name ||
        strcmp(p->type_name, "ModelMaterialSlot") ||
        p->container != PIN_CONTAINER_SINGLE || p->binding || p->default_value.u64) {
        model_expand_error(e, s, n, "Material requires an unevaluated ModelMaterialSlot wire");
        return -2;
    }
    node *from;
    int output;
    if (!graph_input_source(s->graph, n, pin, &from, &output)) {
        if (optional) {
            return -1;
        }
        model_expand_error(e, s, n, "Material slot function argument is required");
        return -2;
    }
    const pin_spec *source = &from->output_pins[output];
    if (source->type != PIN_HANDLE || !source->type_name ||
        strcmp(source->type_name, "ModelMaterialSlot") ||
        source->container != PIN_CONTAINER_SINGLE || source->binding ||
        source->default_value.u64 || graph_input_conversion(s->graph, n, pin)) {
        model_expand_error(e, s, n, "Material requires an exact nominal ModelMaterialSlot wire");
        return -2;
    }
    return material_output(e, s, from, output);
}
static int material_output(expander *e, const scope *s, const node *n, int pin)
{
    if (n->disabled || !model_expand_enter(e, s, n)) {
        return -2;
    }
    int result = -2;
    if (n->desc == &NODE_ModelMaterialSlot) {
        result = material_value(e, s, n);
    } else if (n->desc == &NODE_FunctionEntry) {
        if (s->call && s->parent) {
            result = material_input(
                e,
                s->parent,
                s->call,
                model_expand_data_pin(s->call, false, model_expand_ordinal(n, pin)),
                false
            );
        } else {
            model_expand_error(e, s, n, "Missing material slot function scope");
        }
    } else if (function_call(n)) {
        scope target;
        const node *ret;
        int result_pin;
        if (model_expand_function_scope(e, s, n, pin, &target, &ret, &result_pin)) {
            result = material_input(e, &target, ret, result_pin, false);
        }
    } else if (n->desc == &NODE_RerouteData) {
        result = material_input(e, s, n, 0, false);
    } else {
        model_expand_error(e, s, n, "Unsupported material slot construction");
    }
    e->depth--;
    return result;
}

static bool declarations(expander *e, const scope *s)
{
    if (!validate_function_dependencies(e, s)) {
        return false;
    }
    for (int gi = 0; gi < acount(s->asset->graphs); gi++) {
        for (int ni = 0; ni < acount(s->asset->graphs[gi]->nodes); ni++) {
            const node *n = s->asset->graphs[gi]->nodes[ni];
            if (n->desc != &NODE_ModelMaterialSlot) {
                continue;
            }
            if (s->asset->graphs[gi] != s->asset->graph) {
                return model_expand_error(e, s, n, "Material Slot declarations belong in the model root graph");
            }
            model_material_decl decl;
            if (!model_material_get(n, &decl, e->error) ||
                !material_dependency(e, s, n, decl.material)) {
                return false;
            }
            for (int j = 0; j < ni; j++) {
                const node *previous = s->asset->graphs[gi]->nodes[j];
                if ((previous->desc == &NODE_ModelMaterialSlot ||
                     previous->desc == &NODE_ModelParameter) &&
                    previous->reference_name &&
                    !strcmp(previous->reference_name, decl.name)) {
                    return model_expand_error(e, s, n, "Model public declaration names must be unique");
                }
            }
            for (int j = ni + 1; j < acount(s->asset->graph->nodes); j++) {
                const node *other = s->asset->graph->nodes[j];
                if (other->desc == &NODE_ModelParameter && other->reference_name &&
                    !strcmp(other->reference_name, decl.name)) {
                    return model_expand_error(e, s, n, "Model public declaration names must be unique");
                }
            }
        }
    }
    const graph *g = s->asset->graph;
    for (int i = 0; i < acount(g->nodes); i++) {
        const node *n = g->nodes[i];
        if (n->desc != &NODE_ModelParameter) {
            continue;
        }
        model_parameter_decl decl;
        if (!model_parameter_get(n, &decl, e->error)) {
            return false;
        }
        if (s->asset == e->root && !s->parent &&
            model_numeric_parameter(&e->result->numeric, &decl, s->asset->name, decl.name, "", e->error) <
                0) {
            return false;
        }
        for (int p = 0; p < 3; p++) {
            node *from;
            int pin;
            if (graph_input_source(g, n, p, &from, &pin)) {
                return model_expand_error(e, s, n, "Model Parameter declaration settings must be literals");
            }
        }
        for (int j = 0; j < i; j++) {
            const node *previous = g->nodes[j];
            if (previous->desc == &NODE_ModelParameter &&
                !strcmp(previous->reference_name, decl.name)) {
                return model_expand_error(e, s, n, "Model Parameter names must be unique");
            }
        }
    }
    return model_expand_anchor_declarations(e, s);
}
bool model_expand_instance_scope(expander *e, const scope *s, const node *n, scope *out)
{
    if (!n->reference_asset || !*n->reference_asset) {
        model_expand_error(e, s, n, "Model Instance requires a referenced model asset");
        return false;
    }
    const behavior_asset *asset = NULL;
    for (int i = 0; i < e->count; i++) {
        if (e->catalog[i]->domain != BEHAVIOR_DOMAIN_MODEL ||
            strcmp(e->catalog[i]->name, n->reference_asset)) {
            continue;
        }
        if (asset && asset != e->catalog[i]) {
            model_expand_error(e, s, n, "Ambiguous referenced model asset name");
            return false;
        }
        asset = e->catalog[i];
    }
    if (!strcmp(e->root->name, n->reference_asset)) {
        asset = e->root;
    }
    if (!strcmp(s->asset->name, n->reference_asset)) {
        asset = s->asset;
    }
    if (!asset || asset->domain != BEHAVIOR_DOMAIN_MODEL) {
        char message[96];
        snprintf(message, sizeof message, "Missing referenced model '%.52s'", n->reference_asset);
        model_expand_error(e, s, n, message);
        return false;
    }
    for (const scope *p = s; p; p = p->parent) {
        if (p->asset == asset) {
            model_expand_error(e, s, n, "Model asset reference cycle");
            return false;
        }
    }
    scope target = { .asset = asset, .graph = asset->graph, .parent = s, .instance = n };
    if (!model_expand_path(e, s, n, &target, 'r', -1)) {
        return false;
    }
    if (!declarations(e, &target)) {
        return false;
    }
    for (int i = 0; i < n->input_count; i++) {
        bool found = false;
        for (int j = 0; j < acount(asset->graph->nodes); j++) {
            const node *p = asset->graph->nodes[j];
            if (p->id != n->input_pins[i].declaration_id) {
                continue;
            }
            if (p->desc == &NODE_ModelParameter) {
                number_value value;
                if (!parameter_value(e, &target, p, &value)) {
                    return false;
                }
            } else if (p->desc == &NODE_ModelMaterialSlot) {
                if (material_value(e, &target, p) < -1) {
                    return false;
                }
            } else {
                continue;
            }
            found = true;
        }
        if (!found) {
            model_expand_error(e, s, n, "Model override refers to a missing parameter declaration");
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (n->input_pins[j].declaration_id == n->input_pins[i].declaration_id) {
                model_expand_error(e, s, n, "Model parameter override identity is duplicated");
                return false;
            }
        }
    }
    for (int i = 1; i < n->output_count; i++) {
        const pin_spec *pin = &n->output_pins[i];
        bool found = false;
        if (pin->type != PIN_HANDLE || !pin->type_name ||
            strcmp(pin->type_name, "Anchor3D")) {
            return model_expand_error(e, s, n, "Model instance exported output must be Anchor3D");
        }
        for (int j = 0; j < acount(asset->graph->nodes); j++) {
            const node *anchor = asset->graph->nodes[j];
            found |= anchor->desc == &NODE_ModelAnchor &&
                     anchor->id == pin->declaration_id;
        }
        if (!found) {
            return model_expand_error(e, s, n, "Model instance anchor declaration is missing");
        }
        for (int j = 1; j < i; j++) {
            if (n->output_pins[j].declaration_id == pin->declaration_id) {
                return model_expand_error(e, s, n, "Model instance anchor output identity is duplicated");
            }
        }
    }
    dependency(e, asset->name);
    *out = target;
    return true;
}
static node *instance(expander *e, const scope *s, const node *n)
{
    scope target;
    if (!model_expand_instance_scope(e, s, n, &target)) {
        return NULL;
    }
    const node *root = model_root(e, &target);
    return root ? model_expand_geometry_input(e, &target, root, 0) : NULL;
}
static node *array(expander *e, const scope *s, const node *n, bool radial)
{
    if (n->input_count != (radial ? 4 : 3) ||
        n->input_pins[1].type != PIN_I32 || n->input_pins[1].binding) {
        model_expand_error(e, s, n, "Model Array requires a literal structural Count");
        return NULL;
    }
    node *from;
    int pin;
    if (graph_input_source(s->graph, n, 1, &from, &pin)) {
        model_expand_error(e, s, n, "Model Array Count is structural and cannot be wired");
        return NULL;
    }
    int count = n->input_pins[1].default_value.i;
    if (count < 1 || count > MODEL_ARRAY_MAX) {
        model_expand_error(e, s, n, "Model Array Count must be between 1 and 64");
        return NULL;
    }
    int axis = radial ? n->input_pins[3].default_value.e : 0;
    if (radial && (axis < 0 || axis > 2)) {
        model_expand_error(e, s, n, "Model Array Axis must be X, Y or Z");
        return NULL;
    }
    number_value amount;
    if (!model_expand_number_input(e, s, n, 2, &amount) ||
        amount.width != (radial ? 1 : 3)) {
        model_expand_error(e, s, n, "Model Array step has the wrong type");
        return NULL;
    }
    node *combined = NULL;
    for (int i = 0; i < count; i++) {
        scope member = *s;
        if (!model_expand_path(e, s, n, &member, 'a', i)) {
            return NULL;
        }
        node *geometry = model_expand_geometry_input(e, &member, n, 0);
        if (!geometry) {
            return NULL;
        }
        node *transform = model_expand_add(e, s, n, radial ? &NODE_ModelRotate : &NODE_ModelTranslate);
        if (!transform) {
            return NULL;
        }
        number_value v = { .width = 3, .expression = -1 };
        if (radial) {
            v.values[axis] = amount.values[0] * i;
        } else {
            for (int j = 0; j < 3; j++) {
                v.values[j] = amount.values[j] * i;
            }
        }
        if (amount.expression >= 0 && i > 0) {
            number_value count_value = { .width = 1, .expression = -1, .values = { i } };
            number_value scaled = amount;
            number_value args[4] = { amount, count_value };
            if (!model_expand_numeric_expression(e, n, SHADER_MULTIPLY, args, 2, &scaled)) {
                return NULL;
            }
            if (radial) {
                number_value zero = { .width = 1, .expression = -1 };
                number_value components[4] = { zero, zero, zero };
                components[axis] = scaled;
                if (!model_expand_numeric_expression(e, n, SHADER_VECTOR3, components, 3, &v)) {
                    return NULL;
                }
            } else {
                v.expression = scaled.expression;
            }
        }
        for (int j = 0; j < 3; j++) {
            if (!isfinite(v.values[j])) {
                model_expand_error(e, s, n, "Model Array placement exceeds finite range");
                return NULL;
            }
        }
        if (!model_expand_set_number(e, s, n, transform, 1, &v)) {
            return NULL;
        }
        graph_connect_data(e->result->asset->graph, geometry, 0, transform, 0);
        if (!combined) {
            combined = transform;
            continue;
        }
        node *u = model_expand_add(e, s, n, &NODE_ModelUnion);
        if (!u) {
            return NULL;
        }
        graph_connect_data(e->result->asset->graph, combined, 0, u, 0);
        graph_connect_data(e->result->asset->graph, transform, 0, u, 1);
        combined = u;
    }
    return combined;
}
static node *bezier(expander *e, const scope *s, const node *n)
{
    bool profile = n->desc == &NODE_ModelProfileBezier;
    int steps_pin = profile ? 1 : 2;
    if (n->input_count != (profile ? 2 : 4) || n->output_count != 1 ||
        n->input_pins[0].type != PIN_STRUCT ||
        n->input_pins[0].container != PIN_CONTAINER_ARRAY || !n->input_pins[0].type_name ||
        strcmp(n->input_pins[0].type_name, profile ? "ShaderVec2" : "ShaderVec3") ||
        n->input_pins[steps_pin].type != PIN_I32 ||
        (!profile &&
         (n->input_pins[1].type != PIN_F32 || n->input_pins[3].type != PIN_BOOL))) {
        model_expand_error(e, s, n, "Bezier typed construction contract is invalid");
        return NULL;
    }
    for (int i = 0; i < n->input_count; i++) {
        if (i == 1 && !profile) {
            continue;
        }
        node *from;
        int pin;
        if (n->input_pins[i].binding ||
            graph_input_source(s->graph, n, i, &from, &pin)) {
            model_expand_error(e, s, n, "Bezier control points, Steps and Closed are structural literals");
            return NULL;
        }
    }
    pin_array *sampled = NULL;
    if (!model_profile_sample_cubic(
            n->input_pins[0].default_value.ptr,
            profile ? 2 : 3,
            profile || n->input_pins[3].default_value.b,
            n->input_pins[steps_pin].default_value.i,
            n->id,
            &sampled,
            e->error
        )) {
        return NULL;
    }
    node *result = model_expand_add(e, s, n, profile ? &NODE_ModelProfilePolygon : &NODE_ModelTubeSweep);
    if (!result) {
        model_profile_samples_free(sampled);
        return NULL;
    }
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = profile ? "ShaderVec2" : "ShaderVec3",
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = sampled };
    bool valid = !graph_set_pin_default(e->result->asset->graph, result, 0, 0, &value);
    model_profile_samples_free(sampled);
    if (!profile && valid) {
        number_value radius;
        valid = model_expand_number_input(e, s, n, 1, &radius) &&
                model_expand_set_number(e, s, n, result, 1, &radius);
        pin_value closed = n->input_pins[3].default_value;
        valid = valid &&
                !graph_set_pin_default(e->result->asset->graph, result, 0, 2, &closed);
    }
    return valid ? result : NULL;
}
static node *geometry_node(expander *e, const scope *s, const node *n, int pin)
{
    if (n->disabled || pin < 0 || pin >= n->output_count ||
        !geometry(&n->output_pins[pin])) {
        model_expand_error(e, s, n, "Model output must be unevaluated Geometry3D");
        return NULL;
    }
    if (n->desc == &NODE_FunctionEntry) {
        if (!s->call || !s->parent) {
            model_expand_error(e, s, n, "Missing model geometry function argument");
            return NULL;
        }
        scope caller = *s->parent;
        snprintf(caller.path, sizeof caller.path, "%s", s->path);
        return model_expand_geometry_input(
            e,
            &caller,
            s->call,
            model_expand_data_pin(s->call, false, model_expand_ordinal(n, pin))
        );
    }
    if (function_call(n)) {
        scope target;
        const node *ret;
        int result;
        if (!model_expand_function_scope(e, s, n, pin, &target, &ret, &result)) {
            return NULL;
        }
        return model_expand_geometry_input(e, &target, ret, result);
    }
    if (n->desc == &NODE_RerouteData) {
        return model_expand_geometry_input(e, s, n, 0);
    }
    if (n->desc == &NODE_ModelInstance) {
        return instance(e, s, n);
    }
    if (n->desc == &NODE_ModelLinearArray || n->desc == &NODE_ModelRadialArray) {
        return array(e, s, n, n->desc == &NODE_ModelRadialArray);
    }
    if (n->desc == &NODE_ModelRotateAround || n->desc == &NODE_ModelScaleAround) {
        return model_expand_around(e, s, n, n->desc == &NODE_ModelScaleAround);
    }
    if (n->desc == &NODE_ModelAttach) {
        return model_expand_attach(e, s, n);
    }
    if (n->desc == &NODE_ModelProfileBezier || n->desc == &NODE_ModelBezierTube) {
        return bezier(e, s, n);
    }
    bool supported = false;
    for (int i = 0; i < MODEL_PACK.node_count; i++) {
        supported |= MODEL_PACK.nodes[i] == n->desc;
    }
    if (!supported || n->desc == &NODE_ModelOutput ||
        n->input_count != n->desc->input_count || n->output_count != 1) {
        model_expand_error(e, s, n, "Unsupported model construction node");
        return NULL;
    }
    node *result = model_expand_add(e, s, n, (node_desc *)n->desc);
    if (!result) {
        return NULL;
    }
    if (n->desc == &NODE_ModelPart) {
        if (!n->reference_name || !*n->reference_name) {
            model_expand_error(e, s, n, "Model Part requires a readable name");
            return NULL;
        }
        char path[MODEL_PATH_MAX + 32];
        snprintf(path, sizeof path, "%s/p%llu", s->path, (unsigned long long)n->id);
        model_part part = { .node = n->id,
                            .name = xstrdup(n->reference_name),
                            .path = xstrdup(path),
                            .asset = xstrdup(s->asset->name),
                            .distance = -1,
                            .position = -1,
                            .material = -1 };
        int material = material_input(e, s, n, 1, true);
        if (material < -1) {
            free(part.name);
            free(part.path);
            free(part.asset);
            return NULL;
        }
        part.material = material;
        apush(e->result->parts, part);
        result->reference_id = acount(e->result->parts);
    }
    for (int i = 0; i < n->input_count; i++) {
        if (n->desc == &NODE_ModelSampledSdf) {
            node *from;
            int from_pin;
            if (n->input_pins[i].binding ||
                graph_input_source(s->graph, n, i, &from, &from_pin) ||
                graph_set_pin_default(
                    e->result->asset->graph,
                    result,
                    0,
                    i,
                    &n->input_pins[i].default_value
                )) {
                model_expand_error(e, s, n, "Sampled SDF metadata must be literal and cannot have runtime bindings");
                return NULL;
            }
            continue;
        }
        if (n->desc == &NODE_ModelPart && i == 1) {
            continue;
        }
        if (n->desc->inputs[i].container == PIN_CONTAINER_ARRAY) {
            node *from;
            int from_pin;
            const pin_spec *p = &n->input_pins[i];
            if (p->binding || graph_input_source(s->graph, n, i, &from, &from_pin) ||
                p->container != PIN_CONTAINER_ARRAY || p->type != PIN_STRUCT ||
                !p->type_name || strcmp(p->type_name, n->desc->inputs[i].type_name)) {
                model_expand_error(e, s, n, "Polygon Vertices require a literal structural ShaderVec2 array");
                return NULL;
            }
            if (graph_set_pin_default(e->result->asset->graph, result, 0, i, &p->default_value)) {
                model_expand_error(e, s, n, "Invalid polygon vertex array");
                return NULL;
            }
            continue;
        }
        if (n->input_pins[i].type == PIN_HANDLE) {
            scope context = *s;
            if (n->desc == &NODE_ModelTranslate || n->desc == &NODE_ModelRotate ||
                n->desc == &NODE_ModelScale || n->desc == &NODE_ModelReflect) {
                if (!model_expand_path(e, s, n, &context, 't', -1)) {
                    return NULL;
                }
            }
            node *child = model_expand_geometry_input(e, &context, n, i);
            if (!child) {
                return NULL;
            }
            graph_connect_data(e->result->asset->graph, child, 0, result, i);
            continue;
        }
        if (n->input_pins[i].type == PIN_ENUM || n->input_pins[i].type == PIN_BOOL) {
            if (n->input_pins[i].binding) {
                model_expand_error(e, s, n, "Model enum settings cannot have runtime bindings");
                return NULL;
            }
            pin_value value = n->input_pins[i].default_value;
            if (graph_set_pin_default(e->result->asset->graph, result, 0, i, &value)) {
                model_expand_error(e, s, n, "Invalid model enum setting");
                return NULL;
            }
            continue;
        }
        number_value value;
        if (!model_expand_number_input(e, s, n, i, &value) ||
            !model_expand_set_number(e, s, n, result, i, &value)) {
            return NULL;
        }
    }
    return result;
}
static node *geometry_output(expander *e, const scope *s, const node *n, int pin)
{
    if (!model_expand_enter(e, s, n)) {
        return NULL;
    }
    node *result = geometry_node(e, s, n, pin);
    e->depth--;
    return result;
}
void model_expansion_free(model_expansion *value)
{
    behavior_asset_free(value->asset);
    value->asset = NULL;
    for (int i = 0; i < acount(value->dependencies); i++) {
        free(value->dependencies[i]);
    }
    afree(value->dependencies);
    for (int i = 0; i < acount(value->parts); i++) {
        free(value->parts[i].name);
        free(value->parts[i].path);
        free(value->parts[i].asset);
    }
    afree(value->parts);
    for (int i = 0; i < acount(value->anchors); i++) {
        free(value->anchors[i].asset);
        free(value->anchors[i].name);
        free(value->anchors[i].scope);
    }
    afree(value->anchors);
    for (int i = 0; i < acount(value->sources); i++) {
        free(value->sources[i].asset);
        free(value->sources[i].path);
    }
    afree(value->sources);
    model_material_bindings_free(&value->materials);
    model_numeric_free(&value->numeric);
    afree(value->inputs);
}
bool model_expand(
    const behavior_asset *source,
    const behavior_asset *const *catalog,
    int count,
    model_expansion *result,
    shader_error *err
)
{
    *result = (model_expansion){ .asset = behavior_asset_new(source->name) };
    result->asset->domain = BEHAVIOR_DOMAIN_MODEL;
    model_numeric_init(&result->numeric);
    scope s = { .asset = source, .graph = source->graph };
    snprintf(s.path, sizeof s.path, "%s", source->name);
    expander e = { .result = result,
                   .root = source,
                   .catalog = catalog,
                   .count = count,
                   .error = err };
    bool valid = false;
    if (strlen(source->name) >= MODEL_PATH_MAX) {
        model_expand_error(&e, &s, NULL, "Model asset name exceeds stable identity capacity");
        goto done;
    }
    if (acount(source->variables)) {
        model_expand_error(&e, &s, NULL, "Model variables are unsupported; use dimension parameters");
        goto done;
    }
    if (!declarations(&e, &s)) {
        goto done;
    }
    const node *root = model_root(&e, &s);
    if (!root) {
        goto done;
    }
    node *built_geometry = model_expand_geometry_input(&e, &s, root, 0);
    if (!built_geometry) {
        goto done;
    }
    for (int i = 0; i < acount(source->graph->nodes); i++) {
        const node *n = source->graph->nodes[i];
        bool seen = n == root;
        for (int j = 0; j < acount(e.visited); j++) {
            seen |= e.visited[j] == n;
        }
        if (seen || n->desc == &NODE_ModelParameter ||
            n->desc == &NODE_ModelMaterialSlot || n->desc == &NODE_ModelAnchor) {
            continue;
        }
        if (n->output_count > 0 && geometry(&n->output_pins[0])) {
            if (!geometry_output(&e, &s, n, 0)) {
                goto done;
            }
        } else {
            number_value value;
            if (!model_expand_number_output(&e, &s, n, 0, &value)) {
                goto done;
            }
        }
    }
    node *output = model_expand_add(&e, &s, root, &NODE_ModelOutput);
    if (!output) {
        goto done;
    }
    graph_connect_data(result->asset->graph, built_geometry, 0, output, 0);
    valid = true;
done:
    afree(e.visited);
    if (!valid) {
        model_expansion_free(result);
    }
    return valid;
}
