#include "backend.h"
#include "ecs/ecs.h"
#include "execution/delay.h"
#include "execution/function.h"
#include "execution/function_builder.h"
#include "execution/inspection.h"
#include "execution/module.h"
#include "execution/module_builder.h"
#include "execution/status.h"
#include "language/lower.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    RUN_EVENT = 10,
    TEXT_EVENT,
    WAIT_EVENT,
    DELAY_ACTION,
    REENTER_EVENT,
    WIDE_VALUES = 4096,
    CACHE_LIMIT = 64 * 1024
};
static const script_catalog_parameter amount = { .name = "amount",
                                                 .type = { .storage = PIN_F64 } };
static const script_catalog_parameter text = { .name = "value",
                                               .type = { .storage = PIN_STR } };
static const script_catalog_parameter duration = { .name = "Duration",
                                                   .type = { .storage = PIN_F64 } };
static const char *const completed[] = { "Completed" };
static const script_catalog_capability capabilities[] = {
    { .id = RUN_EVENT,
      .name = "Run",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = &amount,
      .parameter_count = 1 },
    { .id = TEXT_EVENT,
      .name = "Text",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = &text,
      .parameter_count = 1 },
    { .id = WAIT_EVENT,
      .name = "Wait",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = &amount,
      .parameter_count = 1 },
    { .id = DELAY_ACTION,
      .name = "Delay",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .parameters = &duration,
      .parameter_count = 1,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_SUSPEND },
    { .id = REENTER_EVENT,
      .name = "Reenter",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = &amount,
      .parameter_count = 1 },
};
static const script_catalog catalog = { .capabilities = capabilities,
                                        .capability_count = 5 };
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module *module;
static execution_module_plan *wide_plan;
static execution_program *program;
static execution_inspector *inspector;
static ecs_world *world;
static char *large_payload;
static uint64_t owners[2];
static execution_instance *instances[2];

