/* core/instance.c -- persistent Blueprint instances and the activation
 * scheduler (ADR 0011). An instance owns variable storage, per-node scratch,
 * an optional owner handle, and the ready/suspended activations that belong
 * to it. Activations are flow_run values created instance-owned: they never
 * acquire or release the definition's mutation guard, which the instance
 * holds for its whole lifetime (EXEC-19, RUN-20). */
#include "instance.h"

#include "pin_conv.h"
#include "value_array.h"
#include "xalloc.h"

#include <stdlib.h>
#include <string.h>

static void set_error(flow_error *error, int code, const node *n, const char *message)
{
    if (error) {
        *error = (flow_error){ code, n, -1, message, 0 };
    }
}

/* RUN-12: refuse to start when the blueprint has compile errors, reporting
 * the first blocking diagnostic (CMPL-16 order). The report is cached per
 * instance and rebuilt whenever the topology revision moves, so it stays
 * correct across edits. Messages point into the cached report and remain
 * valid for the instance's lifetime. */
static int instance_blocked(blueprint_instance *bi, flow_error *error)
{
    unsigned long long rev = graph_topology_revision(bi->bp->graph);
    if (bi->compile_revision != rev) {
        compile_report_free(&bi->compile);
        compile_report_init(&bi->compile);
        blueprint_compile(bi->bp, &bi->compile);
        bi->compile_revision = rev;
    }
    if (!compile_report_blocked(&bi->compile)) {
        return 0;
    }
    const compile_diagnostic *d = compile_report_first_error(&bi->compile);
    set_error(error, FLOW_ERR_COMPILE, graph_node_by_id(bi->bp->graph, d->node_id), d->message);
    return 1;
}

static char *instance_own_str(blueprint_instance *bi, const char *s)
{
    if (!s) {
        return NULL;
    }
    char *copy = xstrdup(s);
    apush(bi->strings, copy);
    return copy;
}

static void instance_clear_values(blueprint_instance *bi)
{
    for (int i = 0; i < bi->variable_count; i++) {
        if (bi->variables[i].container >= PIN_CONTAINER_ARRAY) {
            pin_array_free((pin_array *)bi->variables[i].ptr);
        }
    }
    for (int i = 0; i < acount(bi->strings); i++) {
        free(bi->strings[i]);
    }
    aclear(bi->strings);
}

static void instance_init_values(blueprint_instance *bi)
{
    int count = acount(bi->bp->variables);
    instance_clear_values(bi);
    if (count != bi->variable_count) {
        free(bi->variables);
        bi->variables = count ? (pin_value *)xcalloc((size_t)count, sizeof *bi->variables)
                              : NULL;
        bi->variable_count = count;
    } else if (count) {
        memset(bi->variables, 0, (size_t)count * sizeof *bi->variables);
    }
    for (int i = 0; i < count; i++) {
        bi->variables[i] = bi->bp->variables[i].default_value;
        if (bi->variables[i].container >= PIN_CONTAINER_ARRAY &&
            bi->variables[i].ptr) {
            bi->variables[i].ptr = pin_array_clone(
                (const pin_array *)bi->variables[i].ptr
            );
        } else if (
            (bi->variables[i].type == PIN_STR || bi->variables[i].type == PIN_NAME) &&
            bi->variables[i].str
        ) {
            bi->variables[i].str = instance_own_str(bi, bi->variables[i].str);
        }
    }
}

static void instance_set_owner(blueprint_instance *bi, const pin_value *owner)
{
    bi->owner = (pin_value){ .type = PIN_FLOW };
    if (bi->bp->owner_type_name) {
        bi->owner = (pin_value){
            .type = PIN_HANDLE,
            .u64 = owner ? owner->u64 : 0,
            .type_name = bi->bp->owner_type_name,
        };
    }
}

static void instance_trace_clear(blueprint_instance *bi)
{
    for (int i = 0; i < acount(bi->trace); i++) {
        free(bi->trace[i].event_name);
    }
    aclear(bi->trace);
}

