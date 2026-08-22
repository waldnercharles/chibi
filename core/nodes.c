#include "nodes.h"

#include "arena.h"

#include <string.h>

void node_eval_make(node *n, runtime_node *state, arena *a)
{
    const type_desc *t = n->desc->type;
    pin_value *inputs = runtime_node_inputs(state);
    pin_value *outputs = runtime_node_outputs(state);
    void *inst = arena_alloc(a, t->size);
    if (!inst) {
        return;
    }
    memset(inst, 0, t->size);
    for (int i = 0; i < t->field_count; i++) {
        pin_write_field(inst, &t->fields[i], inputs[i]);
    }
    outputs[0].type = PIN_STRUCT;
    outputs[0].type_name = t->name;
    outputs[0].ptr = inst;
}

void node_eval_break(node *n, runtime_node *state, arena *a)
{
    (void)a;
    const type_desc *t = n->desc->type;
    pin_value *inputs = runtime_node_inputs(state);
    pin_value *outputs = runtime_node_outputs(state);
    const void *src = inputs[0].ptr;
    if (!src) {
        return;
    }
    for (int i = 0; i < t->field_count; i++) {
        pin_read_field(src, &t->fields[i], &outputs[i]);
    }
}

void node_eval_call(node *n, runtime_node *state, arena *a)
{
    pin_value *inputs = runtime_node_inputs(state);
    pin_value *outputs = runtime_node_outputs(state);
    outputs[0] = n->desc->fn(inputs, n->input_count, a);
}

void node_eval_const(node *n, runtime_node *state, arena *a)
{
    (void)n;
    (void)state;
    (void)a;
}
