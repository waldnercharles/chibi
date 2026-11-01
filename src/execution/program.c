#include "execution/borrow.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/native.h"
#include "execution/program_internal.h"
#include "execution/status.h"
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
/* Collect once the managed heap has allocated this much since the last
 * collection and more than it kept. */
#define COLLECT_MIN_BYTES ((size_t)4 * 1024)

bool program_same_type(pin_value a, pin_value b)
{
    /* A native record frozen into one side matches the catalog layout the
     * other names. */
    return a.type == b.type && a.container == b.container &&
           pin_value_layout_same(&a, &b) &&
           ((!a.type_name && !b.type_name) ||
            (a.type_name && b.type_name && !strcmp(a.type_name, b.type_name)));
}
static size_t pointer_slot(const program_pointer_set *set, const pin_pointer *pointer)
{
    uint64_t hash = (uint64_t)(uintptr_t)pointer * 0x9e3779b97f4a7c15ull;
    size_t i = (size_t)(hash >> 32) & (set->capacity - 1);
    while (set->slots[i] && set->slots[i] != pointer) {
        i = (i + 1) & (set->capacity - 1);
    }
    return i;
}
/* Keeps the table at most half full, so long pointer chains stay linear. */
bool program_pointer_first_visit(program_pointer_set *set, const pin_pointer *pointer)
{
    if (2 * (set->count + 1) > set->capacity) {
        const pin_pointer **old = set->slots;
        size_t old_capacity = set->capacity;
        set->capacity = old_capacity ? old_capacity * 2 : 64;
        set->slots = xcalloc(set->capacity, sizeof *set->slots);
        for (size_t i = 0; i < old_capacity; i++) {
            if (old[i]) {
                set->slots[pointer_slot(set, old[i])] = old[i];
            }
        }
        free(old);
    }
    size_t i = pointer_slot(set, pointer);
    if (set->slots[i]) {
        return false;
    }
    set->slots[i] = pointer;
    set->count++;
    return true;
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
static int initialize_states(execution_program *p)
{
    pin_value *defaults = NULL;
    for (int i = 0; i < p->module->plan.state_count; i++) {
        apush(defaults, p->module->plan.states[i].initial);
    }
    int count = acount(defaults);
    int status = program_validate_values(p, defaults, count);
    for (int i = 0; i < count && !status; i++) {
        status = state_assign(p, &p->states[i], &defaults[i]);
    }
    afree(defaults);
    return status;
}
/* The calls' view of state i: its value, with the layout a borrow checks. */
static void sync_state(execution_program *p)
{
    for (int i = 0; i < p->module->plan.state_count; i++) {
        const state_value *state = &p->states[i];
        p->values[i] = state->value;
        const type_desc *type = state->value.type == PIN_STRUCT
                                  ? pin_value_type(&state->value)
                                  : NULL;
        p->layouts[i] = (behavior_variable_storage){
            .data = state->value.ptr,
            .type_name = state->value.type_name,
            .type = type,
            .layout = type ? type_layout_hash(type) : 0
        };
    }
}
static void commit_state(execution_program *p)
{
    for (int i = 0; i < p->module->plan.state_count; i++) {
        state_value *state = &p->states[i];
        pin_value value = p->values[i];
        if (state->value.type_name != value.type_name) {
            if (state->value.type_name && value.type_name &&
                !strcmp(state->value.type_name, value.type_name)) {
                value.type_name = state->value.type_name;
            } else {
                int status = state_assign(p, state, &value);
                assert(!status);
                (void)status;
                p->values[i] = state->value;
                continue;
            }
        }
        if (state->value.ptr != value.ptr) {
            pin_value_collection_release(&state->value);
        }
        state->value = value;
        p->values[i] = value;
    }
}
static execution_program *program_new(execution_module *m, const service_binding *services, bool imports)
{
    assert(m);
    if (!program_preflight(m)) {
        return NULL;
    }
    execution_program *p = xcalloc(1, sizeof *p);
    p->module = m;
    p->owner_program = p;
    p->managed = xcalloc(1, sizeof *p->managed);
    behavior_managed_heap_init(p->managed);
    execution_module_retain(m);
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
    for (int i = 0; i < m->plan.service_count; i++) {
        if (!service_present(p->services, m->plan.services[i])) {
            execution_program_free(p);
            return NULL;
        }
    }
    int count = m->plan.state_count;
    p->states = xcalloc((size_t)(count + 1), sizeof *p->states);
    p->values = xcalloc((size_t)(count + 1), sizeof *p->values);
    p->layouts = xcalloc((size_t)(count + 1), sizeof *p->layouts);
    if (initialize_states(p)) {
        execution_program_free(p);
        return NULL;
    }
    sync_state(p);
    if (imports) {
        execution_module **modules = NULL;
        collect_imports(m, &modules);
        for (int i = 0; i < acount(modules); i++) {
            execution_program *child = program_new(modules[i], p->services, false);
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
    return p;
}
behavior_managed_heap *execution_program_managed_heap(execution_program *program)
{
    return program->owner_program->managed;
}
execution_program *execution_program_new(execution_module *m, const service_binding *services)
{
    return program_new(m, services, true);
}
int program_validate_values(execution_program *program, const pin_value *values, int count)
{
    if (count < 0 || (count && !values)) {
        return FLOW_ERR_ARGUMENT;
    }
    for (int i = 0; i < count; i++) {
        if (behavior_managed_storage_value_error(
                &program->owner_program->managed->storage,
                &values[i]
            )) {
            return FLOW_ERR_ARGUMENT;
        }
    }
    return FLOW_OK;
}
static void managed_value(arena_collection *collection, const pin_value *value)
{
    behavior_managed_heap_trace_value(value, arena_collect_mark, collection);
}
static void scope_roots(const execution_program *p, arena_collection *collection)
{
    for (int i = 0; i < p->module->plan.state_count; i++) {
        managed_value(collection, &p->states[i].value);
        managed_value(collection, &p->values[i]);
    }
}
static void scope_release_retired(execution_program *p)
{
    for (int i = 0; i < p->module->plan.state_count; i++) {
        state_value *state = &p->states[i];
        for (int v = 0; v < acount(state->retired); v++) {
            arena_free(&state->retired[v]);
        }
        aclear(state->retired);
    }
}
void program_collect_states(execution_program *p)
{
    p = p->owner_program;
    assert(!p->running);
    scope_release_retired(p);
    for (int i = 0; i < acount(p->imports); i++) {
        scope_release_retired(p->imports[i]);
    }
    size_t allocated = 0, kept = 0;
    for (const behavior_managed_heap *heap = p->managed; heap; heap = heap->previous) {
        allocated += heap->storage.allocated;
        kept += heap->storage.kept;
    }
    if (allocated < COLLECT_MIN_BYTES || allocated < kept) {
        return;
    }
    arena **arenas = NULL;
    for (behavior_managed_heap *heap = p->managed; heap; heap = heap->previous) {
        apush(arenas, &heap->storage);
    }
    arena_collection c;
    arena_collect_begin(&c, arenas, acount(arenas));
    behavior_managed_heap_trace_roots(p->managed, arena_collect_mark, &c);
    scope_roots(p, &c);
    for (int i = 0; i < acount(p->imports); i++) {
        scope_roots(p->imports[i], &c);
    }
    arena_collect_end(&c);
    afree(arenas);
    behavior_managed_heap_release_empty(p->managed);
}
static bool module_function(const execution_module *m, const behavior_function *f)
{
    for (int i = 0; i < m->plan.function_count; i++) {
        if (m->plan.functions[i].body == f || m->plan.functions[i].subscription == f) {
            return true;
        }
    }
    return false;
}
/* The program whose states function f reads: the root or one of its imports. */
static execution_program *function_scope(execution_program *root, const behavior_function *f)
{
    if (module_function(root->module, f)) {
        return root;
    }
    for (int i = 0; i < acount(root->imports); i++) {
        if (module_function(root->imports[i]->module, f)) {
            return root->imports[i];
        }
    }
    assert(!"executable function has no owning module");
    return NULL;
}
static void program_bind_instance(const behavior_function *f, behavior_function_context *ctx)
{
    execution_program *scope = function_scope(ctx->inspection_user, f);
    sync_state(scope);
    ctx->variables = scope->values;
    ctx->variable_storage = scope->layouts;
    ctx->value_owner = scope;
    ctx->module = scope->module;
    ctx->program = scope;
    ctx->instance_time = &scope->time;
}
static void program_commit_instance(behavior_function_context *ctx)
{
    commit_state(ctx->value_owner);
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
    const execution_program *scope = function_scope(ctx->inspection_user, f);
    int count = scope->module->plan.state_count;
    if (behavior_borrow_overlap(address, bytes, scope, 1, sizeof *scope) ||
        behavior_borrow_overlap(
            address,
            bytes,
            scope->values,
            (size_t)count,
            sizeof *scope->values
        ) ||
        behavior_borrow_overlap(
            address,
            bytes,
            scope->layouts,
            (size_t)count,
            sizeof *scope->layouts
        )) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (behavior_borrow_overlap(
                address,
                bytes,
                &scope->states[i],
                1,
                sizeof scope->states[i]
            )) {
            return false;
        }
    }
    return true;
}
static void store_value(void *owner, int variable, const pin_value *value)
{
    execution_program *scope = owner;
    assert(variable >= 0 && variable < scope->module->plan.state_count);
    assert(program_same_type(scope->module->plan.states[variable].initial, *value));
    commit_state(scope);
    int result = state_assign(scope, &scope->states[variable], value);
    assert(!result);
    (void)result;
    sync_state(scope);
}
static void add_set_value(void *owner, int variable, const pin_value *value)
{
    execution_program *scope = owner;
    assert(variable >= 0 && variable < scope->module->plan.state_count);
    commit_state(scope);
    state_value *state = &scope->states[variable];
    assert(state->value.container == PIN_CONTAINER_SET);
    pin_array *set = state->value.ptr;
    int result = pin_set_add_owned(&state->storage, &set, value);
    assert(!result);
    (void)result;
    state->value.ptr = set;
    sync_state(scope);
}
/* Names live as long as the running call's payload. */
static const char *own_name(void *owner, const char *name)
{
    execution_program *scope = owner;
    if (!name) {
        return NULL;
    }
    assert(scope->owner_program->running);
    size_t size = strlen(name) + 1;
    char *copy = arena_alloc(scope->owner_program->payload, size);
    memcpy(copy, name, size);
    return copy;
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
    for (int i = 0; i < acount(p->imports); i++) {
        execution_program_free(p->imports[i]);
    }
    afree(p->imports);
    if (p->states) {
        for (int i = 0; i < p->module->plan.state_count; i++) {
            state_free(&p->states[i]);
        }
    }
    free(p->states);
    free(p->values);
    free(p->layouts);
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
    execution_module_free(p->module);
    free(p->name);
    free(p);
}
static void record_failure(const behavior_function *f, const behavior_function_context *ctx, int status)
{
    execution_program *p = ctx->inspection_user;
    if (p->failure.status) {
        return;
    }
    const execution_module_plan *scope = &((execution_program *)ctx->value_owner)
                                              ->module->plan;
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
bool program_call_views_allowed(const behavior_function_context *ctx)
{
    return !ctx->inspect && !ctx->watch && !ctx->trace && !ctx->source_pause &&
           !ctx->source_capture && !ctx->call_enter && !ctx->call_leave &&
           ((!ctx->bind_instance && !ctx->commit_instance && !ctx->failure_observer) ||
            (ctx->bind_instance == program_bind_instance &&
             ctx->commit_instance == program_commit_instance &&
             ctx->failure_observer == record_failure));
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
static int synchronous_wait(behavior_function_context *context, int)
{
    context->error_message = "a synchronous call must finish without waiting";
    return FLOW_ERR_STATE;
}
static int synchronous_task(behavior_function_context *context, int)
{
    context->error_message = "a synchronous call cannot start a task";
    const behavior_function *function = context->source_function;
    const behavior_function_instruction
        *call = &function->instructions[context->activation->pc - 1];
    context->failure = BEHAVIOR_FUNCTION_COMMAND;
    context->node_id = call->node_id;
    context->error_source = call->source;
    context->error_frame = call->frame;
    context->error_stack = call->stack;
    /* Task refusal returns through the call scheduler, outside the normal
     * instruction failure observer. Capture its diagnostic before unwinding. */
    record_failure(function, context, FLOW_ERR_STATE);
    return FLOW_ERR_STATE;
}
int execution_program_call(execution_program *p, uint64_t id, int *result, unsigned budget)
{
    assert(p && result && p->owner_program == p);
    if (p->running) {
        return FLOW_ERR_STATE;
    }
    const behavior_function *function = NULL;
    for (int i = 0; i < p->module->plan.function_count; i++) {
        const execution_module_function *candidate = &p->module->plan.functions[i];
        if (candidate->id == id &&
            candidate->kind == EXECUTION_FUNCTION_AUTHORED && !candidate->parent) {
            function = candidate->body;
            break;
        }
    }
    if (!function) {
        return FLOW_ERR_ARGUMENT;
    }
    int parameters = 0, results = 0;
    behavior_function_arguments(function, &parameters);
    const pin_value *signature = behavior_function_results(function, &results);
    if (parameters || results > 1 ||
        (results && (signature[0].type != PIN_I32 || signature[0].container != PIN_CONTAINER_SINGLE ||
                     behavior_function_kind(function, BEHAVIOR_BANK_RESULT, 0) !=
                         BEHAVIOR_VALUE))) {
        return FLOW_ERR_ARGUMENT;
    }
    arena storage = { 0 }, payload = { 0 };
    arena_init_collectable(&payload);
    pin_value *values = arena_alloc(&storage, (size_t)(results + 1) * sizeof *values);
    if (results) {
        values[0] = signature[0];
    }
    behavior_function_prepare_results(function, values, &storage);
    behavior_function_activation activation = { 0 };
    bool resumable = behavior_function_resumable(function);
    if (resumable) {
        behavior_function_activation_init(function, &activation);
    }
    activation.natives = p->natives;
    behavior_function_context context = { .activation = resumable ? &activation : NULL,
                                          .variables = p->values,
                                          .variable_storage = p->layouts,
                                          .value_owner = p,
                                          .value_storage = &payload,
                                          .managed_heap = p->managed,
                                          .module = p->module,
                                          .program = p,
                                          .result_storage = &payload,
                                          .results = values,
                                          .budget = budget ? budget : 10000,
                                          .instance_time = &p->time,
                                          .services = p->services,
                                          .store_value = store_value,
                                          .add_set_value = add_set_value,
                                          .own_type_name = own_name,
                                          .bind_instance = program_bind_instance,
                                          .commit_instance = program_commit_instance,
                                          .failure_observer = record_failure,
                                          .begin_command = synchronous_wait,
                                          .start_task = synchronous_task,
                                          .program_name = p->name,
                                          .inspection_user = p };
    int status = FLOW_ERR_ARGUMENT;
    behavior_function_bind_storage(&context, &payload);
    if (behavior_function_bind(function, &context, p->services, 0)) {
        behavior_function_prepare_host(function, &context);
        p->running = true;
        p->payload = &payload;
        program_bind_instance(function, &context);
        status = behavior_function_run(function, &context, p->module->backend != BEHAVIOR_BYTECODE);
        program_commit_instance(&context);
        p->running = false;
        p->payload = NULL;
        /* A function that ends without `return` still completed: void functions
         * and named results end this way. Only a pending status means it waited. */
        if (status > 0) {
            const behavior_function *active = function;
            behavior_function_context *failed = behavior_function_active_context(&context, &active);
            failed->error_message = "a synchronous call must finish without "
                                    "waiting";
            status = FLOW_ERR_STATE;
            record_failure(active, failed, status);
        }
        if (status == FLOW_OK) {
            *result = results ? context.results[0].i : 0;
        }
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    arena_free(&payload);
    program_apply_services(p);
    program_collect_states(p);
    return status;
}
const pin_value *execution_program_state(const execution_program *p, uint64_t id)
{
    for (int i = 0; i < p->module->plan.state_count; i++) {
        if (p->module->plan.states[i].id == id) {
            return &p->states[i].value;
        }
    }
    return NULL;
}
int execution_program_set_state(execution_program *p, uint64_t id, pin_value value)
{
    if (behavior_managed_storage_owner_error(&p->owner_program->managed->storage, &value)) {
        return FLOW_ERR_ARGUMENT;
    }
    for (int i = 0; i < p->module->plan.state_count; i++) {
        if (p->module->plan.states[i].id == id) {
            if (!program_same_type(p->module->plan.states[i].initial, value)) {
                return FLOW_ERR_ARGUMENT;
            }
            int status = state_assign(p, &p->states[i], &value);
            sync_state(p);
            if (!p->owner_program->running) {
                program_collect_states(p);
            }
            return status;
        }
    }
    return FLOW_ERR_ARGUMENT;
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
/* Keeps each value next can keep. A reload always applies: a value that
 * cannot convert keeps next's initializer. */
static void preserve_states(execution_program *next, const execution_program *before)
{
    for (int i = 0; i < next->module->plan.state_count; i++) {
        const execution_module_state *state = &next->module->plan.states[i];
        int old = prior_state(before->module, state);
        if (old < 0) {
            continue;
        }
        arena scratch = { 0 };
        pin_value value;
        /* A staged candidate deliberately retains the original family's
         * identities; its stable root snapshots pin them until adoption. */
        if (kept_value(
                &scratch,
                &before->module->plan.states[old],
                state,
                before->states[old].value,
                &value
            )) {
            int status = state_assign((execution_program *)before, &next->states[i], &value);
            assert(!status);
            (void)status;
        }
        arena_free(&scratch);
    }
    sync_state(next);
}
/* Resets each value a candidate scope keeps that reaches a record layout the
 * replacement changed to its initializer. Kept cells are shared with the
 * running program, which may change them until commit. */
static void reset_stale(execution_program *scope)
{
    const execution_module *module = scope->module;
    layout_check c = { .module = module };
    for (int i = 0; i < module->plan.state_count; i++) {
        if (!pointees_current(&c, scope->states[i].value)) {
            int status = state_assign(
                scope,
                &scope->states[i],
                &module->plan.states[i].initial
            );
            assert(!status);
            (void)status;
        }
    }
    sync_state(scope);
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
        replacement_root(candidate, scope->states[i].value);
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
    assert(!p->running && module);
    service_binding *services = NULL;
    for (int i = 0; i < acount(p->services); i++) {
        apush(services, p->services[i]);
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
    execution_program *next = execution_program_new(module, services);
    afree(services);
    if (!next) {
        return NULL;
    }
    execution_program_set_name(next, p->name);
    if (!restart) {
        preserve_states(next, p);
        for (int n = 0; n < acount(next->imports); n++) {
            for (int o = 0; o < acount(p->imports); o++) {
                const execution_module *old = p->imports[o]->module;
                const execution_module *now = next->imports[n]->module;
                if (old == now || (now->plan.id && now->plan.id == old->plan.id)) {
                    preserve_states(next->imports[n], p->imports[o]);
                    break;
                }
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
    assert(p && next && !p->running && !next->running);
    assert(next->replacement_of == p && next->replacement_module == p->module);
    reset_stale_candidate(next);
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
    p->owner_program = p;
    for (int i = 0; i < acount(p->imports); i++) {
        p->imports[i]->owner_program = p;
    }
    *next = old;
    next->owner_program = next;
    for (int i = 0; i < acount(next->imports); i++) {
        next->imports[i]->owner_program = next;
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
    for (int i = 0; i < acount(p->imports); i++) {
        bind_service(p->imports[i], name, value);
    }
}
void program_apply_services(execution_program *p)
{
    p = p->owner_program;
    if (p->running) {
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
    if (!p->running) {
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
    if (!p->running) {
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

void execution_program_set_natives(execution_program *p, const execution_natives *natives)
{
    p->natives = natives;
    for (int i = 0; i < acount(p->imports); i++) {
        execution_program_set_natives(p->imports[i], natives);
    }
}
void execution_program_set_name(execution_program *p, const char *name)
{
    assert(!p->running);
    free(p->name);
    p->name = name ? xstrdup(name) : NULL;
}

const execution_module *execution_program_module(const execution_program *p)
{
    assert(p);
    return p->module;
}
execution_memory_usage execution_program_memory_usage(const execution_program *p)
{
    assert(p && !p->owner_program->running);
    execution_memory_usage result = { 0 };
    for (const behavior_managed_heap *heap = p->owner_program->managed; heap;
         heap = heap->previous) {
        size_t bytes, capacity;
        arena_usage(&heap->storage, &bytes, &capacity);
        result.managed_bytes += bytes;
        result.managed_capacity += capacity;
    }
    p = p->owner_program;
    for (int scope = -1; scope < acount(p->imports); scope++) {
        const execution_program *program = scope < 0 ? p : p->imports[scope];
        result.state_bank_bytes += (size_t)program->module->plan.state_count *
                                   (sizeof(state_value) + sizeof(pin_value) +
                                    sizeof(behavior_variable_storage));
    }
    return result;
}
