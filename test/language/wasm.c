#include "backend.h"
#include "execution/module.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "language/lower.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include "../../src/execution/wasm_jit.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "support/array.h"
#include "support/xalloc.h"
#ifdef __EMSCRIPTEN_PTHREADS__
#include <pthread.h>
#endif

static const script_catalog_capability run_event = {
    .id = 10,
    .name = "Run",
    .kind = SCRIPT_CAPABILITY_EVENT,
};
static const script_catalog catalog = { .capabilities = &run_event,
                                        .capability_count = 1 };
static execution_module *module;
static execution_program *program;
static unsigned char *prepared;
static uint64_t total_id;
static behavior_function *scratch_function;
static behavior_function_activation scratch_activation;
static behavior_function_context scratch_context;
static arena scratch_storage;
static behavior_function **live_functions;

static void cleanup()
{
    execution_program_free(program);
    execution_module_free(module);
    free(prepared);
    behavior_function_release_host(&scratch_context);
    behavior_function_activation_free(&scratch_activation);
    behavior_function_free(scratch_function);
    arena_free(&scratch_storage);
    scratch_function = NULL;
    scratch_context = (behavior_function_context){ 0 };
    for (int i = 0; i < acount(live_functions); i++) {
        behavior_function_free(live_functions[i]);
    }
    afree(live_functions);
    program = NULL;
    module = NULL;
    prepared = NULL;
    total_id = 0;
}

static bool compile(const char *text, behavior_backend backend)
{
    script_syntax *syntax = script_parse(text);
    script_analysis *analysis = script_analyze(syntax, &catalog);
    bool valid = analysis->valid;
    for (int i = 0; valid && i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].kind == SCRIPT_STATE &&
            !strcmp(analysis->symbols[i].name, "total")) {
            total_id = analysis->symbols[i].id;
        }
    }
    const script_lower_bindings bindings = { 0 };
    execution_module_plan *plan = valid ? script_lower(analysis, &catalog, &bindings)
                                        : NULL;
    char error[256] = { 0 };
    if (plan && plan->valid) {
        module = execution_module_finish(&plan, backend, error, sizeof error);
    }
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    if (!module) {
        fprintf(stderr, "wasm fixture preparation: %s\n", error);
    }
    return module && total_id;
}

static execution_instance *attach()
{
    program = execution_program_new(module, NULL, NULL);
    return program ? execution_program_attach(program, 1) : NULL;
}

static bool total(execution_instance *instance, double expected)
{
    const pin_value *value = execution_instance_state(instance, total_id);
    return value && value->type == PIN_F64 && value->d == expected;
}

TEST_CASE(wasm_default_executes_emitted_code_with_numeric_and_array_helpers)
{
    REQUIRE(behavior_default_backend() == BEHAVIOR_WASM);
    REQUIRE(behavior_backend_available(BEHAVIOR_WASM));
    const char
        *source = "B :: behavior { total: f64 = 0;Pair :: (x: f64) -> "
                  "(r: f64[]) { return [x, x + 1] }Step :: (x: int) -> (r: "
                  "f64) {if (x & 1) == 0 { return f64(x * 3) "
                  "}return f64(-x) }on Run :: () { total = 0; i: int = 0; "
                  "while i < 8 {row := Pair(Step(i)); total += row[0] + "
                  "row[1]; i += 1 } } }";
    execution_wasm_jit_stats before, after;
    execution_wasm_jit_get_stats(&before);
    REQUIRE(compile(source, behavior_default_backend()));
    execution_instance *instance = attach();
    REQUIRE(instance);
    REQUIRE(execution_instance_dispatch(instance, 10, NULL, 0, 0, 0, 10000) == FLOW_OK);
    REQUIRE(total(instance, 48));
    execution_wasm_jit_get_stats(&after);
    REQUIRE(after.compilations > before.compilations);
    REQUIRE(after.executions > before.executions);
    REQUIRE(after.compile_failures == before.compile_failures);
    cleanup();
    REQUIRE(compile(source, BEHAVIOR_BYTECODE));
    instance = attach();
    REQUIRE(instance);
    REQUIRE(execution_instance_dispatch(instance, 10, NULL, 0, 0, 0, 10000) == FLOW_OK);
    REQUIRE(total(instance, 48));
    return true;
}

