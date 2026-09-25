#include "graph/runtime/profile.h"
#include "support/arena.h"
/* graph/runtime/instance.c -- persistent Behavior Asset instances and the activation
 * scheduler. An instance owns variable storage, per-node scratch,
 * an optional owner handle, and the ready/suspended activations that belong
 * to it. Activations are flow_run values created instance-owned: they never
 * acquire or release the definition's mutation guard, which the instance
 * holds for its whole lifetime (EXEC-19, RUN-20). */
#include "graph/model/pin_conv.h"
#include "graph/model/registry.h"
#include "graph/nodes/component_nodes.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/instance_bank.h"
#include "graph/runtime/value_array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local behavior_backend debug_backend;

behavior_backend behavior_debug_backend(behavior_backend backend)
{
    behavior_backend previous = debug_backend;
    debug_backend = backend;
    return previous;
}

behavior_backend behavior_default_backend()
{
    if (debug_backend) {
        return debug_backend;
    }
#ifdef __EMSCRIPTEN__
    return BEHAVIOR_BYTECODE;
#else
    return BEHAVIOR_SLJIT;
#endif
}

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
    unsigned long long rev = behavior_asset_revision(bi->asset);
    if (bi->compile_revision != rev) {
        compile_report_free(&bi->compile);
        compile_report_init(&bi->compile);
        behavior_asset_compile(bi->asset, &bi->compile);
        bi->compile_revision = rev;
    }
    if (!compile_report_blocked(&bi->compile)) {
        if (bi->backend == BEHAVIOR_ORACLE &&
            !behavior_asset_has_component_nodes(bi->asset)) {
            return 0;
        }
        behavior_program *program = behavior_program_publish(bi->asset);
        const compile_diagnostic *d = behavior_program_diagnostic(program);
        if (!d) {
            return 0;
        }
        graph *g = behavior_asset_find_graph(bi->asset, d->graph_id);
        set_error(error, FLOW_ERR_COMPILE, g ? graph_node_by_id(g, d->node_id) : NULL, d->message);
        return 1;
    }
    const compile_diagnostic *d = compile_report_first_error(&bi->compile);
    set_error(error, FLOW_ERR_COMPILE, graph_node_by_id(bi->asset->graph, d->node_id), d->message);
    return 1;
}

