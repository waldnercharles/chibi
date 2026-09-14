#include "graph/compiler/behavior_lower_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

static behavior_function *definition(builder *b, graph *g, node *entry)
{
    for (int i = 0; i < acount(b->unit->definitions); i++) {
        const lowered_definition *d = &b->unit->definitions[i];
        if (d->graph == g && d->entry == entry) {
            return d->function;
        }
    }
    behavior_function *f = xcalloc(1, sizeof *f);
    apush(b->unit->definitions, ((lowered_definition){ g, entry, f }));
    return f;
}

static bool bank(builder *b, const pin_spec *spec, bool argument)
{
    pin_value value = { .type = spec->type,
                        .type_name = spec->type_name,
                        .container = spec->container };
    if (spec->kind != PIN_DATA) {
        value = (pin_value){ .type = PIN_BOOL };
    }
    size_t size = 0;
    if (value.type == PIN_STRUCT && value.container == PIN_CONTAINER_SINGLE) {
        aggregate shape = behavior_lower_shape(value.type_name);
        if (shape.type && argument) {
            size = shape.type->size;
        }
        afree(shape.fields);
        if (!shape.type) {
            return false;
        }
    }
    apush(b->function->data_values, ((behavior_function_data_value){ .initial = value, .size = size }));
    return true;
}

bool behavior_lower_invoke(builder *b, node *n, graph *target)
{
    node *entry = graph_node_by_id(target, target->entry_id);
    node *ret = graph_node_by_id(target, target->return_id);
    if (!entry || !ret || !entry->output_count || entry->output_pins[0].kind != PIN_EXEC) {
        behavior_lower_unsupported(b, n, -1, false, "outlined function signature");
        return false;
    }
    int first = acount(b->function->data_values), count = 0;
    for (int i = 1; i < entry->output_count; i++) {
        const pin_spec *spec = &entry->output_pins[i];
        if (spec->kind != PIN_DATA || spec->field_source) {
            continue;
        }
        if (!bank(b, spec, true)) {
            return false;
        }
        count++;
    }
    int result_first = acount(b->function->data_values);
    for (int i = 0; i < ret->input_count; i++) {
        if (!bank(b, &ret->input_pins[i], false)) {
            return false;
        }
    }
    int index = acount(b->function->calls);
    apush(
        b->function->calls,
        ((behavior_function_invocation){ .function = definition(b, target, entry),
                                         .first = first,
                                         .count = count,
                                         .result_first = result_first,
                                         .result_count = ret->input_count })
    );
    if (target->call_depth_limit) {
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_CALL_CHECK,
                                             .left = target->call_depth_limit,
                                             .node_id = n->id }
        );
    }
    int pin = 1, argument = first;
    for (int i = 1; i < entry->output_count; i++) {
        const pin_spec *spec = &entry->output_pins[i];
        if (spec->kind != PIN_DATA || spec->field_source) {
            continue;
        }
        while (pin < n->input_count && n->input_pins[pin].kind != PIN_DATA) {
            pin++;
        }
        int value = pin < n->input_count ? behavior_lower_input(b, n, pin++) : -1;
        if (value >= 0) {
            value = behavior_lower_convert(b, value, spec->type);
        }
        if (value < 0 || !behavior_lower_pack_value(b, value, argument++)) {
            return false;
        }
    }
    b->pending_watch_frame = -1;
    behavior_lower_capture(b, n, false);
    b->pending_watch_frame = 0;
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_INVOKE, .left = index, .node_id = n->id }
    );
    pin = 1;
    for (int i = 0; i < ret->input_count; i++) {
        if (ret->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        while (pin < n->output_count && (n->output_pins[pin].kind != PIN_DATA ||
                                         n->output_pins[pin].field_source)) {
            pin++;
        }
        if (pin == n->output_count) {
            return false;
        }
        int value = behavior_lower_data_result(b, &ret->input_pins[i], result_first + i);
        if (value >= 0) {
            value = behavior_lower_convert(b, value, n->output_pins[pin].type);
        }
        int local = behavior_lower_local(b, b->frame, n, pin++);
        if (value < 0 || local < 0 || !behavior_lower_store(b, BF_SAVE, value, local)) {
            return false;
        }
    }
    behavior_lower_capture(b, n, true);
    const edge *edges;
    if (graph_exec_output_edges(b->graph, n, 0, &edges)) {
        node *to = graph_node_by_id(b->graph, edges[0].to_id);
        int output, input;
        bool found = graph_pin_by_id(to, edges[0].to_pin_id, &output, &input);
        assert(found && !output);
        behavior_lower_trace(b, n, 0, to, input, GRAPH_EDGE_EXEC);
    }
    return true;
}
