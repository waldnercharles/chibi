#include "authoring/source_presentation_anchors.h"

#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ANCHOR_MAX_RECORDS 262144

static uint64_t key(const source_presentation_origin *o)
{
    char shape[512];
    snprintf(
        shape,
        sizeof shape,
        "%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%u:%llu:%llu:%llu:%llu:%llu:%d:"
        "%d",
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
        (unsigned long long)o->type.nominal,
        (unsigned long long)o->type.parameter,
        (unsigned long long)o->type.collection,
        (unsigned long long)o->port_name,
        (unsigned long long)o->function_name,
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
        yyjson_mut_obj_add_uint(doc, record, "anchor", key(&p->origins[i]));
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
char *source_presentation_anchors_resolve(const char *json, const authoring_source_presentation *p)
{
    yyjson_doc *source = yyjson_read(json, strlen(json), 0);
    if (!source) {
        return NULL;
    }
    yyjson_val *source_root = yyjson_doc_get_root(source);
    yyjson_val *records = yyjson_obj_get(source_root, "source_origins");
    yyjson_val *root_id = yyjson_obj_get(source_root, "source_root");
    bool valid = yyjson_is_arr(records) && yyjson_is_uint(root_id) &&
                 yyjson_get_uint(root_id) &&
                 yyjson_arr_size(records) <= ANCHOR_MAX_RECORDS &&
                 yyjson_arr_size(records) == (size_t)acount(p->origins);
    int pair_count = acount(p->origins) + 1;
    anchor_pair *pairs = valid ? xmalloc((size_t)pair_count * sizeof *pairs) : NULL;
    for (int i = 0; valid && i < acount(p->origins); i++) {
        yyjson_val *record = yyjson_arr_get(records, (size_t)i);
        yyjson_val *id = yyjson_obj_get(record, "identity"),
                   *anchor = yyjson_obj_get(record, "anchor");
        valid = yyjson_is_obj(record) && yyjson_is_uint(id) &&
                yyjson_get_uint(id) && yyjson_is_uint(anchor) &&
                yyjson_get_uint(anchor) == key(&p->origins[i]);
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
