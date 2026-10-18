#include "execution/runner.h"

#include "execution/definition.h"
#include "execution/status.h"
#include "execution/work_internal.h"
#include "support/array.h"

#include <assert.h>
#ifdef __EMSCRIPTEN__
#include "execution/wasm_jit.h"
#endif

static _Thread_local behavior_run_scope *run_scope;

void behavior_run_scope_begin(behavior_run_scope *scope)
{
    assert(scope && !scope->active);
    scope->previous = run_scope;
    scope->active = true;
    run_scope = scope;
}

void behavior_run_scope_end(behavior_run_scope *scope)
{
    assert(scope && scope->active && run_scope == scope);
    run_scope = scope->previous;
    scope->previous = NULL;
    scope->active = false;
}

void behavior_run_scope_reset(behavior_run_scope *scope)
{
    assert(scope && !scope->active);
    scope->counts = (behavior_run_counts){ 0 };
}

static void increment(behavior_run_counts *counts, uint64_t *value)
{
    if (*value == UINT64_MAX) {
        counts->overflow = true;
        return;
    }
    ++*value;
}

void behavior_run_record(execution_run_event event)
{
    if (!run_scope) {
        return;
    }
    behavior_run_counts *counts = &run_scope->counts;
    switch (event) {
        case EXECUTION_RUN_BYTECODE:
            increment(counts, &counts->bytecode_entries);
            return;
        case EXECUTION_RUN_SLJIT:
        case EXECUTION_RUN_INSPECTED_SLJIT:
            increment(counts, &counts->sljit_entries);
            if (event == EXECUTION_RUN_INSPECTED_SLJIT) {
                increment(counts, &counts->inspected_sljit_entries);
            }
            return;
        case EXECUTION_RUN_WASM:
        case EXECUTION_RUN_INSPECTED_WASM:
            increment(counts, &counts->wasm_entries);
            if (event == EXECUTION_RUN_INSPECTED_WASM) {
                increment(counts, &counts->inspected_wasm_entries);
            }
            return;
        case EXECUTION_RUN_INSPECTION_BYTECODE_FALLBACK:
            increment(counts, &counts->inspection_bytecode_fallbacks);
            return;
    }
    assert(false);
}

static int run_compiled(const behavior_function *f, behavior_function_context *ctx)
{
#ifdef __EMSCRIPTEN__
    return behavior_function_run_wasm(f, ctx);
#else
    bool inspected = behavior_function_inspection_required(f, ctx);
    assert(f->artifact->code);
    if (inspected && !behavior_function_compile_inspection(f)) {
        behavior_run_record(EXECUTION_RUN_INSPECTION_BYTECODE_FALLBACK);
        return behavior_function_run_interpreted(f, ctx);
    }
    void *entry = inspected ? f->artifact->inspection_code
                            : behavior_function_entry(f);
    behavior_run_record(inspected ? EXECUTION_RUN_INSPECTED_SLJIT : EXECUTION_RUN_SLJIT);
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
    if (ctx->work && f->root_steps) {
        int result = behavior_function_reject_work(ctx);
        if (ctx->failure_observer) {
            ctx->failure_observer(f, ctx, result);
        }
        if (ctx->owned_host) {
            behavior_function_release_host(ctx);
        }
        return result;
    }
    bool owned = !ctx->queries && !ctx->commands &&
                 (acount(f->queries) || acount(f->commands) ||
                  (!ctx->data && (acount(f->host_calls) ||
                                  acount(f->data_values) || acount(f->calls))));
    if (owned) {
        behavior_function_prepare_host(f, ctx);
        ctx->owned_host = true;
    }
    if (f->appends && behavior_function_inspection_required(f, ctx)) {
        behavior_function_append_reset(ctx->data);
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
