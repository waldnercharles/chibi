/* core/instance.c -- persistent Blueprint instances and the activation
 * scheduler (ADR 0011). An instance owns variable storage, per-node scratch,
 * an optional owner handle, and the ready/suspended activations that belong
 * to it. Activations are flow_run values created instance-owned: they never
 * acquire or release the definition's mutation guard, which the instance
 * holds for its whole lifetime (EXEC-19, RUN-20). */
#include "instance.h"

#include "xalloc.h"

#include <stdlib.h>
#include <string.h>

static void set_error(flow_error *error, int code, const node *n, const char *message)
{
    if (error) *error = (flow_error){ code, n, -1, message, 0 };
}

static char *instance_own_str(blueprint_instance *bi, const char *s)
{
    if (!s) return NULL;
    char *copy = xstrdup(s);
    apush(bi->strings, copy);
    return copy;
}

/* Run the scheduler until no activation can make progress: at most one
 * activation runs at a time, and the next ready activation is the oldest
 * non-terminal one (FIFO by dispatch sequence, EXEC-17). */
static int scheduler_pump(blueprint_instance *bi)
{
    int result = FLOW_OK;
    for (;;) {
        flow_run *act = NULL;
        for (int i = 0; i < acount(bi->activations); i++) {
            if (flow_run_ready(bi->activations[i], bi->current_time)) {
                act = bi->activations[i];
                break;
            }
        }
        if (!act) break;
        result = flow_run_tick_at(act, bi->current_time);
    }
    return result;
}

blueprint_instance *blueprint_instance_create(
    blueprint *bp,
    const pin_value *owner,
    flow_clock_fn now,
    void *now_user
)
{
    if (!bp) return NULL;
    blueprint_instance *bi = (blueprint_instance *)xcalloc(1, sizeof *bi);
    bi->bp = bp;
    bi->now = now;
    bi->now_user = now_user;
    int n = acount(bp->variables);
    if (n) {
        bi->variables = (pin_value *)xcalloc((size_t)n, sizeof *bi->variables);
        for (int i = 0; i < n; i++) {
            /* VAR-11: initialize runtime storage from authored defaults once. */
            bi->variables[i] = bp->variables[i].default_value;
            if ((bi->variables[i].type == PIN_STR || bi->variables[i].type == PIN_NAME) &&
                bi->variables[i].str)
                bi->variables[i].str = instance_own_str(bi, bi->variables[i].str);
        }
    }
    bi->owner = (pin_value){ .type = PIN_FLOW };
    if (bp->owner_type_name) {
        bi->owner = (pin_value){
            .type = PIN_HANDLE,
            .u64 = owner ? owner->u64 : 0,
            .type_name = bp->owner_type_name,
        };
    }
    /* EXEC-19 / RUN-20: the instance holds the definition's mutation guard
     * for its whole lifetime. */
    graph_flow_pause(bp->graph);
    return bi;
}

void blueprint_instance_destroy(blueprint_instance *bi)
{
    if (!bi) return;
    /* RUN-15: cancel every activation before releasing instance storage. */
    for (int i = 0; i < acount(bi->activations); i++)
        flow_run_free(bi->activations[i]);
    afree(bi->activations);
    free(bi->variables); /* xcalloc'd plain array, not a dyna buffer */
    afree(bi->scratch);
    for (int i = 0; i < acount(bi->strings); i++) free(bi->strings[i]);
    afree(bi->strings);
    graph_flow_resume(bi->bp->graph);
    free(bi);
}

flow_run *blueprint_instance_enqueue(blueprint_instance *bi, node *entry, flow_inputs inputs, flow_error *error)
{
    if (!bi || !entry) {
        set_error(error, FLOW_ERR_ARGUMENT, entry, "invalid activation entry");
        return NULL;
    }
    flow_options opts = {
        .now = bi->now,
        .now_user = bi->now_user,
        .initial_time = bi->current_time,
        .sink = bi->sink,
        .sink_user = bi->sink_user,
        .instance = bi,
    };
    flow_run *act = flow_run_start(bi->bp->graph, entry, inputs, &opts, error);
    if (!act) return NULL;
    apush(bi->activations, act);
    return act;
}

flow_run *blueprint_instance_dispatch(
    blueprint_instance *bi,
    const char *name,
    flow_inputs inputs,
    flow_error *error
)
{
    set_error(error, FLOW_OK, NULL, NULL);
    if (!bi || !name || !name[0]) {
        set_error(error, FLOW_ERR_ARGUMENT, NULL, "event name is required");
        return NULL;
    }
    node *found = NULL;
    for (int i = 0; i < acount(bi->bp->graph->nodes); i++) {
        node *n = bi->bp->graph->nodes[i];
        if (n->desc->flow_kind != NODE_FLOW_EVENT || !n->reference_name ||
            strcmp(n->reference_name, name) != 0)
            continue;
        if (found) {
            set_error(error, FLOW_ERR_DUPLICATE_EVENT, n, "duplicate event name");
            return NULL;
        }
        found = n;
    }
    if (!found) {
        set_error(error, FLOW_ERR_UNKNOWN_EVENT, NULL, "unknown event name");
        return NULL;
    }
    flow_run *act = blueprint_instance_enqueue(bi, found, inputs, error);
    if (!act) return NULL;
    scheduler_pump(bi);
    return act;
}

int blueprint_instance_tick(blueprint_instance *bi, double now)
{
    if (!bi) return FLOW_ERR_ARGUMENT;
    bi->current_time = now;
    return scheduler_pump(bi);
}

int blueprint_instance_stop_activation(blueprint_instance *bi, flow_run *act)
{
    (void)bi;
    return flow_run_stop(act);
}

int blueprint_instance_get_variable(blueprint_instance *bi, uint64_t id, pin_value *out)
{
    if (!bi || !out) return -1;
    for (int i = 0; i < acount(bi->bp->variables); i++)
        if (bi->bp->variables[i].id == id) {
            *out = bi->variables[i];
            return 0;
        }
    return -1;
}

int blueprint_instance_set_variable(blueprint_instance *bi, uint64_t id, const pin_value *value)
{
    if (!bi || !value) return -1;
    for (int i = 0; i < acount(bi->bp->variables); i++)
        if (bi->bp->variables[i].id == id) {
            bi->variables[i] = *value;
            if ((value->type == PIN_STR || value->type == PIN_NAME) && value->str)
                bi->variables[i].str = instance_own_str(bi, value->str);
            return 0;
        }
    return -1;
}

pin_value blueprint_instance_owner(const blueprint_instance *bi)
{ return bi ? bi->owner : (pin_value){ .type = PIN_FLOW }; }

pin_value *blueprint_instance_scratch(blueprint_instance *bi, uint64_t node_id)
{
    for (int i = 0; i < acount(bi->scratch); i++)
        if (bi->scratch[i].node_id == node_id) return &bi->scratch[i].value;
    apush(bi->scratch, (instance_scratch_slot){ .node_id = node_id });
    return &bi->scratch[acount(bi->scratch) - 1].value;
}
