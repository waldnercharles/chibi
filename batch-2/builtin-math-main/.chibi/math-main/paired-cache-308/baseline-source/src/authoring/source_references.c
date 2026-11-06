#include "authoring/source_references.h"

#include "authoring/reference_index.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool relationship_target(const script_catalog *catalog, authoring_target target)
{
    for (int i = 0; target.kind == AUTHORING_TYPE && i < catalog->relationship_count;
         i++) {
        if (catalog->relationships[i].id == target.id) {
            return true;
        }
    }
    return false;
}
static int source_asset(
    reference_index *index,
    const authoring_document *document,
    authoring_target target,
    const script_catalog *catalog
)
{
    reference_asset_kind kind;
    if (document->analysis) {
        switch (document->analysis->kind) {
            case SCRIPT_PROGRAM_BEHAVIOR:
                kind = REFERENCE_ASSET_BEHAVIOR;
                break;
            case SCRIPT_PROGRAM_LIBRARY: kind = REFERENCE_ASSET_LIBRARY; break;
            case SCRIPT_PROGRAM_SYSTEM: kind = REFERENCE_ASSET_SYSTEM; break;
            default: assert(false); return -1;
        }
    } else {
        for (int i = 0; i < reference_index_asset_count(index); i++) {
            const reference_asset *asset = reference_index_asset_at(index, i);
            if (asset->path && !strcmp(asset->path, document->path)) {
                return i;
            }
        }
        kind = relationship_target(catalog, target) ? REFERENCE_ASSET_RELATIONSHIP
             : target.kind == AUTHORING_PROTOTYPE ? REFERENCE_ASSET_PROTOTYPE
                                                  : REFERENCE_ASSET_EVENT;
    }
    const char *name = document->analysis ? document->analysis->name : NULL;
    if (!name && relationship_target(catalog, target)) {
        for (int i = 0; i < catalog->relationship_count; i++) {
            if (catalog->relationships[i].id == target.id) {
                name = catalog->relationships[i].name;
                break;
            }
        }
    }
    char *fallback = NULL;
    if (!name || !*name) {
        const char *base = strrchr(document->path, '/');
        fallback = xstrdup(base ? base + 1 : document->path);
        char *extension = strrchr(fallback, '.');
        if (extension) {
            *extension = 0;
        }
        name = fallback;
    }
    int asset = reference_index_add_asset(index, kind, name, document->path, NULL);
    free(fallback);
    return asset;
}

static reference_role source_role(authoring_reference_kind kind)
{
    switch (kind) {
        case AUTHORING_DEFINE: return REFERENCE_DEFINE;
        case AUTHORING_READ: return REFERENCE_READ;
        case AUTHORING_WRITE: return REFERENCE_WRITE;
        case AUTHORING_CALL: return REFERENCE_CALL;
        case AUTHORING_HANDLE: return REFERENCE_HANDLE;
    }
    assert(false);
    return REFERENCE_READ;
}

static reference_role resolved_role(
    const script_catalog *catalog,
    const script_analysis *analysis,
    const authoring_reference *fact
)
{
    if (fact->kind == AUTHORING_HANDLE && fact->target.kind == AUTHORING_PROTOTYPE) {
        return REFERENCE_BIND;
    }
    if (fact->kind == AUTHORING_HANDLE && fact->target.kind == AUTHORING_CALLABLE) {
        for (int i = 0; i < analysis->expression_count; i++) {
            const script_expression *expression = &analysis->expressions[i];
            if (expression->kind != SCRIPT_EXPR_CALL ||
                expression->range.start > fact->location.range.start ||
                expression->range.end < fact->location.range.end) {
                continue;
            }
            const script_catalog_capability *
                capability = script_catalog_capability_by_id(catalog, expression->capability);
            if (capability && capability->event_sender) {
                return REFERENCE_SEND;
            }
        }
    }
    return source_role(fact->kind);
}

static uint64_t function_id(const script_analysis *analysis, int index)
{
    if (index >= 0 && index < analysis->function_count) {
        const script_function *function = &analysis->functions[index];
        if (function->symbol >= 0 && function->symbol < analysis->symbol_count) {
            const script_symbol *symbol = &analysis->symbols[function->symbol];
            return symbol->capability ? symbol->capability : symbol->id;
        }
        if (function->event) {
            return function->event;
        }
    }
    return script_catalog_id("program", analysis->identity);
}

