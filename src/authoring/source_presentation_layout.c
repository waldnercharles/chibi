#include "authoring/source_presentation_anchors.h"
#include "authoring/source_presentation_details.h"
#include "authoring/source_presentation_internal.h"
#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

/* Editor state snapshots record a hash of the exact text they describe. */
static void text_hash(const char *text, char out[17])
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; text[i]; i++) {
        hash = (hash ^ (unsigned char)text[i]) * UINT64_C(1099511628211);
    }
    snprintf(out, 17, "%016llx", (unsigned long long)hash);
}
static bool bound_to(const char *json, const char *text)
{
    assert(json);
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    const char *bound = yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(doc), "source_hash"));
    char expected[17];
    text_hash(text, expected);
    bool matches = bound && !strcmp(bound, expected);
    yyjson_doc_free(doc);
    return matches;
}

char *authoring_source_presentation_metadata(const authoring_source_presentation *p)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc), *nodes = yyjson_mut_arr(doc),
                   *watches = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, root);
    char hash[17];
    text_hash(p->text, hash);
    yyjson_mut_obj_add_uint(doc, root, "version", 2);
    yyjson_mut_obj_add_strcpy(doc, root, "source_hash", hash);
    yyjson_mut_obj_add_val(doc, root, "source_layout", nodes);
    yyjson_mut_obj_add_val(doc, root, "source_pin_watches", watches);
    source_presentation_anchors_write(doc, root, p);
    for (int i = 0; i < acount(p->origins); i++) {
        const source_presentation_origin *o = &p->origins[i];
        if (o->node && o->pin && !o->flow) {
            const graph *g = behavior_asset_find_graph(p->view, o->graph);
            const node *n = graph_node_by_id(g, o->node);
            if (!n || o->port_index < 0 ||
                o->port_index >= (o->output ? n->output_count : n->input_count)) {
                continue;
            }
            const pin_spec *pin = o->output ? &n->output_pins[o->port_index]
                                            : &n->input_pins[o->port_index];
            if (pin->flags & PIN_FLAG_WATCH) {
                yyjson_mut_val *watch = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_uint(doc, watch, "identity", o->identity);
                yyjson_mut_arr_add_val(watches, watch);
            }
        }
        if (!o->node || o->pin) {
            continue;
        }
        const graph *g = behavior_asset_find_graph(p->view, o->graph);
        const node *n = graph_node_by_id(g, o->node);
        if (!n) {
            continue;
        }
        yyjson_mut_val *record = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_uint(doc, record, "identity", o->identity);
        /* Automatic arrangement belongs to the view, not document history. */
        yyjson_mut_obj_add_real(doc, record, "x", n->placed ? n->x : 0);
        yyjson_mut_obj_add_real(doc, record, "y", n->placed ? n->y : 0);
        yyjson_mut_obj_add_bool(doc, record, "placed", n->placed);
        yyjson_mut_obj_add_uint(doc, record, "presentation", n->presentation);
        yyjson_mut_obj_add_bool(doc, record, "breakpoint", n->breakpoint != 0);
        yyjson_mut_obj_add_bool(doc, record, "show_advanced", n->show_advanced);
        if (n->comment) {
            yyjson_mut_obj_add_strcpy(doc, record, "comment", n->comment);
            yyjson_mut_obj_add_bool(doc, record, "comment_hidden", n->comment_hidden);
        }
        yyjson_mut_arr_add_val(nodes, record);
    }
    if (!source_presentation_details_write(doc, root, p)) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}
