#ifndef CHIBI_SOURCE_ASSET_H
#define CHIBI_SOURCE_ASSET_H

#include "assets/prototype.h"
#include "authoring/source_catalog.h"
#include "authoring/source_intelligence.h"

/* Static authored defaults in the ordinary prototype value schema. Source
 * ranges remain attached to values for shared navigation/refactoring. */
av_doc *authoring_prototype_source_read(const char *, const prototype_registry *, char *, size_t);
av_doc *authoring_prototype_document_read(const char *, const prototype_registry *, char *, size_t);
bool authoring_relationship_declarations_load(const char *, ecs_relationship_desc ***, char *, size_t);
/* The returned descriptor owns its name/key; release it with the paired free. */
ecs_relationship_desc *authoring_relationship_source_read(const char *, char *, size_t);
void authoring_relationship_source_free(ecs_relationship_desc *);
void authoring_asset_source_complete(const script_syntax *, const source_catalog *, int, authoring_completions *);

#endif
