#include "authoring/native_bindings.h"
#include "backend.h"
#include "execution/definition.h"
#include "execution/module.h"
#include "execution/status.h"
#include "language/lower.h"
#include "runtime/arrays.h"
#include "runtime/text_definition.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array_internal.h"
#include "types/copy.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Six fixed cases,
 * eight source preparations/module finalizations, 24 top-level invocations and
 * 402 synchronous scalar-helper calls across bytecode and the compiled backend.
 * Fourteen additional static proofs use private instruction copies; no malformed
 * instruction is executed and no extra module is prepared.
 * The final case adds seven direct appends, one overflow rejection and two GC
 * cycles; no source preparation or authored entry.
 * TWO also makes ten length and eight get calls. The reader callback case
 * makes ten direct native calls over one element, including four invalid gets.
 * Each authored frame has a 20,000-instruction budget. Maximum loop count 128;
 * no world, engine frames, files, processes, network, sleeps or timing loops.
 * Preparation/build and execution require the parent's separate admission. */
enum
{
    BUDGET = 20000
};
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static const int sizes[] = { 0, 1, 8, 64, 128 };
static const char TWO[] = "Numbers :: library { "
                          "Build :: (count:int)->(All:int[],Even:int[]) { "
                          "all:int[]=[]; even:int[]=[]; i:int=0; "
                          "while i<count { value:int=Value(i,all); "
                          "if i%2==0 { all=array_add(all,value); } "
                          "else { all=array_add(all,value); } "
                          "if i%2==0 { even=array_add(even,value); } i=i+1; } "
                          "if length(all)!=count { return(even,even); } "
                          "if count>0 { if get(all,count-1)!=count+count-1 { "
                          "return(even,even); } } "
                          "return(all,even); } "
                          "Value :: (input:int,values:int[])->int { "
                          "if input==0 { return 1; } "
                          "return input+input+values[0]; } }";
static const char
    ALIASES[] = "Numbers :: library { "
                "Build :: (count:int)->(Current:int[],Old:int[],Fork:int[]) { "
                "values:int[]=[]; i:int=0; "
                "while i<count { values=array_add(values,i); i=i+1; } "
                "old:int[]=values; currentValue:int=101; forkValue:int=202; "
                "values=array_add(values,currentValue); "
                "fork:int[]=old; fork=array_add(fork,forkValue); "
                "return(values,old,fork); } }";
static const char ONE
    [] = "Numbers :: library { "
         "Build :: (count:int,base:int)->int[] { values:int[]=[]; i:int=0; "
         "if text_byte_length(format_text(\"{}\",base))==0 { return values; } "
         "while i<count { values=array_add(values,base+i); i=i+1; } "
         "return values; } }";

static native_capabilities *native;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module;
static const behavior_function *function;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage;
static pin_value arguments[2], results[3];
static behavior_run_scope runs;
static pin_copy_scope copies;
static int observations, replaced_calls;
static void tail_cleanup();