static void cleanup()
{
    execution_program_free(program);
    execution_inspector_free(inspector);
    execution_module_free(module);
    execution_module_plan_free(wide_plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    ecs_world_free(world);
    free(large_payload);
    program = NULL;
    inspector = NULL;
    module = NULL;
    wide_plan = NULL;
    analysis = NULL;
    syntax = NULL;
    world = NULL;
    large_payload = NULL;
}
static bool host(void *, uint64_t id, script_lower_host *out)
{
    if (id != DELAY_ACTION) {
        return false;
    }
    *out = (script_lower_host){ .command = &EXECUTION_Delay };
    return true;
}
static bool prepare(const char *source, behavior_backend backend)
{
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &catalog);
    if (!analysis->valid) {
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            fprintf(
                stderr,
                "activation cache semantic: %s\n",
                analysis->diagnostics[i].message
            );
        }
        return false;
    }
    const script_lower_bindings bindings = { .host = host };
    execution_module_plan *plan = script_lower(analysis, &catalog, &bindings);
    if (!plan || !plan->valid) {
        for (int i = 0; plan && i < plan->diagnostic_count; i++) {
            fprintf(stderr, "activation cache lower: %s\n", plan->diagnostics[i].message);
        }
        execution_module_plan_free(plan);
        return false;
    }
    char error[256];
    module = execution_module_finish(&plan, backend, error, sizeof error);
    execution_module_plan_free(plan);
    if (!module) {
        fprintf(stderr, "activation cache publish: %s\n", error);
        return false;
    }
    world = ecs_world_new();
    program = execution_program_new(module, world, NULL);
    if (!program) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        owners[i] = ecs_entity_to_u64(ecs_world_create_entity(world));
        instances[i] = execution_program_attach(program, owners[i]);
        if (!instances[i]) {
            return false;
        }
    }
    return true;
}
static uint64_t state_id(const char *name)
{
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        if (symbol->kind == SCRIPT_STATE && !strcmp(symbol->name, name)) {
            return symbol->id;
        }
    }
    return 0;
}
static bool total(int owner, double expected)
{
    const pin_value *value = execution_instance_state(instances[owner], state_id("total"));
    return value && value->type == PIN_F64 && value->d == expected;
}
static int dispatch(int owner, int event, pin_value argument, double now)
{
    return execution_instance_dispatch(instances[owner], event, &argument, 1, now, 0, 1000);
}
static int run(int owner, double amount, double now)
{
    return dispatch(owner, RUN_EVENT, (pin_value){ .type = PIN_F64, .d = amount }, now);
}
static bool cached()
{
    execution_memory_usage usage = execution_program_memory_usage(program);
    return usage.activation_bytes == 0 && usage.activation_cache_bytes > 0 &&
           usage.activation_cache_bytes <= CACHE_LIMIT &&
           usage.payload_capacity == 0;
}
static void count_runs(void *user, const execution_profile_sample *sample)
{
    (void)sample;
    (*(int *)user)++;
}
TEST_CASE(activation_cache_rebinds_owner_and_preserves_reentrant_work)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(prepare(
            "Cache :: behavior { total:retain f64=0; Work :: (value:f64) "
            "{ total+=value; if value<0 { await Delay(1); } } Leaf :: "
            "(value:f64)->f64 { return value; } Forward :: "
            "(value:f64)->f64 { return Leaf(value); } on "
            "Run :: (amount: f64) { total+=Forward(amount); } on "
            "Reenter :: (amount: f64) { total+=amount; child:task=Work(7); "
            "total+=amount; } }",
            backends[backend]
        ));
        REQUIRE(
            execution_instance_set_state(
                instances[1],
                state_id("total"),
                (pin_value){ .type = PIN_F64, .d = 10 }
            ) == FLOW_OK
        );
        REQUIRE(run(0, 1, 0) == FLOW_OK && cached());
        printf(
            "ACTIVATION_CACHE backend=%s retained_bytes=%zu limit=%d\n",
            behavior_backend_name(backends[backend]),
            execution_program_memory_usage(program).activation_cache_bytes,
            CACHE_LIMIT
        );
        xalloc_stats before, after;
        xalloc_stats_get(&before);
        int first = run(1, 2, 1);
        int second = run(0, 3, 2);
        xalloc_stats_get(&after);
        REQUIRE(first == FLOW_OK && second == FLOW_OK);
        REQUIRE(after.calls == before.calls);
        REQUIRE(total(0, 4) && total(1, 12) && cached());
        REQUIRE(
            execution_instance_dispatch(instances[0], RUN_EVENT, NULL, 0, 2.5, 0, 1000) == FLOW_OK
        );
        REQUIRE(total(0, 4) && cached());
        /* A task starts a distinct activation while this handler is still
         * live. The untaken wait makes it a task that completes immediately. */
        int samples = 0;
        execution_program_observe(program, count_runs, &samples);
        int nested_status = dispatch(0, REENTER_EVENT, (pin_value){ .type = PIN_F64, .d = 2 }, 3);
        execution_program_observe(program, NULL, NULL);
        REQUIRE(nested_status == FLOW_OK && samples == 2);
        REQUIRE(total(0, 15) && total(1, 12) && cached());
        REQUIRE(dispatch(1, REENTER_EVENT, (pin_value){ .type = PIN_F64, .d = 3 }, 4) == FLOW_OK);
        REQUIRE(total(0, 15) && total(1, 25) && cached());
        REQUIRE(execution_program_replace(program, module, false) == FLOW_OK);
        REQUIRE(execution_program_memory_usage(program).activation_cache_bytes == 0);
        for (int i = 0; i < 2; i++) {
            instances[i] = execution_program_instance(program, owners[i]);
            REQUIRE(instances[i]);
        }
        REQUIRE(total(0, 15) && total(1, 25));
        REQUIRE(run(1, 4, 5) == FLOW_OK && total(1, 29) && cached());
        cleanup();
    }
    return true;
}
TEST_CASE(activation_cache_releases_managed_calls_and_failed_runs)
{
    const char
        *source = "Cache :: behavior { total:retain f64=0; kept:retain "
                  "string=\"\"; Echo :: (value:string)->string { return value; "
                  "} on Text :: (value: string) { kept=Echo(value); } on "
                  "Run :: (amount: f64) { if amount<0 { values:=[10,20]; "
                  "total=values[int(-amount)]; } else { total+=amount; } "
                  "} }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(prepare(source, backends[backend]));
        char first[] = "first", second[] = "second", third[] = "third!";
        REQUIRE(
            dispatch(
                0,
                TEXT_EVENT,
                (pin_value){ .type = PIN_STR,
                             .text = &(const pin_string){ .bytes = first,
                                                          .length = (uint32_t)strlen(first) } },
                0
            ) == FLOW_OK &&
            cached()
        );
        REQUIRE(
            dispatch(
                1,
                TEXT_EVENT,
                (pin_value){ .type = PIN_STR,
                             .text = &(const pin_string){ .bytes = second,
                                                          .length = (uint32_t)strlen(second) } },
                1
            ) == FLOW_OK &&
            cached()
        );
        first[0] = second[0] = 'X';
        const pin_value *one = execution_instance_state(instances[0], state_id("kept"));
        const pin_value *two = execution_instance_state(instances[1], state_id("kept"));
        REQUIRE(
            one && two && !strcmp(pin_text(*one).bytes, "first") &&
            !strcmp(pin_text(*two).bytes, "second")
        );
        REQUIRE(
            dispatch(
                1,
                TEXT_EVENT,
                (pin_value){ .type = PIN_STR,
                             .text = &(const pin_string){ .bytes = third,
                                                          .length = (uint32_t)strlen(third) } },
                1.5
            ) == FLOW_OK &&
            cached()
        );
        third[0] = 'X';
        one = execution_instance_state(instances[0], state_id("kept"));
        two = execution_instance_state(instances[1], state_id("kept"));
        REQUIRE(
            one && two && !strcmp(pin_text(*one).bytes, "first") &&
            !strcmp(pin_text(*two).bytes, "third!")
        );
        REQUIRE(run(0, 1, 2) == FLOW_OK && cached());
        REQUIRE(run(0, -2, 3) < 0);
        REQUIRE(execution_program_memory_usage(program).activation_cache_bytes == 0);
        REQUIRE(execution_program_error(program));
        REQUIRE(run(1, 3, 4) == FLOW_OK && total(0, 1) && total(1, 3) && cached());
        REQUIRE(
            dispatch(
                0,
                RUN_EVENT,
                (pin_value){
                    .type = PIN_STR,
                    .text = &(const pin_string){ .bytes = "wrong",
                                                 .length = sizeof("wrong") - 1 } },
                5
            ) == FLOW_ERR_ARGUMENT
        );
        REQUIRE(execution_program_memory_usage(program).activation_cache_bytes == 0);
        REQUIRE(run(0, 2, 6) == FLOW_OK && total(0, 3) && cached());
        cleanup();
    }
    return true;
}
TEST_CASE(activation_cache_reuses_completed_payloads_with_bounded_retention)
{
    const char *source = "Cache :: behavior { total:retain f64=0; on "
                         "Run :: (amount: f64) { values:=[amount,amount+1]; "
                         "values[0]+=2; total+=values[0]+values[1]; } on "
                         "Text :: () { total+=1; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(prepare(source, backends[backend]));
        REQUIRE(run(0, 1, 0) == FLOW_OK && total(0, 5) && cached());
        REQUIRE(run(1, 2, 1) == FLOW_OK && run(0, 3, 2) == FLOW_OK);
        REQUIRE(total(0, 14) && total(1, 7) && cached());

        /* An input larger than the complete idle cache budget is shared by
         * reference, so the idle activation keeps none of it. */
        large_payload = xmalloc(CACHE_LIMIT + 1);
        memset(large_payload, 'x', CACHE_LIMIT);
        large_payload[CACHE_LIMIT] = '\0';
        REQUIRE(
            dispatch(
                0,
                TEXT_EVENT,
                (pin_value){ .type = PIN_STR,
                             .text = &(const pin_string){ .bytes = large_payload,
                                                          .length = (uint32_t)strlen(large_payload) } },
                3
            ) == FLOW_OK
        );
        REQUIRE(total(0, 15) && cached());
        free(large_payload);
        large_payload = NULL;

        REQUIRE(
            dispatch(
                1,
                TEXT_EVENT,
                (pin_value){
                    .type = PIN_STR,
                    .text = &(const pin_string){ .bytes = "tiny",
                                                 .length = sizeof("tiny") - 1 } },
                4
            ) == FLOW_OK &&
            total(1, 8) && cached()
        );
        int small = dispatch(
            0,
            TEXT_EVENT,
            (pin_value){ .type = PIN_STR,
                         .text = &(const pin_string){ .bytes = "tiny",
                                                      .length = sizeof("tiny") - 1 } },
            5
        );
        REQUIRE(small == FLOW_OK && total(0, 16) && cached());

        /* Each run's array lives on the program's managed heap until a later
         * event boundary collects it, so later runs keep no more of the heap
         * than earlier ones. */
        size_t peaks[2] = { 0 };
        for (int i = 0; i < 256; i++) {
            REQUIRE(run(i % 2, 1, 6 + i) == FLOW_OK && cached());
            size_t managed = execution_program_memory_usage(program).managed_bytes;
            peaks[i / 128] = managed > peaks[i / 128] ? managed : peaks[i / 128];
        }
        REQUIRE(peaks[0] > 0 && peaks[1] <= peaks[0]);
        cleanup();
    }
    return true;
}
/* A helper called many times in one run reuses its spare frame each time:
 * retention is counted once per retired frame, never drifting toward the
 * budget, so later runs allocate nothing and keep the same cache size. */
