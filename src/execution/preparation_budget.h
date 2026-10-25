#pragma once
#include "execution/definition.h"
#include "execution/module_builder.h"
/* Additional peak allocation bound for decoded verification/finalization.
 * Decoded payloads stay owned; no source lowering or payload snapshot is run.
 * `retained` is derived runtime optimization storage the definition keeps;
 * `scratch` is verification storage freed before the next definition is
 * prepared. Installed descriptor copies use a separate bounded arena
 * allocation path. SIZE_MAX in both reports an unsupported count or overflow. */
typedef struct
{
    size_t retained, scratch;
} behavior_function_preparation;
behavior_function_preparation behavior_function_preparation_bytes(const behavior_function *);
bool behavior_function_own_decoded(behavior_function *, size_t arena_budget);

/* Allocation-free closure walk bounds publication scratch before allocation.
 * Owned/import depth <=64 and counted modules/definitions <=65536; DAG paths may
 * overcount. Returns SIZE_MAX when those bounds or arithmetic fail. */
size_t execution_module_preparation_bytes(const execution_module_plan *);
