#include "editor/ui/labels.h"
#include "editor/workspace/editor_internal.h"
#include "graph/nodes/compare_nodes.h"
#include "graph/nodes/component_nodes.h"
#include "graph/nodes/math_nodes.h"
#include "support/array.h"

#include <stdio.h>
#include <string.h>

bool ed_is_graph_call(const node *n)
{
    return n && (n->desc == &NODE_CallFunction || n->desc == &NODE_CallPureFunction ||
                 n->desc == &NODE_BehaviorExpression || n->desc == &NODE_CallMacro);
}

const char *ed_node_symbol(const node_desc *desc)
{
    static const struct
    {
        const char *name;
        const char *symbol;
    } shader_symbols[] = {
        { "Shader Add", "+" },
        { "Shader Subtract", "−" },
        { "Shader Multiply", "×" },
        { "Shader Divide", "÷" },
    };
    for (int i = 0; i < (int)(sizeof shader_symbols / sizeof *shader_symbols); i++) {
        if (!strcmp(desc->name, shader_symbols[i].name)) {
            return shader_symbols[i].symbol;
        }
    }
    switch (desc->jit_kind) {
        case NODE_JIT_ADD: return "+";
        case NODE_JIT_SUB: return "−";
        case NODE_JIT_MUL: return "×";
        case NODE_JIT_DIV: return "÷";
        default: break;
    }
    if (desc == &NODE_Less || desc == &NODE_Integer_Less) {
        return "<";
    }
    if (desc == &NODE_Less_Or_Equal || desc == &NODE_Integer_Less_Or_Equal) {
        return "≤";
    }
    if (desc == &NODE_Greater || desc == &NODE_Integer_Greater) {
        return ">";
    }
    if (desc == &NODE_Greater_Or_Equal || desc == &NODE_Integer_Greater_Or_Equal) {
        return "≥";
    }
    if (desc == &NODE_Equal || desc == &NODE_Integer_Equal) {
        return "=";
    }
    if (desc == &NODE_Not_Equal || desc == &NODE_Integer_Not_Equal) {
        return "≠";
    }
    if (desc == &NODE_Sine) {
        return "SIN";
    }
    if (desc == &NODE_Cosine) {
        return "COS";
    }
    if (desc == &NODE_Tangent) {
        return "TAN";
    }
    return NULL;
}

/* Component nodes and the generated nodes they lower to read the same:
 * the access, then the component or struct. */
static bool ed_component_title(const node *n, char *buf, size_t cap)
{
    const char *verb = NULL, *name = NULL;
    if (!component_node_access(n, &verb, &name)) {
        return false;
    }
    char label[128];
    editor_component_label(name, label, sizeof label);
    snprintf(buf, cap, "%s %s", verb, label);
    return true;
}

const char *ed_node_title(const editor *e, const node *n, char *buf, size_t cap)
{
    if (ed_component_title(n, buf, cap)) {
        return buf;
    }
    if (n->reference_name && !strcmp(n->desc->name, "Shader Parameter")) {
        snprintf(buf, cap, "%s", n->reference_name);
        return buf;
    }
    if (n->reference_name && (!strcmp(n->desc->name, "Send Event") ||
                              !strcmp(n->desc->name, "Call Event"))) {
        snprintf(buf, cap, "%s %s", !strcmp(n->desc->name, "Send Event") ? "Send" : "Call", n->reference_name);
        return buf;
    }
    if (ed_is_graph_call(n)) {
        const behavior_asset *asset = e->asset;
        if (n->reference_asset) {
            asset = NULL;
            for (int i = 0; i < acount(e->asset_catalog); i++) {
                if (!strcmp(e->asset_catalog[i]->name, n->reference_asset)) {
                    asset = e->asset_catalog[i];
                    break;
                }
            }
        }
        const graph *target = behavior_asset_find_graph(asset, n->reference_id);
        const char *name = target ? target->name : n->reference_name;
        if (n->reference_asset && target) {
            snprintf(buf, cap, "%s.%s", asset->name, name);
            return buf;
        }
        if (!name || !name[0]) {
            editor_node_label(n->desc, buf, cap);
            return buf;
        }
        if (behavior_expression_source(e->asset, n)) {
            snprintf(buf, cap, "%s", name);
        } else {
            editor_identifier_label(name, buf, cap);
        }
        return buf;
    }
    const char *expression = behavior_expression_source(e->asset, n);
    if (expression) {
        snprintf(buf, cap, "%s", expression);
        return buf;
    }
    if (n->reference_name && n->reference_name[0] &&
        n->desc->flow_kind == NODE_FLOW_EVENT) {
        snprintf(buf, cap, "On %s", n->reference_name);
        return buf;
    }
    char label[256];
    editor_node_label(n->desc, label, sizeof label);
    if (n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable) {
        const behavior_variable *variable = behavior_asset_find_variable_by_id(e->asset, n->reference_id);
        if (variable && variable->name) {
            char name[192];
            editor_identifier_label(variable->name, name, sizeof name);
            snprintf(buf, cap, "%s %s", n->desc == &NODE_GetVariable ? "Get" : "Set", name);
            return buf;
        }
    }
    snprintf(buf, cap, "%s", label);
    return buf;
}
