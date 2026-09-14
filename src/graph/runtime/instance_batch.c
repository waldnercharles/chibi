#include "graph/runtime/flow_internal.h"
#include "graph/runtime/instance.h"
#include "graph/runtime/profile.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>

struct behavior_batch
{
    const behavior_function *function;
    uint64_t revision;
    behavior_function_activation scratch;
    behavior_data_storage *data;
    arena storage;
};

void behavior_batch_free(behavior_batch *batch)
{
    if (!batch) {
        return;
    }
    behavior_function_activation_free(&batch->scratch);
    behavior_function_free_data(batch->data);
    arena_free(&batch->storage);
    free(batch);
}

static _Thread_local uint64_t batch_dispatches;

uint64_t behavior_batch_dispatches()
{
    return batch_dispatches;
}

bool behavior_instance_dispatch_batch(
    behavior_instance *bi,
    const char *name,
    behavior_batch **batch,
    flow_run **failed
)
{
    *failed = NULL;
    if (!name || !name[0] || !bi->alive ||
        (bi->backend != BEHAVIOR_SLJIT && bi->backend != BEHAVIOR_BYTECODE) ||
        bi->trace_enabled || acount(bi->activations)) {
        return false;
    }
    const behavior_program *program = bi->asset->program;
    if (!program || behavior_program_diagnostic(program)) {
        return false;
    }
    node *entry;
    const behavior_function *function = behavior_program_batch_event(program, name, &entry);
    if (!function) {
        return false;
    }

    bool profile = bi->profile != NULL;
    behavior_profile_scope scope;
    if (profile) {
        behavior_profile_enter(&scope);
    }
    behavior_function_context ctx = { .budget = FLOW_DEFAULT_STEP_BUDGET,
                                      .delta_seconds = bi->delta_seconds };
    bool bound = behavior_function_bind(function, &ctx, bi->services, bi->owner.u64);
    if (bound) {
        behavior_instance_bind_values(bi, &ctx);
        uint64_t revision = behavior_program_revision(program);
        if (*batch &&
            ((*batch)->function != function || (*batch)->revision != revision)) {
            behavior_batch_free(*batch);
            *batch = NULL;
        }
        if (!*batch) {
            *batch = xcalloc(1, sizeof **batch);
            (*batch)->function = function;
            (*batch)->revision = revision;
            if (behavior_function_resumable(function)) {
                behavior_function_activation_init(function, &(*batch)->scratch);
            }
        }
        /* Synchronous bytecode needs reusable scratch but no saved continuation.
         * Each entry resets locals before processing the next instance. */
        ctx.activation = &(*batch)->scratch;
        behavior_function_prepare_data(function, bi->services, &(*batch)->data);
        ctx.data = (*batch)->data;
        if (behavior_function_uses_storage(function)) {
            arena_reset(&(*batch)->storage);
            behavior_function_bind_storage(&ctx, &(*batch)->storage);
        }
    }
    if (profile) {
        uint64_t elapsed = behavior_profile_leave(&scope);
        if (bound) {
            bi->profile(name, elapsed, BEHAVIOR_PROFILE_SETUP, bi->profile_user);
        }
    }
    if (!bound) {
        return false;
    }

    if (profile) {
        behavior_profile_enter(&scope);
    }
    int result = behavior_function_run(function, &ctx, bi->backend == BEHAVIOR_SLJIT);
    assert(result <= FLOW_OK);
    batch_dispatches++;
    if (result) {
        /* Preserve the exact failure after any earlier writes. Construct its
         * inspection record without executing the event a second time. */
        *failed = behavior_instance_enqueue(bi, entry, (flow_inputs){ 0 }, NULL);
        assert(*failed);
        if (ctx.value_storage) {
            arena_free(&(*failed)->scratch);
            (*failed)->scratch = (*batch)->storage;
            (*batch)->storage = (arena){ 0 };
            behavior_function_bind_storage(&ctx, &(*failed)->scratch);
        }
        flow_run_finish_compiled(*failed, function, &ctx, result);
    }
    if (profile) {
        uint64_t elapsed = behavior_profile_leave(&scope);
        bi->profile(name, elapsed, BEHAVIOR_PROFILE_START, bi->profile_user);
    }
    return true;
}
