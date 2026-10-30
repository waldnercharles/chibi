#ifndef CHIBI_MODULE_SET_H
#define CHIBI_MODULE_SET_H

#include "execution/module.h"

#define MODULE_SET_MAX_MODULES 1024
#define MODULE_SET_MAX_BYTES (64 * 1024 * 1024)
#define MODULE_SET_MAX_IMPORT_DEPTH 64
#define MODULE_SET_MAX_EVENT_DECLARATIONS 16384
#define MODULE_SET_MAX_EVENT_FIELDS 65536

/* Borrowed finalized modules. Validate their full retained import closure,
 * stable generations and event schemas without publishing or executing.
 * root_bytes is zero for native generations, or their already bounded encoded
 * byte count when the caller has decoded package records. */
bool module_set_validate(
    const execution_module *const *,
    int count,
    size_t root_bytes,
    char *error,
    size_t size
);

#endif
