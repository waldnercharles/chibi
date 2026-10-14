#include "authoring/document_graph_selection.h"

#include "authoring/document_graph.h"
#include "graph/authoring/graph_edit.h"
#include "graph/authoring/layout.h"
#include "graph/document/serialize.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
void document_graph_selection_result_free(document_graph_selection_result *result)
{
    free(result->text);
    free(result->metadata);
    afree(result->created);
    *result = (document_graph_selection_result){ 0 };
}
static bool coordinate(float value)
{
    return isfinite(value) && fabsf(value) <= 1000000;
}
static bool selected(
    const graph *g,
    const document_graph_selection_request *r,
    node ***nodes,
    char *error,
    size_t size
)
{
    if (r->count < 1 || r->count > DOCUMENT_GRAPH_SELECTION_MAX || !r->nodes) {
        return fail(error, size, "Select one to 256 nodes");
    }
    for (int i = 0; i < r->count; i++) {
        node *n = graph_node_by_id(g, r->nodes[i]);
        if (!n) {
            return fail(error, size, "A selected node does not belong to this graph revision");
        }
        if ((r->operation == DOCUMENT_SELECTION_DELETE ||
             r->operation == DOCUMENT_SELECTION_CLONE) &&
            (n->id == g->entry_id || n->id == g->return_id)) {
            return fail(error, size, "Edit function entry and return through its signature");
        }
        for (int j = 0; j < i; j++) {
            if (r->nodes[j] == r->nodes[i]) {
                return fail(error, size, "A selection cannot contain a node twice");
            }
        }
        apush(*nodes, n);
    }
    return true;
}
static bool move_nodes(graph *g, const document_graph_selection_request *r, node **nodes)
{
    for (int i = 0; i < r->count; i++) {
        nodes[i]->x = r->x[i];
        nodes[i]->y = r->y[i];
        nodes[i]->placed = true;
    }
    graph_place_unplaced(g);
    return true;
}
static bool change(
    behavior_asset *asset,
    graph *g,
    const document_graph_selection_request *r,
    document_graph_selection_result *out,
    char *error,
    size_t size
)
{
    node **nodes = NULL;
    bool valid = true;
    if (r->operation <= DOCUMENT_SELECTION_PLACE ||
        r->operation == DOCUMENT_SELECTION_COPY) {
        valid = selected(g, r, &nodes, error, size);
    }
    if (!valid) {
        afree(nodes);
        return false;
    }
    switch (r->operation) {
        case DOCUMENT_SELECTION_DELETE:
            for (int i = 0; valid && i < r->count; i++) {
                node *n = graph_node_by_id(g, r->nodes[i]);
                valid = !n || !graph_remove_node_tree(g, n);
            }
            break;
        case DOCUMENT_SELECTION_COPY:
            out->text = graph_save_nodes(g, nodes, r->count);
            valid = out->text != NULL;
            break;
        case DOCUMENT_SELECTION_CLONE:
        case DOCUMENT_SELECTION_PASTE: {
            if (!coordinate(r->dx) || !coordinate(r->dy)) {
                valid = fail(error, size, "Paste offsets must be finite bounded coordinates");
                break;
            }
            bool positions = r->operation == DOCUMENT_SELECTION_CLONE && r->points > 0;
            if (positions) {
                valid = r->points == r->count && r->x && r->y;
                for (int i = 0; valid && i < r->count; i++) {
                    valid = coordinate(r->x[i]) && coordinate(r->y[i]);
                }
                if (!valid) {
                    fail(error, size, "Clone positions must cover the selection with finite bounded coordinates");
                    break;
                }
            }
            float original_x[DOCUMENT_GRAPH_SELECTION_MAX],
                original_y[DOCUMENT_GRAPH_SELECTION_MAX];
            bool original_placed[DOCUMENT_GRAPH_SELECTION_MAX];
            if (positions) {
                for (int i = 0; i < r->count; i++) {
                    original_x[i] = nodes[i]->x;
                    original_y[i] = nodes[i]->y;
                    original_placed[i] = nodes[i]->placed;
                    nodes[i]->x = r->x[i];
                    nodes[i]->y = r->y[i];
                    nodes[i]->placed = true;
                }
            }
            int before = acount(g->nodes);
            char *clipboard = r->operation == DOCUMENT_SELECTION_CLONE
                                ? graph_save_nodes(g, nodes, r->count)
                                : NULL;
            if (positions) {
                for (int i = 0; i < r->count; i++) {
                    nodes[i]->x = original_x[i];
                    nodes[i]->y = original_y[i];
                    nodes[i]->placed = original_placed[i];
                }
            }
            const char *text = clipboard ? clipboard : r->text;
            av_doc *document = text && strlen(text) <= 4 * 1024 * 1024
                                 ? av_doc_read(text, error, size)
                                 : NULL;
            const av_value *root = document ? av_doc_root(document) : NULL;
            size_t count = av_arr_size(av_obj_get(root, "nodes"));
            valid = count > 0 && count <= DOCUMENT_GRAPH_SELECTION_MAX &&
                    acount(g->nodes) <= 4096 - (int)count &&
                    !graph_append_value(g, av_doc_root(document), r->dx, r->dy);
            av_doc_free(document);
            free(clipboard);
            if (valid && acount(g->nodes) - before > DOCUMENT_GRAPH_SELECTION_MAX) {
                valid = fail(error, size, "Pasted selection exceeds 256 nodes including inline children");
            }
            for (int i = before; valid && i < acount(g->nodes); i++) {
                const node *n = g->nodes[i];
                valid = coordinate(n->x) && coordinate(n->y);
                if (r->operation == DOCUMENT_SELECTION_PASTE) {
                    valid = valid &&
                            document_graph_node_allowed(asset->domain, n->desc);
                }
                if (valid) {
                    apush(out->created, n->id);
                }
            }
            break;
        }
        case DOCUMENT_SELECTION_PLACE:
            valid = r->points == r->count && r->x && r->y;
            for (int i = 0; valid && i < r->count; i++) {
                valid = coordinate(r->x[i]) && coordinate(r->y[i]);
            }
            if (valid) {
                valid = move_nodes(g, r, nodes);
            }
            break;
        case DOCUMENT_SELECTION_COMMENT: {
            node *n = graph_node_by_id(g, r->node);
            valid = n && r->text && strlen(r->text) <= 16384 &&
                    !graph_set_node_comment(g, n, r->text, r->hidden);
            break;
        }
        case DOCUMENT_SELECTION_BOX: {
            graph_comment_box *box = NULL;
            for (int i = 0; i < acount(g->comment_boxes); i++) {
                if (g->comment_boxes[i].id == r->box) {
                    box = &g->comment_boxes[i];
                }
            }
            valid = r->text && strlen(r->text) <= 1024 && coordinate(r->dx) &&
                    coordinate(r->dy) && coordinate(r->width) &&
                    coordinate(r->height) && r->width >= 1 && r->height >= 1 &&
                    (!r->box || box);
            if (!valid) {
                break;
            }
            if (!box && acount(g->comment_boxes) < DOCUMENT_GRAPH_SELECTION_MAX) {
                box = graph_add_comment_box(g, r->text, r->dx, r->dy, r->width, r->height);
            }
            valid = box && !graph_set_comment_box(g, box, r->text, r->color);
            if (valid) {
                box->x = r->dx;
                box->y = r->dy;
                box->width = r->width;
                box->height = r->height;
                out->box = box->id;
            }
            break;
        }
        case DOCUMENT_SELECTION_REMOVE_BOX:
            valid = !graph_remove_comment_box(g, r->box);
            break;
        case DOCUMENT_SELECTION_REROUTE: {
            edge *found = NULL;
            for (int i = 0; i < acount(g->edges); i++) {
                edge *e = &g->edges[i];
                if (e->from_id == r->from_node && e->from_pin_id == r->from_pin &&
                    e->to_id == r->to_node && e->to_pin_id == r->to_pin) {
                    found = e;
                }
            }
            valid = found && r->points >= 0 && r->points <= GRAPH_EDGE_MAX_REROUTES &&
                    (!r->points || (r->x && r->y));
            for (int i = 0; valid && i < r->points; i++) {
                valid = coordinate(r->x[i]) && coordinate(r->y[i]);
            }
            if (valid) {
                found->reroute_count = r->points;
                found->custom_handles = false;
                for (int i = 0; i < r->points; i++) {
                    found->reroutes[i].x = r->x[i];
                    found->reroutes[i].y = r->y[i];
                }
            }
            break;
        }
        default: valid = false; break;
    }
    afree(nodes);
    if (!valid && (!size || !*error)) {
        return fail(error, size, "Invalid graph selection or presentation request");
    }
    return valid;
}
bool document_graph_selection_prepare(
    const document_semantics *snapshot,
    const document_graph_selection_request *r,
    document_graph_selection_result *out,
    char *error,
    size_t size
)
{
    *out = (document_graph_selection_result){ 0 };
    if (size) {
        *error = 0;
    }
    const authoring_source_presentation *source = document_semantics_presentation(snapshot);
    if (r->operation == DOCUMENT_SELECTION_SOURCE_COPY) {
        if (!source || r->count < 1 ||
            r->count > DOCUMENT_GRAPH_SELECTION_MAX || !r->nodes) {
            return fail(error, size, "Source copy requires one to 256 semantic origins");
        }
        out->text = authoring_source_clipboard(source, r->nodes, r->count, error, size);
        return out->text != NULL;
    }
    if (r->operation == DOCUMENT_SELECTION_NODE_FLAG && r->flag_kind == 0 && source) {
        if (r->count != 1 || !r->nodes) {
            return fail(error, size, "Disable one complete source node at a time");
        }
        const source_presentation_origin *selected = NULL;
        for (int i = 0; i < authoring_source_presentation_origin_count(source); i++) {
            const source_presentation_origin *o = authoring_source_presentation_origin_at(source, i);
            if (o->graph == r->graph && o->node == r->nodes[0] && !o->pin) {
                selected = o;
            }
        }
        if (!selected) {
            return fail(error, size, "Selected source node no longer belongs to this revision");
        }
        authoring_edit_request request = {
            .kind = AUTHORING_EDIT_SET_DISABLED,
            .generation = authoring_source_edit_generation(source),
            .origin = selected->identity,
            .disabled = r->hidden
        };
        authoring_edit_result result = { 0 };
        bool ok = document_semantics_prepare_edit(snapshot, &request, &result, error, size);
        if (ok) {
            out->text = result.text;
            out->metadata = result.metadata;
        }
        return ok;
    }
    bool metadata = source != NULL;
    const behavior_asset *original = document_semantics_graph(snapshot);
    if (!original) {
        return fail(error, size, "Document has no graph representation");
    }
    if (metadata && (r->operation <= DOCUMENT_SELECTION_CLONE ||
                     r->operation == DOCUMENT_SELECTION_COPY ||
                     r->operation == DOCUMENT_SELECTION_PASTE)) {
        return fail(error, size, "Source topology and clipboard use semantic source edit requests");
    }
    authoring_source_presentation *presentation = metadata
                                                    ? authoring_source_presentation_clone(source)
                                                    : NULL;
    behavior_asset *asset = presentation
                              ? authoring_source_presentation_view(presentation)
                              : behavior_asset_clone(original);
    graph *g = asset ? behavior_asset_find_graph(asset, r->graph) : NULL;
    bool valid = false;
    if (g && r->operation == DOCUMENT_SELECTION_NODE_FLAG) {
        valid = r->nodes && r->count > 0 && r->count <= DOCUMENT_GRAPH_SELECTION_MAX &&
                r->flag_kind >= 0 && r->flag_kind <= 3;
        for (int i = 0; valid && i < r->count; i++) {
            valid = graph_node_by_id(g, r->nodes[i]) != NULL;
        }
        for (int i = 0; valid && i < r->count; i++) {
            node *selected = graph_node_by_id(g, r->nodes[i]);
            if (r->flag_kind == 0) {
                valid = !graph_set_node_disabled(g, selected, r->hidden);
            } else if (r->flag_kind == 1) {
                valid = !graph_set_breakpoint(g, selected, r->hidden);
            } else if (r->flag_kind == 2) {
                valid = !graph_set_node_presentation(
                    g,
                    selected,
                    r->hidden ? NODE_PRESENTATION_INLINE : NODE_PRESENTATION_EXPANDED
                );
            } else {
                selected->show_advanced = r->hidden;
            }
        }
    } else {
        valid = g && change(asset, g, r, out, error, size);
    }
    if (!g) {
        fail(error, size, "Graph does not belong to this document revision");
    }
    if (valid && metadata) {
        out->metadata = authoring_source_presentation_metadata(presentation);
        out->text = xstrdup(authoring_source_presentation_text(source));
        valid = out->metadata != NULL;
    } else if (valid && r->operation != DOCUMENT_SELECTION_COPY) {
        out->text = behavior_asset_save(asset);
        behavior_asset *check = out->text ? behavior_asset_load(out->text) : NULL;
        valid = check != NULL;
        behavior_asset_free(check);
    }
    if (presentation) {
        authoring_source_presentation_free(presentation);
    } else {
        behavior_asset_free(asset);
    }
    if (!valid) {
        document_graph_selection_result_free(out);
        if (!size || !*error) {
            fail(error, size, "Could not serialize graph candidate");
        }
    }
    return valid;
}
