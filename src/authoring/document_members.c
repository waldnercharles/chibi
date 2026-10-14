#include "authoring/document_members.h"

#include "authoring/document_source_inventory.h"
#include "graph/document/graph_signature.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"
#include "types/value_document.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *copy(arena *a, const char *s)
{
    if (!s) {
        s = "";
    }
    char *out = arena_alloc(a, strlen(s) + 1);
    strcpy(out, s);
    return out;
}
static const char *identity(arena *a, uint64_t id)
{
    char out[32];
    snprintf(out, sizeof out, "%llu", (unsigned long long)id);
    return copy(a, out);
}
static const char *slice(arena *a, const char *text, TSNode n)
{
    if (ts_node_is_null(n)) {
        return copy(a, "");
    }
    int start = (int)ts_node_start_byte(n), end = (int)ts_node_end_byte(n);
    char *out = arena_alloc(a, (size_t)(end - start) + 1);
    memcpy(out, text + start, (size_t)(end - start));
    out[end - start] = 0;
    return out;
}
static bool fail(char *error, size_t size, const char *s)
{
    if (size) {
        snprintf(error, size, "%s", s);
    }
    return false;
}
static TSNode field(TSNode n, const char *s)
{
    return ts_node_child_by_field_name(n, s, (uint32_t)strlen(s));
}
static const source_presentation_origin *function_origin(const document_semantics *s, uint64_t graph)
{
    const authoring_source_presentation *p = document_semantics_presentation(s);
    for (int i = 0; p && i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
        if (o->graph == graph && o->kind == SOURCE_PRESENTATION_FUNCTION && !o->pin) {
            return o;
        }
    }
    return NULL;
}
static const char *literal(arena *a, const pin_value *value)
{
    av_doc *doc = av_doc_new();
    av_value *v = pin_value_to_document(doc, value);
    char *text = v ? av_value_write(v) : NULL;
    const char *out = copy(a, text);
    free(text);
    av_doc_free(doc);
    return out;
}
static void native_pin(arena *a, document_member *out, const pin_spec *p, uint64_t graph, int index, bool result)
{
    *out = (document_member){
        .kind = copy(a, result ? "output" : "input"),
        .id = identity(a, p->id),
        .origin = copy(a, "0"),
        .graph = identity(a, graph),
        .name = copy(a, p->name),
        .type = copy(a, p->type_name ? p->type_name : pin_type_name(p->type)),
        .value = literal(a, &p->default_value),
        .storage = p->type,
        .container = p->container,
        .index = index,
        .result = result,
        .editable = true,
        .has_default = !result
    };
}
bool document_members_inspect(
    const document_semantics *s,
    uint64_t selected,
    arena *a,
    document_member **members,
    int *member_count,
    document_member **pins,
    int *pin_count,
    document_member **types,
    int *type_count,
    char *error,
    size_t size
)
{
    *members = *pins = *types = NULL;
    *member_count = *pin_count = *type_count = 0;
    if (size) {
        *error = 0;
    }
    const behavior_asset *asset = document_semantics_graph(s);
    if (!asset) {
        return fail(error, size, "Document has no graph representation");
    }
    if (acount(asset->graphs) > 4096) {
        return fail(error, size, "Member inventory exceeds4096 graphs");
    }
    *members = arena_alloc(a, 4096 * sizeof **members);
    *pins = arena_alloc(a, 256 * sizeof **pins);
    const script_analysis *analysis = document_semantics_analysis(s);
    if (analysis) {
        document_source_member *rows = NULL;
        int count = 0, total = 0;
        for (int first = 0;; first += 256) {
            if (!document_source_members(s, selected, first, 256, "", a, &rows, &count, &total, error, size)) {
                return false;
            }
            if (total > 4096) {
                return fail(error, size, "Member inventory exceeds4096 declarations");
            }
            for (int i = 0; i < count; i++) {
                const document_source_member *r = &rows[i];
                if (!strcmp(r->kind, "parameter") || !strcmp(r->kind, "result") ||
                    !strcmp(r->kind, "local") || !strcmp(r->kind, "constant")) {
                    continue;
                }
                (*members)[(*member_count)++] = (document_member){
                    .kind = copy(a, r->kind),
                    .id = copy(a, r->id),
                    .origin = copy(a, r->origin),
                    .graph = copy(a, r->graph),
                    .name = copy(a, r->name),
                    .type = copy(a, r->type),
                    .value = copy(a, r->text),
                    .pure = r->pure,
                    .editable = strcmp(r->origin, "0") != 0
                };
            }
            if (first + count >= total) {
                break;
            }
        }
        const source_presentation_origin *o = function_origin(s, selected);
        if (o && o->function >= 0 && o->function < analysis->function_count) {
            const script_function *f = &analysis->functions[o->function];
            script_syntax *syntax = script_parse(analysis->text);
            TSNode node = ts_node_named_descendant_for_byte_range(
                ts_tree_root_node(syntax->tree),
                (uint32_t)f->range.start,
                (uint32_t)f->range.end
            );
            while (!ts_node_is_null(node) &&
                   strcmp(ts_node_type(node), "function_declaration") &&
                   strcmp(ts_node_type(node), "macro_declaration")) {
                node = ts_node_parent(node);
            }
            for (int side = 0; side < 2; side++) {
                TSNode list = field(node, side ? "results" : "parameters");
                if (ts_node_is_null(list)) {
                    continue;
                }
                bool shorthand = strcmp(ts_node_type(list), "parameter_list") != 0;
                int n = shorthand ? 1 : (int)ts_node_named_child_count(list);
                if (*pin_count + n > 256) {
                    script_syntax_free(syntax);
                    return fail(error, size, "Callable exceeds256 pins");
                }
                for (int i = 0; i < n; i++) {
                    TSNode parameter = shorthand
                                         ? list
                                         : ts_node_named_child(list, (uint32_t)i);
                    TSNode name = shorthand ? (TSNode){ 0 } : field(parameter, "name"),
                           type = shorthand ? parameter : field(parameter, "type"),
                           value = shorthand ? (TSNode){ 0 }
                                             : field(parameter, "value");
                    int symbol = i < (side ? f->result_count : f->parameter_count)
                                   ? (side ? f->results[i] : f->parameters[i])
                                   : -1;
                    script_type resolved = symbol >= 0 && symbol < analysis->symbol_count
                                             ? analysis->symbols[symbol].type
                                             : (script_type){ 0 };
                    (*pins)[(*pin_count)++] = (document_member){
                        .kind = copy(a, side ? "output" : "input"),
                        .id = identity(
                            a,
                            symbol >= 0 && symbol < analysis->symbol_count
                                ? analysis->symbols[symbol].id
                                : 0
                        ),
                        .origin = identity(a, o->identity),
                        .graph = identity(a, selected),
                        .name = slice(a, analysis->text, name),
                        .type = slice(a, analysis->text, type),
                        .value = slice(a, analysis->text, value),
                        .storage = resolved.storage,
                        .container = resolved.container,
                        .index = i,
                        .result = side != 0,
                        .pure = f->pure,
                        .editable = true,
                        .has_default = !ts_node_is_null(value)
                    };
                }
            }
            script_syntax_free(syntax);
        }
    } else {
        if (acount(asset->variables) + acount(asset->graphs) > 4096) {
            return fail(error, size, "Member inventory exceeds4096 declarations");
        }
        for (int i = 0; i < acount(asset->variables); i++) {
            const behavior_variable *v = &asset->variables[i];
            (*members)[(*member_count)++] = (document_member){
                .kind = copy(a, "variable"),
                .id = identity(a, v->id),
                .origin = copy(a, "0"),
                .graph = identity(a, selected),
                .name = copy(a, v->name),
                .type = copy(a, v->type_name ? v->type_name : pin_type_name(v->type)),
                .value = literal(a, &v->default_value),
                .storage = v->type,
                .container = v->container,
                .editable = true,
                .has_default = true
            };
        }
        for (int i = 0; i < acount(asset->graphs); i++) {
            const graph *g = asset->graphs[i];
            if (g->kind != GRAPH_FUNCTION && g->kind != GRAPH_MACRO) {
                continue;
            }
            (*members)[(*member_count)++] = (document_member){
                .kind = copy(a, g->kind == GRAPH_MACRO ? "macro" : "function"),
                .id = identity(a, g->graph_id),
                .origin = copy(a, "0"),
                .graph = identity(a, g->graph_id),
                .name = copy(a, g->name),
                .type = copy(a, ""),
                .value = copy(a, ""),
                .pure = graph_signature_pure(g),
                .editable = true
            };
        }
        const graph *g = behavior_asset_find_graph(asset, selected);
        if (g && (g->kind == GRAPH_FUNCTION || g->kind == GRAPH_MACRO)) {
            const node *entry = graph_node_by_id(g, g->entry_id),
                       *exit = graph_node_by_id(g, g->return_id);
            for (int side = 0; side < 2; side++) {
                const node *n = side ? exit : entry;
                if (!n) {
                    continue;
                }
                int count = side ? n->input_count : n->output_count;
                if (*pin_count + count > 256) {
                    return fail(error, size, "Callable exceeds256 pins");
                }
                for (int i = 0; i < count; i++) {
                    native_pin(
                        a,
                        &(*pins)[(*pin_count)++],
                        side ? &n->input_pins[i] : &n->output_pins[i],
                        selected,
                        i,
                        side != 0
                    );
                }
            }
        }
    }
    const script_catalog *catalog = document_semantics_catalog(s);
    int count = catalog ? catalog->type_count : 0;
    if (count > 4096) {
        return fail(error, size, "Type inventory exceeds4096 declarations");
    }
    *types = arena_alloc(a, (size_t)(count + 16) * sizeof **types);
    const char *builtin[] = { "bool",  "int",    "int64", "uint64",
                              "float", "double", "string" };
    for (int i = 0; i < 7; i++) {
        script_type t = script_builtin_type(builtin[i]);
        (*types)[(*type_count)++] = (document_member){ .kind = copy(a, "type"),
                                                       .id = copy(a, "0"),
                                                       .origin = copy(a, "0"),
                                                       .graph = copy(a, "0"),
                                                       .name = copy(a, builtin[i]),
                                                       .type = copy(a, builtin[i]),
                                                       .value = copy(a, ""),
                                                       .storage = t.storage,
                                                       .editable = true };
    }
    for (int i = 0; i < count; i++) {
        const script_catalog_type *t = &catalog->types[i];
        if (t->internal || !t->name) {
            continue;
        }
        (*types)[(*type_count)++] = (document_member){
            .kind = copy(a, "type"),
            .id = identity(a, t->id),
            .origin = copy(a, "0"),
            .graph = copy(a, "0"),
            .name = copy(a, t->name),
            .type = copy(a, t->source_name ? t->source_name : t->name),
            .value = copy(a, ""),
            .storage = t->type.storage,
            .container = t->type.container,
            .editable = true
        };
    }
    return true;
}
