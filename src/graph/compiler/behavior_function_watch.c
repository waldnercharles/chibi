#include "graph/compiler/behavior_lower_internal.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <stdlib.h>

static void field(builder *b, int value, pin_type type, size_t offset, bool structure)
{
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_WATCH_VALUE,
                                         .type = type,
                                         .left = value,
                                         .offset = offset,
                                         .right = structure }
    );
}
static void watch_value(builder *b, const node *n, int pin, bool output, int value, int present)
{
    if (!b->recording || value < 0) {
        return;
    }
    const pin_spec *spec = output ? &n->output_pins[pin] : &n->input_pins[pin];
    array_value *array = behavior_lower_array(b, value);
    aggregate *a = behavior_lower_aggregate(b, value);
    int index = acount(b->function->watches);
    pin_value initial = spec->default_value;
    initial.type = array || a ? spec->type : b->function->slots[value];
    initial.container = spec->container;
    apush(
        b->function->watches,
        ((behavior_function_watch){ .graph = b->graph,
                                    .node = n,
                                    .frame = b->frame,
                                    .pin = pin,
                                    .pending_frame = b->pending_watch_frame,
                                    .output = output,
                                    .initial = initial })
    );
    behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_WATCH_BEGIN, .target = index });
    if (array) {
        int pointer = array->pointer;
        field(b, pointer, BF_POINTER_TYPE, offsetof(pin_value, ptr), false);
        int name = array->name;
        field(b, name, BF_POINTER_TYPE, offsetof(pin_value, type_name), false);
    } else if (a) {
        aggregate copy = *a;
        if (copy.type->size > b->function->watch_size) {
            b->function->watch_size = copy.type->size;
        }
        int name = copy.name >= 0 ? copy.name
                                  : behavior_lower_static_name(b, copy.type->name);
        field(b, name, BF_POINTER_TYPE, offsetof(pin_value, type_name), false);
        for (int i = 0; i < acount(copy.fields); i++) {
            aggregate_field f = copy.fields[i];
            field(b, f.slot, f.type, f.offset, true);
        }
        behavior_lower_emit(
            b,
            (behavior_function_instruction){ .op = BF_WATCH_PRESENT, .left = copy.present }
        );
    } else {
        scalar_value *s = behavior_lower_scalar_value(b, value);
        int name = s ? s->name : behavior_lower_static_name(b, initial.type_name);
        field(b, value, b->function->slots[value], offsetof(pin_value, d), false);
        field(b, name, BF_POINTER_TYPE, offsetof(pin_value, type_name), false);
    }
    if (present < 0) {
        present = behavior_lower_value(
            b,
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = PIN_BOOL,
                                             .value = { .type = PIN_BOOL, .b = true } }
        );
    }
    behavior_lower_emit(
        b,
        (behavior_function_instruction){ .op = BF_WATCH_END, .target = index, .left = present }
    );
}
void behavior_lower_watch(builder *b, const node *n, int pin, bool output, int value)
{
    watch_value(b, n, pin, output, value, -1);
}
void behavior_lower_watch_reset(builder *b, int frame)
{
    if (!b->recording) {
        return;
    }
    int index = acount(b->function->watches);
    apush(
        b->function->watches,
        ((behavior_function_watch){ .graph = b->function->frames[frame].source.graph,
                                    .frame = frame,
                                    .pin = -1 })
    );
    behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_WATCH_RESET, .target = index });
}
void behavior_lower_watch_commit(builder *b, int frame)
{
    if (!b->recording) {
        return;
    }
    int index = acount(b->function->watches);
    apush(
        b->function->watches,
        ((
            behavior_function_watch
        ){ .graph = b->graph, .frame = frame, .pin = -1, .pending_frame = frame })
    );
    behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_WATCH_RESET, .target = index });
}
void behavior_lower_capture(builder *b, node *n, bool outputs_only)
{
    if (!b->recording) {
        return;
    }
    if (!outputs_only) {
        for (int i = 0; i < n->input_count; i++) {
            if (n->input_pins[i].kind != PIN_DATA) {
                continue;
            }
            for (int j = acount(b->inputs) - 1; j >= 0; j--) {
                output input = b->inputs[j];
                if (input.node == n && input.pin == i) {
                    behavior_lower_watch(b, n, i, false, input.slot);
                    break;
                }
            }
        }
    }
    int retained = 0;
    for (int i = 0; i < acount(b->outputs); i++) {
        if (b->outputs[i].node != n) {
            b->outputs[retained++] = b->outputs[i];
        }
    }
    asetlen(b->outputs, retained);
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].kind != PIN_DATA) {
            continue;
        }
        b->capture_outputs = true;
        int value = behavior_lower_output(b, n, i);
        int present = -1;
        if (n->output_pins[i].field_source) {
            int source = -1;
            const type_field *field = graph_output_field(n, i, &source);
            int parent = field ? behavior_lower_output(b, n, source) : -1;
            aggregate *a = behavior_lower_aggregate(b, parent);
            if (a) {
                present = a->present;
            }
        }
        b->capture_outputs = false;
        watch_value(b, n, i, true, value, present);
    }
}
void behavior_lower_trace(builder *b, node *from, int output, node *to, int input, graph_edge_kind kind)
{
    if (!b->recording || b->suppress_trace) {
        return;
    }
    int index = acount(b->function->edges);
    apush(
        b->function->edges,
        ((behavior_function_edge){ from->id,
                                   from->output_pins[output].id,
                                   to->id,
                                   to->input_pins[input].id,
                                   kind,
                                   b->frame })
    );
    behavior_lower_emit(b, (behavior_function_instruction){ .op = BF_TRACE, .target = index });
}
static void dependencies(builder *b, node *n, int depth)
{
    if (!b->recording || b->suppress_trace || depth > 128) {
        return;
    }
    for (int i = 0; i < acount(b->traced); i++) {
        if (b->traced[i] == n) {
            return;
        }
    }
    apush(b->traced, n);
    for (int pin = 0; pin < n->input_count; pin++) {
        node *source;
        int output;
        if (!graph_input_source(b->graph, n, pin, &source, &output)) {
            continue;
        }
        behavior_lower_trace(b, source, output, n, pin, GRAPH_EDGE_DATA);
        dependencies(b, source, depth + 1);
    }
    if (n->desc->pure_kind != NODE_PURE_FUNCTION) {
        return;
    }
    graph *target = behavior_asset_find_graph(b->asset, n->reference_id);
    if (!target) {
        return;
    }
    node *ret = graph_node_by_id(target, target->return_id);
    if (!ret) {
        return;
    }
    graph *previous = b->graph;
    const node **traced = b->traced;
    b->traced = NULL;
    b->graph = target;
    dependencies(b, ret, depth + 1);
    afree(b->traced);
    b->traced = traced;
    b->graph = previous;
}
void behavior_lower_trace_dependencies(builder *b, node *n)
{
    dependencies(b, n, 0);
}
size_t behavior_function_watch_size(const behavior_function *f)
{
    return f->watch_size;
}
void behavior_function_remove_observers(behavior_function *f)
{
    int count = acount(f->instructions), used = 0;
    int *map = xcalloc((size_t)count, sizeof *map);
    for (int i = 0; i < count; i++) {
        map[i] = used;
        if (!behavior_function_observer(f->instructions[i].op)) {
            f->instructions[used++] = f->instructions[i];
        }
    }
    asetlen(f->instructions, used);
    for (int i = 0; i < used; i++) {
        behavior_function_instruction *op = &f->instructions[i];
        if (op->op == BF_BRANCH || op->op == BF_JUMP) {
            op->target = map[op->target];
        }
    }
    free(map);
    afree(f->watches);
    afree(f->edges);
    f->watch_size = 0;
}
