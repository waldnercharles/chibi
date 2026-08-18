#include "nodes.h"

#include "arena.h"

#include <string.h>

void node_eval_make(node *n, arena *a)
{
    const type_desc *t = n->desc->type;
    void *inst = arena_alloc(a, t->size);
    if (!inst) return;
    memset(inst, 0, t->size);
    for (int i = 0; i < t->field_count; i++)
        pin_write_field(inst, &t->fields[i], n->inputs[i]);
    n->outputs[0].type = PIN_STRUCT;
    n->outputs[0].type_name = t->name;
    n->outputs[0].ptr = inst;
}

void node_eval_break(node *n, arena *a)
{
    (void)a;
    const type_desc *t = n->desc->type;
    const void *src = n->inputs[0].ptr;
    for (int i = 0; i < t->field_count; i++)
        pin_read_field(src, &t->fields[i], &n->outputs[i]);
}

void node_eval_call(node *n, arena *a)
{ n->outputs[0] = n->desc->fn(n->inputs, n->desc->input_count, a); }

void node_eval_const(node *n, arena *a)
{
    (void)n;
    (void)a;
}
