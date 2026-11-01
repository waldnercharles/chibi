#include "ecs/ecs.h"
#include "ecs/execution.h"
#include "execution/component.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/integer_report.h"
#include "execution/intrinsics.h"
#include "execution/module_effects.h"
#include "execution/module_internal.h"
#include "execution/request.h"
#include "execution/state.h"
#include "execution/status.h"
#include "execution/storage.h"
#include "execution/wasm_jit.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/record.h"
#include "types/text.h"
#include "types/union.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_storage(size_t *bytes, size_t count, size_t size)
{
    if (size && count > (SIZE_MAX - *bytes) / size) {
        *bytes = SIZE_MAX;
        return;
    }
    *bytes += count * size;
}
static void vector_storage(size_t *bytes, const void *values, size_t size)
{
    if (!values) {
        return;
    }
    const dyna_hdr *header = (const dyna_hdr *)values - 1;
    add_storage(bytes, 1, sizeof *header);
    add_storage(bytes, (size_t)header->cap, size);
}
size_t execution_module_storage_bytes(const execution_module *m)
{
    assert(m);
    const execution_module_plan *p = &m->plan;
    size_t bytes = sizeof *m;
    add_storage(&bytes, 1, m->effect_bytes);
    add_storage(&bytes, 1, arena_storage_bytes(&p->storage));
#define VECTOR(name) vector_storage(&bytes, p->name, sizeof *p->name)
    VECTOR(states);
    VECTOR(functions);
    VECTOR(queries);
    VECTOR(descriptions);
    VECTOR(bindings);
    VECTOR(actions);
    VECTOR(diagnostics);
    VECTOR(services);
    VECTOR(imports);
    VECTOR(types);
    VECTOR(events);
#undef VECTOR
    for (int i = 0; i < p->function_count; i++) {
        const execution_module_function *f = &p->functions[i];
        add_storage(&bytes, 1, behavior_function_storage_bytes(f->body));
        if (f->subscription) {
            add_storage(&bytes, 1, behavior_function_storage_bytes(f->subscription));
        }
        add_storage(&bytes, (size_t)f->local_capture_count, sizeof *f->captures);
        if (f->entry) {
            add_storage(&bytes, 1, strlen(f->entry) + 1);
        }
        if (f->event_name) {
            add_storage(&bytes, 1, strlen(f->event_name) + 1);
        }
    }
    for (int i = 0; i < p->binding_count; i++) {
        const execution_module_binding *b = &p->bindings[i];
        add_storage(&bytes, 1, behavior_function_storage_bytes(b->body));
        add_storage(&bytes, 1, behavior_function_storage_bytes(b->dependencies));
        add_storage(&bytes, (size_t)b->read_count, sizeof *b->reads);
        add_storage(&bytes, (size_t)b->write_count, sizeof *b->writes);
        vector_storage(&bytes, b->fields, sizeof *b->fields);
    }
    for (int i = 0; i < p->description_count; i++) {
        add_storage(
            &bytes,
            1,
            behavior_function_storage_bytes(p->descriptions[i].evaluate)
        );
    }
    for (int i = 0; i < p->query_count; i++) {
        if (p->queries[i].predicate) {
            add_storage(&bytes, 1, behavior_function_storage_bytes(p->queries[i].predicate));
        }
        add_storage(
            &bytes,
            (size_t)p->queries[i].child_count,
            sizeof *p->queries[i].children
        );
    }
    for (int i = 0; i < p->action_count; i++) {
        add_storage(
            &bytes,
            (size_t)p->actions[i].outcome_count,
            sizeof *p->actions[i].outcomes
        );
        vector_storage(&bytes, p->actions[i].defaults, sizeof *p->actions[i].defaults);
    }
    for (int i = 0; i < p->event_count; i++) {
        add_storage(
            &bytes,
            (size_t)p->events[i].field_count,
            sizeof *p->events[i].fields
        );
    }
    return bytes;
}