static void release_call()
{
    if (copies.active) {
        pin_copy_scope_end(&copies);
    }
    if (runs.active) {
        behavior_run_scope_end(&runs);
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    context = (behavior_function_context){ 0 };
    arena_free(&storage);
    memset(results, 0, sizeof results);
}
static void cleanup()
{
    tail_cleanup();
    release_call();
    execution_module_free(module);
    module = NULL;
    function = NULL;
    execution_module_plan_free(plan);
    plan = NULL;
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    native_capabilities_free(native);
    native = NULL;
}
static bool prepare(const char *source, behavior_backend backend, int argc, int resultc, int functions)
{
    char error[256];
    const native_definition_pack packs[] = { NATIVE_ARRAY_DEFINITION_PACK,
                                             NATIVE_TEXT_DEFINITION_PACK };
    native = native_capabilities_from_definition_packs(NULL, 0, packs, 2, error, sizeof error);
    if (!native) {
        fprintf(stderr, "scalar-append catalog: %s\n", error);
    }
    REQUIRE(native);
    syntax = script_parse(source);
    for (int i = 0; i < syntax->diagnostic_count; i++) {
        fprintf(stderr, "scalar-append parse: %s\n", syntax->diagnostics[i].message);
    }
    REQUIRE(!syntax->diagnostic_count);
    const script_catalog *catalog = native_capabilities_catalog(native);
    analysis = script_analyze(syntax, catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "scalar-append semantic: %s\n", analysis->diagnostics[i].message);
    }
    REQUIRE(analysis->valid);
    script_lower_bindings bindings = authoring_native_bindings(native);
    plan = script_lower(analysis, catalog, &bindings);
    for (int i = 0; plan && i < plan->diagnostic_count; i++) {
        fprintf(stderr, "scalar-append lower: %s\n", plan->diagnostics[i].message);
    }
    REQUIRE(plan && plan->valid);
    module = execution_module_finish(&plan, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "scalar-append finish: %s\n", error);
    }
    REQUIRE(module && execution_module_backend(module) == backend);
    REQUIRE(execution_module_function_count(module) == functions);
    for (int i = 0; i < functions; i++) {
        const behavior_function *candidate = execution_module_function_at(module, i)
                                                 .body;
        if (candidate->argument_count == argc && candidate->result_count == resultc) {
            REQUIRE(!function);
            function = candidate;
        }
    }
    REQUIRE(function && !function->rows);
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    return true;
}
static void reset_entry(int count, int base)
{
    behavior_function_activation_free(&activation);
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &activation);
    }
    arguments[0] = (pin_value){ .type = PIN_I32, .i = count };
    arguments[1] = (pin_value){ .type = PIN_I32, .i = base };
    for (int i = 0; i < function->result_count; i++) {
        results[i] = function->result_defaults[i];
    }
    context.activation = &activation;
    context.arguments = arguments;
    context.argument_count = function->argument_count;
    context.results = results;
    context.budget = BUDGET;
    context.steps = 0;
    context.returned = false;
    context.exit_requested = false;
    context.returned_outcome = 0;
    context.failure = 0;
    context.error_message = NULL;
    behavior_function_bind_storage(&context, &storage);
}
static bool begin(int count, int base)
{
    release_call();
    arena_init(&storage, 65536);
    reset_entry(count, base);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    behavior_function_prepare_host(function, &context);
    REQUIRE(context.data);
    return true;
}
static bool execute(behavior_backend backend)
{
    behavior_run_scope_reset(&runs);
    pin_copy_scope_reset(&copies);
    behavior_run_scope_begin(&runs);
    pin_copy_scope_begin(&copies);
    int status = behavior_function_run(function, &context, backend != BEHAVIOR_BYTECODE);
    pin_copy_scope_end(&copies);
    behavior_run_scope_end(&runs);
    if (status != FLOW_OK) {
        fprintf(
            stderr,
            "scalar-append status=%d: %s\n",
            status,
            context.error_message ? context.error_message : ""
        );
    }
    REQUIRE(status == FLOW_OK && context.returned);
    REQUIRE(!runs.counts.overflow && !copies.counts.overflow);
    REQUIRE(!runs.counts.inspection_bytecode_fallbacks);
    if (backend == BEHAVIOR_BYTECODE) {
        REQUIRE(
            runs.counts.bytecode_entries && !runs.counts.sljit_entries &&
            !runs.counts.wasm_entries
        );
    } else if (backend == BEHAVIOR_SLJIT) {
        REQUIRE(
            runs.counts.sljit_entries && !runs.counts.bytecode_entries &&
            !runs.counts.wasm_entries
        );
    } else {
        REQUIRE(
            runs.counts.wasm_entries && !runs.counts.bytecode_entries &&
            !runs.counts.sljit_entries
        );
    }
    return true;
}
static bool sequence(const pin_value *value, int count, int first, int step)
{
    REQUIRE(value->type == PIN_I32 && value->container == PIN_CONTAINER_ARRAY);
    REQUIRE(!value->type_name && !value->record);
    const pin_array *array = value->ptr;
    REQUIRE(array && pin_array_valid(array) && arena_contains(&storage, array));
    REQUIRE(array->element_type == PIN_I32 && array->count == count);
    REQUIRE(!array->type_name && !array->record && array->stride == sizeof(int32_t));
    REQUIRE(!count || arena_contains(&storage, array->data));
    for (int i = 0; i < count; i++) {
        pin_value item = pin_array_get(array, i);
        REQUIRE(item.type == PIN_I32 && item.i == first + i * step);
    }
    return true;
}
static bool linear(int count, int elements)
{
    /* ONE invokes Format Text once. Its native result snapshot copies the
     * decimal text (including NUL) and its "const char *" type name once.
     * The formatter's internal raw arena copies are outside these counters.
     * TWO has no text or nominal payload. Array-copy limits remain separate. */
    char text[32];
    uint64_t text_bytes = function->argument_count == 2
                            ? (
                                  uint64_t
                              )snprintf(text, sizeof text, "%d", arguments[1].i) +
                                  1
                            : 0;
    uint64_t nominal_bytes = function->argument_count == 2
                               ? sizeof "const char *"
                               : 0;
    if (copies.counts.text_bytes != text_bytes ||
        copies.counts.nominal_bytes != nominal_bytes || copies.counts.record_bytes ||
        copies.counts.value_buffer_bytes < (uint64_t)elements * sizeof(int32_t) ||
        copies.counts.value_buffer_bytes > (uint64_t)64 * count + 256) {
        fprintf(
            stderr,
            "scalar-append copies count=%d elements=%d text=%llu expected=%llu "
            "nominal=%llu expected=%llu record=%llu packed=%llu headers=%llu\n",
            count,
            elements,
            (unsigned long long)copies.counts.text_bytes,
            (unsigned long long)text_bytes,
            (unsigned long long)copies.counts.nominal_bytes,
            (unsigned long long)nominal_bytes,
            (unsigned long long)copies.counts.record_bytes,
            (unsigned long long)copies.counts.value_buffer_bytes,
            (unsigned long long)copies.counts.value_header_moves
        );
    }
    REQUIRE(
        copies.counts.text_bytes == text_bytes &&
        copies.counts.nominal_bytes == nominal_bytes && !copies.counts.record_bytes
    );
    REQUIRE(copies.counts.value_buffer_bytes >= (uint64_t)elements * sizeof(int32_t));
    REQUIRE(copies.counts.value_buffer_bytes <= (uint64_t)64 * count + 256);
    return true;
}
static bool derived(int count)
{
    if (acount(function->appends) != count) {
        fprintf(
            stderr,
            "scalar-append plans=%d expected=%d instructions=%d hosts=%d "
            "slots=%d fast=%d batch=%d waits=%d\n",
            acount(function->appends),
            count,
            acount(function->instructions),
            acount(function->host_calls),
            acount(function->slots),
            function->fast != NULL,
            function->batchable,
            function->waits
        );
        /* A failed preparation reports at most 256 total host/IR records.
         * This does not prepare or run an additional authored function. */
        int remaining = 256;
        for (int j = 0; j < acount(function->host_calls) && remaining;
             j++, remaining--) {
            const behavior_function_host_call *h = &function->host_calls[j];
            fprintf(
                stderr,
                "host %d first=%d result=%d count=%d value=%d invoke=%d "
                "context=%d action=%d variadic=%d reusable=%d\n",
                j,
                h->first,
                h->result_first,
                h->result_count,
                h->call->value != NULL,
                h->call->invoke != NULL,
                h->call->context != NULL,
                h->call->action != NULL,
                h->call->variadic,
                h->call->reusable
            );
        }
        for (int pc = 0; pc < acount(function->instructions) && remaining;
             pc++, remaining--) {
            const behavior_function_instruction *i = &function->instructions[pc];
            const behavior_function_instruction *r = &function->runtime_body[pc];
            fprintf(
                stderr,
                "ir %d op=%d type=%d out=%d left=%d/%d right=%d/%d "
                "target=%d offset=%zu kept=%d runtime=%d,%d,%d,%d,%zu\n",
                pc,
                i->op,
                i->type,
                i->output,
                i->left,
                i->left_slot,
                i->right,
                i->right_slot,
                i->target,
                i->offset,
                behavior_function_runtime_keeps(function, pc),
                r->op,
                r->left,
                r->right,
                r->target,
                r->offset
            );
        }
    }
    REQUIRE(acount(function->appends) == count);
    for (int i = 0; i < count; i++) {
        const behavior_append_plan *p = &function->appends[i];
        REQUIRE(p->add_pc >= 0 && p->retain_pc > p->add_pc);
        REQUIRE(p->retain_pc < acount(function->instructions));
        REQUIRE(function->instructions[p->add_pc].target == p->add);
        REQUIRE(function->instructions[p->retain_pc].target == p->retain);
        REQUIRE(function->runtime_body[p->add_pc].right == BF_HOST_APPEND_SCALAR);
        REQUIRE(function->runtime_body[p->retain_pc].right == BF_HOST_RETAIN_SCALAR);
        REQUIRE(
            function->runtime_instructions[function->runtime_map[p->add_pc]].right == BF_HOST_APPEND_SCALAR
        );
        REQUIRE(
            function->runtime_instructions[function->runtime_map[p->retain_pc]].right == BF_HOST_RETAIN_SCALAR
        );
    }
    return true;
}
/* Re-prove owned instruction vectors, never modify or execute the module.
 * ONE has one pair: its loop branch and post-append integer counter write make
 * the two rejected transformations unambiguous. All other tables are borrowed. */
