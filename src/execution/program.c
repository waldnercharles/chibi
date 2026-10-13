#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/component.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/program_inspection.h"
#include "execution/program_internal.h"
#include "execution/request.h"
#include "execution/runner.h"
#include "execution/status.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PENDING_SERVICE_LIMIT 256

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
    return a.type == b.type && a.container == b.container &&
           ((!a.type_name && !b.type_name) ||
            (a.type_name && b.type_name && !strcmp(a.type_name, b.type_name)));
}
static void state_free(state_value *state)
{
    arena_free(&state->storage);
    for (int i = 0; i < acount(state->retired); i++) {
        arena_free(&state->retired[i]);
    }
    afree(state->retired);
}
static int state_assign(state_value *state, const pin_value *value)
{
    arena storage = { 0 };
    pin_value copy;
    if (pin_value_snapshot_to_arena(&storage, &copy, value)) {
        arena_free(&storage);
        return FLOW_ERR_ARGUMENT;
    }
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
static execution_program *program_new(
    execution_module *m,
    ecs_world *world,
    const service_binding *services,
    bool imports
)
{
    assert(m);
    if ((!world && (m->plan.query >= 0 || m->plan.binding_count)) ||
        !program_preflight(m, world)) {
        return NULL;
    }
    execution_program *p = xcalloc(1, sizeof *p);
    p->enabled = true;
    p->module = m;
    p->owner_program = p;
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
    for (int i = 0; i < m->plan.state_count; i++) {
        if (m->plan.states[i].shared &&
            state_assign(&p->shared[i], &m->plan.states[i].initial)) {
            execution_program_free(p);
            return NULL;
        }
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
        query_import(m, m->plan.query, ECS_AND, &terms);
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
            execution_program *child = program_new(modules[i], world, child_services, false);
            afree(child_services);
            if (!child) {
                afree(modules);
                execution_program_free(p);
                return NULL;
            }
            child->owner_program = p;
            apush(p->imports, child);
        }
        afree(modules);
    }
    execution_effect_summary effects = execution_module_effects(m);
    /* Shared callees can occur along many paths. Bound this optional proof;
     * falling back to retained rows does not limit program execution. */
    int stream_steps = 4096;
    p->query_streaming = m->plan.query_variable_count && m->plan.query >= 0 &&
                         !m->plan.state_count && !m->plan.binding_count &&
                         !m->plan.action_count && !m->plan.import_count &&
                         m->plan.function_count == 1 &&
                         m->plan.functions[0].kind == EXECUTION_FUNCTION_SYSTEM &&
                         !acount(p->change_queries) && effects.declared &&
                         !effects.structural && !effects.host_sink &&
                         stream_function(m->plan.functions[0].body, 0, &stream_steps);
    return p;
}
execution_program *execution_program_new(execution_module *m, ecs_world *world, const service_binding *services)
{
    return program_new(m, world, services, true);
}
static state_value *instance_storage(execution_instance *instance, int index)
{
    return instance->program->module->plan.states[index].shared
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
                                  ? type_catalog_find(state->value.type_name)
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
                int status = state_assign(state, &value);
                assert(!status);
                (void)status;
                instance->values[i] = state->value;
                continue;
            }
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
        if (scope->module->plan.states[s].shared) {
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
    int result = state_assign(instance_storage(instance, variable), value);
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
            memset((char *)state->str, '0', (size_t)count);
        }
        return -1;
    }
    if (state->type != PIN_STR) {
        char *used = arena_alloc(&instance->selections, (size_t)count + 1);
        memset(used, '0', (size_t)count);
        used[count] = 0;
        *state = (pin_value){ .type = PIN_STR, .str = used };
    }
    return behavior_state_select((char *)state->str, count, start, flags);
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
    for (int i = 0; i < count; i++) {
        if (!p->module->plan.states[i].shared) {
            int result = state_assign(
                &instance->states[i],
                &p->module->plan.states[i].initial
            );
            assert(!result);
            (void)result;
        }
    }
    sync_state(instance);
    instance->index = acount(p->instances);
    apush(p->instances, instance);
    row_insert(p, instance);
    for (int i = 0; i < acount(p->imports); i++) {
        attach_row(p->imports[i], owner, variables);
    }
    return instance;
}
execution_instance *execution_program_attach(execution_program *p, uint64_t owner)
{
    assert(!p->dispatching && !p->system_prepared);
    if (p->module->plan.query_variable_count) {
        return NULL; /* Multi-target rows are bound by the structural query. */
    }
    uint64_t variables[ECS_QUERY_VARS] = { 0 };
    execution_instance *instance = attach_row(p, owner, variables);
    instance->query_managed = false;
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
    assert(!p->dispatching && !p->system_prepared);
    for (int i = 0; i < acount(p->instances);) {
        if (p->instances[i]->owner == owner) {
            detach_row(p, i);
            continue;
        }
        i++;
    }
}
void execution_program_free(execution_program *p)
{
    if (!p) {
        return;
    }
    assert(!p->system_prepared);
    afree(p->system_activations);
    program_inspection_retire(p);
    for (int i = 0; i < acount(p->instances); i++) {
        instance_free(p->instances[i]);
    }
    afree(p->instances);
    afree(p->row_table);
    afree(p->change_queries);
    afree(p->query_admissions);
    for (int i = 0; i < acount(p->imports); i++) {
        execution_program_free(p->imports[i]);
    }
    afree(p->imports);
    for (int i = 0; i < acount(p->events); i++) {
        free(p->events[i].arguments);
        arena_free(&p->events[i].storage);
    }
    afree(p->events);
    for (int i = 0; i < acount(p->field_edges); i++) {
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
    afree(p->services);
    for (int i = 0; i < acount(p->pending_services); i++) {
        free((void *)p->pending_services[i].type_name);
    }
    afree(p->pending_services);
    arena_free(&p->service_names);
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
    ctx->failure_observer = record_failure;
    ctx->event_argument = program_event_argument;
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
            matches |= array->items[i].u64 == owner;
        }
    } else {
        matches = value.u64 == owner;
    }
    program_activation_free(a);
    return matches;
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
    int status = dispatch_to(instance, event, instance->owner, arguments, count, now, delta, budget);
    program_apply_services(instance->program->owner_program);
    program_collect_states(instance->program);
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
    for (int i = 0; i < instance->program->module->plan.state_count; i++) {
        if (instance->program->module->plan.states[i].id == id) {
            if (!program_same_type(instance->program->module->plan.states[i].initial, value)) {
                return FLOW_ERR_ARGUMENT;
            }
            int status = state_assign(instance_storage(instance, i), &value);
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
    if (!event || count < 0) {
        return FLOW_ERR_ARGUMENT;
    }
    queued_event e = { .event = event, .owner = owner, .count = count };
    e.arguments = xcalloc((size_t)(count + 1), sizeof *e.arguments);
    for (int i = 0; i < count; i++) {
        if (pin_value_snapshot_to_arena(&e.storage, &e.arguments[i], &arguments[i])) {
            free(e.arguments);
            arena_free(&e.storage);
            return FLOW_ERR_ARGUMENT;
        }
    }
    apush(p->events, e);
    return FLOW_OK;
}
void execution_program_set_enabled(execution_program *p, bool enabled)
{
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
static int query_matches(execution_instance *instance, int index)
{
    execution_program *p = instance->program;
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
            arena storage = { 0 };
            behavior_function_activation activation = { 0 };
            bool resumable = behavior_function_resumable(q->predicate);
            if (resumable) {
                behavior_function_activation_init(q->predicate, &activation);
            }
            pin_value result = { .type = PIN_BOOL };
            behavior_function_context context = {
                .activation = resumable ? &activation : NULL,
                .owner = instance->owner,
                .query_variables = instance->query_variables,
                .results = &result,
                .now = p->now,
                .delta_seconds = p->delta,
                .budget = p->budget,
                .services = p->services
            };
            behavior_function_bind_storage(&context, &storage);
            if (!behavior_function_bind(
                    q->predicate,
                    &context,
                    p->services,
                    instance->owner
                )) {
                behavior_function_activation_free(&activation);
                arena_free(&storage);
                return FLOW_ERR_ARGUMENT;
            }
            bool compiled = p->module->backend != BEHAVIOR_BYTECODE;
            int status = resumable
                           ? behavior_function_run(q->predicate, &context, compiled)
                           : behavior_function_run_once(q->predicate, &context, compiled);
            if (status < 0 && !p->failure.status) {
                p->failure = (execution_program_failure){
                    .status = status,
                    .kind = context.failure,
                    .module_id = p->module->plan.id,
                    .owner = instance->owner
                };
                snprintf(
                    p->failure.message,
                    sizeof p->failure.message,
                    "%s",
                    context.error_message ? context.error_message : "query predicate failed"
                );
            }
            behavior_function_release_host(&context);
            behavior_function_activation_free(&activation);
            arena_free(&storage);
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
            bool added = !instance;
            if (added) {
                instance = &candidate;
            }
            instance->query_seen = true;
            query_snapshot(instance);
            int match = query_matches(instance, p->module->plan.query);
            if (added && match > 0) {
                instance = attach_row(p, candidate.owner, candidate.query_variables);
                instance->query_managed = true;
                instance->query_new = true;
                instance->query_seen = true;
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
    p->query_rows = 0;
    if (!p->enabled || !query_dynamic(p->module, p->module->plan.query)) {
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
        execution_activation *a = program_activation_new(
            &row,
            p->module->plan.functions[0].body,
            NULL,
            0,
            p->now,
            p->delta,
            p->budget
        );
        if (!a) {
            status = FLOW_ERR_ARGUMENT;
            break;
        }
        status = program_activation_run(a, p->now, p->delta, p->budget);
        assert(status <= 0 && !acount(a->requests));
        program_activation_free(a);
        if (status < 0) {
            break;
        }
    }
    p->dispatching = false;
    ecs_query_end(&query);
    return status;
}
static int program_run(execution_program *p, double now, double delta, unsigned budget, int phase)
{
    assert(!p->dispatching && !p->system_prepared);
    bool update = phase > 0;
    p->updates += update;
    p->now = now;
    p->delta = delta;
    p->budget = budget ? budget : 10000;
    if (streaming(p)) {
        /* These modules have no handlers, bindings, state, or suspension. */
        for (int i = 0; i < acount(p->events); i++) {
            free(p->events[i].arguments);
            arena_free(&p->events[i].storage);
        }
        asetlen(p->events, 0);
        int status = prepare_stream(p);
        if (status < 0 || !update) {
            if (status < 0 || phase != -1) {
                afree(p->query_admissions);
            }
            return status;
        }
        status = stream_rows(p);
        afree(p->query_admissions);
        return status;
    }
    int refreshed = refresh_rows(p);
    if (refreshed < 0) {
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
    for (int i = 0; update && p->enabled && i < acount(p->instances); i++) {
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
    queued_event *events = p->events;
    p->events = NULL;
    for (int i = 0; i < acount(events); i++) {
        queued_event *e = &events[i];
        int failure = FLOW_OK;
        for (int j = 0; j < acount(p->instances); j++) {
            execution_instance *instance = p->instances[j];
            int status = dispatch_to(instance, e->event, e->owner, e->arguments, e->count, now, delta, p->budget);
            if (status == FLOW_ERR_UNKNOWN_EVENT) {
                continue;
            }
            if (status < 0) {
                failure = status;
                break;
            }
        }
        free(e->arguments);
        arena_free(&e->storage);
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
            afree(events);
            p->dispatching = false;
            return failure;
        }
    }
    afree(events);
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
    int status = program_run(p, now, delta, budget, 2);
    program_apply_services(p);
    return status;
}
int execution_program_pump(execution_program *p, double now, double delta, unsigned budget)
{
    int status = program_run(p, now, delta, budget, 0);
    program_apply_services(p);
    return status;
}
int execution_program_update(execution_program *p, double now, double delta, unsigned budget)
{
    int status = program_run(p, now, delta, budget, 1);
    program_apply_services(p);
    return status;
}
int execution_program_prepare_system(execution_program *p, double now, double delta, unsigned budget)
{
    assert(p && !p->system_prepared && !p->running && !p->dispatching);
    p->updates++;
    int status = program_run(p, now, delta, budget, -1);
    program_apply_services(p);
    if (status < 0) {
        return status;
    }
    p->system_prepared = true;
    p->system_ran = false;
    p->system_status = FLOW_OK;
    for (int i = 0; i < acount(p->instances); i++) {
        execution_instance *instance = p->instances[i];
        instance->time += delta;
        instance->last_time = now;
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
int execution_program_run_system(execution_program *p)
{
    assert(p && p->system_prepared && !p->system_ran && !p->dispatching && !p->running);
    p->system_ran = true;
    if (streaming(p)) {
        p->system_status = stream_rows(p);
        return p->system_status;
    }
    p->dispatching = true;
    for (int i = 0; i < acount(p->system_activations); i++) {
        execution_activation *a = p->system_activations[i];
        if (program_instance_busy(a->instance)) {
            program_activation_free(a);
            p->system_activations[i] = NULL;
            continue;
        }
        program_query_consume(a->instance);
        int status = program_activation_run(a, p->now, p->delta, p->budget);
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
    for (int i = 0; i < acount(p->system_activations); i++) {
        execution_activation *a = p->system_activations[i];
        if (a) {
            program_activation_free(a);
        }
    }
    asetlen(p->system_activations, 0);
    p->system_prepared = false;
    afree(p->query_admissions);
    int status = p->system_status;
    if (status >= 0) {
        status = program_observers_maintain(p, p->now, p->delta, p->budget);
    }
    program_apply_services(p);
    program_collect_states(p);
    return status;
}
int execution_program_bindings(execution_program *p, double now, double delta, unsigned budget)
{
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
static int matching_state(const execution_module *before, const execution_module_state *state)
{
    for (int i = 0; i < before->plan.state_count; i++) {
        const execution_module_state *old = &before->plan.states[i];
        if (old->id == state->id && old->shared == state->shared &&
            program_same_type(old->initial, state->initial)) {
            return i;
        }
    }
    return -1;
}
static void preserve_shared(execution_program *next, const execution_program *before)
{
    for (int i = 0; i < next->module->plan.state_count; i++) {
        const execution_module_state *state = &next->module->plan.states[i];
        if (!state->shared) {
            continue;
        }
        int old = matching_state(before->module, state);
        if (old >= 0) {
            int status = state_assign(&next->shared[i], &before->shared[old].value);
            assert(!status);
            (void)status;
        }
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
        int index = matching_state(before->module, state);
        if (state->shared || index < 0) {
            continue;
        }
        int status = execution_instance_set_state(
            instance,
            next->module->plan.states[i].id,
            old->states[index].value
        );
        assert(!status);
        (void)status;
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
    assert(!p->dispatching && !p->running && !p->system_prepared && module);
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
        for (int n = 0; n < acount(next->imports); n++) {
            for (int o = 0; o < acount(p->imports); o++) {
                const execution_module *old = p->imports[o]->module;
                const execution_module *now = next->imports[n]->module;
                if (old == now || (now->plan.id && now->plan.id == old->plan.id)) {
                    preserve_shared(next->imports[n], p->imports[o]);
                    break;
                }
            }
        }
    }
    for (int i = 0; i < acount(p->instances); i++) {
        execution_instance *before = p->instances[i];
        if (before->query_managed && !query_shape_equal(module, p->module)) {
            continue;
        }
        execution_instance *after = attach_row(next, before->owner, before->query_variables);
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
    next->replacement_of = p;
    next->replacement_module = p->module;
    return next;
}
void execution_program_commit_replace(execution_program *p, execution_program *next)
{
    assert(
        p && next && !p->dispatching && !p->running && !p->system_prepared &&
        !next->dispatching && !next->running && !next->system_prepared
    );
    assert(next->replacement_of == p && next->replacement_module == p->module);
    next->replacement_of = NULL;
    next->replacement_module = NULL;
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
