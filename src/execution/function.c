#include "ecs/execution.h"
#include "execution/borrow.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/managed.h"
#include "execution/module_effects.h"
#include "execution/service.h"
#include "execution/status.h"
#include "execution/wasm_jit.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/query.h"
#include "types/record.h"
#include "types/schema.h"
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
    const char *error = pin_value_text_share(
        storage,
        &value,
        (pin_value){ .type = PIN_STR, .text = text }
    );
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
static behavior_managed_heap *managed_owner(behavior_managed_heap *);
static arena *array_storage(behavior_function_context *context)
{
    if (!context->managed_heap || !context->managed_heap->storage.collectable) {
        context->error_message = "Dynamic arrays require an initialized "
                                 "caller-owned managed heap";
        return NULL;
    }
    return &managed_owner(context->managed_heap)->storage;
}
pin_array *behavior_function_copy_array(behavior_function_context *context, const pin_array *source)
{
    arena *storage = array_storage(context);
    if (!storage) {
        return NULL;
    }
    if (!source) {
        context
            ->error_message = "Array copy requires a materialized typed array";
        return NULL;
    }
    pin_value input = { .type = source->element_type,
                        .type_name = source->type_name,
                        .record = source->record,
                        .container = source->pointer ? PIN_CONTAINER_VIEW
                                                     : PIN_CONTAINER_ARRAY,
                        .ptr = (void *)source },
              value;
    const char *error = behavior_managed_storage_value_error(storage, &input);
    if (error || pin_value_collection_copy(storage, &value, &input)) {
        context->error_message = error ? error : "Cannot retain dynamic array";
        return NULL;
    }
    behavior_function_collection_retain(context, &value);
    return value.ptr;
}
typedef struct
{
    behavior_function_context *from, *to;
    bool drop;
} collection_transfer;
static bool collection_owner_same(pin_value a, pin_value b)
{
    return a.ptr == b.ptr && a.container == b.container;
}
static void collection_retain(const pin_value *value, void *user)
{
    behavior_function_context *context = user;
    if (!value->ptr) {
        return;
    }
    for (int i = 0; i < acount(context->collection_owners); i++) {
        if (collection_owner_same(context->collection_owners[i], *value)) {
            return;
        }
    }
    apush(context->collection_owners, *value);
}
void behavior_function_collection_retain(behavior_function_context *context, const pin_value *value)
{
    pin_value_collection_visit_owners(value, collection_retain, context);
}
static void collection_remove(const pin_value *value, void *user)
{
    collection_transfer *transfer = user;
    pin_value *owners = transfer->from->collection_owners;
    for (int i = 0; i < acount(owners); i++) {
        if (collection_owner_same(owners[i], *value)) {
            owners[i] = owners[acount(owners) - 1];
            asetlen(owners, acount(owners) - 1);
            if (transfer->to) {
                collection_retain(value, transfer->to);
            }
            if (transfer->drop) {
                pin_value_collection_release(value);
            }
            return;
        }
    }
    if (transfer->to) {
        collection_retain(value, transfer->to);
    }
}
void behavior_function_collection_drop(behavior_function_context *context, const pin_value *value)
{
    collection_transfer transfer = { .from = context, .drop = true };
    pin_value_collection_visit_owners(value, collection_remove, &transfer);
}
void behavior_function_collection_transfer(
    behavior_function_context *from,
    behavior_function_context *to,
    const pin_value *value
)
{
    collection_transfer transfer = { .from = from, .to = to };
    pin_value_collection_visit_owners(value, collection_remove, &transfer);
}
typedef struct
{
    const behavior_function_context *context;
    bool held;
} collection_held;
static void collection_find(const pin_value *value, void *user)
{
    collection_held *find = user;
    for (int i = 0; i < acount(find->context->collection_owners); i++) {
        find->held |= collection_owner_same(find->context->collection_owners[i], *value);
    }
}
bool behavior_function_collection_holds(const behavior_function_context *context, const pin_value *value)
{
    collection_held find = { .context = context };
    pin_value_collection_visit_owners(value, collection_find, &find);
    return find.held;
}
static void collection_check(const pin_value *value, void *user)
{
    collection_held *check = user;
    bool found = false;
    for (int i = 0; i < acount(check->context->collection_owners); i++) {
        found |= collection_owner_same(check->context->collection_owners[i], *value);
    }
    check->held &= found;
}
bool behavior_function_collection_owns(const behavior_function_context *context, const pin_value *value)
{
    collection_held check = { .context = context, .held = true };
    pin_value_collection_visit_owners(value, collection_check, &check);
    return check.held;
}
void behavior_function_collection_empty(behavior_function_context *context)
{
    for (int i = 0; i < acount(context->collection_owners); i++) {
        pin_value_collection_release(&context->collection_owners[i]);
    }
    if (context->collection_owners) {
        asetlen(context->collection_owners, 0);
    }
}
void behavior_function_collection_clear(behavior_function_context *context)
{
    behavior_function_collection_empty(context);
    afree(context->collection_owners);
}
void behavior_function_collection_trace(
    const behavior_function_context *context,
    arena_marker mark,
    void *user
)
{
    for (int i = 0; i < acount(context->collection_owners); i++) {
        pin_value_trace(&context->collection_owners[i], mark, user);
    }
}
/* Set in a scan's `arrays` to stop at the first collection or pointer found. */
#define MANAGED_SCAN_FIRST 8u
static const char *managed_scan(behavior_managed_heap *, const pin_value *, bool, unsigned *);
const char *behavior_function_value_call(behavior_function_context *context, int host_call)
{
    const behavior_function *function = context->data->function;
    const behavior_function_host_call *spec = &function->host_calls[host_call];
    const behavior_data_binding *binding = &context->data->calls[host_call];
    pin_value *arguments = context->data->values + spec->first;
    pin_value *result = context->data->values + spec->result_first;
    int count = spec->result_first - spec->first;
    execution_intrinsic kind = execution_intrinsic_kind(spec->capability_id);
    bool intrinsic = execution_intrinsic_resolve(spec->capability_id) != NULL;
    bool strict_range = intrinsic && (kind == EXECUTION_INTRINSIC_SEQUENCE_SLICE ||
                                      kind == EXECUTION_INTRINSIC_SEQUENCE_VIEW);
    /* A view value always has its window; a header without one is a type
     * schema, such as a reference's. */
    for (int i = 0; i < count; i++) {
        if (arguments[i].container == PIN_CONTAINER_VIEW && arguments[i].ptr &&
            !pin_array_view_valid(arguments[i].ptr) && !(strict_range && i == 0)) {
            return "Array view is out of range";
        }
    }
    bool array_result = result->container == PIN_CONTAINER_ARRAY ||
                        result->container == PIN_CONTAINER_VIEW;
    bool array_input = count && (arguments[0].container == PIN_CONTAINER_ARRAY ||
                                 arguments[0].container == PIN_CONTAINER_VIEW);
    bool sequence = intrinsic && kind >= EXECUTION_INTRINSIC_SEQUENCE_SLICE &&
                    kind <= EXECUTION_INTRINSIC_SEQUENCE_CLONE;
    bool mutation = intrinsic && (kind == EXECUTION_INTRINSIC_ARRAY_PUSH ||
                                  kind == EXECUTION_INTRINSIC_ARRAY_STORE ||
                                  kind == EXECUTION_INTRINSIC_ARRAY_EXTEND);
    bool retained_collections = false;
    if (intrinsic && (kind == EXECUTION_INTRINSIC_VALUE_COPY ||
                      kind == EXECUTION_INTRINSIC_CONSTRUCT ||
                      kind == EXECUTION_INTRINSIC_UNION_MAKE ||
                      kind == EXECUTION_INTRINSIC_UNION_KEEP)) {
        unsigned managed = MANAGED_SCAN_FIRST;
        for (int i = 0; i < count && managed == MANAGED_SCAN_FIRST; i++) {
            const char *error = managed_scan(NULL, &arguments[i], false, &managed);
            if (error) {
                return error;
            }
        }
        retained_collections = managed != MANAGED_SCAN_FIRST;
    }
    bool array_operation = array_result || mutation || retained_collections ||
                           (intrinsic &&
                            (kind == EXECUTION_INTRINSIC_MAP_MAKE ||
                             kind == EXECUTION_INTRINSIC_MAP_REMOVE ||
                             kind == EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE ||
                             kind == EXECUTION_INTRINSIC_MAP_DELETE_PLACE ||
                             (kind == EXECUTION_INTRINSIC_SEQUENCE_VIEW &&
                              count && arguments[0].type == PIN_POINTER))) ||
                           (intrinsic && ((kind == EXECUTION_INTRINSIC_CONSTRUCT ||
                                           kind == EXECUTION_INTRINSIC_SNAPSHOT) &&
                                          array_input));
    arena *storage = execution_intrinsic_in_result(spec->capability_id)
                       ? context->result_storage
                       : context->value_storage;
    /* A union payload read retains nothing new: the union passed these
     * checks when it was made or imported, so they are not repeated. */
    bool payload_read = intrinsic && kind == EXECUTION_INTRINSIC_UNION_GET;
    if (!payload_read && (array_operation || (sequence && array_input))) {
        storage = array_storage(context);
        if (!storage) {
            return context->error_message;
        }
        for (int i = 0; i < count; i++) {
            const char *error = behavior_managed_storage_value_error(storage, &arguments[i]);
            if (error) {
                return error;
            }
        }
    }
    if (intrinsic && kind == EXECUTION_INTRINSIC_VALUE_DROP) {
        /* A moved or borrowed shell has no owner here and must stay live. */
        behavior_function_collection_drop(context, arguments);
    }
    const char *error = binding->value(storage, arguments, count, result);
    if (error) {
        return error;
    }
    if (!intrinsic) {
        error = behavior_function_validate_value(context, result);
        if (error) {
            return error;
        }
    }
    bool owned_result = spec->call->owned_result &&
                        spec->call->value == binding->value &&
                        binding->capability_abi == spec->capability_abi &&
                        spec->capability_abi;
    if (!intrinsic && !owned_result) {
        pin_value copy;
        arena *heap = context->managed_heap ? array_storage(context) : storage;
        if (!heap || pin_value_collection_copy(heap, &copy, result)) {
            return "Cannot copy native result";
        }
        *result = copy;
    }
    if (result->container == PIN_CONTAINER_ARRAY ||
        result->container == PIN_CONTAINER_VIEW) {
        arena *heap = array_storage(context);
        if (!heap) {
            return context->error_message;
        }
        pin_array *array = result->ptr;
        if (array && !array->pointer && !array->storage && arena_contains(heap, array)) {
            if (pin_array_manage(heap, array)) {
                *result = (pin_value){ 0 };
                return "Cannot retain native array layout";
            }
        }
        pin_value retained;
        if (pin_value_array_promote(heap, &retained, result)) {
            return "Cannot retain dynamic array result";
        }
        *result = retained;
    }
    if (!intrinsic || kind == EXECUTION_INTRINSIC_VALUE_COPY ||
        kind == EXECUTION_INTRINSIC_CONSTRUCT || kind == EXECUTION_INTRINSIC_UNION_MAKE ||
        kind == EXECUTION_INTRINSIC_UNION_KEEP ||
        kind == EXECUTION_INTRINSIC_MAP_MAKE || kind == EXECUTION_INTRINSIC_MAP_REMOVE ||
        kind == EXECUTION_INTRINSIC_STRING_BYTES ||
        kind == EXECUTION_INTRINSIC_SEQUENCE_SLICE ||
        kind == EXECUTION_INTRINSIC_SEQUENCE_CLONE) {
        behavior_function_collection_retain(context, result);
    }
    return NULL;
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
void behavior_managed_heap_adopt(behavior_managed_heap *heap, behavior_managed_heap *other)
{
    assert(heap->owner == heap && other->owner == other);
    behavior_managed_heap *tail = heap;
    while (tail->previous) {
        tail = tail->previous;
    }
    tail->previous = other;
    /* Every heap names the head directly, so releasing one never strands an
     * owner link. */
    for (; other; other = other->previous) {
        other->owner = heap;
    }
}
void behavior_managed_heap_release_empty(behavior_managed_heap *heap)
{
    assert(heap->owner == heap);
    for (behavior_managed_heap **link = &heap->previous; *link;) {
        behavior_managed_heap *scope = *link;
        size_t live, capacity;
        arena_usage(&scope->storage, &live, &capacity);
        if (live || acount(scope->roots)) {
            link = &scope->previous;
            continue;
        }
        *link = scope->previous;
        scope->previous = NULL;
        behavior_managed_heap_free(scope);
        free(scope);
    }
}
static behavior_managed_heap *managed_owner(behavior_managed_heap *heap)
{
    while (heap->owner && heap->owner != heap) {
        heap = heap->owner;
    }
    return heap;
}
typedef struct
{
    const void *address, *shape;
    pin_type type;
    pin_container container;
    bool restricted;
} managed_seen_entry;
typedef struct
{
    managed_seen_entry *entries;
    size_t count, capacity;
} managed_seen;
static size_t managed_seen_slot(managed_seen_entry entry, size_t capacity)
{
    uint64_t hash = (uint64_t)(uintptr_t)entry.address * UINT64_C(0x9e3779b97f4a7c15);
    hash ^= (uint64_t)(uintptr_t)entry.shape * UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= (uint64_t)entry.type * UINT64_C(0x94d049bb133111eb);
    hash ^= (unsigned)entry.container * 2u + entry.restricted;
    return (size_t)(hash ^ (hash >> 32)) & (capacity - 1);
}
static bool managed_seen_before(managed_seen *seen, pin_value value, bool restricted)
{
    if (!value.ptr || (value.container == PIN_CONTAINER_SINGLE &&
                       value.type != PIN_STRUCT && value.type != PIN_UNION &&
                       value.type != PIN_POINTER && value.type != PIN_QUERY)) {
        return false;
    }
    managed_seen_entry entry = { .address = value.ptr,
                                 .type = value.type,
                                 .container = value.container,
                                 .restricted = restricted,
                                 .shape = value.container == PIN_CONTAINER_SINGLE &&
                                                  value.type == PIN_STRUCT
                                            ? pin_value_type(&value)
                                            : NULL };
    if (!seen->capacity || (seen->count + 1) * 2 > seen->capacity) {
        size_t capacity = seen->capacity ? seen->capacity * 2 : 32;
        managed_seen_entry *entries = xcalloc(capacity, sizeof *entries);
        for (size_t i = 0; i < seen->capacity; i++) {
            managed_seen_entry old = seen->entries[i];
            if (!old.address) {
                continue;
            }
            size_t slot = managed_seen_slot(old, capacity);
            while (entries[slot].address) {
                slot = (slot + 1) & (capacity - 1);
            }
            entries[slot] = old;
        }
        free(seen->entries);
        seen->entries = entries;
        seen->capacity = capacity;
    }
    size_t slot = managed_seen_slot(entry, seen->capacity);
    while (seen->entries[slot].address) {
        managed_seen_entry old = seen->entries[slot];
        if (old.address == entry.address && old.shape == entry.shape &&
            old.type == entry.type && old.container == entry.container &&
            old.restricted == entry.restricted) {
            return true;
        }
        slot = (slot + 1) & (seen->capacity - 1);
    }
    seen->entries[slot] = entry;
    seen->count++;
    return false;
}
/* A value a walk still has to visit, or the items of an array from `next`
 * on. An array stays one entry, so a walk's stack grows with nesting depth,
 * not with item counts. `flag` is the walk's own per-value bit. */
typedef struct
{
    pin_value value;
    const pin_array *items;
    int next;
    bool flag;
} managed_work;
/* Most walks stay within the stack's first entries, which need no
 * allocation. A stack must not move while in use. */
#define MANAGED_LOCAL_WORK 16
typedef struct
{
    managed_work *items, local[MANAGED_LOCAL_WORK];
    int count, capacity;
} managed_stack;
static void managed_stack_init(managed_stack *stack)
{
    stack->items = stack->local;
    stack->count = 0;
    stack->capacity = MANAGED_LOCAL_WORK;
}
static void managed_stack_free(managed_stack *stack)
{
    if (stack->items != stack->local) {
        free(stack->items);
    }
}
static void managed_push(managed_stack *stack, managed_work work)
{
    if (stack->count == stack->capacity) {
        size_t bytes = (size_t)stack->capacity * sizeof *stack->items;
        stack->items = stack->items == stack->local
                         ? memcpy(xmalloc(2 * bytes), stack->local, bytes)
                         : xrealloc(stack->items, 2 * bytes);
        stack->capacity *= 2;
    }
    stack->items[stack->count++] = work;
}
static void managed_push_items(managed_stack *stack, const pin_array *items, bool flag)
{
    if (items->count) {
        managed_push(stack, (managed_work){ .items = items, .flag = flag });
    }
}
/* Pops the next value to visit. An array's item comes with `items` set. */
static managed_work managed_pop(managed_stack *stack)
{
    managed_work *top = &stack->items[stack->count - 1];
    if (!top->items) {
        stack->count--;
        return *top;
    }
    managed_work item = { .value = pin_array_get(top->items, top->next),
                          .items = top->items,
                          .flag = top->flag };
    if (++top->next == top->items->count) {
        stack->count--;
    }
    return item;
}
static bool managed_family_contains(const behavior_managed_heap *heap, const void *address)
{
    for (const behavior_managed_heap *scope = heap; scope; scope = scope->previous) {
        if (arena_contains(&scope->storage, address)) {
            return true;
        }
    }
    return false;
}
static bool managed_family_owns(const behavior_managed_heap *heap, const arena *storage, const void *address)
{
    for (const behavior_managed_heap *scope = heap; scope; scope = scope->previous) {
        if (&scope->storage == storage && arena_contains(storage, address)) {
            return true;
        }
    }
    return false;
}
/* Native schemas resolve their own nested layouts. */
static type_resolver value_resolver(const pin_value *value)
{
    return value->schema ? pin_schema_resolver(value->schema) : (type_resolver){ 0 };
}
/* Whether a record holds nothing a scan checks: no union or pointer, its own
 * or a nested record's, and, for a newly retained value, no nested layout
 * whose freeze is still unchecked. */
static bool managed_plain_record(const type_desc *layout, type_resolver resolver, bool restricted)
{
    for (int i = 0; i < layout->field_count; i++) {
        const type_field *field = &layout->fields[i];
        if (field->container != PIN_CONTAINER_SINGLE || field->type == PIN_UNION ||
            field->type == PIN_POINTER || field->type == PIN_QUERY) {
            return false;
        }
        if (field->type != PIN_STRUCT) {
            continue;
        }
        const type_desc *child = type_field_type(field, resolver);
        if (!child || (restricted && !child->carrier) ||
            !managed_plain_record(child, resolver, restricted)) {
            return false;
        }
    }
    return true;
}
/* Whether a scan told to stop at the first collection or pointer found one. */
static bool managed_scan_found(const unsigned *arrays)
{
    return arrays && (*arrays & MANAGED_SCAN_FIRST) && *arrays != MANAGED_SCAN_FIRST;
}
/* A single value that is neither an aggregate, a pointer nor a query reaches
 * no other value, so ownership scans and traces need no work list for it. */
static bool managed_leaf(const pin_value *value)
{
    return value->container == PIN_CONTAINER_SINGLE &&
           value->type != PIN_STRUCT && value->type != PIN_UNION &&
           value->type != PIN_POINTER && value->type != PIN_QUERY;
}
/* Packed scalar items reach no other value and have no layout to check. */
static bool managed_items_reach(const pin_array *items)
{
    return !items->stride || items->element_type == PIN_STRUCT ||
           items->element_type == PIN_UNION ||
           items->element_type == PIN_POINTER || items->element_type == PIN_QUERY;
}
/* Shape restrictions belong to newly retained values. An existing managed
 * array already passed retention validation; all heap-family edges still need
 * checking when it crosses an execution boundary. */
static const char *managed_scan(
    behavior_managed_heap *heap,
    const pin_value *root,
    bool restricted,
    unsigned *arrays
)
{
    if (managed_leaf(root)) {
        return NULL;
    }
    /* A record of scalars and text reaches nothing; only a new native layout
     * still needs its freeze checked. */
    if (root->container == PIN_CONTAINER_SINGLE && root->type == PIN_STRUCT &&
        (!restricted || root->record)) {
        const type_desc *layout = pin_value_type(root);
        if (layout && managed_plain_record(layout, value_resolver(root), restricted)) {
            return NULL;
        }
    }
    managed_stack pending;
    managed_stack_init(&pending);
    managed_seen seen = { 0 };
    typedef struct
    {
        const type_desc *source;
        const pin_record *frozen;
    } checked_layout;
    checked_layout *checked = NULL;
    arena proof = { 0 };
    const char *error = NULL;
    managed_push(&pending, (managed_work){ .value = *root, .flag = restricted });
    /* Only a cycle reaches the root again, and its second visit stops the
     * cycle, so the root needs no seen entry. */
    bool first = true;
    while (pending.count && !error) {
        managed_work current = managed_pop(&pending);
        pin_value value = current.value;
        bool restricted_value = current.flag;
        bool dynamic = value.container == PIN_CONTAINER_ARRAY ||
                       value.container == PIN_CONTAINER_VIEW;
        const pin_array *array = dynamic ? value.ptr : NULL;
        const pin_array *owner = array ? pin_array_owner(array) : NULL;
        if (dynamic && arrays) {
            *arrays |= 1u;
            if (!owner || !owner->storage) {
                *arrays |= 2u;
            }
        }
        if (value.container == PIN_CONTAINER_MAP) {
            const pin_map *map = value.ptr;
            if (arrays) {
                *arrays |= 1u;
                if (!map || !map->storage) {
                    *arrays |= 2u;
                }
            }
            if (map && map->storage) {
                if (heap && !managed_family_owns(heap, map->storage, map)) {
                    error = "Managed maps cannot cross heap owners";
                    break;
                }
                continue;
            }
        }
        if (owner && owner->storage) {
            if (heap && (!managed_family_owns(heap, owner->storage, owner) ||
                         (array->pointer && !managed_family_contains(heap, array)))) {
                error = "Managed arrays cannot cross heap owners";
                break;
            }
            /* Import and each write establish the subtree's ownership. Reads
             * and alias transfers validate this root in constant time. */
            continue;
        }
        if (managed_scan_found(arrays)) {
            break;
        }
        if (!first && managed_seen_before(&seen, value, restricted_value)) {
            continue;
        }
        first = false;
#define PUSH_VALUE(v)                                                          \
    managed_push(&pending, (managed_work){ .value = (v), .flag = restricted_value })
        if (arrays && value.type == PIN_POINTER &&
            value.container == PIN_CONTAINER_SINGLE && value.ptr) {
            *arrays |= 4u;
        }
        if (heap && value.type == PIN_POINTER &&
            value.container == PIN_CONTAINER_SINGLE && value.ptr) {
            const pin_pointer *pointer = value.ptr;
            if (!managed_family_contains(heap, pointer) ||
                !managed_family_owns(heap, pointer->storage, pointer)) {
                error = "Managed pointers cannot cross heap owners";
                break;
            }
        }
        if (value.type == PIN_QUERY &&
            value.container == PIN_CONTAINER_SINGLE && value.ptr) {
            const pin_query *query = value.ptr;
            if (arrays) {
                *arrays |= 4u;
            }
            if (heap && (!managed_family_contains(heap, query) ||
                         !managed_family_owns(heap, query->storage, query))) {
                error = "Queries cannot cross heap owners";
                break;
            }
            for (int i = 0; i < query->count; i++) {
                PUSH_VALUE(query->values[i]);
            }
        }
        /* Synthetic nested-collection headers describe boxed containers, not
         * native struct payloads. Their actual items carry their own shapes. */
        bool collection_header = value.container != PIN_CONTAINER_SINGLE &&
                                 !value.record && value.type_name &&
                                 !strncmp(value.type_name, "__collection_", 13);
        /* New native layouts are frozen by the import/snapshot path. A
         * borrowed descriptor is used only while this boundary validates it. */
        if (value.type == PIN_STRUCT && !collection_header) {
            const type_desc *layout = pin_value_type(&value);
            if (!layout) {
                error = "Managed value requires a resolved record layout";
                break;
            }
            if (restricted_value && !value.record && !value.schema) {
                const pin_record *frozen = NULL;
                for (int i = 0; i < acount(checked); i++) {
                    if (checked[i].source == layout) {
                        frozen = checked[i].frozen;
                    }
                }
                if (!frozen) {
                    frozen = pin_record_from_type(&proof, layout);
                    if (!frozen) {
                        error = "Cannot retain native record layout";
                        break;
                    }
                    apush(checked, ((checked_layout){ layout, frozen }));
                }
                layout = &frozen->type;
            }
            for (int i = 0; i < layout->field_count; i++) {
                const type_field *field = &layout->fields[i];
                if (value.container != PIN_CONTAINER_SINGLE || !value.ptr) {
                    continue;
                }
                pin_value member = { .type = field->type,
                                     .container = field->container,
                                     .type_name = field->type_name,
                                     .schema = value.schema };
                const void *data = (const char *)value.ptr + field->offset;
                if (field->container == PIN_CONTAINER_ARRAY) {
                    memcpy(&member.ptr, data, sizeof member.ptr);
                    member.record = field->layout ? field->layout->carrier : NULL;
                } else if (field->type == PIN_STRUCT) {
                    const type_desc *child = type_field_type(field, value_resolver(&value));
                    if (!child) {
                        error = "Managed value requires a resolved nested "
                                "record layout";
                        break;
                    }
                    member.ptr = (void *)data;
                    member.type_name = child->name;
                    member.record = child->carrier;
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
            const pin_array *items = value.ptr;
            if (items && pin_array_view_valid(items) && managed_items_reach(items)) {
                managed_push_items(&pending, items, restricted_value);
            }
        } else if (value.type == PIN_UNION) {
            const pin_union *sum = value.ptr;
            for (int i = 0; sum && i < sum->count; i++) {
                PUSH_VALUE(sum->values[i]);
            }
        }
#undef PUSH_VALUE
    }
    managed_stack_free(&pending);
    free(seen.entries);
    afree(checked);
    arena_free(&proof);
    return error;
}
static const char *managed_value_error(behavior_managed_heap *heap, const pin_value *value, bool restricted)
{
    return managed_scan(heap, value, restricted, NULL);
}
const char *behavior_function_validate_value(behavior_function_context *context, const pin_value *value)
{
    if (context->managed_heap) {
        return behavior_managed_storage_value_error(array_storage(context), value);
    }
    unsigned managed = 0;
    const char *error = managed_scan(NULL, value, true, &managed);
    if (error) {
        return error;
    }
    return managed ? "Managed values require an initialized caller-owned heap" : NULL;
}
const char *behavior_function_value_arguments(behavior_function_context *context)
{
    if (context->arguments_stabilized) {
        return NULL;
    }
    const pin_value *source = context->arguments;
    arena *storage = context->managed_heap ? array_storage(context)
                                           : context->value_storage;
    for (int i = 0; i < context->argument_count; i++) {
        const char *error = behavior_function_validate_value(context, &source[i]);
        if (error) {
            return error;
        }
    }
    /* Scalars are their own copies, and a function proven to leave its
     * collections unchanged reads an array the caller lends it in place, so
     * only other arguments that hold storage need copying into an owner. */
    const behavior_function *f = context->source_function;
    bool held = false;
    for (int i = 0; i < context->argument_count; i++) {
        pin_value lent;
        held |= pin_value_holds_storage(&source[i]) &&
                !(f && f->readonly_collections &&
                  behavior_function_borrow_argument(f, context, &source[i], &lent));
    }
    if (!held) {
        return NULL;
    }
    if (!storage) {
        return "Value arguments require invocation storage";
    }
    pin_value *values = context->argument_count
                          ? arena_alloc(storage, (size_t)context->argument_count * sizeof *values)
                          : NULL;
    for (int i = 0; i < context->argument_count; i++) {
        if (pin_value_collection_copy(storage, &values[i], &context->arguments[i])) {
            for (int j = 0; j < i; j++) {
                behavior_function_collection_drop(context, &values[j]);
            }
            context->arguments = source;
            return "Cannot copy value argument";
        }
        behavior_function_collection_retain(context, &values[i]);
    }
    context->argument_source = source;
    context->arguments = values;
    context->arguments_stabilized = true;
    return NULL;
}
void behavior_function_restore_arguments(behavior_function_context *context)
{
    if (context->argument_source) {
        context->arguments = context->argument_source;
    }
    context->argument_source = NULL;
    context->arguments_stabilized = false;
}
int behavior_function_finish_values(behavior_function_context *context, int status, int result_count)
{
    /* Program contexts detach escaping reference boxes in their leave hook
     * before host teardown releases the remaining logical owners. */
    if (context->parent || context->call_leave || status == FLOW_PAUSED_RESULT ||
        status == FLOW_SUSPENDED_RESULT || status == BEHAVIOR_FUNCTION_CALL_RESULT) {
        return status;
    }
    if (status >= 0) {
        for (int i = 0; context->results && i < result_count; i++) {
            behavior_function_collection_transfer(context, NULL, &context->results[i]);
        }
    }
    behavior_function_collection_clear(context);
    behavior_function_restore_arguments(context);
    return status;
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
    if (pin_value_array_promote(&heap->storage, &retained, value)) {
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
/* The values one collection has traced, so a value that many roots reach is
 * traced once. */
static _Thread_local managed_seen *collection_seen;
void behavior_managed_heap_trace_value(const pin_value *root, arena_marker mark, void *user)
{
    if (managed_leaf(root)) {
        pin_value_trace(root, mark, user);
        return;
    }
    /* A record held inside an array or another record is reached only through
     * its holder, which is visited once, so only shareable values need the
     * seen set. `flag` marks a held record. */
    managed_stack pending;
    managed_stack_init(&pending);
    managed_seen local = { 0 };
    managed_seen *seen = collection_seen ? collection_seen : &local;
    managed_push(&pending, (managed_work){ .value = *root });
    while (pending.count) {
        managed_work next = managed_pop(&pending);
        pin_value value = next.value;
        bool held = next.items ? value.type == PIN_STRUCT : next.flag;
        if (!held && managed_seen_before(seen, value, false)) {
            continue;
        }
        pin_value_trace(&value, mark, user);
        if (value.container == PIN_CONTAINER_MAP) {
            const pin_map *map = value.ptr;
            for (int i = 0; map && i < map->count; i++) {
                managed_push(&pending, (managed_work){ .value = map->entries[i].key });
                managed_push(&pending, (managed_work){ .value = map->entries[i].value });
            }
        } else if (value.container != PIN_CONTAINER_SINGLE) {
            const pin_array *array = value.ptr;
            /* A view's items live in what its pointer reaches, so they are
             * walked whatever their type. */
            if (!array || !pin_array_view_valid(array) ||
                (!array->pointer && !pin_array_items_reach(array))) {
                continue;
            }
            managed_push_items(&pending, array, false);
        } else if (value.type == PIN_UNION) {
            const pin_union *sum = value.ptr;
            for (int i = 0; sum && i < sum->count; i++) {
                managed_push(&pending, (managed_work){ .value = sum->values[i] });
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
                    const type_desc *child = type_field_type(field, value_resolver(&value));
                    assert(child);
                    member.ptr = (void *)data;
                    member.type_name = child->name;
                    member.record = child->carrier;
                } else if (field->type == PIN_POINTER || field->type == PIN_QUERY || field->type == PIN_UNION) {
                    memcpy(&member.ptr, data, sizeof member.ptr);
                } else {
                    continue;
                }
                managed_push(
                    &pending,
                    (managed_work){ .value = member, .flag = field->type == PIN_STRUCT }
                );
            }
        }
    }
    managed_stack_free(&pending);
    free(local.entries);
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
    managed_seen seen = { 0 };
    collection_seen = &seen;
    arena_collect_begin(&collection, storages, acount(storages));
    behavior_managed_heap_trace_roots(heap, arena_collect_mark, &collection);
    if (function && context) {
        behavior_function_trace_values(function, context, arena_collect_mark, &collection);
    }
    arena_collect_end(&collection);
    collection_seen = NULL;
    free(seen.entries);
    afree(storages);
}
void behavior_managed_heap_safepoint(
    behavior_managed_heap *heap,
    const behavior_function *function,
    const behavior_function_context *root
)
{
    if (!heap || !heap->call_collection_bytes || heap->running_calls != 1) {
        return;
    }
    size_t allocated = 0, kept = 0;
    for (behavior_managed_heap *scope = managed_owner(heap); scope;
         scope = scope->previous) {
        allocated += scope->storage.allocated;
        kept += scope->storage.kept;
    }
    if (allocated >= heap->call_collection_bytes && allocated >= 2 * kept) {
        behavior_managed_heap_collect(heap, function, root);
    }
}