static bool static_rejections()
{
    REQUIRE(derived(1));
    const behavior_append_plan p = function->appends[0];
    int branch = -1, payload = -1, write = -1;
    for (int pc = 0; pc < acount(function->instructions); pc++) {
        const behavior_function_instruction *i = &function->instructions[pc];
        if (i->op == BF_BRANCH && pc < p.add_pc && branch < 0) {
            branch = pc;
        }
        if (i->op == BF_LOAD && pc < p.add_pc && i->type == BF_POINTER_TYPE &&
            i->offset == offsetof(pin_value, d)) {
            for (int at = 0; at < pc; at++) {
                const behavior_function_instruction *header = &function->instructions[at];
                if (header->op == BF_HOST_SLOT && header->output == i->left &&
                    !header->target && header->left == p.local) {
                    payload = i->output;
                }
            }
        }
        if (i->op == BF_WRITE && pc > p.retain_pc && i->type == PIN_I32 &&
            behavior_function_runtime_keeps(function, pc) &&
            function->runtime_body[pc].op == BF_WRITE && write < 0) {
            write = pc;
        }
    }
    REQUIRE(branch >= 0 && payload >= 0 && write >= 0);
    for (int mode = 0; mode < 3; mode++) {
        behavior_function copy = *function;
        copy.instructions = NULL;
        copy.runtime_body = NULL;
        copy.runtime_instructions = NULL;
        copy.appends = NULL;
        acopy(copy.instructions, function->instructions);
        acopy(copy.runtime_body, function->runtime_body);
        acopy(copy.runtime_instructions, function->runtime_instructions);
        int pair[] = { p.add_pc, p.retain_pc };
        for (int i = 0; i < 2; i++) {
            copy.runtime_body[pair[i]].right = 0;
            copy.runtime_instructions[copy.runtime_map[pair[i]]].right = 0;
        }
        if (mode == 1) {
            copy.instructions[branch].target = p.retain_pc;
            copy.runtime_body[branch].target = p.retain_pc;
        } else if (mode == 2) {
            copy.instructions[write].right = payload;
            copy.instructions[write].offset = 0;
            copy.runtime_body[write].right = payload;
            copy.runtime_body[write].offset = 0;
        }
        behavior_function_scalar_builder(&copy);
        bool correct = acount(copy.appends) == (mode == 0 ? 1 : 0);
        afree(copy.appends);
        afree(copy.runtime_instructions);
        afree(copy.runtime_body);
        afree(copy.instructions);
        REQUIRE(correct);
    }
    REQUIRE(derived(1));
    return true;
}
static const char *equivalent_get(arena *owner, const pin_value *args, int count, pin_value *out)
{
    return pin_array_get_call(owner, args, count, out);
}
/* A matching callback implementation and the scalar reader's physical bank
 * signature are both required. Copied invalid declarations are never run. */
