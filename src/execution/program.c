#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/borrow.h"
#include "execution/component.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/native.h"
#include "execution/program_inspection.h"
#include "execution/program_internal.h"
#include "execution/query.h"
#include "execution/request.h"
#include "execution/runner.h"
#include "execution/scalar.h"
#include "execution/status.h"
#include "runtime/tasks.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/pointer.h"
#include "types/record.h"
#include "types/text.h"
#include "types/union.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PENDING_SERVICE_LIMIT 256
#define SYSTEM_SCRATCH_BYTES ((size_t)64 * 1024)
#define ROW_WORKER_BYTES ((size_t)1024 * 1024)
#define ROW_CHUNK_MIN 64

typedef struct execution_row_worker
{
    execution_program *program;
    const uint64_t *rows;
    int first, end, width, status;
    uint32_t worker;
} execution_row_worker;
static void row_workers_trim(execution_program *, int);

void program_query_evaluator_free(query_evaluator *e)
{
    behavior_function_release_host(&e->context);
    behavior_function_activation_free(&e->activation);
    arena_free(&e->storage);
    execution_module_free((execution_module *)e->module);
    free(e);
}
static void query_cache_clear(execution_program *p)
{
    while (p->query_evaluators) {
        query_evaluator *e = p->query_evaluators;
        p->query_evaluators = e->next;
        program_query_evaluator_free(e);
    }
    p->query_cache_bytes = 0;
}
query_evaluator *program_query_evaluator_take(
    execution_program *p,
    const execution_module *module,
    const behavior_function *f
)
{
    query_evaluator **slot = p ? &p->query_evaluators : NULL;
    while (slot && *slot) {
        query_evaluator *e = *slot;
        if (e->function == f) {
            *slot = e->next;
            p->query_cache_bytes -= e->bytes;
            e->next = NULL;
            e->activation.natives = p->natives;
            return e;
        }
        slot = &e->next;
    }
    query_evaluator *e = xcalloc(1, sizeof *e);
    e->function = f;
    e->module = module;
    execution_module_retain((execution_module *)module);
    if (behavior_function_resumable(f)) {
        behavior_function_activation_init(f, &e->activation);
    }
    /* Predicates and evaluators call the program's bound natives. */
    e->activation.natives = p ? p->natives : NULL;
    return e;
}
void program_query_evaluator_keep(execution_program *p, query_evaluator *e)
{
    /* Accepted row fields already belong to the managed heap. Clear every
     * borrowed value before reusing the evaluator's scratch storage. */
    behavior_function_reset_host(&e->context);
    behavior_function_reset_activation(e->function, &e->activation);
    arena_reset(&e->storage);
    e->bytes = sizeof *e + arena_storage_bytes(&e->storage) +
               behavior_function_host_storage_bytes(&e->context) +
               behavior_function_activation_storage_bytes(e->function, &e->activation);
    if (!p || p->module != e->module || e->bytes > SYSTEM_SCRATCH_BYTES) {
        program_query_evaluator_free(e);
        return;
    }
    while (p->query_cache_bytes > SYSTEM_SCRATCH_BYTES - e->bytes) {
        query_evaluator **oldest = &p->query_evaluators;
        while ((*oldest)->next) {
            oldest = &(*oldest)->next;
        }
        query_evaluator *old = *oldest;
        *oldest = NULL;
        p->query_cache_bytes -= old->bytes;
        program_query_evaluator_free(old);
    }
    e->next = p->query_evaluators;
    p->query_evaluators = e;
    p->query_cache_bytes += e->bytes;
}

static bool paused_system(const execution_program *p)
{
    return p->system_prepared && p->module->effects.reduce_count &&
           p->system_status == FLOW_PAUSED_RESULT && !p->system_ran;
}

/* The shared array implementation uses int capacities and byte multiplication.
 * Check both its next doubling and requested length before growing storage. */
static bool array_room(int count, int capacity, int extra, size_t item_size)
{
    size_t limit = ((size_t)INT_MAX - sizeof(dyna_hdr)) / item_size;
    return extra >= 0 && (size_t)count + (size_t)extra <= limit &&
           ((size_t)count + (size_t)extra <= (size_t)capacity ||
            (size_t)capacity <= limit / 2);
}

static size_t scratch_bytes(const void *array, int capacity, size_t item)
{
    return array ? sizeof(dyna_hdr) + (size_t)capacity * item : 0;
}

static size_t row_scratch_bytes(const execution_program *p)
{
    return scratch_bytes(p->query_values, acap(p->query_values), sizeof(pin_value)) +
           scratch_bytes(p->system_values, acap(p->system_values), sizeof(pin_value)) +
           scratch_bytes(p->system_rows, acap(p->system_rows), sizeof *p->system_rows);
}

/* Row tuples and their value snapshots share one retained budget, so a
 * query's values never multiply what an idle program keeps. */
static void row_scratch_trim(execution_program *p)
{
    if (row_scratch_bytes(p) > SYSTEM_SCRATCH_BYTES) {
        afree(p->query_values);
    }
    if (row_scratch_bytes(p) > SYSTEM_SCRATCH_BYTES) {
        afree(p->system_values);
    }
    if (!acount(p->system_rows) && row_scratch_bytes(p) > SYSTEM_SCRATCH_BYTES) {
        afree(p->system_rows);
    }
}

static void query_values_clear(execution_program *p)
{
    aclear(p->query_values);
    aclear(p->system_values);
    row_scratch_trim(p);
}

static bool query_dynamic(const execution_module *m, int index)
{
    const execution_module_query *q = &m->plan.queries[index];
    if (q->kind == EXECUTION_QUERY_VALUE || q->kind == EXECUTION_QUERY_CHANGED) {
        return true;
    }
    for (int i = 0; i < q->child_count; i++) {
        if (query_dynamic(m, q->children[i])) {
            return true;
        }
    }
    return false;
}
static bool stream_function(const behavior_function *f, int depth, int *steps)
{
    if (depth > 32 || --*steps < 0) {
        return false;
    }
    for (int i = 0; i < acount(f->instructions); i++) {
        if (--*steps < 0) {
            return false;
        }
        const behavior_function_instruction *instruction = &f->instructions[i];
        /* Debug points also have suspension storage, but cannot pause the
         * uninspected stream. Only real waits and task lifetime prevent reuse. */
        if (instruction->op == BF_SUSPEND || instruction->op == BF_DELAY ||
            instruction->op == BF_RETRIGGER) {
            return false;
        }
        if (instruction->op == BF_CONTEXT && !instruction->target &&
            instruction->offset == offsetof(behavior_function_context, instance_time)) {
            return false;
        }
    }
    for (int i = 0; i < acount(f->calls); i++) {
        if (f->calls[i].task ||
            !stream_function(f->calls[i].function, depth + 1, steps)) {
            return false;
        }
    }
    return true;
}

bool program_same_type(pin_value a, pin_value b)
{
    /* A native record frozen into one side matches the catalog layout the
     * other names. */
    return a.type == b.type && a.container == b.container &&
           pin_value_layout_same(&a, &b) &&
           ((!a.type_name && !b.type_name) ||
            (a.type_name && b.type_name && !strcmp(a.type_name, b.type_name)));
}
static void state_free(state_value *state)
{
    pin_value_collection_release(&state->value);
    arena_free(&state->storage);
    for (int i = 0; i < acount(state->retired); i++) {
        arena_free(&state->retired[i]);
    }
    afree(state->retired);
}
static void event_free(queued_event *event)
{
    for (int i = 0; i < event->count; i++) {
        pin_value_collection_release(&event->arguments[i]);
    }
    free(event->arguments);
    arena_free(&event->storage);
    *event = (queued_event){ 0 };
}
static int event_copy_arguments(
    execution_program *program,
    queued_event *event,
    const pin_value *arguments,
    int count
)
{
    if (program_validate_values(program, arguments, count)) {
        return FLOW_ERR_ARGUMENT;
    }
    event->count = count;
    event->arguments = xcalloc((size_t)(count + 1), sizeof *event->arguments);
    for (int i = 0; i < count; i++) {
        pin_value owned;
        if (pin_value_collection_copy(&program->owner_program->managed->storage, &owned, &arguments[i])) {
            event_free(event);
            return FLOW_ERR_ARGUMENT;
        }
        if (pin_value_snapshot_to_arena(&event->storage, &event->arguments[i], &owned)) {
            pin_value_collection_release(&owned);
            event_free(event);
            return FLOW_ERR_ARGUMENT;
        }
    }
    return FLOW_OK;
}
static int state_assign(execution_program *program, state_value *state, const pin_value *value)
{
    arena storage = { 0 };
    pin_value owned, copy;
    if (program_validate_values(program, value, 1)) {
        return FLOW_ERR_ARGUMENT;
    }
    /* Only values that hold storage share collection owners in the managed
     * heap; a scalar or pointer header would leave its type name there. */
    owned = *value;
    if (pin_value_holds_storage(value) &&
        pin_value_collection_copy(&program->owner_program->managed->storage, &owned, value)) {
        return FLOW_ERR_ARGUMENT;
    }
    if (pin_value_snapshot_to_arena(&storage, &copy, &owned)) {
        pin_value_collection_release(&owned);
        arena_free(&storage);
        return FLOW_ERR_ARGUMENT;
    }
    pin_value_collection_release(&state->value);
    if (state->storage.first) {
        apush(state->retired, state->storage);
    }
    state->value = copy;
    state->storage = storage;
    return FLOW_OK;
}
/* Lowers a compiled query tree to terms. A group needs a scope unless it is
 * an AND of ANDs; every ANY gets one, so neighboring OR chains stay apart.
 * An empty scope holds, so an empty ANY holds a negated one instead. */