execution_module_plan *execution_module_plan_new()
{
    execution_module_plan *m = xcalloc(1, sizeof *m);
    m->query = -1;
    return m;
}
void execution_module_retain(execution_module *m)
{
    assert(m->references > 0);
    m->references++;
}
static void destroy_plan(execution_module_plan *m)
{
    for (int i = 0; i < acount(m->functions); i++) {
        behavior_function_free(m->functions[i].body);
        behavior_function_free(m->functions[i].subscription);
        free(m->functions[i].captures);
        free(m->functions[i].entry);
        free(m->functions[i].event_name);
    }
    for (int i = 0; i < acount(m->bindings); i++) {
        behavior_function_free(m->bindings[i].body);
        behavior_function_free(m->bindings[i].dependencies);
        free(m->bindings[i].reads);
        free(m->bindings[i].writes);
        afree(m->bindings[i].fields);
    }
    for (int i = 0; i < acount(m->queries); i++) {
        behavior_function_free(m->queries[i].predicate);
        free(m->queries[i].children);
    }
    for (int i = 0; i < acount(m->actions); i++) {
        free(m->actions[i].outcomes);
        afree(m->actions[i].defaults);
    }
    for (int i = 0; i < acount(m->events); i++) {
        free(m->events[i].fields);
    }
    afree(m->events);
    afree(m->states);
    afree(m->functions);
    afree(m->bindings);
    afree(m->queries);
    for (int i = 0; i < acount(m->descriptions); i++) {
        behavior_function_free(m->descriptions[i].evaluate);
    }
    afree(m->descriptions);
    afree(m->actions);
    afree(m->diagnostics);
    afree(m->services);
    afree(m->types);
    for (int i = 0; i < acount(m->imports); i++) {
        execution_module_free(m->imports[i]);
    }
    afree(m->imports);
    arena_free(&m->storage);
}
void execution_module_plan_free(execution_module_plan *m)
{
    if (m) {
        destroy_plan(m);
        free(m);
    }
}
void execution_module_free(execution_module *m)
{
    if (!m) {
        return;
    }
    assert(m->references > 0);
    if (--m->references) {
        return;
    }
    destroy_plan(&m->plan);
    free(m->effect_ids);
    free(m);
}
static bool query_valid(
    const execution_module_plan *m,
    int index,
    unsigned char *seen,
    unsigned *bound,
    bool generators,
    unsigned depth,
    int *steps,
    int row_root,
    bool row_context
)
{
    row_context |= index == row_root;
    if (index < 0 || index >= m->query_count || seen[index] == 1 ||
        depth > 64 || --*steps < 0) {
        return false;
    }
    seen[index] = 1;
    const execution_module_query *q = &m->queries[index];
    if (q->kind < EXECUTION_QUERY_HAS || q->kind > EXECUTION_QUERY_CHANGED ||
        q->child_count < 0 || (q->child_count && !q->children) ||
        q->source_variable < 0 || q->source_variable > m->query_variable_count ||
        q->target_variable < 0 || q->target_variable > m->query_variable_count ||
        !(*bound & (1u << q->source_variable))) {
        return false;
    }
    if (q->required && q->kind != EXECUTION_QUERY_HAS) {
        return false;
    }
    bool leaf = q->kind == EXECUTION_QUERY_HAS ||
                q->kind == EXECUTION_QUERY_RELATION ||
                q->kind == EXECUTION_QUERY_GENERATOR ||
                q->kind == EXECUTION_QUERY_CHANGED;
    if ((leaf && (!q->component || q->child_count)) || (!leaf && q->component) ||
        (q->kind != EXECUTION_QUERY_GENERATOR && q->target_variable) ||
        (q->kind != EXECUTION_QUERY_VALUE && q->predicate)) {
        return false;
    }
    if (q->source_traversal < 0 || q->source_traversal > 2 ||
        (q->source_traversal && !q->source_relationship) ||
        (q->kind != EXECUTION_QUERY_HAS && q->kind != EXECUTION_QUERY_CHANGED &&
         q->source_relationship)) {
        return false;
    }
    if (q->kind == EXECUTION_QUERY_GENERATOR) {
        if (!generators || !q->target_variable ||
            *bound != (1u << q->target_variable) - 1) {
            return false;
        }
        *bound |= 1u << q->target_variable;
    }
    if (q->kind == EXECUTION_QUERY_VALUE && q->child_count) {
        return false;
    }
    if (q->kind == EXECUTION_QUERY_VALUE && !q->predicate && !row_context) {
        return false;
    }
    if (q->kind == EXECUTION_QUERY_VALUE && q->predicate) {
        const behavior_function *f = q->predicate;
        if (q->child_count || !f || !f->frozen || f->argument_count ||
            f->variable_count != m->state_count || f->result_count != 1 ||
            f->result_defaults[0].type != PIN_BOOL ||
            f->result_defaults[0].container != PIN_CONTAINER_SINGLE ||
            !execution_query_predicate_valid(f, *bound)) {
            return false;
        }
    }
    if (q->kind == EXECUTION_QUERY_NOT && q->child_count != 1) {
        return false;
    }
    for (int i = 0; i < q->child_count; i++) {
        if (!query_valid(
                m,
                q->children[i],
                seen,
                bound,
                generators && q->kind == EXECUTION_QUERY_ALL,
                depth + 1,
                steps,
                row_root,
                row_context
            )) {
            return false;
        }
    }
    seen[index] = 2;
    return true;
}
static void query_reachable(const execution_module_plan *m, int index, bool *seen)
{
    if (seen[index]) {
        return;
    }
    seen[index] = true;
    const execution_module_query *term = &m->queries[index];
    for (int i = 0; i < term->child_count; i++) {
        query_reachable(m, term->children[i], seen);
    }
}
static bool query_filter_valid(const execution_module_plan *m, int index, unsigned char *seen)
{
    if (seen[index] == 3) {
        return true;
    }
    seen[index] = 3;
    const execution_module_query *q = &m->queries[index];
    if (q->kind == EXECUTION_QUERY_GENERATOR) {
        return false;
    }
    for (int i = 0; i < q->child_count; i++) {
        if (!query_filter_valid(m, q->children[i], seen)) {
            return false;
        }
    }
    return true;
}
static bool query_filter_path(
    const execution_module_plan *m,
    int index,
    bool mandatory,
    int *count,
    int *steps
)
{
    if (--*steps < 0) {
        return false;
    }
    if (index == m->query_filter - 1) {
        return mandatory && ++*count == 1;
    }
    const execution_module_query *q = &m->queries[index];
    for (int i = 0; i < q->child_count; i++) {
        if (!query_filter_path(m, q->children[i], mandatory && q->kind == EXECUTION_QUERY_ALL, count, steps)) {
            return false;
        }
    }
    return true;
}
static bool require_service(execution_module_plan *m, const char *name, bool decoded)
{
    if (!name) {
        return true;
    }
    for (int i = 0; i < acount(m->services); i++) {
        if (!strcmp(m->services[i], name)) {
            return true;
        }
    }
    if (!name[0]) {
        return false;
    }
    if (decoded) {
        if (acount(m->services) == acap(m->services)) {
            return false;
        }
        apush(m->services, name);
        m->service_count = acount(m->services);
        return true;
    }
    size_t size = strlen(name) + 1;
    char *copy = arena_alloc(&m->storage, size);
    memcpy(copy, name, size);
    apush(m->services, copy);
    m->service_count = acount(m->services);
    return true;
}
static bool function_requirements(
    execution_module_plan *m,
    const behavior_function *f,
    const behavior_function ***seen,
    bool decoded
)
{
    if (decoded) {
        if (f->component_access && !require_service(m, "ecs.world", true)) {
            return false;
        }
        if (f->component_bindings &&
            !require_service(m, EVAL_COMPONENT_READER_SERVICE, true)) {
            return false;
        }
        for (int i = 0; i < acount(f->host_calls); i++) {
            const node_host_call *call = f->host_calls[i].call;
            if (call->service && !call->service[0]) {
                return false;
            }
            if (!call->optional_service && !require_service(m, call->service, true)) {
                return false;
            }
        }
        for (int i = 0; i < acount(f->commands); i++) {
            if (!require_service(m, f->commands[i].command->service, true)) {
                return false;
            }
        }
        for (int i = 0; i < acount(f->queries); i++) {
            if (!require_service(m, f->queries[i].service, true)) {
                return false;
            }
        }
        for (int i = 0; i < acount(f->definitions); i++) {
            if (!function_requirements(m, f->definitions[i], seen, true)) {
                return false;
            }
        }
        return true;
    }
    for (int i = 0; i < acount(*seen); i++) {
        if ((*seen)[i] == f) {
            return true;
        }
    }
    int first = acount(*seen);
    apush(*seen, f);
    for (int at = first; at < acount(*seen); at++) {
        const behavior_function *current = (*seen)[at];
        if (current->component_access) {
            if (!require_service(m, "ecs.world", false)) {
                return false;
            }
        }
        if (current->component_bindings) {
            if (!require_service(m, EVAL_COMPONENT_READER_SERVICE, false)) {
                return false;
            }
        }
        for (int i = 0; i < acount(current->host_calls); i++) {
            const node_host_call *call = current->host_calls[i].call;
            if (call->service && !call->service[0]) {
                return false;
            }
            if (!call->optional_service && !require_service(m, call->service, false)) {
                return false;
            }
        }
        for (int i = 0; i < acount(current->commands); i++) {
            if (!require_service(m, current->commands[i].command->service, false)) {
                return false;
            }
        }
        for (int i = 0; i < acount(current->queries); i++) {
            if (!require_service(m, current->queries[i].service, false)) {
                return false;
            }
        }
        for (int i = 0; i < acount(current->calls); i++) {
            const behavior_function *callee = current->calls[i].function;
            bool found = false;
            for (int j = 0; j < acount(*seen); j++) {
                found |= (*seen)[j] == callee;
            }
            if (!found) {
                apush(*seen, callee);
            }
        }
    }
    return true;
}
const char *const *execution_module_services(const execution_module *m, int *count)
{
    assert(m && count);
    *count = m->plan.service_count;
    return m->plan.services;
}
static bool same_type(pin_value a, pin_value b)
{
    return a.type == b.type && a.container == b.container &&
           pin_value_layout_same(&a, &b) &&
           ((!a.type_name && !b.type_name) ||
            (a.type_name && b.type_name && !strcmp(a.type_name, b.type_name)));
}
static const char *own_string(arena *storage, const char *value)
{
    size_t size = strlen(value) + 1;
    char *copy = arena_alloc(storage, size);
    memcpy(copy, value, size);
    return copy;
}
static bool events_valid(execution_module_plan *m, bool decoded)
{
    for (int i = 0; i < m->event_count; i++) {
        execution_module_event_declaration *event = &m->events[i];
        if (!event->id || !event->abi || !event->name || !event->name[0] ||
            event->field_count < 0 || (event->field_count && !event->fields)) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (event->id == m->events[j].id ||
                !strcmp(event->name, m->events[j].name)) {
                return false;
            }
        }
        if (!decoded) {
            event->name = own_string(&m->storage, event->name);
        }
        for (int j = 0; j < event->field_count; j++) {
            execution_module_event_field *field = &event->fields[j];
            if (!field->name || !field->name[0] ||
                field->initial.type <= PIN_FLOW || field->initial.type > PIN_ENUM ||
                field->initial.container > PIN_CONTAINER_VIEW ||
                (field->initial.container == PIN_CONTAINER_VIEW &&
                 field->initial.ptr) ||
                (field->default_owner &&
                 (!field->has_default || field->initial.type != PIN_HANDLE ||
                  field->initial.container != PIN_CONTAINER_SINGLE))) {
                return false;
            }
            for (int k = 0; k < j; k++) {
                if (!strcmp(field->name, event->fields[k].name)) {
                    return false;
                }
            }
            if (field->nominal) {
                const execution_type_requirement *type = NULL;
                for (int k = 0; k < m->type_count; k++) {
                    if (m->types[k].id == field->nominal) {
                        type = &m->types[k];
                        break;
                    }
                }
                if (!type || type->storage != field->initial.type ||
                    !type->name || !field->initial.type_name ||
                    strcmp(type->name, field->initial.type_name) ||
                    !pin_value_layout_same(
                        &(pin_value){ .type = type->storage,
                                      .type_name = type->name,
                                      .record = type->record },
                        &field->initial
                    )) {
                    return false;
                }
            }
            pin_value value = field->initial;
            if (!decoded &&
                pin_value_snapshot_to_arena(&m->storage, &value, &field->initial)) {
                return false;
            }
            field->initial = value;
            if (!decoded) {
                field->name = own_string(&m->storage, field->name);
            }
        }
    }
    return true;
}
static const execution_module_event_declaration *event_declaration(
    const execution_module_plan *m,
    uint64_t id
)
{
    for (int i = 0; i < m->event_count; i++) {
        if (m->events[i].id == id) {
            return &m->events[i];
        }
    }
    return NULL;
}
static bool captures_match(
    const behavior_function *parent,
    const behavior_function *child,
    int arguments,
    const execution_module_capture *captures,
    int count
)
{
    if (arguments < 0 || count < 0 || arguments > child->argument_count ||
        count > child->argument_count - arguments || (count && !captures) ||
        (arguments && (!parent || arguments > parent->argument_count))) {
        return false;
    }
    for (int i = 0; i < arguments; i++) {
        if (!same_type(parent->argument_defaults[i], child->argument_defaults[i])) {
            return false;
        }
    }
    for (int i = 0; i < count; i++) {
        const execution_module_capture *capture = &captures[i];
        pin_value physical = child->argument_defaults[arguments + i];
        if (physical.type != BF_POINTER_TYPE ||
            physical.container != PIN_CONTAINER_SINGLE) {
            return false;
        }
        if (capture->value < 0 || !parent ||
            capture->value >= acount(parent->data_values) ||
            !same_type(capture->type, parent->data_values[capture->value].initial)) {
            return false;
        }
    }
    return true;
}
static bool captures_valid(const execution_module_plan *m, const execution_module_function *f)
{
    const behavior_function *parent = NULL;
    if (f->parent) {
        for (int i = 0; i < m->function_count; i++) {
            if (m->functions[i].id == f->parent) {
                parent = m->functions[i].body;
                break;
            }
        }
        if (!parent) {
            return false;
        }
    }
    return captures_match(parent, f->body, f->capture_count, f->captures, f->local_capture_count);
}
static const execution_module_action *module_action(const execution_module_plan *m, uint64_t id)
{
    for (int i = 0; i < m->action_count; i++) {
        if (m->actions[i].id == id) {
            return &m->actions[i];
        }
    }
    return NULL;
}
static bool state_function_valid(const execution_module_plan *m, const behavior_function *f)
{
    for (int i = 0; i < acount(f->state_selections); i++) {
        const behavior_state_selection *selection = &f->state_selections[i];
        if (selection->index < 0 || selection->index >= m->action_count ||
            m->actions[selection->index].state_kind != NATIVE_STATE_MULTI ||
            m->actions[selection->index].outcome_count != selection->outputs) {
            return false;
        }
    }
    for (int i = 0; i < acount(f->host_calls); i++) {
        const behavior_function_host_call *host = &f->host_calls[i];
        const node_host_call *canonical = execution_intrinsic_resolve(host->capability_id);
        const node_host_call *actual = host->call;
        bool intrinsic = canonical &&
                         host->capability_abi ==
                             execution_intrinsic_abi(host->capability_id) &&
                         actual->invoke == canonical->invoke &&
                         actual->value == canonical->value &&
                         actual->context == canonical->context &&
                         actual->action == canonical->action &&
                         actual->service == canonical->service &&
                         actual->missing_service == canonical->missing_service &&
                         actual->optional_service == canonical->optional_service &&
                         actual->variadic == canonical->variadic &&
                         actual->reusable == canonical->reusable;
        execution_intrinsic operation = intrinsic
                                          ? execution_intrinsic_kind(host->capability_id)
                                          : 0;
        if (!intrinsic || operation < EXECUTION_INTRINSIC_STATE_DEMAND ||
            operation > EXECUTION_INTRINSIC_RETRIGGER_ACTION) {
            continue;
        }
        const behavior_function_data_value *values = f->data_values + host->first;
        const execution_module_action
            *action = module_action(m, values[1].initial.u64);
        if (!action || action->state_kind <= NATIVE_STATE_NONE ||
            action->state_kind > NATIVE_STATE_RETRIGGER) {
            return false;
        }
        if (operation == EXECUTION_INTRINSIC_RETRIGGER_ACTION) {
            if (action->state_kind != NATIVE_STATE_RETRIGGER) {
                return false;
            }
            continue;
        }
        pin_value uninitialized = { 0 };
        if (native_state_inputs(action->state_kind, values[2].initial.i, &uninitialized) <
            0) {
            return false;
        }
        if (operation == EXECUTION_INTRINSIC_STATE_TRANSITION) {
            int count = native_state_input_count(action->state_kind);
            if (action->state_kind == NATIVE_STATE_RETRIGGER ||
                host->result_first - host->first != count + 3) {
                return false;
            }
            for (int j = 0; j < count; j++) {
                pin_type type = action->state_kind == NATIVE_STATE_COUNT ||
                                        (action->state_kind == NATIVE_STATE_MULTI &&
                                         j == 2)
                                  ? PIN_I32
                                  : PIN_BOOL;
                if (values[j + 3].initial.type != type ||
                    values[j + 3].initial.container != PIN_CONTAINER_SINGLE) {
                    return false;
                }
            }
        }
    }
    return true;
}
static bool value_tasks(pin_value value, int depth)
{
    if (value.type == PIN_TASK || depth == 64) {
        return true;
    }
    if (value.container == PIN_CONTAINER_MAP) {
        const pin_map *map = value.ptr;
        if (!map) {
            return false;
        }
        if (value_tasks(map->key_type, depth + 1) ||
            value_tasks(map->value_type, depth + 1)) {
            return true;
        }
        for (int i = 0; i < map->count; i++) {
            if (value_tasks(map->entries[i].key, depth + 1) ||
                value_tasks(map->entries[i].value, depth + 1)) {
                return true;
            }
        }
        return false;
    }
    if (value.container >= PIN_CONTAINER_ARRAY) {
        const pin_array *array = value.ptr;
        if (!array) {
            return false;
        }
        pin_value element = { .type = array->element_type,
                              .type_name = array->type_name,
                              .record = array->record };
        if (value_tasks(element, depth + 1)) {
            return true;
        }
        for (int i = 0; i < array->count; i++) {
            if (value_tasks(pin_array_get(array, i), depth + 1)) {
                return true;
            }
        }
        return false;
    }
    if (value.type == PIN_UNION) {
        const pin_union *tagged = value.ptr;
        for (int i = 0; tagged && i < tagged->count; i++) {
            if (value_tasks(tagged->values[i], depth + 1)) {
                return true;
            }
        }
        return false;
    }
    if (value.type != PIN_STRUCT) {
        return false;
    }
    const type_desc *type = pin_value_type(&value);
    for (int i = 0; type && i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        if (field->type != PIN_TASK && field->type != PIN_STRUCT &&
            field->type != PIN_UNION) {
            continue;
        }
        pin_value member = { .type = field->type, .type_name = field->type_name };
        if (value.ptr && field->type != PIN_TASK) {
            pin_read_field(NULL, value.ptr, field, &member);
        }
        if (value_tasks(member, depth + 1)) {
            return true;
        }
    }
    return false;
}
static bool retained_tasks(const execution_module_plan *plan)
{
    for (int i = 0; i < plan->state_count; i++) {
        if (plan->states[i].contains_tasks || value_tasks(plan->states[i].initial, 0)) {
            return true;
        }
    }
    for (int i = 0; i < plan->import_count; i++) {
        if (plan->imports[i]->retained_tasks) {
            return true;
        }
    }
    return false;
}
static int publish(execution_module_plan *m, behavior_backend backend, char *error, size_t size, bool decoded)
{
    if (!m->valid || !behavior_backend_available(backend)) {
        snprintf(error, size, "module is invalid or backend is unavailable");
        return FLOW_ERR_COMPILE;
    }
    if (m->state_count != acount(m->states) ||
        m->function_count != acount(m->functions) ||
        m->query_count != acount(m->queries) ||
        m->description_count != acount(m->descriptions) ||
        m->binding_count != acount(m->bindings) ||
        m->action_count != acount(m->actions) || m->import_count != acount(m->imports) ||
        m->diagnostic_count != acount(m->diagnostics) ||
        m->service_count != acount(m->services) ||
        m->type_count != acount(m->types) || m->event_count != acount(m->events)) {
        snprintf(error, size, "module table counts do not match owned plans");
        return FLOW_ERR_COMPILE;
    }
    for (int i = 0; i < m->service_count; i++) {
        if (!m->services[i] || !m->services[i][0]) {
            snprintf(error, size, "invalid required service name");
            return FLOW_ERR_COMPILE;
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(m->services[i], m->services[j])) {
                snprintf(error, size, "duplicate required service name");
                return FLOW_ERR_COMPILE;
            }
        }
    }
    for (int i = 0; i < m->type_count; i++) {
        execution_type_requirement *type = &m->types[i];
        if (!type->record) {
            continue;
        }
        const pin_record *record = type->record;
        if (record->native || !pin_record_valid(record) ||
            type->storage != PIN_STRUCT || record->id != type->id ||
            record->layout != type->layout || record->type.size != type->size ||
            !type->name || strcmp(record->type.name, type->name)) {
            snprintf(error, size, "authored type requirement has incompatible layout");
            return FLOW_ERR_COMPILE;
        }
        if (!decoded) {
            type->record = pin_record_snapshot(&m->storage, record);
            type->name = type->record->type.name;
        }
    }
    if (!events_valid(m, decoded)) {
        snprintf(error, size, "event declaration table is invalid");
        return FLOW_ERR_COMPILE;
    }
    for (int i = 0; i < m->import_count; i++) {
        if (!m->imports[i] || execution_module_backend(m->imports[i]) != backend) {
            snprintf(error, size, "imported module generation or backend is unavailable");
            return FLOW_ERR_COMPILE;
        }
        const execution_module_plan *import = &m->imports[i]->plan;
        for (int j = 0; j < import->event_count; j++) {
            const execution_module_event_declaration
                *event = event_declaration(m, import->events[j].id);
            if (event && (event->abi != import->events[j].abi ||
                          strcmp(event->name, import->events[j].name))) {
                snprintf(error, size, "imported event declaration is incompatible");
                return FLOW_ERR_COMPILE;
            }
            for (int k = 0; k < i; k++) {
                event = event_declaration(
                    &m->imports[k]->plan,
                    import->events[j].id
                );
                if (event && (event->abi != import->events[j].abi ||
                              strcmp(event->name, import->events[j].name))) {
                    snprintf(error, size, "imported event declarations disagree");
                    return FLOW_ERR_COMPILE;
                }
            }
        }
    }
    bool registration = false;
    for (int i = 0; i < m->function_count; i++) {
        registration |= m->functions[i].kind == EXECUTION_FUNCTION_SYSTEM;
    }
    if (registration && retained_tasks(m)) {
        snprintf(error, size, "System-owned retained bindings, including imported libraries, cannot contain tasks; keep task handles in coroutine locals");
        return FLOW_ERR_COMPILE;
    }
    for (int i = 0; i < m->state_count; i++) {
        if (!m->states[i].contains_tasks && value_tasks(m->states[i].initial, 0)) {
            snprintf(error, size, "Retained task schema is missing its task metadata");
            return FLOW_ERR_COMPILE;
        }
        if (m->states[i].shared && (m->states[i].contains_tasks ||
                                    m->states[i].initial.type == PIN_TASK)) {
            snprintf(error, size, "Shared retained bindings cannot contain tasks");
            return FLOW_ERR_COMPILE;
        }
        if (!m->states[i].id) {
            snprintf(error, size, "state %d has no stable identity", i);
            return FLOW_ERR_COMPILE;
        }
        for (int j = 0; j < i; j++) {
            if (m->states[j].id == m->states[i].id) {
                snprintf(error, size, "state identity is duplicated");
                return FLOW_ERR_COMPILE;
            }
        }
        pin_value copy = m->states[i].initial;
        if (!decoded &&
            pin_value_snapshot_to_arena(&m->storage, &copy, &m->states[i].initial)) {
            snprintf(error, size, "state layout is unavailable");
            return FLOW_ERR_COMPILE;
        }
        m->states[i].initial = copy;
    }
    if (m->query_filter < 0 || m->query_filter > m->query_count ||
        m->query_variable_count < 0 ||
        m->query_variable_count > ECS_QUERY_VARS || m->query < -1 ||
        (m->query < 0 && (m->query_variable_count || m->query_filter)) ||
        m->system_description < 0 || m->system_description > m->description_count) {
        snprintf(error, size, "query variable schema is invalid");
        return FLOW_ERR_COMPILE;
    }
    if (m->query >= 0) {
        unsigned char *seen = xcalloc((size_t)(m->query_count + 1), 1);
        unsigned bound = 1;
        int steps = 200000;
        int row_root = m->system_description
                         ? m->descriptions[m->system_description - 1].query
                         : -1;
        bool valid = query_valid(m, m->query, seen, &bound, true, 0, &steps, row_root, false) &&
                     bound == (1u << (m->query_variable_count + 1)) - 1;
        for (int i = 0; i < m->query_count; i++) {
            valid &= seen[i] == 2 || m->description_count;
        }
        if (valid && m->query_filter) {
            int matches = 0;
            steps = 200000;
            valid = query_filter_path(m, m->query, true, &matches, &steps) &&
                    matches == 1 && query_filter_valid(m, m->query_filter - 1, seen);
            for (int i = 0; i < m->query_count; i++) {
                if ((m->queries[i].kind == EXECUTION_QUERY_VALUE ||
                     m->queries[i].kind == EXECUTION_QUERY_CHANGED) &&
                    seen[i] != 3) {
                    valid = false;
                }
            }
        }
        free(seen);
        if (!valid) {
            snprintf(error, size, "query has invalid structure, variable bindings, or an impure predicate");
            return FLOW_ERR_COMPILE;
        }
    }
    for (int i = 0; i < m->description_count; i++) {
        execution_module_query_description *q = &m->descriptions[i];
        const behavior_function *f = q->evaluate;
        if (q->query < 0 || q->query >= m->query_count || q->query_variable_count < 0 ||
            q->query_variable_count > ECS_QUERY_VARS || q->row_type.type != PIN_QUERY ||
            q->row_type.container != PIN_CONTAINER_SINGLE || q->row_type.ptr ||
            !q->row_type.type_name || !q->row_type.type_name[0] || !f || !f->frozen ||
            f->result_count < 1 || f->result_defaults[0].type != PIN_BOOL ||
            f->result_defaults[0].container != PIN_CONTAINER_SINGLE ||
            !execution_query_evaluator_valid(f, (1u << (q->query_variable_count + 1)) - 1)) {
            snprintf(error, size, "query description has an invalid row evaluator");
            return FLOW_ERR_COMPILE;
        }
        unsigned char *seen = xcalloc((size_t)m->query_count + 1, 1);
        unsigned bound = 1;
        int steps = 200000;
        execution_module_plan schema = *m;
        schema.query_variable_count = q->query_variable_count;
        bool valid = query_valid(&schema, q->query, seen, &bound, true, 0, &steps, q->query, false) &&
                     bound == (1u << (q->query_variable_count + 1)) - 1;
        free(seen);
        bool contained = true;
        if (i == m->system_description - 1) {
            execution_module_plan path = *m;
            path.query_filter = q->query + 1;
            int matches = 0, scope_steps = 200000;
            contained = query_filter_path(&path, m->query, true, &matches, &scope_steps) &&
                        matches == 1;
        }
        if (!valid || (i == m->system_description - 1 &&
                       (f->argument_count || !contained ||
                        q->query_variable_count != m->query_variable_count))) {
            snprintf(error, size, "query description has invalid variable bindings");
            return FLOW_ERR_COMPILE;
        }
        if (!decoded) {
            pin_value retained;
            if (pin_value_snapshot_to_arena(&m->storage, &retained, &q->row_type)) {
                snprintf(error, size, "query row type cannot be retained");
                return FLOW_ERR_COMPILE;
            }
            q->row_type = retained;
        }
        if (execution_integer_reports_enabled()) {
            behavior_function_report_integers(q->evaluate);
        }
        behavior_function_prepare_borrows(q->evaluate);
        if (backend != BEHAVIOR_BYTECODE && !behavior_function_compile(q->evaluate)) {
            snprintf(error, size, "query row evaluator emission failed");
            return FLOW_ERR_COMPILE;
        }
    }
    bool *reachable = xcalloc((size_t)m->query_count + 1, sizeof *reachable);
    if (m->query >= 0) {
        query_reachable(m, m->query, reachable);
    }
    for (int i = 0; i < m->description_count; i++) {
        query_reachable(m, m->descriptions[i].query, reachable);
    }
    bool all_reachable = true;
    for (int i = 0; i < m->query_count; i++) {
        all_reachable &= reachable[i];
    }
    free(reachable);
    if (!all_reachable) {
        snprintf(error, size, "query term is not reachable from a verified description");
        return FLOW_ERR_COMPILE;
    }
    execution_module_plan_report_integers(m);
    for (int i = 0; i < m->query_count; i++) {
        behavior_function *predicate = m->queries[i].predicate;
        behavior_function_prepare_borrows(predicate);
        if (predicate && backend != BEHAVIOR_BYTECODE &&
            !behavior_function_compile(predicate)) {
            snprintf(error, size, "%s query predicate emission failed", behavior_backend_name(backend));
            return FLOW_ERR_COMPILE;
        }
    }
    for (int i = 0; i < m->function_count; i++) {
        const execution_module_function *f = &m->functions[i];
        if (!f->body || !f->body->frozen || f->body->variable_count != m->state_count) {
            snprintf(error, size, "function %d has an invalid definition", i);
            return FLOW_ERR_COMPILE;
        }
    }
    for (int i = 0; i < m->function_count; i++) {
        execution_module_function *f = &m->functions[i];
        if (!f->body || !f->body->frozen ||
            (f->subscription && !f->subscription->frozen)) {
            snprintf(error, size, "function %d is not finalized", i);
            return FLOW_ERR_COMPILE;
        }
        if (f->observer.component) {
            execution_field_observer *observer = &f->observer;
            if (!observer->layout || !observer->field || !f->event ||
                !f->subscription || f->action || f->body->argument_count != 1 ||
                observer->argument != 0 || f->subscription->result_count != 1 ||
                f->subscription->result_defaults[0].type != PIN_HANDLE ||
                behavior_function_resumable(f->subscription) ||
                (observer->type.record && !observer->type.record->native) ||
                observer->type.container != PIN_CONTAINER_SINGLE ||
                observer->type.type == PIN_FLOW ||
                !same_type(observer->type, f->body->argument_defaults[0])) {
                snprintf(error, size, "field observer has an incompatible typed receiver or payload");
                return FLOW_ERR_COMPILE;
            }
            pin_value type = observer->type;
            if (!decoded &&
                pin_value_snapshot_to_arena(&m->storage, &type, &observer->type)) {
                snprintf(error, size, "field observer type cannot be retained");
                return FLOW_ERR_COMPILE;
            }
            observer->type = type;
        }
        if (f->event) {
            const execution_module_event_declaration *event = event_declaration(m, f->event);
            if (!event || event->abi != f->event_abi ||
                event->field_count != f->body->argument_count) {
                snprintf(error, size, "handler event declaration is unavailable or incompatible");
                return FLOW_ERR_COMPILE;
            }
            for (int j = 0; j < event->field_count; j++) {
                if (!same_type(event->fields[j].initial, f->body->argument_defaults[j])) {
                    snprintf(error, size, "handler event payload differs from its declaration");
                    return FLOW_ERR_COMPILE;
                }
            }
        }
        for (int j = 0; f->event && j < i; j++) {
            const execution_module_function *other = &m->functions[j];
            if (other->event != f->event) {
                continue;
            }
            if (other->event_abi != f->event_abi ||
                other->body->argument_count != f->body->argument_count) {
                snprintf(error, size, "event declaration ABI differs between handlers");
                return FLOW_ERR_COMPILE;
            }
            for (int argument = 0; argument < f->body->argument_count; argument++) {
                if (!same_type(
                        other->body->argument_defaults[argument],
                        f->body->argument_defaults[argument]
                    )) {
                    snprintf(error, size, "event payload type differs between handlers");
                    return FLOW_ERR_COMPILE;
                }
            }
        }
        if (!captures_valid(m, f)) {
            snprintf(error, size, "function %d has an invalid capture schema", i);
            return FLOW_ERR_COMPILE;
        }
        behavior_function_prepare_borrows(f->body);
        behavior_function_prepare_borrows(f->subscription);
        if (backend != BEHAVIOR_BYTECODE &&
            (!behavior_function_compile(f->body) ||
             (f->subscription && !behavior_function_compile(f->subscription)))) {
            snprintf(
                error,
                size,
                "%s function emission failed: %s",
                behavior_backend_name(backend),
                backend == BEHAVIOR_WASM ? execution_wasm_jit_error() : "SLJIT emission failed"
            );
            return FLOW_ERR_COMPILE;
        }
    }
    for (int i = 0; i < m->function_count; i++) {
    }
    for (int i = 0; i < m->binding_count; i++) {
        const execution_module_binding *b = &m->bindings[i];
        if (!b->body || !b->body->frozen || b->body->argument_count ||
            b->body->result_count != 1 || b->body->variable_count != m->state_count ||
            !b->component || !b->target) {
            snprintf(error, size, "binding %d has no finalized destination", i);
            return FLOW_ERR_COMPILE;
        }
        if (!b->dependencies || !b->dependencies->frozen ||
            b->dependencies->argument_count ||
            b->dependencies->variable_count != m->state_count ||
            b->dependency_count < 0 ||
            b->dependencies->result_count != b->dependency_count ||
            b->field_count != acount(b->fields)) {
            snprintf(error, size, "binding dependency evaluator is not finalized");
            return FLOW_ERR_COMPILE;
        }
        if (b->target_type.record && !b->target_type.record->native) {
            snprintf(error, size, "authored record component bindings are not supported");
            return FLOW_ERR_COMPILE;
        }
        const type_desc *type = b->target_type.type == PIN_STRUCT
                                  ? type_catalog_find(b->target_type.type_name)
                                  : NULL;
        size_t width = b->target_type.container >= PIN_CONTAINER_ARRAY
                         ? sizeof(pin_array *)
                     : b->target_type.type == PIN_STRUCT
                         ? (type ? type->size : 0)
                         : behavior_scalar_size(b->target_type.type);
        if (!width || b->offset > SIZE_MAX - width) {
            snprintf(error, size, "binding destination layout is invalid");
            return FLOW_ERR_COMPILE;
        }
        for (int j = 0; j < b->field_count; j++) {
            if (b->fields[j].offset > SIZE_MAX - b->fields[j].size) {
                snprintf(error, size, "binding read footprint overflows its component");
                return FLOW_ERR_COMPILE;
            }
        }
        behavior_function_prepare_borrows(b->body);
        behavior_function_prepare_borrows(b->dependencies);
        if (backend != BEHAVIOR_BYTECODE &&
            (!behavior_function_compile(b->body) ||
             (b->dependencies && !behavior_function_compile(b->dependencies)))) {
            snprintf(
                error,
                size,
                "%s binding emission failed: %s",
                behavior_backend_name(backend),
                backend == BEHAVIOR_WASM ? execution_wasm_jit_error() : "SLJIT emission failed"
            );
            return FLOW_ERR_COMPILE;
        }
    }
    for (int i = 0; i < m->action_count; i++) {
        execution_module_action *a = &m->actions[i];
        if (!a->id || a->result_count < 0 || (a->result_count && !a->defaults) ||
            a->state_kind < NATIVE_STATE_NONE ||
            a->state_kind > NATIVE_STATE_RETRIGGER) {
            snprintf(error, size, "action result schema is invalid");
            return FLOW_ERR_COMPILE;
        }
        for (int j = 0; j < i; j++) {
            if (m->actions[j].id == a->id) {
                snprintf(error, size, "duplicate action identity");
                return FLOW_ERR_COMPILE;
            }
        }
        if (a->state_kind != NATIVE_STATE_NONE) {
            bool result = a->state_kind == NATIVE_STATE_COUNT ||
                          a->state_kind == NATIVE_STATE_FLIP;
            int outcomes = a->state_kind == NATIVE_STATE_FLIP ? 2 : 1;
            if (a->result_count != (result ? 1 : 0) ||
                (a->state_kind == NATIVE_STATE_MULTI ? a->outcome_count < 1
                                                     : a->outcome_count != outcomes) ||
                (result &&
                 (a->defaults[0].container != PIN_CONTAINER_SINGLE ||
                  a->defaults[0].type !=
                      (a->state_kind == NATIVE_STATE_COUNT ? PIN_I32 : PIN_BOOL)))) {
                snprintf(error, size, "invalid action state schema");
                return FLOW_ERR_COMPILE;
            }
        }
        for (int j = 0; j < a->result_count; j++) {
            pin_value value = a->defaults[j];
            if (!decoded &&
                pin_value_snapshot_to_arena(&m->storage, &value, &a->defaults[j])) {
                snprintf(error, size, "action result schema cannot be retained");
                return FLOW_ERR_COMPILE;
            }
            a->defaults[j] = value;
        }
    }
    for (int i = 0; i < m->function_count; i++) {
        const execution_module_function *function = &m->functions[i];
        if (!state_function_valid(m, function->body) ||
            (function->subscription &&
             !state_function_valid(m, function->subscription))) {
            snprintf(error, size, "invalid function action state contract");
            return FLOW_ERR_COMPILE;
        }
    }
    for (int i = 0; i < m->binding_count; i++) {
        if (!state_function_valid(m, m->bindings[i].body) ||
            (m->bindings[i].dependencies &&
             !state_function_valid(m, m->bindings[i].dependencies))) {
            snprintf(error, size, "invalid binding action state contract");
            return FLOW_ERR_COMPILE;
        }
    }
    const behavior_function **seen = NULL;
    for (int i = 0; i < m->description_count; i++) {
        if (!function_requirements(m, m->descriptions[i].evaluate, &seen, decoded)) {
            goto services_missing;
        }
    }
    for (int i = 0; i < m->query_count; i++) {
        if (m->queries[i].predicate &&
            !function_requirements(m, m->queries[i].predicate, &seen, decoded)) {
            goto services_missing;
        }
    }
    for (int i = 0; i < m->function_count; i++) {
        if (!function_requirements(m, m->functions[i].body, &seen, decoded)) {
            goto services_missing;
        }
        if (m->functions[i].subscription) {
            if (!function_requirements(m, m->functions[i].subscription, &seen, decoded)) {
                goto services_missing;
            }
        }
    }
    for (int i = 0; i < m->binding_count; i++) {
        if (!function_requirements(m, m->bindings[i].body, &seen, decoded)) {
            goto services_missing;
        }
        if (m->bindings[i].dependencies) {
            if (!function_requirements(m, m->bindings[i].dependencies, &seen, decoded)) {
                goto services_missing;
            }
        }
    }
    if (m->query >= 0 || m->description_count || m->binding_count) {
        if (!require_service(m, "ecs.world", decoded)) {
            goto services_missing;
        }
    }
    if (decoded) {
        for (int i = 0; i < m->import_count; i++) {
            for (int j = 0; j < m->imports[i]->plan.service_count; j++) {
                if (!require_service(m, m->imports[i]->plan.services[j], true)) {
                    goto services_missing;
                }
            }
        }
    }
    afree(seen);
    if (size) {
        error[0] = 0;
    }
    return FLOW_OK;