static void instance_trace_push(blueprint_instance *bi, const flow_run *run, const flow_traversal *traversal)
{
    if (!bi->trace_enabled) {
        return;
    }
    if (acount(bi->trace) == BLUEPRINT_INSTANCE_TRACE_MAX) {
        free(bi->trace[0].event_name);
        memmove(
            bi->trace,
            bi->trace + 1,
            (size_t)(BLUEPRINT_INSTANCE_TRACE_MAX - 1) * sizeof *bi->trace
        );
        asetlen(bi->trace, BLUEPRINT_INSTANCE_TRACE_MAX - 1);
    }
    const node *current = flow_run_current_node(run);
    const flow_error *error = flow_run_error(run);
    blueprint_instance_trace_entry entry = {
        .traversal = traversal ? *traversal : (flow_traversal){
            .timestamp = bi->current_time,
        },
        .event_name = xstrdup(flow_run_event_name(run) ? flow_run_event_name(run) : ""),
        .current_node_id = current ? current->id : 0,
        .state = flow_run_status(run),
        .error_code = error ? error->code : FLOW_OK,
    };
    apush(bi->trace, entry);
}

static void instance_trace_run(blueprint_instance *bi, const flow_run *run)
{
    int count = 0;
    const flow_traversal *traversals = flow_run_traversals(run, &count);
    if (!count) {
        instance_trace_push(bi, run, NULL);
        return;
    }
    for (int i = 0; i < count; i++) {
        instance_trace_push(bi, run, &traversals[i]);
    }
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
        if (!act) {
            break;
        }
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
    if (!bp) {
        return NULL;
    }
    blueprint_instance *bi = (blueprint_instance *)xcalloc(1, sizeof *bi);
    bi->bp = bp;
    bi->next_delegate_id = 1;
    bi->alive = 1;
    bi->trace_enabled = 1;
    bi->now = now;
    bi->now_user = now_user;
    compile_report_init(&bi->compile);
    instance_init_values(bi);
    instance_set_owner(bi, owner);
    /* EXEC-19 / RUN-20: the instance holds the definition's mutation guard
     * for its whole lifetime. */
    graph_flow_pause(bp->graph);
    return bi;
}

static node *instance_event_by_id(blueprint_instance *bi, uint64_t event_id)
{
    if (!bi || !bi->alive) {
        return NULL;
    }
    for (int i = 0; i < acount(bi->bp->graph->nodes); i++) {
        node *n = bi->bp->graph->nodes[i];
        if (n->id == event_id && n->desc->flow_kind == NODE_FLOW_EVENT) {
            return n;
        }
    }
    return NULL;
}

static blueprint_delegate_binding *find_delegate(blueprint_instance *source, uint64_t id, int *index)
{
    if (!source) {
        return NULL;
    }
    for (int i = 0; i < acount(source->delegates); i++) {
        if (source->delegates[i].id == id) {
            if (index) {
                *index = i;
            }
            return &source->delegates[i];
        }
    }
    return NULL;
}

static void remove_incoming_ref(blueprint_instance *target, blueprint_instance *source, uint64_t id)
{
    if (!target) {
        return;
    }
    for (int i = 0; i < acount(target->delegate_incoming); i++) {
        if (target->delegate_incoming[i].source == source &&
            target->delegate_incoming[i].binding_id == id) {
            adel(target->delegate_incoming, i);
            return;
        }
    }
}

static int delegate_signature_matches(const blueprint_delegate_binding *binding, const node *event)
{
    int count = 0;
    for (int i = 0; i < event->output_count; i++) {
        if (event->output_pins[i].kind == PIN_DATA) {
            count++;
        }
    }
    if (count != acount(binding->pin_ids)) {
        return 0;
    }
    int at = 0;
    for (int i = 0; i < event->output_count; i++) {
        const pin_spec *p = &event->output_pins[i];
        if (p->kind != PIN_DATA) {
            continue;
        }
        if (binding->pin_ids[at] != p->id || binding->types[at] != p->type ||
            strcmp(
                binding->type_names[at] ? binding->type_names[at] : "",
                p->type_name ? p->type_name : ""
            ) != 0) {
            return 0;
        }
        at++;
    }
    return 1;
}

int blueprint_instance_remove_activation(blueprint_instance *bi, flow_run *act)
{
    if (!bi || !act) {
        return -1;
    }
    for (int i = 0; i < acount(bi->activations); i++) {
        if (bi->activations[i] == act) {
            adel(bi->activations, i);
            return 0;
        }
    }
    return -1;
}

int blueprint_instance_rebind(blueprint_instance *bi, const pin_value *owner)
{
    if (!bi || !bi->alive || acount(bi->activations)) {
        return -1;
    }
    instance_init_values(bi);
    aclear(bi->scratch);
    instance_set_owner(bi, owner);
    return 0;
}

void blueprint_instance_set_trace_enabled(blueprint_instance *bi, int enabled)
{
    if (bi) {
        bi->trace_enabled = enabled != 0;
        if (!bi->trace_enabled) {
            instance_trace_clear(bi);
        }
    }
}

