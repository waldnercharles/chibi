#ifndef CHIBI_PACKAGE_H
#define CHIBI_PACKAGE_H

#include "execution/prepared.h"
#include "runtime/behaviors/module_set.h"
#include "runtime/event_catalog.h"

#define PACKAGE_MAX_MODULES MODULE_SET_MAX_MODULES
#define PACKAGE_MAX_BYTES MODULE_SET_MAX_BYTES
#define PACKAGE_MAX_IMPORT_DEPTH MODULE_SET_MAX_IMPORT_DEPTH
#define PACKAGE_MAX_EVENT_DECLARATIONS MODULE_SET_MAX_EVENT_DECLARATIONS
#define PACKAGE_MAX_EVENT_FIELDS MODULE_SET_MAX_EVENT_FIELDS

typedef struct
{
    const char *name;
    uint64_t id;
    const void *data;
    size_t length;
} package_module;

typedef struct package_collection package_collection;
/* Decodes immutable modules only, without an ECS world, programs, services or
 * dispatch. Input records and linker are borrowed during this call; accepted
 * collections own copied names and retained roots/imports. Optional original
 * event declarations validate the complete executable closure. Failure leaves
 * remaining unchanged. The budget includes collection metadata and modules. */
package_collection *package_read_budget(
    const package_module *,
    int count,
    const execution_prepared_linker *,
    const event_catalog *,
    behavior_backend,
    size_t *remaining,
    char *error,
    size_t size
);
void package_collection_free(package_collection *);
int package_collection_count(const package_collection *);
const char *package_collection_name(const package_collection *, int);
execution_module *package_collection_module(const package_collection *, int);

#endif
