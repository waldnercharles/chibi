#include "graph/authoring/graph_edit.h"
#include "graph/modeling/model_expand_internal.h"
#include "graph/modeling/model_internal.h"
#include "graph/nodes/core_nodes.h"
#include "support/xalloc.h"

#include <stdlib.h>
#include <string.h>

static bool vector_pin(const pin_spec *p)
{
    return p->kind == PIN_DATA && p->type == PIN_STRUCT &&
           p->container == PIN_CONTAINER_SINGLE && p->type_name &&
           !strcmp(p->type_name, "ShaderVec3");
}
bool model_anchor_get(const node *n, model_anchor_decl *out, shader_error *error)
{
    if (n->desc != &NODE_ModelAnchor || n->disabled || n->input_count != 2 ||
        n->output_count != 3 || !n->reference_name || !*n->reference_name ||
        !vector_pin(&n->input_pins[0]) || !vector_pin(&n->input_pins[1]) ||
        !vector_pin(&n->output_pins[1]) || !vector_pin(&n->output_pins[2]) ||
        n->output_pins[0].type != PIN_HANDLE || !n->output_pins[0].type_name ||
        strcmp(n->output_pins[0].type_name, "Anchor3D")) {
        return model_fail(error, n->id, "Model Anchor requires a name and its enabled typed contract");
    }
    *out = (model_anchor_decl){ n->id, n->reference_name };
    return true;
}
static bool nominal(const pin_spec *p)
{
    return p->type == PIN_HANDLE && p->type_name &&
           !strcmp(p->type_name, "Anchor3D") && !p->binding;
}
bool model_expand_anchor_input(expander *e, const scope *s, const node *n, int pin, anchor_value *out)
{
    node *from;
    int output;
    if (pin < 0 || pin >= n->input_count || !nominal(&n->input_pins[pin]) ||
        !graph_input_source(s->graph, n, pin, &from, &output) ||
        graph_input_conversion(s->graph, n, pin) ||
        !nominal(&from->output_pins[output])) {
        return model_expand_error(e, s, n, "Anchor requires an exact unevaluated Anchor3D wire");
    }
    return model_expand_anchor_output(e, s, from, output, out);
}
static int expression(expander *e, const node *n, const number_value *v)
{
    return v->expression >= 0
             ? v->expression
             : model_numeric_constant(&e->result->numeric, v->width, v->values, n->id, e->error);
}
static bool anchor_node(expander *e, const scope *s, const node *n, int pin, anchor_value *out)
{
    if (n->desc == &NODE_ModelAnchor) {
        model_anchor_decl decl;
        if (pin != 0 || !model_anchor_get(n, &decl, e->error) ||
            !model_expand_number_input(e, s, n, 0, &out->position) ||
            !model_expand_number_input(e, s, n, 1, &out->angles) ||
            out->position.width != 3 || out->angles.width != 3) {
            return model_expand_error(e, s, n, "Anchor requires vector position and angles");
        }
        char invocation[MODEL_PATH_MAX];
        model_expand_identity(s, invocation);
        const char *path = strchr(invocation, '/');
        path = path ? path + 1 : "";
        for (int i = 0; i < acount(e->result->anchors); i++) {
            const model_anchor *a = &e->result->anchors[i];
            if (a->node == n->id && !strcmp(a->asset, s->asset->name) &&
                !strcmp(a->scope, path)) {
                return true;
            }
        }
        int p = expression(e, n, &out->position), a = expression(e, n, &out->angles);
        if (p < 0 || a < 0) {
            return false;
        }
        apush(
            e->result->anchors,
            ((model_anchor){ .node = n->id,
                             .asset = xstrdup(s->asset->name),
                             .name = xstrdup(decl.name),
                             .scope = xstrdup(path),
                             .position = p,
                             .angles = a })
        );
        return true;
    }
    if (n->desc == &NODE_ModelInstance) {
        scope target;
        if (!model_expand_instance_scope(e, s, n, &target)) {
            return false;
        }
        for (int i = 0; i < acount(target.graph->nodes); i++) {
            const node *a = target.graph->nodes[i];
            if (a->desc == &NODE_ModelAnchor &&
                a->id == n->output_pins[pin].declaration_id) {
                return model_expand_anchor_output(e, &target, a, 0, out);
            }
        }
        return model_expand_error(e, s, n, "Instance anchor refers to a missing declaration");
    }
    if (n->desc == &NODE_FunctionEntry && s->parent && s->call) {
        int input = model_expand_data_pin(s->call, false, model_expand_ordinal(n, pin));
        return model_expand_anchor_input(e, s->parent, s->call, input, out);
    }
    if (n->desc == &NODE_CallPureFunction || n->desc == &NODE_BehaviorExpression) {
        scope target;
        const node *ret;
        int input;
        if (!model_expand_function_scope(e, s, n, model_expand_ordinal(n, pin), &target, &ret, &input)) {
            return false;
        }
        return model_expand_anchor_input(e, &target, ret, input, out);
    }
    if (n->desc == &NODE_RerouteData) {
        return model_expand_anchor_input(e, s, n, 0, out);
    }
    return model_expand_error(e, s, n, "Unsupported anchor source");
}
bool model_expand_anchor_output(expander *e, const scope *s, const node *n, int pin, anchor_value *out)
{
    if (pin < 0 || pin >= n->output_count || !nominal(&n->output_pins[pin]) ||
        n->disabled) {
        return model_expand_error(e, s, n, "Anchor output contract is invalid");
    }
    if (!model_expand_enter(e, s, n)) {
        return false;
    }
    bool valid = anchor_node(e, s, n, pin, out);
    e->depth--;
    return valid;
}
bool model_expand_anchor_declarations(expander *e, const scope *s)
{
    for (int i = 0; i < acount(s->graph->nodes); i++) {
        const node *n = s->graph->nodes[i];
        if (n->desc != &NODE_ModelAnchor) {
            continue;
        }
        for (int j = 0; j < i; j++) {
            const node *p = s->graph->nodes[j];
            if (p->desc == &NODE_ModelAnchor && p->reference_name &&
                n->reference_name && !strcmp(p->reference_name, n->reference_name)) {
                return model_expand_error(e, s, n, "Anchor declaration names must be unique");
            }
        }
        anchor_value value;
        if (!model_expand_anchor_output(e, s, n, 0, &value)) {
            return false;
        }
    }
    return true;
}
static bool negate(expander *e, const node *n, const number_value *v, number_value *out)
{
    *out = *v;
    out->expression = -1;
    for (int i = 0; i < v->width; i++) {
        out->values[i] = -v->values[i];
    }
    number_value args[4] = { *v, { .width = 1, .expression = -1, .values = { -1 } } };
    return model_expand_numeric_expression(e, n, SHADER_MULTIPLY, args, 2, out);
}
static node *transform(
    expander *e,
    const scope *s,
    const node *source,
    node *child,
    node_desc *desc,
    const number_value *v
)
{
    node *n = model_expand_add(e, s, source, desc);
    if (!n || !model_expand_set_number(e, s, source, n, 1, v)) {
        return NULL;
    }
    if (graph_connect_data(e->result->asset->graph, child, 0, n, 0)) {
        return NULL;
    }
    return n;
}
node *model_expand_around(expander *e, const scope *s, const node *n, bool scale)
{
    if (n->input_count != 3 || n->output_count != 1) {
        model_expand_error(e, s, n, "Pivot transform contract is invalid");
        return NULL;
    }
    number_value pivot, amount, negative;
    if (!model_expand_number_input(e, s, n, 1, &pivot) ||
        !model_expand_number_input(e, s, n, 2, &amount) || pivot.width != 3 ||
        amount.width != 3 || !negate(e, n, &pivot, &negative)) {
        return NULL;
    }
    scope nested = *s;
    if (!model_expand_path(e, s, n, &nested, 't', -1)) {
        return NULL;
    }
    node *child = model_expand_geometry_input(e, &nested, n, 0);
    if (!child) {
        return NULL;
    }
    child = transform(e, s, n, child, &NODE_ModelTranslate, &negative);
    if (child) {
        child = transform(e, s, n, child, scale ? &NODE_ModelScale : &NODE_ModelRotate, &amount);
    }
    return child ? transform(e, s, n, child, &NODE_ModelTranslate, &pivot) : NULL;
}
node *model_expand_attach(expander *e, const scope *s, const node *n)
{
    if (n->input_count != 3 || n->output_count != 1) {
        model_expand_error(e, s, n, "Attach contract is invalid");
        return NULL;
    }
    anchor_value from, to;
    number_value negative;
    if (!model_expand_anchor_input(e, s, n, 1, &from) ||
        !model_expand_anchor_input(e, s, n, 2, &to) ||
        !negate(e, n, &from.position, &negative)) {
        return NULL;
    }
    scope nested = *s;
    if (!model_expand_path(e, s, n, &nested, 't', -1)) {
        return NULL;
    }
    node *child = model_expand_geometry_input(e, &nested, n, 0);
    if (!child) {
        return NULL;
    }
    child = transform(e, s, n, child, &NODE_ModelTranslate, &negative);
    for (int axis = 2; axis >= 0 && child; axis--) {
        if (from.angles.expression < 0 && from.angles.values[axis] == 0) {
            continue;
        }
        number_value component = { .width = 1,
                                   .expression = -1,
                                   .values = { from.angles.values[axis] } };
        number_value args[4] = { from.angles };
        if (!model_expand_numeric_expression(e, n, SHADER_X + axis, args, 1, &component)) {
            return NULL;
        }
        number_value neg;
        if (!negate(e, n, &component, &neg)) {
            return NULL;
        }
        number_value angles = { .width = 3, .expression = -1 };
        angles.values[axis] = neg.values[0];
        number_value components[4];
        for (int i = 0; i < 3; i++) {
            components[i] = (number_value){ .width = 1, .expression = -1 };
        }
        components[axis] = neg;
        if (!model_expand_numeric_expression(e, n, SHADER_VECTOR3, components, 3, &angles)) {
            return NULL;
        }
        child = transform(e, s, n, child, &NODE_ModelRotate, &angles);
    }
    if (child) {
        child = transform(e, s, n, child, &NODE_ModelRotate, &to.angles);
    }
    return child ? transform(e, s, n, child, &NODE_ModelTranslate, &to.position) : NULL;
}