static void query_generators(const execution_module *m, int index, ecs_term **terms)
{
    const execution_module_query *q = &m->plan.queries[index];
    if (q->kind == EXECUTION_QUERY_GENERATOR) {
        apush(
            *terms,
            ((ecs_term){ .id = q->component,
                         .src_var = q->source_variable,
                         .target_var = q->target_variable })
        );
        return;
    }
    if (q->kind == EXECUTION_QUERY_ALL) {
        for (int i = 0; i < q->child_count; i++) {
            query_generators(m, q->children[i], terms);
        }
    }
}
static bool query_names_disabled(const execution_module *m, int index)
{
    const execution_module_query *q = &m->plan.queries[index];
    if (q->component == ECS_DISABLED) {
        return true;
    }
    for (int i = 0; i < q->child_count; i++) {
        if (query_names_disabled(m, q->children[i])) {
            return true;
        }
    }
    return false;
}
static void query_import(const execution_module *m, int index, ecs_oper oper, ecs_term **terms)
{
    const execution_module_query *q = &m->plan.queries[index];
    bool filter = (m->plan.query_filter && index == m->plan.query_filter - 1) ||
                  (!m->plan.query_filter && index == m->plan.query);
    if (filter && query_dynamic(m, index)) {
        query_generators(m, index, terms);
        if (query_names_disabled(m, index)) {
            apush(*terms, ((ecs_term){ .id = ECS_DISABLED, .oper = ECS_OPTIONAL }));
        }
        apush(*terms, ((ecs_term){ .kind = ECS_TERM_OPEN, .oper = oper == ECS_NOT ? ECS_AND : oper }));
        apush(*terms, ((ecs_term){ .kind = ECS_TERM_CLOSE }));
        return;
    }
    if (q->kind == EXECUTION_QUERY_HAS || q->kind == EXECUTION_QUERY_RELATION ||
        q->kind == EXECUTION_QUERY_GENERATOR) {
        apush(
            *terms,
            ((ecs_term){ .oper = oper,
                         .id = q->component,
                         .target_var = q->kind == EXECUTION_QUERY_RELATION ? ECS_WILDCARD
                                                                           : q->target_variable,
                         .src_var = q->source_variable,
                         .src_rel = q->source_traversal ? 0 : q->source_relationship,
                         .trav = (ecs_trav)q->source_traversal,
                         .trav_rel = q->source_relationship })
        );
        return;
    }
    if (q->kind == EXECUTION_QUERY_NOT && oper != ECS_OR) {
        query_import(m, q->children[0], oper == ECS_NOT ? ECS_AND : ECS_NOT, terms);
        return;
    }
    bool scoped = q->kind != EXECUTION_QUERY_ALL || oper != ECS_AND;
    ecs_oper inner = q->kind == EXECUTION_QUERY_ANY ? ECS_OR
                   : q->kind == EXECUTION_QUERY_NOT ? ECS_NOT
                                                    : ECS_AND;
    if (scoped) {
        apush(*terms, ((ecs_term){ .kind = ECS_TERM_OPEN, .oper = oper }));
    }
    if (q->kind == EXECUTION_QUERY_ANY && !q->child_count) {
        apush(*terms, ((ecs_term){ .kind = ECS_TERM_OPEN, .oper = ECS_NOT }));
        apush(*terms, ((ecs_term){ .kind = ECS_TERM_CLOSE }));
    }
    for (int i = 0; i < q->child_count; i++) {
        query_import(m, q->children[i], inner, terms);
    }
    if (scoped) {
        apush(*terms, ((ecs_term){ .kind = ECS_TERM_CLOSE }));
    }
}
static bool service_present(const service_binding *services, const char *name)
{
    if (!name) {
        return true;
    }
    for (int i = 0; i < acount(services); i++) {
        if (!strcmp(services[i].type_name, name) && services[i].value) {
            return true;
        }
    }
    return false;
}
static void collect_imports(const execution_module *m, execution_module ***modules)
{
    for (int i = 0; i < m->plan.import_count; i++) {
        execution_module *import = m->plan.imports[i];
        bool seen = false;
        for (int j = 0; j < acount(*modules); j++) {
            seen |= (*modules)[j] == import;
        }
        if (seen) {
            continue;
        }
        apush(*modules, import);
        collect_imports(import, modules);
    }
}
static bool registration_state(const execution_program *p, int index)
{
    return p->registration_state || p->module->plan.states[index].shared;
}
static int initialize_states(execution_program *p, state_value *states, bool shared)
{
    pin_value *defaults = NULL;
    int *indices = NULL;
    for (int i = 0; i < p->module->plan.state_count; i++) {
        if (registration_state(p, i) == shared) {
            apush(defaults, p->module->plan.states[i].initial);
            apush(indices, i);
        }
    }
    int count = acount(defaults);
    int status = program_validate_values(p, defaults, count);
    for (int i = 0; i < count && !status; i++) {
        status = state_assign(p, &states[indices[i]], &defaults[i]);
    }
    afree(defaults);
    afree(indices);
    return status;
}
static bool system_module(const execution_module *module)
{
    for (int i = 0; i < module->plan.function_count; i++) {
        if (module->plan.functions[i].kind == EXECUTION_FUNCTION_SYSTEM) {
            return true;
        }
    }
    return false;
}
static execution_program *program_new(
    execution_module *m,
    ecs_world *world,
    const service_binding *services,
    bool imports,
    bool registration
)
{
    assert(m);
    if ((!world && (m->plan.query >= 0 || m->plan.description_count ||
                    m->plan.binding_count)) ||
        !program_preflight(m, world)) {
        return NULL;
    }
    execution_program *p = xcalloc(1, sizeof *p);
    p->enabled = true;
    p->module = m;
    p->owner_program = p;
    p->managed = xcalloc(1, sizeof *p->managed);
    behavior_managed_heap_init(p->managed);
    p->registration_state = registration;
    execution_module_retain(m);
    p->world = world;
    p->budget = 10000;
    for (int i = 0; i < acount(services); i++) {
        const char *name = services[i].type_name;
        if (!name || !*name) {
            execution_program_free(p);
            return NULL;
        }
        size_t length = strlen(name) + 1;
        char *owned = arena_alloc(&p->service_names, length);
        memcpy(owned, name, length);
        apush(p->services, ((service_binding){ owned, services[i].value }));
    }
    if (world && !service_present(p->services, "ecs.world")) {
        apush(p->services, ((service_binding){ "ecs.world", world }));
    }
    if (world && !service_present(p->services, EVAL_COMPONENT_READER_SERVICE)) {
        p->reader.execution = ecs_world_execution(world);
        apush(p->services, ((service_binding){ EVAL_COMPONENT_READER_SERVICE, &p->reader }));
    }
    for (int i = 0; i < m->plan.service_count; i++) {
        if (!service_present(p->services, m->plan.services[i])) {
            execution_program_free(p);
            return NULL;
        }
    }
    p->shared = xcalloc((size_t)(m->plan.state_count + 1), sizeof *p->shared);
    if (initialize_states(p, p->shared, true)) {
        execution_program_free(p);
        return NULL;
    }
    if (m->plan.query >= 0) {
        for (int i = 0; i < m->plan.query_count; i++) {
            if (m->plan.queries[i].kind == EXECUTION_QUERY_CHANGED) {
                if (ecs_component_track_changes(world, m->plan.queries[i].component)) {
                    execution_program_free(p);
                    return NULL;
                }
                apush(p->change_queries, i);
            }
        }
        ecs_term *terms = NULL;
        if (m->plan.system_description) {
            execution_query_terms(m, m->plan.query, &terms);
        } else {
            query_import(m, m->plan.query, ECS_AND, &terms);
        }
        int status = ecs_query_plan_new(world, terms, acount(terms), &p->query_plan);
        afree(terms);
        if (status) {
            execution_program_free(p);
            return NULL;
        }
        p->queried = true;
    }
    if (!program_binding_order(p)) {
        execution_program_free(p);
        return NULL;
    }
    if (imports) {
        execution_module **modules = NULL;
        collect_imports(m, &modules);
        for (int i = 0; i < acount(modules); i++) {
            service_binding *child_services = NULL;
            for (int j = 0; j < acount(p->services); j++) {
                if (p->services[j].value != &p->reader) {
                    apush(child_services, p->services[j]);
                }
            }
            execution_program *child = program_new(modules[i], world, child_services, false, registration);
            afree(child_services);
            if (!child) {
                afree(modules);
                execution_program_free(p);
                return NULL;
            }
            child->owner_program = p;
            behavior_managed_heap_adopt(p->managed, child->managed);
            child->managed = NULL;
            apush(p->imports, child);
        }
        afree(modules);
    }
    execution_effect_summary effects = execution_module_effects(m);
    /* Shared callees can occur along many paths. Bound this optional proof;
     * falling back to retained rows does not limit program execution. */
    int stream_steps = 4096;
    p->stream_system = -1;
    bool stream_entries = m->plan.function_count == 1 || effects.parallel_safe;
    for (int i = 0; i < m->plan.function_count; i++) {
        const execution_module_function *function = &m->plan.functions[i];
        if (function->kind == EXECUTION_FUNCTION_SYSTEM && p->stream_system < 0) {
            p->stream_system = i;
        } else if (function->kind != EXECUTION_FUNCTION_AUTHORED) {
            stream_entries = false;
        }
    }
    p->query_streaming = (m->plan.query_variable_count || effects.reduce_count) &&
                         m->plan.query >= 0 && !m->plan.state_count &&
                         !m->plan.binding_count && !m->plan.action_count &&
                         !m->plan.import_count && stream_entries &&
                         p->stream_system >= 0 && !acount(p->change_queries) &&
                         effects.declared && !effects.structural &&
                         !effects.host_sink &&
                         stream_function(m->plan.functions[p->stream_system].body, 0, &stream_steps);
    return p;
}
behavior_managed_heap *execution_program_managed_heap(execution_program *program)
{
    return program->owner_program->managed;
}
execution_program *execution_program_new(execution_module *m, ecs_world *world, const service_binding *services)
{
    return program_new(m, world, services, true, system_module(m));
}
static state_value *instance_storage(execution_instance *instance, int index)
{
    return registration_state(instance->program, index)
             ? &instance->program->shared[index]
             : &instance->states[index];
}
static void sync_state(execution_instance *instance)
{
    const execution_module *m = instance->program->module;
    for (int i = 0; i < m->plan.state_count; i++) {
        state_value *state = instance_storage(instance, i);
        instance->values[i] = state->value;
        const type_desc *type = state->value.type == PIN_STRUCT
                                  ? pin_value_type(&state->value)
                                  : NULL;
        instance->layouts[i] = (behavior_variable_storage){
            .data = state->value.ptr,
            .type_name = state->value.type_name,
            .type = type,
            .layout = type ? type_layout_hash(type) : 0
        };
    }
}
static void commit_state(execution_instance *instance)
{
    for (int i = 0; i < instance->program->module->plan.state_count; i++) {
        state_value *state = instance_storage(instance, i);
        pin_value value = instance->values[i];
        if (state->value.type_name != value.type_name) {
            if (state->value.type_name && value.type_name &&
                !strcmp(state->value.type_name, value.type_name)) {
                value.type_name = state->value.type_name;
            } else {
                int status = state_assign(instance->program, state, &value);
                assert(!status);
                (void)status;
                instance->values[i] = state->value;
                continue;
            }
        }
        if (state->value.ptr != value.ptr) {
            pin_value_collection_release(&state->value);
        }
        state->value = value;
        instance->values[i] = value;
    }
}
static void collect_state(execution_program *p, state_value *state)
{
    for (int v = 0; v < acount(state->retired);) {
        bool pinned = false;
        for (int i = 0; i < acount(p->instances); i++) {
            for (int a = 0; a < acount(p->instances[i]->activations); a++) {
                execution_activation *act = p->instances[i]->activations[a];
                if (!act->finished && behavior_function_retains_storage(
                                          act->function,
                                          &act->context,
                                          &state->retired[v]
                                      )) {
                    pinned = true;
                }
            }
        }
        if (pinned) {
            v++;
            continue;
        }
        arena_free(&state->retired[v]);
        adel(state->retired, v);
    }
}
static void collect_scope(execution_program *owner, execution_program *scope)
{
    for (int s = 0; s < scope->module->plan.state_count; s++) {
        if (registration_state(scope, s)) {
            collect_state(owner, &scope->shared[s]);
        } else {
            for (int i = 0; i < acount(scope->instances); i++) {
                collect_state(owner, &scope->instances[i]->states[s]);
            }
        }
    }
}
void program_collect_states(execution_program *p)
{
    p = p->owner_program;
    assert(!p->running);
    collect_scope(p, p);
    for (int i = 0; i < acount(p->imports); i++) {
        collect_scope(p, p->imports[i]);
    }
    program_tasks_collect(p);
    program_collect_payloads(p, NULL);
}
static bool module_function(const execution_module *m, const behavior_function *f)
{
    for (int i = 0; i < m->plan.query_count; i++) {
        if (m->plan.queries[i].predicate == f) {
            return true;
        }
    }
    for (int i = 0; i < m->plan.function_count; i++) {
        if (m->plan.functions[i].body == f || m->plan.functions[i].subscription == f) {
            return true;
        }
    }
    for (int i = 0; i < m->plan.binding_count; i++) {
        if (m->plan.bindings[i].body == f || m->plan.bindings[i].dependencies == f) {
            return true;
        }
    }
    return false;
}
static execution_instance *function_instance(execution_activation *a, const behavior_function *f)
{
    execution_program *p = a->instance->program;
    if (module_function(p->module, f)) {
        return a->instance;
    }
    for (int i = 0; i < acount(p->imports); i++) {
        if (module_function(p->imports[i]->module, f)) {
            return program_instance_row(
                p->imports[i],
                a->instance->owner,
                a->instance->query_variables
            );
        }
    }
    assert(!"executable function has no owning module");
    return NULL;
}
bool program_view_disjoint(
    const behavior_function *f,
    const behavior_function_context *ctx,
    const void *address,
    size_t bytes
)
{
    if (ctx->bind_instance != program_bind_instance &&
        ctx->commit_instance != program_commit_instance) {
        return true;
    }
    execution_instance *instance = function_instance(ctx->inspection_user, f);
    int count = instance->program->module->plan.state_count;
    if (behavior_borrow_overlap(address, bytes, instance, 1, sizeof *instance) ||
        behavior_borrow_overlap(
            address,
            bytes,
            instance->values,
            (size_t)count,
            sizeof *instance->values
        ) ||
        behavior_borrow_overlap(
            address,
            bytes,
            instance->layouts,
            (size_t)count,
            sizeof *instance->layouts
        )) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        const state_value *state = instance_storage(instance, i);
        if (behavior_borrow_overlap(address, bytes, state, 1, sizeof *state)) {
            return false;
        }
    }
    return true;
}
void program_bind_instance(const behavior_function *f, behavior_function_context *ctx)
{
    execution_activation *a = ctx->inspection_user;
    execution_instance *instance = function_instance(a, f);
    assert(instance);
    sync_state(instance);
    if (a->running) {
        instance->running = a;
    }
    instance->time = a->instance->time;
    ctx->variables = instance->values;
    ctx->variable_storage = instance->layouts;
    ctx->value_owner = instance;
    ctx->query_variables = a->instance->query_variables;
    ctx->module = instance->program->module;
    ctx->program = instance->program;
    ctx->states = instance->node_states;
    ctx->instance_time = &instance->time;
}
void program_commit_instance(behavior_function_context *ctx)
{
    commit_state(ctx->value_owner);
}
static void store_value(void *owner, int variable, const pin_value *value)
{
    execution_instance *instance = owner;
    assert(variable >= 0 && variable < instance->program->module->plan.state_count);
    assert(program_same_type(instance->program->module->plan.states[variable].initial, *value));
    commit_state(instance);
    int result = state_assign(instance->program, instance_storage(instance, variable), value);
    assert(!result);
    (void)result;
    sync_state(instance);
}
static void add_set_value(void *owner, int variable, const pin_value *value)
{
    execution_instance *instance = owner;
    assert(variable >= 0 && variable < instance->program->module->plan.state_count);
    commit_state(instance);
    state_value *state = instance_storage(instance, variable);
    assert(state->value.container == PIN_CONTAINER_SET);
    pin_array *set = state->value.ptr;
    int result = pin_set_add_owned(&state->storage, &set, value);
    assert(!result);
    (void)result;
    state->value.ptr = set;
    sync_state(instance);
}
static const char *own_name(void *owner, const char *name)
{
    execution_instance *instance = owner;
    if (!name) {
        return NULL;
    }
    assert(instance->running);
    size_t size = strlen(name) + 1;
    char *copy = arena_alloc(&instance->running->payload, size);
    memcpy(copy, name, size);
    return copy;
}
static int select_state(void *owner, const behavior_state_selection *selection, int start, int flags)
{
    execution_instance *instance = owner;
    assert(selection->index >= 0 && selection->outputs > 0);
    assert(selection->index < acount(instance->node_states));
    const execution_module_action *plan = &instance->program->module->plan
                                               .actions[selection->index];
    assert(
        plan->state_kind == NATIVE_STATE_MULTI &&
        plan->outcome_count == selection->outputs
    );
    (void)plan;
    pin_value *state = &instance->node_states[selection->index];
    int count = selection->outputs;
    if (flags & BEHAVIOR_STATE_RESET) {
        if (state->type == PIN_STR) {
            memset((char *)pin_text(*state).bytes, '0', (size_t)count);
        }
        return -1;
    }
    if (state->type != PIN_STR) {
        char *used = arena_alloc(&instance->selections, (size_t)count + 1);
        memset(used, '0', (size_t)count);
        used[count] = 0;
        const char *error = pin_value_text(&instance->selections, state, used, (size_t)count);
        assert(!error);
        (void)error;
    }
    return behavior_state_select((char *)pin_text(*state).bytes, count, start, flags);
}
execution_instance *execution_program_instance(execution_program *p, uint64_t owner)
{
    for (int i = 0; i < acount(p->instances); i++) {
        if (p->instances[i]->owner == owner) {
            return p->instances[i];
        }
    }
    return NULL;
}
static uint64_t row_hash(uint64_t owner, const uint64_t *variables)
{
    uint64_t hash = owner;
    for (int i = 0; i < ECS_QUERY_VARS; i++) {
        hash ^= variables[i] + UINT64_C(0x9e3779b97f4a7c15) + (hash << 6) +
                (hash >> 2);
    }
    hash ^= hash >> 30;
    hash *= UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= hash >> 27;
    return hash;
}
static int row_bucket(execution_program *p, uint64_t owner, const uint64_t *variables)
{
    int mask = acount(p->row_table) - 1;
    int bucket = (int)(row_hash(owner, variables) & (unsigned)mask);
    while (p->row_table[bucket]) {
        p->query_lookup_steps++;
        execution_instance *instance = p->row_table[bucket];
        if (instance->owner == owner &&
            !memcmp(instance->query_variables, variables, sizeof instance->query_variables)) {
            break;
        }
        bucket = (bucket + 1) & mask;
    }
    return bucket;
}
execution_instance *program_instance_row(execution_program *p, uint64_t owner, const uint64_t *variables)
{
    return acount(p->row_table) ? p->row_table[row_bucket(p, owner, variables)] : NULL;
}
execution_instance *execution_program_row(
    execution_program *p,
    uint64_t owner,
    const uint64_t *variables,
    int count
)
{
    if (count < 0 || count > ECS_QUERY_VARS || (count && !variables)) {
        return NULL;
    }
    uint64_t tuple[ECS_QUERY_VARS] = { 0 };
    if (count) {
        memcpy(tuple, variables, (size_t)count * sizeof *tuple);
    }
    return program_instance_row(p, owner, tuple);
}
static void row_insert(execution_program *p, execution_instance *instance)
{
    int capacity = acount(p->row_table);
    if (acount(p->instances) * 2 >= capacity) {
        capacity = capacity ? capacity * 2 : 16;
        asetlen(p->row_table, capacity);
        memset(p->row_table, 0, (size_t)capacity * sizeof *p->row_table);
        for (int i = 0; i < acount(p->instances); i++) {
            execution_instance *row = p->instances[i];
            p->row_table[row_bucket(p, row->owner, row->query_variables)] = row;
        }
    }
    p->row_table[row_bucket(p, instance->owner, instance->query_variables)] = instance;
}
static void instance_free(execution_instance *);
static void detach_row(execution_program *, int index);
static execution_instance *attach_row(execution_program *p, uint64_t owner, const uint64_t *variables)
{
    execution_instance *existing = program_instance_row(p, owner, variables);
    if (existing) {
        return existing;
    }
    execution_instance *instance = xcalloc(1, sizeof *instance);
    instance->program = p;
    instance->owner = owner;
    memcpy(instance->query_variables, variables, sizeof instance->query_variables);
    asetlen(instance->node_states, p->module->plan.action_count);
    if (p->module->plan.action_count) {
        memset(
            instance->node_states,
            0,
            (size_t)p->module->plan.action_count * sizeof *instance->node_states
        );
    }
    int count = p->module->plan.state_count;
    if (count) {
        instance->states = xcalloc((size_t)count, sizeof *instance->states);
        instance->values = xcalloc((size_t)count, sizeof *instance->values);
        instance->layouts = xcalloc((size_t)count, sizeof *instance->layouts);
    }
    if (p->module->plan.binding_count) {
        instance->bindings = xcalloc(
            (size_t)p->module->plan.binding_count,
            sizeof *instance->bindings
        );
    }
    if (acount(p->change_queries)) {
        instance->query_changes = xcalloc(
            (size_t)acount(p->change_queries),
            sizeof *instance->query_changes
        );
        instance->query_snapshot = xcalloc(
            (size_t)acount(p->change_queries),
            sizeof *instance->query_snapshot
        );
    }
    if (initialize_states(p, instance->states, false)) {
        instance_free(instance);
        return NULL;
    }
    sync_state(instance);
    bool *attached = xcalloc((size_t)(acount(p->imports) + 1), sizeof *attached);
    for (int i = 0; i < acount(p->imports); i++) {
        execution_program *child = p->imports[i];
        attached[i] = !program_instance_row(child, owner, variables);
        if (!attach_row(child, owner, variables)) {
            for (int j = 0; j < i; j++) {
                if (attached[j]) {
                    execution_instance *row = program_instance_row(p->imports[j], owner, variables);
                    detach_row(p->imports[j], row->index);
                }
            }
            free(attached);
            instance_free(instance);
            return NULL;
        }
    }
    free(attached);
    instance->index = acount(p->instances);
    apush(p->instances, instance);
    row_insert(p, instance);
    return instance;
}
execution_instance *execution_program_attach(execution_program *p, uint64_t owner)
{
    if (paused_system(p)) {
        execution_program_cancel_system(p);
    }
    assert(!p->dispatching && !p->system_prepared);
    if (p->module->plan.query_variable_count) {
        return NULL; /* Multi-target rows are bound by the structural query. */
    }
    uint64_t variables[ECS_QUERY_VARS] = { 0 };
    execution_instance *instance = attach_row(p, owner, variables);
    if (instance) {
        instance->query_managed = false;
    }
    return instance;
}
static void instance_free(execution_instance *instance)
{
    program_inspection_remove_owner(instance);
    while (acount(instance->activations)) {
        int index = acount(instance->activations) - 1;
        execution_activation *activation = instance->activations[index];
        adel(instance->activations, index);
        program_activation_free(activation);
    }
    afree(instance->activations);
    program_tasks_free(instance);
    program_observers_free(instance);
    for (int i = 0; i < instance->program->module->plan.state_count; i++) {
        state_free(&instance->states[i]);
    }
    free(instance->states);
    free(instance->values);
    free(instance->layouts);
    for (int i = 0; i < instance->program->module->plan.binding_count; i++) {
        free(instance->bindings[i].inputs);
        arena_free(&instance->bindings[i].storage);
    }
    free(instance->bindings);
    free(instance->query_changes);
    free(instance->query_snapshot);
    afree(instance->node_states);
    arena_free(&instance->selections);
    free(instance);
}
static void detach_row(execution_program *p, int index)
{
    execution_instance *instance = p->instances[index];
    uint64_t owner = instance->owner;
    uint64_t variables[ECS_QUERY_VARS];
    memcpy(variables, instance->query_variables, sizeof variables);
    int bucket = row_bucket(p, instance->owner, instance->query_variables);
    int mask = acount(p->row_table) - 1;
    p->row_table[bucket] = NULL;
    for (int next = (bucket + 1) & mask; p->row_table[next]; next = (next + 1) & mask) {
        execution_instance *row = p->row_table[next];
        p->row_table[next] = NULL;
        p->row_table[row_bucket(p, row->owner, row->query_variables)] = row;
    }
    instance_free(instance);
    int last = acount(p->instances) - 1;
    if (index != last) {
        p->instances[index] = p->instances[last];
        p->instances[index]->index = index;
    }
    asetlen(p->instances, last);
    for (int j = 0; j < acount(p->imports); j++) {
        execution_program *child = p->imports[j];
        execution_instance *row = program_instance_row(child, owner, variables);
        if (row) {
            detach_row(child, row->index);
        }
    }
}
void execution_program_detach(execution_program *p, uint64_t owner)
{
    if (paused_system(p)) {
        bool found = false;
        for (int i = 0; i < acount(p->instances); i++) {
            found |= p->instances[i]->owner == owner;
        }
        if (!found) {
            return;
        }
        execution_program_cancel_system(p);
    }
    assert(!p->dispatching && !p->system_prepared);
    for (int i = 0; i < acount(p->instances);) {
        if (p->instances[i]->owner == owner) {
            detach_row(p, i);
            continue;
        }
        i++;
    }
}
static void replacement_unroot(execution_program *p)
{
    if (p->replacement_of) {
        behavior_managed_heap *heap = p->replacement_of->owner_program->managed;
        for (int i = 0; i < p->replacement_root_count; i++) {
            behavior_managed_heap_unroot(heap, &p->replacement_roots[i]);
        }
    }
    afree(p->replacement_roots);
    p->replacement_root_count = 0;
}
void execution_program_free(execution_program *p)
{
    if (!p) {
        return;
    }
    replacement_unroot(p);
    if (p->system_prepared) {
        assert(paused_system(p));
        execution_program_cancel_system(p);
    }
    row_workers_trim(p, 0);
    afree(p->system_activations);
    afree(p->system_rows);
    afree(p->reductions);
    afree(p->reduction_table);
    program_inspection_retire(p);
    for (int i = 0; i < acount(p->instances); i++) {
        instance_free(p->instances[i]);
    }
    afree(p->instances);
    afree(p->row_table);
    afree(p->change_queries);
    afree(p->query_admissions);
    afree(p->query_values);
    afree(p->system_values);
    query_cache_clear(p);
    for (int i = 0; i < acount(p->imports); i++) {
        execution_program_free(p->imports[i]);
    }
    afree(p->imports);
    for (int i = 0; i < acount(p->events); i++) {
        event_free(&p->events[i]);
    }
    afree(p->events);
    for (int i = 0; i < acount(p->field_edges); i++) {
        pin_value_collection_release(&p->field_edges[i].value);
        arena_free(&p->field_edges[i].storage);
    }
    afree(p->field_edges);
    if (p->shared) {
        for (int i = 0; i < p->module->plan.state_count; i++) {
            state_free(&p->shared[i]);
        }
    }
    free(p->shared);
    ecs_query_plan_free(p->query_plan);
    afree(p->binding_order);
    program_bindings_free(p);
    afree(p->services);
    for (int i = 0; i < acount(p->pending_services); i++) {
        free((void *)p->pending_services[i].type_name);
    }
    afree(p->pending_services);
    arena_free(&p->service_names);
    if (p->managed) {
        behavior_managed_heap_free(p->managed);
        free(p->managed);
    }
    program_activation_cache_free(p);
    execution_module_free(p->module);
    free(p->name);
    free(p);
}
static void record_failure(const behavior_function *f, const behavior_function_context *ctx, int status)
{
    execution_activation *a = ctx->inspection_user;
    execution_program *p = a->instance->program;
    if (p->failure.status) {
        return;
    }
    const execution_module_plan *scope = &((execution_instance *)ctx->value_owner)
                                              ->program->module->plan;
    execution_program_failure error = { .status = status,
                                        .kind = ctx->failure,
                                        .module_id = scope->id,
                                        .owner = ctx->owner,
                                        .node_id = ctx->node_id };
    for (int i = 0; i < scope->function_count; i++) {
        const execution_module_function *fn = &scope->functions[i];
        if (fn->body == f || fn->subscription == f) {
            error.function_id = fn->id;
        }
    }
    const char *asset = p->name;
    if (ctx->error_source >= 0 && ctx->error_source < acount(f->sources)) {
        const behavior_function_source *source = &f->sources[ctx->error_source];
        error.start = source->start;
        error.end = source->end;
        error.line = source->line;
        error.column = source->column;
        if (source->asset_name) {
            asset = source->asset_name;
        }
    }
    snprintf(error.asset_name, sizeof error.asset_name, "%s", asset ? asset : "");
    snprintf(error.message, sizeof error.message, "%s", ctx->error_message ? ctx->error_message : "script execution failed");
    p->failure = error;
}
bool program_world_views_allowed(const behavior_function_context *ctx)
{
    return (!ctx->bind_instance && !ctx->commit_instance && !ctx->failure_observer) ||
           (ctx->bind_instance == program_bind_instance &&
            ctx->commit_instance == program_commit_instance &&
            ctx->failure_observer == record_failure);
}
const execution_program_failure *execution_program_error(const execution_program *p)
{
    assert(p);
    return p->failure.status ? &p->failure : NULL;
}
void execution_program_clear_error(execution_program *p)
{
    assert(p);
    p->failure = (execution_program_failure){ 0 };
}
static int component_membership(
    behavior_function_context *ctx,
    uint64_t owner,
    uint64_t component,
    bool add,
    bool *changed
)
{
    execution_activation *a = ctx->inspection_user;
    ecs_world *world = a->instance->program->world;
    ecs_entity entity = ecs_entity_from_u64(owner);
    if (!world || !ecs_entity_is_alive(world, entity) ||
        !ecs_world_component(world, component)) {
        return FLOW_ERR_ARGUMENT;
    }
    bool present = ecs_component_has(world, entity, component);
    *changed = add != present;
    if (!*changed) {
        return FLOW_OK;
    }
    int status = add ? ecs_world_record_add(world, entity, component, NULL, 0)
                     : ecs_world_record_remove(world, entity, component, 0);
    return status == ECS_OK ? FLOW_OK : FLOW_ERR_STATE;
}
static int store_component(
    behavior_function_context *ctx,
    uint64_t entity,
    uint64_t component,
    uint64_t layout,
    size_t offset,
    pin_value declared,
    pin_value value
)
{
    execution_activation *a = ctx->inspection_user;
    execution_program *p = a->instance->program;
    if (!p->world) {
        return FLOW_ERR_STATE;
    }
    const ecs_component_desc *desc = ecs_world_component(p->world, component);
    if (!desc || type_layout_hash(desc->type) != layout) {
        return FLOW_ERR_STATE;
    }
    return ecs_component_write_value(p->world, ecs_entity_from_u64(entity), component, offset, declared, value) ==
                   ECS_OK
             ? FLOW_OK
             : FLOW_ERR_ARGUMENT;
}
static bool unbounded_field(const type_desc *type, size_t offset, pin_type scalar, int depth)
{
    if (!type || depth == TYPE_MAX_NESTING) {
        return false;
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (offset < field->offset || offset - field->offset >= field->size) {
            continue;
        }
        if (field->range_contract) {
            return false;
        }
        if (field->type == PIN_STRUCT) {
            return unbounded_field(
                type_catalog_find(field->type_name),
                offset - field->offset,
                scalar,
                depth + 1
            );
        }
        return offset == field->offset && field->type == scalar;
    }
    return false;
}
static bool reduction_target(execution_program *p, const component_reduction *r)
{
    const ecs_execution_view *world = p->world ? ecs_world_execution(p->world) : NULL;
    uint32_t entity = world ? ecs_execution_entity(world, r->entity) : 0;
    const ecs_execution_column *column = world
                                           ? ecs_execution_component(world, entity, r->component)
                                           : NULL;
    if (!column || column->layout != r->layout) {
        return false;
    }
    /* Registration fixes the schema for this program's world. Reacquire the
     * column and live entity on every check: storage may grow or rows disappear
     * between collection and whole-log preflight, including across a pause. */
    int count = (int)(sizeof p->reduction_fields / sizeof *p->reduction_fields);
    for (int i = 0; i < count; i++) {
        const reduction_field *field = &p->reduction_fields[i];
        if (field->component == r->component && field->layout == r->layout &&
            field->type == column->type && field->offset == r->offset &&
            field->scalar == r->value.type) {
            return r->op != EXECUTION_REDUCTION_ADD_UNORDERED || field->unbounded;
        }
    }
    if (!ecs_component_accepts_value(
            p->world,
            r->component,
            r->offset,
            (pin_value){ .type = r->value.type }
        )) {
        return false;
    }
    reduction_field field = {
        .component = r->component,
        .layout = r->layout,
        .type = column->type,
        .offset = r->offset,
        .scalar = r->value.type,
        .unbounded = unbounded_field(column->type, r->offset, r->value.type, 0)
    };
    p->reduction_fields[p->next_reduction_field] = field;
    p->next_reduction_field = (p->next_reduction_field + 1) % count;
    return r->op != EXECUTION_REDUCTION_ADD_UNORDERED || field.unbounded;
}
static pin_value reduction_value(pin_value, pin_value, execution_reduction_op);
static bool same_reduction(const component_reduction *a, const component_reduction *b)
{
    return a->entity == b->entity && a->component == b->component &&
           a->layout == b->layout && a->offset == b->offset &&
           a->value.type == b->value.type;
}
static unsigned reduction_hash(const component_reduction *r)
{
    uint64_t hash = r->entity;
    hash ^= r->component + UINT64_C(0x9e3779b97f4a7c15) + (hash << 6) + (hash >> 2);
    hash ^= r->offset + UINT64_C(0x9e3779b97f4a7c15) + (hash << 6) + (hash >> 2);
    hash ^= hash >> 30;
    hash *= UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= hash >> 27;
    return (unsigned)hash;
}
static int reduction_bucket(const execution_program *p, const component_reduction *record)
{
    unsigned mask = (unsigned)acount(p->reduction_table) - 1;
    unsigned bucket = reduction_hash(record) & mask;
    while (p->reduction_table[bucket] &&
           !same_reduction(&p->reductions[p->reduction_table[bucket] - 1], record)) {
        bucket = (bucket + 1) & mask;
    }
    return (int)bucket;
}
static bool reduction_reserve(execution_program *p)
{
    int count = acount(p->reduction_table);
    if (count && p->unordered_reductions < count - count / 4) {
        return true;
    }
    if (!array_room(
            count,
            acap(p->reduction_table),
            count ? count : 16,
            sizeof *p->reduction_table
        )) {
        return false;
    }
    asetlen(p->reduction_table, count ? count * 2 : 16);
    memset(p->reduction_table, 0, (size_t)acount(p->reduction_table) * sizeof *p->reduction_table);
    for (int i = 0; i < acount(p->reductions); i++) {
        if (p->reductions[i].op == EXECUTION_REDUCTION_ADD_UNORDERED) {
            p->reduction_table[reduction_bucket(p, &p->reductions[i])] = i + 1;
        }
    }
    return true;
}
static size_t reduction_capacity(const execution_program *p)
{
    return (p->reductions
                ? sizeof(dyna_hdr) + (size_t)acap(p->reductions) * sizeof *p->reductions
                : 0) +
           (p->reduction_table ? sizeof(dyna_hdr) + (size_t)acap(p->reduction_table) *
                                                        sizeof *p->reduction_table
                               : 0);
}
static void begin_reductions(execution_program *p)
{
    assert(!p->reducing && !acount(p->reductions));
    p->reducing = p->module->effects.reduce_count > 0;
    p->reduction_contributions = p->reduction_records = 0;
    p->reduction_peak_bytes = 0;
    p->row_parallel_jobs = p->row_parallel_workers = 0;
}
static int append_reduction(execution_program *p, component_reduction record)
{
    bool unordered = record.op == EXECUTION_REDUCTION_ADD_UNORDERED;
    if ((unordered && p->ordered_reductions) ||
        (!unordered && p->unordered_reductions)) {
        for (int i = 0; i < acount(p->reductions); i++) {
            if (p->reductions[i].component == record.component &&
                (p->reductions[i].op == EXECUTION_REDUCTION_ADD_UNORDERED) != unordered) {
                return FLOW_ERR_ARGUMENT;
            }
        }
    }
    int bucket = -1;
    if (unordered) {
        int previous = acount(p->reduction_table)
                         ? p->reduction_table[reduction_bucket(p, &record)]
                         : 0;
        if (previous) {
            component_reduction *r = &p->reductions[previous - 1];
            r->value = reduction_value(r->value, record.value, EXECUTION_REDUCTION_ADD);
            p->reduction_contributions++;
            return FLOW_OK;
        }
    }
    if (!array_room(acount(p->reductions), acap(p->reductions), 1, sizeof *p->reductions)) {
        return FLOW_ERR_ARGUMENT;
    }
    if (unordered) {
        if (!reduction_reserve(p)) {
            return FLOW_ERR_ARGUMENT;
        }
        bucket = reduction_bucket(p, &record);
        p->unordered_reductions++;
    } else {
        p->ordered_reductions++;
    }
    apush(p->reductions, record);
    if (bucket >= 0) {
        p->reduction_table[bucket] = acount(p->reductions);
    }
    p->reduction_contributions++;
    p->reduction_records++;
    size_t capacity = reduction_capacity(p);
    if (capacity > p->reduction_peak_bytes) {
        p->reduction_peak_bytes = capacity;
    }
    return FLOW_OK;
}
static int reduce_component(
    behavior_function_context *ctx,
    uint64_t entity,
    uint64_t component,
    uint64_t layout,
    size_t offset,
    pin_value declared,
    pin_value value,
    execution_reduction_op op
)
{
    execution_activation *a = ctx->inspection_user;
    execution_program *p = a->instance->program;
    if (!p->reducing || declared.container != PIN_CONTAINER_SINGLE ||
        declared.type < PIN_I8 || declared.type > PIN_F64 ||
        value.type != declared.type || value.container != PIN_CONTAINER_SINGLE ||
        op < EXECUTION_REDUCTION_ADD || op > EXECUTION_REDUCTION_ADD_UNORDERED) {
        return FLOW_ERR_ARGUMENT;
    }
    component_reduction record = { .entity = entity,
                                   .component = component,
                                   .layout = layout,
                                   .offset = offset,
                                   .value = { .type = declared.type },
                                   .op = op };
    memcpy(&record.value.d, &value.d, behavior_scalar_size(declared.type));
    return reduction_target(p, &record) ? append_reduction(p, record) : FLOW_ERR_ARGUMENT;
}
void execution_program_discard_reductions(execution_program *p)
{
    assert(p && !p->running && !p->dispatching);
    aclear(p->reductions);
    if (reduction_capacity(p) > SYSTEM_SCRATCH_BYTES) {
        afree(p->reductions);
        afree(p->reduction_table);
    }
    if (p->reduction_table) {
        memset(
            p->reduction_table,
            0,
            (size_t)acount(p->reduction_table) * sizeof *p->reduction_table
        );
    }
    p->ordered_reductions = p->unordered_reductions = 0;
    p->reducing = false;
}
static pin_value reduction_value(pin_value current, pin_value value, execution_reduction_op op)
{
    if (op == EXECUTION_REDUCTION_ADD_UNORDERED) {
        op = EXECUTION_REDUCTION_ADD;
    }
    if (op == EXECUTION_REDUCTION_ADD && behavior_scalar_integer(value.type)) {
        return behavior_scalar_from_bits(
            value.type,
            behavior_scalar_bits(current) + behavior_scalar_bits(value)
        );
    }
    switch (value.type) {
#define REDUCE(pin, field)                                                     \
    case pin:                                                                  \
        if (op == EXECUTION_REDUCTION_ADD) {                                   \
            value.field = current.field + value.field;                         \
        } else if (                                                            \
            op == EXECUTION_REDUCTION_MIN ? current.field < value.field        \
                                          : current.field > value.field        \
        ) {                                                                    \
            value.field = current.field;                                       \
        }                                                                      \
        break
        REDUCE(PIN_I8, i8);
        REDUCE(PIN_U8, u8);
        REDUCE(PIN_I16, i16);
        REDUCE(PIN_U16, u16);
        REDUCE(PIN_I32, i);
        REDUCE(PIN_U32, u);
        REDUCE(PIN_I64, i64);
        REDUCE(PIN_U64, u64);
        REDUCE(PIN_F32, f);
        REDUCE(PIN_F64, d);
#undef REDUCE
        default: assert(false); break;
    }
    return value;
}
static int finish_reductions(execution_program *p, int status)
{
    if (status == FLOW_OK) {
        /* Validate the complete log before publishing any contribution. The
         * caller keeps ECS structure fixed through this serial boundary. */
        for (int i = 0; i < acount(p->reductions); i++) {
            if (!reduction_target(p, &p->reductions[i])) {
                status = FLOW_ERR_STATE;
                if (!p->failure.status) {
                    p->failure.status = status;
                    p->failure.kind = BEHAVIOR_FUNCTION_DATA;
                    p->failure.owner = p->reductions[i].entity;
                    snprintf(p->failure.message, sizeof p->failure.message, "Reduction destination changed before publication");
                }
                break;
            }
        }
    }
    for (int i = 0; status == FLOW_OK && i < acount(p->reductions); i++) {
        const component_reduction *r = &p->reductions[i];
        const void *row = ecs_component_data_const(p->world, ecs_entity_from_u64(r->entity), r->component);
        pin_value current = { .type = r->value.type };
        memcpy(
            &current.d,
            (const char *)row + r->offset,
            behavior_scalar_size(current.type)
        );
        pin_value value = reduction_value(current, r->value, r->op);
        int written = ecs_component_write_value(
            p->world,
            ecs_entity_from_u64(r->entity),
            r->component,
            r->offset,
            (pin_value){ .type = value.type },
            value
        );
        assert(written == ECS_OK);
        (void)written;
    }
    execution_program_discard_reductions(p);
    return status;
}
void program_bind_world_callbacks(behavior_function_context *ctx)
{
    ctx->store_value = store_value;
    ctx->add_set_value = add_set_value;
    ctx->own_type_name = own_name;
    ctx->select_state = select_state;
    ctx->bind_instance = program_bind_instance;
    ctx->commit_instance = program_commit_instance;
    ctx->component_membership = component_membership;
    ctx->store_component = store_component;
    ctx->reduce_component = reduce_component;
    ctx->failure_observer = record_failure;
}
static int subscription_matches(
    execution_instance *instance,
    const behavior_function *function,
    uint64_t owner,
    double now,
    double delta,
    unsigned budget
)
{
    execution_activation *a = program_activation_new(instance, function, NULL, 0, now, delta, budget);
    if (!a) {
        return FLOW_ERR_ARGUMENT;
    }
    int status = program_activation_run(a, now, delta, budget);
    if (status != FLOW_OK) {
        program_activation_free(a);
        return status > 0 ? FLOW_ERR_STATE : status;
    }
    pin_value value = a->context.results[0];
    bool matches = false;
    if (value.container >= PIN_CONTAINER_ARRAY) {
        const pin_array *array = value.ptr;
        for (int i = 0; array && i < array->count; i++) {
            matches |= pin_array_get(array, i).u64 == owner;
        }
    } else {
        matches = value.u64 == owner;
    }
    program_activation_free(a);
    return matches;
}
static bool arguments_owned(execution_program *program, const pin_value *arguments, int count)
{
    if (count < 0 || (count && !arguments)) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (behavior_managed_storage_owner_error(
                &program->owner_program->managed->storage,
                &arguments[i]
            )) {
            return false;
        }
    }
    return true;
}
int program_dispatch_handler(
    execution_instance *instance,
    const execution_module_function *function,
    const pin_value *arguments,
    int count,
    double now,
    double delta,
    unsigned budget
)
{
    execution_activation *
        a = program_activation_new(instance, function->body, arguments, count, now, delta, budget);
    if (!a) {
        return FLOW_ERR_ARGUMENT;
    }
    int status = program_activation_run(a, now, delta, budget);
    if (status == FLOW_SUSPENDED_RESULT || status == FLOW_PAUSED_RESULT ||
        acount(a->requests)) {
        apush(instance->activations, a);
    } else {
        program_activation_free(a);
    }
    program_apply_services(instance->program->owner_program);
    return status < 0 ? status : FLOW_OK;
}
static int dispatch_to(
    execution_instance *instance,
    uint64_t event,
    uint64_t source,
    const pin_value *arguments,
    int count,
    double now,
    double delta,
    unsigned budget
)
{
    execution_program *p = instance->program;
    int found = 0;
    for (int i = 0; i < p->module->plan.function_count; i++) {
        const execution_module_function *f = &p->module->plan.functions[i];
        if (f->event != event || f->action || !f->body ||
            f->kind == EXECUTION_FUNCTION_SYSTEM) {
            continue;
        }
        if (f->observer.component) {
            /* Field hooks deliver qualified edges; accepting the event here
             * keeps the direct host notification from rejecting the queue. */
            found++;
            continue;
        }
        if (source && f->subscription) {
            int matches = subscription_matches(instance, f->subscription, source, now, delta, budget);
            if (matches < 0) {
                return matches;
            }
            if (!matches) {
                continue;
            }
        } else if (source && source != instance->owner) {
            continue;
        }
        execution_activation *
            a = program_activation_new(instance, f->body, arguments, count, now, delta, budget);
        if (!a) {
            return FLOW_ERR_ARGUMENT;
        }
        found++;
        int status = program_activation_run(a, now, delta, budget);
        if (status == FLOW_SUSPENDED_RESULT || status == FLOW_PAUSED_RESULT ||
            acount(a->requests)) {
            apush(instance->activations, a);
        } else {
            program_activation_free(a);
        }
        if (status < 0) {
            return status;
        }
    }
    return found ? FLOW_OK : FLOW_ERR_UNKNOWN_EVENT;
}
int execution_instance_dispatch(
    execution_instance *instance,
    uint64_t event,
    const pin_value *arguments,
    int count,
    double now,
    double delta,
    unsigned budget
)
{
    execution_program *program = instance->program;
    if (!arguments_owned(program, arguments, count)) {
        return FLOW_ERR_ARGUMENT;
    }
    /* Scalars are their own copies, so only arguments that hold storage need
     * an owned copy the dispatch roots. */
    bool held = false;
    for (int i = 0; i < count; i++) {
        held |= pin_value_holds_storage(&arguments[i]);
    }
    queued_event values = { 0 };
    queued_event *events = NULL;
    queued_event_batch batch = { .previous = program->dispatch_events };
    if (held) {
        if (event_copy_arguments(program, &values, arguments, count)) {
            return FLOW_ERR_ARGUMENT;
        }
        apush(events, values);
        batch.events = events;
        program->dispatch_events = &batch;
        arguments = values.arguments;
    } else if (program_validate_values(program, arguments, count)) {
        return FLOW_ERR_ARGUMENT;
    }
    int status = dispatch_to(instance, event, instance->owner, arguments, count, now, delta, budget);
    program->dispatch_events = batch.previous;
    afree(events);
    event_free(&values);
    program_apply_services(program->owner_program);
    program_collect_states(program);
    return status;
}
const pin_value *execution_instance_state(const execution_instance *instance, uint64_t id)
{
    for (int i = 0; i < instance->program->module->plan.state_count; i++) {
        if (instance->program->module->plan.states[i].id == id) {
            return &instance_storage((execution_instance *)instance, i)->value;
        }
    }
    return NULL;
}
int execution_instance_set_state(execution_instance *instance, uint64_t id, pin_value value)
{
    if (!arguments_owned(instance->program, &value, 1)) {
        return FLOW_ERR_ARGUMENT;
    }
    for (int i = 0; i < instance->program->module->plan.state_count; i++) {
        if (instance->program->module->plan.states[i].id == id) {
            if (!program_same_type(instance->program->module->plan.states[i].initial, value)) {
                return FLOW_ERR_ARGUMENT;
            }
            int status = state_assign(instance->program, instance_storage(instance, i), &value);
            sync_state(instance);
            if (!instance->running) {
                program_collect_states(instance->program);
            }
            return status;
        }
    }
    return FLOW_ERR_ARGUMENT;
}
int execution_instance_pending(const execution_instance *instance)
{
    int pending = 0;
    for (int i = 0; i < acount(instance->activations); i++) {
        pending += !instance->activations[i]->finished;
    }
    return pending;
}
int execution_program_emit(
    execution_program *p,
    uint64_t event,
    uint64_t owner,
    const pin_value *arguments,
    int count
)
{
    if (!event || !arguments_owned(p, arguments, count)) {
        return FLOW_ERR_ARGUMENT;
    }
    queued_event e = { .event = event, .owner = owner, .count = count };
    if (event_copy_arguments(p, &e, arguments, count)) {
        return FLOW_ERR_ARGUMENT;
    }
    apush(p->events, e);
    return FLOW_OK;
}
void execution_program_set_enabled(execution_program *p, bool enabled)
{
    if (paused_system(p)) {
        if (p->enabled == enabled) {
            return;
        }
        execution_program_cancel_system(p);
    }
    assert(p && !p->dispatching && !p->running && !p->system_prepared);
    p->enabled = enabled;
    if (enabled) {
        return;
    }
    for (int i = 0; i < acount(p->instances);) {
        execution_instance *instance = p->instances[i];
        if (instance->query_managed) {
            detach_row(p, i);
            continue;
        }
        i++;
    }
    program_collect_states(p);
}
static ecs_entity query_source(execution_instance *instance, const execution_module_query *q)
{
    uint64_t source = q->source_variable
                        ? instance->query_variables[q->source_variable - 1]
                        : instance->owner;
    return ecs_query_source(
        instance->program->world,
        ecs_entity_from_u64(source),
        q->component,
        q->source_relationship,
        (ecs_trav)q->source_traversal
    );
}
static void query_snapshot(execution_instance *instance)
{
    execution_program *p = instance->program;
    for (int i = 0; i < acount(p->change_queries); i++) {
        const execution_module_query *q = &p->module->plan.queries[p->change_queries[i]];
        ecs_entity source = query_source(instance, q);
        instance->query_snapshot[i] = (query_change){
            .entity = ecs_entity_to_u64(source),
            .version = ecs_component_version(p->world, source, q->component)
        };
    }
}
static int query_matches(execution_instance *, int);
static int query_test(behavior_function_context *context, int term)
{
    execution_instance *instance = context->value_owner;
    if (!instance || term < 0 || term >= instance->program->module->plan.query_count ||
        instance->program->module->plan.queries[term].kind != EXECUTION_QUERY_CHANGED) {
        return FLOW_ERR_STATE;
    }
    return query_matches(instance, term);
}
static int query_row_matches(execution_instance *instance)
{
    execution_program *program = instance->program;
    instance->query_row = (pin_value){ 0 };
    behavior_function_context context = { .program = program,
                                          .module = program->module,
                                          .managed_heap = program->owner_program->managed,
                                          .value_owner = instance,
                                          .query_test = query_test,
                                          .services = program->services,
                                          .now = program->now,
                                          .delta_seconds = program->delta,
                                          .budget = program->budget,
                                          .world = ecs_world_execution(program->world) };
    bool accepted = false;
    const char *error = execution_query_evaluate(
        &context,
        program->module,
        program->module->plan.system_description - 1,
        instance->owner,
        instance->query_variables,
        NULL,
        0,
        &instance->query_row,
        &accepted
    );
    if (error) {
        if (!program->failure.status) {
            program->failure = (execution_program_failure){
                .status = FLOW_ERR_STATE,
                .module_id = program->module->plan.id,
                .owner = instance->owner,
                .kind = BEHAVIOR_FUNCTION_DATA
            };
            snprintf(program->failure.message, sizeof program->failure.message, "%s", error);
        }
        return FLOW_ERR_STATE;
    }
    return accepted;
}
static int query_matches(execution_instance *instance, int index)
{
    execution_program *p = instance->program;
    if (p->module->plan.system_description &&
        index ==
            p->module->plan.descriptions[p->module->plan.system_description - 1].query) {
        return query_row_matches(instance);
    }
    const execution_module_query *q = &p->module->plan.queries[index];
    switch (q->kind) {
        case EXECUTION_QUERY_GENERATOR: return true;
        case EXECUTION_QUERY_HAS: return query_source(instance, q).slot != 0;
        case EXECUTION_QUERY_RELATION: {
            uint64_t source = q->source_variable
                                ? instance->query_variables[q->source_variable - 1]
                                : instance->owner;
            return ecs_pair_target_count(p->world, ecs_entity_from_u64(source), q->component) >
                   0;
        }
        case EXECUTION_QUERY_CHANGED:
            for (int i = 0; i < acount(p->change_queries); i++) {
                if (p->change_queries[i] == index) {
                    query_change now = instance->query_snapshot[i];
                    query_change before = instance->query_changes[i];
                    return now.entity && (!instance->query_started ||
                                          now.entity != before.entity ||
                                          now.version != before.version ||
                                          now.version == UINT64_MAX);
                }
            }
            assert(!"change query is not tracked");
            return FLOW_ERR_STATE;
        case EXECUTION_QUERY_VALUE: {
            query_evaluator *e = program_query_evaluator_take(p, p->module, q->predicate);
            bool resumable = behavior_function_resumable(q->predicate);
            pin_value result = { .type = PIN_BOOL };
            behavior_function_context *context = &e->context;
            /* Even a non-suspending predicate can reuse its VM scratch. */
            context->activation = &e->activation;
            context->query_variables = instance->query_variables;
            context->managed_heap = p->owner_program->managed;
            context->module = p->module;
            context->results = &result;
            context->now = p->now;
            context->delta_seconds = p->delta;
            context->budget = p->budget;
            behavior_function_bind_storage(context, &e->storage);
            if (!behavior_function_bind(
                    q->predicate,
                    context,
                    p->services,
                    instance->owner
                )) {
                program_query_evaluator_free(e);
                return FLOW_ERR_ARGUMENT;
            }
            behavior_function_prepare_host(q->predicate, context);
            bool compiled = p->module->backend != BEHAVIOR_BYTECODE;
            int status = resumable
                           ? behavior_function_run(q->predicate, context, compiled)
                           : behavior_function_run_once(q->predicate, context, compiled);
            if (status < 0 && !p->failure.status) {
                p->failure = (execution_program_failure){
                    .status = status,
                    .kind = context->failure,
                    .module_id = p->module->plan.id,
                    .owner = instance->owner
                };
                snprintf(
                    p->failure.message,
                    sizeof p->failure.message,
                    "%s",
                    context->error_message ? context->error_message : "query predicate failed"
                );
            }
            if (!status) {
                program_query_evaluator_keep(p, e);
            } else {
                program_query_evaluator_free(e);
            }
            return status < 0 ? status : status ? FLOW_ERR_STATE : result.b;
        }
        case EXECUTION_QUERY_ALL:
        case EXECUTION_QUERY_ANY:
        case EXECUTION_QUERY_NOT:
            for (int i = 0; i < q->child_count; i++) {
                int match = query_matches(instance, q->children[i]);
                if (match < 0) {
                    return match;
                }
                if (q->kind == EXECUTION_QUERY_NOT) {
                    return !match;
                }
                if ((q->kind == EXECUTION_QUERY_ALL && !match) ||
                    (q->kind == EXECUTION_QUERY_ANY && match)) {
                    return match;
                }
            }
            return q->kind == EXECUTION_QUERY_ALL;
    }
    return FLOW_ERR_STATE;
}
void program_query_consume(execution_instance *instance)
{
    instance->query_started = true;
    int count = acount(instance->program->change_queries);
    if (count) {
        memcpy(
            instance->query_changes,
            instance->query_snapshot,
            (size_t)count * sizeof *instance->query_changes
        );
    }
}
static int compare_rows(const void *left, const void *right)
{
    const execution_instance *a = *(execution_instance *const *)left;
    const execution_instance *b = *(execution_instance *const *)right;
    if (a->query_managed != b->query_managed) {
        return a->query_managed ? 1 : -1;
    }
    if (!a->query_managed) {
        return (a->index > b->index) - (a->index < b->index);
    }
    return (a->query_order > b->query_order) - (a->query_order < b->query_order);
}
static int refresh_rows(execution_program *p)
{
    p->query_rows = 0;
    p->query_lookup_steps = 0;
    for (int i = 0; i < acount(p->instances); i++) {
        execution_instance *instance = p->instances[i];
        instance->query_seen = !instance->query_managed;
        instance->query_ready = !instance->query_managed;
    }
    if (p->queried && p->enabled) {
        ecs_query query;
        int status = ecs_query_begin_plan(p->query_plan, &query);
        if (status) {
            return FLOW_ERR_STATE;
        }
        query_change *snapshot = acount(p->change_queries)
                                   ? xcalloc((size_t)acount(p->change_queries), sizeof *snapshot)
                                   : NULL;
        query_change *initial = acount(p->change_queries)
                                  ? xcalloc((size_t)acount(p->change_queries), sizeof *initial)
                                  : NULL;
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            execution_instance candidate = { .program = p,
                                             .owner = ecs_entity_to_u64(entity),
                                             .query_snapshot = snapshot,
                                             .query_changes = initial };
            for (int i = 0; i < p->module->plan.query_variable_count; i++) {
                candidate.query_variables[i] = ecs_entity_to_u64(
                    ecs_query_var(&query, i + 1)
                );
            }
            p->query_rows++;
            execution_instance
                *instance = program_instance_row(p, candidate.owner, candidate.query_variables);
            if (instance && p->module->plan.system_description &&
                !execution_query_membership_matches(
                    p->module,
                    p->module->plan.query,
                    ecs_world_execution(p->world),
                    candidate.owner,
                    candidate.query_variables
                )) {
                detach_row(p, instance->index);
                instance = NULL;
            }
            bool added = !instance;
            if (added) {
                instance = &candidate;
            }
            instance->query_seen = true;
            query_snapshot(instance);
            int match = query_matches(instance, p->module->plan.query);
            if (added && match > 0) {
                instance = attach_row(p, candidate.owner, candidate.query_variables);
                if (!instance) {
                    ecs_query_end(&query);
                    free(snapshot);
                    free(initial);
                    return FLOW_ERR_ARGUMENT;
                }
                instance->query_managed = true;
                instance->query_new = true;
                instance->query_seen = true;
                instance->query_row = candidate.query_row;
                if (acount(p->change_queries)) {
                    memcpy(
                        instance->query_snapshot,
                        snapshot,
                        (size_t)acount(p->change_queries) * sizeof *snapshot
                    );
                }
            }
            instance->query_ready = match > 0;
            instance->query_order = p->query_rows;
            if (match < 0) {
                ecs_query_end(&query);
                free(snapshot);
                free(initial);
                return match;
            }
        }
        free(snapshot);
        free(initial);
        ecs_query_end(&query);
    }
    for (int i = 0; i < acount(p->instances);) {
        execution_instance *instance = p->instances[i];
        bool alive = !instance->owner || !p->world ||
                     ecs_entity_is_alive(p->world, ecs_entity_from_u64(instance->owner));
        if (!instance->query_seen || !alive) {
            detach_row(p, i);
            continue;
        }
        i++;
    }
    if (acount(p->instances) > 1) {
        qsort(p->instances, (size_t)acount(p->instances), sizeof *p->instances, compare_rows);
        for (int i = 0; i < acount(p->instances); i++) {
            p->instances[i]->index = i;
        }
    }
    uint64_t begin = execution_module_event(p->module, "Begin");
    for (int i = 0; i < acount(p->instances); i++) {
        execution_instance *instance = p->instances[i];
        if (instance->query_new && instance->query_ready) {
            instance->query_new = false;
            if (!begin) {
                continue;
            }
            int status = execution_instance_dispatch(instance, begin, NULL, 0, p->now, p->delta, p->budget);
            if (status < 0) {
                return status;
            }
        }
    }
    return FLOW_OK;
}
static bool streaming(const execution_program *p)
{
    return p->query_streaming && !p->inspector && !acount(p->instances);
}
static int prepare_stream(execution_program *p)
{
    asetlen(p->query_admissions, 0);
    asetlen(p->query_values, 0);
    p->query_rows = 0;
    if (!p->enabled || (!p->module->plan.system_description &&
                        !query_dynamic(p->module, p->module->plan.query))) {
        return FLOW_OK;
    }
    ecs_query query;
    if (ecs_query_begin_plan(p->query_plan, &query)) {
        return FLOW_ERR_STATE;
    }
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        execution_instance row = { .program = p, .owner = ecs_entity_to_u64(entity) };
        for (int i = 0; i < p->module->plan.query_variable_count; i++) {
            row.query_variables[i] = ecs_entity_to_u64(ecs_query_var(&query, i + 1));
        }
        int match = query_matches(&row, p->module->plan.query);
        if (p->module->plan.system_description) {
            apush(p->query_values, row.query_row);
        }
        if (match < 0) {
            ecs_query_end(&query);
            return match;
        }
        unsigned bit = (unsigned)(p->query_rows % 64);
        if (!bit) {
            apush(p->query_admissions, UINT64_C(0));
        }
        if (match) {
            p->query_admissions[acount(p->query_admissions) - 1] |= UINT64_C(1) << bit;
        }
        p->query_rows++;
    }
    ecs_query_end(&query);
    return FLOW_OK;
}
static int prepare_system_rows(execution_program *p)
{
    asetlen(p->system_values, 0);
    aclear(p->system_rows);
    p->query_rows = 0;
    p->query_lookup_steps = 0;
    if (!p->enabled) {
        return FLOW_OK;
    }
    ecs_query query;
    if (ecs_query_begin_plan(p->query_plan, &query)) {
        return FLOW_ERR_STATE;
    }
    int width = p->module->plan.query_variable_count + 1;
    uint64_t index = 0;
    ecs_entity entity;
    int status = FLOW_OK;
    while (ecs_query_next(&query, &entity)) {
        uint64_t row = index++;
        p->query_rows++;
        if (acount(p->query_admissions) &&
            !(p->query_admissions[row / 64] & (UINT64_C(1) << (row % 64)))) {
            continue;
        }
        if (!array_room(
                acount(p->system_rows),
                acap(p->system_rows),
                width,
                sizeof *p->system_rows
            )) {
            status = FLOW_ERR_STATE;
            break;
        }
        if (acount(p->query_values)) {
            apush(p->system_values, p->query_values[row]);
        }
        int first = acount(p->system_rows);
        asetlen(p->system_rows, first + width);
        p->system_rows[first] = ecs_entity_to_u64(entity);
        for (int i = 1; i < width; i++) {
            p->system_rows[first + i] = ecs_entity_to_u64(ecs_query_var(&query, i));
        }
    }
    ecs_query_end(&query);
    return status;
}
static int run_stream_row(execution_program *p, execution_instance *row)
{
    execution_activation *a = program_activation_new(
        row,
        p->module->plan.functions[p->stream_system].body,
        NULL,
        0,
        p->now,
        p->delta,
        p->budget
    );
    if (!a) {
        return FLOW_ERR_ARGUMENT;
    }
    int status = program_activation_run(a, p->now, p->delta, p->budget);
    assert(status <= 0 && !acount(a->requests));
    program_activation_free(a);
    return status;
}
static int run_system_rows(execution_program *p)
{
    int width = p->module->plan.query_variable_count + 1;
    execution_instance row = { .program = p, .query_managed = true };
    p->dispatching = true;
    int status = FLOW_OK;
    for (int at = 0; at < acount(p->system_rows); at += width) {
        row.owner = p->system_rows[at];
        if (acount(p->system_values)) {
            row.query_row = p->system_values[at / width];
        }
        for (int i = 1; i < width; i++) {
            row.query_variables[i - 1] = p->system_rows[at + i];
        }
        status = run_stream_row(p, &row);
        if (status < 0) {
            break;
        }
    }
    p->dispatching = false;
    return status;
}
size_t program_row_worker_bytes(const execution_program *p)
{
    size_t bytes = p->row_workers ? sizeof(dyna_hdr) + (size_t)acap(p->row_workers) *
                                                           sizeof *p->row_workers
                                  : 0;
    for (int i = 0; i < acount(p->row_workers); i++) {
        const execution_program *worker = p->row_workers[i].program;
        bytes += sizeof *worker + program_activation_cache_bytes(worker) +
                 reduction_capacity(worker) + sizeof *worker->managed +
                 arena_storage_bytes(&worker->managed->storage);
    }
    return bytes;
}
static void row_workers_trim(execution_program *p, int count)
{
    while (acount(p->row_workers) > count) {
        execution_program *worker = p->row_workers[acount(p->row_workers) - 1].program;
        /* Worker programs borrow their module, names, services and world. */
        assert(!worker->running && !worker->dispatching && !worker->query_evaluators);
        program_activation_cache_free(worker);
        behavior_managed_heap_free(worker->managed);
        free(worker->managed);
        afree(worker->reductions);
        afree(worker->reduction_table);
        free(worker);
        asetlen(p->row_workers, acount(p->row_workers) - 1);
    }
    if (!count) {
        afree(p->row_workers);
    }
}
static void row_workers_idle(execution_program *p)
{
    for (int i = acount(p->row_workers) - 1;
         i >= 0 && program_row_worker_bytes(p) > ROW_WORKER_BYTES;
         i--) {
        execution_program *worker = p->row_workers[i].program;
        program_activation_cache_free(worker);
        afree(worker->reductions);
        afree(worker->reduction_table);
    }
    while (program_row_worker_bytes(p) > ROW_WORKER_BYTES) {
        row_workers_trim(p, acount(p->row_workers) - 1);
    }
}
static void run_row_chunks(int start, int end, uint32_t worker, void *context)
{
    execution_program *parent = context;
    for (int chunk = start; chunk < end; chunk++) {
        execution_row_worker *work = &parent->row_workers[chunk];
        execution_program *p = work->program;
        execution_instance row = { .program = p, .query_managed = true };
        work->worker = worker;
        p->dispatching = true;
        for (int at = work->first; at < work->end; at++) {
            const uint64_t *values = work->rows + (size_t)at * work->width;
            row.owner = values[0];
            if (acount(parent->system_values)) {
                row.query_row = parent->system_values[at];
            }
            for (int i = 1; i < work->width; i++) {
                row.query_variables[i - 1] = values[i];
            }
            work->status = run_stream_row(p, &row);
            if (work->status < 0) {
                break;
            }
        }
        p->dispatching = false;
    }
}
static int merge_row_chunks(execution_program *p)
{
    int status = FLOW_OK;
    uint32_t workers = 0;
    uint64_t contributions = 0;
    size_t peak = 0;
    for (int i = 0; i < acount(p->row_workers); i++) {
        execution_row_worker *work = &p->row_workers[i];
        execution_program *worker = work->program;
        workers |= UINT32_C(1) << work->worker;
        contributions += worker->reduction_contributions;
        peak += worker->reduction_peak_bytes;
    }
    for (; workers; workers &= workers - 1) {
        p->row_parallel_workers++;
    }
    for (int i = 0; status == FLOW_OK && i < acount(p->row_workers); i++) {
        const execution_row_worker *work = &p->row_workers[i];
        const execution_program *worker = work->program;
        for (int j = 0; j < acount(worker->reductions); j++) {
            status = append_reduction(p, worker->reductions[j]);
            if (status < 0) {
                break;
            }
        }
        if (status == FLOW_OK && work->status < 0) {
            status = work->status;
            if (!p->failure.status) {
                p->failure = worker->failure;
            }
        }
    }
    p->reduction_contributions = contributions;
    p->reduction_peak_bytes += peak;
    for (int i = 0; i < acount(p->row_workers); i++) {
        execution_program_discard_reductions(p->row_workers[i].program);
        p->row_workers[i].rows = NULL;
    }
    row_workers_idle(p);
    return status;
}
int execution_program_run_system_parallel(execution_program *p, const task_system *tasks)
{
    assert(p && p->system_prepared && !p->system_ran && !p->dispatching && !p->running);
    int budget = tasks ? tasks->workers : 1;
    assert(budget > 0 && budget <= TASK_WORKER_MAX);
    int width = p->module->plan.query_variable_count + 1;
    int rows = acount(p->system_rows) / width;
    int chunks = rows / ROW_CHUNK_MIN;
    if (chunks > budget) {
        chunks = budget;
    }
    execution_effect_summary effects = p->module->effects;
    if (chunks < 2 || !streaming(p) || p->observer || !effects.reduce_count ||
        !effects.parallel_safe || effects.write_count) {
        row_workers_trim(p, budget > 1 ? budget : 0);
        return execution_program_run_system(p);
    }
    assert(tasks->enqueue && tasks->finish && p->reducing && !acount(p->reductions));
    row_workers_trim(p, chunks);
    while (acount(p->row_workers) < chunks) {
        execution_program *worker = xcalloc(1, sizeof *worker);
        worker->managed = xcalloc(1, sizeof *worker->managed);
        behavior_managed_heap_init(worker->managed);
        apush(p->row_workers, ((execution_row_worker){ .program = worker }));
    }
    for (int i = 0; i < chunks; i++) {
        execution_program *worker = p->row_workers[i].program;
        worker->module = p->module;
        worker->stream_system = p->stream_system;
        worker->world = p->world;
        worker->services = p->services;
        worker->name = p->name;
        worker->owner_program = p->owner_program;
        worker->now = p->now;
        worker->delta = p->delta;
        worker->budget = p->budget;
        worker->enabled = p->enabled;
        worker->failure = (execution_program_failure){ 0 };
        begin_reductions(worker);
        p->row_workers[i] = (execution_row_worker){
            .program = worker,
            .rows = p->system_rows,
            .first = (int)((int64_t)rows * i / chunks),
            .end = (int)((int64_t)rows * (i + 1) / chunks),
            .width = width
        };
    }
    p->system_ran = true;
    p->row_parallel_jobs = chunks;
    p->dispatching = true;
    int locked = ecs_world_iteration_begin(p->world);
    assert(locked == ECS_OK);
    (void)locked;
    void *ticket = tasks->enqueue(run_row_chunks, chunks, 1, p, tasks->user);
    if (ticket) {
        tasks->finish(ticket, tasks->user);
    }
    ecs_world_iteration_end(p->world);
    p->dispatching = false;
    p->system_status = merge_row_chunks(p);
    return p->system_status;
}
static int stream_rows(execution_program *p)
{
    p->query_rows = 0;
    p->query_lookup_steps = 0;
    if (!p->enabled) {
        return FLOW_OK;
    }
    ecs_query query;
    if (ecs_query_begin_plan(p->query_plan, &query)) {
        return FLOW_ERR_STATE;
    }
    execution_instance row = { .program = p, .query_managed = true };
    ecs_entity entity;
    int status = FLOW_OK;
    p->dispatching = true;
    while (ecs_query_next(&query, &entity)) {
        row.owner = ecs_entity_to_u64(entity);
        for (int i = 0; i < p->module->plan.query_variable_count; i++) {
            row.query_variables[i] = ecs_entity_to_u64(ecs_query_var(&query, i + 1));
        }
        uint64_t index = p->query_rows++;
        if (acount(p->query_admissions) &&
            !(p->query_admissions[index / 64] & (UINT64_C(1) << (index % 64)))) {
            continue;
        }
        if (acount(p->query_values)) {
            row.query_row = p->query_values[index];
        }
        status = run_stream_row(p, &row);
        if (status < 0) {
            break;
        }
    }
    p->dispatching = false;
    ecs_query_end(&query);
    return status;
}
static int dispatch_events(execution_program *p)
{
    queued_event *events = p->events;
    p->events = NULL;
    queued_event_batch batch = { .previous = p->dispatch_events, .events = events };
    p->dispatch_events = &batch;
    for (int i = 0; i < acount(events); i++) {
        queued_event *e = &events[i];
        int failure = FLOW_OK;
        for (int j = 0; j < acount(p->instances); j++) {
            execution_instance *instance = p->instances[j];
            int status = dispatch_to(
                instance,
                e->event,
                e->owner,
                e->arguments,
                e->count,
                p->now,
                p->delta,
                p->budget
            );
            if (status == FLOW_ERR_UNKNOWN_EVENT) {
                continue;
            }
            if (status < 0) {
                failure = status;
                break;
            }
        }
        event_free(e);
        batch.first = i + 1;
        if (failure < 0) {
            queued_event *later = p->events;
            p->events = NULL;
            for (int j = i + 1; j < acount(events); j++) {
                apush(p->events, events[j]);
            }
            for (int j = 0; j < acount(later); j++) {
                apush(p->events, later[j]);
            }
            afree(later);
            p->dispatch_events = batch.previous;
            afree(events);
            return failure;
        }
    }
    p->dispatch_events = batch.previous;
    afree(events);
    return FLOW_OK;
}
static int program_run(execution_program *p, double now, double delta, unsigned budget, int phase)
{
    if (p->system_prepared && p->module->effects.reduce_count &&
        p->system_status == FLOW_PAUSED_RESULT) {
        return FLOW_PAUSED_RESULT;
    }
    assert(!p->dispatching && !p->system_prepared);
    bool prepare_update = phase == -2;
    bool update = phase > 0 || prepare_update;
    p->updates += update;
    p->now = now;
    p->delta = delta;
    p->budget = budget ? budget : 10000;
    if (streaming(p)) {
        /* These modules have no handlers, bindings, state, or suspension. */
        for (int i = 0; i < acount(p->events); i++) {
            event_free(&p->events[i]);
        }
        asetlen(p->events, 0);
        int status = prepare_stream(p);
        if (status < 0 || !update || prepare_update) {
            if (status < 0 || (phase != -1 && !prepare_update)) {
                afree(p->query_admissions);
            }
            if (status < 0 || (phase != -1 && !prepare_update)) {
                query_values_clear(p);
            }
            program_collect_states(p);
            return status;
        }
        status = stream_rows(p);
        afree(p->query_admissions);
        query_values_clear(p);
        program_collect_states(p);
        return status;
    }
    int refreshed = refresh_rows(p);
    if (refreshed < 0) {
        program_collect_states(p);
        return refreshed;
    }
    p->dispatching = true;
    int observed = program_observers_maintain(p, now, delta, p->budget);
    if (observed < 0) {
        p->dispatching = false;
        return observed;
    }
    for (int i = 0; i < acount(p->instances); i++) {
        execution_instance *instance = p->instances[i];
        if (update) {
            instance->time += delta;
        }
        instance->last_time = now;
        program_tasks_tick(instance, now);
        for (int j = 0; j < acount(instance->activations);) {
            execution_activation *a = instance->activations[j];
            if (!a->finished && a->context.wake_time > now) {
                j++;
                continue;
            }
            int status = program_activation_run(a, now, delta, p->budget);
            if (status == FLOW_SUSPENDED_RESULT ||
                status == FLOW_PAUSED_RESULT || acount(a->requests)) {
                j++;
                continue;
            }
            program_activation_free(a);
            adel(instance->activations, j);
            if (status < 0) {
                p->dispatching = false;
                return status;
            }
        }
    }
    for (int i = 0;
         update && !prepare_update && p->enabled && i < acount(p->instances);
         i++) {
        execution_instance *instance = p->instances[i];
        for (int f = 0; f < p->module->plan.function_count; f++) {
            const execution_module_function *function = &p->module->plan.functions[f];
            if (function->kind != EXECUTION_FUNCTION_SYSTEM) {
                continue;
            }
            bool active = program_instance_busy(instance);
            if (active || !instance->query_ready) {
                continue;
            }
            execution_activation *
                a = program_activation_new(instance, function->body, NULL, 0, now, delta, p->budget);
            if (!a) {
                p->dispatching = false;
                return FLOW_ERR_ARGUMENT;
            }
            program_query_consume(instance);
            int status = program_activation_run(a, now, delta, p->budget);
            if (!a->finished || acount(a->requests)) {
                apush(instance->activations, a);
            } else {
                program_activation_free(a);
            }
            if (status < 0) {
                p->dispatching = false;
                return status;
            }
        }
    }
    if (prepare_update) {
        /* The prepared systems publish before this update's queued handlers. */
        p->dispatching = false;
        return FLOW_OK;
    }
    int delivered = dispatch_events(p);
    if (delivered < 0) {
        p->dispatching = false;
        return delivered;
    }
    for (int i = 0; phase == 2 && i < acount(p->instances); i++) {
        for (int j = 0; j < acount(p->binding_order); j++) {
            int status = program_binding_run(p->instances[i], p->binding_order[j], now, delta, p->budget);
            if (status < 0) {
                p->dispatching = false;
                return status;
            }
        }
    }
    observed = program_observers_maintain(p, now, delta, p->budget);
    p->dispatching = false;
    program_collect_states(p);
    return observed;
}
int execution_program_step(execution_program *p, double now, double delta, unsigned budget)
{
    if (p->module->effects.reduce_count) {
        int status = execution_program_update(p, now, delta, budget);
        return status ? status : execution_program_bindings(p, now, delta, budget);
    }
    begin_reductions(p);
    int status = program_run(p, now, delta, budget, 2);
    status = finish_reductions(p, status);
    program_apply_services(p);
    return status;
}
int execution_program_pump(execution_program *p, double now, double delta, unsigned budget)
{
    if (p->system_prepared && p->module->effects.reduce_count) {
        return FLOW_PAUSED_RESULT;
    }
    int status = program_run(p, now, delta, budget, 0);
    program_apply_services(p);
    return status;
}
static int prepare_system(execution_program *, double, double, unsigned, bool);
int execution_program_update(execution_program *p, double now, double delta, unsigned budget)
{
    if (p->module->effects.reduce_count) {
        int status = prepare_system(p, now, delta, budget, true);
        if (status < 0) {
            return status;
        }
        execution_program_run_system(p);
        return execution_program_finish_system(p);
    }
    begin_reductions(p);
    int status = program_run(p, now, delta, budget, 1);
    status = finish_reductions(p, status);
    program_apply_services(p);
    return status;
}
static int prepare_system(execution_program *p, double now, double delta, unsigned budget, bool update)
{
    if (p->system_prepared && p->module->effects.reduce_count) {
        assert(
            p->system_status == FLOW_PAUSED_RESULT && !p->system_ran &&
            !p->dispatching && !p->running && p->reducing
        );
        /* A debug pause retains the original query admission and clock. */
        return FLOW_OK;
    }
    assert(p && !p->system_prepared && !p->running && !p->dispatching);
    begin_reductions(p);
    p->updates += !update;
    int status = program_run(p, now, delta, budget, update ? -2 : -1);
    program_apply_services(p);
    if (status < 0) {
        execution_program_discard_reductions(p);
        return status;
    }
    p->system_prepared = true;
    p->system_update = update;
    p->system_ran = false;
    p->system_status = FLOW_OK;
    if (streaming(p) && p->module->effects.reduce_count) {
        status = prepare_system_rows(p);
        if (status < 0) {
            p->system_status = status;
            execution_program_finish_system(p);
        }
        return status;
    }
    for (int i = 0; i < acount(p->instances); i++) {
        execution_instance *instance = p->instances[i];
        if (!update) {
            instance->time += delta;
            instance->last_time = now;
        }
        if (!p->enabled || !instance->query_ready || program_instance_busy(instance)) {
            continue;
        }
        for (int f = 0; f < p->module->plan.function_count; f++) {
            const execution_module_function *function = &p->module->plan.functions[f];
            if (function->kind != EXECUTION_FUNCTION_SYSTEM) {
                continue;
            }
            execution_activation *
                a = program_activation_new(instance, function->body, NULL, 0, now, delta, p->budget);
            if (!a) {
                p->system_status = FLOW_ERR_ARGUMENT;
                execution_program_finish_system(p);
                return FLOW_ERR_ARGUMENT;
            }
            apush(p->system_activations, a);
        }
    }
    return FLOW_OK;
}
int execution_program_prepare_system(execution_program *p, double now, double delta, unsigned budget)
{
    return prepare_system(p, now, delta, budget, false);
}
int execution_program_run_system(execution_program *p)
{
    assert(p && p->system_prepared && !p->system_ran && !p->dispatching && !p->running);
    p->system_ran = true;
    p->system_status = FLOW_OK;
    if (streaming(p)) {
        p->system_status = p->module->effects.reduce_count ? run_system_rows(p)
                                                           : stream_rows(p);
        return p->system_status;
    }
    p->dispatching = true;
    for (int i = 0; i < acount(p->system_activations); i++) {
        execution_activation *a = p->system_activations[i];
        if (!a) {
            continue;
        }
        if (program_instance_busy(a->instance)) {
            program_activation_free(a);
            p->system_activations[i] = NULL;
            continue;
        }
        program_query_consume(a->instance);
        int status = program_activation_run(a, p->now, p->delta, p->budget);
        if (status == FLOW_PAUSED_RESULT && p->module->effects.reduce_count) {
            p->system_status = status;
            break;
        }
        if (p->module->effects.reduce_count && (!a->finished || acount(a->requests))) {
            program_activation_cancel(a);
            status = FLOW_ERR_STATE;
        }
        if (!a->finished || acount(a->requests)) {
            apush(a->instance->activations, a);
        } else {
            program_activation_free(a);
        }
        p->system_activations[i] = NULL;
        if (status < 0) {
            p->system_status = status;
            break;
        }
    }
    p->dispatching = false;
    return p->system_status;
}
int execution_program_finish_system(execution_program *p)
{
    assert(p && p->system_prepared && !p->dispatching && !p->running);
    if (p->module->effects.reduce_count && p->reducing &&
        p->system_status == FLOW_PAUSED_RESULT) {
        p->system_ran = false;
        return FLOW_PAUSED_RESULT;
    }
    for (int i = 0; i < acount(p->system_activations); i++) {
        execution_activation *a = p->system_activations[i];
        if (a) {
            program_activation_free(a);
        }
    }
    asetlen(p->system_activations, 0);
    p->system_prepared = false;
    bool update = p->system_update;
    p->system_update = false;
    afree(p->query_admissions);
    aclear(p->system_rows);
    query_values_clear(p);
    int status = finish_reductions(p, p->system_status);
    if (status >= 0 && update) {
        p->dispatching = true;
        status = dispatch_events(p);
        p->dispatching = false;
    }
    if (status >= 0) {
        status = program_observers_maintain(p, p->now, p->delta, p->budget);
    }
    program_apply_services(p);
    program_collect_states(p);
    return status;
}
void execution_program_cancel_system(execution_program *p)
{
    assert(p && !p->dispatching && !p->running);
    if (!p->system_prepared) {
        return;
    }
    execution_program_discard_reductions(p);
    for (int i = 0; i < acount(p->system_activations); i++) {
        if (p->system_activations[i]) {
            program_activation_cancel(p->system_activations[i]);
        }
    }
    p->system_status = FLOW_ERR_STATE;
    execution_program_finish_system(p);
}
int execution_program_bindings(execution_program *p, double now, double delta, unsigned budget)
{
    if (p->system_prepared && p->module->effects.reduce_count) {
        return FLOW_PAUSED_RESULT;
    }
    assert(!p->dispatching && !p->running && !p->system_prepared);
    p->now = now;
    p->delta = delta;
    p->budget = budget ? budget : 10000;
    p->dispatching = true;
    int status = FLOW_OK;
    for (int i = 0; i < acount(p->instances) && !status; i++) {
        for (int j = 0; j < acount(p->binding_order); j++) {
            status = program_binding_run(p->instances[i], p->binding_order[j], now, delta, p->budget);
            if (status) {
                break;
            }
        }
    }
    if (!status) {
        status = program_observers_maintain(p, now, delta, p->budget);
    }
    p->dispatching = false;
    program_apply_services(p);
    program_collect_states(p);
    return status;
}
/* Index of before's state with state's id, or -1 for a new state. */
static int prior_state(const execution_module *before, const execution_module_state *state)
{
    for (int i = 0; i < before->plan.state_count; i++) {
        if (before->plan.states[i].id == state->id) {
            return i;
        }
    }
    return -1;
}
/* A kept value of prior as state keeps it: unchanged, or converted when its
 * type changed. False when no conversion exists, and state keeps its
 * initializer. */
