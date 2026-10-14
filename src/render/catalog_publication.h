#ifndef CHIBI_CATALOG_PUBLICATION_H
#define CHIBI_CATALOG_PUBLICATION_H
#include <stddef.h>
/* Prepared publication tokens borrow their address until replacement/retirement.
 * The caller exchanges all resident records first, then publishes the matching
 * token. Clear is forbidden while any prepared generation is installed. */
void shader_catalog_publication_exchange(const void *expected, const void *next);
void shader_catalog_resources_changed();

/* Opaque context-owned runtime cache, overrides and revision counters. Asset
 * records and typed values travel in their own detached tables. */
typedef struct shader_catalog_state shader_catalog_state;
size_t shader_catalog_state_bytes();
unsigned shader_catalog_next_revision();
shader_catalog_state *shader_catalog_state_new();
void shader_catalog_state_exchange(shader_catalog_state *);
void shader_catalog_state_free(shader_catalog_state *);
#endif
