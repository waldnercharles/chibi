#ifndef CHIBI_AUTHORING_SOURCE_REFERENCES_H
#define CHIBI_AUTHORING_SOURCE_REFERENCES_H
#include "authoring/source_navigation.h"

typedef struct reference_index reference_index;

/* Appends resolved executable source facts using exact document paths and
 * stable semantic identities. The index owns copied names/ranges; navigation
 * may be freed afterward. Native KDL/UI facts remain with their owning adapters. */
void authoring_source_references_add(reference_index *, const authoring_navigation *);
#endif