static bool kept_value(
    arena *scratch,
    const execution_module_state *prior,
    const execution_module_state *state,
    pin_value value,
    pin_value *out
)
{
    if (prior->shared != state->shared) {
        return false;
    }
    if (program_same_type(prior->initial, state->initial)) {
        *out = value;
        return true;
    }
    return program_migrate_value(scratch, value, state->initial, out);
}
static bool record_current(const execution_module *module, const pin_record *record)
{
    for (int i = 0; i < module->plan.type_count; i++) {
        const execution_type_requirement *type = &module->plan.types[i];
        if (type->id == record->id) {
            return pin_record_same(type->record, record);
        }
    }
    return false;
}
typedef struct
{
    pin_value value;
    bool pointee;
} layout_value;
/* Records already found current and cells already visited, shared by every
 * value of one scope. */
typedef struct
{
    const execution_module *module;
    layout_value *pending;
    const pin_record **current;
    program_pointer_set cells;
} layout_check;
static bool pointees_reach_current(layout_check *c)
{
    while (acount(c->pending)) {
        layout_value item = apop(c->pending);
        pin_value v = item.value;
        const pin_array *array = v.container != PIN_CONTAINER_SINGLE &&
                                         v.container != PIN_CONTAINER_MAP
                                   ? v.ptr
                                   : NULL;
        const pin_record *record = array ? array->record : v.record;
        if (item.pointee && record) {
            bool known = false;
            for (int i = 0; i < acount(c->current) && !known; i++) {
                known = c->current[i] == record;
            }
            if (!known && !record_current(c->module, record)) {
                return false;
            }
            if (!known) {
                apush(c->current, record);
            }
        }
        if (v.container == PIN_CONTAINER_MAP) {
            const pin_map *map = v.ptr;
            for (int i = 0; map && i < map->count; i++) {
                apush(c->pending, ((layout_value){ map->entries[i].key, item.pointee }));
                apush(c->pending, ((layout_value){ map->entries[i].value, item.pointee }));
            }
        } else if (v.container != PIN_CONTAINER_SINGLE) {
            /* Packed scalar items reach nothing; boxed items may be nested
             * containers. */
            bool reaches = array && (!array->stride || array->element_type == PIN_STRUCT ||
                                     array->element_type == PIN_UNION ||
                                     array->element_type == PIN_POINTER);
            for (int i = 0; reaches && i < array->count; i++) {
                apush(c->pending, ((layout_value){ pin_array_get(array, i), item.pointee }));
            }
        } else if (v.type == PIN_POINTER && v.ptr) {
            const pin_pointer *cell = v.ptr;
            while (cell->base) {
                cell = cell->base;
            }
            if (program_pointer_first_visit(&c->cells, cell)) {
                apush(c->pending, ((layout_value){ cell->value, true }));
            }
        } else if (v.type == PIN_UNION && v.ptr) {
            const pin_union *sum = v.ptr;
            for (int i = 0; i < sum->count; i++) {
                apush(c->pending, ((layout_value){ sum->values[i], item.pointee }));
            }
        } else if (v.type == PIN_STRUCT && v.ptr) {
            const type_desc *layout = pin_value_type(&v);
            for (int i = 0; layout && i < layout->field_count; i++) {
                const type_field *field = &layout->fields[i];
                pin_value member;
                if ((field->type == PIN_STRUCT || field->type == PIN_POINTER ||
                     field->type == PIN_UNION) &&
                    !pin_read_field(NULL, v.ptr, field, &member)) {
                    apush(c->pending, ((layout_value){ member, item.pointee }));
                }
            }
        }
    }
    return true;
}
/* Whether every authored record that value reaches through a managed pointer
 * has the layout module declares. A kept cell keeps the layout it was made
 * with. Cells stay visited only after a check that holds, since every record
 * they reach was then found current. */
