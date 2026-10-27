#include "ecs/execution.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/module_effects.h"
#include "execution/service.h"
#include "execution/wasm_jit.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/query.h"
#include "types/record.h"
#include "types/text.h"
#include "types/trace.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void behavior_jit_free(void *code);

bool behavior_function_static_name(const char **name)
{
    if (!*name) {
        return true;
    }
    const type_desc *type = type_catalog_find(*name);
    if (type) {
        *name = type->name;
        return true;
    }
    static const char *const names[] = {
        "bool",           "i8",     "u8",        "char",
        "unsigned char",  "i16",    "u16",       "short",
        "unsigned short", "int",    "i32",       "u32",
        "unsigned int",   "i64",    "int64_t",   "u64",
        "uint64_t",       "number", "f32",       "f64",
        "float",          "double", "ecs.entity"
    };
    for (int i = 0; i < (int)(sizeof names / sizeof *names); i++) {
        if (!strcmp(*name, names[i])) {
            *name = names[i];
            return true;
        }
    }
    return false;
}
static const char *own_string(arena *a, const char *text)
{
    if (!text) {
        return NULL;
    }
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(a, size);
    memcpy(copy, text, size);
    return copy;
}
typedef struct
{
    arena *storage;
    const pin_record **records;
} value_owner;
static bool own_value(value_owner *owner, pin_value *value)
{
    pin_value source = *value, copy;
    if (source.record) {
        if (!pin_record_valid(source.record) || !pin_value_type(&source)) {
            return false;
        }
        const pin_record *record = NULL;
        for (int i = 0; i < acount(owner->records); i++) {
            if (pin_record_same(owner->records[i], source.record)) {
                record = owner->records[i];
                break;
            }
        }
        if (!record) {
            record = pin_record_snapshot(owner->storage, source.record);
            if (!record) {
                return false;
            }
            apush(owner->records, record);
        }
        source.record = record;
        source.type_name = record->type.name;
    }
    int result = pin_value_snapshot_to_arena(owner->storage, &copy, &source);
    if (result) {
        return false;
    }
    if (!copy.record) {
        behavior_function_static_name(&copy.type_name);
    }
    *value = copy;
    return true;
}
static bool own_instructions(value_owner *owner, behavior_function_instruction *instructions)
{
    arena *a = owner->storage;
    for (int i = 0; i < acount(instructions); i++) {
        behavior_function_instruction *op = &instructions[i];
        op->message = own_string(a, op->message);
        /* Header selectors carry a type discriminator and its name in value,
         * rather than a struct instance. Aggregate locals retain that metadata. */
        if (op->op == BF_TYPE_NAME ||
            (op->op == BF_LOCAL && op->type == BF_POINTER_TYPE &&
             op->value.type == PIN_STRUCT && !op->value.type_name)) {
            if (!behavior_function_static_name(&op->value.str)) {
                op->value.str = own_string(a, op->value.str);
            }
        } else if (op->value.type != PIN_FLOW) {
            if (!own_value(owner, &op->value)) {
                return false;
            }
        }
    }
    return true;
}
static node_access *own_access(arena *a, const node_access *source)
{
    if (!source) {
        return NULL;
    }
    node_access *copy = arena_alloc(a, sizeof *copy);
    *copy = *source;
    uint64_t *reads = arena_alloc(a, (size_t)source->read_count * sizeof *reads);
    uint64_t *writes = arena_alloc(a, (size_t)source->write_count * sizeof *writes);
    if (source->read_count) {
        memcpy(reads, source->reads, (size_t)source->read_count * sizeof *reads);
    }
    if (source->write_count) {
        memcpy(writes, source->writes, (size_t)source->write_count * sizeof *writes);
    }
    copy->reads = reads;
    copy->writes = writes;
    if (source->read_refs) {
        int count = 0;
        while (source->read_refs[count]) {
            count++;
        }
        const uint64_t **refs = arena_alloc(a, (size_t)(count + 1) * sizeof *refs);
        uint64_t *ids = arena_alloc(a, (size_t)count * sizeof *ids);
        for (int i = 0; i < count; i++) {
            ids[i] = *source->read_refs[i];
            refs[i] = &ids[i];
        }
        refs[count] = NULL;
        copy->read_refs = refs;
    }
    return copy;
}
bool behavior_function_own(behavior_function *f)
{
    if (f->frozen) {
        return true;
    }
    arena *a = &f->owned;
    value_owner owner = { .storage = a };
    for (int i = 0; i < acount(f->argument_defaults); i++) {
        if (!own_value(&owner, &f->argument_defaults[i])) {
            goto invalid;
        }
    }
    for (int i = 0; i < acount(f->result_defaults); i++) {
        if (!own_value(&owner, &f->result_defaults[i])) {
            goto invalid;
        }
    }
    for (int i = 0; i < acount(f->results); i++) {
        int index = f->results[i].index;
        if (index < 0 || index >= acount(f->result_defaults) ||
            f->result_defaults[index].type != PIN_STRUCT ||
            f->result_defaults[index].container != PIN_CONTAINER_SINGLE) {
            goto invalid;
        }
        f->results[i].type = pin_value_type(&f->result_defaults[index]);
        if (!f->results[i].type) {
            goto invalid;
        }
    }
    for (int i = 0; i < acount(f->variable_defaults); i++) {
        if (!own_value(&owner, &f->variable_defaults[i])) {
            goto invalid;
        }
    }
    for (int i = 0; i < acount(f->value_roots); i++) {
        if (!own_value(&owner, &f->value_roots[i].shape)) {
            goto invalid;
        }
    }
    for (int i = 0; i < acount(f->suspensions); i++) {
        for (int j = 0; j < f->suspensions[i].root_count; j++) {
            if (!own_value(&owner, &f->suspensions[i].roots[j].shape)) {
                goto invalid;
            }
        }
    }
    if (!own_instructions(&owner, f->instructions)) {
        goto invalid;
    }
    if (!own_instructions(&owner, f->runtime_instructions)) {
        goto invalid;
    }
    if (!own_instructions(&owner, f->runtime_body)) {
        goto invalid;
    }
    for (int i = 0; i < acount(f->locals); i++) {
        behavior_function_local *local = &f->locals[i];
        if (local->offset == SIZE_MAX - 1) {
            local->initial.str = own_string(a, local->initial.str);
        } else {
            if (!own_value(&owner, &local->initial)) {
                goto invalid;
            }
        }
    }
    for (int i = 0; i < acount(f->parameters); i++) {
        if (!own_value(&owner, &f->parameters[i].initial)) {
            goto invalid;
        }
    }
    for (int i = 0; i < acount(f->frames); i++) {
        f->frames[i].source.asset_name = own_string(a, f->frames[i].source.asset_name);
    }
    for (int i = 0; i < acount(f->sources); i++) {
        f->sources[i].asset_name = own_string(a, f->sources[i].asset_name);
    }
    for (int i = 0; i < acount(f->stacks); i++) {
        f->stacks[i].asset_name = own_string(a, f->stacks[i].asset_name);
    }
    for (int i = 0; i < acount(f->watches); i++) {
        if (!own_value(&owner, &f->watches[i].initial)) {
            goto invalid;
        }
    }
    for (int i = 0; i < acount(f->source_watches); i++) {
        if (!own_value(&owner, &f->source_watches[i].initial)) {
            goto invalid;
        }
    }
    for (int i = 0; i < acount(f->data_values); i++) {
        if (!own_value(&owner, &f->data_values[i].initial)) {
            goto invalid;
        }
    }
    for (int i = 0; i < acount(f->command_arguments); i++) {
        if (!own_value(&owner, &f->command_arguments[i])) {
            goto invalid;
        }
    }
    for (int i = 0; i < acount(f->command_results); i++) {
        if (!own_value(&owner, &f->command_results[i])) {
            goto invalid;
        }
    }
    for (int i = 0; i < acount(f->queries); i++) {
        f->queries[i].service = own_string(a, f->queries[i].service);
        f->queries[i].key = own_string(a, f->queries[i].key);
    }
    for (int i = 0; i < acount(f->host_calls); i++) {
        behavior_function_host_call *host = &f->host_calls[i];
        node_host_call *copy = arena_alloc(a, sizeof *copy);
        *copy = *host->call;
        copy->service = own_string(a, copy->service);
        copy->missing_service = own_string(a, copy->missing_service);
        host->call = copy;
        host->access = own_access(a, host->access);
        if (host->outcome_count) {
            uint64_t *ids = arena_alloc(a, (size_t)host->outcome_count * sizeof *ids);
            memcpy(ids, host->outcomes, (size_t)host->outcome_count * sizeof *ids);
            host->outcomes = ids;
        }
    }
    for (int i = 0; i < acount(f->commands); i++) {
        behavior_function_command *command = &f->commands[i];
        node_command *copy = arena_alloc(a, sizeof *copy);
        *copy = *command->command;
        copy->service = own_string(a, copy->service);
        copy->missing_service = own_string(a, copy->missing_service);
        copy->missing_reference = own_string(a, copy->missing_reference);
        copy->failure_message = own_string(a, copy->failure_message);
        command->command = copy;
        command->reference = own_string(a, command->reference);
        if (command->result_pins) {
            int *pins = arena_alloc(a, (size_t)command->result_count * sizeof *pins);
            memcpy(pins, command->result_pins, (size_t)command->result_count * sizeof *pins);
            command->result_pins = pins;
        }
    }
    for (int i = 0; i < acount(f->definitions); i++) {
        if (!behavior_function_own(f->definitions[i])) {
            goto invalid;
        }
    }
    if (f->fast) {
        if (!behavior_function_own(f->fast)) {
            goto invalid;
        }
    }
    afree(owner.records);
    behavior_function_note_derived(f);
    f->frozen = true;
    return true;
invalid:
    afree(owner.records);
    return false;
}

