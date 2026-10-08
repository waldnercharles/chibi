#include "authoring/source_presentation_anchors.h"

#include "authoring/source_metadata.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ANCHOR_MAX_RECORDS 262144

static uint64_t key(const source_presentation_origin *o, bool rename)
{
    char shape[512];
    snprintf(
        shape,
        sizeof shape,
        "%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%u:%llu:%llu:%llu:%llu:%d:%d",
        o->function,
        o->kind,
        o->record,
        o->operation,
        o->entry,
        o->port_index,
        o->result_index,
        o->parameter,
        o->output,
        o->flow,
        o->display_class,
        o->type.storage,
        o->type.container,
        o->type.flags,
        (unsigned long long)((rename && (o->type.flags & SCRIPT_TYPE_RESULTS))
                                 ? 0
                                 : o->type.nominal),
        (unsigned long long)o->type.parameter,
        (unsigned long long)(rename ? 0 : o->port_name),
        (unsigned long long)(rename ? 0 : o->function_name),
        o->node_kind,
        o->node_record
    );
    return script_catalog_id("source-anchor", shape);
}
void source_presentation_anchors_write(
    yyjson_mut_doc *doc,
    yyjson_mut_val *root,
    const authoring_source_presentation *p
)
{
    yyjson_mut_val *records = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "source_origins", records);
    yyjson_mut_obj_add_uint(
        doc,
        root,
        "source_root",
        script_catalog_id("source-presentation-root", p->identity)
    );
    for (int i = 0; i < acount(p->origins); i++) {
        yyjson_mut_val *record = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, record, "identity", p->origins[i].identity);
        yyjson_mut_obj_add_uint(doc, record, "anchor", key(&p->origins[i], false));
        yyjson_mut_arr_add_val(records, record);
    }
}
typedef struct
{
    uint64_t previous, current;
} anchor_pair;
static int compare(const void *left, const void *right)
{
    const anchor_pair *a = left, *b = right;
    return (a->previous > b->previous) - (a->previous < b->previous);
}
static uint64_t mapped(const anchor_pair *pairs, int count, uint64_t id)
{
    anchor_pair wanted = { .previous = id };
    const anchor_pair *found = bsearch(&wanted, pairs, (size_t)count, sizeof *pairs, compare);
    return found ? found->current : 0;
}
static bool translate(
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const char *field,
    const anchor_pair *pairs,
    int count
)
{
    yyjson_mut_val *value = yyjson_mut_obj_get(object, field);
    if (!yyjson_mut_is_uint(value) || !yyjson_mut_get_uint(value)) {
        return false;
    }
    uint64_t id = mapped(pairs, count, yyjson_mut_get_uint(value));
    if (!id) {
        return false;
    }
    yyjson_mut_obj_put(object, yyjson_mut_str(doc, field), yyjson_mut_uint(doc, id));
    return true;
}
static char *resolve(
    const char *json,
    const authoring_source_presentation *previous,
    const authoring_source_presentation *p
)
{
    yyjson_doc *source = yyjson_read(json, strlen(json), 0);
    if (!source) {
        return NULL;
    }
    yyjson_val *source_root = yyjson_doc_get_root(source);
    yyjson_val *records = yyjson_obj_get(source_root, "source_origins");
    if (!records) {
        yyjson_doc_free(source);
        return xstrdup(json);
    }
    yyjson_val *root_id = yyjson_obj_get(source_root, "source_root");
    bool valid = yyjson_is_arr(records) && yyjson_is_uint(root_id) &&
                 yyjson_get_uint(root_id) &&
                 yyjson_arr_size(records) <= ANCHOR_MAX_RECORDS &&
                 yyjson_arr_size(records) == (size_t)acount(p->origins) &&
                 acount(previous->origins) == acount(p->origins);
    int pair_count = acount(p->origins) + 1;
    anchor_pair *pairs = valid ? xmalloc((size_t)pair_count * sizeof *pairs) : NULL;
    for (int i = 0; valid && i < acount(p->origins); i++) {
        yyjson_val *record = yyjson_arr_get(records, (size_t)i);
        yyjson_val *id = yyjson_obj_get(record, "identity"),
                   *anchor = yyjson_obj_get(record, "anchor");
        valid = yyjson_is_obj(record) && yyjson_is_uint(id) &&
                yyjson_get_uint(id) && yyjson_is_uint(anchor) &&
                yyjson_get_uint(anchor) == key(&previous->origins[i], false) &&
                (previous == p ||
                 key(&previous->origins[i], true) == key(&p->origins[i], true));
        pairs[i] = (anchor_pair){ yyjson_get_uint(id), p->origins[i].identity };
    }
    if (valid) {
        pairs[pair_count - 1] = (anchor_pair){
            yyjson_get_uint(root_id),
            script_catalog_id("source-presentation-root", p->identity)
        };
        qsort(pairs, (size_t)pair_count, sizeof *pairs, compare);
        for (int i = 1; valid && i < pair_count; i++) {
            valid = pairs[i - 1].previous != pairs[i].previous;
        }
    }
    yyjson_mut_doc *copy = valid ? yyjson_doc_mut_copy(source, NULL) : NULL;
    yyjson_mut_val *root = copy ? yyjson_mut_doc_get_root(copy) : NULL;
    const char *arrays[] = { "source_layout",
                             "source_pin_watches",
                             "source_comment_boxes",
                             "source_edge_routes" };
    for (int a = 0; valid && a < 4; a++) {
        yyjson_mut_val *array = yyjson_mut_obj_get(root, arrays[a]);
        if (array && !yyjson_mut_is_arr(array)) {
            valid = false;
            break;
        }
        size_t i, count;
        yyjson_mut_val *record;
        yyjson_mut_arr_foreach(array, i, count, record)
        {
            valid = translate(
                copy,
                record,
                a < 2    ? "identity"
                : a == 2 ? "owner"
                         : "from",
                pairs,
                pair_count
            );
            if (valid && a == 3) {
                valid = translate(copy, record, "to", pairs, pair_count);
            }
            if (!valid) {
                break;
            }
        }
    }
    if (valid) {
        yyjson_mut_obj_remove_str(root, "source_origins");
        yyjson_mut_obj_remove_str(root, "source_root");
        source_presentation_anchors_write(copy, root, p);
    }
    char *out = valid ? yyjson_mut_write(copy, 0, NULL) : NULL;
    free(pairs);
    yyjson_mut_doc_free(copy);
    yyjson_doc_free(source);
    return out;
}

