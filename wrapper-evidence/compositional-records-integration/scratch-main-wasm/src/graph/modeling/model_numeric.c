#include "graph/modeling/model_numeric.h"

#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failure(shader_error *error, uint64_t node, const char *message)
{
    if (error) {
        error->node = node;
        snprintf(error->message, sizeof error->message, "%s", message);
    }
    return -1;
}
int model_numeric_operation(
    model_numeric *n,
    shader_op op,
    int width,
    const int inputs[4],
    uint64_t source,
    shader_error *error
)
{
    if (inputs && op != SHADER_CONSTANT && op != SHADER_PARAMETER) {
        for (int i = 0; i < acount(n->graph.nodes); i++) {
            const shader_node *existing = &n->graph.nodes[i];
            if (existing->op == op && existing->width == width &&
                !memcmp(existing->input, inputs, sizeof existing->input)) {
                return i;
            }
        }
    }
    if (acount(n->graph.nodes) == SHADER_NODE_MAX) {
        return failure(error, source, "Model numeric expansion exceeds 1024 shader nodes");
    }
    int index = shader_graph_add(&n->graph, op, width);
    n->graph.nodes[index].source_id = source;
    if (inputs) {
        memcpy(n->graph.nodes[index].input, inputs, sizeof n->graph.nodes[index].input);
    }
    n->graph.output_count = 1;
    n->graph.output[0] = index;
    return index;
}
int model_numeric_constant(model_numeric *n, int width, const float values[4], uint64_t source, shader_error *error)
{
    for (int i = 0; i < acount(n->graph.nodes); i++) {
        const shader_node *existing = &n->graph.nodes[i];
        if (existing->op == SHADER_CONSTANT && existing->width == width &&
            !memcmp(existing->value, values, width * sizeof(float))) {
            return i;
        }
    }
    int index = model_numeric_operation(n, SHADER_CONSTANT, width, NULL, source, error);
    if (index >= 0) {
        memcpy(n->graph.nodes[index].value, values, width * sizeof(float));
    }
    return index;
}
int model_numeric_parameter(
    model_numeric *n,
    const model_parameter_decl *d,
    const char *asset,
    const char *name,
    const char *scope,
    shader_error *error
)
{
    int slot = -1;
    for (int i = 0; i < acount(n->parameters); i++) {
        model_parameter *p = &n->parameters[i];
        if (p->node == d->id && !strcmp(p->asset, asset) && !strcmp(p->scope, scope)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < acount(n->parameters); i++) {
            if (!strcmp(n->parameters[i].name, name)) {
                return failure(error, d->id, "Model public parameter lookup name is ambiguous");
            }
        }
        if (acount(n->parameters) == SHADER_PARAMETER_MAX) {
            return failure(error, d->id, "Model exceeds 32 public and derived parameter slots");
        }
        model_parameter p = { .slot = acount(n->parameters),
                              .width = d->width,
                              .node = d->id,
                              .asset = xstrdup(asset),
                              .name = xstrdup(name),
                              .scope = xstrdup(scope),
                              .type_name = d->type_name ? xstrdup(d->type_name) : NULL,
                              .type = d->type };
        memcpy(p.defaults, d->defaults, sizeof p.defaults);
        memcpy(p.minimum, d->minimum, sizeof p.minimum);
        memcpy(p.maximum, d->maximum, sizeof p.maximum);
        apush(n->parameters, p);
        slot = p.slot;
    }
    for (int i = 0; i < acount(n->graph.nodes); i++) {
        if (n->graph.nodes[i].op == SHADER_PARAMETER &&
            n->graph.nodes[i].parameter == slot) {
            return i;
        }
    }
    int index = model_numeric_operation(n, SHADER_PARAMETER, d->width, NULL, d->id, error);
    if (index >= 0) {
        n->graph.nodes[index].parameter = slot;
        memcpy(n->graph.nodes[index].value, d->defaults, sizeof d->defaults);
    }
    return index;
}
