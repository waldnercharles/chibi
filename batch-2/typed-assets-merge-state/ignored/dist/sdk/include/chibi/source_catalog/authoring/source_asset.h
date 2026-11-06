#ifndef CHIBI_SOURCE_ASSET_H
#define CHIBI_SOURCE_ASSET_H

#include "assets/prototype.h"
#include "authoring/source_catalog.h"
#include "authoring/source_intelligence.h"

typedef struct
{
    uint64_t instruction_budget; /* Zero selects the packaged evaluation budget. */
    bool (*cancelled)(void *user);
    void *user;
} authoring_prototype_source_options;
typedef struct
{
    bool computed;
    bool overridden;
    av_source expression;
} authoring_prototype_field_source;

/* Evaluated defaults in the ordinary prototype value schema. Source ranges
 * remain attached to original expressions. Parameters and literal overrides
 * are retained as authoring metadata; prototype loading ignores those keys. */
av_doc *authoring_prototype_source_read(const char *, const prototype_registry *, char *, size_t);
av_doc *authoring_prototype_source_read_with_options(
    const char *,
    const prototype_registry *,
    const authoring_prototype_source_options *,
    char *,
    size_t
);
/* Syntax-only inspector queries: these never execute helper functions. */
av_doc *authoring_prototype_source_parameters(const char *, char *, size_t);
bool authoring_prototype_source_field(
    const char *,
    const char *component,
    const char *field_path,
    authoring_prototype_field_source *
);
av_doc *authoring_prototype_document_read(const char *, const prototype_registry *, char *, size_t);
bool authoring_relationship_declarations_load(const char *, ecs_relationship_desc ***, char *, size_t);
/* The returned descriptor owns its name/key; release it with the paired free. */
ecs_relationship_desc *authoring_relationship_source_read(const char *, char *, size_t);
void authoring_relationship_source_free(ecs_relationship_desc *);
void authoring_asset_source_complete(const script_syntax *, const source_catalog *, int, authoring_completions *);

#endif
