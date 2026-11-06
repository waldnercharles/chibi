#ifndef CHIBI_EXECUTION_MODULE_INTERNAL_H
#define CHIBI_EXECUTION_MODULE_INTERNAL_H
#include "execution/module_builder.h"

/* Runtime and serialization ownership only. Public consumers cannot write the
 * finalized payload, backend selection, or lifetime counters. */
struct execution_module
{
    execution_module_plan plan;
    behavior_backend backend;
    unsigned references;
    bool retained_tasks; /* Immutable transitive summary of finalized imports. */
    execution_effect_summary effects;
    uint64_t *effect_ids;
    size_t effect_bytes;
};
/* Gives every definition report sites when development reports are enabled. */
void execution_module_plan_report_integers(execution_module_plan *);
/* Decoder-owned payloads already belong to their canonical arenas. */
execution_module *execution_module_finish_decoded(
    execution_module_plan **,
    behavior_backend,
    char *,
    size_t
);
#endif
