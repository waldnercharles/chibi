#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/suite_internal.h"
#include "test/whole_command_fixture.h"
#include "test/whole_suspend_fixture.h"

#include <dlfcn.h>

static void same(const pin_value *a, const pin_value *b, int count)
{
    for (int j = 0; j < count; j++) {
        assert(a[j].type == b[j].type && a[j].container == b[j].container);
        assert(!memcmp(&a[j].d, &b[j].d, behavior_scalar_size(a[j].type)));
        assert((a[j].type_name != NULL) == (b[j].type_name != NULL));
        assert(!a[j].type_name || !strcmp(a[j].type_name, b[j].type_name));
    }
}
void test_suspended_commands(void *library)
{
    for (int kind = 0; kind < WHOLE_COMMAND_COUNT; kind++) {
        for (int fail = 0; fail < 2; fail++) {
            behavior_asset *a = whole_command_fixture(kind, false, false);
            graph *g = a->graphs[acount(a->graphs) - 1];
            behavior_function_diagnostic diagnostic;
            behavior_function *f = behavior_function_lower(a, g, g->nodes[0], &diagnostic);
            assert(f);
            whole_suspend_instrument(f, true);
            assert(behavior_function_compile(f));
            pin_value items[] = { { .type = PIN_F64, .type_name = "first", .d = -0. },
                                  { .type = PIN_F64, .d = 3.25 },
                                  { .type = PIN_F64, .type_name = "last", .d = -2.75 } };
            pin_array array = { .element_type = PIN_F64,
                                .type_name = "double",
                                .items = items,
                                .count = 3,
                                .capacity = 3 };
            int (*generated)(behavior_function_context *) = NULL;
            if (library) {
                char name[40];
                snprintf(name, sizeof name, "generated_suspend_command%d", kind);
                generated = (int (*)(behavior_function_context *))dlsym(library, name);
                assert(generated);
            }
            pin_value args[] = {
                { .type = PIN_I32, .type_name = "signed", .i = -123 },
                { .type = PIN_F64, .type_name = "wide", .d = 0x1p-140 },
                { .type = PIN_F32, .type_name = "small", .f = -0.0f },
                { .type = PIN_HANDLE,
                  .type_name = "ecs.entity",
                  .u64 = UINT64_C(0xfabc0123456789) },
                { .type = PIN_F64,
                  .type_name = "double",
                  .container = PIN_CONTAINER_ARRAY,
                  .ptr = &array }
            };
            whole_command_capture expected = { 0 };
            arena values = { 0 };
            pin_value expected_result[5] = { 0 }, expected_variables[4] = { 0 };
            int expected_status = 0;
            unsigned long expected_steps = 0;
            for (int mode = 0; mode < (generated ? 5 : 4); mode++) {
                whole_command_capture capture = { .record = true,
                                                  .fail_at = 2,
                                                  .error = fail ? FLOW_ERR_STATE : 0 };
                behavior_instance *instance = behavior_instance_create(a, NULL, NULL, NULL);
                assert(!behavior_instance_bind_service(instance, "test.commands", &capture));
                pin_value result[5] = { 0 };
                for (int j = 1; j < 5; j++) {
                    result[j] = g->nodes[1]->input_pins[j].default_value;
                }
                if (!mode) {
                    assert(!behavior_instance_set_backend(instance, BEHAVIOR_ORACLE));
                    flow_options options = { .instance = instance };
                    flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ args, 5 }, &options, NULL);
                    assert(run);
                    expected_status = flow_run_resume(run);
                    expected_steps = flow_run_steps(run);
                    if (!expected_status) {
                        for (int j = 1; j < 5; j++) {
                            assert(!pin_value_snapshot_to_arena(
                                &values,
                                &expected_result[j],
                                flow_run_input_value(run, g->nodes[1], j)
                            ));
                        }
                    }
                    for (int j = 0; j < 4; j++) {
                        assert(!pin_value_snapshot_to_arena(
                            &values,
                            &expected_variables[j],
                            &instance->variables[j]
                        ));
                    }
                    expected = capture;
                    flow_run_free(run);
                    behavior_instance_destroy(instance);
                    continue;
                }
                behavior_function_activation activation = { 0 };
                behavior_function_activation_init(f, &activation);
                behavior_function_context ctx = { .activation = &activation,
                                                  .arguments = args,
                                                  .argument_count = 5,
                                                  .results = result,
                                                  .budget = 10000 };
                behavior_instance_bind_values(instance, &ctx);
                assert(behavior_function_bind(f, &ctx, instance->services, 0));
                int status, count = 0;
                uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
                do {
                    if (mode == 4) {
                        if (!ctx.commands) {
                            behavior_function_prepare_host(f, &ctx);
                            ctx.owned_host = true;
                        }
                        status = generated(&ctx);
                        if (status != FLOW_SUSPENDED_RESULT) {
                            behavior_function_release_host(&ctx);
                        }
                    } else {
                        status = behavior_function_run(f, &ctx, mode == 2 || (mode == 3 && count % 2));
                    }
                    if (status == FLOW_SUSPENDED_RESULT) {
                        /* Arguments partially assembled before a yield must retain their buffer. */
                        assert(ctx.commands && ctx.command_arguments && ctx.owned_host);
                    }
                    assert(++count < 5000);
                } while (status == FLOW_SUSPENDED_RESULT);
                if (status != expected_status || capture.calls != expected.calls) {
                    fprintf(
                        stderr,
                        "suspended command mismatch: kind=%d fail=%d mode=%d "
                        "resumes=%d status=%d/%d calls=%llu/%llu failure=%d "
                        "source=%d pin=%d\n",
                        kind,
                        fail,
                        mode,
                        count,
                        status,
                        expected_status,
                        (unsigned long long)capture.calls,
                        (unsigned long long)expected.calls,
                        ctx.failure,
                        ctx.error_source,
                        ctx.error_pin
                    );
                }
                assert(status == expected_status && capture.calls == expected.calls);
                assert(ctx.steps == expected_steps);
                assert(
                    capture.checksum == expected.checksum &&
                    acount(capture.values) == acount(expected.values)
                );
                same(capture.values, expected.values, acount(capture.values));
                same(instance->variables, expected_variables, 4);
                if (!status) {
                    same(result + 1, expected_result + 1, 4);
                }
                assert(!ctx.owned_host && !ctx.commands && !ctx.command_arguments);
                assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
                /* Reuse after completion, then cancel at the first suspension. */
                ctx.steps = 0;
                assert(behavior_function_run(f, &ctx, true) == FLOW_SUSPENDED_RESULT);
                behavior_function_release_host(&ctx);
                behavior_function_activation_free(&activation);
                whole_command_clear(&capture);
                behavior_instance_destroy(instance);
            }
            whole_command_clear(&expected);
            arena_free(&values);
            behavior_function_free(f);
            behavior_asset_free(a);
        }
    }
    test_note("PASS: suspended command arguments, FP bits, scalar ownership, host errors, completion and cancellation cleanup\n");
}

void emit_suspended_commands(FILE *out)
{
    for (int kind = 0; kind < WHOLE_COMMAND_COUNT; kind++) {
        behavior_asset *a = whole_command_fixture(kind, false, false);
        graph *g = a->graphs[acount(a->graphs) - 1];
        behavior_function_diagnostic diagnostic;
        behavior_function *f = behavior_function_lower(a, g, g->nodes[0], &diagnostic);
        assert(f);
        whole_suspend_instrument(f, true);
        char name[40];
        snprintf(name, sizeof name, "generated_suspend_command%d", kind);
        behavior_function_write_c(f, out, name);
        behavior_function_free(f);
        behavior_asset_free(a);
    }
}