int blueprint_instance_set_activation_pool(blueprint_instance *bi, int enabled)
{
    if (!bi || !bi->alive || acount(bi->activations)) {
        return -1;
    }
    if (enabled) {
        bi->pool_enabled = 1;
        return 0;
    }
    for (int i = 0; i < acount(bi->activation_pool); i++) {
        flow_run_free(bi->activation_pool[i]);
    }
    afree(bi->activation_pool);
    bi->pool_enabled = 0;
    return 0;
}

int blueprint_instance_release_activation(blueprint_instance *bi, flow_run *act)
{
    if (!bi || !act || blueprint_instance_remove_activation(bi, act) != 0) {
        return -1;
    }
    instance_trace_run(bi, act);
    if (!bi->pool_enabled) {
        flow_run_free(act);
        return 0;
    }
    if (flow_run_recycle(act) != FLOW_OK) {
        flow_run_free(act);
        return -1;
    }
    apush(bi->activation_pool, act);
    return 0;
}

const blueprint_instance_trace_entry *blueprint_instance_trace(const blueprint_instance *bi, int *count)
{
    if (count) {
        *count = bi ? acount(bi->trace) : 0;
    }
    return bi ? bi->trace : NULL;
}

static void instance_detach_bindings(blueprint_instance *bi)
{
    /* EVT-14: detach weak incoming and outgoing bindings before releasing the
     * target instance. No callback can observe this instance afterward. */
    while (acount(bi->delegate_incoming)) {
        blueprint_delegate_ref ref = bi->delegate_incoming[acount(bi->delegate_incoming) - 1];
        adel(bi->delegate_incoming, acount(bi->delegate_incoming) - 1);
        blueprint_instance_unbind_event(ref.source, ref.binding_id);
    }
    for (int i = 0; i < acount(bi->delegates); i++) {
        remove_incoming_ref(bi->delegates[i].target, bi, bi->delegates[i].id);
    }
    afree(bi->delegates);
    afree(bi->delegate_incoming);
}

int blueprint_instance_pool_release(blueprint_instance *bi)
{
    if (!bi) {
        return -1;
    }
    if (!bi->alive) {
        return 0;
    }
    bi->alive = 0;
    instance_detach_bindings(bi);
    while (acount(bi->activations)) {
        flow_run_free(bi->activations[0]);
    }
    aclear(bi->activations);
    aclear(bi->services);
    instance_trace_clear(bi);
    instance_clear_values(bi);
    aclear(bi->scratch);
    compile_report_free(&bi->compile);
    compile_report_init(&bi->compile);
    bi->compile_revision = 0;
    bi->breakpoints_override = 0;
    bi->breakpoints_enabled = 0;
    bi->owner = (pin_value){ .type = PIN_FLOW };
    graph_flow_resume(bi->bp->graph);
    return 0;
}

blueprint_instance *blueprint_instance_pool_reuse(
    blueprint_instance *bi,
    blueprint *bp,
    const pin_value *owner,
    flow_clock_fn now,
    void *now_user
)
{
    if (!bi || !bp || bi->alive || bi->bp != bp) {
        return NULL;
    }
    bi->alive = 1;
    bi->now = now;
    bi->now_user = now_user;
    bi->breakpoints_override = 0;
    bi->breakpoints_enabled = 0;
    bi->next_delegate_id = 1;
    instance_init_values(bi);
    instance_set_owner(bi, owner);
    graph_flow_pause(bp->graph);
    return bi;
}

void blueprint_instance_destroy(blueprint_instance *bi)
{
    if (!bi) {
        return;
    }
    blueprint_instance_pool_release(bi);
    for (int i = 0; i < acount(bi->activation_pool); i++) {
        flow_run_free(bi->activation_pool[i]);
    }
    afree(bi->activation_pool);
    instance_trace_clear(bi);
    afree(bi->trace);
    free(bi->variables);
    afree(bi->activations);
    afree(bi->services);
    afree(bi->scratch);
    for (int i = 0; i < acount(bi->strings); i++) {
        free(bi->strings[i]);
    }
    afree(bi->strings);
    compile_report_free(&bi->compile);
    free(bi);
}

