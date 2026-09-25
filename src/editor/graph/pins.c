#include "editor/ui/labels.h"
#include "editor/workspace/editor_internal.h"
#include "graph/authoring/layout.h"
#include "graph/model/local.h"
#include "graph/model/pin_conv.h"
#include "graph/nodes/component_nodes.h"

#include <stdio.h>
#include <string.h>

const char *ed_pin_type_name(const pin_spec *spec)
{
    if (spec->type_name && spec->type_name[0]) {
        return spec->type_name;
    }
    if (spec->kind == PIN_EXEC) {
        return "exec";
    }
    const char *name = pin_type_name(spec->type);
    return name ? name : "unknown";
}

bool ed_pin_visible(const graph *g, const node *n, int kind, int pin)
{
    return graph_pin_visible(g, n, kind == ED_PIN_OUTPUT, pin);
}

static bool editable_output(const node *n)
{
    if (n->desc->category && !strncmp(n->desc->category, "Shader", 6)) {
        return !strcmp(n->desc->name, "Shader Constant") ||
               !strcmp(n->desc->name, "Shader Parameter");
    }
    return n->desc->pure_kind == NODE_PURE_CONST &&
           n->desc->display_class == NODE_DISPLAY_LITERAL;
}

int ed_pin_editable(const graph *g, const node *n, int kind, int pin)
{
    if (g->math_expression || !ed_pin_visible(g, n, kind, pin)) {
        return 0;
    }
    const pin_spec *spec = kind == ED_PIN_INPUT ? &n->input_pins[pin]
                                                : &n->output_pins[pin];
    node *source = NULL;
    int source_pin = -1;
    if (kind == ED_PIN_INPUT &&
        (spec->binding || graph_inline_source(g, n, pin, &source, &source_pin))) {
        return 1;
    }
    if (spec->kind != PIN_DATA || spec->field_source) {
        return 0;
    }
    return kind == ED_PIN_INPUT ? !graph_input_source(g, n, pin, &source, &source_pin)
                                : editable_output(n);
}

bool ed_node_has_advanced_inputs(const node *n)
{
    for (int p = 0; p < n->input_count; p++) {
        if (n->input_pins[p].flags & PIN_FLAG_ADVANCED) {
            return true;
        }
    }
    return false;
}

void ed_pin_label(const node *n, const pin_spec *pin, char *buf, size_t cap)
{
    /* A generated field setter reads like Set Component showing that field. */
    if (n->desc->component_kind == NODE_COMPONENT_SET_FIELD && n->input_count == 3 &&
        (pin == &n->input_pins[1] || pin == &n->input_pins[2])) {
        const type_desc *type = n->desc->component->type;
        const char *field = type->fields[n->desc->component_field].name;
        editor_identifier_label(pin == &n->input_pins[1] ? field : "Entity", buf, cap);
        return;
    }
    /* An expression's inputs are the names its source text uses. */
    if (n->desc == &NODE_BehaviorExpression && pin->kind == PIN_DATA && pin->name) {
        snprintf(buf, cap, "%s", pin->name);
        return;
    }
    editor_identifier_label(pin->name ? pin->name : "Pin", buf, cap);
    if (!pin->field_source) {
        return;
    }
    int aggregates = 0;
    const pin_spec *source = NULL;
    for (int i = 0; i < n->output_count; i++) {
        aggregates += graph_output_struct(n, i) != NULL;
        if (n->output_pins[i].id == pin->field_source) {
            source = &n->output_pins[i];
        }
    }
    if (aggregates > 1 && source) {
        char name[128], field[128];
        editor_identifier_label(source->name, name, sizeof name);
        editor_identifier_label(pin->name, field, sizeof field);
        snprintf(buf, cap, "%s %s", name, field);
    }
}

const editor_choices *ed_pin_choices(editor *e, node *n, int output, int index)
{
    if (output) {
        return NULL;
    }
    const pin_spec *spec = &n->input_pins[index];
    if ((spec->type != PIN_I32 && spec->type != PIN_ENUM && spec->type != PIN_NAME) ||
        spec->container != PIN_CONTAINER_SINGLE) {
        return NULL;
    }
    const editor_choices *choices = e->choices_resolver
                                      ? e->choices_resolver(n, output, index, e->choices_user)
                                      : NULL;
    return choices                ? choices
         : spec->type == PIN_ENUM ? editor_choices_for_enum(spec->type_name)
                                  : NULL;
}