static uint64_t containing_function(const script_analysis *analysis, script_range range)
{
    int selected = -1;
    for (int i = 0; i < analysis->function_count; i++) {
        const script_range candidate = analysis->functions[i].range;
        if (candidate.start <= range.start && candidate.end >= range.end &&
            (selected < 0 || candidate.end - candidate.start <
                                 analysis->functions[selected].range.end -
                                     analysis->functions[selected].range.start)) {
            selected = i;
        }
    }
    return function_id(analysis, selected);
}

void authoring_source_references_add(reference_index *index, const authoring_navigation *navigation)
{
    const script_catalog *catalog = authoring_navigation_catalog(navigation);
    for (int i = 0; i < authoring_navigation_reference_count(navigation); i++) {
        const authoring_reference *fact = authoring_navigation_reference_at(navigation, i);
        const authoring_document *document = authoring_navigation_document_at(
            navigation,
            fact->location.document
        );
        if (!document->analysis) {
            continue; /* Native declarations are indexed by their owning KDL adapters. */
        }
        int source = source_asset(index, document, fact->target, catalog);
        if (source < 0) {
            continue;
        }
        const authoring_reference
            *definition = authoring_navigation_definition(navigation, fact->target);
        int asset = definition ? source_asset(
                                     index,
                                     authoring_navigation_document_at(
                                         navigation,
                                         definition->location.document
                                     ),
                                     fact->target,
                                     catalog
                                 )
                               : -1;
        const char *name = authoring_navigation_name(navigation, fact->target);
        reference_symbol symbol = { .asset = asset, .id = fact->target.id, .name = name };
        switch (fact->target.kind) {
            case AUTHORING_PROGRAM:
            case AUTHORING_PROTOTYPE:
                symbol.kind = REFERENCE_SYMBOL_ASSET;
                break;
            case AUTHORING_CALLABLE: {
                const script_catalog_capability
                    *cap = script_catalog_capability_by_id(catalog, fact->target.id);
                symbol.kind = cap && cap->kind == SCRIPT_CAPABILITY_EVENT
                                ? REFERENCE_SYMBOL_EVENT
                                : REFERENCE_SYMBOL_GRAPH;
                symbol.graph_id = fact->target.id;
                break;
            }
            case AUTHORING_SYMBOL: {
                symbol.kind = REFERENCE_SYMBOL_VARIABLE;
                const authoring_document *owner = definition
                                                    ? authoring_navigation_document_at(
                                                          navigation,
                                                          definition->location.document
                                                      )
                                                    : document;
                if (owner->analysis) {
                    for (int j = 0; j < owner->analysis->symbol_count; j++) {
                        const script_symbol *item = &owner->analysis->symbols[j];
                        if (item->id != fact->target.id) {
                            continue;
                        }
                        if (item->kind == SCRIPT_PARAMETER ||
                            item->kind == SCRIPT_RESULT) {
                            symbol.kind = REFERENCE_SYMBOL_PARAMETER;
                            symbol.graph_id = function_id(owner->analysis, item->function);
                        } else if (item->kind == SCRIPT_LOCAL || item->kind == SCRIPT_ACTION) {
                            symbol.kind = REFERENCE_SYMBOL_LOCAL;
                            symbol.graph_id = function_id(owner->analysis, item->function);
                        }
                        break;
                    }
                }
                break;
            }
            case AUTHORING_TYPE:
                symbol.kind = relationship_target(catalog, fact->target)
                                ? REFERENCE_SYMBOL_ASSET
                                : REFERENCE_SYMBOL_COMPONENT;
                if (symbol.kind == REFERENCE_SYMBOL_ASSET && symbol.asset < 0 && name) {
                    symbol.asset = reference_index_named_asset(index, REFERENCE_ASSET_RELATIONSHIP, name);
                }
                break;
            case AUTHORING_FIELD:
                symbol.kind = REFERENCE_SYMBOL_FIELD;
                symbol.field = name;
                for (int j = 0; j < catalog->type_count; j++) {
                    if (catalog->types[j].id == fact->target.owner) {
                        symbol.name = catalog->types[j].name;
                    }
                }
                break;
        }
        reference_index_add_reference(
            index,
            (reference){
                .role = resolved_role(catalog, document->analysis, fact),
                .symbol = symbol,
                .source = source,
                .graph_id = containing_function(
                    document->analysis,
                    fact->location.range
                ),
                .source_start = fact->location.range.start,
                .source_end = fact->location.range.end,
            }
        );
    }
}
