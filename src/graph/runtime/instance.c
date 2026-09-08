#include "support/arena.h"
/* graph/runtime/instance.c -- persistent Behavior Asset instances and the activation
 * scheduler. An instance owns variable storage, per-node scratch,
 * an optional owner handle, and the ready/suspended activations that belong
 * to it. Activations are flow_run values created instance-owned: they never
 * acquire or release the definition's mutation guard, which the instance
 * holds for its whole lifetime (EXEC-19, RUN-20). */
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/value_array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void set_error(flow_error *error, int code, const node *n, const char *message)
{
    if (error) {
        *error = (flow_error){ code, n, -1, message, 0 };
    }
}

/* RUN-12: refuse to start when the behavior_asset has compile errors, reporting
 * the first blocking diagnostic (CMPL-16 order). The report is cached per
 * instance and rebuilt whenever the topology revision moves, so it stays
 * correct across edits. Messages point into the cached report and remain
 * valid for the instance's lifetime. */
static int instance_blocked(behavior_instance *bi, flow_error *error)
{
    unsigned long long rev = graph_topology_revision(bi->bp->graph);
    if (bi->compile_revision != rev) {
        compile_report_free(&bi->compile);
        compile_report_init(&bi->compile);
        behavior_asset_compile(bi->bp, &bi->compile);
        bi->compile_revision = rev;
    }
    if (!compile_report_blocked(&bi->compile)) {
        return 0;
    }
    const compile_diagnostic *d = compile_report_first_error(&bi->compile);
    set_error(error, FLOW_ERR_COMPILE, graph_node_by_id(bi->bp->graph, d->node_id), d->message);
    return 1;
}

static char *instance_own_str(behavior_instance *bi, const char *s)
{
    if (!s) {
        return NULL;
    }
    for (int i = 0; i < acount(bi->strings); i++) {
        if (strcmp(bi->strings[i], s) == 0) {
            return bi->strings[i];
        }
    }
    char *copy = xstrdup(s);
    apush(bi->strings, copy);
    return copy;
}

static void instance_own_type_names(behavior_instance *bi, pin_value *value)
{
    value->type_name = instance_own_str(bi, value->type_name);
    if (value->container < PIN_CONTAINER_ARRAY || !value->ptr) {
        return;
    }
    pin_array *array = value->ptr;
    array->type_name = instance_own_str(bi, array->type_name);
    for (int i = 0; i < array->count; i++) {
        instance_own_type_names(bi, &array->items[i]);
    }
}

static int instance_copy_value(arena *storage, pin_value *out, const pin_value *value)
{
    if (pin_value_copy_to_arena(storage, out, value)) {
        return -1;
    }
    if (out->container >= PIN_CONTAINER_ARRAY) {
        pin_array *array = out->ptr;
        for (int i = 0; array && i < array->count; i++) {
            pin_value copy;
            if (instance_copy_value(storage, &copy, &array->items[i])) {
                return -1;
            }
            array->items[i] = copy;
        }
        return 0;
    }
    if (out->type != PIN_STRUCT || !out->ptr) {
        return 0;
    }
    const type_desc *type = registry_type_by_name(out->type_name);
    if (!type || !type->size) {
        return -1;
    }
    void *copy = arena_alloc(storage, type->size);
    memcpy(copy, out->ptr, type->size);
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->type != PIN_STRUCT && field->type != PIN_STR &&
            field->type != PIN_NAME) {
            continue;
        }
        pin_value member;
        pin_value owned;
        pin_read_field(out->ptr, field, &member);
        if (instance_copy_value(storage, &owned, &member)) {
            return -1;
        }
        pin_write_field(copy, field, owned);
    }
    out->ptr = copy;
    return 0;
}

static int instance_store_value(behavior_instance *bi, int index, const pin_value *value)
{
    arena storage = { 0 };
    pin_value owned;
    if (instance_copy_value(&storage, &owned, value)) {
        arena_free(&storage);
        return -1;
    }
    instance_own_type_names(bi, &owned);
    arena_free(&bi->variable_arenas[index]);
    bi->variable_arenas[index] = storage;
    bi->variables[index] = owned;
    return 0;
}

static void instance_clear_values(behavior_instance *bi)
{
    for (int i = 0; i < bi->variable_count; i++) {
        arena_free(&bi->variable_arenas[i]);
        bi->variables[i] = (pin_value){ 0 };
    }
    for (int i = 0; i < acount(bi->strings); i++) {
        free(bi->strings[i]);
    }
    aclear(bi->strings);
}

