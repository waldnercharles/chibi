#include "graph/modeling/model_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool model_material_get(const node *n, model_material_decl *out, shader_error *error)
{
    if (n->disabled || n->desc != &NODE_ModelMaterialSlot || n->input_count ||
        n->output_count != 1 || !n->reference_name || !*n->reference_name ||
        strlen(n->reference_name) > 255 || !n->reference_asset || !*n->reference_asset) {
        return model_fail(error, n->id, "Model Material Slot requires a name and default Material asset");
    }
    const pin_spec *p = &n->output_pins[0];
    if (p->type != PIN_HANDLE || !p->type_name ||
        strcmp(p->type_name, "ModelMaterialSlot") ||
        p->container != PIN_CONTAINER_SINGLE || p->binding || p->default_value.u64) {
        return model_fail(error, n->id, "Material Slot requires a compiler-only nominal Slot output");
    }
    *out = (model_material_decl){ n->id, n->reference_name, n->reference_asset };
    return true;
}
static bool parameters_equal(const model_program *p, const model_numeric *n)
{
    int public_count = 0;
    for (int i = 0; i < acount(p->parameters); i++) {
        public_count += !p->parameters[i].hidden;
    }
    if (public_count != acount(n->parameters)) {
        return false;
    }
    for (int i = 0; i < public_count; i++) {
        const model_parameter *a = &p->parameters[i], *b = &n->parameters[i];
        if (a->slot != b->slot || a->node != b->node || a->width != b->width ||
            a->type != b->type || strcmp(a->asset, b->asset) ||
            strcmp(a->name, b->name) || strcmp(a->scope, b->scope) ||
            memcmp(a->defaults, b->defaults, sizeof a->defaults) ||
            memcmp(a->minimum, b->minimum, sizeof a->minimum) ||
            memcmp(a->maximum, b->maximum, sizeof a->maximum)) {
            return false;
        }
    }
    return true;
}
static bool construction_equal(const model_program *p, const model_expansion *e)
{
    const struct model_bounds_program *old = p->bounds_program;
    if (!old || acount(e->parts) != acount(p->parts) ||
        acount(e->inputs) != acount(old->inputs) ||
        acount(e->numeric.graph.nodes) != acount(old->numeric.graph.nodes) ||
        !parameters_equal(p, &e->numeric)) {
        return false;
    }
    for (int i = 0; i < acount(e->parts); i++) {
        const model_part *a = &p->parts[i], *b = &e->parts[i];
        if (a->node != b->node || strcmp(a->asset, b->asset) ||
            strcmp(a->path, b->path) || strcmp(a->name, b->name)) {
            return false;
        }
    }
    if (acount(p->anchors) != acount(e->anchors)) {
        return false;
    }
    for (int i = 0; i < acount(p->anchors); i++) {
        const model_anchor *a = &p->anchors[i], *b = &e->anchors[i];
        if (a->node != b->node || a->position != b->position ||
            a->angles != b->angles || strcmp(a->asset, b->asset) ||
            strcmp(a->name, b->name) || strcmp(a->scope, b->scope)) {
            return false;
        }
    }
    for (int i = 0; i < acount(e->inputs); i++) {
        if (e->inputs[i].node != old->inputs[i].node ||
            e->inputs[i].pin != old->inputs[i].pin ||
            e->inputs[i].expression != old->inputs[i].expression) {
            return false;
        }
    }
    if (acount(e->numeric.constraints) != acount(old->numeric.constraints)) {
        return false;
    }
    for (int i = 0; i < acount(e->numeric.constraints); i++) {
        const model_numeric_constraint *a = &e->numeric.constraints[i],
                                       *b = &old->numeric.constraints[i];
        if (a->node != b->node || a->expression != b->expression ||
            a->width != b->width || strcmp(a->asset, b->asset) ||
            memcmp(a->minimum, b->minimum, sizeof a->minimum) ||
            memcmp(a->maximum, b->maximum, sizeof a->maximum)) {
            return false;
        }
    }
    for (int i = 0; i < acount(e->numeric.graph.nodes); i++) {
        if (memcmp(&e->numeric.graph.nodes[i], &old->numeric.graph.nodes[i], sizeof(shader_node))) {
            return false;
        }
    }
    model_bounds_plan *
        candidate = model_prepare_bounds(e->asset->graph, e->inputs, acount(e->inputs), NULL);
    bool equal = model_bounds_plan_equal(old->plan, candidate);
    model_bounds_plan_free(candidate);
    return equal;
}
model_bindings_result model_program_compile_bindings(
    const model_program *geometry,
    const behavior_asset *source,
    const behavior_asset *const *catalog,
    int count,
    model_material_bindings *result,
    shader_error *error
)
{
    if (source->domain != BEHAVIOR_DOMAIN_MODEL) {
        model_fail(error, 0, "Material binding refresh requires a model asset");
        return MODEL_BINDINGS_INVALID;
    }
    model_expansion expanded;
    if (!model_expand(source, catalog, count, &expanded, error)) {
        return MODEL_BINDINGS_INVALID;
    }
    if (!construction_equal(geometry, &expanded)) {
        model_expansion_free(&expanded);
        return MODEL_BINDINGS_GEOMETRY_CHANGED;
    }
    model_material_bindings fresh = expanded.materials;
    model_material_bindings_init(&expanded.materials);
    for (int i = 0; i < acount(expanded.parts); i++) {
        apush(fresh.part_slots, expanded.parts[i].material);
    }
    model_expansion_free(&expanded);
    model_material_bindings_free(result);
    *result = fresh;
    return MODEL_BINDINGS_READY;
}
