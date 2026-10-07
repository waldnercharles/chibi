#ifndef CHIBI_CODE_SOURCE_CONTEXT_H
#define CHIBI_CODE_SOURCE_CONTEXT_H

#include "graph/model/behavior_asset.h"

typedef enum
{
    CODE_REFERENCE_NONE,
    CODE_REFERENCE_PROTOTYPE,
    CODE_REFERENCE_COMPONENT
} code_reference_kind;
typedef struct
{
    code_reference_kind kind;
    const char *name;
} code_reference;
typedef struct
{
    const char *name;
    const char *const *components;
    int component_count;
    const char *const *behaviors;
    int behavior_count;
} code_prototype;

/* Borrowed authored definitions. Parsing uses their signatures without linking,
 * compiling, mutating, or taking ownership of any catalog entry. */
typedef struct
{
    const behavior_asset *const *libraries;
    int library_count;
    const behavior_asset *const *events;
    int event_count;
    const code_reference *references;
    int reference_count;
    const code_prototype *prototypes;
    int prototype_count;
    const char *const *owner_prototypes;
    int owner_prototype_count;
} code_source_context;

/* Resolve a typed source symbol to its canonical runtime identity. */
const char *code_source_context_reference(
    const code_source_context *,
    code_reference_kind,
    const char *,
    char *,
    size_t
);

/* Unqualified calls resolve in the authored scope first, then in the catalog.
 * Qualified calls require exactly one matching asset and callable signature.
 * resolved_asset is NULL for a local call, or the borrowed library name. */
const graph *code_source_context_graph(
    const code_source_context *context,
    const behavior_asset *local,
    const char *asset,
    const char *name,
    graph_kind kind,
    const char **resolved_asset,
    char *error,
    size_t size
);
const behavior_asset *code_source_context_event(
    const code_source_context *context,
    const char *name,
    char *error,
    size_t size
);

#endif