/* A definition whose generated module would pass the size budget runs on
 * bytecode, with the same result, and its runs count as size fallbacks. */
TEST_CASE(wasm_oversized_definition_runs_on_bytecode)
{
    enum
    {
        STEPS = 3000
    };
    static const char
        head[] = "B :: behavior { total: f64 = 0;on Run :: () { total = 0;";
    static const char step[] = "total += 1;";
    char *source = xmalloc(sizeof head + STEPS * (sizeof step - 1) + 3);
    char *at = source + sprintf(source, "%s", head);
    for (int i = 0; i < STEPS; i++) {
        at += sprintf(at, "%s", step);
    }
    sprintf(at, "} }");
    bool compiled = compile(source, BEHAVIOR_WASM);
    free(source);
    REQUIRE(compiled);
    execution_instance *instance = attach();
    REQUIRE(instance);
    behavior_run_scope scope = { 0 };
    behavior_run_scope_begin(&scope);
    int status = execution_instance_dispatch(instance, 10, NULL, 0, 0, 0, 1000000);
    behavior_run_scope_end(&scope);
    REQUIRE(status == FLOW_OK);
    REQUIRE(total(instance, STEPS));
    REQUIRE(scope.counts.size_bytecode_fallbacks > 0 && !scope.counts.wasm_entries);
    REQUIRE(scope.counts.bytecode_entries == scope.counts.size_bytecode_fallbacks);
    return true;
}

TEST_CASE(wasm_helper_resumes_and_budget_failure_is_reported)
{
    REQUIRE(
        compile("B :: behavior { total: f64 = 0;Later :: (x: f64) -> (r: f64) { yield; return x * 2 }on Run :: () { total = 1; total += await Later(4); total += 10 } }", BEHAVIOR_WASM)
    );
    execution_instance *instance = attach();
    REQUIRE(instance);
    REQUIRE(execution_instance_dispatch(instance, 10, NULL, 0, 0, 0, 10000) == FLOW_OK);
    REQUIRE(total(instance, 1));
    REQUIRE(execution_instance_pending(instance) == 1);
    REQUIRE(execution_program_update(program, 1, 0, 10000) == FLOW_OK);
    REQUIRE(total(instance, 19));
    REQUIRE(execution_instance_pending(instance) == 0);
    cleanup();
    REQUIRE(
        compile("B :: behavior { total: f64 = 0;on Run :: () { while true { total += 1 } } }", BEHAVIOR_WASM)
    );
    instance = attach();
    REQUIRE(instance);
    REQUIRE(execution_instance_dispatch(instance, 10, NULL, 0, 0, 0, 20) == FLOW_ERR_STEP_BUDGET);
    const execution_program_failure *failure = execution_program_error(program);
    REQUIRE(failure && failure->kind == BEHAVIOR_FUNCTION_BUDGET);
    return true;
}

TEST_CASE(wasm_integer_operations_preserve_width_and_exact_bits)
{
    const char
        *source = "B :: behavior { total: f64 = 0;a: i64 = "
                  "9007199254740993; b: i64 = -8;u: u64 = "
                  "18446744073709551615; small: i8 = -2;on Run :: () { total "
                  "= "
                  "0;if a + 2 == 9007199254740995 { total += 1 }if a << 1 == "
                  "18014398509481986 { total += 1 }if ~a == -9007199254740994 "
                  "{ total += 1 }if b >> 1 == -4 { total += 1 }if u >> 63 == 1 "
                  "{ total += 1 }if small >> 7 == -1 { total += 1 } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_WASM };
    for (int i = 0; i < 2; i++) {
        cleanup();
        REQUIRE(compile(source, backends[i]));
        execution_instance *instance = attach();
        REQUIRE(instance);
        REQUIRE(execution_instance_dispatch(instance, 10, NULL, 0, 0, 0, 10000) == FLOW_OK);
        REQUIRE(total(instance, 6));
    }
    return true;
}

