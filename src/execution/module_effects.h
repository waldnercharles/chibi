#pragma once
#include "execution/module_internal.h"
/* Derived from finalized canonical instructions and installed host access. */
size_t execution_module_effects_bytes(const execution_module_plan *);
/* Fills the module's effects; `bytes` is execution_module_effects_bytes of its
 * plan, which the caller has already checked, so the plan is walked once more
 * rather than twice. */
bool execution_module_build_effects(execution_module *, size_t bytes);
/* Query gates may read, but cannot observe instance state or cause effects.
 * Bound includes bit zero for the root and one bit per earlier generator. */
bool execution_query_predicate_valid(const behavior_function *, unsigned bound);
/* Settles whether calls may pass this definition's arguments without copying
 * them: it and every reachable callee leave storage they did not allocate
 * untouched. Runs once per definition, after its callees are frozen. */
void execution_prepare_argument_sharing(behavior_function *);
/* Settles which pointer arguments a task started on each definition reachable
 * from this one depends on. Runs after argument sharing, on frozen callees. */
void execution_prepare_task_dependencies(behavior_function *);
static inline uint64_t execution_task_argument_bit(int argument)
{
    return UINT64_C(1) << (argument < 63 ? argument : 63);
}
/* Whether a definition changes the values it builds in its caller's storage
 * only through updates that run there, so its results may move there. */
bool execution_builds_safely(const behavior_function *);

bool execution_query_evaluator_valid(const behavior_function *, unsigned bound);
