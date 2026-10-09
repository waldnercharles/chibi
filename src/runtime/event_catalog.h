#pragma once

#define GAME_EVENT_CATALOG_VERSION 3
#include "execution/prepared.h"

typedef struct game_event_catalog game_event_catalog;
typedef struct
{
    execution_module_event_info event;
    const execution_module_event_field_info *fields;
} game_event_declaration;
/* Immutable original declarations, including events with no executable handler.
 * Values/names are owned snapshots. Host type descriptors and the selected
 * immutable type catalog outlive these generations. */
game_event_catalog *game_event_catalog_new(
    const game_event_declaration *,
    int count,
    const execution_prepared_linker *types,
    char *error,
    size_t size
);
void game_event_catalog_retain(game_event_catalog *);
void game_event_catalog_free(game_event_catalog *);
int game_event_catalog_count(const game_event_catalog *);
size_t game_event_catalog_storage_bytes(const game_event_catalog *);
execution_module_event_info game_event_catalog_at(const game_event_catalog *, int);
execution_module_event_field_info game_event_catalog_field_at(
    const game_event_catalog *,
    int event,
    int field
);
int game_event_catalog_find(const game_event_catalog *, const char *name);
/* Reject any ID/name/ABI/field/default disagreement across the full retained
 * import closure. Referenced events absent from this standalone catalog remain
 * module-owned; their normal cross-module validation still applies. */
bool game_event_catalog_validate(
    const game_event_catalog *,
    const execution_module *const *,
    int count,
    char *error,
    size_t size
);
bool game_event_catalog_write(
    const game_event_catalog *,
    unsigned char **data,
    size_t *length,
    char *error,
    size_t size
);
/* Charges authoritative retained arena capacity and owner bytes. Failed reads
 * preserve the caller budget; decode allocations are bounded by its remainder. */
game_event_catalog *game_event_catalog_read_budget(
    const void *,
    size_t length,
    const execution_prepared_linker *types,
    size_t *remaining,
    char *error,
    size_t size
);
game_event_catalog *game_event_catalog_read(
    const void *,
    size_t length,
    const execution_prepared_linker *types,
    char *error,
    size_t size
);