static behavior_function *scratch_body(int slots, int value)
{
    behavior_function_builder *builder = behavior_function_builder_new();
    pin_value number = { .type = PIN_I32 };
    behavior_function_builder_signature(builder, NULL, 0, &number, 1, NULL, 0);
    for (int i = 0; i < slots; i++) {
        behavior_function_builder_slot(builder, PIN_I32);
    }
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){
            .op = BF_CONSTANT,
            .type = PIN_I32,
            .output = slots - 1,
            .value = { .type = PIN_I32, .i = value },
        }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){
            .op = BF_RESULT,
            .type = PIN_I32,
            .left = slots - 1,
        }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    behavior_function *body = behavior_function_builder_finish(builder, error, sizeof error);
    behavior_function_builder_free(builder);
    return body;
}

TEST_CASE(wasm_canonical_body_reuses_storage_after_smaller_vm_body)
{
    scratch_function = scratch_body(16, 42);
    REQUIRE(scratch_function);
    scratch_function->fast = scratch_body(1, 42);
    REQUIRE(scratch_function->fast);
    pin_value result = { .type = PIN_I32 };
    scratch_context = (behavior_function_context){
        .activation = &scratch_activation,
        .results = &result,
        .budget = 100,
    };
    behavior_function_bind_storage(&scratch_context, &scratch_storage);
    REQUIRE(behavior_function_bind(scratch_function, &scratch_context, NULL, 0));
    xalloc_stats before, after;
    xalloc_stats_get(&before);
    REQUIRE(behavior_function_run(scratch_function, &scratch_context, false) == FLOW_OK);
    xalloc_stats_get(&after);
    REQUIRE(result.i == 42);
    /* Catch insufficient capacity before executing a canonical write beyond
     * the compact body's bank, so the regression fails without heap damage. */
    size_t canonical_bytes = (size_t)(acount(scratch_function->slots) + 1) *
                             sizeof(pin_value);
    REQUIRE(after.requested_bytes - before.requested_bytes >= canonical_bytes);
    behavior_function_vm_storage *bank = scratch_activation.vm;
    REQUIRE(bank);
    REQUIRE(behavior_function_compile(scratch_function));
    result.i = 0;
    REQUIRE(behavior_function_run(scratch_function, &scratch_context, true) == FLOW_OK);
    REQUIRE(result.i == 42);
    REQUIRE(scratch_activation.vm == bank);
    return true;
}

static bool runs_value(const behavior_function *function, int expected)
{
    pin_value result = { .type = PIN_I32 };
    arena storage = { 0 };
    behavior_function_context context = { .results = &result, .budget = 100 };
    behavior_function_bind_storage(&context, &storage);
    bool bound = behavior_function_bind(function, &context, NULL, 0);
    int status = bound ? behavior_function_run(function, &context, true) : FLOW_ERR_STATE;
    behavior_function_release_host(&context);
    arena_free(&storage);
    return status == FLOW_OK && result.type == PIN_I32 && result.i == expected;
}

static int inspect_point(void *, const behavior_function_point *)
{
    return false;
}

TEST_CASE(wasm_inspection_entry_is_lazy_reused_and_retired_with_its_function)
{
    execution_wasm_jit_stats before, normal, inspected, reused, retired;
    execution_wasm_jit_get_stats(&before);
    scratch_function = scratch_body(1, 42);
    REQUIRE(scratch_function && behavior_function_compile(scratch_function));
    pin_value result = { .type = PIN_I32 };
    scratch_context = (behavior_function_context){
        .activation = &scratch_activation,
        .results = &result,
        .budget = 100,
    };
    behavior_function_bind_storage(&scratch_context, &scratch_storage);
    REQUIRE(behavior_function_bind(scratch_function, &scratch_context, NULL, 0));
    REQUIRE(behavior_function_run(scratch_function, &scratch_context, true) == FLOW_OK);
    REQUIRE(result.i == 42);
    execution_wasm_jit_get_stats(&normal);
    REQUIRE(normal.compilations == before.compilations + 1);
    REQUIRE(normal.live_entries == before.live_entries + 1);

    scratch_context.inspect = inspect_point;
    result.i = 0;
    REQUIRE(behavior_function_run(scratch_function, &scratch_context, true) == FLOW_OK);
    REQUIRE(result.i == 42);
    execution_wasm_jit_get_stats(&inspected);
    REQUIRE(inspected.compilations == normal.compilations + 1);
    REQUIRE(inspected.live_entries == normal.live_entries + 1);
    REQUIRE(behavior_function_run(scratch_function, &scratch_context, true) == FLOW_OK);
    scratch_context.inspect = NULL;
    REQUIRE(behavior_function_run(scratch_function, &scratch_context, true) == FLOW_OK);
    execution_wasm_jit_get_stats(&reused);
    REQUIRE(reused.compilations == inspected.compilations);
    REQUIRE(reused.live_entries == inspected.live_entries);
    REQUIRE(reused.compile_failures == before.compile_failures);
    cleanup();
    execution_wasm_jit_get_stats(&retired);
    REQUIRE(retired.live_entries == before.live_entries);
    return true;
}

