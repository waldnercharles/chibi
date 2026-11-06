#ifndef CHIBI_CATALOG_PUBLICATION_H
#define CHIBI_CATALOG_PUBLICATION_H
/* Prepared publication tokens borrow their address until replacement/retirement.
 * The caller exchanges all resident records first, then publishes the matching
 * token. Clear is forbidden while any prepared generation is installed. */
void shader_catalog_publication_exchange(const void *expected, const void *next);
void shader_catalog_resources_changed();
#endif
