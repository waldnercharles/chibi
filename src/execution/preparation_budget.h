#pragma once
#include "execution/definition.h"
#include "execution/module_builder.h"
/* Additional peak allocation bound for decoded verification/finalization:
 * the bytes the prepared definition keeps, and in *scratch the bytes its
 * preparation frees before returning. Definitions prepare one at a time, so
 * a module needs its definitions' kept bytes plus their largest scratch.
 * Decoded payloads stay owned; no source lowering or payload snapshot is run.
 * Derived runtime optimization storage is included in the kept bytes.
 * Installed descriptor copies use a separate bounded arena allocation path.
 * SIZE_MAX, returned or in *scratch, reports an unsupported count or
 * overflow. */
size_t behavior_function_preparation_bytes(const behavior_function *, size_t *scratch);
bool behavior_function_own_decoded(behavior_function *, size_t arena_budget);

/* Allocation-free closure walk bounds publication scratch before allocation.
 * Owned/import depth <=64 and counted modules/definitions <=65536; DAG paths may
 * overcount. Returns SIZE_MAX when those bounds or arithmetic fail. */
size_t execution_module_preparation_bytes(const execution_module_plan *);
