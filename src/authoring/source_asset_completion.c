#include "authoring/source_asset.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <string.h>

static bool is(TSNode node, const char *kind)
{
    return !ts_node_is_null(node) && !strcmp(ts_node_type(node), kind);
}
static void offer(
    authoring_completions *out,
    const char *prefix,
    int length,
    const char *name,
    const char *detail,
    authoring_target_kind target,
    uint64_t id,
    uint64_t owner
)
{
    if (!name || strncmp(name, prefix, (size_t)length)) {
        return;
    }
    apush(
        out->items,
        ((authoring_completion){ .label = xstrdup(name),
                                 .detail = xstrdup(detail),
                                 .insert = xstrdup(name),
                                 .kind = target == AUTHORING_FIELD ? AUTHORING_COMPLETE_FIELD
                                                                   : AUTHORING_COMPLETE_TYPE,
                                 .target = { target, -1, id, owner } })
    );
    out->count = acount(out->items);
}
void authoring_asset_source_complete(
    const script_syntax *syntax,
    const source_catalog *project,
    int offset,
    authoring_completions *out
)
{
    memset(out, 0, sizeof *out);
    int size = (int)strlen(syntax->text);
    if (offset < 0 || offset > size) {
        return;
    }
    int start = offset, end = offset;
    while (start && (isalnum((unsigned char)syntax->text[start - 1]) ||
                     syntax->text[start - 1] == '_')) {
        start--;
    }
    while (end < size && (isalnum((unsigned char)syntax->text[end]) ||
                          syntax->text[end] == '_')) {
        end++;
    }
    out->range = (script_range){ .start = start, .end = end };
    const char *prefix = syntax->text + start;
    int length = offset - start;
    TSNode tree = ts_tree_root_node(syntax->tree);
    TSNode node = ts_node_named_descendant_for_byte_range(tree, (uint32_t)start, (uint32_t)end);
    bool relationship = false, bases = false, links = false, behaviors = false;
    TSNode component = { 0 };
    for (TSNode at = node; !ts_node_is_null(at); at = ts_node_parent(at)) {
        relationship |= is(at, "relationship_declaration");
        bases |= is(at, "prototype_bases");
        if (is(at, "prototype_component")) {
            component = at;
        }
        if (is(at, "struct_field")) {
            TSNode name = ts_node_child_by_field_name(at, "name", 4);
            if (ts_node_is_null(name)) {
                continue;
            }
            uint32_t first = ts_node_start_byte(name), last = ts_node_end_byte(name);
            links |= last - first == 5 && !strncmp(syntax->text + first, "links", 5);
            behaviors |= last - first == 9 &&
                         !strncmp(syntax->text + first, "behaviors", 9);
        }
    }
    static const script_catalog empty;
    const script_catalog *catalog = project ? source_catalog_view(project) : &empty;
    if (relationship) {
        const char *const names[] = { "exclusive",   "acyclic",
                                      "traversable", "on_delete_target",
                                      "Remove",      "Delete",
                                      "Panic",       "true",
                                      "false" };
        for (int i = 0; i < 9; i++) {
            offer(out, prefix, length, names[i], "relationship property or value", AUTHORING_TYPE, 0, 0);
        }
        return;
    }
    if (links) {
        for (int i = 0; i < catalog->relationship_count; i++) {
            offer(
                out,
                prefix,
                length,
                catalog->relationships[i].name,
                "relationship",
                AUTHORING_TYPE,
                catalog->relationships[i].id,
                0
            );
        }
        offer(out, prefix, length, "child", "prototype child target namespace", AUTHORING_SYMBOL, 0, 0);
        return;
    }
    if (behaviors) {
        for (int i = 0; project && i < source_catalog_source_count(project); i++) {
            const source_catalog_source *source = source_catalog_source_at(project, i);
            const script_analysis *analysis = source->declarations;
            if (analysis && analysis->kind == SCRIPT_PROGRAM_BEHAVIOR &&
                analysis->name) {
                offer(
                    out,
                    prefix,
                    length,
                    analysis->name,
                    "behavior",
                    AUTHORING_PROGRAM,
                    script_catalog_id("program", analysis->identity),
                    SCRIPT_PROGRAM_BEHAVIOR
                );
            }
        }
        return;
    }
    if (!ts_node_is_null(component)) {
        TSNode name = ts_node_child_by_field_name(component, "name", 4);
        if (ts_node_is_null(name)) {
            return;
        }
        uint32_t first = ts_node_start_byte(name), last = ts_node_end_byte(name);
        for (int i = 0; i < catalog->type_count; i++) {
            const script_catalog_type *record = &catalog->types[i];
            const char *label = record->source_name ? record->source_name
                                                    : record->name;
            if (!label || strlen(label) != last - first ||
                strncmp(label, syntax->text + first, last - first)) {
                continue;
            }
            for (int j = 0; j < record->field_count; j++) {
                offer(
                    out,
                    prefix,
                    length,
                    record->fields[j].name,
                    "component default field",
                    AUTHORING_FIELD,
                    record->fields[j].id,
                    record->id
                );
            }
        }
        return;
    }
    for (int i = 0; i < catalog->capability_count; i++) {
        if (catalog->capabilities[i].kind == SCRIPT_CAPABILITY_PROTOTYPE) {
            offer(
                out,
                prefix,
                length,
                catalog->capabilities[i].name,
                "prototype base or child prototype",
                AUTHORING_PROTOTYPE,
                catalog->capabilities[i].id,
                0
            );
        }
    }
    if (bases) {
        return;
    }
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *type = &catalog->types[i];
        if (type->component) {
            offer(
                out,
                prefix,
                length,
                type->source_name ? type->source_name : type->name,
                "prototype component defaults",
                AUTHORING_TYPE,
                type->id,
                0
            );
        }
    }
    const char *const properties[] = { "behaviors", "children", "links", "identity" };
    for (int i = 0; i < 4; i++) {
        offer(out, prefix, length, properties[i], "prototype property", AUTHORING_TYPE, 0, 0);
    }
}
void authoring_completions_free(authoring_completions *out)
{
    if (!out) {
        return;
    }
    for (int i = 0; i < out->count; i++) {
        free(out->items[i].label);
        free(out->items[i].detail);
        free(out->items[i].insert);
    }
    afree(out->items);
    *out = (authoring_completions){ 0 };
}