uint64_t blueprint_instance_bind_event(blueprint_instance *source, blueprint_instance *target, uint64_t event_id)
{
    if (!source || !target || !source->alive || !target->alive || source == target) {
        return 0;
    }
    node *event = instance_event_by_id(target, event_id);
    if (!event) {
        return 0;
    }
    for (int i = 0; i < acount(source->delegates); i++) {
        if (source->delegates[i].target == target &&
            source->delegates[i].event_id == event_id) {
            return source->delegates[i].id;
        }
    }
    blueprint_delegate_binding binding = { .id = source->next_delegate_id++,
                                           .target = target,
                                           .event_id = event_id };
    for (int i = 0; i < event->output_count; i++) {
        const pin_spec *p = &event->output_pins[i];
        if (p->kind != PIN_DATA) {
            continue;
        }
        apush(binding.pin_ids, p->id);
        apush(binding.types, p->type);
        apush(binding.type_names, p->type_name);
    }
    apush(source->delegates, binding);
    apush(target->delegate_incoming, (blueprint_delegate_ref){ source, binding.id });
    return binding.id;
}

int blueprint_instance_unbind_event(blueprint_instance *source, uint64_t binding_id)
{
    int index = -1;
    blueprint_delegate_binding *binding = find_delegate(source, binding_id, &index);
    if (!binding) {
        return -1;
    }
    remove_incoming_ref(binding->target, source, binding_id);
    afree(binding->pin_ids);
    afree(binding->types);
    afree(binding->type_names);
    adel(source->delegates, index);
    return 0;
}

flow_run *blueprint_instance_dispatch_delegate(
    blueprint_instance *source,
    uint64_t binding_id,
    flow_inputs inputs,
    flow_error *error
)
{
    set_error(error, FLOW_OK, NULL, NULL);
    blueprint_delegate_binding *binding = find_delegate(source, binding_id, NULL);
    if (!binding || !binding->target || !binding->target->alive) {
        set_error(error, FLOW_ERR_INVALID_GRAPH, NULL, "delegate target is unavailable");
        return NULL;
    }
    node *event = instance_event_by_id(binding->target, binding->event_id);
    if (!event || !delegate_signature_matches(binding, event)) {
        set_error(error, FLOW_ERR_INVALID_GRAPH, event, "delegate event signature changed");
        return NULL;
    }
    int expected = acount(binding->pin_ids);
    if (inputs.count != expected || (expected && !inputs.values)) {
        set_error(error, FLOW_ERR_ARGUMENT, event, "delegate parameter count mismatch");
        return NULL;
    }
    int at = 0;
    for (int i = 0; i < event->output_count; i++) {
        const pin_spec *p = &event->output_pins[i];
        if (p->kind != PIN_DATA) {
            continue;
        }
        pin_spec supplied = { .type = inputs.values[at].type,
                              .type_name = inputs.values[at].type_name,
                              .container = inputs.values[at].container };
        if (!pin_compatible(&supplied, p)) {
            set_error(error, FLOW_ERR_INVALID_GRAPH, event, "delegate parameter type mismatch");
            return NULL;
        }
        at++;
    }
    return blueprint_instance_dispatch(binding->target, event->reference_name, inputs, error);
}

int blueprint_instance_bind_service(blueprint_instance *bi, const char *type_name, void *value)
{
    if (!bi || !bi->alive || !type_name || !type_name[0]) {
        return -1;
    }
    for (int i = 0; i < acount(bi->services); i++) {
        if (strcmp(bi->services[i].type_name, type_name) == 0) {
            bi->services[i].value = value;
            return 0;
        }
    }
    apush(bi->services, (service_binding){ type_name, value });
    return 0;
}

void *blueprint_instance_service(const blueprint_instance *bi, const char *type_name)
{
    if (!bi || !bi->alive || !type_name) {
        return NULL;
    }
    for (int i = 0; i < acount(bi->services); i++) {
        if (strcmp(bi->services[i].type_name, type_name) == 0) {
            return bi->services[i].value;
        }
    }
    return NULL;
}

int blueprint_instance_broadcast_event(
    blueprint_instance *source,
    uint64_t event_id,
    flow_inputs inputs,
    flow_error *error
)
{
    if (!source || !source->alive) {
        return -1;
    }
    uint64_t *snapshot = NULL;
    for (int i = 0; i < acount(source->delegates); i++) {
        if (source->delegates[i].event_id == event_id) {
            apush(snapshot, source->delegates[i].id);
        }
    }
    int dispatched = 0;
    for (int i = 0; i < acount(snapshot); i++) {
        if (!find_delegate(source, snapshot[i], NULL)) {
            continue;
        }
        if (blueprint_instance_dispatch_delegate(source, snapshot[i], inputs, error)) {
            dispatched++;
        }
    }
    afree(snapshot);
    return dispatched;
}