void behavior_function_note_derived(behavior_function *f)
{
    f->binds_world = f->component_access;
    for (int i = 0; i < acount(f->argument_kinds); i++) {
        f->binds_world |= f->argument_kinds[i] == BEHAVIOR_REFERENCE;
    }
    f->builds_results = false;
    for (int i = 0; i < acount(f->host_calls); i++) {
        f->builds_results |= execution_intrinsic_in_result(f->host_calls[i].capability_id);
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
        execution_intrinsic kind = intrinsic
                                     ? execution_intrinsic_kind(host->capability_id)
                                     : 0;
        f->binds_world |= kind == EXECUTION_INTRINSIC_QUERY_BEGIN ||
                          kind == EXECUTION_INTRINSIC_QUERY_NEXT ||
                          kind == EXECUTION_INTRINSIC_QUERY_SOURCE ||
                          kind == EXECUTION_INTRINSIC_QUERY_TARGET ||
                          kind == EXECUTION_INTRINSIC_COMPONENT_HAS ||
                          kind == EXECUTION_INTRINSIC_COMPONENT_MEMBERSHIP ||
                          kind == EXECUTION_INTRINSIC_COMPONENT_POINTER ||
                          kind == EXECUTION_INTRINSIC_COMPONENT_SNAPSHOT;
        if (kind == EXECUTION_INTRINSIC_REFERENCE_MAKE &&
            f->data_values[host->first + 2].initial.i == LANGUAGE_REFERENCE_COMPONENT) {
            f->binds_world = true;
        }
    }
    /* A result moves only when execution_builds_safely proves that nothing
     * else changes or shares what the function builds there. */
    f->builds_results = f->builds_results && execution_builds_safely(f);
}

