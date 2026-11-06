#include "graph/modeling/model_authoring.h"

#include "graph/authoring/graph_edit.h"
#include "graph/model/value_copy.h"
#include "support/array.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void model_author_error(shader_error *error, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(error->message, sizeof error->message, format, arguments);
    va_end(arguments);
}

int model_instance_override(
    graph *g,
    node *instance,
    const behavior_asset *target,
    const char *name,
    shader_error *error
)
{
    error->node = instance->id;
    for (int i = 0; i < instance->input_count; i++) {
        if (!strcmp(instance->input_pins[i].name, name)) {
            return i;
        }
    }
    if (!target || target->domain != BEHAVIOR_DOMAIN_MODEL) {
        model_author_error(
            error,
            "Model Instance references missing model '%s'",
            instance->reference_asset ? instance->reference_asset : ""
        );
        return -1;
    }
    for (int graph_index = 0; graph_index < acount(target->graphs); graph_index++) {
        const graph *source = target->graphs[graph_index];
        for (int i = 0; i < acount(source->nodes); i++) {
            const node *parameter = source->nodes[i];
            bool material = !strcmp(parameter->desc->name, "Model Material Slot");
            if ((!material && strcmp(parameter->desc->name, "Model Parameter")) ||
                !parameter->reference_name ||
                strcmp(parameter->reference_name, name)) {
                continue;
            }
            model_parameter_decl declaration = { .id = parameter->id,
                                                 .name = parameter->reference_name,
                                                 .type = PIN_STR,
                                                 .type_name = "MaterialAsset" };
            model_material_decl slot;
            if ((material && !model_material_get(parameter, &slot, error)) ||
                (!material && !model_parameter_get(parameter, &declaration, error))) {
                return -1;
            }
            for (int p = 0; p < instance->input_count; p++) {
                if (instance->input_pins[p].declaration_id == declaration.id) {
                    if (graph_set_pin_name(g, instance, false, p, declaration.name)) {
                        model_author_error(error, "could not refresh model parameter '%s'", name);
                        return -1;
                    }
                    return p;
                }
            }
            const char *type_name = declaration.type_name
                                      ? graph_own_str(
                                            g,
                                            declaration.type_name,
                                            strlen(declaration.type_name)
                                        )
                                      : NULL;
            pin_spec pin = { .name = declaration.name,
                             .type = declaration.type,
                             .type_name = type_name,
                             .kind = PIN_DATA,
                             .declaration_id = declaration.id,
                             .default_value = { .type = declaration.type,
                                                .type_name = type_name } };
            int index = graph_add_pin(g, instance, false, &pin);
            pin_value value = pin.default_value;
            if (material) {
                const char *asset = parameter->reference_asset ? parameter->reference_asset
                                                               : "";
                value = graph_text_value(g, asset, strlen(asset));
                value.type_name = type_name;
            } else if (declaration.type == PIN_STRUCT) {
                value.ptr = declaration.defaults;
            } else {
                value.f = declaration.defaults[0];
            }
            if (index < 0 || graph_set_pin_default(g, instance, false, index, &value)) {
                model_author_error(error, "could not add model parameter override '%s'", name);
                return -1;
            }
            return index;
        }
    }
    model_author_error(
        error,
        "Model '%s' has no parameter or material slot '%s'",
        target->name,
        name
    );
    return -1;
}

int model_instance_anchor(
    graph *g,
    node *instance,
    const behavior_asset *target,
    const char *name,
    shader_error *error
)
{
    error->node = instance->id;
    if (!target || target->domain != BEHAVIOR_DOMAIN_MODEL) {
        model_author_error(
            error,
            "Model Instance references missing model '%s'",
            instance->reference_asset ? instance->reference_asset : ""
        );
        return -1;
    }
    for (int i = 0; i < acount(target->graph->nodes); i++) {
        const node *anchor = target->graph->nodes[i];
        if (strcmp(anchor->desc->name, "Model Anchor") ||
            !anchor->reference_name || strcmp(anchor->reference_name, name)) {
            continue;
        }
        model_anchor_decl declaration;
        if (!model_anchor_get(anchor, &declaration, error)) {
            return -1;
        }
        for (int p = 1; p < instance->output_count; p++) {
            if (instance->output_pins[p].declaration_id == anchor->id) {
                graph_set_pin_name(g, instance, true, p, name);
                return p;
            }
        }
        pin_spec pin = { .name = name,
                         .type = PIN_HANDLE,
                         .type_name = "Anchor3D",
                         .kind = PIN_DATA,
                         .declaration_id = anchor->id,
                         .default_value = { .type = PIN_HANDLE,
                                            .type_name = "Anchor3D" } };
        int output = graph_add_pin(g, instance, true, &pin);
        if (output < 0) {
            model_author_error(error, "could not expose anchor '%s': output name conflicts", name);
        }
        return output;
    }
    model_author_error(error, "Model '%s' has no anchor '%s'", target->name, name);
    return -1;
}
