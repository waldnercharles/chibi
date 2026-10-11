#include "execution/runner.h"

#include "execution/definition.h"
#include "execution/status.h"
#include "support/array.h"

#include <assert.h>
#ifdef __EMSCRIPTEN__
#include "execution/wasm_jit.h"
#endif

static int run_compiled(const behavior_function *f, behavior_function_context *ctx)
{
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

int behavior_function_run_once(const behavior_function *f, behavior_function_context *ctx, bool compiled)
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
    int result = compiled ? run_compiled(f, ctx)
                          : behavior_function_run_interpreted(f, ctx);
    if (result < 0 && ctx->failure_observer) {
        ctx->failure_observer(f, ctx, result);
    }
    if (ctx->owned_host && result != FLOW_SUSPENDED_RESULT &&
        result != FLOW_PAUSED_RESULT && result != BEHAVIOR_FUNCTION_CALL_RESULT) {
        behavior_function_release_host(ctx);
    }
    return result;
}

int behavior_function_run(const behavior_function *f, behavior_function_context *ctx, bool compiled)
{
    if (f->calls || (ctx->activation && ctx->activation->calls)) {
        return behavior_function_run_calls(f, ctx, compiled);
    }
    return behavior_function_run_once(f, ctx, compiled);
}