#define LIVE_FUNCTION_COUNT 257
TEST_CASE(wasm_live_functions_keep_their_installed_entries)
{
    execution_wasm_jit_stats before, published, after;
    execution_wasm_jit_get_stats(&before);
    for (int i = 0; i < LIVE_FUNCTION_COUNT; i++) {
        behavior_function *function = scratch_body(1, i + 1000);
        apush(live_functions, function);
        REQUIRE(function);
        REQUIRE(behavior_function_compile(function));
    }
    execution_wasm_jit_get_stats(&published);
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < LIVE_FUNCTION_COUNT; i++) {
            REQUIRE(runs_value(live_functions[i], i + 1000));
        }
    }
    execution_wasm_jit_get_stats(&after);
    /* All these artifacts remain live. A fixed-size LRU would evict their
     * registrations and compile the entire workload again on every pass. */
    REQUIRE(after.compilations == published.compilations);
    REQUIRE(after.executions == published.executions + 2 * LIVE_FUNCTION_COUNT);
    REQUIRE(after.compile_failures == published.compile_failures);
    REQUIRE(published.live_entries == before.live_entries + LIVE_FUNCTION_COUNT);
    cleanup();
    execution_wasm_jit_get_stats(&after);
    REQUIRE(after.live_entries == before.live_entries);
    return true;
}

TEST_CASE(wasm_retiring_one_function_preserves_other_live_entries)
{
    for (int i = 0; i < 2; i++) {
        behavior_function *function = scratch_body(1, 100 + i);
        apush(live_functions, function);
        REQUIRE(function);
        REQUIRE(behavior_function_compile(function));
        REQUIRE(runs_value(function, 100 + i));
    }
    execution_wasm_jit_stats before, after;
    execution_wasm_jit_get_stats(&before);
    behavior_function_free(live_functions[0]);
    live_functions[0] = NULL;
    REQUIRE(runs_value(live_functions[1], 101));
    REQUIRE(runs_value(live_functions[1], 101));
    execution_wasm_jit_get_stats(&after);
    REQUIRE(after.compilations == before.compilations);
    REQUIRE(after.executions == before.executions + 2);
    REQUIRE(after.compile_failures == before.compile_failures);
    REQUIRE(after.live_entries + 1 == before.live_entries);
    return true;
}

TEST_CASE(wasm_prepared_reload_owns_its_compiled_generation)
{
    REQUIRE(
        compile("B :: behavior { total: f64 = 0;Add :: (x: f64) -> (r: f64) { return x + 3 }on Run :: () { total += Add(4) } }", BEHAVIOR_WASM)
    );
    size_t length = 0;
    char error[256];
    REQUIRE(execution_prepared_write(module, &prepared, &length, error, sizeof error));
    execution_module_free(module);
    module = NULL;
    module = execution_prepared_read(prepared, length, NULL, BEHAVIOR_WASM, error, sizeof error);
    REQUIRE(module);
    free(prepared);
    prepared = NULL;
    execution_instance *instance = attach();
    REQUIRE(instance);
    /* The live program owns this generation after both its producer and the
     * prepared input bytes have retired. */
    execution_module_free(module);
    module = NULL;
    REQUIRE(execution_instance_dispatch(instance, 10, NULL, 0, 0, 0, 10000) == FLOW_OK);
    REQUIRE(execution_instance_dispatch(instance, 10, NULL, 0, 1, 0, 10000) == FLOW_OK);
    REQUIRE(total(instance, 14));
    return true;
}

