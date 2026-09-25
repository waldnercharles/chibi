#ifndef CHIBI_GRAPH_PIN_EDIT_H
#define CHIBI_GRAPH_PIN_EDIT_H

#include "graph/model/graph.h"

/* Pin edits shared by the editor and automation. */

/* Adds `pin` after n's pins on one side. Past a wildcard group member it joins
 * that group and takes its resolved type, and a switch's Default stays its
 * last output. Returns the new pin's index, or -1. */
int graph_append_pin(graph *g, node *n, int output, const pin_spec *pin);

/* Variadic nodes grow and shrink one run of like pins: Sequence's and
 * MultiGate's outputs, a switch's cases before its Default, Select's options,
 * and the items of Make Array, Make Set and Format Text. Generated names
 * (Then0, Case1, Item2) follow the run's order. */
typedef struct graph_pin_run
{
    int output; /* the run's side */
    int first;  /* its first pin */
    int count;  /* its pins; a switch's Default follows them */
} graph_pin_run;

/* False for a node without a run. */
bool graph_pin_run_of(const node *n, graph_pin_run *run);
/* Appends a pin to n's run; returns its index, or -1. */
int graph_add_run_pin(graph *g, node *n);
/* Whether the pin is in n's run and the run keeps another pin. */
bool graph_run_pin_removable(const node *n, int output, int index);
/* Removes the pin and its wires, then renumbers the run's generated names. */
int graph_remove_run_pin(graph *g, node *n, int output, int index);

/* Removes every wire at a pin; an input's folded source goes with its wire.
 * Returns the number of wires removed. */
int graph_break_pin_links(graph *g, node *n, int output, int index);
/* The value a pin starts with: its descriptor pin's default when the
 * descriptor declares it, otherwise the zero value of its type. */
pin_value graph_pin_reset_value(const node *n, int output, int index);

#endif
