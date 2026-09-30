#include "graph/compiler/behavior_function_internal.h"
#include "graph/runtime/flow_internal.h"
#include "test/suite_internal.h"
#include "test/whole_loop_fixture.h"
#include "test/whole_suspend_fixture.h"

#include <dlfcn.h>

typedef struct
{
    int pc, frame;
    uintptr_t steps, root_steps;
    int count, limit;
} checkpoint;
static void parity(int kind, int budget, bool expressions, int (*generated)(behavior_function_context *))
{
    behavior_asset *a = whole_loop_fixture(kind);
    graph *g = a->graphs[1];
    behavior_function_diagnostic diagnostic;
    behavior_function *f = behavior_function_lower(a, g, g->nodes[0], &diagnostic);
    assert(f);
    whole_suspend_instrument(f, expressions);
    assert(behavior_function_compile(f));
    pin_value args[] = { { .type = PIN_I32, .i = -1 }, { .type = PIN_I32, .i = 2 } };
    behavior_instance *oracle = behavior_instance_create(a, NULL, NULL, NULL);
    assert(!behavior_instance_set_backend(oracle, BEHAVIOR_ORACLE));
    flow_options options = { .instance = oracle, .step_budget = budget };
    flow_error error;
    flow_run *run = flow_run_start(g, g->nodes[0], (flow_inputs){ args, 2 }, &options, &error);
    assert(run);
    int expected = flow_run_resume(run);
    uint64_t eval = eval_oracle_calls(), flow = flow_oracle_calls();
    checkpoint *checkpoints = NULL;
    for (int mode = 0; mode < (generated ? 4 : 3); mode++) {
        behavior_instance *instance = behavior_instance_create(a, NULL, NULL, NULL);
        behavior_function_activation activation = { 0 };
        behavior_function_activation_init(f, &activation);
        pin_value result[2] = { 0 };
        behavior_function_context ctx = { .activation = &activation,
                                          .arguments = args,
                                          .argument_count = 2,
                                          .results = result,
                                          .budget = budget };
        behavior_instance_bind_values(instance, &ctx);
        int status, at = 0;
        do {
            status = mode == 3
                       ? generated(&ctx)
                       : behavior_function_run(f, &ctx, mode == 1 || (mode == 2 && at % 2));
            if (status != FLOW_SUSPENDED_RESULT) {
                break;
            }
            checkpoint c = { .pc = activation.pc,
                             .frame = activation.frame,
                             .steps = activation.steps,
                             .root_steps = ctx.steps,
                             .count = ctx.variables[0].i,
                             .limit = ctx.variables[1].i };
            if (!mode) {
                apush(checkpoints, c);
            } else {
                assert(at < acount(checkpoints));
                checkpoint e = checkpoints[at];
                assert(
                    c.pc == e.pc && c.frame == e.frame && c.steps == e.steps &&
                    c.root_steps == e.root_steps && c.count == e.count &&
                    c.limit == e.limit
                );
            }
            assert(++at < 5000);
        } while (true);
        assert(status == expected && ctx.steps == flow_run_steps(run));
        assert(
            ctx.variables[0].i == oracle->variables[0].i &&
            ctx.variables[1].i == oracle->variables[1].i
        );
        if (status) {
            assert(ctx.node_id == flow_run_error(run)->node->id);
            assert(ctx.error_steps == flow_run_error(run)->steps);
            assert(ctx.error_pin == flow_run_error(run)->pin);
        } else {
            assert(result[1].i == flow_run_input_value(run, g->nodes[1], 1)->i);
        }
        assert(!activation.pc && (!mode || at == acount(checkpoints)));
        behavior_function_activation_free(&activation);
        behavior_instance_destroy(instance);
    }
    assert(eval == eval_oracle_calls() && flow == flow_oracle_calls());
    afree(checkpoints);
    flow_run_free(run);
    behavior_instance_destroy(oracle);
    behavior_function_free(f);
    behavior_asset_free(a);
}
static void float_parity(pin_type type, void *library, bool raw)
{
    behavior_asset *a = whole_suspend_float_fixture(type);
    graph *g = a->graphs[1];
    behavior_function_diagnostic diagnostic;
    behavior_function *f = behavior_function_lower(a, g, g->nodes[0], &diagnostic);
    assert(f);
    if (raw) {
        behavior_function_free(f);
        f = whole_suspend_float_ir(type);
    } else {
        whole_suspend_instrument(f, true);
    }
    assert(behavior_function_compile(f));
    int (*generated)(
        behavior_function_context *
    ) = library
          ? (int (*)(behavior_function_context *))dlsym(
                library,
                raw ? (type == PIN_F32 ? "suspend_raw_f32" : "suspend_raw_f64")
                    : (type == PIN_F32 ? "suspend_f32" : "suspend_f64")
            )
          : NULL;
    assert(!library || generated);
    for (int mode = 0; mode < (generated ? 4 : 3); mode++) {
        behavior_function_activation activation = { 0 };
        behavior_function_activation_init(f, &activation);
        pin_value args[3], result[2] = { 0 };
        double inputs[] = { 3.5, 7., 2. };
        for (int j = 0; j < 3; j++) {
            args[j] = (pin_value){ .type = type };
            if (type == PIN_F32) {
                args[j].f = inputs[j];
            }
            if (type == PIN_F64) {
                args[j].d = inputs[j];
            }
        }
        behavior_function_context ctx = { .activation = &activation,
                                          .arguments = args,
                                          .argument_count = 3,
                                          .results = result,
                                          .budget = 1000 };
        int status, count = 0;
        do {
            status = mode == 3
                       ? generated(&ctx)
                       : behavior_function_run(f, &ctx, mode == 1 || (mode == 2 && count % 2));
            assert(++count < 100);
        } while (status == FLOW_SUSPENDED_RESULT);
        assert(!status && (type == PIN_F32 ? result[1].f : result[1].d) == 7.);
        behavior_function_activation_free(&activation);
    }
    behavior_function_free(f);
    behavior_asset_free(a);
}
void test_compiled_suspensions()
{
    for (int kind = 0; kind < WHOLE_LOOP_COUNT; kind++) {
        for (int budget = 1; budget <= 45; budget++) {
            parity(kind, budget, false, NULL);
            parity(kind, budget, true, NULL);
        }
    }
    float_parity(PIN_F32, NULL, false);
    float_parity(PIN_F32, NULL, true);
    float_parity(PIN_F64, NULL, false);
    float_parity(PIN_F64, NULL, true);
    test_suspended_commands(NULL);
    whole_suspend_smoke();
    test_note("PASS: suspension checkpoints and oracle parity for nested calls, loops, Break, Return, state, errors and budgets\n");
}
void emit_suspend_c(const char *path)
{
    FILE *out = fopen(path, "w");
    assert(out);
    for (int kind = 0; kind < WHOLE_LOOP_COUNT; kind++) {
        for (int expression = 0; expression < 2; expression++) {
            behavior_asset *a = whole_loop_fixture(kind);
            graph *g = a->graphs[1];
            behavior_function_diagnostic diagnostic;
            behavior_function *f = behavior_function_lower(a, g, g->nodes[0], &diagnostic);
            assert(f);
            whole_suspend_instrument(f, expression);
            char name[40];
            snprintf(name, sizeof name, "generated_suspend%d_%d", kind, expression);
            behavior_function_write_c(f, out, name);
            behavior_function_free(f);
            behavior_asset_free(a);
        }
    }
    for (int wide = 0; wide < 2; wide++) {
        behavior_asset *a = whole_suspend_float_fixture(wide ? PIN_F64 : PIN_F32);
        graph *g = a->graphs[1];
        behavior_function_diagnostic diagnostic;
        behavior_function *f = behavior_function_lower(a, g, g->nodes[0], &diagnostic);
        assert(f);
        whole_suspend_instrument(f, true);
        behavior_function_write_c(f, out, wide ? "suspend_f64" : "suspend_f32");
        behavior_function_free(f);
        f = whole_suspend_float_ir(wide ? PIN_F64 : PIN_F32);
        behavior_function_write_c(f, out, wide ? "suspend_raw_f64" : "suspend_raw_f32");
        behavior_function_free(f);
        behavior_asset_free(a);
    }
    emit_suspended_commands(out);
    assert(!fclose(out));
}
void test_generated_suspensions(const char *path)
{
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    assert(library);
    for (int kind = 0; kind < WHOLE_LOOP_COUNT; kind++) {
        for (int expression = 0; expression < 2; expression++) {
            char name[40];
            snprintf(name, sizeof name, "generated_suspend%d_%d", kind, expression);
            int (*fn)(
                behavior_function_context *
            ) = (int (*)(behavior_function_context *))dlsym(library, name);
            assert(fn);
            for (int budget = 1; budget <= 45; budget++) {
                parity(kind, budget, expression, fn);
            }
        }
    }
    float_parity(PIN_F32, library, false);
    float_parity(PIN_F32, library, true);
    float_parity(PIN_F64, library, false);
    float_parity(PIN_F64, library, true);
    test_suspended_commands(library);
    assert(!dlclose(library));
    test_note("PASS: generated-C, native, bytecode and mixed-backend continuation parity against oracle\n");
}
