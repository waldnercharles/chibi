#pragma once

#define EVENT_CATALOG_VERSION 3
#include "execution/prepared.h"

typedef struct event_catalog event_catalog;
typedef struct
{
    execution_module_event_info event;
    const execution_module_event_field_info *fields;
} event_declaration;
/* Immutable original declarations, including events with no executable handler.
 * Values/names are owned snapshots. Host type descriptors and the selected
 * immutable type catalog outlive these generations. */
event_catalog *event_catalog_new(
    const event_declaration *,
    int count,
    const execution_prepared_linker *types,
    char *error,
    size_t size
);
void event_catalog_retain(event_catalog *);
void event_catalog_free(event_catalog *);
int event_catalog_count(const event_catalog *);
size_t event_catalog_storage_bytes(const event_catalog *);
execution_module_event_info event_catalog_at(const event_catalog *, int);
execution_module_event_field_info event_catalog_field_at(const event_catalog *, int event, int field);
int event_catalog_find(const event_catalog *, const char *name);
/* Reject any ID/name/ABI/field/default disagreement across the full retained
 * import closure. Referenced events absent from this standalone catalog remain
 * module-owned; their normal cross-module validation still applies. */
bool event_catalog_validate(
    const event_catalog *,
    const execution_module *const *,
    int count,
    char *error,
    size_t size
);
bool event_catalog_write(const event_catalog *, unsigned char **data, size_t *length, char *error, size_t size);
/* Charges authoritative retained arena capacity and owner bytes. Failed reads
 * preserve the caller budget; decode allocations are bounded by its remainder. */
event_catalog *event_catalog_read_budget(
    const void *,
    size_t length,
    const execution_prepared_linker *types,
    size_t *remaining,
    char *error,
    size_t size
);
event_catalog *event_catalog_read(
    const void *,
    size_t length,
    const execution_prepared_linker *types,
    char *error,
    size_t size
);