static bool reader_rejections()
{
    int get = -1, length = -1;
    for (int j = 0; j < acount(function->host_calls); j++) {
        node_value_call_fn value = function->host_calls[j].call->value;
        if (value == pin_array_get_call) {
            REQUIRE(get < 0);
            get = j;
        }
        if (value == pin_array_length_call) {
            REQUIRE(length < 0);
            length = j;
        }
    }
    REQUIRE(get >= 0 && length >= 0);
    pin_value index = function->data_values[function->host_calls[get].first + 1].initial;
    pin_value item = function
                         ->data_values[function->host_calls[get].result_first]
                         .initial;
    pin_value size = function
                         ->data_values[function->host_calls[length].result_first]
                         .initial;
    REQUIRE(index.type == PIN_I32 && index.type_name && !strcmp(index.type_name, "int"));
    REQUIRE(item.type == PIN_I32 && !item.type_name && !item.record);
    REQUIRE(size.type == PIN_I32 && !size.type_name && !size.record);
    for (int mode = 0; mode < 4; mode++) {
        behavior_function copy = *function;
        copy.runtime_body = NULL;
        copy.runtime_instructions = NULL;
        copy.host_calls = NULL;
        copy.data_values = NULL;
        copy.appends = NULL;
        acopy(copy.runtime_body, function->runtime_body);
        acopy(copy.runtime_instructions, function->runtime_instructions);
        acopy(copy.host_calls, function->host_calls);
        acopy(copy.data_values, function->data_values);
        for (int j = 0; j < acount(function->appends); j++) {
            const behavior_append_plan *p = &function->appends[j];
            int pair[] = { p->add_pc, p->retain_pc };
            for (int k = 0; k < 2; k++) {
                copy.runtime_body[pair[k]].right = 0;
                copy.runtime_instructions[copy.runtime_map[pair[k]]].right = 0;
            }
        }
        node_host_call callback = *copy.host_calls[get].call;
        if (mode == 1) {
            callback.value = equivalent_get;
            copy.host_calls[get].call = &callback;
        } else if (mode == 2) {
            copy.data_values[copy.host_calls[get].first + 1].initial.type = PIN_F64;
        } else if (mode == 3) {
            copy.data_values[copy.host_calls[get].first + 1].initial.type_name = "IndexAlias";
        }
        behavior_function_scalar_builder(&copy);
        bool correct = acount(copy.appends) == (mode ? 0 : 3);
        afree(copy.appends);
        afree(copy.data_values);
        afree(copy.host_calls);
        afree(copy.runtime_instructions);
        afree(copy.runtime_body);
        REQUIRE(correct);
    }
    return true;
}