TEST_CASE(activation_cache_reuses_helper_frames_across_many_calls)
{
    const char *source = "Cache :: behavior { total: retain f64=0; "
                         "Leaf :: (value:f64)->f64 { return value + 1; } "
                         "on Run :: (amount: f64) { i:int=0; "
                         "while i < 300 { total += Leaf(amount); i += 1; } } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(prepare(source, backends[backend]));
        REQUIRE(run(0, 1, 0) == FLOW_OK && total(0, 600) && cached());
        size_t bytes = execution_program_memory_usage(program).activation_cache_bytes;
        for (int pass = 1; pass <= 3; pass++) {
            xalloc_stats before, after;
            xalloc_stats_get(&before);
            int status = run(0, 1, pass);
            xalloc_stats_get(&after);
            REQUIRE(status == FLOW_OK && total(0, 600.0 * (pass + 1)) && cached());
            REQUIRE(
                after.calls == before.calls && after.requested_bytes == before.requested_bytes
            );
            REQUIRE(execution_program_memory_usage(program).activation_cache_bytes == bytes);
        }
        cleanup();
    }
    return true;
}
/* A callee whose frame alone exceeds the idle budget is reused while its run
 * lasts, so a run sets up the same frames however often it calls it; the
 * settled activation keeps no more than the idle budget. */
