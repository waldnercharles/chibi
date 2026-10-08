#pragma once
#include "execution/module_internal.h"
/* Derived from finalized canonical instructions and installed host access. */
size_t execution_module_effects_bytes(const execution_module_plan *);
bool execution_module_build_effects(execution_module *);