static bool mutable_state_string(const behavior_instance *bi, const char *s)
{
    for (int i = 0; i < acount(bi->node_states); i++) {
        if (bi->node_states[i].type == PIN_STR && bi->node_states[i].str == s) {
            return true;
        }
    }
    for (int i = 0; i < acount(bi->scratch); i++) {
        if (bi->scratch[i].value.type == PIN_STR && bi->scratch[i].value.str == s) {
            return true;
        }
    }
    return false;
}
static char *instance_own_str(behavior_instance *bi, const char *s)
{
    if (!s) {
        return NULL;
    }
    for (int i = 0; i < acount(bi->strings); i++) {
        if (strcmp(bi->strings[i], s) == 0 &&
            !mutable_state_string(bi, bi->strings[i])) {
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

static const char *copy_type_name(arena *storage, pin_type kind, const char *name)
{
    if (!name) {
        return NULL;
    }
    const char *builtin = pin_type_name(kind);
    if (builtin && !strcmp(name, builtin)) {
        return builtin;
    }
    const type_desc *type = kind == PIN_STRUCT ? registry_type_by_name(name) : NULL;
    if (type) {
        return type->name;
    }
    char *copy = arena_alloc(storage, strlen(name) + 1);
    strcpy(copy, name);
    return copy;
}
static int instance_copy_value(arena *storage, pin_value *out, const pin_value *value, bool names)
{
    *out = *value;
    if (names) {
        out->type_name = copy_type_name(storage, value->type, value->type_name);
    }
    if (out->container >= PIN_CONTAINER_ARRAY) {
        const pin_array *source = value->ptr;
        if (!source) {
            return 0;
        }
        pin_array *array = arena_alloc(
            storage,
            sizeof *array + (size_t)source->count * sizeof *array->items
        );
        *array = (pin_array){ .element_type = source->element_type,
                              .type_name = names ? copy_type_name(
                                                       storage,
                                                       source->element_type,
                                                       source->type_name
                                                   )
                                                 : source->type_name,
                              .count = source->count,
                              .capacity = source->count,
                              .items = (pin_value *)(array + 1) };
        out->ptr = array;
        for (int i = 0; i < array->count; i++) {
            if (instance_copy_value(storage, &array->items[i], &source->items[i], names)) {
                return -1;
            }
        }
        if (out->container == PIN_CONTAINER_SET) {
            pin_array_index(storage, array);
        }
        return 0;
    }
    if ((out->type == PIN_STR || out->type == PIN_NAME) && out->str) {
        size_t size = strlen(out->str) + 1;
        char *copy = arena_alloc(storage, size);
        memcpy(copy, out->str, size);
        out->str = copy;
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
        if (instance_copy_value(storage, &owned, &member, names)) {
            return -1;
        }
        pin_write_field(copy, field, owned);
    }
    out->ptr = copy;
    return 0;
}

static int instance_store_value(behavior_instance *bi, int index, const pin_value *value)
{
    if (value->container == PIN_CONTAINER_SINGLE && value->type != PIN_STR &&
        value->type != PIN_NAME && value->type != PIN_STRUCT) {
        pin_value owned = *value;
        owned.type_name = instance_own_str(bi, value->type_name);
        arena_reset(&bi->variable_arenas[index]);
        bi->variables[index] = owned;
        return 0;
    }
    behavior_variable_storage *fixed = &bi->variable_storage[index];
    if (fixed->data && value->type == PIN_STRUCT &&
        value->container == PIN_CONTAINER_SINGLE) {
        const type_desc *type = value->ptr ? registry_type_by_name(value->type_name)
                                           : NULL;
        if (!value->ptr || (type && type_layout_hash(type) == fixed->layout)) {
            pin_value owned = *value;
            owned.type_name = instance_own_str(bi, value->type_name);
            if (value->ptr) {
                memmove(fixed->data, value->ptr, fixed->type->size);
                owned.ptr = fixed->data;
            }
            arena_free(&bi->variable_arenas[index]);
            bi->variables[index] = owned;
            return 0;
        }
    }
    if (!bi->variable_spares) {
        size_t bytes = (size_t)bi->variable_count * sizeof *bi->variable_arenas;
        bi->variable_arenas = xrealloc(bi->variable_arenas, bytes * 2);
        memset(bi->variable_arenas + bi->variable_count, 0, bytes);
        bi->variable_spares = true;
    }
    arena *current = &bi->variable_arenas[index];
    arena *spare = &bi->variable_arenas[bi->variable_count + index];
    arena_reset(spare);
    pin_value owned;
    if (instance_copy_value(spare, &owned, value, false)) {
        arena_reset(spare);
        return -1;
    }
    instance_own_type_names(bi, &owned);
    /* Inputs may point into the current value. Publish only after the copy and
     * name retention complete, then invalidate the old value for reuse. */
    arena previous = *current;
    *current = *spare;
    *spare = previous;
    bi->variables[index] = owned;
    arena_reset(spare);
    return 0;
}

static void instance_clear_values(behavior_instance *bi)
{
    for (int i = 0; i < bi->variable_count; i++) {
        arena_free(&bi->variable_arenas[i]);
        if (bi->variable_spares) {
            arena_free(&bi->variable_arenas[bi->variable_count + i]);
        }
        bi->variables[i] = (pin_value){ 0 };
        bi->variable_storage[i].type_name = NULL;
    }
    for (int i = 0; i < acount(bi->strings); i++) {
        free(bi->strings[i]);
    }
    aclear(bi->strings);
}

static void instance_free_storage(behavior_instance *bi)
{
    for (int i = 0; i < bi->variable_count; i++) {
        free(bi->variable_storage[i].data);
    }
    free(bi->variable_storage);
}
static void instance_prepare_storage(behavior_instance *bi, int index)
{
    const behavior_variable *v = &bi->asset->variables[index];
    const type_desc *declared = v->type == PIN_STRUCT
                                  ? registry_type_by_name(v->type_name)
                                  : NULL;
    const type_desc *type = v->container == PIN_CONTAINER_SINGLE &&
                                    type_is_plain(declared)
                              ? declared
                              : NULL;
    uint64_t layout = declared ? type_layout_hash(declared) : 0;
    behavior_variable_storage *fixed = &bi->variable_storage[index];
    if (fixed->layout != layout || !!fixed->data != !!type) {
        free(fixed->data);
        fixed->data = type ? xcalloc(1, type->size) : NULL;
    }
    fixed->type = type;
    fixed->layout = layout;
    fixed->type_name = type ? instance_own_str(bi, v->type_name) : NULL;
}
static void instance_free_variables(behavior_instance *bi)
{
    if (bi->variables != behavior_instance_bank_values(bi, bi->variable_count)) {
        free(bi->variables);
    }
    bi->variables = NULL;
}

static void instance_init_values(behavior_instance *bi)
{
    int count = acount(bi->asset->variables);
    instance_clear_values(bi);
    int states;
    bi->state_ids = behavior_program_states(bi->asset->program, &states);
    asetlen(bi->node_states, states);
    if (states) {
        memset(bi->node_states, 0, (size_t)states * sizeof *bi->node_states);
    }
    if (count != bi->variable_count) {
        instance_free_storage(bi);
        instance_free_variables(bi);
        free(bi->variable_arenas);
        bi->variable_arenas = count ? xcalloc((size_t)count, sizeof *bi->variable_arenas)
                                    : NULL;
        bi->variable_spares = false;
        bi->variables = behavior_instance_bank_values(bi, count);
        if (count && !bi->variables) {
            bi->variables = xcalloc((size_t)count, sizeof *bi->variables);
        }
        bi->variable_storage = count ? xcalloc((size_t)count, sizeof *bi->variable_storage)
                                     : NULL;
        bi->variable_count = count;
    }
    if (count) {
        memset(bi->variables, 0, (size_t)count * sizeof *bi->variables);
    }
    for (int i = 0; i < count; i++) {
        instance_prepare_storage(bi, i);
        int result = instance_store_value(bi, i, &bi->asset->variables[i].default_value);
        assert(result == 0);
    }
}

static void instance_set_owner(behavior_instance *bi, const pin_value *owner)
{
    bi->owner = (pin_value){ .type = PIN_FLOW };
    if (bi->asset->owner_type_name) {
        bi->owner = (pin_value){
            .type = PIN_HANDLE,
            .u64 = owner ? owner->u64 : 0,
            .type_name = bi->asset->owner_type_name,
        };
    }
}

static void instance_trace_clear(behavior_instance *bi)
{
    for (int i = 0; i < acount(bi->trace_names); i++) {
        free(bi->trace_names[i]);
    }
    aclear(bi->trace_names);
    aclear(bi->trace);
    aclear(bi->trace_view);
    bi->trace_head = 0;
    bi->trace_view_dirty = false;
}

static const char *instance_trace_name(behavior_instance *bi, const char *name)
{
    /* Event names are fixed by the guarded definition; clearing history also
     * clears this table on hot apply and instance reuse. */
    name = name ? name : "";
    for (int i = 0; i < acount(bi->trace_names); i++) {
        if (!strcmp(bi->trace_names[i], name)) {
            return bi->trace_names[i];
        }
    }
    char *copy = xstrdup(name);
    apush(bi->trace_names, copy);
    return copy;
}

static void instance_trace_push(behavior_instance *bi, behavior_instance_trace_entry entry)
{
    bi->trace_view_dirty = true;
    if (acount(bi->trace) == BEHAVIOR_ASSET_INSTANCE_TRACE_MAX) {
        bi->trace[bi->trace_head] = entry;
        bi->trace_head = (bi->trace_head + 1) % BEHAVIOR_ASSET_INSTANCE_TRACE_MAX;
        return;
    }
    apush(bi->trace, entry);
}

static void instance_trace_run(behavior_instance *bi, const flow_run *run)
{
    if (!bi->trace_enabled) {
        return;
    }
    int count = 0;
    const flow_traversal *traversals = flow_run_traversals(run, &count);
    const node *current = flow_run_current_node(run);
    const flow_error *error = flow_run_error(run);
    behavior_instance_trace_entry entry = {
        .event_name = instance_trace_name(bi, flow_run_event_name(run)),
        .current_node_id = current ? current->id : 0,
        .state = flow_run_status(run),
        .error_code = error ? error->code : FLOW_OK,
    };
    if (!count) {
        entry.traversal.timestamp = bi->clock->current_time;
        instance_trace_push(bi, entry);
        return;
    }
    for (int i = 0; i < count; i++) {
        entry.traversal = traversals[i];
        instance_trace_push(bi, entry);
    }
}

/* Run the scheduler until no activation can make progress: at most one
 * activation runs at a time, and the next ready activation is the oldest
 * non-terminal one (FIFO by dispatch sequence, EXEC-17). */
int behavior_instance_pump(behavior_instance *bi)
{
    int result = FLOW_OK;
    int cursor = 0;
    while (cursor < acount(bi->activations)) {
        flow_run *act = bi->activations[cursor++];
        if (!flow_run_ready(act, bi->clock->current_time)) {
            continue;
        }
        uint64_t revision = bi->schedule_revision;
        double time = bi->clock->current_time;
        result = flow_run_tick_at(act, time);
        /* A callback can edit this queue or wake an older host wait. Restart
         * only then, preserving oldest-ready delivery without quadratic scans
         * over retained completed activations. Never inspect a removed act. */
        if (revision != bi->schedule_revision || time != bi->clock->current_time) {
            cursor = 0;
            continue;
        }
        if (flow_run_ready(act, bi->clock->current_time)) {
            cursor--;
        }
    }
    return result;
}

static behavior_instance *instance_create(behavior_instance *bi, behavior_asset *asset, const pin_value *owner)
{
    bi->asset = asset;
    bi->clock = &bi->local_clock;
    bi->backend = behavior_default_backend();
    behavior_program_publish(asset);
    bi->next_delegate_id = 1;
    bi->alive = 1;
    bi->trace_enabled = 1;
    compile_report_init(&bi->compile);
    instance_init_values(bi);
    instance_set_owner(bi, owner);
    /* EXEC-19 / RUN-20: the instance holds the definition's mutation guard
     * for its whole lifetime. */
    graph_flow_pause(asset->graph);
    return bi;
}

behavior_instance *behavior_instance_create(
    behavior_asset *asset,
    const pin_value *owner,
    flow_clock_fn now,
    void *now_user
)
{
    if (!asset) {
        return NULL;
    }
    behavior_instance *bi = xcalloc(1, sizeof *bi);
    bi->now = now;
    bi->now_user = now_user;
    return instance_create(bi, asset, owner);
}

behavior_instance *behavior_instance_create_in_bank(
    behavior_instance_bank **bank,
    behavior_asset *asset,
    const pin_value *owner
)
{
    if (!asset) {
        return NULL;
    }
    return instance_create(behavior_instance_bank_alloc(bank, acount(asset->variables)), asset, owner);
}

static node *instance_event_by_id(behavior_instance *bi, uint64_t event_id)
{
    if (!bi || !bi->alive) {
        return NULL;
    }
    for (int i = 0; i < acount(bi->asset->graph->nodes); i++) {
        node *n = bi->asset->graph->nodes[i];
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

void behavior_instance_clear_failure(behavior_instance *bi)
{
    if (!bi->failure) {
        return;
    }
    free((char *)bi->failure->event_name);
    free((char *)bi->failure->graph_name);
    free((char *)bi->failure->message);
    free(bi->failure);
    bi->failure = NULL;
}

static void instance_capture_failure(behavior_instance *bi, const flow_run *act)
{
    if (!bi->alive || flow_run_status(act) != FLOW_FAILED) {
        return;
    }
    const flow_error *error = flow_run_error(act);
    const graph *source = NULL;
    int count;
    const flow_call_frame *stack = flow_run_call_stack(act, &count);
    for (int i = count - 1; error->node && i >= 0; i--) {
        if (graph_node_by_id(stack[i].graph, error->node->id) == error->node) {
            source = stack[i].graph;
            break;
        }
    }
    for (int i = 0; !source && error->node && i < acount(bi->asset->graphs); i++) {
        graph *g = bi->asset->graphs[i];
        if (graph_node_by_id(g, error->node->id) == error->node) {
            source = g;
        }
    }
    const char *event = flow_run_event_name(act);
    behavior_instance_failure next = {
        .code = error->code,
        .pin = error->pin,
        .steps = error->steps,
        .graph_id = source ? source->graph_id : 0,
        .node_id = error->node ? error->node->id : 0,
        .program_revision = behavior_program_revision(bi->asset->program),
        .backend = bi->backend,
        .event_name = event ? event : "",
        .graph_name = source && source->name ? source->name : "",
        .message = error->message ? error->message : "Behavior execution failed"
    };
    const behavior_instance_failure *old = bi->failure;
    if (old && old->code == next.code && old->pin == next.pin &&
        old->steps == next.steps && old->graph_id == next.graph_id &&
        old->node_id == next.node_id && old->program_revision == next.program_revision &&
        old->backend == next.backend && !strcmp(old->event_name, next.event_name) &&
        !strcmp(old->message, next.message)) {
        return;
    }
    next.event_name = xstrdup(next.event_name);
    next.graph_name = xstrdup(next.graph_name);
    next.message = xstrdup(next.message);
    behavior_instance_clear_failure(bi);
    bi->failure = xmalloc(sizeof next);
    *bi->failure = next;
}

const behavior_instance_failure *behavior_instance_last_failure(const behavior_instance *bi)
{
    if (!bi) {
        return NULL;
    }
    for (int i = acount(bi->activations) - 1; i >= 0; i--) {
        if (flow_run_status(bi->activations[i]) == FLOW_FAILED) {
            instance_capture_failure((behavior_instance *)bi, bi->activations[i]);
            break;
        }
    }
    return bi->failure;
}

int behavior_instance_remove_activation(behavior_instance *bi, flow_run *act)
{
    if (!bi || !act) {
        return -1;
    }
    /* Direct function runs borrow instance state without joining its event
     * queue. Their teardown must retain failures too. */
    instance_capture_failure(bi, act);
    for (int i = 0; i < acount(bi->activations); i++) {
        if (bi->activations[i] == act) {
            adel(bi->activations, i);
            bi->schedule_revision++;
            if (!acount(bi->activations) && bi->activations_changed) {
                bi->activations_changed(bi);
            }
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
    behavior_instance_clear_failure(bi);
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
    if (!bi || !bi->trace_head) {
        return bi ? bi->trace : NULL;
    }
    /* Inspection is logically const; only its ordered view is refreshed. */
    behavior_instance *view = (behavior_instance *)bi;
    if (view->trace_view_dirty) {
        int length = acount(bi->trace);
        int tail = length - bi->trace_head;
        asetlen(view->trace_view, length);
        memcpy(
            view->trace_view,
            bi->trace + bi->trace_head,
            (size_t)tail * sizeof *bi->trace
        );
        memcpy(
            view->trace_view + tail,
            bi->trace,
            (size_t)bi->trace_head * sizeof *bi->trace
        );
        view->trace_view_dirty = false;
    }
    return bi->trace_view;
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
    bi->local_clock = *bi->clock;
    bi->clock = &bi->local_clock;
    instance_detach_bindings(bi);
    while (acount(bi->activations)) {
        flow_run_free(bi->activations[0]);
    }
    aclear(bi->activations);
    /* Inactive instances release the mutation guard. Cached frames must not
     * retain function graphs that may now be removed from the asset. */
    for (int i = 0; i < acount(bi->activation_pool); i++) {
        flow_run_reset(bi->activation_pool[i]);
    }
    aclear(bi->services);
    behavior_instance_clear_failure(bi);
    bi->profile = NULL;
    bi->profile_user = NULL;
    instance_trace_clear(bi);
    instance_clear_values(bi);
    aclear(bi->scratch);
    aclear(bi->node_states);
    bi->state_ids = NULL;
    compile_report_free(&bi->compile);
    compile_report_init(&bi->compile);
    bi->compile_revision = 0;
    bi->breakpoints_override = 0;
    bi->breakpoints_enabled = 0;
    bi->owner = (pin_value){ .type = PIN_FLOW };
    graph_flow_resume(bi->asset->graph);
    return 0;
}

behavior_instance *behavior_instance_pool_reuse(
    behavior_instance *bi,
    behavior_asset *asset,
    const pin_value *owner,
    flow_clock_fn now,
    void *now_user
)
{
    if (!bi || !asset || bi->alive || bi->asset != asset) {
        return NULL;
    }
    bi->alive = 1;
    bi->now = now;
    bi->now_user = now_user;
    bi->breakpoints_override = 0;
    bi->breakpoints_enabled = 0;
    bi->next_delegate_id = 1;
    behavior_program_publish(asset);
    instance_init_values(bi);
    instance_set_owner(bi, owner);
    graph_flow_pause(asset->graph);
    return bi;
}

static bool same_type_name(const char *a, const char *b)
{
    return strcmp(a ? a : "", b ? b : "") == 0;
}

static int variable_index(const behavior_asset *asset, uint64_t id)
{
    for (int i = 0; i < acount(asset->variables); i++) {
        if (asset->variables[i].id == id) {
            return i;
        }
    }
    return -1;
}

static const node *asset_node(const behavior_asset *asset, uint64_t id)
{
    for (int i = 0; i < acount(asset->graphs); i++) {
        const node *n = graph_node_by_id(asset->graphs[i], id);
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

static bool state_compatible(
    const behavior_instance *bi,
    const behavior_asset *next,
    uint64_t id,
    const pin_value *value
)
{
    const node *old = asset_node(bi->asset, id), *replacement = asset_node(next, id);
    return old && replacement && old->desc == replacement->desc &&
           old->reference_id == replacement->reference_id &&
           old->input_count == replacement->input_count &&
           old->output_count == replacement->output_count &&
           !value_borrows_struct(value);
}
static void retain_state(behavior_instance *bi, pin_value *value)
{
    instance_own_type_names(bi, value);
    if (value->container == PIN_CONTAINER_SINGLE &&
        (value->type == PIN_STR || value->type == PIN_NAME) && value->str) {
        /* Mutable masks with equal contents must remain independent. Already
         * owned buffers survive a generation change without another copy. */
        for (int i = 0; i < acount(bi->strings); i++) {
            if (value->str == bi->strings[i]) {
                return;
            }
        }
        char *copy = xstrdup(value->str);
        apush(bi->strings, copy);
        value->str = copy;
    }
}

instance_update_status behavior_instance_can_update(const behavior_instance *bi, const behavior_asset *next)
{
    assert(bi && bi->alive && next);
    if (!same_type_name(bi->asset->owner_type_name, next->owner_type_name) ||
        acount(bi->asset->variables) != acount(next->variables) ||
        acount(bi->delegates) || acount(bi->delegate_incoming)) {
        return INSTANCE_UPDATE_RESTART;
    }
    for (int i = 0; i < acount(next->variables); i++) {
        const behavior_variable *v = &next->variables[i];
        int old = variable_index(bi->asset, v->id);
        if (old < 0) {
            return INSTANCE_UPDATE_RESTART;
        }
        const behavior_variable *previous = &bi->asset->variables[old];
        if (v->type != previous->type || v->container != previous->container ||
            !same_type_name(v->type_name, previous->type_name) ||
            (bi->variable_storage[old].layout &&
             bi->variable_storage[old].layout !=
                 type_layout_hash(registry_type_by_name(v->type_name)))) {
            return INSTANCE_UPDATE_RESTART;
        }
    }
    for (int i = 0; i < acount(bi->scratch); i++) {
        if (!state_compatible(bi, next, bi->scratch[i].node_id, &bi->scratch[i].value)) {
            return INSTANCE_UPDATE_RESTART;
        }
    }
    for (int i = 0; i < acount(bi->node_states); i++) {
        if (bi->node_states[i].type != PIN_FLOW &&
            !state_compatible(bi, next, bi->state_ids[i], &bi->node_states[i])) {
            return INSTANCE_UPDATE_RESTART;
        }
    }
    return acount(bi->activations) ? INSTANCE_UPDATE_BUSY : INSTANCE_UPDATE_READY;
}

void behavior_instance_update(behavior_instance *bi, behavior_asset *next)
{
    assert(behavior_instance_can_update(bi, next) == INSTANCE_UPDATE_READY);
    int count;
    const uint64_t *ids = behavior_program_states(behavior_program_publish(next), &count);
    pin_value *states = NULL;
    asetlen(states, count);
    for (int i = 0; i < count; i++) {
        states[i] = (pin_value){ 0 };
        for (int j = 0; j < acount(bi->node_states); j++) {
            if (ids[i] == bi->state_ids[j]) {
                states[i] = bi->node_states[j];
                retain_state(bi, &states[i]);
                break;
            }
        }
    }
    afree(bi->node_states);
    bi->node_states = states;
    bi->state_ids = ids;
    pin_value *values = bi->variable_count
                          ? xmalloc((size_t)bi->variable_count * sizeof *values)
                          : NULL;
    arena *arenas = bi->variable_count
                      ? xmalloc(
                            (size_t)bi->variable_count *
                            (bi->variable_spares ? 2 : 1) * sizeof *arenas
                        )
                      : NULL;
    behavior_variable_storage *storage = bi->variable_count
                                           ? xmalloc((size_t)bi->variable_count * sizeof *storage)
                                           : NULL;
    for (int i = 0; i < bi->variable_count; i++) {
        int old = variable_index(bi->asset, next->variables[i].id);
        values[i] = bi->variables[old];
        arenas[i] = bi->variable_arenas[old];
        if (bi->variable_spares) {
            arenas[bi->variable_count + i] = bi->variable_arenas[bi->variable_count + old];
        }
        storage[i] = bi->variable_storage[old];
    }
    for (int i = 0; i < acount(bi->scratch); i++) {
        retain_state(bi, &bi->scratch[i].value);
    }
    if (bi->variable_count) {
        memcpy(bi->variables, values, (size_t)bi->variable_count * sizeof *values);
    }
    free(values);
    free(bi->variable_arenas);
    bi->variable_arenas = arenas;
    free(bi->variable_storage);
    bi->variable_storage = storage;
    for (int i = 0; i < acount(bi->activation_pool); i++) {
        flow_run_free(bi->activation_pool[i]);
    }
    aclear(bi->activation_pool);
    instance_trace_clear(bi);
    compile_report_free(&bi->compile);
    compile_report_init(&bi->compile);
    bi->compile_revision = 0;
    graph_flow_resume(bi->asset->graph);
    bi->asset = next;
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
    afree(bi->trace_view);
    afree(bi->trace_names);
    instance_free_variables(bi);
    free(bi->variable_arenas);
    instance_free_storage(bi);
    afree(bi->activations);
    afree(bi->services);
    afree(bi->scratch);
    afree(bi->node_states);
    for (int i = 0; i < acount(bi->strings); i++) {
        free(bi->strings[i]);
    }
    afree(bi->strings);
    compile_report_free(&bi->compile);
    behavior_instance_bank_release(bi);
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
            bi->services_revision++;
            bi->services[i].value = value;
            if (bi->services_changed) {
                bi->services_changed(bi);
            }
            return 0;
        }
    }
    apush(bi->services, (service_binding){ type_name, value });
    bi->services_revision++;
    if (bi->services_changed) {
        bi->services_changed(bi);
    }
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
        .initial_time = bi->clock->current_time,
        .delta_seconds = bi->clock->delta_seconds,
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
        act = flow_run_start_reuse(apop(bi->activation_pool), bi->asset->graph, entry, inputs, &opts, error);
    } else {
        act = flow_run_start(bi->asset->graph, entry, inputs, &opts, error);
    }
    if (!act) {
        return NULL;
    }
    apush(bi->activations, act);
    bi->schedule_revision++;
    if (acount(bi->activations) == 1 && bi->activations_changed) {
        bi->activations_changed(bi);
    }
    return act;
}

static flow_run *dispatch_prepare(behavior_instance *bi, const char *name, flow_inputs inputs, flow_error *error)
{
    set_error(error, FLOW_OK, NULL, NULL);
    if (!bi || !name || !name[0]) {
        set_error(error, FLOW_ERR_ARGUMENT, NULL, "event name is required");
        return NULL;
    }
    node *found = NULL;
    if (bi->backend != BEHAVIOR_ORACLE) {
        bool duplicate;
        /* Creation, reuse, hot apply and backend selection publish before
         * dispatch. The instance guards that immutable generation. */
        found = behavior_program_event(bi->asset->program, name, &duplicate);
        if (duplicate) {
            set_error(error, FLOW_ERR_DUPLICATE_EVENT, found, "duplicate event name");
            return NULL;
        }
    }
    for (int i = 0;
         bi->backend == BEHAVIOR_ORACLE && i < acount(bi->asset->graph->nodes);
         i++) {
        node *n = bi->asset->graph->nodes[i];
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
    return behavior_instance_enqueue(bi, found, inputs, error);
}

flow_run *behavior_instance_dispatch(
    behavior_instance *bi,
    const char *name,
    flow_inputs inputs,
    flow_error *error
)
{
    behavior_profile_scope scope;
    bool profile = bi && bi->profile;
    if (profile) {
        behavior_profile_enter(&scope);
    }
    flow_run *act = dispatch_prepare(bi, name, inputs, error);
    if (profile) {
        uint64_t elapsed = behavior_profile_leave(&scope);
        if (act) {
            bi->profile(name, elapsed, elapsed + scope.children, BEHAVIOR_PROFILE_SETUP, bi->profile_user);
        }
    }
    if (act) {
        behavior_instance_pump(bi);
    }
    return act;
}

int behavior_instance_tick(behavior_instance *bi, double now)
{
    if (!bi) {
        return FLOW_ERR_ARGUMENT;
    }
    assert(bi->clock == &bi->local_clock);
    bi->local_clock.current_time = now;
    return behavior_instance_pump(bi);
}

void behavior_instance_bind_clock(behavior_instance *bi, const behavior_clock *clock)
{
    assert(clock && !acount(bi->activations));
    bi->clock = clock;
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
    for (int i = 0; i < acount(bi->asset->variables); i++) {
        if (bi->asset->variables[i].id == id) {
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
    return instance_copy_value(storage, out, &value, true);
}

int behavior_instance_set_variable(behavior_instance *bi, uint64_t id, const pin_value *value)
{
    if (!bi || !value) {
        return -1;
    }
    for (int i = 0; i < acount(bi->asset->variables); i++) {
        if (bi->asset->variables[i].id == id) {
            return instance_store_value(bi, i, value);
        }
    }
    return -1;
}
static void store_value(void *owner, int index, const pin_value *value)
{
    int result = instance_store_value(owner, index, value);
    assert(!result);
    (void)result;
}
static const char *own_type_name(void *owner, const char *name)
{
    return instance_own_str(owner, name);
}
void behavior_instance_bind_values(behavior_instance *bi, behavior_function_context *ctx)
{
    ctx->variables = bi->variables;
    ctx->states = bi->node_states;
    ctx->select_state = behavior_instance_select_state;
    ctx->instance_time = &bi->clock->current_time;
    ctx->variable_storage = bi->variable_storage;
    ctx->owner_type_name = bi->owner.type_name;
    ctx->value_owner = bi;
    ctx->store_value = store_value;
    ctx->own_type_name = own_type_name;
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
        assert(bi->clock == &bi->local_clock);
        bi->local_clock.delta_seconds = delta_seconds;
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

pin_value *behavior_instance_scratch(behavior_instance *bi, uint64_t node_id)
{
    for (int i = 0; i < acount(bi->node_states); i++) {
        if (bi->state_ids[i] == node_id) {
            return &bi->node_states[i];
        }
    }
    for (int i = 0; i < acount(bi->scratch); i++) {
        if (bi->scratch[i].node_id == node_id) {
            return &bi->scratch[i].value;
        }
    }
    apush(bi->scratch, (instance_scratch_slot){ .node_id = node_id });
    return &bi->scratch[acount(bi->scratch) - 1].value;
}

int behavior_instance_set_backend(behavior_instance *bi, behavior_backend backend)
{
    if (acount(bi->activations) || backend == BEHAVIOR_UNAVAILABLE) {
        return -1;
    }
#ifdef __EMSCRIPTEN__
    if (backend == BEHAVIOR_SLJIT) {
        return -1;
    }
#endif
    bi->backend = backend;
    if (backend != BEHAVIOR_ORACLE) {
        behavior_program_publish(bi->asset);
    }
    return 0;
}

behavior_backend behavior_instance_backend(const behavior_instance *bi)
{
    if (bi->backend == BEHAVIOR_ORACLE) {
        return BEHAVIOR_ORACLE;
    }
    return behavior_program_backend(bi->asset->program) == BEHAVIOR_UNAVAILABLE
             ? BEHAVIOR_UNAVAILABLE
             : bi->backend;
}