static bool pointees_current(layout_check *c, pin_value value)
{
    apush(c->pending, ((layout_value){ value, false }));
    if (!pointees_reach_current(c)) {
        asetlen(c->pending, 0);
        free(c->cells.slots);
        c->cells = (program_pointer_set){ 0 };
        return false;
    }
    return true;
}
/* Keeps each shared value next can keep. A reload always applies: a value
 * that cannot convert, or whose state became per instance, keeps next's
 * initializer. preserve_state keeps instance values per row. */
static void preserve_shared(execution_program *next, const execution_program *before)
{
    for (int i = 0; i < next->module->plan.state_count; i++) {
        const execution_module_state *state = &next->module->plan.states[i];
        int old = prior_state(before->module, state);
        if (old < 0 || !registration_state(before, old) ||
            !registration_state(next, i)) {
            continue;
        }
        arena scratch = { 0 };
        pin_value value;
        if (kept_value(
                &scratch,
                &before->module->plan.states[old],
                state,
                before->shared[old].value,
                &value
            )) {
            int status = state_assign((execution_program *)before, &next->shared[i], &value);
            assert(!status);
            (void)status;
        }
        arena_free(&scratch);
    }
}
static void preserve_state(
    execution_program *next,
    const execution_program *before,
    uint64_t owner,
    const uint64_t *variables
)
{
    execution_instance *old = program_instance_row((execution_program *)before, owner, variables);
    execution_instance *instance = program_instance_row(next, owner, variables);
    assert(instance);
    if (!old) {
        return;
    }
    instance->time = old->time;
    for (int i = 0; i < next->module->plan.action_count; i++) {
        const execution_module_action *action = &next->module->plan.actions[i];
        if (action->state_kind == NATIVE_STATE_NONE ||
            action->state_kind == NATIVE_STATE_RETRIGGER) {
            continue;
        }
        for (int j = 0; j < before->module->plan.action_count; j++) {
            const execution_module_action *prior = &before->module->plan.actions[j];
            if (prior->id == action->id && prior->state_kind == action->state_kind &&
                prior->outcome_count == action->outcome_count) {
                int status = pin_value_snapshot_to_arena(
                    &instance->selections,
                    &instance->node_states[i],
                    &old->node_states[j]
                );
                assert(!status);
                (void)status;
                break;
            }
        }
    }
    for (int i = 0; i < next->module->plan.state_count; i++) {
        const execution_module_state *state = &next->module->plan.states[i];
        int index = prior_state(before->module, state);
        if (index < 0 || registration_state(before, index) ||
            registration_state(next, i)) {
            continue;
        }
        arena scratch = { 0 };
        pin_value value;
        /* A staged candidate deliberately retains the original family's
         * identities; its stable root snapshots pin them until adoption. */
        if (kept_value(
                &scratch,
                &before->module->plan.states[index],
                state,
                old->states[index].value,
                &value
            )) {
            int status = state_assign((execution_program *)before, instance_storage(instance, i), &value);
            assert(!status);
            (void)status;
        }
        arena_free(&scratch);
    }
    sync_state(instance);
}
static void reset_state(execution_program *scope, state_value *slot, const execution_module_state *state)
{
    int status = state_assign(scope, slot, &state->initial);
    assert(!status);
    (void)status;
}
/* Resets each value a candidate scope keeps that reaches a record layout the
 * replacement changed: shared and instance state to its initializer, a node
 * state to a fresh one. Kept cells are shared with the running program, which
 * may change them until commit. */