services_missing:
    afree(seen);
    snprintf(error, size, "service requirements are invalid or exceed reserved capacity");
    return FLOW_ERR_COMPILE;
}

uint64_t execution_module_event(const execution_module *m, const char *name)
{
    assert(m);
    for (int i = 0; i < m->plan.event_count; i++) {
        if (!strcmp(m->plan.events[i].name, name)) {
            return m->plan.events[i].id;
        }
    }
    return 0;
}
int execution_module_event_count(const execution_module *m)
{
    assert(m);
    return m->plan.event_count;
}
execution_module_event_info execution_module_event_at(const execution_module *m, int index)
{
    assert(m && index >= 0 && index < m->plan.event_count);
    const execution_module_event_declaration *event = &m->plan.events[index];
    return (
        execution_module_event_info
    ){ event->id, event->abi, event->name, event->field_count };
}
execution_module_event_field_info execution_module_event_field_at(
    const execution_module *m,
    int event,
    int field
)
{
    assert(m && event >= 0 && event < m->plan.event_count);
    assert(field >= 0 && field < m->plan.events[event].field_count);
    const execution_module_event_field *value = &m->plan.events[event].fields[field];
    return (execution_module_event_field_info){ value->name,
                                                value->initial,
                                                value->nominal,
                                                value->has_default,
                                                value->default_owner };
}

