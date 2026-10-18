#include "execution/authority.h"

#include "execution/program_internal.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdlib.h>

struct execution_dispatch
{
    uint64_t id;
    unsigned references, pending;
    bool cancelled;
    bool work_enabled, work_granted;
    execution_work work;
};

static atomic_uint_fast64_t identities;
static _Thread_local execution_activation *active;

static uint64_t identity()
{
    uint_fast64_t before = atomic_load_explicit(&identities, memory_order_relaxed);
    do {
        /* Saturate rather than letting an old proof become valid after wrap. */
        if (before == UINT64_MAX) {
            abort();
        }
    } while (
        !atomic_compare_exchange_weak_explicit(&identities, &before, before + 1, memory_order_relaxed, memory_order_relaxed));
    return before + 1;
}

execution_dispatch *execution_dispatch_new()
{
    execution_dispatch *dispatch = xcalloc(1, sizeof *dispatch);
    dispatch->id = identity();
    dispatch->references = 1;
    return dispatch;
}
execution_dispatch *execution_dispatch_new_work(uintptr_t initial_limit)
{
    assert(initial_limit);
    execution_dispatch *dispatch = execution_dispatch_new();
    dispatch->work_enabled = true;
    dispatch->work.limit = initial_limit;
    return dispatch;
}
execution_work *program_dispatch_work(execution_dispatch *dispatch)
{
    return dispatch && dispatch->work_enabled ? &dispatch->work : NULL;
}
bool execution_dispatch_work_usage(const execution_dispatch *dispatch, uintptr_t *spent, uintptr_t *limit)
{
    if (!dispatch || !dispatch->work_enabled) {
        return false;
    }
    *spent = dispatch->work.spent;
    *limit = dispatch->work.limit;
    return true;
}
execution_dispatch *execution_dispatch_retain(execution_dispatch *dispatch)
{
    if (dispatch) {
        assert(dispatch->references);
        if (dispatch->references == UINT_MAX) {
            abort();
        }
        dispatch->references++;
    }
    return dispatch;
}
void execution_dispatch_free(execution_dispatch *dispatch)
{
    if (dispatch) {
        assert(dispatch->references);
        if (!--dispatch->references) {
            free(dispatch);
        }
    }
}
execution_dispatch *program_dispatch_retain(execution_dispatch *dispatch)
{
    execution_dispatch_retain(dispatch);
    if (dispatch) {
        dispatch->pending++;
    }
    return dispatch;
}
void program_dispatch_free(execution_dispatch *dispatch)
{
    if (dispatch) {
        assert(dispatch->pending);
        dispatch->pending--;
    }
    execution_dispatch_free(dispatch);
}
bool execution_dispatch_pending(const execution_dispatch *dispatch)
{
    return dispatch && dispatch->pending;
}
bool execution_dispatch_cancelled(const execution_dispatch *dispatch)
{
    return dispatch && dispatch->cancelled;
}
uint64_t execution_dispatch_id(const execution_dispatch *dispatch)
{
    return dispatch ? dispatch->id : 0;
}
void execution_dispatch_cancel(execution_dispatch *dispatch)
{
    assert(dispatch);
    dispatch->cancelled = true;
    for (execution_activation *a = active; a; a = a->authority_previous) {
        if (a->dispatch == dispatch) {
            a->authority_valid = false;
            a->stopping = true;
        }
    }
}
void program_authority_invalidate()
{
    for (execution_activation *a = active; a; a = a->authority_previous) {
        a->authority_valid = false;
    }
}
void execution_authority_invalidate()
{
    program_authority_invalidate();
}
void program_authority_enter(execution_activation *a)
{
    assert(!a->active);
    program_authority_invalidate();
    a->active = true;
    a->authority_previous = active;
    active = a;
    if (a->dispatch) {
        if (!a->authority_activation) {
            a->authority_activation = identity();
        }
        a->authority_slice = identity();
    }
}
void program_authority_begin(execution_activation *a)
{
    assert(active == a);
    a->authority_valid = a->authority_handler && !a->has_run && !a->stopping &&
                         !execution_dispatch_cancelled(a->dispatch);
}
void program_authority_end(execution_activation *a)
{
    a->authority_valid = false;
}
void program_authority_leave(execution_activation *a)
{
    assert(active == a);
    a->authority_valid = false;
    a->authority_handler = false;
    a->active = false;
    active = a->authority_previous;
    a->authority_previous = NULL;
}
void program_authority_inherit(execution_activation *child, const execution_activation *parent)
{
    assert(!child->dispatch && !child->has_run);
    child->dispatch = program_dispatch_retain(parent->dispatch);
    child->dispatch_event = parent->dispatch_event;
    /* This is cancellation ancestry only, never a new event-handler grant. */
}
bool execution_context_authority(const behavior_function_context *context, execution_authority *proof)
{
    execution_activation *a = active;
    if (!a || !a->authority_valid || !a->running || a->stopping ||
        !a->dispatch || execution_dispatch_cancelled(a->dispatch)) {
        return false;
    }
    /* Walk only runtime-owned live contexts. In particular, never dereference
     * an arbitrary inspection_user or parent supplied by a fabricated context. */
    const behavior_function *function = a->function;
    behavior_function_context *current = &a->context;
    for (;;) {
        behavior_function_context *next = behavior_function_active_context(current, &function);
        if (next == current) {
            break;
        }
        current = next;
    }
    bool found = false;
    for (; current; current = current->parent) {
        found |= current == context;
        if (current->error_status || current->failure) {
            return false;
        }
        if (current == &a->context) {
            break;
        }
    }
    if (!found || current != &a->context) {
        return false;
    }
    *proof = (execution_authority){ .program = a->instance->program,
                                    .module = a->instance->program->module,
                                    .function = a->function,
                                    .owner = a->instance->owner,
                                    .event = a->dispatch_event,
                                    .request = execution_dispatch_id(a->dispatch),
                                    .activation = a->authority_activation,
                                    .slice = a->authority_slice };
    return true;
}
bool execution_authority_current(const behavior_function_context *context, const execution_authority *proof)
{
    execution_authority now;
    return execution_context_authority(context, &now) &&
           now.program == proof->program && now.module == proof->module &&
           now.function == proof->function && now.owner == proof->owner &&
           now.event == proof->event && now.request == proof->request &&
           now.activation == proof->activation && now.slice == proof->slice;
}
bool execution_context_grant_work(
    const behavior_function_context *context,
    execution_dispatch *dispatch,
    uintptr_t total_limit
)
{
    execution_authority proof;
    if (!active || active->dispatch != dispatch ||
        !execution_context_authority(context, &proof) || !dispatch->work_enabled ||
        dispatch->work_granted || total_limit < dispatch->work.limit) {
        return false;
    }
    dispatch->work.limit = total_limit;
    dispatch->work_granted = true;
    return true;
}
bool execution_context_state(const behavior_function_context *context, uint64_t id, pin_value *value)
{
    execution_authority proof;
    if (!value || !execution_context_authority(context, &proof)) {
        return false;
    }
    const execution_instance *instance = active->instance;
    const execution_module_plan *plan = &instance->program->module->plan;
    for (int i = 0; i < plan->state_count; i++) {
        if (plan->states[i].id == id) {
            *value = instance->values[i];
            return true;
        }
    }
    return false;
}