static bool valid_record(const yyjson_val *record)
{
    const yyjson_val *identity = yyjson_obj_get(record, "identity");
    const yyjson_val *x = yyjson_obj_get(record, "x"),
                     *y = yyjson_obj_get(record, "y");
    const yyjson_val *placed = yyjson_obj_get(record, "placed");
    const yyjson_val *presentation = yyjson_obj_get(record, "presentation");
    const yyjson_val *breakpoint = yyjson_obj_get(record, "breakpoint");
    const yyjson_val *advanced = yyjson_obj_get(record, "show_advanced");
    const yyjson_val *comment = yyjson_obj_get(record, "comment");
    const yyjson_val *hidden = yyjson_obj_get(record, "comment_hidden");
    return yyjson_is_obj(record) && yyjson_is_uint(identity) &&
           yyjson_get_uint(identity) != 0 && yyjson_is_num(x) &&
           yyjson_is_num(y) && isfinite((float)yyjson_get_num(x)) &&
           isfinite((float)yyjson_get_num(y)) && yyjson_is_bool(placed) &&
           yyjson_is_uint(presentation) &&
           yyjson_get_uint(presentation) <= NODE_PRESENTATION_EXPANDED &&
           yyjson_is_bool(breakpoint) && yyjson_is_bool(advanced) &&
           (!comment ||
            (yyjson_is_str(comment) &&
             !memchr(yyjson_get_str(comment), 0, yyjson_get_len(comment)))) &&
           (!hidden || yyjson_is_bool(hidden));
}
static bool restore(authoring_source_presentation *p, const char *json)
{
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *version = yyjson_obj_get(root, "version");
    yyjson_val *nodes = yyjson_obj_get(root, "source_layout");
    if (!yyjson_is_uint(version) || yyjson_get_uint(version) != 2 ||
        !yyjson_is_arr(nodes) || yyjson_arr_size(nodes) > (size_t)acount(p->origins)) {
        yyjson_doc_free(doc);
        return false;
    }
    yyjson_val *watches = yyjson_obj_get(root, "source_pin_watches");
    if (watches && (!yyjson_is_arr(watches) ||
                    yyjson_arr_size(watches) > (size_t)acount(p->origins))) {
        yyjson_doc_free(doc);
        return false;
    }
    size_t i, count;
    yyjson_val *record;
    yyjson_arr_foreach(watches, i, count, record)
    {
        yyjson_val *id = yyjson_obj_get(record, "identity");
        if (!yyjson_is_obj(record) || !yyjson_is_uint(id) || !yyjson_get_uint(id)) {
            yyjson_doc_free(doc);
            return false;
        }
    }
    yyjson_arr_foreach(nodes, i, count, record)
    {
        if (!valid_record(record)) {
            yyjson_doc_free(doc);
            return false;
        }
    }
    if (!source_presentation_details_validate(root, p)) {
        yyjson_doc_free(doc);
        return false;
    }
    yyjson_arr_foreach(nodes, i, count, record)
    {
        uint64_t id = yyjson_get_uint(yyjson_obj_get(record, "identity"));
        for (int j = 0; j < acount(p->origins); j++) {
            const source_presentation_origin *o = &p->origins[j];
            if (o->identity != id || !o->node || o->pin) {
                continue;
            }
            graph *g = behavior_asset_find_graph(p->view, o->graph);
            node *n = graph_node_by_id(g, o->node);
            if (!n) {
                break;
            }
            n->x = (float)yyjson_get_num(yyjson_obj_get(record, "x"));
            n->y = (float)yyjson_get_num(yyjson_obj_get(record, "y"));
            n->placed = yyjson_get_bool(yyjson_obj_get(record, "placed"));
            n->presentation = (node_presentation)yyjson_get_uint(
                yyjson_obj_get(record, "presentation")
            );
            n->breakpoint = yyjson_get_bool(yyjson_obj_get(record, "breakpoint"));
            n->show_advanced = yyjson_get_bool(yyjson_obj_get(record, "show_advanced"));
            graph_set_node_comment(
                g,
                n,
                yyjson_get_str(yyjson_obj_get(record, "comment")),
                yyjson_get_bool(yyjson_obj_get(record, "comment_hidden"))
            );
            break;
        }
    }
    /* The snapshot lists every watch, so restoring it clears removed ones. */
    for (int j = 0; j < acount(p->origins); j++) {
        const source_presentation_origin *o = &p->origins[j];
        if (!o->node || !o->pin || o->flow) {
            continue;
        }
        bool watched = false;
        yyjson_arr_foreach(watches, i, count, record)
        {
            watched |= yyjson_get_uint(yyjson_obj_get(record, "identity")) == o->identity;
        }
        graph *g = behavior_asset_find_graph(p->view, o->graph);
        node *n = graph_node_by_id(g, o->node);
        if (!n || o->port_index < 0 ||
            o->port_index >= (o->output ? n->output_count : n->input_count)) {
            continue;
        }
        int status = graph_set_pin_watch(g, n, o->output, o->port_index, watched);
        assert(!status);
        (void)status;
    }
    source_presentation_details_apply(root, p);
    yyjson_doc_free(doc);
    return true;
}

bool authoring_source_presentation_restore(authoring_source_presentation *p, const char *json)
{
    if (!bound_to(json, p->text)) {
        return false;
    }
    char *resolved = source_presentation_anchors_resolve(json, p);
    if (!resolved) {
        return false;
    }
    bool valid = restore(p, resolved);
    free(resolved);
    return valid;
}
