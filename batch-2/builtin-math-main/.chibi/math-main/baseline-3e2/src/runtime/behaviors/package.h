#ifndef CHIBI_PACKAGE_H
#define CHIBI_PACKAGE_H

#include "execution/prepared.h"
#include "runtime/behaviors/module_set.h"
#include "runtime/behaviors/source.h"

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

/* Records, linker callbacks and services are borrowed during loading. Native
 * capability descriptors and services must outlive the returned registry.
 * Service collections are NULL or support/array.h vectors (apush/acount), never
 * fixed/sentinel arrays. Rows are copied; vector storage may retire after the
 * call, while binding name strings and host values remain borrowed.
 * Imports within the package resolve independently of record order. All modules
 * and their transitive imports are validated before registration, including
 * consistent generations and event declarations; failure returns no partially
 * loaded world.
 * Loading does not attach entities or dispatch Begin. */
/* Decode and preflight into an existing quiescent registry, preserving all
 * current registrations/work on failure. Named records are new registrations;
 * collisions reject. Commit/discard use the ordinary source stage ownership. */
/* Shares a remaining canonical decoded-storage budget across every record and
 * retained external import generation. Failure preserves the caller budget. */
source_staging *package_stage_budget(
    source_registry *,
    const package_module *,
    int,
    const execution_prepared_linker *,
    const service_binding *,
    behavior_backend,
    size_t *remaining,
    char *error,
    size_t size
);
source_staging *package_stage(
    source_registry *,
    const package_module *,
    int,
    const execution_prepared_linker *,
    const service_binding *,
    behavior_backend,
    char *error,
    size_t size
);
source_registry *package_load(
    ecs_world *,
    const package_module *,
    int count,
    const execution_prepared_linker *,
    const service_binding *,
    behavior_backend,
    char *error,
    size_t size
);

#endif
