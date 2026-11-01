#include "execution/runner.h"

#include "execution/definition.h"
#include "execution/status.h"
#include "support/array.h"

#include <assert.h>
#ifdef __EMSCRIPTEN__
#include "execution/wasm_jit.h"
#endif

static _Thread_local behavior_run_scope *run_scope;
static _Thread_local behavior_execution_scope *execution_scope;

const behavior_function_source *behavior_function_failure_source(const behavior_function_context *ctx)
{
    const behavior_function *f = ctx ? ctx->source_function : NULL;
    return f && ctx->error_source >= 0 && ctx->error_source < acount(f->sources)
             ? &f->sources[ctx->error_source]
             : NULL;
}

void behavior_execution_scope_begin(behavior_execution_scope *scope)
{
    assert(scope && !scope->active);
    scope->previous = execution_scope;
    scope->active = true;
    execution_scope = scope;
}
void behavior_execution_scope_end(behavior_execution_scope *scope)
{
    assert(scope && scope->active && execution_scope == scope);
    execution_scope = scope->previous;
    scope->previous = NULL;
    scope->active = false;
}
bool behavior_execution_scope_active()
{
    return execution_scope != NULL;
}
static behavior_execution_scope *inherited_scope(behavior_execution_scope *scope)
{
    return scope->inherit ? scope->previous : NULL;
}
bool behavior_execution_scope_pure()
{
    for (behavior_execution_scope *scope = execution_scope; scope;
         scope = inherited_scope(scope)) {
        if (scope->pure) {
            return true;
        }
    }
    return false;
}
bool behavior_execution_scope_cancelled()
{
    for (behavior_execution_scope *scope = execution_scope; scope;
         scope = inherited_scope(scope)) {
        if (scope->cancelled && scope->cancelled(scope->user)) {
            return true;
        }
    }
    return false;
}
const char *behavior_execution_scope_step()
{
    if (!execution_scope) {
        return NULL;
    }
    if (behavior_execution_scope_cancelled()) {
        return "execution cancelled";
    }
    for (behavior_execution_scope *scope = execution_scope; scope;
         scope = inherited_scope(scope)) {
        if (!scope->remaining) {
            return "shared instruction budget exhausted";
        }
    }
    for (behavior_execution_scope *scope = execution_scope; scope;
         scope = inherited_scope(scope)) {
        scope->remaining--;
    }
    return NULL;
}

static bool scope_prefers_compiled()
{
    for (behavior_execution_scope *scope = execution_scope; scope;
         scope = inherited_scope(scope)) {
        if (!scope->prefer_compiled || scope->pure) {
            return false;
        }
    }
    return execution_scope != NULL;
}

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
        case EXECUTION_RUN_SIZE_BYTECODE_FALLBACK:
            increment(counts, &counts->size_bytecode_fallbacks);
            return;
        case EXECUTION_RUN_NATIVE:
            increment(counts, &counts->native_entries);
            return;
    }
    assert(false);
}

static int run_compiled(const behavior_function *f, behavior_function_context *ctx)
{
#ifdef __EMSCRIPTEN__
    if (f->artifact->interpreted) {
        behavior_run_record(EXECUTION_RUN_SIZE_BYTECODE_FALLBACK);
        return behavior_function_run_interpreted(f, ctx);
    }
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
    bool scoped = scope_prefers_compiled() &&
                  !behavior_function_inspection_required(f, ctx) &&
                  behavior_function_compile_scoped(f);
    compiled &= !behavior_execution_scope_active();
    ctx->source_function = f;
    ctx->source_points = f->source_points;
    ctx->source_watches = f->source_watches;
    ctx->points = f->points;
    ctx->watches = f->watches;
    ctx->edges = f->edges;
    bool owned = !ctx->commands &&
                 (acount(f->commands) ||
                  (!ctx->data && (acount(f->host_calls) ||
                                  acount(f->data_values) || acount(f->calls))));
    if (owned) {
        behavior_function_prepare_host(f, ctx);
        ctx->owned_host = true;
    }
    ctx->error_message = behavior_function_value_arguments(ctx);
    int result;
    if (ctx->error_message) {
        result = FLOW_ERR_ARGUMENT;
    } else if (scoped) {
        behavior_run_record(EXECUTION_RUN_SLJIT);
        result = ((int (*)(behavior_function_context *))f->artifact->scoped_code)(ctx);
    } else {
        result = compiled ? run_compiled(f, ctx)
                          : behavior_function_run_interpreted(f, ctx);
    }
    if (result < 0 && ctx->failure_observer) {
        ctx->failure_observer(f, ctx, result);
    }
    result = behavior_function_finish_values(ctx, result, acount(f->result_defaults));
    if (ctx->owned_host && result != FLOW_SUSPENDED_RESULT &&
        result != FLOW_PAUSED_RESULT && result != BEHAVIOR_FUNCTION_CALL_RESULT) {
        behavior_function_release_host(ctx);
    }
    return result;
}

int behavior_function_run(const behavior_function *f, behavior_function_context *ctx, bool compiled)
{
    /* Every invocation counts, so a run a native reenters from one without
     * authored calls is nested and leaves collection to the outer run. */
    behavior_managed_heap *heap = ctx->managed_heap;
    if (heap) {
        heap->running_calls++;
    }
    int result = f->calls || (ctx->activation && ctx->activation->calls)
                   ? behavior_function_run_calls(f, ctx, compiled)
                   : behavior_function_run_once(f, ctx, compiled);
    if (heap) {
        heap->running_calls--;
    }
    return result;
}
const char *behavior_function_failure_text(behavior_function_failure failure)
{
    switch (failure) {
        case BEHAVIOR_FUNCTION_BUDGET: return "instruction budget exhausted";
        case BEHAVIOR_FUNCTION_DEPTH: return "call depth exceeded";
        case BEHAVIOR_FUNCTION_ARGUMENT: return "array index out of range";
        default: return "execution failed";
    }
}