static void reset_stale(execution_program *scope)
{
    const execution_module *module = scope->module;
    layout_check c = { .module = module };
    for (int i = 0; i < module->plan.state_count; i++) {
        if (!pointees_current(&c, scope->shared[i].value)) {
            reset_state(scope, &scope->shared[i], &module->plan.states[i]);
        }
    }
    for (int i = 0; i < acount(scope->instances); i++) {
        execution_instance *instance = scope->instances[i];
        for (int j = 0; j < module->plan.state_count; j++) {
            if (!registration_state(scope, j) &&
                !pointees_current(&c, instance->states[j].value)) {
                reset_state(scope, &instance->states[j], &module->plan.states[j]);
            }
        }
        for (int j = 0; j < acount(instance->node_states); j++) {
            if (!pointees_current(&c, instance->node_states[j])) {
                instance->node_states[j] = (pin_value){ 0 };
            }
        }
        sync_state(instance);
    }
    afree(c.pending);
    afree(c.current);
    free(c.cells.slots);
}
static void reset_stale_candidate(execution_program *candidate)
{
    reset_stale(candidate);
    for (int i = 0; i < acount(candidate->imports); i++) {
        reset_stale(candidate->imports[i]);
    }
}
static bool query_shape_equal(const execution_module *a, const execution_module *b)
{
    if (a->plan.query_variable_count != b->plan.query_variable_count) {
        return false;
    }
    int next = 0;
    for (int i = 0; i < a->plan.query_count; i++) {
        const execution_module_query *x = &a->plan.queries[i];
        if (x->kind != EXECUTION_QUERY_GENERATOR) {
            continue;
        }
        while (next < b->plan.query_count &&
               b->plan.queries[next].kind != EXECUTION_QUERY_GENERATOR) {
            next++;
        }
        if (next == b->plan.query_count) {
            return false;
        }
        const execution_module_query *y = &b->plan.queries[next++];
        if (x->component != y->component || x->source_variable != y->source_variable ||
            x->target_variable != y->target_variable) {
            return false;
        }
    }
    return true;
}
static void preserve_changes(execution_instance *next, const execution_instance *old)
{
    if (!old->query_started) {
        return;
    }
    const execution_program *a = next->program, *b = old->program;
    for (int i = 0; i < acount(a->change_queries); i++) {
        const execution_module_query *x = &a->module->plan.queries[a->change_queries[i]];
        bool found = false;
        for (int j = 0; j < acount(b->change_queries); j++) {
            const execution_module_query *y = &b->module->plan.queries[b->change_queries[j]];
            if (x->component == y->component && x->source_variable == y->source_variable &&
                x->source_relationship == y->source_relationship &&
                x->source_traversal == y->source_traversal) {
                next->query_changes[i] = old->query_changes[j];
                found = true;
                break;
            }
        }
        if (!found) {
            return;
        }
    }
    next->query_started = true;
}
/* Roots each pointer the candidate preserved, rather than the state value
 * holding it: a root must not carry inline record fields (see
 * behavior_managed_heap_root), while ordinary state may. */
