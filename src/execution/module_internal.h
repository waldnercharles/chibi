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
    execution_effect_summary effects;
    uint64_t *effect_ids;
    size_t effect_bytes;
};
/* Decoder-owned payloads already belong to their canonical arenas. */
execution_module *execution_module_finish_decoded(
    execution_module_plan **,
    behavior_backend,
    char *,
    size_t
);
#endif
