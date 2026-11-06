#include "authoring/source_presentation_details.h"

#include "authoring/source_presentation_internal.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DETAILS_MAX_RECORDS 4096
#define DETAILS_MAX_TITLE (16 * 1024)
#define DETAILS_MAX_TITLES (1024 * 1024)

static uint64_t graph_owner(const authoring_source_presentation *p, const graph *g)
{
    if (g == p->view->graph) {
        return script_catalog_id("source-presentation-root", p->identity);
    }
    const source_presentation_origin
        *o = authoring_source_presentation_origin(p, g->graph_id, g->entry_id, 0);
    return o && o->kind == SOURCE_PRESENTATION_FUNCTION ? o->identity : 0;
}
static graph *owner_graph(const authoring_source_presentation *p, uint64_t owner)
{
    for (int i = 0; i < acount(p->view->graphs); ++i) {
        graph *g = p->view->graphs[i];
        if (graph_owner(p, g) == owner) {
            return g;
        }
    }
    return NULL;
}
static const source_presentation_origin *origin(const authoring_source_presentation *p, uint64_t identity)
{
    for (int i = 0; i < acount(p->origins); ++i) {
        if (p->origins[i].identity == identity) {
            return &p->origins[i];
        }
    }
    return NULL;
}
static uint64_t endpoint(const authoring_source_presentation *p, const graph *g, const edge *e, bool output)
{
    const source_presentation_origin *o = authoring_source_presentation_origin(
        p,
        g->graph_id,
        output ? e->from_id : e->to_id,
        output ? e->from_pin_id : e->to_pin_id
    );
    return o && o->output == output ? o->identity : 0;
}
static edge *find_edge(
    const authoring_source_presentation *p,
    uint64_t from,
    uint64_t to,
    graph_edge_kind kind,
    int order
)
{
    const source_presentation_origin *a = origin(p, from), *b = origin(p, to);
    if (!a || !b || !a->pin || !b->pin || !a->output || b->output ||
        a->graph != b->graph || a->flow != (kind == GRAPH_EDGE_EXEC) ||
        b->flow != (kind == GRAPH_EDGE_EXEC)) {
        return NULL;
    }
    graph *g = behavior_asset_find_graph(p->view, a->graph);
    for (int i = 0; g && i < acount(g->edges); ++i) {
        edge *e = &g->edges[i];
        if (e->from_id == a->node && e->from_pin_id == a->pin && e->to_id == b->node &&
            e->to_pin_id == b->pin && e->kind == kind && e->order == order) {
            return e;
        }
    }
    return NULL;
}
static bool finite_number(yyjson_val *record, const char *key, bool nonnegative)
{
    yyjson_val *v = yyjson_obj_get(record, key);
    double number = yyjson_get_num(v);
    return yyjson_is_num(v) && isfinite(number) && isfinite((float)number) &&
           (!nonnegative || number >= 0);
}
static uint64_t number(yyjson_val *record, const char *key)
{
    return yyjson_get_uint(yyjson_obj_get(record, key));
}
static bool identity(yyjson_val *record, const char *key)
{
    yyjson_val *v = yyjson_obj_get(record, key);
    return yyjson_is_uint(v) && yyjson_get_uint(v) != 0;
}
static bool boxes_valid(yyjson_val *boxes, const authoring_source_presentation *p)
{
    size_t i, count;
    yyjson_val *record;
    size_t title_bytes = 0;
    yyjson_arr_foreach(boxes, i, count, record)
    {
        yyjson_val *title = yyjson_obj_get(record, "title");
        yyjson_val *color = yyjson_obj_get(record, "color");
        if (!yyjson_is_obj(record) || !identity(record, "owner") ||
            !identity(record, "id") ||
            number(record, "id") > UINT64_MAX - DETAILS_MAX_RECORDS - 1 ||
            !owner_graph(p, number(record, "owner")) ||
            !finite_number(record, "x", false) || !finite_number(record, "y", false) ||
            !finite_number(record, "width", true) ||
            !finite_number(record, "height", true) || !yyjson_is_str(title) ||
            yyjson_get_len(title) > DETAILS_MAX_TITLE ||
            memchr(yyjson_get_str(title), 0, yyjson_get_len(title)) ||
            !yyjson_is_uint(color) || yyjson_get_uint(color) > UINT32_MAX) {
            return false;
        }
        title_bytes += yyjson_get_len(title);
        if (title_bytes > DETAILS_MAX_TITLES) {
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (number(yyjson_arr_get(boxes, j), "id") == number(record, "id")) {
                return false;
            }
        }
    }
    return true;
}
static edge *record_edge(yyjson_val *record, const authoring_source_presentation *p)
{
    yyjson_val *kind = yyjson_obj_get(record, "kind");
    yyjson_val *order = yyjson_obj_get(record, "order");
    if (!yyjson_is_obj(record) || !identity(record, "from") ||
        !identity(record, "to") || !yyjson_is_uint(kind) ||
        yyjson_get_uint(kind) > GRAPH_EDGE_EXEC || !yyjson_is_int(order) ||
        yyjson_get_sint(order) < 0 || yyjson_get_sint(order) > INT32_MAX) {
        return NULL;
    }
    return find_edge(
        p,
        number(record, "from"),
        number(record, "to"),
        (graph_edge_kind)yyjson_get_uint(kind),
        (int)yyjson_get_sint(order)
    );
}
static bool routes_valid(yyjson_val *routes, const authoring_source_presentation *p)
{
    size_t i, count;
    yyjson_val *record;
    yyjson_arr_foreach(routes, i, count, record)
    {
        edge *e = record_edge(record, p);
        yyjson_val *points = yyjson_obj_get(record, "points");
        if (!e || !yyjson_is_arr(points) ||
            yyjson_arr_size(points) > GRAPH_EDGE_MAX_REROUTES) {
            return false;
        }
        size_t j, n;
        yyjson_val *point;
        yyjson_arr_foreach(points, j, n, point)
        {
            if (!yyjson_is_obj(point) || !finite_number(point, "x", false) ||
                !finite_number(point, "y", false)) {
                return false;
            }
        }
        for (size_t j = 0; j < i; ++j) {
            if (record_edge(yyjson_arr_get(routes, j), p) == e) {
                return false;
            }
        }
    }
    return true;
}
bool source_presentation_details_validate(yyjson_val *root, const authoring_source_presentation *p)
{
    yyjson_val *boxes = yyjson_obj_get(root, "source_comment_boxes");
    yyjson_val *routes = yyjson_obj_get(root, "source_edge_routes");
    return yyjson_is_obj(root) &&
           (!boxes || (yyjson_is_arr(boxes) &&
                       yyjson_arr_size(boxes) <= DETAILS_MAX_RECORDS)) &&
           (!routes || (yyjson_is_arr(routes) &&
                        yyjson_arr_size(routes) <= DETAILS_MAX_RECORDS)) &&
           boxes_valid(boxes, p) && routes_valid(routes, p);
}
static void write_box(yyjson_mut_doc *doc, yyjson_mut_val *array, uint64_t owner, const graph_comment_box *box)
{
    yyjson_mut_val *record = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_uint(doc, record, "owner", owner);
    yyjson_mut_obj_add_uint(doc, record, "id", box->id);
    yyjson_mut_obj_add_real(doc, record, "x", box->x);
    yyjson_mut_obj_add_real(doc, record, "y", box->y);
    yyjson_mut_obj_add_real(doc, record, "width", box->width);
    yyjson_mut_obj_add_real(doc, record, "height", box->height);
    yyjson_mut_obj_add_strcpy(doc, record, "title", box->title ? box->title : "");
    yyjson_mut_obj_add_uint(doc, record, "color", box->color);
    yyjson_mut_arr_add_val(array, record);
}
static void write_route(yyjson_mut_doc *doc, yyjson_mut_val *array, uint64_t from, uint64_t to, const edge *e)
{
    yyjson_mut_val *record = yyjson_mut_obj(doc), *points = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_uint(doc, record, "from", from);
    yyjson_mut_obj_add_uint(doc, record, "to", to);
    yyjson_mut_obj_add_uint(doc, record, "kind", e->kind);
    yyjson_mut_obj_add_int(doc, record, "order", e->order);
    for (int i = 0; i < e->reroute_count; ++i) {
        yyjson_mut_val *point = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_real(doc, point, "x", e->reroutes[i].x);
        yyjson_mut_obj_add_real(doc, point, "y", e->reroutes[i].y);
        yyjson_mut_arr_add_val(points, point);
    }
    yyjson_mut_obj_add_val(doc, record, "points", points);
    yyjson_mut_arr_add_val(array, record);
}
static uint64_t mapped(uint64_t id, const source_presentation_identity_map *map, int count)
{
    if (!map) {
        return id;
    }
    for (int i = 0; i < count; ++i) {
        if (map[i].previous == id) {
            return map[i].current;
        }
    }
    return 0;
}
static bool write_details(
    yyjson_mut_doc *doc,
    yyjson_mut_val *root,
    const authoring_source_presentation *source,
    const authoring_source_presentation *target,
    const source_presentation_identity_map *map,
    int count
)
{
    yyjson_mut_val *boxes = yyjson_mut_arr(doc), *routes = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "source_comment_boxes", boxes);
    yyjson_mut_obj_add_val(doc, root, "source_edge_routes", routes);
    int box_count = 0, route_count = 0;
    for (int i = 0; i < acount(source->view->graphs); ++i) {
        const graph *g = source->view->graphs[i];
        uint64_t owner = graph_owner(source, g);
        if (!owner && acount(g->comment_boxes)) {
            return false;
        }
        if (g == source->view->graph && !strcmp(source->identity, target->identity)) {
            owner = graph_owner(target, target->view->graph);
        } else {
            owner = mapped(owner, map, count);
        }
        for (int j = 0; owner_graph(target, owner) && j < acount(g->comment_boxes);
             ++j) {
            if (++box_count > DETAILS_MAX_RECORDS) {
                return false;
            }
            write_box(doc, boxes, owner, &g->comment_boxes[j]);
        }
        for (int j = 0; j < acount(g->edges); ++j) {
            const edge *e = &g->edges[j];
            if (!e->reroute_count) {
                continue;
            }
            if (e->reroute_count < 0 || e->reroute_count > GRAPH_EDGE_MAX_REROUTES) {
                return false;
            }
            uint64_t from = endpoint(source, g, e, true),
                     to = endpoint(source, g, e, false);
            if (!from || !to) {
                return false;
            }
            from = mapped(from, map, count);
            to = mapped(to, map, count);
            if (!find_edge(target, from, to, e->kind, e->order)) {
                continue;
            }
            if (++route_count > DETAILS_MAX_RECORDS) {
                return false;
            }
            write_route(doc, routes, from, to, e);
        }
    }
    return true;
}
bool source_presentation_details_write(
    yyjson_mut_doc *doc,
    yyjson_mut_val *root,
    const authoring_source_presentation *p
)
{
    if (!write_details(doc, root, p, p, NULL, 0)) {
        return false;
    }
    yyjson_doc *copy = yyjson_mut_val_imut_copy(root, NULL);
    bool valid = source_presentation_details_validate(yyjson_doc_get_root(copy), p);
    yyjson_doc_free(copy);
    return valid;
}
static bool recorded_id(yyjson_val *root, uint64_t id)
{
    yyjson_val *boxes = yyjson_obj_get(root, "source_comment_boxes");
    size_t i, count;
    yyjson_val *record;
    yyjson_arr_foreach(boxes, i, count, record)
    {
        if (number(record, "id") == id) {
            return true;
        }
    }
    return false;
}
static bool id_used(const authoring_source_presentation *p, uint64_t id)
{
    for (int i = 0; i < acount(p->view->graphs); ++i) {
        const graph *g = p->view->graphs[i];
        if (g->graph_id == id) {
            return true;
        }
        for (int j = 0; j < acount(g->nodes); ++j) {
            const node *n = g->nodes[j];
            if (n->id == id) {
                return true;
            }
            for (int k = 0; k < n->input_count; ++k) {
                if (n->input_pins[k].id == id) {
                    return true;
                }
            }
            for (int k = 0; k < n->output_count; ++k) {
                if (n->output_pins[k].id == id) {
                    return true;
                }
            }
        }
        for (int j = 0; j < acount(g->comment_boxes); ++j) {
            if (g->comment_boxes[j].id == id) {
                return true;
            }
        }
    }
    return false;
}
void source_presentation_details_apply(yyjson_val *root, authoring_source_presentation *p)
{
    assert(source_presentation_details_validate(root, p));
    for (int i = 0; i < acount(p->view->graphs); ++i) {
        graph *g = p->view->graphs[i];
        asetlen(g->comment_boxes, 0);
        for (int j = 0; j < acount(g->edges); ++j) {
            g->edges[j].reroute_count = 0;
            g->edges[j].custom_handles = false;
        }
    }
    size_t i, count;
    yyjson_val *record;
    yyjson_arr_foreach(yyjson_obj_get(root, "source_comment_boxes"), i, count, record)
    {
        graph *g = owner_graph(p, number(record, "owner"));
        uint64_t id = number(record, "id");
        if (id_used(p, id)) {
            do {
                id = graph_alloc_id(g);
            } while (id_used(p, id) || recorded_id(root, id));
        }
        uint64_t *counter = g->shared_id_counter ? g->shared_id_counter : &g->id_counter;
        if (*counter <= id) {
            *counter = id + 1;
        }
        graph_comment_box box = {
            .id = id,
            .x = (float)yyjson_get_num(yyjson_obj_get(record, "x")),
            .y = (float)yyjson_get_num(yyjson_obj_get(record, "y")),
            .width = (float)yyjson_get_num(yyjson_obj_get(record, "width")),
            .height = (float)yyjson_get_num(yyjson_obj_get(record, "height")),
            .title = source_view_own(g, yyjson_get_str(yyjson_obj_get(record, "title"))),
            .color = (uint32_t)number(record, "color")
        };
        apush(g->comment_boxes, box);
    }
    yyjson_arr_foreach(yyjson_obj_get(root, "source_edge_routes"), i, count, record)
    {
        edge *e = record_edge(record, p);
        yyjson_val *points = yyjson_obj_get(record, "points");
        size_t j, n;
        yyjson_val *point;
        yyjson_arr_foreach(points, j, n, point)
        {
            e->reroutes[j].x = (float)yyjson_get_num(yyjson_obj_get(point, "x"));
            e->reroutes[j].y = (float)yyjson_get_num(yyjson_obj_get(point, "y"));
        }
        e->reroute_count = (int)yyjson_arr_size(points);
    }
}
bool source_presentation_details_carry(
    const authoring_source_presentation *previous,
    authoring_source_presentation *current,
    const source_presentation_identity_map *map,
    int count
)
{
    if (count < 0 || (!map && count)) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        if (!map[i].previous || !map[i].current) {
            return false;
        }
        for (int j = 0; j < i; ++j) {
            if (map[j].previous == map[i].previous || map[j].current == map[i].current) {
                return false;
            }
        }
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    char *json = write_details(doc, root, previous, current, map, count)
                   ? yyjson_mut_write(doc, 0, NULL)
                   : NULL;
    yyjson_mut_doc_free(doc);
    if (!json) {
        return false;
    }
    yyjson_doc *read = yyjson_read(json, strlen(json), 0);
    free(json);
    yyjson_val *snapshot = yyjson_doc_get_root(read);
    bool valid = source_presentation_details_validate(snapshot, current);
    if (valid) {
        source_presentation_details_apply(snapshot, current);
    }
    yyjson_doc_free(read);
    return valid;
}
