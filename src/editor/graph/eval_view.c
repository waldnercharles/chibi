#include "editor/graph/eval_view.h"

#include "graph/model/behavior_asset.h"
#include "graph/model/pin_fmt.h"
#include "graph/runtime/eval.h"
#include "graph/runtime/runtime.h"
#include "support/arena.h"

#include <stdlib.h>

#define EV_ARENA_CAP 65536

struct eval_view
{
    arena a;
    eval_context eval;
    const flow_run *run;
    const behavior_instance *instance;
    const char *error;
    editor_value_formatter formatter;
    void *formatter_user;
    char formatted[8][256];
    unsigned format_slot;
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

static int eval_view_node_safe(const eval_view *v, const graph *g, const node *n, int depth)
{
    if (depth > acount(g->nodes) || n->desc->flow_kind != NODE_FLOW_NONE ||
        n->desc->pure_kind == NODE_PURE_READ) {
        return 0;
    }
    if (!v->instance && (n->desc->pure_kind == NODE_PURE_SELF ||
                         n->desc->pure_kind == NODE_PURE_GET ||
                         n->desc->pure_kind == NODE_PURE_FUNCTION)) {
        return 0;
    }
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].kind != PIN_DATA) {
            continue;
        }
        node *source;
        int source_pin;
        if (graph_input_source(g, n, i, &source, &source_pin) &&
            !eval_view_node_safe(v, g, source, depth + 1)) {
            return 0;
        }
    }
    return 1;
}

void eval_view_refresh(eval_view *v, graph *g)
{
    v->error = NULL;
    v->run = NULL;
    eval_context_reset(&v->eval);
    eval_context_reset_runtime(&v->eval);
    v->eval.instance = (behavior_instance *)v->instance; /* Get/Self preview reads */
    arena_reset(&v->a);
    if (g->owner && (g->owner->domain == BEHAVIOR_DOMAIN_MATERIAL ||
                     g->owner->domain == BEHAVIOR_DOMAIN_SHAPE ||
                     g->owner->domain == BEHAVIOR_DOMAIN_SHADER_LIBRARY)) {
        return;
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        if (!eval_view_node_safe(v, g, n, 0)) {
            continue;
        }
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

void eval_view_set_instance(eval_view *v, const behavior_instance *bi)
{
    if (v) {
        v->instance = bi;
    }
}

void eval_view_set_formatter(eval_view *v, editor_value_formatter formatter, void *user)
{
    v->formatter = formatter;
    v->formatter_user = user;
}

static const char *eval_view_format(const eval_view *v, const pin_value *value)
{
    if (v->formatter) {
        eval_view *view = (eval_view *)v;
        char *buf = view->formatted[view->format_slot++ % 8];
        buf[0] = '\0';
        if (v->formatter(value, v->instance, buf, sizeof view->formatted[0], v->formatter_user)) {
            buf[sizeof view->formatted[0] - 1] = '\0';
            return buf;
        }
    }
    return pin_value_to_string(value);
}

const char *eval_view_error(const eval_view *v)
{
    return v && v->error ? v->error : "";
}

static int has_preview_value(const eval_view *v, const node *n)
{
    if (!v || !v->eval.runtime) {
        return 0;
    }
    const runtime_node *state = runtime_store_node_const(v->eval.runtime, n);
    return state && runtime_node_computed(state);
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
        return eval_view_format(v, &value);
    }
    pin_value value;
    if (!v || eval_context_output(&v->eval, n, pin, &value) != 0 ||
        !has_preview_value(v, n)) {
        return "";
    }
    return eval_view_format(v, &value);
}

const char *eval_view_input(const eval_view *v, const graph *g, const node *n, int pin)
{
    if (!n || pin < 0 || pin >= n->input_count || n->input_pins[pin].kind != PIN_DATA) {
        return NULL;
    }
    const pin_spec *spec = &n->input_pins[pin];
    node *source;
    int source_pin;
    bool connected = graph_input_source(g, n, pin, &source, &source_pin);
    if (!connected && spec->type == PIN_HANDLE &&
        (spec->flags & PIN_FLAG_DEFAULT_SELF) && !spec->default_value.u64) {
        return "Self";
    }
    if (v && v->run) {
        pin_value value;
        if (flow_run_watch_input(v->run, n, pin, &value) != 0) {
            return "";
        }
        return eval_view_format(v, &value);
    }
    if (connected) {
        return NULL;
    }
    pin_value val;
    if (!v || eval_context_input(&v->eval, n, pin, &val) != 0 ||
        !has_preview_value(v, n)) {
        return "";
    }
    val.type = n->input_pins[pin].type;
    return eval_view_format(v, &val);
}
