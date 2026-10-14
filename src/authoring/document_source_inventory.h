#ifndef CHIBI_DOCUMENT_SOURCE_INVENTORY_H
#define CHIBI_DOCUMENT_SOURCE_INVENTORY_H
#include "authoring/document_semantics.h"

typedef struct
{
    const char *kind, *name, *id, *origin, *body, *graph, *type, *signature,
        *text, *initializer;
    int start, end, function;
    bool disabled, pure, writable;
} document_source_member;
typedef struct
{
    const char *symbol, *component, *member, *label, *type, *unit;
    int binding, container;
    bool compatible, writable, range;
    double min, max;
} document_source_binding;
/* Read-only, bounded semantic inventories. Rows/strings belong to storage.
 * Graph zero selects declarations; a function graph also includes its locals,
 * parameters/results. Binding compatibility is assignment typing; validated
 * source preparation remains the final authority for scope/effect/unit rules. */
bool document_source_members(
    const document_semantics *,
    uint64_t graph,
    int first,
    int count,
    const char *query,
    arena *,
    document_source_member **,
    int *rows,
    int *total,
    char *,
    size_t
);
bool document_source_bindings(
    const document_semantics *,
    uint64_t origin,
    int first,
    int count,
    const char *query,
    arena *,
    document_source_binding **,
    int *rows,
    int *total,
    char *,
    size_t
);
#endif