static bool preserves_components_uncached(const behavior_function *f, const uint64_t *components, int count)
{
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        if (f->instructions[pc].op == BF_COMPONENT_WRITE) {
            /* The publication callback is an observable storage boundary,
             * independently of optional producer component-write metadata. */
            return false;
        }
    }
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < acount(f->component_writes); j++) {
            if (f->component_writes[j] == components[i]) {
                return false;
            }
        }
    }
    for (int i = 0; i < acount(f->host_calls); i++) {
        const node_access *a = f->host_calls[i].access;
        if (!a || (a->flags & NODE_EFFECT_STRUCTURAL)) {
            return false;
        }
        for (int j = 0; j < a->write_count; j++) {
            for (int k = 0; k < count; k++) {
                if (a->writes[j] == components[k]) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool behavior_function_preserves_components(const behavior_function *f, const uint64_t *components, int count)
{
    if (!f || !f->batchable) {
        return false;
    }
    for (int i = 0; i < acount(f->component_proofs); i++) {
        const behavior_component_proof *proof = &f->component_proofs[i];
        if (acount(proof->components) != count) {
            continue;
        }
        bool same = true;
        for (int j = 0; j < count; j++) {
            if (proof->components[j] != components[j]) {
                same = false;
                break;
            }
        }
        if (same) {
            return proof->result;
        }
    }
    /* Published functions are immutable. Keep the proof cache mutable so
     * repeated query-scope checks do not rescan a function generation. */
    behavior_component_proof proof = {
        .result = preserves_components_uncached(f, components, count)
    };
    for (int i = 0; i < count; i++) {
        apush(proof.components, components[i]);
    }
    apush(((behavior_function *)f)->component_proofs, proof);
    return proof.result;
}
bool behavior_function_batchable(const behavior_function *f)
{
    return f && f->batchable;
}
bool behavior_function_uses_storage(const behavior_function *f)
{
    return f->uses_storage;
}
behavior_value_kind behavior_function_kind(const behavior_function *f, behavior_value_bank bank, int index)
{
    assert(index >= 0);
    const behavior_value_kind *values = NULL;
    switch (bank) {
        case BEHAVIOR_BANK_SLOT: values = f->slot_kinds; break;
        case BEHAVIOR_BANK_ARGUMENT: values = f->argument_kinds; break;
        case BEHAVIOR_BANK_RESULT: values = f->result_kinds; break;
        case BEHAVIOR_BANK_COMMAND_ARGUMENT:
            values = f->command_argument_kinds;
            break;
        case BEHAVIOR_BANK_COMMAND_RESULT:
            values = f->command_result_kinds;
            break;
        case BEHAVIOR_BANK_LOCAL:
            assert(index < acount(f->locals));
            return f->locals[index].kind;
        case BEHAVIOR_BANK_DATA:
            assert(index < acount(f->data_values));
            return f->data_values[index].kind;
        default: assert(false);
    }
    assert(index < acount(values));
    return values[index];
}
bool behavior_function_observed(const behavior_function *function)
{
    return function->edges || function->watches || function->source_watches;
}

bool behavior_function_has_invocations(const behavior_function *function)
{
    return function->calls != NULL;
}
void behavior_function_free(behavior_function *f)
{
    if (!f) {
        return;
    }
    for (int i = 0; i < acount(f->definitions); i++) {
        behavior_function_free(f->definitions[i]);
    }
    afree(f->definitions);
    afree(f->reductions);
    afree(f->query_diagnostics);
    execution_artifact_free(f->artifact);
    afree(f->points);
    afree(f->watches);
    afree(f->source_points);
    afree(f->source_watches);
    afree(f->edges);
    behavior_function_free(f->fast);
    afree(f->suspensions);
    afree(f->value_roots);
    afree(f->instructions);
    afree(f->runtime_instructions);
    afree(f->runtime_body);
    free(f->runtime_map);
    free(f->integer_reports);
    afree(f->slots);
    afree(f->slot_kinds);
    afree(f->argument_kinds);
    afree(f->result_kinds);
    afree(f->command_argument_kinds);
    afree(f->command_result_kinds);
    afree(f->locals);
    afree(f->parameters);
    afree(f->argument_defaults);
    afree(f->result_defaults);
    afree(f->variable_defaults);
    afree(f->frames);
    afree(f->sources);
    afree(f->stacks);
    afree(f->results);
    afree(f->queries);
    afree(f->host_calls);
    afree(f->state_selections);
    afree(f->calls);
    afree(f->component_writes);
    for (int i = 0; i < acount(f->component_proofs); i++) {
        afree(f->component_proofs[i].components);
    }
    afree(f->component_proofs);
    afree(f->parallel_reads);
    afree(f->components);
    afree(f->data_values);
    afree(f->commands);
    afree(f->command_arguments);
    afree(f->command_results);
    arena_free(&f->owned);
    free(f);
}
bool behavior_function_compile_system(behavior_function *f)
{
    assert(f && f->batchable && f->artifact->code && !f->artifact->system_code);
    f->artifact->system_code = behavior_function_jit_system(f);
    return f->artifact->system_code != NULL;
}
void *behavior_function_system_entry(const behavior_function *f)
{
    return f->artifact->system_code;
}
static bool compile_artifact(behavior_function *f)
{
    for (int i = 0; i < acount(f->definitions); i++) {
        if (!behavior_function_compile(f->definitions[i])) {
            return false;
        }
    }
#ifdef __EMSCRIPTEN__
    return behavior_function_compile_wasm(f);
#else
    if (acount(f->suspensions)) {
        assert(!f->artifact->resume_addresses);
        f->artifact->resume_addresses = xcalloc(
            (size_t)acount(f->instructions),
            sizeof *f->artifact->resume_addresses
        );
    }
    f->artifact->code = behavior_function_jit(f, false);
    if (!f->artifact->code || !f->fast) {
        return f->artifact->code != NULL;
    }
    if (!behavior_function_compile(f->fast)) {
        return false;
    }
    f->artifact->dispatch = behavior_function_guard(f);
    return f->artifact->dispatch != NULL;
#endif
}
bool behavior_function_compile_inspection(const behavior_function *f)
{
    execution_artifact *artifact = f->artifact;
    unsigned state = atomic_load_explicit(&artifact->inspection_state, memory_order_acquire);
    if (state == EXECUTION_INSPECTION_COMPILED) {
        return true;
    }
    if (state != EXECUTION_INSPECTION_UNCOMPILED ||
        !atomic_compare_exchange_strong(&artifact->inspection_state, &state, EXECUTION_INSPECTION_COMPILING)) {
        /* Another activation may be compiling. Its caller can use bytecode
         * immediately without waiting or reading partially published maps. */
        return false;
    }
    artifact->inspection_addresses = xcalloc(
        (size_t)acount(f->instructions),
        sizeof *artifact->inspection_addresses
    );
    artifact->inspection_code = behavior_function_jit(f, true);
    bool compiled = artifact->inspection_code != NULL;
    atomic_store_explicit(
        &artifact->inspection_state,
        compiled ? EXECUTION_INSPECTION_COMPILED : EXECUTION_INSPECTION_FAILED,
        memory_order_release
    );
    return compiled;
}
bool behavior_function_compile_scoped(const behavior_function *f)
{
#if CHIBI_HAVE_SJLIT && !defined(__EMSCRIPTEN__)
    execution_artifact *artifact = f->artifact;
    unsigned state = atomic_load_explicit(&artifact->scoped_state, memory_order_acquire);
    if (state == EXECUTION_INSPECTION_COMPILED) {
        return true;
    }
    if (behavior_execution_scope_cancelled() || state != EXECUTION_INSPECTION_UNCOMPILED ||
        !atomic_compare_exchange_strong(&artifact->scoped_state, &state, EXECUTION_INSPECTION_COMPILING)) {
        return false;
    }
    artifact->scoped_addresses = xcalloc(
        (size_t)acount(f->instructions),
        sizeof *artifact->scoped_addresses
    );
    artifact->scoped_code = behavior_function_jit_scoped(f);
    bool compiled = artifact->scoped_code != NULL;
    bool cancelled = !compiled && behavior_execution_scope_cancelled();
    if (!compiled) {
        free(artifact->scoped_addresses);
        artifact->scoped_addresses = NULL;
    }
    atomic_store_explicit(
        &artifact->scoped_state,
        compiled    ? EXECUTION_INSPECTION_COMPILED
        : cancelled ? EXECUTION_INSPECTION_UNCOMPILED
                    : EXECUTION_INSPECTION_FAILED,
        memory_order_release
    );
    return compiled;
#else
    (void)f;
    return false;
#endif
}
bool behavior_function_compile(behavior_function *f)
{
    if (f->artifact->compilation_attempted) {
        return f->artifact->compiled;
    }
    f->artifact->compilation_attempted = true;
    f->artifact->compiled = compile_artifact(f);
    return f->artifact->compiled;
}
void *behavior_function_entry(const behavior_function *f)
{
    return f ? (f->artifact->dispatch ? f->artifact->dispatch : f->artifact->code)
             : NULL;
}
bool behavior_function_interpreted(const behavior_function *f)
{
    return f->artifact->interpreted;
}
bool behavior_function_pure_depth(const behavior_function *f, unsigned depth)
{
    return !f->pure_depth_limit || depth < f->pure_depth_limit;
}
int behavior_function_source_count(const behavior_function *f)
{
    return acount(f->sources);
}
pin_type behavior_function_slot_type(const behavior_function *f, int slot)
{
    assert(f && slot >= 0 && slot < acount(f->slots));
    return f->slots[slot];
}
const behavior_function_source *behavior_function_instruction_source(const behavior_function *f, int pc)
{
    assert(f);
    if (pc < 0 || pc >= acount(f->instructions)) {
        return NULL;
    }
    int source = f->instructions[pc].source;
    return source >= 0 && source < acount(f->sources) ? &f->sources[source] : NULL;
}
const behavior_function_source *behavior_function_source_at(const behavior_function *f, int source)
{
    return &f->sources[source];
}

static const char *copy_string(void *storage, const char *text)
{
    if (!text) {
        return NULL;
    }
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(storage, size);
    memcpy(copy, text, size);
    return copy;
}
static const pin_string *copy_text(void *storage, const pin_string *text)
{
    if (!text) {
        return NULL;
    }
    pin_value value;
    const char *error = pin_value_text(storage, &value, text->bytes, text->length);
    assert(!error);
    (void)error;
    return value.text;
}
const pin_string *behavior_function_intern_text(
    behavior_function_context *context,
    const ecs_execution_view *world,
    const pin_string *text
)
{
    pin_value source = { .type = PIN_STR, .text = text }, result;
    const char *bytes;
    context->error_message = pin_text_cstring(context->value_storage, source, &bytes, NULL);
    if (context->error_message) {
        return NULL;
    }
    const char *interned = world->intern_string(world, bytes);
    context->error_message = pin_value_text(
        context->value_storage,
        &result,
        interned,
        pin_text(source).length
    );
    return context->error_message ? NULL : result.text;
}
static pin_array *copy_array(void *storage, const pin_array *array)
{
    pin_array *copy = pin_array_snapshot_to_arena(storage, array);
    assert(!array || copy);
    return copy;
}
void behavior_function_bind_storage(behavior_function_context *ctx, arena *storage)
{
    ctx->value_storage = storage;
    ctx->result_storage = storage;
    ctx->copy_string = copy_string;
    ctx->copy_text = copy_text;
    ctx->copy_array = copy_array;
}

void behavior_function_prepare_results(const behavior_function *f, pin_value *results, arena *storage)
{
    for (int i = 0; i < acount(f->results); i++) {
        behavior_function_result r = f->results[i];
        void *data = arena_alloc(storage, r.type->size);
        memset(data, 0, r.type->size);
        pin_value value = f->result_defaults[r.index];
        value.ptr = NULL;
        if (value.record) {
            int status = pin_value_snapshot_to_arena(storage, &value, &value);
            assert(!status);
            (void)status;
        } else {
            value.type_name = r.type->name;
        }
        value.ptr = data;
        pin_arena_trace_struct(storage, data, pin_value_type(&value));
        results[r.index] = value;
    }
}

const behavior_function_source *behavior_function_stack_at(const behavior_function *f, int stack)
{
    return &f->stacks[stack];
}

size_t behavior_function_watch_size(const behavior_function *f)
{
    return f->watch_size;
}

static void read_component(uint64_t **ids, uint64_t id)
{
    for (int i = 0; i < acount(*ids); i++) {
        if ((*ids)[i] == id) {
            return;
        }
    }
    apush(*ids, id);
}
static void read_components(const behavior_function *f, const behavior_function ***seen, uint64_t **ids)
{
    for (int i = 0; i < acount(*seen); i++) {
        if ((*seen)[i] == f) {
            return;
        }
    }
    apush(*seen, f);
    for (int i = 0; i < acount(f->components); i++) {
        read_component(ids, f->components[i].id);
    }
    if (acount(f->queries)) {
        read_component(ids, 0);
    }
    for (int i = 0; i < acount(f->host_calls); i++) {
        const node_access *access = f->host_calls[i].access;
        if (!access) {
            read_component(ids, 0);
            continue;
        }
        for (int j = 0; j < access->read_count; j++) {
            read_component(ids, access->reads[j]);
        }
        for (int j = 0; access->read_refs && access->read_refs[j]; j++) {
            read_component(ids, *access->read_refs[j]);
        }
    }
    for (int i = 0; i < acount(f->calls); i++) {
        read_components(f->calls[i].function, seen, ids);
    }
}
int behavior_function_component_reads(const behavior_function *f, uint64_t *out, int capacity)
{
    assert(f && capacity >= 0 && (!capacity || out));
    const behavior_function **seen = NULL;
    uint64_t *ids = NULL;
    read_components(f, &seen, &ids);
    int count = acount(ids);
    for (int i = 0; i < count && i < capacity; i++) {
        out[i] = ids[i];
    }
    afree(seen);
    afree(ids);
    return count;
}

void behavior_managed_heap_init(behavior_managed_heap *heap)
{
    *heap = (behavior_managed_heap){ 0 };
    arena_init_collectable(&heap->storage);
    heap->owner = heap;
}
void behavior_managed_heap_free(behavior_managed_heap *heap)
{
    for (int i = 0; i < acount(heap->roots); i++) {
        *heap->roots[i] = (pin_value){ 0 };
    }
    arena_free(&heap->storage);
    afree(heap->roots);
    if (heap->previous) {
        behavior_managed_heap_free(heap->previous);
        free(heap->previous);
    }
    *heap = (behavior_managed_heap){ 0 };
}
static behavior_managed_heap *managed_owner(behavior_managed_heap *heap)
{
    while (heap->owner && heap->owner != heap) {
        heap = heap->owner;
    }
    return heap;
}
/* A single value that is neither an aggregate nor a pointer reaches no other
 * value, so ownership scans and traces need no work list for it. */
static bool managed_leaf(const pin_value *value)
{
    return value->container == PIN_CONTAINER_SINGLE && value->type != PIN_STRUCT &&
           value->type != PIN_UNION && value->type != PIN_POINTER;
}
/* Inline record layouts currently resolve nested types through the mutable
 * catalog. Such shapes cannot escape a module generation in a managed heap. */
static const char *managed_value_error(behavior_managed_heap *heap, const pin_value *root, bool restricted)
{
    if (managed_leaf(root)) {
        return NULL;
    }
    typedef struct
    {
        pin_value value;
        int depth;
    } pending_value;
    pending_value *pending = NULL;
    const pin_query **queries = NULL;
    const char *error = NULL;
    apush(pending, ((pending_value){ *root, 0 }));
    while (acount(pending) && !error) {
        pending_value current = apop(pending);
        pin_value value = current.value;
        if (current.depth >= 64) {
            error = "Managed ownership scan exceeds value nesting limit";
            break;
        }
#define PUSH_VALUE(v)                                                          \
    apush(pending, ((pending_value){ (v), current.depth + 1 }))
        if (heap && (value.type == PIN_POINTER || value.type == PIN_QUERY) &&
            value.container == PIN_CONTAINER_SINGLE && value.ptr) {
            bool owned = false;
            for (behavior_managed_heap *scope = heap; scope; scope = scope->previous) {
                if (arena_contains(&scope->storage, value.ptr)) {
                    owned = value.type == PIN_QUERY
                              ? ((const pin_query *)value.ptr)->storage ==
                                    &scope->storage
                              : ((const pin_pointer *)value.ptr)->storage ==
                                    &scope->storage;
                    break;
                }
            }
            if (!owned) {
                error = value.type == PIN_QUERY
                          ? "Queries cannot cross heap owners"
                          : "Managed pointers cannot cross heap owners";
                break;
            }
        }
        if (heap && value.type == PIN_QUERY &&
            value.container == PIN_CONTAINER_SINGLE && value.ptr) {
            const pin_query *query = value.ptr;
            bool seen = false;
            for (int i = 0; i < acount(queries); i++) {
                seen |= queries[i] == query;
            }
            if (seen) {
                continue;
            }
            apush(queries, query);
            for (int i = 0; i < query->count; i++) {
                PUSH_VALUE(query->values[i]);
            }
        }
        bool collection_header = value.container != PIN_CONTAINER_SINGLE &&
                                 value.type_name &&
                                 !strncmp(value.type_name, "__collection_", 13);
        if (restricted && value.type == PIN_STRUCT && !value.record &&
            !value.schema && !collection_header) {
            error = "Managed values require authored record layouts";
            break;
        }
        if (value.type == PIN_STRUCT &&
            (value.container == PIN_CONTAINER_SINGLE || value.record)) {
            const type_desc *layout = pin_value_type(&value);
            if (!layout) {
                error = "Managed value requires a resolved record layout";
                break;
            }
            for (int i = 0; i < layout->field_count; i++) {
                const type_field *field = &layout->fields[i];
                if (restricted && !value.schema && field->type == PIN_STRUCT) {
                    error = "Managed pointers do not yet support inline nested "
                            "record layouts";
                    break;
                }
                if (!heap || value.container != PIN_CONTAINER_SINGLE || !value.ptr) {
                    continue;
                }
                pin_value member = { .type = field->type,
                                     .type_name = field->type_name,
                                     .schema = value.schema };
                const void *data = (const char *)value.ptr + field->offset;
                if (field->type == PIN_STRUCT) {
                    member.ptr = (void *)data;
                } else if (field->type == PIN_POINTER || field->type == PIN_QUERY || field->type == PIN_UNION) {
                    memcpy(&member.ptr, data, sizeof member.ptr);
                } else {
                    continue;
                }
                PUSH_VALUE(member);
            }
        }
        if (value.container == PIN_CONTAINER_MAP) {
            const pin_map *map = value.ptr;
            for (int i = 0; map && i < map->count; i++) {
                PUSH_VALUE(map->entries[i].key);
                PUSH_VALUE(map->entries[i].value);
            }
        } else if (value.container != PIN_CONTAINER_SINGLE) {
            const pin_array *array = value.ptr;
            for (int i = 0; array && i < array->count; i++) {
                PUSH_VALUE(pin_array_get(array, i));
            }
        } else if (value.type == PIN_UNION) {
            const pin_union *sum = value.ptr;
            for (int i = 0; sum && i < sum->count; i++) {
                PUSH_VALUE(sum->values[i]);
            }
        }
#undef PUSH_VALUE
    }
    afree(pending);
    afree(queries);
    return error;
}
const char *behavior_managed_value_error(const pin_value *value)
{
    return managed_value_error(NULL, value, true);
}
const char *behavior_managed_storage_value_error(arena *storage, const pin_value *value)
{
    /* C guarantees a struct and its initial member share an address. */
    behavior_managed_heap *heap = (behavior_managed_heap *)storage;
    return managed_value_error(managed_owner(heap), value, true);
}
const char *behavior_managed_storage_owner_error(arena *storage, const pin_value *value)
{
    return managed_value_error(managed_owner((behavior_managed_heap *)storage), value, false);
}
const char *behavior_managed_heap_root(behavior_managed_heap *heap, pin_value *value)
{
    assert(heap && value);
    heap = managed_owner(heap);
    const char *error = managed_value_error(heap, value, true);
    if (error) {
        return error;
    }
    bool registered = false;
    for (behavior_managed_heap *scope = heap; scope; scope = scope->previous) {
        for (int i = 0; i < acount(scope->roots); i++) {
            registered |= scope->roots[i] == value;
        }
    }
    pin_value retained;
    if (pin_value_snapshot_to_arena(&heap->storage, &retained, value)) {
        return "Cannot retain managed host root";
    }
    *value = retained;
    if (!registered) {
        apush(heap->roots, value);
    }
    return NULL;
}
void behavior_managed_heap_unroot(behavior_managed_heap *heap, pin_value *value)
{
    heap = managed_owner(heap);
    for (behavior_managed_heap *scope = heap; scope; scope = scope->previous) {
        for (int i = 0; i < acount(scope->roots); i++) {
            if (scope->roots[i] == value) {
                /* The retained copy is garbage once unregistered. */
                *value = (pin_value){ 0 };
                adel(scope->roots, i);
                return;
            }
        }
    }
    assert(false);
}
/* Persistent state and host roots can live in ordinary arenas, outside the
 * collection. Walk their value containers explicitly to discover heap edges. */
void behavior_managed_heap_trace_value(const pin_value *root, arena_marker mark, void *user)
{
    if (managed_leaf(root)) {
        pin_value_trace(root, mark, user);
        return;
    }
    pin_value *pending = NULL;
    apush(pending, *root);
    while (acount(pending)) {
        pin_value value = apop(pending);
        pin_value_trace(&value, mark, user);
        if (value.container == PIN_CONTAINER_MAP) {
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
                const void *data = (const char *)value.ptr + field->offset;
                pin_value member = { .type = field->type,
                                     .type_name = field->type_name,
                                     .schema = value.schema };
                if (field->type == PIN_STRUCT) {
                    member.ptr = (void *)data;
                } else if (field->type == PIN_POINTER || field->type == PIN_QUERY || field->type == PIN_UNION) {
                    memcpy(&member.ptr, data, sizeof member.ptr);
                } else {
                    continue;
                }
                apush(pending, member);
            }
        }
    }
    afree(pending);
}
void behavior_managed_heap_trace_roots(const behavior_managed_heap *heap, arena_marker mark, void *user)
{
    for (const behavior_managed_heap *scope = heap; scope; scope = scope->previous) {
        for (int i = 0; i < acount(scope->roots); i++) {
            behavior_managed_heap_trace_value(scope->roots[i], mark, user);
        }
    }
}
void behavior_managed_heap_collect(
    behavior_managed_heap *heap,
    const behavior_function *function,
    const behavior_function_context *context
)
{
    heap = managed_owner(heap);
    arena **storages = NULL;
    for (behavior_managed_heap *scope = heap; scope; scope = scope->previous) {
        apush(storages, &scope->storage);
    }
    arena_collection collection;
    arena_collect_begin(&collection, storages, acount(storages));
    behavior_managed_heap_trace_roots(heap, arena_collect_mark, &collection);
    if (function && context) {
        behavior_function_trace_values(function, context, arena_collect_mark, &collection);
    }
    arena_collect_end(&collection);
    afree(storages);
}
