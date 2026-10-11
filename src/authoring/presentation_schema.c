#include "authoring/presentation_schema.h"

#include <assert.h>
#include <string.h>

static const char *own(graph *g, const char *text)
{
    return text ? graph_own_str(g, text, strlen(text)) : NULL;
}
pin_spec authoring_presentation_pin(graph *g, const script_catalog *catalog, const char *name, script_type type)
{
    const char *nominal = NULL;
    for (int i = 0; i < catalog->type_count; i++) {
        if (catalog->types[i].id == type.nominal) {
            nominal = catalog->types[i].name;
            break;
        }
    }
    return (pin_spec){
        .name = own(g, name),
        .type = type.storage,
        .type_name = own(g, nominal),
        .container = type.container,
        .flags = (type.flags & SCRIPT_TYPE_REFERENCE ? PIN_FLAG_REFERENCE : 0) |
                 (type.flags & SCRIPT_TYPE_UNTYPED ? PIN_FLAG_WILDCARD : 0)
    };
}
static pin_spec *pins(graph *g, const pin_spec *source, int count)
{
    if (!count) {
        return NULL;
    }
    pin_spec *result = graph_own_block(g, (size_t)count * sizeof *result);
    memcpy(result, source, (size_t)count * sizeof *result);
    for (int i = 0; i < count; i++) {
        result[i].name = own(g, source[i].name);
        result[i].type_name = own(g, source[i].type_name);
    }
    return result;
}
node_desc *authoring_presentation_descriptor(
    graph *g,
    const char *name,
    node_display_class display,
    const char *category,
    const pin_spec *inputs,
    int input_count,
    const pin_spec *outputs,
    int output_count
)
{
    assert(g && name && input_count >= 0 && output_count >= 0);
    assert((!input_count || inputs) && (!output_count || outputs));
    node_desc *desc = graph_own_block(g, sizeof *desc);
    *desc = (node_desc){
        .name = own(g, name),
        .metadata_only = true,
        .display_class = display,
        .flow_kind = display == NODE_DISPLAY_EVENT ? NODE_FLOW_EVENT : NODE_FLOW_NONE,
        .category = own(g, category),
        .pure_kind = display == NODE_DISPLAY_LITERAL  ? NODE_PURE_CONST
                   : display == NODE_DISPLAY_FUNCTION ? NODE_PURE_FUNCTION
                                                      : NODE_PURE_READ,
        .input_count = input_count,
        .output_count = output_count,
        .inputs = pins(g, inputs, input_count),
        .outputs = pins(g, outputs, output_count)
    };
    return desc;
}
