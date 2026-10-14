#pragma once
#include "execution/module_internal.h"
/* Derived from finalized canonical instructions and installed host access. */
size_t execution_module_effects_bytes(const execution_module_plan *);
bool execution_module_build_effects(execution_module *);
/* Query gates may read, but cannot observe instance state or cause effects.
 * Bound includes bit zero for the root and one bit per earlier generator. */
bool execution_query_predicate_valid(const behavior_function *, unsigned bound);