static void replacement_root(execution_program *candidate, pin_value root)
{
    pin_value *pending = NULL;
    apush(pending, root);
    while (acount(pending)) {
        pin_value value = apop(pending);
        if (value.container == PIN_CONTAINER_SINGLE &&
            (value.type == PIN_POINTER || value.type == PIN_QUERY)) {
            if (value.ptr) {
                apush(candidate->replacement_roots, value);
            }
        } else if (value.container == PIN_CONTAINER_MAP) {
            const pin_map *map = value.ptr;
            for (int i = 0; map && i < map->count; i++) {
                apush(pending, map->entries[i].key);
                apush(pending, map->entries[i].value);
            }
        } else if (value.container != PIN_CONTAINER_SINGLE) {
            const pin_array *array = value.ptr;
            for (int i = 0; array && i < array->count; i++) {
                apush(pending, pin_array_get(array, i));
            }
        } else if (value.type == PIN_UNION) {
            const pin_union *sum = value.ptr;
            for (int i = 0; sum && i < sum->count; i++) {
                apush(pending, sum->values[i]);
            }
        } else if (value.type == PIN_STRUCT && value.ptr) {
            const type_desc *layout = pin_value_type(&value);
            for (int i = 0; layout && i < layout->field_count; i++) {
                const type_field *field = &layout->fields[i];
                if (field->type != PIN_STRUCT && field->type != PIN_POINTER &&
                    field->type != PIN_QUERY && field->type != PIN_UNION) {
                    continue;
                }
                pin_value member;
                if (!pin_read_field(NULL, value.ptr, field, &member)) {
                    apush(pending, member);
                }
            }
        }
    }
    afree(pending);
}
static void replacement_scope_roots(execution_program *candidate, const execution_program *scope)
{
    for (int i = 0; i < scope->module->plan.state_count; i++) {
        replacement_root(candidate, scope->shared[i].value);
    }
    for (int i = 0; i < acount(scope->instances); i++) {
        const execution_instance *instance = scope->instances[i];
        for (int j = 0; j < scope->module->plan.state_count; j++) {
            replacement_root(candidate, instance->states[j].value);
        }
        for (int j = 0; j < acount(instance->node_states); j++) {
            replacement_root(candidate, instance->node_states[j]);
        }
    }
}
execution_program *execution_program_prepare_replace(
    const execution_program *p,
    execution_module *module,
    bool restart
)
{
    return execution_program_prepare_replace_services(p, module, restart, NULL, NULL);
}
execution_program *execution_program_prepare_replace_services(
    const execution_program *p,
    execution_module *module,
    bool restart,
    const service_binding *defaults,
    const service_binding *overrides
)
{
    assert(!p->dispatching && !p->running && (!p->system_prepared || paused_system(p)) && module);
    service_binding *services = NULL;
    for (int i = 0; i < acount(p->services); i++) {
        if (p->services[i].value != &p->reader) {
            apush(services, p->services[i]);
        }
    }
    const service_binding *additions[] = { defaults, overrides };
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < acount(additions[pass]); i++) {
            service_binding value = additions[pass][i];
            if (!value.type_name || !*value.type_name) {
                afree(services);
                return NULL;
            }
            int index = -1;
            for (int j = 0; j < acount(services); j++) {
                if (!strcmp(services[j].type_name, value.type_name)) {
                    index = j;
                    break;
                }
            }
            if (index < 0) {
                apush(services, value);
            } else if (pass) {
                services[index] = value;
            }
        }
    }
    execution_program *next = execution_program_new(module, p->world, services);
    afree(services);
    if (!next) {
        return NULL;
    }
    execution_program_set_name(next, p->name);
    next->enabled = p->enabled;
    if (!restart) {
        preserve_shared(next, p);
    }
    for (int n = 0; !restart && n < acount(next->imports); n++) {
        for (int o = 0; o < acount(p->imports); o++) {
            const execution_module *old = p->imports[o]->module;
            const execution_module *now = next->imports[n]->module;
            if (old == now || (now->plan.id && now->plan.id == old->plan.id)) {
                preserve_shared(next->imports[n], p->imports[o]);
                break;
            }
        }
    }
    for (int i = 0; i < acount(p->instances); i++) {
        execution_instance *before = p->instances[i];
        if (before->query_managed && !query_shape_equal(module, p->module)) {
            continue;
        }
        execution_instance *after = attach_row(next, before->owner, before->query_variables);
        if (!after) {
            execution_program_free(next);
            return NULL;
        }
        after->query_managed = before->query_managed;
        after->time = restart ? 0 : before->time;
        if (restart) {
            continue;
        }
        preserve_state(next, p, before->owner, before->query_variables);
        preserve_changes(after, before);
        for (int n = 0; n < acount(next->imports); n++) {
            for (int o = 0; o < acount(p->imports); o++) {
                const execution_module *old = p->imports[o]->module;
                const execution_module *now = next->imports[n]->module;
                if (old != now && (!now->plan.id || now->plan.id != old->plan.id)) {
                    continue;
                }
                preserve_state(
                    next->imports[n],
                    p->imports[o],
                    before->owner,
                    before->query_variables
                );
                break;
            }
        }
    }
    reset_stale_candidate(next);
    next->replacement_of = p;
    next->replacement_module = p->module;
    replacement_scope_roots(next, next);
    for (int i = 0; i < acount(next->imports); i++) {
        replacement_scope_roots(next, next->imports[i]);
    }
    /* Finish the vector before registering addresses: growth must never move
     * slots that the original heap visits while the candidate is staged. */
    for (int i = 0; i < acount(next->replacement_roots); i++) {
        if (behavior_managed_heap_root(p->owner_program->managed, &next->replacement_roots[i])) {
            execution_program_free(next);
            return NULL;
        }
        next->replacement_root_count++;
    }
    return next;
}
void execution_program_commit_replace(execution_program *p, execution_program *next)
{
    assert(
        p && next && !p->dispatching && !p->running &&
        (!p->system_prepared || paused_system(p)) && !next->dispatching &&
        !next->running && !next->system_prepared
    );
    assert(next->replacement_of == p && next->replacement_module == p->module);
    reset_stale_candidate(next);
    execution_program_cancel_system(p);
    replacement_unroot(next);
    next->replacement_of = NULL;
    next->replacement_module = NULL;
    /* The family keeps its head, so the program's heap handle stays valid. */
    behavior_managed_heap *heap = p->managed;
    behavior_managed_heap_adopt(heap, next->managed);
    next->managed = heap;
    p->managed = NULL;
    execution_program old = *p;
    *p = *next;
    p->observer = old.observer;
    p->observer_context = old.observer_context;
    p->owner_program = p;
    for (int i = 0; i < acount(p->imports); i++) {
        p->imports[i]->owner_program = p;
    }
    for (int i = 0; i < acount(p->instances); i++) {
        p->instances[i]->program = p;
    }
    /* Component-reader service points to the embedded reader of this program. */
    for (int i = 0; i < acount(p->services); i++) {
        if (p->services[i].value == &next->reader) {
            p->services[i].value = &p->reader;
        }
    }
    *next = old;
    next->owner_program = next;
    for (int i = 0; i < acount(next->imports); i++) {
        next->imports[i]->owner_program = next;
    }
    for (int i = 0; i < acount(next->instances); i++) {
        next->instances[i]->program = next;
    }
    execution_program_free(next);
    behavior_managed_heap_release_empty(heap);
}
int execution_program_replace(execution_program *p, execution_module *module, bool restart)
{
    execution_program *next = execution_program_prepare_replace(p, module, restart);
    if (!next) {
        return FLOW_ERR_COMPILE;
    }
    execution_program_commit_replace(p, next);
    return FLOW_OK;
}