TEST_CASE(activation_cache_reuses_large_frames_within_a_run)
{
    enum
    {
        ITEMS = 3000
    };
    static const char
        head[] = "Cache :: behavior { total: retain f64=0; "
                 "Big :: (value:f64)->f64 { values:=[value",
        item[] = ",value",
        tail[] = "]; return values[0]; } "
                 "on Run :: (amount: f64) { i:int=0; "
                 "while i < int(amount) { total += Big(1); i += 1; } } }";
    char *source = xmalloc(sizeof head + (ITEMS - 1) * (sizeof item - 1) + sizeof tail);
    char *at = source;
    memcpy(at, head, sizeof head - 1);
    at += sizeof head - 1;
    for (int i = 1; i < ITEMS; i++) {
        memcpy(at, item, sizeof item - 1);
        at += sizeof item - 1;
    }
    memcpy(at, tail, sizeof tail);
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(prepare(source, backends[backend]));
        REQUIRE(run(0, 2, 0) == FLOW_OK && total(0, 2));
        uint64_t frames[2];
        for (int pass = 0; pass < 2; pass++) {
            uint64_t before = behavior_function_data_allocations();
            REQUIRE(run(0, pass ? 12 : 2, pass + 1) == FLOW_OK);
            REQUIRE(execution_program_memory_usage(program).activation_cache_bytes <= CACHE_LIMIT);
            frames[pass] = behavior_function_data_allocations() - before;
        }
        REQUIRE(total(0, 16) && frames[1] == frames[0]);
        cleanup();
    }
    free(source);
    return true;
}
TEST_CASE(activation_cache_keeps_waits_and_inspection_independent)
{
    const char *source = "Cache :: behavior { total:retain f64=0; on "
                         "Run :: (amount: f64) { total+=amount; } on Wait "
                         ":: (amount: f64) "
                         "{ total+=amount; await Delay(1); total+=amount; } }";
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        REQUIRE(prepare(source, backends[backend]));
        REQUIRE(dispatch(0, WAIT_EVENT, (pin_value){ .type = PIN_F64, .d = 3 }, 0) == FLOW_OK);
        REQUIRE(total(0, 3) && execution_instance_pending(instances[0]) == 1);
        REQUIRE(execution_program_memory_usage(program).activation_cache_bytes == 0);
        REQUIRE(run(1, 7, .1) == FLOW_OK && total(1, 7));
        REQUIRE(execution_program_memory_usage(program).activation_cache_bytes > 0);
        REQUIRE(execution_program_pump(program, .5, 0, 1000) == FLOW_OK && total(0, 3));
        REQUIRE(execution_program_pump(program, 1, 0, 1000) == FLOW_OK);
        REQUIRE(total(0, 6) && execution_instance_pending(instances[0]) == 0 && cached());
        inspector = execution_program_inspector(program);
        REQUIRE(inspector && execution_inspector_pause(inspector, owners[0]));
        REQUIRE(run(0, 2, 2) == FLOW_OK && execution_program_paused(program));
        REQUIRE(total(0, 6) && execution_program_memory_usage(program).activation_cache_bytes == 0);
        REQUIRE(execution_inspector_control(inspector, EXECUTION_DEBUG_RESUME) == FLOW_OK);
        REQUIRE(execution_program_pump(program, 2, 0, 1000) == FLOW_OK && total(0, 8));
        REQUIRE(execution_instance_pending(instances[0]) == 0);
        REQUIRE(run(1, 1, 3) == FLOW_OK && total(1, 8));
        REQUIRE(execution_program_memory_usage(program).activation_cache_bytes == 0);
        cleanup();
    }
    return true;
}
static const char *fill_wide_host(void *, const pin_value *, pin_value *results)
{
    for (int i = 0; i < WIDE_VALUES; i++) {
        results[i].d = i;
    }
    return NULL;
}
TEST_CASE(activation_cache_does_not_retain_oversized_argument_banks)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int backend = 0; backend < 2; backend++) {
        for (int host_bank = 0; host_bank < 2; host_bank++) {
            pin_value *arguments = xcalloc(WIDE_VALUES, sizeof *arguments);
            for (int i = 0; i < WIDE_VALUES; i++) {
                arguments[i].type = PIN_F64;
            }
            behavior_function_builder *builder = behavior_function_builder_new();
            if (host_bank) {
                static const node_host_call host = { .invoke = fill_wide_host };
                static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
                int call = behavior_function_builder_host(builder, &host, &access, NULL, 0, arguments, WIDE_VALUES);
                behavior_function_builder_emit(
                    builder,
                    (behavior_function_instruction){ .op = BF_HOST_CALL,
                                                     .target = call,
                                                     .left = BEHAVIOR_FUNCTION_HOST_DATA }
                );
            } else {
                behavior_function_builder_signature(builder, arguments, WIDE_VALUES, NULL, 0, NULL, 0);
            }
            behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_RETURN });
            behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
            char error[256];
            behavior_function *function = behavior_function_builder_finish(builder, error, sizeof error);
            behavior_function_builder_free(builder);
            free(arguments);
            REQUIRE(function);
            wide_plan = execution_module_plan_new();
            wide_plan->valid = true;
            wide_plan->id = 42;
            apush(
                wide_plan->functions,
                ((execution_module_function){ .id = 43,
                                              .kind = EXECUTION_FUNCTION_SYSTEM,
                                              .body = function })
            );
            wide_plan->function_count = 1;
            module = execution_module_finish(&wide_plan, backends[backend], error, sizeof error);
            REQUIRE(module);
            world = ecs_world_new();
            program = execution_program_new(module, world, NULL);
            REQUIRE(program);
            REQUIRE(
                execution_program_attach(program, ecs_entity_to_u64(ecs_world_create_entity(world)))
            );
            for (int i = 0; i < 2; i++) {
                REQUIRE(execution_program_step(program, i, 0, 1000) == FLOW_OK);
                execution_memory_usage usage = execution_program_memory_usage(program);
                REQUIRE(usage.activation_bytes == 0 && usage.activation_cache_bytes == 0);
            }
            cleanup();
        }
    }
    return true;
}
void test_language_activation_cache()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(activation_cache_rebinds_owner_and_preserves_reentrant_work);
    RUN_TEST_CASE(activation_cache_releases_managed_calls_and_failed_runs);
    RUN_TEST_CASE(activation_cache_reuses_completed_payloads_with_bounded_retention);
    RUN_TEST_CASE(activation_cache_reuses_helper_frames_across_many_calls);
    RUN_TEST_CASE(activation_cache_reuses_large_frames_within_a_run);
    RUN_TEST_CASE(activation_cache_keeps_waits_and_inspection_independent);
    RUN_TEST_CASE(activation_cache_does_not_retain_oversized_argument_banks);
    pu_clear_setup();
}