static void instance_init_values(behavior_instance *bi)
{
    int count = acount(bi->bp->variables);
    instance_clear_values(bi);
    if (count != bi->variable_count) {
        free(bi->variables);
        free(bi->variable_arenas);
        bi->variable_arenas = count ? xcalloc((size_t)count, sizeof *bi->variable_arenas)
                                    : NULL;
        bi->variables = count ? (pin_value *)xcalloc((size_t)count, sizeof *bi->variables)
                              : NULL;
        bi->variable_count = count;
    } else if (count) {
        memset(bi->variables, 0, (size_t)count * sizeof *bi->variables);
    }
    for (int i = 0; i < count; i++) {
        int result = instance_store_value(bi, i, &bi->bp->variables[i].default_value);
        assert(result == 0);
    }
}

static void instance_set_owner(behavior_instance *bi, const pin_value *owner)
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

static void instance_trace_clear(behavior_instance *bi)
{
    for (int i = 0; i < acount(bi->trace); i++) {
        free(bi->trace[i].event_name);
    }
    aclear(bi->trace);
}

static void instance_trace_push(behavior_instance *bi, const flow_run *run, const flow_traversal *traversal)
{
    if (!bi->trace_enabled) {
        return;
    }
    if (acount(bi->trace) == BEHAVIOR_ASSET_INSTANCE_TRACE_MAX) {
        free(bi->trace[0].event_name);
        memmove(
            bi->trace,
            bi->trace + 1,
            (size_t)(BEHAVIOR_ASSET_INSTANCE_TRACE_MAX - 1) * sizeof *bi->trace
        );
        asetlen(bi->trace, BEHAVIOR_ASSET_INSTANCE_TRACE_MAX - 1);
    }
    const node *current = flow_run_current_node(run);
    const flow_error *error = flow_run_error(run);
    behavior_instance_trace_entry entry = {
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

static void instance_trace_run(behavior_instance *bi, const flow_run *run)
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
static int scheduler_pump(behavior_instance *bi)
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

behavior_instance *behavior_instance_create(
    behavior_asset *bp,
    const pin_value *owner,
    flow_clock_fn now,
    void *now_user
)
{
    if (!bp) {
        return NULL;
    }
    behavior_instance *bi = (behavior_instance *)xcalloc(1, sizeof *bi);
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

static node *instance_event_by_id(behavior_instance *bi, uint64_t event_id)
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

static behavior_delegate_binding *find_delegate(behavior_instance *source, uint64_t id, int *index)
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

static void remove_incoming_ref(behavior_instance *target, behavior_instance *source, uint64_t id)
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

static int delegate_signature_matches(const behavior_delegate_binding *binding, const node *event)
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

int behavior_instance_remove_activation(behavior_instance *bi, flow_run *act)
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

int behavior_instance_rebind(behavior_instance *bi, const pin_value *owner)
{
    if (!bi || !bi->alive || acount(bi->activations)) {
        return -1;
    }
    instance_init_values(bi);
    aclear(bi->scratch);
    instance_set_owner(bi, owner);
    return 0;
}

void behavior_instance_set_trace_enabled(behavior_instance *bi, int enabled)
{
    if (bi) {
        bi->trace_enabled = enabled != 0;
        if (!bi->trace_enabled) {
            instance_trace_clear(bi);
        }
    }
}

int behavior_instance_set_activation_pool(behavior_instance *bi, int enabled)
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

int behavior_instance_release_activation(behavior_instance *bi, flow_run *act)
{
    if (!bi || !act || behavior_instance_remove_activation(bi, act) != 0) {
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

const behavior_instance_trace_entry *behavior_instance_trace(const behavior_instance *bi, int *count)
{
    if (count) {
        *count = bi ? acount(bi->trace) : 0;
    }
    return bi ? bi->trace : NULL;
}

static void instance_detach_bindings(behavior_instance *bi)
{
    /* EVT-14: detach weak incoming and outgoing bindings before releasing the
     * target instance. No callback can observe this instance afterward. */
    while (acount(bi->delegate_incoming)) {
        behavior_delegate_ref ref = bi->delegate_incoming[acount(bi->delegate_incoming) - 1];
        adel(bi->delegate_incoming, acount(bi->delegate_incoming) - 1);
        behavior_instance_unbind_event(ref.source, ref.binding_id);
    }
    for (int i = 0; i < acount(bi->delegates); i++) {
        remove_incoming_ref(bi->delegates[i].target, bi, bi->delegates[i].id);
    }
    afree(bi->delegates);
    afree(bi->delegate_incoming);
}

int behavior_instance_pool_release(behavior_instance *bi)
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

behavior_instance *behavior_instance_pool_reuse(
    behavior_instance *bi,
    behavior_asset *bp,
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

static bool same_type_name(const char *a, const char *b)
{
    return strcmp(a ? a : "", b ? b : "") == 0;
}

static int variable_index(const behavior_asset *bp, uint64_t id)
{
    for (int i = 0; i < acount(bp->variables); i++) {
        if (bp->variables[i].id == id) {
            return i;
        }
    }
    return -1;
}

static const node *asset_node(const behavior_asset *bp, uint64_t id)
{
    for (int i = 0; i < acount(bp->graphs); i++) {
        const node *n = graph_node_by_id(bp->graphs[i], id);
        if (n) {
            return n;
        }
    }
    return NULL;
}

static bool value_borrows_struct(const pin_value *value)
{
    if (value->container < PIN_CONTAINER_ARRAY) {
        return value->type == PIN_STRUCT && value->ptr;
    }
    const pin_array *array = value->ptr;
    for (int i = 0; array && i < array->count; i++) {
        if (value_borrows_struct(&array->items[i])) {
            return true;
        }
    }
    return false;
}

instance_update_status behavior_instance_can_update(const behavior_instance *bi, const behavior_asset *next)
{
    assert(bi && bi->alive && next);
    if (!same_type_name(bi->bp->owner_type_name, next->owner_type_name) ||
        acount(bi->bp->variables) != acount(next->variables) ||
        acount(bi->delegates) || acount(bi->delegate_incoming)) {
        return INSTANCE_UPDATE_RESTART;
    }
    for (int i = 0; i < acount(next->variables); i++) {
        const behavior_variable *v = &next->variables[i];
        int old = variable_index(bi->bp, v->id);
        if (old < 0) {
            return INSTANCE_UPDATE_RESTART;
        }
        const behavior_variable *previous = &bi->bp->variables[old];
        if (v->type != previous->type || v->container != previous->container ||
            !same_type_name(v->type_name, previous->type_name) ||
            value_borrows_struct(&bi->variables[old])) {
            return INSTANCE_UPDATE_RESTART;
        }
    }
    for (int i = 0; i < acount(bi->scratch); i++) {
        const node *old = asset_node(bi->bp, bi->scratch[i].node_id);
        const node *replacement = asset_node(next, bi->scratch[i].node_id);
        if (!old || !replacement || old->desc != replacement->desc ||
            old->reference_id != replacement->reference_id ||
            old->input_count != replacement->input_count ||
            old->output_count != replacement->output_count ||
            value_borrows_struct(&bi->scratch[i].value)) {
            return INSTANCE_UPDATE_RESTART;
        }
    }
    return acount(bi->activations) ? INSTANCE_UPDATE_BUSY : INSTANCE_UPDATE_READY;
}

void behavior_instance_update(behavior_instance *bi, behavior_asset *next)
{
    assert(behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_READY);
    pin_value *values = bi->variable_count
                          ? xmalloc((size_t)bi->variable_count * sizeof *values)
                          : NULL;
    arena *arenas = bi->variable_count
                      ? xmalloc((size_t)bi->variable_count * sizeof *arenas)
                      : NULL;
    for (int i = 0; i < bi->variable_count; i++) {
        int old = variable_index(bi->bp, next->variables[i].id);
        values[i] = bi->variables[old];
        arenas[i] = bi->variable_arenas[old];
    }
    for (int i = 0; i < acount(bi->scratch); i++) {
        instance_own_type_names(bi, &bi->scratch[i].value);
        if (bi->scratch[i].value.container == PIN_CONTAINER_SINGLE &&
            (bi->scratch[i].value.type == PIN_STR ||
             bi->scratch[i].value.type == PIN_NAME)) {
            bi->scratch[i].value.str = instance_own_str(bi, bi->scratch[i].value.str);
        }
    }
    free(bi->variables);
    bi->variables = values;
    free(bi->variable_arenas);
    bi->variable_arenas = arenas;
    for (int i = 0; i < acount(bi->activation_pool); i++) {
        flow_run_free(bi->activation_pool[i]);
    }
    aclear(bi->activation_pool);
    instance_trace_clear(bi);
    compile_report_free(&bi->compile);
    compile_report_init(&bi->compile);
    bi->compile_revision = 0;
    graph_flow_resume(bi->bp->graph);
    bi->bp = next;
    bi->owner.type_name = next->owner_type_name;
    graph_flow_pause(next->graph);
}

void behavior_instance_destroy(behavior_instance *bi)
{
    if (!bi) {
        return;
    }
    behavior_instance_pool_release(bi);
    for (int i = 0; i < acount(bi->activation_pool); i++) {
        flow_run_free(bi->activation_pool[i]);
    }
    afree(bi->activation_pool);
    instance_trace_clear(bi);
    afree(bi->trace);
    free(bi->variables);
    free(bi->variable_arenas);
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

uint64_t behavior_instance_bind_event(behavior_instance *source, behavior_instance *target, uint64_t event_id)
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
    behavior_delegate_binding binding = { .id = source->next_delegate_id++,
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
    apush(target->delegate_incoming, (behavior_delegate_ref){ source, binding.id });
    return binding.id;
}

int behavior_instance_unbind_event(behavior_instance *source, uint64_t binding_id)
{
    int index = -1;
    behavior_delegate_binding *binding = find_delegate(source, binding_id, &index);
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

flow_run *behavior_instance_dispatch_delegate(
    behavior_instance *source,
    uint64_t binding_id,
    flow_inputs inputs,
    flow_error *error
)
{
    set_error(error, FLOW_OK, NULL, NULL);
    behavior_delegate_binding *binding = find_delegate(source, binding_id, NULL);
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
    return behavior_instance_dispatch(binding->target, event->reference_name, inputs, error);
}

int behavior_instance_bind_service(behavior_instance *bi, const char *type_name, void *value)
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

void *behavior_instance_service(const behavior_instance *bi, const char *type_name)
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

int behavior_instance_broadcast_event(
    behavior_instance *source,
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
        if (behavior_instance_dispatch_delegate(source, snapshot[i], inputs, error)) {
            dispatched++;
        }
    }
    afree(snapshot);
    return dispatched;
}

flow_run *behavior_instance_enqueue(behavior_instance *bi, node *entry, flow_inputs inputs, flow_error *error)
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
        .delta_seconds = bi->delta_seconds,
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

flow_run *behavior_instance_dispatch(
    behavior_instance *bi,
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
     * behavior_asset reports the first blocking diagnostic. */
    if (instance_blocked(bi, error)) {
        return NULL;
    }
    flow_run *act = behavior_instance_enqueue(bi, found, inputs, error);
    if (!act) {
        return NULL;
    }
    scheduler_pump(bi);
    return act;
}

int behavior_instance_tick(behavior_instance *bi, double now)
{
    if (!bi) {
        return FLOW_ERR_ARGUMENT;
    }
    bi->current_time = now;
    return scheduler_pump(bi);
}

int behavior_instance_stop_activation(behavior_instance *bi, flow_run *act)
{
    (void)bi;
    return flow_run_stop(act);
}

int behavior_instance_get_variable(behavior_instance *bi, uint64_t id, pin_value *out)
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

int behavior_instance_copy_variable(behavior_instance *bi, uint64_t id, arena *storage, pin_value *out)
{
    pin_value value;
    if (behavior_instance_get_variable(bi, id, &value)) {
        return -1;
    }
    return instance_copy_value(storage, out, &value);
}

int behavior_instance_set_variable(behavior_instance *bi, uint64_t id, const pin_value *value)
{
    if (!bi || !value) {
        return -1;
    }
    for (int i = 0; i < acount(bi->bp->variables); i++) {
        if (bi->bp->variables[i].id == id) {
            return instance_store_value(bi, i, value);
        }
    }
    return -1;
}

pin_value behavior_instance_owner(const behavior_instance *bi)
{
    return bi ? bi->owner : (pin_value){ .type = PIN_FLOW };
}

void behavior_instance_set_parallel_for(
    behavior_instance *bi,
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

void behavior_instance_set_delta_seconds(behavior_instance *bi, double delta_seconds)
{
    if (bi) {
        bi->delta_seconds = delta_seconds;
    }
}

void behavior_instance_set_breakpoints_enabled(behavior_instance *bi, int enabled)
{
    if (!bi) {
        return;
    }
    bi->breakpoints_override = 1;
    bi->breakpoints_enabled = enabled != 0;
}

int behavior_instance_breakpoints_enabled(const behavior_instance *bi)
{
    if (!bi || !bi->bp || !bi->bp->graph) {
        return 0;
    }
    return bi->breakpoints_override ? bi->breakpoints_enabled
                                    : bi->bp->graph->breakpoints_enabled != 0;
}

pin_value *behavior_instance_scratch(behavior_instance *bi, uint64_t node_id)
{
    for (int i = 0; i < acount(bi->scratch); i++) {
        if (bi->scratch[i].node_id == node_id) {
            return &bi->scratch[i].value;
        }
    }
    apush(bi->scratch, (instance_scratch_slot){ .node_id = node_id });
    return &bi->scratch[acount(bi->scratch) - 1].value;
}
