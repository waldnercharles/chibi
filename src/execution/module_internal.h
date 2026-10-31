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
    /* FNV-1a of this generation's prepared bytes, which importers record and
     * check; 0 until first needed. A decoded module keeps the hash of the
     * bytes it was read from, so no importer re-encodes it. */
    uint64_t fingerprint;
};
/* Gives every definition report sites when development reports are enabled. */
void execution_module_plan_report_integers(execution_module_plan *);
/* Decoder-owned payloads already belong to their canonical arenas. The
 * module's effect table, counted after publication, must fit effect_budget
 * bytes; it is allocated only then. */
execution_module *execution_module_finish_decoded(
    execution_module_plan **,
    behavior_backend,
    size_t effect_budget,
    char *,
    size_t
);
#endif
