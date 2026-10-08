#include "graph/authoring/pin_edit.h"

#include "graph/authoring/graph_edit.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "graph/nodes/state_nodes.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int graph_append_pin(graph *g, node *n, int output, const pin_spec *pin)
{
    int count = output ? n->output_count : n->input_count;
    const pin_spec *last = count
                             ? &(output ? n->output_pins : n->input_pins)[count - 1]
                             : NULL;
    pin_spec added = *pin;
    if (last && last->kind == pin->kind && (last->flags & PIN_FLAG_WILDCARD) &&
        last->group) {
        added.type = last->type;
        added.type_name = last->type_name;
        added.container = last->container;
        added.flags = last->flags & ~PIN_FLAG_WATCH;
        added.group = last->group;
        added.default_value = (pin_value){ .type = last->type,
                                           .type_name = last->type_name };
    }
    bool before_default = last && output && pin->kind == PIN_EXEC &&
                          last->name && !strcmp(last->name, "Default");
    int index = graph_add_pin(g, n, output, &added);
    return index >= 0 && before_default ? graph_move_pin(g, n, 1, index, index - 1)
                                        : index;
}

/* ---------- Variadic runs ---------- */

typedef struct
{
    int output, first;
    int tail; /* pins after the run that stay, such as Default */
} run_rule;

static bool rule_of(const node_desc *d, run_rule *rule)
{
    /* A variadic call repeats its last declared input. */
    if (d->host_call && d->host_call->variadic) {
        *rule = (run_rule){ 0, d->input_count - 1, 0 };
        return true;
    }
    if (d == &NODE_Select) {
        *rule = (run_rule){ 0, 1, 0 };
        return true;
    }
    if (d == &NODE_SwitchInt || d == &NODE_SwitchString ||
        d == &NODE_SwitchName || d == &NODE_SwitchEnum) {
        *rule = (run_rule){ 1, 0, 1 };
        return true;
    }
    *rule = (run_rule){ 1, 0, 0 };
    return d == &NODE_Sequence || d == &NODE_MultiGate;
}

/* The descriptor pin new run pins copy: the run's last declared pin. */
static const pin_spec *run_template(const node_desc *d, const run_rule *rule)
{
    const pin_spec *pins = rule->output ? d->outputs : d->inputs;
    int count = rule->output ? d->output_count : d->input_count;
    return &pins[count - 1 - rule->tail];
}

/* The template's name without its number: Then0 gives Then, Item gives Item. */
static size_t name_stem(const char *name)
{
    size_t length = strlen(name);
    while (length && isdigit((unsigned char)name[length - 1])) {
        length--;
    }
    return length;
}

/* The generated name at a place in the run. A template without a number,
   such as Make Array's Item, keeps its bare name first. */
static void run_name(const char *template_name, int place, char *out, size_t size)
{
    size_t stem = name_stem(template_name);
    if (!template_name[stem] && !place) {
        snprintf(out, size, "%s", template_name);
        return;
    }
    snprintf(out, size, "%.*s%d", (int)stem, template_name, place);
}

/* Authored case names such as Case:Fire keep their name. */
static bool generated_name(const char *name, const char *template_name)
{
    size_t stem = name_stem(template_name);
    if (!name || strncmp(name, template_name, stem)) {
        return false;
    }
    for (const char *c = name + stem; *c; c++) {
        if (!isdigit((unsigned char)*c)) {
            return false;
        }
    }
    return true;
}

bool graph_pin_run_of(const node *n, graph_pin_run *run)
{
    run_rule rule;
    if (!rule_of(n->desc, &rule)) {
        return false;
    }
    int count = rule.output ? n->output_count : n->input_count;
    *run = (graph_pin_run){ rule.output, rule.first, count - rule.first - rule.tail };
    return run->count > 0;
}

int graph_add_run_pin(graph *g, node *n)
{
    run_rule rule;
    graph_pin_run run;
    if (!graph_pin_run_of(n, &run)) {
        return -1;
    }
    rule_of(n->desc, &rule);
    const pin_spec *template_pin = run_template(n->desc, &rule);
    char name[64];
    run_name(template_pin->name, run.count, name, sizeof name);
    pin_spec pin = *template_pin;
    pin.name = name;
    return graph_append_pin(g, n, run.output, &pin);
}

bool graph_run_pin_removable(const node *n, int output, int index)
{
    graph_pin_run run;
    return graph_pin_run_of(n, &run) && run.output == output && run.count > 1 &&
           index >= run.first && index < run.first + run.count;
}

static void renumber(graph *g, node *n)
{
    run_rule rule;
    graph_pin_run run;
    graph_pin_run_of(n, &run);
    rule_of(n->desc, &rule);
    const char *template_name = run_template(n->desc, &rule)->name;
    for (int i = 0; i < run.count; i++) {
        int index = run.first + i;
        const pin_spec *pin = run.output ? &n->output_pins[index]
                                         : &n->input_pins[index];
        char name[64];
        run_name(template_name, i, name, sizeof name);
        if (generated_name(pin->name, template_name) && strcmp(pin->name, name)) {
            graph_set_pin_name(g, n, run.output, index, name);
        }
    }
}

int graph_remove_run_pin(graph *g, node *n, int output, int index)
{
    if (!graph_run_pin_removable(n, output, index)) {
        return -1;
    }
    graph_break_pin_links(g, n, output, index);
    if (graph_remove_pin(g, n, output, index)) {
        return -1;
    }
    renumber(g, n);
    return 0;
}

/* ---------- Links and values ---------- */

int graph_break_pin_links(graph *g, node *n, int output, int index)
{
    node *source = NULL;
    int source_pin = -1;
    if (!output && graph_inline_source(g, n, index, &source, &source_pin)) {
        return graph_remove_node_tree(g, source) ? 0 : 1;
    }
    uint64_t pin_id = (output ? n->output_pins : n->input_pins)[index].id;
    int removed = 0;
    for (int i = acount(g->edges) - 1; i >= 0; i--) {
        const edge *e = &g->edges[i];
        bool touches = output ? e->from_id == n->id && e->from_pin_id == pin_id
                              : e->to_id == n->id && e->to_pin_id == pin_id;
        node *from = graph_node_by_id(g, e->from_id),
             *to = graph_node_by_id(g, e->to_id);
        int from_pin = -1, to_pin = -1;
        if (!touches || !graph_pin_by_id(from, e->from_pin_id, NULL, &from_pin) ||
            !graph_pin_by_id(to, e->to_pin_id, NULL, &to_pin)) {
            continue;
        }
        removed += graph_remove_edge(g, from, from_pin, to, to_pin) == 0;
    }
    return removed;
}

pin_value graph_pin_reset_value(const node *n, int output, int index)
{
    const pin_spec *pin = output ? &n->output_pins[index] : &n->input_pins[index];
    const pin_spec *declared = output ? n->desc->outputs : n->desc->inputs;
    int count = output ? n->desc->output_count : n->desc->input_count;
    if (index < count && declared[index].type == pin->type && pin->name &&
        declared[index].name && !strcmp(declared[index].name, pin->name)) {
        return graph_pin_initial_default(&declared[index]);
    }
    pin_spec zero = { .type = pin->type,
                      .type_name = pin->type_name,
                      .container = pin->container };
    return graph_pin_initial_default(&zero);
}
