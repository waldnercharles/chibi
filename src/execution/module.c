#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/integer_report.h"
#include "execution/intrinsics.h"
#include "execution/module_effects.h"
#include "execution/module_internal.h"
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
    free(m);
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
    if (m->query_count || m->description_count || m->binding_count ||
        m->action_count || m->event_count || m->query >= 0 ||
        m->query_variable_count || m->query_filter || m->system_description) {
        snprintf(error, size, "behaviors, systems, queries, bindings and actions are archived");
        return FLOW_ERR_COMPILE;
    }
    for (int i = 0; i < m->import_count; i++) {
        if (!m->imports[i] || execution_module_backend(m->imports[i]) != backend) {
            snprintf(error, size, "imported module generation or backend is unavailable");
            return FLOW_ERR_COMPILE;
        }
    }
    for (int i = 0; i < m->state_count; i++) {
        if (!m->states[i].contains_tasks && value_tasks(m->states[i].initial, 0)) {
            snprintf(error, size, "Retained task schema is missing its task metadata");
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
    execution_module_plan_report_integers(m);
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
        if (f->kind != EXECUTION_FUNCTION_AUTHORED || f->event || f->action ||
            f->observer.component || f->subscription) {
            snprintf(error, size, "function %d is a handler, system or action: those are archived", i);
            return FLOW_ERR_COMPILE;
        }
        if (!captures_valid(m, f)) {
            snprintf(error, size, "function %d has an invalid capture schema", i);
            return FLOW_ERR_COMPILE;
        }
        behavior_function_prepare_borrows(f->body);
        if (backend != BEHAVIOR_BYTECODE && !behavior_function_compile(f->body)) {
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
    const behavior_function **seen = NULL;
    for (int i = 0; i < m->function_count; i++) {
        if (!function_requirements(m, m->functions[i].body, &seen, decoded)) {
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
    execution_module *module = xcalloc(1, sizeof *module);
    module->plan = **owned;
    module->backend = backend;
    module->references = 1;
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
