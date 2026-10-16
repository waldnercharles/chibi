#ifndef CHIBI_EXECUTION_PREPARED_INTERNAL_H
#define CHIBI_EXECUTION_PREPARED_INTERNAL_H
#include "execution/definition.h"
#include "execution/module_internal.h"
#include "execution/prepared.h"
#include "types/value_wire.h"

typedef struct
{
    execution_wire wire;
    const execution_prepared_linker *linker;
    behavior_function **functions;
    execution_module_plan *plan;
    struct prepared_dependency **dependencies;
    size_t *dependency_bytes;
    execution_wire *fingerprint_budget;
    size_t function_bytes;
    struct prepared_record_cache *records;
    size_t record_work, record_work_limit;
    size_t *shared_record_work;
    size_t record_cache_bytes;
} prepared_context;
/* Target layout plus stable instruction names at their numeric enum indices.
 * This detects schema differences, not unchanged-name payload/semantic changes. */
uint64_t prepared_abi(const char *const *names, int count);
void prepared_value(prepared_context *, pin_value *);
void prepared_types(prepared_context *);
bool prepared_records_finalize(prepared_context *);
void prepared_table(execution_wire *, int *count, size_t item_size);
void prepared_vector(execution_wire *, void **, int count, size_t item_size);
void prepared_function(prepared_context *, behavior_function *);
void prepared_function_ref(prepared_context *, const behavior_function **);
void prepared_source(execution_wire *, behavior_function_source *);
#endif