flow_run *blueprint_instance_enqueue(blueprint_instance *bi, node *entry, flow_inputs inputs, flow_error *error)
{
    if (!bi || !entry) {
        set_error(error, FLOW_ERR_ARGUMENT, entry, "invalid activation entry");
        return NULL;
    }
    /* RUN-12: refuse against compile errors before any activation starts. */
    if (instance_blocked(bi, error)) {
        return NULL;
    }
    flow_options opts = {
        .now = bi->now,
        .now_user = bi->now_user,
        .initial_time = bi->current_time,
        .sink = bi->sink,
        .sink_user = bi->sink_user,
        .event_name = entry->reference_name,
        .instance = bi,
        .parallel_for = bi->parallel_for,
        .parallel_user = bi->parallel_user,
        .breakpoints_override = bi->breakpoints_override,
        .breakpoints_enabled = bi->breakpoints_enabled,
    };
    flow_run *act = NULL;
    if (bi->pool_enabled && acount(bi->activation_pool)) {
        act = flow_run_start_reuse(apop(bi->activation_pool), bi->bp->graph, entry, inputs, &opts, error);
    } else {
        act = flow_run_start(bi->bp->graph, entry, inputs, &opts, error);
    }
    if (!act) {
        return NULL;
    }
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
            strcmp(n->reference_name, name) != 0) {
            continue;
        }
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
    /* RUN-12: checked after event lookup so unknown and duplicate dispatch
     * failures keep their distinct codes (EVT-7); a valid event on a broken
     * blueprint reports the first blocking diagnostic. */
    if (instance_blocked(bi, error)) {
        return NULL;
    }
    flow_run *act = blueprint_instance_enqueue(bi, found, inputs, error);
    if (!act) {
        return NULL;
    }
    scheduler_pump(bi);
    return act;
}

int blueprint_instance_tick(blueprint_instance *bi, double now)
{
    if (!bi) {
        return FLOW_ERR_ARGUMENT;
    }
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
    if (!bi || !out) {
        return -1;
    }
    for (int i = 0; i < acount(bi->bp->variables); i++) {
        if (bi->bp->variables[i].id == id) {
            *out = bi->variables[i];
            return 0;
        }
    }
    return -1;
}

int blueprint_instance_set_variable(blueprint_instance *bi, uint64_t id, const pin_value *value)
{
    if (!bi || !value) {
        return -1;
    }
    for (int i = 0; i < acount(bi->bp->variables); i++) {
        if (bi->bp->variables[i].id == id) {
            if (bi->variables[i].container >= PIN_CONTAINER_ARRAY) {
                pin_array_free((pin_array *)bi->variables[i].ptr);
            }
            bi->variables[i] = *value;
            if (value->container >= PIN_CONTAINER_ARRAY && value->ptr) {
                bi->variables[i].ptr = pin_array_clone((const pin_array *)value->ptr);
            } else if (
                (value->type == PIN_STR || value->type == PIN_NAME) && value->str
            ) {
                bi->variables[i].str = instance_own_str(bi, value->str);
            }
            return 0;
        }
    }
    return -1;
}

pin_value blueprint_instance_owner(const blueprint_instance *bi)
{
    return bi ? bi->owner : (pin_value){ .type = PIN_FLOW };
}

void blueprint_instance_set_parallel_for(
    blueprint_instance *bi,
    flow_parallel_for_fn parallel_for,
    void *parallel_user
)
{
    if (!bi) {
        return;
    }
    bi->parallel_for = parallel_for;
    bi->parallel_user = parallel_user;
}

void blueprint_instance_set_breakpoints_enabled(blueprint_instance *bi, int enabled)
{
    if (!bi) {
        return;
    }
    bi->breakpoints_override = 1;
    bi->breakpoints_enabled = enabled != 0;
}

int blueprint_instance_breakpoints_enabled(const blueprint_instance *bi)
{
    if (!bi || !bi->bp || !bi->bp->graph) {
        return 0;
    }
    return bi->breakpoints_override ? bi->breakpoints_enabled
                                    : bi->bp->graph->breakpoints_enabled != 0;
}

pin_value *blueprint_instance_scratch(blueprint_instance *bi, uint64_t node_id)
{
    for (int i = 0; i < acount(bi->scratch); i++) {
        if (bi->scratch[i].node_id == node_id) {
            return &bi->scratch[i].value;
        }
    }
    apush(bi->scratch, (instance_scratch_slot){ .node_id = node_id });
    return &bi->scratch[acount(bi->scratch) - 1].value;
}