static bool services_busy(const execution_program *p)
{
    if (p->dispatching || p->running || p->system_prepared) {
        return true;
    }
    for (int i = 0; i < acount(p->imports); i++) {
        if (p->imports[i]->dispatching || p->imports[i]->running ||
            p->imports[i]->system_prepared) {
            return true;
        }
    }
    return false;
}
static bool service_required(const execution_program *p, const char *name)
{
    for (int i = 0; i < p->module->plan.service_count; i++) {
        if (!strcmp(name, p->module->plan.services[i])) {
            return true;
        }
    }
    for (int i = 0; i < acount(p->imports); i++) {
        const execution_module_plan *m = &p->imports[i]->module->plan;
        for (int j = 0; j < m->service_count; j++) {
            if (!strcmp(name, m->services[j])) {
                return true;
            }
        }
    }
    return false;
}
static void bind_service(execution_program *p, const char *name, void *value)
{
    int index = -1;
    for (int i = 0; i < acount(p->services); i++) {
        if (!strcmp(p->services[i].type_name, name)) {
            index = i;
        }
    }
    if (index >= 0) {
        p->services[index].value = value;
    } else {
        size_t length = strlen(name) + 1;
        char *owned = arena_alloc(&p->service_names, length);
        memcpy(owned, name, length);
        apush(p->services, ((service_binding){ owned, value }));
    }
    for (int i = 0; i < acount(p->instances); i++) {
        execution_instance *instance = p->instances[i];
        for (int a = 0; a < acount(instance->activations); a++) {
            execution_activation *activation = instance->activations[a];
            behavior_function_rebind_services(activation->function, &activation->context, p->services);
        }
    }
    for (int i = 0; i < acount(p->imports); i++) {
        bind_service(p->imports[i], name, value);
    }
    execution_program_invalidate_bindings(p);
}
void program_apply_services(execution_program *p)
{
    p = p->owner_program;
    if (services_busy(p)) {
        return;
    }
    service_binding *pending = p->pending_services;
    p->pending_services = NULL;
    for (int i = 0; i < acount(pending); i++) {
        bind_service(p, pending[i].type_name, pending[i].value);
        free((void *)pending[i].type_name);
    }
    afree(pending);
}
bool execution_program_service_bind_valid(const execution_program *p, const char *name, void *value)
{
    assert(p && name);
    p = p->owner_program;
    if (!*name || (!value && service_required(p, name))) {
        return false;
    }
    if (!services_busy(p)) {
        return true;
    }
    for (int i = 0; i < acount(p->pending_services); i++) {
        if (!strcmp(name, p->pending_services[i].type_name)) {
            return true;
        }
    }
    return acount(p->pending_services) < PENDING_SERVICE_LIMIT;
}
int execution_program_bind_service(execution_program *p, const char *name, void *value)
{
    if (!execution_program_service_bind_valid(p, name, value)) {
        return FLOW_ERR_STATE;
    }
    p = p->owner_program;
    if (!services_busy(p)) {
        bind_service(p, name, value);
        return FLOW_OK;
    }
    for (int i = 0; i < acount(p->pending_services); i++) {
        if (strcmp(name, p->pending_services[i].type_name)) {
            continue;
        }
        p->pending_services[i].value = value;
        return FLOW_OK;
    }
    apush(p->pending_services, ((service_binding){ xstrdup(name), value }));
    return FLOW_OK;
}