char *source_presentation_anchors_resolve(const char *json, const authoring_source_presentation *p)
{
    return resolve(json, p, p);
}

static int edit_compare(const void *left, const void *right)
{
    const text_edit *a = left, *b = right;
    return (a->start > b->start) - (a->start < b->start);
}
static bool rename_tree(
    TSNode old,
    TSNode next,
    const char *before,
    const char *after,
    const text_edit *edits,
    int count,
    int depth,
    int *budget
)
{
    if (depth > 256 || --*budget < 0 || strcmp(ts_node_type(old), ts_node_type(next)) ||
        ts_node_child_count(old) != ts_node_child_count(next)) {
        return false;
    }
    uint32_t children = ts_node_child_count(old);
    if (children) {
        for (uint32_t i = 0; i < children; i++) {
            if (!rename_tree(
                    ts_node_child(old, i),
                    ts_node_child(next, i),
                    before,
                    after,
                    edits,
                    count,
                    depth + 1,
                    budget
                )) {
                return false;
            }
        }
        return true;
    }
    int start = (int)ts_node_start_byte(old), end = (int)ts_node_end_byte(old);
    int to = (int)ts_node_start_byte(next), last = (int)ts_node_end_byte(next);
    if (end - start == last - to &&
        !memcmp(before + start, after + to, (size_t)(end - start))) {
        return true;
    }
    const char *type = ts_node_type(old);
    if (strcmp(type, "identifier") && strcmp(type, "string") &&
        strcmp(type, "string_content")) {
        return false;
    }
    int low = 0, high = count;
    while (low < high) {
        int mid = low + (high - low) / 2;
        if (edits[mid].start <= start) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low > 0 && edits[low - 1].end >= end;
}
char *authoring_source_presentation_rebase(
    const authoring_source_presentation *before,
    const authoring_source_presentation *after,
    const char *metadata,
    const text_edit *edits,
    int count
)
{
    if (count < 0 || count > ANCHOR_MAX_RECORDS || (count && !edits) ||
        strlen(before->text) > 8 * 1024 * 1024 || strlen(after->text) > 8 * 1024 * 1024 ||
        !authoring_source_metadata_matches(metadata, before->text)) {
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        if (edits[i].start < 0 || edits[i].end <= edits[i].start || !edits[i].text) {
            return NULL;
        }
    }
    char *expected = text_edits_apply(before->text, edits, count);
    bool valid = expected && !strcmp(expected, after->text);
    free(expected);
    if (!valid) {
        return NULL;
    }
    text_edit *sorted = count ? xmalloc((size_t)count * sizeof *sorted) : NULL;
    if (count) {
        memcpy(sorted, edits, (size_t)count * sizeof *sorted);
        qsort(sorted, (size_t)count, sizeof *sorted, edit_compare);
    }
    script_syntax *old = script_parse(before->text),
                  *next = script_parse(after->text);
    int budget = ANCHOR_MAX_RECORDS;
    valid = !old->diagnostic_count && !next->diagnostic_count &&
            rename_tree(
                ts_tree_root_node(old->tree),
                ts_tree_root_node(next->tree),
                before->text,
                after->text,
                sorted,
                count,
                0,
                &budget
            );
    script_syntax_free(old);
    script_syntax_free(next);
    free(sorted);
    if (!valid) {
        return NULL;
    }
    authoring_source_presentation *source = authoring_source_presentation_clone(before);
    valid = authoring_source_presentation_restore(source, metadata);
    char *complete = valid ? authoring_source_presentation_metadata(source) : NULL;
    authoring_source_presentation_free(source);
    char *mapped = complete ? resolve(complete, before, after) : NULL;
    free(complete);
    char *bound = mapped ? authoring_source_metadata_bind(mapped, after->text) : NULL;
    free(mapped);
    if (!bound) {
        return NULL;
    }
    authoring_source_presentation *destination = authoring_source_presentation_clone(after);
    valid = authoring_source_presentation_restore(destination, bound);
    free(bound);
    char *result = valid ? authoring_source_presentation_metadata(destination) : NULL;
    authoring_source_presentation_free(destination);
    return result;
}
