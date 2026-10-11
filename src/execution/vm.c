#include "execution/definition.h"
#include "execution/machine.h"
#include "execution/status.h"
#include "support/array.h"

#include <assert.h>
#ifdef __EMSCRIPTEN__
#include "execution/wasm_jit.h"
#endif

static int run(const behavior_function *f, behavior_function_context *ctx, bool native)
{
    if (native) {
#ifdef __EMSCRIPTEN__
        return behavior_function_run_wasm(f, ctx);
#else
        bool inspected = ctx->inspect || ctx->watch || ctx->trace;
        assert(f->artifact->code);
        void *entry = inspected && f->artifact->inspection_code
                        ? f->artifact->inspection_code
                        : behavior_function_entry(f);
        return ((int (*)(behavior_function_context *))entry)(ctx);
#endif
    }
    execution_machine machine;
    execution_machine_begin(&machine, f, ctx, true);
    int instruction_count = acount(machine.instructions);
    int result = FLOW_OK;
    for (int pc = machine.pc; pc < instruction_count; pc = machine.pc) {
        result = execution_machine_step(&machine, pc);
        if (result != EXECUTION_CONTINUE) {
            break;
        }
        result = FLOW_OK;
    }
    execution_machine_end(&machine, result);
    return result;
}

int behavior_function_run_once(const behavior_function *f, behavior_function_context *ctx, bool native)
{
    ctx->source_function = f;
    ctx->source_points = f->source_points;
    ctx->source_watches = f->source_watches;
    ctx->points = f->points;
    ctx->watches = f->watches;
    ctx->edges = f->edges;
    bool owned = !ctx->queries && !ctx->commands &&
                 (acount(f->queries) || acount(f->commands) ||
                  (!ctx->data && (acount(f->host_calls) ||
                                  acount(f->data_values) || acount(f->calls))));
    if (owned) {
        behavior_function_prepare_host(f, ctx);
        ctx->owned_host = true;
    }
    int result = run(f, ctx, native);
    if (result < 0 && ctx->failure_observer) {
        ctx->failure_observer(f, ctx, result);
    }
    if (ctx->owned_host && result != FLOW_SUSPENDED_RESULT &&
        result != FLOW_PAUSED_RESULT && result != BEHAVIOR_FUNCTION_CALL_RESULT) {
        behavior_function_release_host(ctx);
    }
    return result;
}

int behavior_function_run(const behavior_function *f, behavior_function_context *ctx, bool native)
{
    if (f->calls || (ctx->activation && ctx->activation->calls)) {
        return behavior_function_run_calls(f, ctx, native);
    }
    return behavior_function_run_once(f, ctx, native);
}
