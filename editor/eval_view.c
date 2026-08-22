#include "eval_view.h"

#include "../core/arena.h"
#include "../core/eval.h"
#include "../core/pin_fmt.h"

#include <stdlib.h>

#define EV_ARENA_CAP 65536

struct eval_view
{
    arena a;
    eval_context eval;
    const flow_run *run;
    const blueprint_instance *instance;
    const char *error;
};

eval_view *eval_view_new(void)
{
    eval_view *v = (eval_view *)calloc(1, sizeof *v);
    if (!v) {
        return NULL;
    }
    arena_init(&v->a, EV_ARENA_CAP);
    eval_context_init(&v->eval);
    return v;
}

void eval_view_free(eval_view *v)
{
    if (!v) {
        return;
    }
    eval_context_free(&v->eval);
    arena_free(&v->a);
    free(v);
}

void eval_view_refresh(eval_view *v, graph *g)
{
    v->error = NULL;
    v->run = NULL;
    eval_context_reset(&v->eval);
    eval_context_reset_runtime(&v->eval);
    v->eval.instance = (blueprint_instance *)v->instance; /* Get/Self preview reads */
    arena_reset(&v->a);
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        for (int p = 0; p < n->output_count; p++) {
            if (n->output_pins[p].kind != PIN_DATA) {
                continue;
            }
            pin_value out;
            if (graph_eval_output(g, n, p, &v->a, &out, &v->eval) != 0 && !v->error) {
                const eval_status *status = eval_context_status(&v->eval);
                v->error = status && status->message ? status->message
                                                     : "evaluation failed";
            }
        }
    }
}

void eval_view_set_run(eval_view *v, const flow_run *run)
{
    if (v) {
        v->run = run;
    }
}

void eval_view_set_instance(eval_view *v, const blueprint_instance *bi)
{
    if (v) {
        v->instance = bi;
    }
}

const char *eval_view_error(const eval_view *v)
{
    return v && v->error ? v->error : "";
}

const char *eval_view_output(const eval_view *v, const node *n, int pin)
{
    if (!n || pin < 0 || pin >= n->output_count || n->output_pins[pin].kind != PIN_DATA) {
        return "";
    }
    if (v && v->run) {
        pin_value value;
        if (flow_run_watch_output(v->run, n, pin, &value) != 0) {
            return "";
        }
        return pin_value_to_string(&value);
    }
    pin_value value;
    if (!v || eval_context_output(&v->eval, n, pin, &value) != 0) {
        return "";
    }
    return pin_value_to_string(&value);
}

const char *eval_view_input(const eval_view *v, const graph *g, const node *n, int pin)
{
    if (!n || pin < 0 || pin >= n->input_count || n->input_pins[pin].kind != PIN_DATA) {
        return NULL;
    }
    if (v && v->run) {
        pin_value value;
        if (flow_run_watch_input(v->run, n, pin, &value) != 0) {
            return "";
        }
        return pin_value_to_string(&value);
    }
    node *src;
    int src_pin;
    if (graph_input_source(g, n, pin, &src, &src_pin)) {
        return NULL;
    }
    pin_value val;
    if (!v || eval_context_input(&v->eval, n, pin, &val) != 0) {
        return "";
    }
    val.type = n->input_pins[pin].type;
    return pin_value_to_string(&val);
}
