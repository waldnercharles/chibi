#ifndef CHIBI_GAME_PACKAGE_H
#define CHIBI_GAME_PACKAGE_H

#include "execution/prepared.h"
#include "runtime/behaviors/module_set.h"
#include "runtime/behaviors/source.h"

#define GAME_PACKAGE_MAX_MODULES GAME_MODULE_SET_MAX_MODULES
#define GAME_PACKAGE_MAX_BYTES GAME_MODULE_SET_MAX_BYTES
#define GAME_PACKAGE_MAX_IMPORT_DEPTH GAME_MODULE_SET_MAX_IMPORT_DEPTH
#define GAME_PACKAGE_MAX_EVENT_DECLARATIONS                                    \
    GAME_MODULE_SET_MAX_EVENT_DECLARATIONS
#define GAME_PACKAGE_MAX_EVENT_FIELDS GAME_MODULE_SET_MAX_EVENT_FIELDS

typedef struct
{
    const char *name;
    uint64_t id;
    const void *data;
    size_t length;
} game_package_module;

typedef struct game_package_collection game_package_collection;
/* Decodes immutable modules only, without an ECS world, programs, services or
 * dispatch. Input records and linker are borrowed during this call; accepted
 * collections own copied names and retained roots/imports. Optional original
 * event declarations validate the complete executable closure. Failure leaves
 * remaining unchanged. The budget includes collection metadata and modules. */
game_package_collection *game_package_read_budget(
    const game_package_module *,
    int count,
    const execution_prepared_linker *,
    const game_event_catalog *,
    behavior_backend,
    size_t *remaining,
    char *error,
    size_t size
);
void game_package_collection_free(game_package_collection *);
int game_package_collection_count(const game_package_collection *);
const char *game_package_collection_name(const game_package_collection *, int);
execution_module *game_package_collection_module(const game_package_collection *, int);

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
game_source_staging *game_package_stage_budget(
    game_source_registry *,
    const game_package_module *,
    int,
    const execution_prepared_linker *,
    const service_binding *,
    behavior_backend,
    size_t *remaining,
    char *error,
    size_t size
);
game_source_staging *game_package_stage(
    game_source_registry *,
    const game_package_module *,
    int,
    const execution_prepared_linker *,
    const service_binding *,
    behavior_backend,
    char *error,
    size_t size
);
game_source_registry *game_package_load(
    ecs_world *,
    const game_package_module *,
    int count,
    const execution_prepared_linker *,
    const service_binding *,
    behavior_backend,
    char *error,
    size_t size
);

#endif