#ifdef __EMSCRIPTEN_PTHREADS__
typedef struct
{
    execution_program *program;
    execution_instance *instance;
    uint64_t state;
    int status;
    double result;
} worker_run;

static void *run_on_worker(void *user)
{
    worker_run *run = user;
    run->status = execution_instance_dispatch(run->instance, 10, NULL, 0, 0, 0, 10000);
    if (run->status != FLOW_OK) {
        return NULL;
    }
    run->status = execution_instance_dispatch(run->instance, 10, NULL, 0, 1, 0, 10000);
    const pin_value *value = execution_instance_state(run->instance, run->state);
    if (value && value->type == PIN_F64) {
        run->result = value->d;
    }
    return NULL;
}

TEST_CASE(wasm_workers_install_and_retire_independent_entries)
{
    for (int generation = 0; generation < 2; generation++) {
        cleanup();
        const char *sources[] = {
            "B :: behavior { total: f64 = 0; on Run :: () { total += 7 } }",
            "B :: behavior { total: f64 = 0; on Run :: () { total += 11 } }",
        };
        REQUIRE(compile(sources[generation], BEHAVIOR_WASM));
        worker_run runs[2] = { 0 };
        pthread_t workers[2];
        /* Module reference changes are serialized. Workers share only the
         * immutable definition; all runtime state belongs to their program. */
        for (int i = 0; i < 2; i++) {
            runs[i].program = execution_program_new(module, NULL, NULL);
            REQUIRE(runs[i].program);
            runs[i].instance = execution_program_attach(runs[i].program, 1);
            REQUIRE(runs[i].instance);
            runs[i].state = total_id;
        }
        execution_wasm_jit_stats before, after;
        execution_wasm_jit_get_stats(&before);
        for (int i = 0; i < 2; i++) {
            REQUIRE(pthread_create(&workers[i], NULL, run_on_worker, &runs[i]) == 0);
        }
        for (int i = 0; i < 2; i++) {
            REQUIRE(pthread_join(workers[i], NULL) == 0);
            REQUIRE(runs[i].status == FLOW_OK);
            REQUIRE(runs[i].result == (generation ? 22 : 14));
        }
        execution_wasm_jit_get_stats(&after);
        REQUIRE(after.compilations >= before.compilations + 2);
        REQUIRE(after.executions >= before.executions + 4);
        REQUIRE(after.compile_failures == before.compile_failures);
        REQUIRE(after.live_entries == before.live_entries);
        for (int i = 0; i < 2; i++) {
            execution_program_free(runs[i].program);
        }
        /* Retire the old generation after its workers have finished. */
        execution_module_free(module);
        module = NULL;
    }
    return true;
}
#endif

void test_language_wasm()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(wasm_default_executes_emitted_code_with_numeric_and_array_helpers);
    RUN_TEST_CASE(wasm_oversized_definition_runs_on_bytecode);
    RUN_TEST_CASE(wasm_helper_resumes_and_budget_failure_is_reported);
    RUN_TEST_CASE(wasm_integer_operations_preserve_width_and_exact_bits);
    RUN_TEST_CASE(wasm_canonical_body_reuses_storage_after_smaller_vm_body);
    RUN_TEST_CASE(wasm_inspection_entry_is_lazy_reused_and_retired_with_its_function);
    RUN_TEST_CASE(wasm_live_functions_keep_their_installed_entries);
    RUN_TEST_CASE(wasm_retiring_one_function_preserves_other_live_entries);
    RUN_TEST_CASE(wasm_prepared_reload_owns_its_compiled_generation);
#ifdef __EMSCRIPTEN_PTHREADS__
    RUN_TEST_CASE(wasm_workers_install_and_retire_independent_entries);
#endif
    pu_clear_setup();
}
#else
TEST_CASE(wasm_backend_is_unavailable_on_native)
{
    REQUIRE(!behavior_backend_available(BEHAVIOR_WASM));
    return true;
}
void test_language_wasm()
{
    RUN_TEST_CASE(wasm_backend_is_unavailable_on_native);
}
#endif
