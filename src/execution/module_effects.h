#pragma once
#include "execution/module_internal.h"
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