TEST_CASE(scalar_append_two_accumulators_and_scalar_helper)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(prepare(TWO, backends[b], 1, 2, 2));
        REQUIRE(derived(3) && acount(function->calls) == 1);
        REQUIRE(function->calls[0].function->readonly_collections);
        REQUIRE(reader_rejections());
        for (int n = 0; n < 5; n++) {
            int count = sizes[n], evens = (count + 1) / 2;
            REQUIRE(begin(count, 0));
            REQUIRE(execute(backends[b]));
            REQUIRE(sequence(&results[0], count, 1, 2));
            REQUIRE(sequence(&results[1], evens, 1, 4));
            REQUIRE(linear(count, count + evens));
        }
    }
    return true;
}
TEST_CASE(scalar_append_retained_prefix_and_escaped_snapshot)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(prepare(ALIASES, backends[b], 1, 3, 1));
        REQUIRE(derived(3));
        REQUIRE(begin(8, 0));
        REQUIRE(execute(backends[b]));
        REQUIRE(sequence(&results[1], 8, 0, 1));
        pin_array *current = results[0].ptr, *old = results[1].ptr,
                  *fork = results[2].ptr;
        REQUIRE(current && fork && current->count == 9 && fork->count == 9);
        REQUIRE(current != old && current != fork && old != fork);
        REQUIRE(
            current->data != old->data && current->data != fork->data &&
            old->data != fork->data
        );
        REQUIRE(pin_array_get(current, 8).i == 101 && pin_array_get(fork, 8).i == 202);
        for (int i = 0; i < 8; i++) {
            REQUIRE(pin_array_get(current, i).i == i && pin_array_get(fork, i).i == i);
        }
        pin_value changed = { .type = PIN_I32, .i = -77 };
        REQUIRE(!pin_array_set(old, 0, &changed));
        REQUIRE(pin_array_get(current, 0).i == 0 && pin_array_get(fork, 0).i == 0);
        changed.i = -88;
        REQUIRE(!pin_array_set(current, 1, &changed));
        REQUIRE(pin_array_get(old, 1).i == 1 && pin_array_get(fork, 1).i == 1);
        /* Returned snapshots cannot mutate either live private accumulator. */
        for (int i = 0; i < acount(function->appends); i++) {
            const pin_array
                *private = context.data->values[function->appends[i].local].ptr;
            REQUIRE(
                private && pin_array_get(private, 0).i == 0 &&
                pin_array_get(private, 1).i == 1
            );
        }
    }
    return true;
}
TEST_CASE(scalar_append_reuses_context_after_arena_reset)
{
    const int counts[] = { 8, 1, 64 };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(prepare(ONE, backends[b], 2, 1, 1));
        REQUIRE(static_rejections());
        REQUIRE(begin(counts[0], 20));
        behavior_data_storage *data = context.data;
        void *marker = arena_alloc(&storage, 16);
        for (int run = 0; run < 3; run++) {
            if (run) {
                arena_reset(&storage);
                REQUIRE(arena_alloc(&storage, 16) == marker);
                reset_entry(counts[run], 20 * (run + 1));
            }
            REQUIRE(context.data == data && context.value_storage == &storage);
            REQUIRE(execute(backends[b]));
            REQUIRE(sequence(&results[0], counts[run], 20 * (run + 1), 1));
            REQUIRE(linear(counts[run], counts[run]));
        }
    }
    return true;
}
static int observe(void *user, behavior_function_context *ctx, const behavior_source_point *point)
{
    (void)user;
    (void)ctx;
    (void)point;
    observations++;
    return observations > 512 ? FLOW_ERR_STATE : FLOW_OK;
}
static const char *replaced_add(arena *owner, const pin_value *args, int count, pin_value *out)
{
    replaced_calls++;
    return pin_array_add_call(owner, args, count, out);
}
TEST_CASE(scalar_append_inspection_and_rebound_callback_fallback)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(prepare(ONE, backends[b], 2, 1, 1));
        REQUIRE(derived(1));
        uint64_t inspected_bytes = 0, rebound_bytes = 0;
        for (int mode = 0; mode < 3; mode++) {
            REQUIRE(begin(8, 7));
            observations = replaced_calls = 0;
            context.source_pause = mode == 0 ? observe : NULL;
            int add = function->appends[0].add;
            if (mode == 1) {
                context.data->calls[add].value = replaced_add;
            }
            REQUIRE(behavior_function_rows_allowed(function, &context) == (mode == 2));
            REQUIRE(execute(backends[b]));
            REQUIRE(sequence(&results[0], 8, 7, 1));
            /* SLJIT embeds the canonical callback even after the runtime
             * binding guard selects its canonical fallback. Bytecode/Wasm
             * dispatch through the rebound data callback. Both must deopt. */
            REQUIRE(replaced_calls == (mode == 1 && backends[b] != BEHAVIOR_SLJIT ? 8 : 0));
            REQUIRE(mode == 0 ? observations > 0 && observations <= 512 : observations == 0);
            if (mode < 2) {
                REQUIRE(copies.counts.value_buffer_bytes >= 4 * 8 * 9);
            }
            if (mode == 0) {
                inspected_bytes = copies.counts.value_buffer_bytes;
                if (backends[b] == BEHAVIOR_SLJIT) {
                    REQUIRE(runs.counts.inspected_sljit_entries > 0);
                }
            } else if (mode == 1) {
                rebound_bytes = copies.counts.value_buffer_bytes;
            } else {
                REQUIRE(linear(8, 8));
                REQUIRE(copies.counts.value_buffer_bytes < inspected_bytes);
                REQUIRE(copies.counts.value_buffer_bytes < rebound_bytes);
            }
        }
    }
    return true;
}
TEST_CASE(scalar_append_reader_callback_contract)
{
    arena_init(&storage, 4096);
    char text[] = "row";
    pin_value item = { .type = PIN_STR, .type_name = "const char *", .str = text };
    pin_array array = { .element_type = PIN_STR, .count = 1, .capacity = 1, .items = &item };
    pin_value args[] = { { .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = &array },
                         { .type = PIN_I32 } };
    pin_value held[2];
    const node_host_call *get[] = { &NATIVE_ARRAY_get_HOST, &NATIVE_ARRAY_get_CALL };
    const node_host_call *length[] = { &NATIVE_ARRAY_length_HOST,
                                       &NATIVE_ARRAY_length_CALL };
    REQUIRE(get[0]->value == pin_array_get_owned_call && get[1]->value == pin_array_get_call);
    REQUIRE(length[0]->value == pin_array_length_owned_call && length[1]->value == pin_array_length_call);
    for (int mode = 0; mode < 2; mode++) {
        pin_copy_scope_reset(&copies);
        pin_copy_scope_begin(&copies);
        const char *error = get[mode]->value(&storage, args, 2, &held[mode]);
        pin_copy_scope_end(&copies);
        REQUIRE(!error && !copies.counts.overflow);
        REQUIRE(held[mode].type == PIN_STR && held[mode].str != text);
        REQUIRE(!strcmp(held[mode].str, "row") && !strcmp(held[mode].type_name, "const char *"));
        REQUIRE(copies.counts.text_bytes == (uint64_t)(mode + 1) * sizeof text);
        REQUIRE(copies.counts.nominal_bytes == (uint64_t)(mode + 1) * sizeof "const char *");
        REQUIRE(copies.counts.value_header_moves == (uint64_t)mode + 1);
        REQUIRE(!copies.counts.value_buffer_bytes && !copies.counts.record_bytes);
        for (int bad = 0; bad < 2; bad++) {
            args[1].i = bad ? 1 : -1;
            pin_value out = { .type = PIN_I32, .i = 99 };
            error = get[mode]->value(&storage, args, 2, &out);
            REQUIRE(error && !strcmp(error, "data function produced an invalid result"));
            REQUIRE(out.type == PIN_I32 && out.container == PIN_CONTAINER_SINGLE && out.i == 99);
            REQUIRE(!out.type_name && !out.record);
        }
        args[1].i = 0;
        pin_value out;
        pin_copy_scope_reset(&copies);
        pin_copy_scope_begin(&copies);
        error = length[mode]->value(&storage, args, 1, &out);
        pin_copy_scope_end(&copies);
        REQUIRE(!error && out.type == PIN_I32 && out.i == 1);
        REQUIRE(copies.counts.value_header_moves == (uint64_t)mode);
        REQUIRE(
            !copies.counts.text_bytes && !copies.counts.nominal_bytes &&
            !copies.counts.record_bytes && !copies.counts.value_buffer_bytes
        );
        args[0].ptr = NULL;
        REQUIRE(!length[mode]->value(&storage, args, 1, &out) && out.i == 0);
        args[0].ptr = &array;
    }
    text[0] = 'x';
    REQUIRE(!strcmp(held[0].str, "row") && !strcmp(held[1].str, "row"));
    ((char *)held[0].str)[0] = 'y';
    REQUIRE(!strcmp(held[1].str, "row") && !strcmp(text, "xow"));
    return true;
}
/* Seven direct tail appends, one checked overflow rejection and two fixed
 * collections. No source preparation, authored entry or execution backend. */