static execution_module *finish(
    execution_module_plan **owned,
    behavior_backend backend,
    char *error,
    size_t size,
    bool decoded
)
{
    assert(owned && *owned && error && size);
    if (publish(*owned, backend, error, size, decoded) != FLOW_OK) {
        return NULL;
    }
    size_t effect_bytes = execution_module_effects_bytes(*owned);
    if (effect_bytes == SIZE_MAX) {
        snprintf(error, size, "%s", "module effect closure exceeds bounded record limit");
        return NULL;
    }
    execution_module *module = xcalloc(1, sizeof *module);
    module->plan = **owned;
    module->backend = backend;
    module->references = 1;
    module->retained_tasks = retained_tasks(&module->plan);
    bool effects_ready = execution_module_build_effects(module, effect_bytes);
    assert(effects_ready);
    (void)effects_ready;
    free(*owned);
    *owned = NULL;
    return module;
}
execution_module *execution_module_finish(
    execution_module_plan **owned,
    behavior_backend backend,
    char *error,
    size_t size
)
{
    return finish(owned, backend, error, size, false);
}
execution_module *execution_module_finish_decoded(
    execution_module_plan **owned,
    behavior_backend backend,
    char *error,
    size_t size
)
{
    return finish(owned, backend, error, size, true);
}
uint64_t execution_module_id(const execution_module *m)
{
    assert(m);
    return m->plan.id;
}
behavior_backend execution_module_backend(const execution_module *m)
{
    assert(m);
    return m->backend;
}
int execution_module_import_count(const execution_module *m)
{
    assert(m);
    return m->plan.import_count;
}
const execution_module *execution_module_import_at(const execution_module *m, int index)
{
    assert(m && index >= 0 && index < m->plan.import_count);
    return m->plan.imports[index];
}
int execution_module_function_count(const execution_module *m)
{
    assert(m);
    return m->plan.function_count;
}
execution_module_function_info execution_module_function_at(const execution_module *m, int index)
{
    assert(m && index >= 0 && index < m->plan.function_count);
    const execution_module_function *f = &m->plan.functions[index];
    return (execution_module_function_info){
        .id = f->id,
        .event = f->event,
        .event_abi = f->event_abi,
        .action = f->action,
        .parent = f->parent,
        .outcome = f->outcome,
        .capture_count = f->capture_count,
        .local_capture_count = f->local_capture_count,
        .continuation_wait = f->continuation_wait,
        .entry = f->entry,
        .event_name = f->event_name,
        .kind = f->kind,
        .body = f->body,
        .subscription = f->subscription,
        .effects = f->effects,
    };
}
