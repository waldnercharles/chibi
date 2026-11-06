#ifndef CHIBI_LANGUAGE_SCALAR_LOWERING_REFERENCE_H
#define CHIBI_LANGUAGE_SCALAR_LOWERING_REFERENCE_H
#include "language/lower.h"

/* Native differential oracle only. Calls the production value_evaluator emitter
 * before optimization/finalization. Never link this into a runtime-only host.
 * Inputs are admitted Bool/I32 typed facts; no malformed-input API is promised.
 * Returned builder borrows input source paths until finish snapshots them. */
behavior_function_builder *script_lower_evaluator_reference(
    const script_analysis *,
    const script_catalog *,
    const execution_module_state *,
    int,
    const int *,
    const script_type *,
    int,
    script_range,
    char *,
    size_t
);
#endif