static behavior_function tail_function;
static behavior_data_storage *tail_data;
static void tail_cleanup()
{
    behavior_function_free_data(tail_data);
    tail_data = NULL;
    afree(tail_function.appends);
    afree(tail_function.data_values);
    tail_function = (behavior_function){ 0 };
}
static const void *visited_tail;
static int tail_visits;
static void visit_tail(void *user, const pin_value *value)
{
    (void)user;
    if (value->container == PIN_CONTAINER_ARRAY && value->ptr == visited_tail) {
        tail_visits++;
    }
}
TEST_CASE(scalar_append_tail_fork_and_collection_roots)
{
    cleanup();
    arena_init_collectable(&storage);
    pin_array empty = { .element_type = PIN_I32, .stride = sizeof(int32_t) };
    pin_value prefix = { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = &empty };
    apush(tail_function.appends, ((behavior_append_plan){ 0 }));
    apush(
        tail_function.data_values,
        ((behavior_function_data_value){ .initial = prefix, .kind = BEHAVIOR_VALUE })
    );
    behavior_function_prepare_data(&tail_function, NULL, &tail_data);
    REQUIRE(tail_data && acount(tail_data->values) == 2 && acount(tail_data->buffers) == 2);
    pin_array_builder *builder = tail_data->buffers[1];
    pin_value first = { 0 };
    for (int i = 0; i < 4; i++) {
        pin_value args[] = { prefix, { .type = PIN_I32, .i = i } };
        REQUIRE(!pin_array_build_i32(&storage, builder, args, &prefix));
        tail_data->values[1] = prefix;
        REQUIRE(
            ((pin_array *)prefix.ptr)->count == i + 1 &&
            builder->latest == prefix.ptr
        );
        if (!i) {
            first = prefix;
        }
    }
    pin_value prior = prefix;
    REQUIRE(((pin_array *)first.ptr)->count == 1);
    REQUIRE(((pin_array *)first.ptr)->data == ((pin_array *)prior.ptr)->data);
    REQUIRE(((pin_array *)prior.ptr)->count < ((pin_array *)prior.ptr)->capacity);
    pin_value fork_args[] = { first, { .type = PIN_I32, .i = 77 } };
    REQUIRE(!pin_array_build_i32(&storage, builder, fork_args, &prefix));
    tail_data->values[1] = prefix;
    REQUIRE(((pin_array *)prefix.ptr)->data != ((pin_array *)prior.ptr)->data);
    REQUIRE(pin_array_get(prefix.ptr, 1).i == 77 && pin_array_get(prior.ptr, 1).i == 1);
    void *fork_data = ((pin_array *)prefix.ptr)->data;
    pin_value next_args[] = { prefix, { .type = PIN_I32, .i = 88 } };
    REQUIRE(!pin_array_build_i32(&storage, builder, next_args, &prefix));
    tail_data->values[1] = prefix;
    REQUIRE(((pin_array *)prefix.ptr)->data == fork_data);
    pin_value escaped;
    REQUIRE(!pin_value_snapshot_to_arena(&storage, &escaped, &prefix));
    REQUIRE(((pin_array *)escaped.ptr)->data != fork_data);
    pin_value changed = { .type = PIN_I32, .i = -99 };
    REQUIRE(!pin_array_set(escaped.ptr, 0, &changed));
    REQUIRE(pin_array_get(prefix.ptr, 0).i == 0 && pin_array_get(first.ptr, 0).i == 0);

    behavior_function_context roots = { .data = tail_data, .value_storage = &storage };
    visited_tail = prefix.ptr;
    tail_visits = 0;
    behavior_function_visit_values(&tail_function, &roots, visit_tail, NULL);
    REQUIRE(tail_visits == 1);
    arena *arenas[] = { &storage };
    arena_collection collection;
    arena_collect_begin(&collection, arenas, 1);
    behavior_function_trace_values(&tail_function, &roots, arena_collect_mark, &collection);
    arena_collect_mark(&collection, first.ptr);
    arena_collect_mark(&collection, prior.ptr);
    arena_collect_mark(&collection, escaped.ptr);
    arena_collect_end(&collection);
    REQUIRE(
        ((pin_array *)prefix.ptr)->count == 3 && pin_array_get(prefix.ptr, 2).i == 88
    );
    REQUIRE(((pin_array *)first.ptr)->count == 1 && pin_array_get(prior.ptr, 3).i == 3);
    pin_value after_args[] = { prefix, { .type = PIN_I32, .i = 99 } };
    REQUIRE(!pin_array_build_i32(&storage, builder, after_args, &prefix));
    tail_data->values[1] = prefix;
    REQUIRE(
        ((pin_array *)prefix.ptr)->data == fork_data &&
        pin_array_get(prefix.ptr, 3).i == 99
    );
    pin_array overflow = { .element_type = PIN_I32,
                           .count = INT_MAX,
                           .capacity = INT_MAX,
                           .data = &changed.i,
                           .stride = sizeof(int32_t) };
    pin_value overflow_args[] = {
        { .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = &overflow },
        changed
    };
    pin_value sentinel = { .type = PIN_I32, .i = 909 }, rejected = sentinel;
    const pin_array *latest = builder->latest;
    REQUIRE(pin_array_build_i32(&storage, builder, overflow_args, &rejected));
    REQUIRE(!memcmp(&rejected, &sentinel, sizeof rejected) && builder->latest == latest);
    behavior_function_append_reset(tail_data);
    REQUIRE(!builder->latest && !tail_data->values[1].ptr);
    arena_collect_begin(&collection, arenas, 1);
    behavior_function_trace_values(&tail_function, &roots, arena_collect_mark, &collection);
    arena_collect_end(&collection);
    size_t used, capacity;
    arena_usage(&storage, &used, &capacity);
    REQUIRE(!used);
    tail_cleanup();
    return true;
}

void test_language_scalar_append()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(scalar_append_two_accumulators_and_scalar_helper);
    RUN_TEST_CASE(scalar_append_retained_prefix_and_escaped_snapshot);
    RUN_TEST_CASE(scalar_append_reuses_context_after_arena_reset);
    RUN_TEST_CASE(scalar_append_inspection_and_rebound_callback_fallback);
    RUN_TEST_CASE(scalar_append_reader_callback_contract);
    RUN_TEST_CASE(scalar_append_tail_fork_and_collection_roots);
    pu_clear_setup();
}
