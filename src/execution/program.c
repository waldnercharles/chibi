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
#include "execution/status.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PENDING_SERVICE_LIMIT 256

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
static void query_import(const execution_module *m, int index, ecs_oper oper, ecs_term **terms)
{
    const execution_module_query *q = &m->plan.queries[index];
    if (q->kind == EXECUTION_QUERY_HAS) {
        apush(*terms, ((ecs_term){ .oper = oper, .id = q->component }));
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
        query_import(m, m->plan.query, ECS_AND, &p->query);
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
    program_collect_payloads(p, NULL);
}
static bool module_function(const execution_module *m, const behavior_function *f)
{
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
            return execution_program_instance(p->imports[i], a->instance->owner);
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
execution_instance *execution_program_attach(execution_program *p, uint64_t owner)
{
    assert(!p->dispatching && !p->system_prepared);
    execution_instance *existing = execution_program_instance(p, owner);
    if (existing) {
        existing->query_managed = false;
        return existing;
    }
    execution_instance *instance = xcalloc(1, sizeof *instance);
    instance->program = p;
    instance->owner = owner;
    asetlen(instance->node_states, p->module->plan.action_count);
    if (p->module->plan.action_count) {
        memset(
            instance->node_states,
            0,
            (size_t)p->module->plan.action_count * sizeof *instance->node_states
        );
    }
    int count = p->module->plan.state_count;
    instance->states = xcalloc((size_t)(count + 1), sizeof *instance->states);
    instance->values = xcalloc((size_t)(count + 1), sizeof *instance->values);
    instance->layouts = xcalloc((size_t)(count + 1), sizeof *instance->layouts);
    instance->bindings = xcalloc(
        (size_t)(p->module->plan.binding_count + 1),
        sizeof *instance->bindings
    );
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
    apush(p->instances, instance);
    for (int i = 0; i < acount(p->imports); i++) {
        execution_program_attach(p->imports[i], owner);
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
    afree(instance->node_states);
    arena_free(&instance->selections);
    free(instance);
}
void execution_program_detach(execution_program *p, uint64_t owner)
{
    assert(!p->dispatching && !p->system_prepared);
    for (int i = 0; i < acount(p->instances); i++) {
        if (p->instances[i]->owner == owner) {
            instance_free(p->instances[i]);
            for (int j = 0; j < acount(p->imports); j++) {
                execution_program_detach(p->imports[j], owner);
            }
            adel(p->instances, i);
            return;
        }
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
    afree(p->query);
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
            execution_program_detach(p, instance->owner);
            continue;
        }
        i++;
    }
    program_collect_states(p);
}
static uint64_t *owners(execution_program *p)
{
    uint64_t *out = NULL;
    for (int i = 0; i < acount(p->instances); i++) {
        if (!p->instances[i]->query_managed) {
            apush(out, p->instances[i]->owner);
        }
    }
    if (!p->queried || !p->enabled) {
        return out;
    }
    ecs_query query;
    int status = ecs_query_begin_terms(p->world, &query, p->query, acount(p->query));
    assert(!status);
    (void)status;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        uint64_t owner = ecs_entity_to_u64(entity);
        if (!execution_program_instance(p, owner) ||
            execution_program_instance(p, owner)->query_managed) {
            apush(out, owner);
        }
    }
    ecs_query_end(&query);
    return out;
}
static int program_run(execution_program *p, double now, double delta, unsigned budget, int phase)
{
    assert(!p->dispatching && !p->system_prepared);
    bool update = phase > 0;
    p->updates += update;
    p->now = now;
    p->delta = delta;
    p->budget = budget ? budget : 10000;
    uint64_t *membership = owners(p);
    for (int i = 0; i < acount(p->instances);) {
        uint64_t owner = p->instances[i]->owner;
        bool retained = !p->instances[i]->query_managed;
        for (int j = 0; j < acount(membership); j++) {
            retained |= membership[j] == owner;
        }
        bool alive = !owner || !p->world ||
                     ecs_entity_is_alive(p->world, ecs_entity_from_u64(owner));
        if (!retained || !alive) {
            execution_program_detach(p, owner);
            continue;
        }
        i++;
    }
    uint64_t begin = execution_module_event(p->module, "Begin");
    for (int i = 0; i < acount(membership); i++) {
        if (execution_program_instance(p, membership[i])) {
            continue;
        }
        execution_instance *instance = execution_program_attach(p, membership[i]);
        instance->query_managed = true;
        if (begin) {
            int status = execution_instance_dispatch(instance, begin, NULL, 0, now, delta, p->budget);
            if (status < 0) {
                afree(membership);
                return status;
            }
        }
    }
    afree(membership);
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
            if (active) {
                continue;
            }
            execution_activation *
                a = program_activation_new(instance, function->body, NULL, 0, now, delta, p->budget);
            if (!a) {
                p->dispatching = false;
                return FLOW_ERR_ARGUMENT;
            }
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
        uint64_t *targets = owners(p);
        int failure = FLOW_OK;
        for (int j = 0; j < acount(targets); j++) {
            execution_instance *instance = execution_program_instance(p, targets[j]);
            if (!instance) {
                continue;
            }
            int status = dispatch_to(instance, e->event, e->owner, e->arguments, e->count, now, delta, p->budget);
            if (status == FLOW_ERR_UNKNOWN_EVENT) {
                continue;
            }
            if (status < 0) {
                failure = status;
                break;
            }
        }
        afree(targets);
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
    int status = execution_program_pump(p, now, delta, budget);
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
        if (!p->enabled || program_instance_busy(instance)) {
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
    p->dispatching = true;
    for (int i = 0; i < acount(p->system_activations); i++) {
        execution_activation *a = p->system_activations[i];
        if (program_instance_busy(a->instance)) {
            program_activation_free(a);
            p->system_activations[i] = NULL;
            continue;
        }
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
static void preserve_state(execution_program *next, const execution_program *before, uint64_t owner)
{
    execution_instance *old = execution_program_instance((execution_program *)before, owner);
    execution_instance *instance = execution_program_instance(next, owner);
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
        execution_instance *after = execution_program_attach(next, before->owner);
        after->query_managed = before->query_managed;
        after->time = restart ? 0 : before->time;
        if (restart) {
            continue;
        }
        preserve_state(next, p, before->owner);
        for (int n = 0; n < acount(next->imports); n++) {
            for (int o = 0; o < acount(p->imports); o++) {
                const execution_module *old = p->imports[o]->module;
                const execution_module *now = next->imports[n]->module;
                if (old != now && (!now->plan.id || now->plan.id != old->plan.id)) {
                    continue;
                }
                preserve_state(next->imports[n], p->imports[o], before->owner);
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
