#ifndef CHIBI_EXECUTION_ROW_BUILDER_H
#define CHIBI_EXECUTION_ROW_BUILDER_H

#include "execution/function.h"

/* Derived, process-local proof. No pointers into pre-ownership headers and no
 * serialized promise: decoding repeats proof after resolving current hosts. */
typedef struct
{
    int add, retain, argument, local;
    uint64_t id, abi, nominal, layout;
} behavior_row_plan;

typedef struct
{
    int add, retain, local, add_pc, retain_pc;
} behavior_append_plan;

void behavior_function_row_builder(behavior_function *);
size_t behavior_function_row_preparation_bytes(const behavior_function *);
bool behavior_function_rows_allowed(const behavior_function *, const behavior_function_context *);
const char *behavior_function_build_row(behavior_function_context *, int host);
const char *behavior_function_retain_rows(behavior_function_context *, int host);
void behavior_function_scalar_builder(behavior_function *);
size_t behavior_function_append_storage_bytes(const behavior_function *);
void behavior_function_append_reset(behavior_data_storage *);
const char *behavior_function_append_scalar(behavior_function_context *, int host);
const char *behavior_function_retain_scalar(behavior_function_context *, int host);

#endif
