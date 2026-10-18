#ifndef CHIBI_EXECUTION_AUTHORITY_H
#define CHIBI_EXECUTION_AUTHORITY_H

#include "execution/module.h"

typedef struct execution_dispatch execution_dispatch;

/* Serialized with the programs using this request. New returns one reference;
 * queues, activations and host requests retain their own references. Cancellation
 * revokes immediately; storage is released at safe execution boundaries. */
execution_dispatch *execution_dispatch_new();
/* Opt in to one finite checkpoint balance shared by this request's handlers
 * and synchronous helpers, including queued delivery. The nonzero initial
 * limit is never renewed by pumps, helper calls, re-enqueue or resumption. */
execution_dispatch *execution_dispatch_new_work(uintptr_t initial_limit);
execution_dispatch *execution_dispatch_retain(execution_dispatch *);
void execution_dispatch_free(execution_dispatch *);
void execution_dispatch_cancel(execution_dispatch *);
bool execution_dispatch_cancelled(const execution_dispatch *);
uint64_t execution_dispatch_id(const execution_dispatch *);
/* Conservative: queued events or retained activations still own this origin.
 * Host references, including detached host requests, do not count. When false,
 * no retained execution can start another slice unless the host enqueues anew. */
bool execution_dispatch_pending(const execution_dispatch *);
/* Host-only accounting snapshot; false for an ordinary dispatch. */
bool execution_dispatch_work_usage(const execution_dispatch *, uintptr_t *spent, uintptr_t *limit);

int execution_program_emit_tagged(
    execution_program *,
    execution_dispatch *,
    uint64_t event,
    uint64_t owner,
    const pin_value *,
    int count
);
int execution_program_dispatch_tagged(
    execution_program *,
    execution_dispatch *,
    uint64_t event,
    uint64_t source,
    const pin_value *,
    int count,
    double now,
    double delta,
    unsigned budget
);

/* Host-only proof of the first uninterrupted explicitly tagged handler slice.
 * Synchronous helpers share it; tasks, continuations and resumed slices do not.
 * Pointer members are borrowed identity metadata, never retained or dereferenced
 * by a later proof check. A proof does not establish host/world/model currentness. */
typedef struct
{
    const execution_program *program;
    const execution_module *module;
    const behavior_function *function;
    uint64_t owner, event, request, activation, slice;
} execution_authority;

bool execution_context_authority(const behavior_function_context *, execution_authority *);
bool execution_authority_current(const behavior_function_context *, const execution_authority *);
/* One absolute grant on this exact original uninterrupted request. Existing
 * work remains charged. Rejects lowering the limit, duplicate/foreign grants,
 * fabricated contexts, cancellation and later resumed slices. This grants
 * checkpoints, never native-operation or wall-clock time. */
bool execution_context_grant_work(const behavior_function_context *, execution_dispatch *, uintptr_t total_limit);
/* Read the original handler instance's current state, including assignments not
 * yet committed at a call/run boundary. Requires live authority for this context.
 * Copies a borrowed view; pointed-to contents must not be changed or retained. */
bool execution_context_state(const behavior_function_context *, uint64_t id, pin_value *);
/* Revoke active proofs at an external scheduler/dispatch boundary, including
 * boundaries that find no matching handler. This can never grant authority. */
void execution_authority_invalidate();

#endif