int execution_program_dispatch(
    execution_program *p,
    uint64_t event,
    uint64_t source,
    const pin_value *arguments,
    int count,
    double now,
    double delta,
    unsigned budget
)
{
    int status = execution_program_emit(p, event, source, arguments, count);
    if (status < 0 || p->dispatching || p->running || p->system_prepared) {
        return status;
    }
    return execution_program_pump(p, now, delta, budget);
}

void execution_program_invalidate_bindings(execution_program *p)
{
    assert(!p->dispatching && !p->running && !p->system_prepared);
    for (int i = 0; i < acount(p->instances); i++) {
        for (int b = 0; b < p->module->plan.binding_count; b++) {
            p->instances[i]->bindings[b].valid = false;
        }
    }
}

void execution_program_set_natives(execution_program *p, const execution_natives *natives)
{
    assert(!acount(p->instances));
    p->natives = natives;
    for (int i = 0; i < acount(p->imports); i++) {
        execution_program_set_natives(p->imports[i], natives);
    }
}
void execution_program_set_name(execution_program *p, const char *name)
{
    assert(!p->dispatching && !p->running && !p->system_prepared);
    char *copy = name ? xstrdup(name) : NULL;
    for (int i = 0; i < acount(p->instances); i++) {
        execution_instance *instance = p->instances[i];
        for (int a = 0; a < acount(instance->activations); a++) {
            execution_activation *activation = instance->activations[a];
            activation->context.program_name = copy;
            behavior_function_rebind_services(activation->function, &activation->context, p->services);
        }
    }
    free(p->name);
    p->name = copy;
}

void execution_program_observe(execution_program *p, execution_profile_observer observer, void *context)
{
    assert(p && p->owner_program == p && !p->dispatching && !p->running && !p->system_prepared);
    p->observer = observer;
    p->observer_context = context;
}
bool execution_program_observed(const execution_program *p)
{
    assert(p);
    return p->owner_program->observer != NULL;
}

const execution_module *execution_program_module(const execution_program *p)
{
    assert(p);
    return p->module;
}
