#include "assets/code_program.h"
#include "assets/code_program_internal.h"
#include "assets/code_source_intelligence.h"
#include "assets/code_source_internal.h"
#include "graph/document/serialize.h"
#include "graph/model/local.h"
#include "graph/model/reactive.h"
#include "graph/model/registry.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/code_lex.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
EM_JS(int, web_save_code_metadata, (const char *path, const char *text), {
    return Module['chibiSaveAsset']
             ? Module['chibiSaveAsset'](UTF8ToString(path), UTF8ToString(text))
             : 0;
})
#endif

#define METADATA_ID_LIMIT 1000000
#define METADATA_PREFIX "/* @authoring "

typedef struct
{
    uint64_t from, to;
    bool variable;
} identity_map;
static uint64_t mapped(const identity_map *map, uint64_t id)
{
    for (int i = 0; i < acount(map); i++) {
        if (map[i].from == id) {
            return map[i].to;
        }
    }
    return id;
}
static bool add_identity_role(identity_map **map, uint64_t from, uint64_t to, bool variable)
{
    if (!from || !to || to > METADATA_ID_LIMIT) {
        return from == to;
    }
    for (int i = 0; i < acount(*map); i++) {
        if ((*map)[i].from == from) {
            return (*map)[i].to == to;
        }
        if ((*map)[i].variable == variable && (*map)[i].to == to) {
            return false;
        }
    }
    apush(*map, ((identity_map){ from, to, variable }));
    return true;
}
static bool add_identity(identity_map **map, uint64_t from, uint64_t to)
{
    return add_identity_role(map, from, to, false);
}
static bool variable_node(const node *n)
{
    return n->desc == &NODE_GetVariable || n->desc == &NODE_SetVariable;
}
static bool action(const node *n)
{
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].kind == PIN_EXEC) {
            return true;
        }
    }
    for (int i = 0; i < n->output_count; i++) {
        if (n->output_pins[i].kind == PIN_EXEC) {
            return true;
        }
    }
    return false;
}
static uint64_t owner(const graph *g, const node *n, int depth)
{
    if (action(n)) {
        return n->id;
    }
    if (depth > acount(g->nodes)) {
        return 0;
    }
    uint64_t found = 0;
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->from_id != n->id || e->kind == GRAPH_EDGE_EXEC) {
            continue;
        }
        const node *next = graph_node_by_id(g, e->to_id);
        uint64_t id = next ? owner(g, next, depth + 1) : 0;
        if (found && id && found != id) {
            return 0;
        }
        if (id) {
            found = id;
        }
    }
    return found;
}
static uint64_t caller(const behavior_asset *asset, const graph *g)
{
    if (g == asset->graph) {
        return 0;
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        for (int j = 0; j < acount(asset->graphs[i]->nodes); j++) {
            const node *n = asset->graphs[i]->nodes[j];
            if (!n->reference_asset && n->reference_id == g->graph_id &&
                (n->desc == &NODE_CallFunction || n->desc == &NODE_CallPureFunction ||
                 n->desc == &NODE_CallMacro || n->desc == &NODE_BehaviorExpression)) {
                return n->id;
            }
        }
    }
    return 0;
}
static const char *reference(const behavior_asset *asset, const node *n)
{
    const behavior_variable *variable = n->reference_asset || !variable_node(n)
                                          ? NULL
                                          : behavior_asset_find_variable_by_id(asset, n->reference_id);
    return variable          ? variable->name
         : n->reference_name ? n->reference_name
                             : "";
}
/* Projection ancestry identifies source-derived aggregate outputs. */
static const char **pin_parent_path(const pin_spec *pins, int count, int index)
{
    const char **path = NULL;
    const pin_spec *pin = &pins[index];
    for (int depth = 0; depth < count; depth++) {
        uint64_t id = pin->field_source ? pin->field_source : pin->split_parent;
        const pin_spec *parent = NULL;
        for (int i = 0; id && i < count; i++) {
            if (pins[i].id == id) {
                parent = &pins[i];
                break;
            }
        }
        if (!parent) {
            break;
        }
        apush(path, parent->name);
        pin = parent;
    }
    return path;
}
static av_value *write_pins(av_doc *doc, const pin_spec *pins, int count)
{
    av_value *items = av_arr(doc);
    for (int i = 0; i < count; i++) {
        av_value *item = av_arr_add_obj(doc, items);
        av_obj_add_uint(doc, item, "id", pins[i].id);
        av_obj_add_str(doc, item, "name", pins[i].name);
        if (pins[i].field_source && pins[i].field_name) {
            av_obj_add_str(doc, item, "field", pins[i].field_name);
        }
        const char **path = pin_parent_path(pins, count, i);
        if (acount(path)) {
            av_value *parents = av_obj_add_arr(doc, item, "parents");
            for (int p = acount(path) - 1; p >= 0; p--) {
                av_arr_add_str(doc, parents, path[p]);
            }
        }
        afree(path);
        if (pins[i].flags & PIN_FLAG_WATCH) {
            av_obj_add_bool(doc, item, "watch", true);
        }
        if (pins[i].display_name) {
            av_obj_add_str(doc, item, "display", pins[i].display_name);
        }
    }
    return items;
}
static uint64_t authored_counter(const behavior_asset *asset)
{
    uint64_t counter = asset->graph->id_counter;
    for (int i = 0; i < acount(asset->variables); i++) {
        if (asset->variables[i].id >= counter) {
            counter = asset->variables[i].id + 1;
        }
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *g = asset->graphs[i];
        if (g->graph_id >= counter) {
            counter = g->graph_id + 1;
        }
        for (int j = 0; j < acount(g->locals); j++) {
            if (g->locals[j].id >= counter) {
                counter = g->locals[j].id + 1;
            }
        }
        for (int j = 0; j < acount(g->comment_boxes); j++) {
            if (g->comment_boxes[j].id >= counter) {
                counter = g->comment_boxes[j].id + 1;
            }
        }
        for (int j = 0; j < acount(g->nodes); j++) {
            const node *n = g->nodes[j];
            if (n->id >= counter) {
                counter = n->id + 1;
            }
            for (int output = 0; output < 2; output++) {
                const pin_spec *pins = output ? n->output_pins : n->input_pins;
                int count = output ? n->output_count : n->input_count;
                for (int k = 0; k < count; k++) {
                    if (pins[k].id >= counter) {
                        counter = pins[k].id + 1;
                    }
                }
            }
        }
    }
    return counter;
}

/* Expression builders may append an inherited literal after its consumers.
 * Presentation records follow the expression's input traversal, independent of
 * allocation order. This never reorders the executable graph. */
static void expression_nodes_visit(const graph *g, const node *n, const node ***seen, const node ***ordered)
{
    if (!n) {
        return;
    }
    for (int i = 0; i < acount(*seen); i++) {
        if ((*seen)[i] == n) {
            return;
        }
    }
    apush(*seen, n);
    for (int i = 0; i < n->input_count; i++) {
        node *source = NULL;
        int output = 0;
        if (graph_input_source(g, n, i, &source, &output)) {
            expression_nodes_visit(g, source, seen, ordered);
        }
    }
    apush(*ordered, n);
}
static const node **presentation_nodes(const graph *g, bool canonical)
{
    const node **ordered = NULL;
    if (!g->math_expression || !canonical) {
        for (int i = 0; i < acount(g->nodes); i++) {
            apush(ordered, g->nodes[i]);
        }
        return ordered;
    }
    const node **seen = NULL;
    const node *entry = graph_node_by_id(g, g->entry_id),
               *ret = graph_node_by_id(g, g->return_id);
    if (entry) {
        apush(seen, entry);
        apush(ordered, entry);
    }
    if (ret) {
        apush(seen, ret);
        apush(ordered, ret);
    }
    for (int i = 0; ret && i < ret->input_count; i++) {
        node *source = NULL;
        int output = 0;
        if (graph_input_source(g, ret, i, &source, &output)) {
            expression_nodes_visit(g, source, &seen, &ordered);
        }
    }
    for (int i = 0; i < acount(g->nodes); i++) {
        expression_nodes_visit(g, g->nodes[i], &seen, &ordered);
    }
    afree(seen);
    return ordered;
}
char *code_source_metadata_export(const behavior_asset *asset)
{
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_uint(doc, root, "version", 2);
    av_obj_add_uint(doc, root, "id_counter", authored_counter(asset));
    if (asset->description) {
        av_obj_add_str(doc, root, "description", asset->description);
    }
    av_value *variables = av_obj_add_arr(doc, root, "variables");
    for (int i = 0; i < acount(asset->variables); i++) {
        const behavior_variable *v = &asset->variables[i];
        av_value *item = av_arr_add_obj(doc, variables);
        av_obj_add_str(doc, item, "name", v->name);
        av_obj_add_uint(doc, item, "id", v->id);
        if (v->category) {
            av_obj_add_str(doc, item, "category", v->category);
        }
    }
    av_value *graphs = av_obj_add_arr(doc, root, "graphs");
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *g = asset->graphs[i];
        av_value *item = av_arr_add_obj(doc, graphs);
        av_obj_add_uint(doc, item, "id", g->graph_id);
        av_obj_add_uint(doc, item, "kind", g->kind);
        av_obj_add_uint(doc, item, "caller", caller(asset, g));
        if (!g->math_expression || !g->name || strcmp(g->name, g->math_expression)) {
            av_obj_add_str(doc, item, "name", g->name ? g->name : "");
        }
        if (!g->breakpoints_enabled) {
            av_obj_add_bool(doc, item, "breakpoints_enabled", false);
        }
        av_obj_add_str(doc, item, "description", g->description ? g->description : "");
        av_value *locals = av_obj_add_arr(doc, item, "locals");
        for (int j = 0; j < acount(g->locals); j++) {
            const graph_local *local = &g->locals[j];
            av_value *record = av_arr_add_obj(doc, locals);
            av_obj_add_uint(doc, record, "id", local->id);
            av_obj_add_str(doc, record, "name", local->name);
        }
        if (g->math_expression) {
            av_obj_add_str(doc, item, "node_order", "inputs");
        }
        av_value *nodes = av_obj_add_arr(doc, item, "nodes");
        const node **ordered = presentation_nodes(g, true);
        for (int j = 0; j < acount(ordered); j++) {
            const node *n = ordered[j];
            av_value *record = av_arr_add_obj(doc, nodes);
            av_obj_add_uint(doc, record, "id", n->id);
            uint64_t owned = !g->math_expression ? owner(g, n, 0) : 0;
            if (owned) {
                av_obj_add_uint(doc, record, "owner", owned);
            }
            av_obj_add_str(doc, record, "type", n->desc->name);
            const char *ref = reference(asset, n);
            if (*ref) {
                av_obj_add_str(doc, record, "reference", ref);
            }
            av_obj_add_float(doc, record, "x", n->x);
            av_obj_add_float(doc, record, "y", n->y);
            if (n->placed) {
                av_obj_add_bool(doc, record, "placed", true);
            }
            if (n->presentation) {
                av_obj_add_uint(doc, record, "presentation", n->presentation);
            }
            if (n->breakpoint != 0) {
                av_obj_add_bool(doc, record, "breakpoint", true);
            }
            if (n->breakpoint_enabled != 0) {
                av_obj_add_bool(doc, record, "breakpoint_enabled", true);
            }
            if (n->show_advanced) {
                av_obj_add_bool(doc, record, "show_advanced", true);
            }
            if (n->comment) {
                av_obj_add_str(doc, record, "comment", n->comment);
            }
            if (n->comment_hidden) {
                av_obj_add_bool(doc, record, "comment_hidden", true);
            }
            if (n->input_count) {
                av_obj_add_val(doc, record, "inputs", write_pins(doc, n->input_pins, n->input_count));
            }
            if (n->output_count) {
                av_obj_add_val(doc, record, "outputs", write_pins(doc, n->output_pins, n->output_count));
            }
        }
        afree(ordered);
        av_value *edges = av_obj_add_arr(doc, item, "edges");
        for (int j = 0; j < acount(g->edges); j++) {
            const edge *e = &g->edges[j];
            if (!e->reroute_count && !e->custom_handles) {
                continue;
            }
            av_value *record = av_arr_add_obj(doc, edges);
            av_obj_add_uint(doc, record, "from", e->from_id);
            av_obj_add_uint(doc, record, "from_pin", e->from_pin_id);
            av_obj_add_uint(doc, record, "to", e->to_id);
            av_obj_add_uint(doc, record, "to_pin", e->to_pin_id);
            av_obj_add_uint(doc, record, "kind", e->kind);
            av_obj_add_bool(doc, record, "custom_handles", e->custom_handles);
            av_obj_add_float(doc, record, "from_x", e->from_handle_x);
            av_obj_add_float(doc, record, "from_y", e->from_handle_y);
            av_obj_add_float(doc, record, "to_x", e->to_handle_x);
            av_obj_add_float(doc, record, "to_y", e->to_handle_y);
            av_value *points = av_obj_add_arr(doc, record, "reroutes");
            for (int k = 0; k < e->reroute_count; k++) {
                av_value *point = av_arr_add_obj(doc, points);
                av_obj_add_float(doc, point, "x", e->reroutes[k].x);
                av_obj_add_float(doc, point, "y", e->reroutes[k].y);
            }
        }
        av_value *boxes = av_obj_add_arr(doc, item, "comments");
        for (int j = 0; j < acount(g->comment_boxes); j++) {
            const graph_comment_box *box = &g->comment_boxes[j];
            av_value *record = av_arr_add_obj(doc, boxes);
            av_obj_add_uint(doc, record, "id", box->id);
            av_obj_add_str(doc, record, "title", box->title ? box->title : "");
            av_obj_add_uint(doc, record, "color", box->color);
            av_obj_add_float(doc, record, "x", box->x);
            av_obj_add_float(doc, record, "y", box->y);
            av_obj_add_float(doc, record, "width", box->width);
            av_obj_add_float(doc, record, "height", box->height);
        }
    }
    char *result = av_json_write(root);
    av_doc_free(doc);
    return result;
}
static av_value *pin_record(const av_value *items, const char *name)
{
    av_value *found = NULL;
    for (size_t i = 0; i < av_arr_size(items); i++) {
        av_value *item = av_arr_get(items, i);
        const char *saved = av_get_str(av_obj_get(item, "name"));
        if (!saved || strcmp(saved, name)) {
            continue;
        }
        if (found) {
            return NULL;
        }
        found = item;
    }
    return found;
}
static av_value *instance_pin_record(const av_value *items, const pin_spec *pins, int count, int index)
{
    const pin_spec *pin = &pins[index];
    const char **path = pin_parent_path(pins, count, index);
    av_value *found = NULL;
    for (size_t i = 0; acount(path) && i < av_arr_size(items); i++) {
        av_value *item = av_arr_get(items, i);
        const char *name = av_get_str(av_obj_get(item, "name"));
        const av_value *parents = av_obj_get(item, "parents");
        if (!name || strcmp(name, pin->name) ||
            av_arr_size(parents) != (size_t)acount(path)) {
            continue;
        }
        bool same = true;
        for (int p = 0; same && p < acount(path); p++) {
            const char *saved = av_get_str(av_arr_get(parents, (size_t)p));
            same = saved && !strcmp(saved, path[acount(path) - p - 1]);
        }
        if (!same) {
            continue;
        }
        if (found) {
            afree(path);
            return NULL;
        }
        found = item;
    }
    afree(path);
    if (found) {
        return found;
    }
    av_value *unowned = pin_record(items, pin->name);
    return av_obj_get(unowned, "parents") ? NULL : unowned;
}
/* Unused reflected fields are graph presentation. Their schema comes solely
 * from the existing source aggregate and registered reflection. */
static int projection_parent(const node *n, const av_value *parents)
{
    if (!av_is_arr(parents) || !av_arr_size(parents)) {
        return -1;
    }
    int found = -1;
    for (int i = 0; i < n->output_count; i++) {
        const char **path = pin_parent_path(n->output_pins, n->output_count, i);
        bool same = av_arr_size(parents) == (size_t)acount(path) + 1;
        for (int j = 0; same && j < acount(path); j++) {
            const char *name = av_get_str(av_arr_get(parents, (size_t)j));
            same = name && !strcmp(name, path[acount(path) - j - 1]);
        }
        const char *last = av_get_str(av_arr_get(parents, av_arr_size(parents) - 1));
        same &= last && !strcmp(last, n->output_pins[i].name);
        afree(path);
        if (!same) {
            continue;
        }
        if (found >= 0) {
            return -1;
        }
        found = i;
    }
    return found;
}
static bool restore_output_fields(graph *g, node *n, const av_value *records)
{
    for (size_t i = 0; i < av_arr_size(records); i++) {
        const av_value *record = av_arr_get(records, i);
        const av_value *hint = av_obj_get(record, "field");
        if (!hint) {
            continue;
        }
        const char *field = av_get_str(hint);
        int parent = projection_parent(n, av_obj_get(record, "parents"));
        if (!field || parent < 0 || graph_expose_output_field(g, n, parent, field) < 0) {
            return false;
        }
    }
    return true;
}
static bool map_pins(identity_map **map, const pin_spec *pins, int count, const av_value *items)
{
    for (int i = 0; i < count; i++) {
        av_value *item = instance_pin_record(items, pins, count, i);
        if (item &&
            !add_identity(map, pins[i].id, av_get_uint(av_obj_get(item, "id")))) {
            return false;
        }
    }
    return true;
}
static uint64_t anchored(const av_value *anchors, int start, const char *kind);

static uint64_t statement_identity(const code_source *source, scope *s, statement *body, const node *n)
{
    for (int i = 0; i < acount(body); i++) {
        statement *item = &body[i];
        const node *statement_node = code_program_statement_node(s, item);
        if (statement_node && statement_node->id == n->id) {
            uint64_t saved = anchored(source->metadata_anchors, item->start, "node");
            return saved;
        }
        if (statement_node && item->native && statement_node != item->native &&
            item->native->id == n->id) {
            uint64_t saved = anchored(source->metadata_anchors, item->start, "statement");
            if (saved) {
                return saved;
            }
        }
        for (int j = 0; j < acount(item->arguments); j++) {
            const argument *a = &item->arguments[j];
            if (a->has_resolved && a->resolved.kind == CODE_CALL_OUTPUT &&
                a->resolved.source.node && a->resolved.source.node->id == n->id) {
                uint64_t saved = anchored(source->metadata_anchors, a->start, "producer");
                if (saved) {
                    return saved;
                }
            }
        }
        uint64_t id = statement_identity(source, s, item->body, n);
        if (!id) {
            id = statement_identity(source, s, item->alternate, n);
        }
        if (id) {
            return id;
        }
    }
    return 0;
}

static uint64_t authored_id(const code_source *source, const graph *g, const node *n)
{
    if (!source->program) {
        return n->id;
    }
    uint64_t previous = code_source_previous_identity(source, n->id);
    const code_program *program = source->program;
    for (int i = 0; i < acount(program->scopes); i++) {
        scope *s = program->scopes[i];
        if (s->native->graph_id != g->graph_id) {
            continue;
        }
        for (int j = 0; j < acount(source->asset->reactives); j++) {
            const behavior_reactive *r = &source->asset->reactives[j];
            if (r->graph_id != g->graph_id) {
                continue;
            }
            behavior_reactive_anchor *anchors = behavior_reactive_anchors(g, r);
            const char *kind = NULL;
            for (int k = 0; k < acount(anchors); k++) {
                if (anchors[k].id == n->id) {
                    kind = anchors[k].kind;
                    break;
                }
            }
            uint64_t saved = 0;
            for (int k = 0; kind && k < acount(s->declarations); k++) {
                const declaration *d = s->declarations[k];
                if (d->native && d->native->id == r->event_id) {
                    saved = anchored(source->metadata_anchors, d->start, kind);
                    break;
                }
            }
            bool derived = kind && r->kind == BEHAVIOR_REACTIVE_BIND;
            afree(anchors);
            if (saved || derived) {
                return saved ? saved : previous;
            }
        }
        for (int j = 0; j < acount(s->declarations); j++) {
            const declaration *d = s->declarations[j];
            if (d->native && d->native->id == n->id && d->start < d->end) {
                uint64_t saved = anchored(source->metadata_anchors, d->start, "node");
                return saved ? saved : previous;
            }
        }
        for (int j = 0; j < acount(s->continuations); j++) {
            const continuation *c = &s->continuations[j];
            for (int k = 0; k < acount(c->aliases); k++) {
                const declaration *d = c->aliases[k];
                if (d->native && d->native->id == n->id && d->start < d->end) {
                    uint64_t saved = anchored(source->metadata_anchors, d->start, "node");
                    return saved ? saved : previous;
                }
            }
            uint64_t marker = statement_identity(source, s, s->continuations[j].body, n);
            if (marker) {
                return marker;
            }
        }
    }
    return previous;
}
static uint64_t value_identity(const code_source *source, uint64_t id)
{
    const code_program *program = source->program;
    uint64_t previous = code_source_previous_identity(source, id);
    if (!program) {
        return previous;
    }
    for (int i = 0; i < acount(program->states); i++) {
        const state_declaration *state = &program->states[i];
        if (state->native_id == id) {
            uint64_t saved = anchored(source->metadata_anchors, state->start, "value");
            return saved ? saved : previous;
        }
    }
    for (int i = 0; i < acount(program->scopes); i++) {
        const scope *s = program->scopes[i];
        for (int j = 0; j < acount(s->locals); j++) {
            const local_declaration *local = &s->locals[j];
            if (local->id == id) {
                uint64_t saved = anchored(source->metadata_anchors, local->start, "value");
                return saved ? saved : previous;
            }
        }
    }
    return previous;
}
static av_value *value_record(const code_source *source, uint64_t id, const char *name, const av_value *records)
{
    uint64_t authored = value_identity(source, id);
    for (size_t i = 0; authored && i < av_arr_size(records); i++) {
        av_value *record = av_arr_get(records, i);
        if (av_get_uint(av_obj_get(record, "id")) == authored) {
            return record;
        }
    }
    return pin_record(records, name);
}
static av_value *graph_record(
    const code_source *source,
    const graph *g,
    const av_value *records,
    const identity_map *map
)
{
    const behavior_asset *asset = source->asset;
    uint64_t authored = code_source_previous_identity(source, g->graph_id);
    const code_program *program = source->program;
    for (int i = 0; program && i < acount(program->scopes); i++) {
        const scope *s = program->scopes[i];
        if (s->native->graph_id == g->graph_id && g != asset->graph) {
            uint64_t saved = anchored(source->metadata_anchors, s->start, "graph");
            if (saved) {
                authored = saved;
            }
            break;
        }
    }
    uint64_t id = caller(asset, g);
    bool matched = false;
    for (int i = 0; i < acount(map); i++) {
        if (map[i].from == id) {
            id = map[i].to;
            matched = true;
            break;
        }
    }
    for (int i = 0; id && !matched && i < acount(asset->graphs); i++) {
        const node *call = graph_node_by_id(asset->graphs[i], id);
        if (call) {
            id = authored_id(source, asset->graphs[i], call);
            break;
        }
    }
    for (size_t i = 0; i < av_arr_size(records); i++) {
        av_value *item = av_arr_get(records, i);
        if ((g == asset->graph && !av_get_uint(av_obj_get(item, "id"))) ||
            (!program && g != asset->graph &&
             av_get_uint(av_obj_get(item, "id")) == g->graph_id &&
             av_get_uint(av_obj_get(item, "kind")) == (uint64_t)g->kind) ||
            (authored && av_get_uint(av_obj_get(item, "id")) == authored &&
             av_get_uint(av_obj_get(item, "kind")) == (uint64_t)g->kind)) {
            return item;
        }
    }
    for (size_t i = 0; id && i < av_arr_size(records); i++) {
        av_value *item = av_arr_get(records, i);
        if (av_get_uint(av_obj_get(item, "kind")) == (uint64_t)g->kind &&
            av_get_uint(av_obj_get(item, "caller")) == id) {
            return item;
        }
    }
    av_value *found = NULL;
    for (size_t i = 0; i < av_arr_size(records); i++) {
        av_value *item = av_arr_get(records, i);
        const char *name = av_get_str(av_obj_get(item, "name"));
        if (av_get_uint(av_obj_get(item, "kind")) != (uint64_t)g->kind) {
            continue;
        }
        if (g != asset->graph && g->name && *g->name && name && !strcmp(g->name, name)) {
            if (found) {
                return NULL;
            }
            found = item;
        }
    }
    return found;
}
/* A shared generated formula is printed as an ordinary pure function. Its
 * caller remains the same source declaration, although its native descriptor
 * no longer carries formula-editing provenance. Only presentation identity is
 * transferable; the footer never changes the parsed descriptor or body. */
static bool node_type_matches(const behavior_asset *asset, const node *n, const av_value *record)
{
    const char *type = av_get_str(av_obj_get(record, "type"));
    if (!type) {
        return false;
    }
    if (!strcmp(type, n->desc->name)) {
        return true;
    }
    const char *ref = av_get_str(av_obj_get(record, "reference"));
    return !strcmp(type, NODE_BehaviorExpression.name) &&
           n->desc == &NODE_CallPureFunction && !n->reference_asset &&
           behavior_asset_find_graph(asset, n->reference_id) && ref &&
           !strcmp(ref, reference(asset, n));
}
static av_value *node_record(
    const code_source *source,
    const graph *g,
    const node *n,
    int index,
    const av_value *records,
    const identity_map *map,
    bool canonical
)
{
    const behavior_asset *asset = source->asset;
    if (g->math_expression) {
        const node **ordered = presentation_nodes(g, canonical);
        av_value *record = NULL;
        for (int i = 0; i < acount(ordered); i++) {
            if (ordered[i] == n) {
                record = av_arr_get(records, (size_t)i);
                break;
            }
        }
        afree(ordered);
        return record;
    }
    uint64_t id = owner(g, n, 0);
    const node *owner_node = graph_node_by_id(g, id);
    if (owner_node) {
        id = authored_id(source, g, owner_node);
    }
    uint64_t authored = authored_id(source, g, n);
    for (size_t i = 0; source->program && authored && i < av_arr_size(records); i++) {
        av_value *item = av_arr_get(records, i);
        if (av_get_uint(av_obj_get(item, "id")) == authored &&
            node_type_matches(asset, n, item)) {
            return item;
        }
    }
    av_value *found = NULL;
    for (size_t i = 0; i < av_arr_size(records); i++) {
        av_value *item = av_arr_get(records, i);
        uint64_t saved_id = av_get_uint(av_obj_get(item, "id"));
        bool claimed = false;
        for (int j = 0; j < acount(map); j++) {
            claimed |= map[j].to == saved_id && map[j].from != n->id;
        }
        if (claimed) {
            continue;
        }
        const char *ref = av_get_str(av_obj_get(item, "reference"));
        if (!ref) {
            ref = "";
        }
        uint64_t saved_owner = av_get_uint(av_obj_get(item, "owner"));
        if ((id && saved_owner && saved_owner != id) ||
            !node_type_matches(asset, n, item) || !ref ||
            strcmp(ref, reference(asset, n))) {
            continue;
        }
        if (found) {
            return NULL;
        }
        found = item;
    }
    return found;
}
static void apply_pins(
    const behavior_asset *asset,
    graph *g,
    node *n,
    bool output,
    const av_value *records,
    identity_map *map
)
{
    pin_spec *pins = output ? n->output_pins : n->input_pins;
    int count = output ? n->output_count : n->input_count;
    bool local_declarations = !n->reference_asset &&
                              (n->desc == &NODE_CallFunction ||
                               n->desc == &NODE_CallPureFunction ||
                               n->desc == &NODE_CallMacro ||
                               n->desc == &NODE_BehaviorExpression ||
                               n->desc == &NODE_FunctionEntry ||
                               n->desc == &NODE_Return);
    av_value **matched = NULL;
    for (int i = 0; i < count; i++) {
        apush(matched, instance_pin_record(records, pins, count, i));
    }
    for (int i = 0; i < count; i++) {
        pin_spec *p = &pins[i];
        av_value *record = matched[i];
        p->id = mapped(map, p->id);
        if (local_declarations) {
            p->declaration_id = mapped(map, p->declaration_id);
        }
        p->split_parent = mapped(map, p->split_parent);
        p->field_source = mapped(map, p->field_source);
        if (record && av_get_bool(av_obj_get(record, "watch"))) {
            p->flags |= PIN_FLAG_WATCH;
        } else if (record) {
            p->flags &= ~PIN_FLAG_WATCH;
        }
        const char *display = av_get_str(av_obj_get(record, "display"));
        if (record) {
            p->display_name = display ? graph_own_str(g, display, strlen(display))
                                      : NULL;
        }
        if (!p->binding) {
            continue;
        }
        pin_binding *b = p->binding;
        if (!b->source_asset || !*b->source_asset ||
            !strcmp(b->source_asset, asset->name)) {
            b->variable_id = mapped(map, b->variable_id);
        }
        b->local_id = mapped(map, b->local_id);
        b->event_node_id = mapped(map, b->event_node_id);
        b->event_pin_id = mapped(map, b->event_pin_id);
    }
    afree(matched);
}
static void restore_projected_order(code_source *source, node *n, const av_value *records, bool remap_source)
{
    int count = n->output_count, projected = 0;
    for (int i = 0; i < count; i++) {
        projected += n->output_pins[i].field_source != 0;
    }
    if (projected < 2) {
        return;
    }
    pin_spec *ordered = xmalloc((size_t)count * sizeof *ordered);
    memcpy(ordered, n->output_pins, (size_t)count * sizeof *ordered);
    int *indices = xmalloc((size_t)count * sizeof *indices);
    for (int i = 0; i < count; i++) {
        indices[i] = i;
    }
    int *sequence = NULL;
    for (size_t i = 0; i < av_arr_size(records); i++) {
        uint64_t id = av_get_uint(av_obj_get(av_arr_get(records, i), "id"));
        for (int j = 0; j < count; j++) {
            if (n->output_pins[j].id == id && n->output_pins[j].field_source) {
                apush(sequence, j);
                break;
            }
        }
    }
    for (int i = 0; i < count; i++) {
        if (!n->output_pins[i].field_source) {
            continue;
        }
        bool recorded = false;
        for (int j = 0; j < acount(sequence); j++) {
            recorded |= sequence[j] == i;
        }
        if (!recorded) {
            apush(sequence, i);
        }
    }
    int next = 0;
    for (int i = 0; i < count; i++) {
        if (!n->output_pins[i].field_source) {
            continue;
        }
        int old = sequence[next++];
        ordered[i] = n->output_pins[old];
        indices[old] = i;
    }
    afree(sequence);
    memcpy(n->output_pins, ordered, (size_t)count * sizeof *ordered);
    if (remap_source && source->program) {
        code_program_reorder_outputs(source->program, n, indices, count);
    }
    free(indices);
    free(ordered);
}

static const char *metadata_begin(const char *text)
{
    for (int i = 0; text[i];) {
        if (text[i] == '"') {
            i++;
            while (text[i] && text[i] != '"') {
                if (text[i] == '\\' && text[i + 1]) {
                    i++;
                }
                i++;
            }
            if (text[i]) {
                i++;
            }
            continue;
        }
        if (text[i] == '/' && text[i + 1] == '/') {
            while (text[i] && text[i] != '\n') {
                i++;
            }
            continue;
        }
        if (text[i] == '/' && text[i + 1] == '*') {
            if (!strncmp(text + i, METADATA_PREFIX, strlen(METADATA_PREFIX))) {
                return text + i + strlen(METADATA_PREFIX);
            }
            i += 2;
            int depth = 1;
            while (text[i] && depth) {
                if (text[i] == '/' && text[i + 1] == '*') {
                    depth++;
                    i += 2;
                } else if (text[i] == '*' && text[i + 1] == '/') {
                    depth--;
                    i += 2;
                } else {
                    i++;
                }
            }
            continue;
        }
        i++;
    }
    return NULL;
}
bool code_source_metadata_range(const char *text, int *start, int *finish)
{
    const char *begin = metadata_begin(text);
    if (!begin) {
        return false;
    }
    const char *end = strstr(begin, "*/");
    if (!end) {
        return false;
    }
    *start = (int)(begin - text - strlen(METADATA_PREFIX));
    *finish = (int)(end - text + 2);
    return true;
}
static bool valid_ids(const av_value *value, uint64_t limit, uint64_t **seen)
{
    if (av_is_arr(value)) {
        for (size_t i = 0; i < av_arr_size(value); i++) {
            if (!valid_ids(av_arr_get(value, i), limit, seen)) {
                return false;
            }
        }
    }
    if (!av_is_obj(value)) {
        return true;
    }
    av_value *presentation = av_obj_get(value, "presentation");
    if (presentation && (!av_is_uint(presentation) ||
                         av_get_uint(presentation) > NODE_PRESENTATION_EXPANDED)) {
        return false;
    }
    av_value *identity = av_obj_get(value, "id");
    if (identity) {
        if (!av_is_uint(identity) || av_get_uint(identity) >= limit) {
            return false;
        }
        uint64_t id = av_get_uint(identity);
        if (!id && (!av_obj_get(value, "caller") ||
                    av_get_uint(av_obj_get(value, "caller")))) {
            return false;
        }
        {
            for (int i = 0; i < acount(*seen); i++) {
                if ((*seen)[i] == id) {
                    return false;
                }
            }
            apush(*seen, id);
        }
    }
    av_value *reroutes = av_obj_get(value, "reroutes");
    if (reroutes && av_arr_size(reroutes) > GRAPH_EDGE_MAX_REROUTES) {
        return false;
    }
    for (size_t i = 0; i < av_obj_size(value); i++) {
        if (!valid_ids(av_obj_value_at(value, i), limit, seen)) {
            return false;
        }
    }
    return true;
}
static bool same_body(const graph *g, const av_value *nodes, bool canonical)
{
    if (av_arr_size(nodes) != (size_t)acount(g->nodes)) {
        return false;
    }
    const node **ordered = presentation_nodes(g, canonical);
    bool same = true;
    for (int i = 0; same && i < acount(ordered); i++) {
        av_value *record = av_arr_get(nodes, (size_t)i);
        const char *type = av_get_str(av_obj_get(record, "type"));
        if (!type || strcmp(type, ordered[i]->desc->name)) {
            same = false;
            break;
        }
        const node *n = ordered[i];
        for (int output = 0; output < 2; output++) {
            const pin_spec *pins = output ? n->output_pins : n->input_pins;
            int count = output ? n->output_count : n->input_count;
            av_value *items = av_obj_get(record, output ? "outputs" : "inputs");
            if (av_arr_size(items) != (size_t)count) {
                same = false;
                break;
            }
            for (int j = 0; j < count; j++) {
                if (!instance_pin_record(items, pins, count, j)) {
                    same = false;
                    break;
                }
            }
        }
    }
    afree(ordered);
    return same;
}
static bool final_identity(uint64_t **seen, identity_map *map, uint64_t id)
{
    id = mapped(map, id);
    if (!id) {
        return true;
    }
    if (id > METADATA_ID_LIMIT) {
        return false;
    }
    for (int i = 0; i < acount(*seen); i++) {
        if ((*seen)[i] == id) {
            return false;
        }
    }
    apush(*seen, id);
    return true;
}
static bool final_identities(const code_source *source, identity_map *map, const av_value *graphs)
{
    const behavior_asset *asset = source->asset;
    uint64_t *seen = NULL;
    bool valid = true;
    for (int i = 0; valid && i < acount(asset->variables); i++) {
        valid = final_identity(&seen, map, asset->variables[i].id);
    }
    afree(seen);
    for (int i = 0; valid && i < acount(asset->graphs); i++) {
        const graph *g = asset->graphs[i];
        valid = final_identity(&seen, map, g->graph_id);
        for (int j = 0; valid && j < acount(g->nodes); j++) {
            const node *n = g->nodes[j];
            valid = final_identity(&seen, map, n->id);
            for (int k = 0; valid && k < n->input_count; k++) {
                valid = final_identity(&seen, map, n->input_pins[k].id);
            }
            for (int k = 0; valid && k < n->output_count; k++) {
                valid = final_identity(&seen, map, n->output_pins[k].id);
            }
        }
        for (int j = 0; valid && j < acount(g->locals); j++) {
            valid = final_identity(&seen, map, g->locals[j].id);
        }
        av_value *record = graph_record(source, g, graphs, map);
        av_value *boxes = av_obj_get(record, "comments");
        for (size_t j = 0; valid && j < av_arr_size(boxes); j++) {
            uint64_t id = av_get_uint(av_obj_get(av_arr_get(boxes, j), "id"));
            /* Comment-box identities are already persisted targets. */
            for (int k = 0; valid && k < acount(seen); k++) {
                valid = seen[k] != id;
            }
            if (valid) {
                apush(seen, id);
            }
        }
    }
    afree(seen);
    return valid;
}
static bool apply_metadata(code_source *source, const av_value *root, bool remap_source)
{
    behavior_asset *asset = source->asset;
    identity_map *map = NULL;
    av_value **records = NULL;
    bool valid = av_get_uint(av_obj_get(root, "version")) == 2 &&
                 av_get_uint(av_obj_get(root, "id_counter")) > 0 &&
                 av_get_uint(av_obj_get(root, "id_counter")) <= METADATA_ID_LIMIT;
    av_value *variables = av_obj_get(root, "variables"),
             *graphs = av_obj_get(root, "graphs");
    valid &= av_is_arr(variables) && av_is_arr(graphs);
    uint64_t *seen = NULL;
    valid &= valid_ids(variables, av_get_uint(av_obj_get(root, "id_counter")), &seen);
    afree(seen);
    valid &= valid_ids(graphs, av_get_uint(av_obj_get(root, "id_counter")), &seen);
    afree(seen);
    for (int i = 0; valid && i < acount(asset->variables); i++) {
        behavior_variable *v = &asset->variables[i];
        av_value *record = value_record(source, v->id, v->name, variables);
        valid = !record ||
                add_identity_role(&map, v->id, av_get_uint(av_obj_get(record, "id")), true);
    }
    for (int i = 0; valid && i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        av_value *record = graph_record(source, g, graphs, map);
        av_value *nodes = av_obj_get(record, "nodes");
        const char *order = av_get_str(av_obj_get(record, "node_order"));
        bool canonical = order && !strcmp(order, "inputs");
        valid = !record ||
                add_identity(&map, g->graph_id, av_get_uint(av_obj_get(record, "id")));
        for (int j = 0; valid && j < acount(g->locals); j++) {
            const graph_local *local = &g->locals[j];
            av_value *saved = value_record(source, local->id, local->name, av_obj_get(record, "locals"));
            valid = !saved ||
                    add_identity(&map, local->id, av_get_uint(av_obj_get(saved, "id")));
        }
        for (int j = 0; valid && j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            av_value *saved = node_record(source, g, n, j, nodes, map, canonical);
            valid = !saved ||
                    restore_output_fields(g, n, av_obj_get(saved, "outputs"));
        }
        bool body = !g->math_expression || same_body(g, nodes, canonical);
        apush(records, record);
        for (int j = 0; valid && j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            av_value *saved = body ? node_record(source, g, n, j, nodes, map, canonical)
                                   : NULL;
            valid = !saved ||
                    (node_type_matches(asset, n, saved) &&
                     add_identity(&map, n->id, av_get_uint(av_obj_get(saved, "id"))) &&
                     map_pins(&map, n->input_pins, n->input_count, av_obj_get(saved, "inputs")) &&
                     map_pins(&map, n->output_pins, n->output_count, av_obj_get(saved, "outputs")));
            apush(records, saved);
        }
    }
    uint64_t limit = av_get_uint(av_obj_get(root, "id_counter"));
    for (int i = 0; valid && i < acount(map); i++) {
        valid = map[i].to < limit;
    }
    for (size_t i = 0; valid && i < av_arr_size(graphs); i++) {
        av_value *boxes = av_obj_get(av_arr_get(graphs, i), "comments");
        for (size_t j = 0; valid && j < av_arr_size(boxes); j++) {
            uint64_t id = av_get_uint(av_obj_get(av_arr_get(boxes, j), "id"));
            valid = id && id < limit;
            for (int k = 0; valid && k < acount(map); k++) {
                valid = map[k].variable || map[k].to != id;
            }
        }
    }
    if (valid) {
        valid = final_identities(source, map, graphs);
    }
    /* Stale/ambiguous metadata must never be partially installed. */
    if (!valid) {
        int start = 0, finish = (int)strlen(source->text);
        code_source_metadata_range(source->text, &start, &finish);
        code_source_error(source, start, finish, "Invalid authoring metadata: identities must be unique and bounded; presentation values and pin records must be valid");
        afree(records);
        afree(map);
        return false;
    }
    for (int i = 0; i < acount(asset->reactives); i++) {
        behavior_reactive *r = &asset->reactives[i];
        r->graph_id = mapped(map, r->graph_id);
        r->event_id = mapped(map, r->event_id);
        r->source_node_id = mapped(map, r->source_node_id);
        r->source_pin_id = mapped(map, r->source_pin_id);
        r->statement_node_id = mapped(map, r->statement_node_id);
        r->value_node_id = mapped(map, r->value_node_id);
        r->value_pin_id = mapped(map, r->value_pin_id);
    }
    int at = 0;
    for (int i = 0; i < acount(asset->graphs); i++) {
        graph *g = asset->graphs[i];
        av_value *record = records[at++];
        g->graph_id = mapped(map, g->graph_id);
        g->entry_id = mapped(map, g->entry_id);
        g->return_id = mapped(map, g->return_id);
        if (record) {
            g->breakpoints_enabled = !av_obj_get(record, "breakpoints_enabled") ||
                                     av_get_bool(av_obj_get(record, "breakpoints_enabled"));
        }
        const char *label = av_get_str(av_obj_get(record, "name"));
        if ((g == asset->graph || g->math_expression) && label) {
            g->name = graph_own_str(g, label, strlen(label));
        }
        const char *description = av_get_str(av_obj_get(record, "description"));
        if (description) {
            g->description = graph_own_str(g, description, strlen(description));
        }
        for (int j = 0; j < acount(g->nodes); j++) {
            node *n = g->nodes[j];
            av_value *saved = records[at++];
            n->id = mapped(map, n->id);
            if (!n->reference_asset) {
                n->reference_id = mapped(map, n->reference_id);
            }
            if (!saved) {
                apply_pins(asset, g, n, false, NULL, map);
                apply_pins(asset, g, n, true, NULL, map);
                continue;
            }
            n->x = (float)av_get_num(av_obj_get(saved, "x"));
            n->y = (float)av_get_num(av_obj_get(saved, "y"));
            n->placed = av_get_bool(av_obj_get(saved, "placed"));
            n->presentation = (node_presentation)av_get_uint(av_obj_get(saved, "presentation"));
            n->breakpoint = av_get_bool(av_obj_get(saved, "breakpoint"));
            n->breakpoint_enabled = av_get_bool(av_obj_get(saved, "breakpoint_enabled"));
            n->show_advanced = av_get_bool(av_obj_get(saved, "show_advanced"));
            n->comment_hidden = av_get_bool(av_obj_get(saved, "comment_hidden"));
            const char *comment = av_get_str(av_obj_get(saved, "comment"));
            if (comment) {
                n->comment = graph_own_str(g, comment, strlen(comment));
            }
            apply_pins(asset, g, n, false, av_obj_get(saved, "inputs"), map);
            apply_pins(asset, g, n, true, av_obj_get(saved, "outputs"), map);
            restore_projected_order(source, n, av_obj_get(saved, "outputs"), remap_source);
        }
        for (int j = 0; j < acount(g->edges); j++) {
            edge *e = &g->edges[j];
            e->from_id = mapped(map, e->from_id);
            e->to_id = mapped(map, e->to_id);
            e->from_pin_id = mapped(map, e->from_pin_id);
            e->to_pin_id = mapped(map, e->to_pin_id);
        }
        for (int j = 0; j < acount(g->locals); j++) {
            graph_local *local = &g->locals[j];
            local->id = mapped(map, local->id);
            local->source_id = mapped(map, local->source_id);
            local->source_pin_id = mapped(map, local->source_pin_id);
        }
        av_value *saved_edges = av_obj_get(record, "edges");
        for (size_t j = 0; j < av_arr_size(saved_edges); j++) {
            av_value *saved = av_arr_get(saved_edges, j);
            for (int k = 0; k < acount(g->edges); k++) {
                edge *e = &g->edges[k];
                if (e->from_id != av_get_uint(av_obj_get(saved, "from")) ||
                    e->from_pin_id != av_get_uint(av_obj_get(saved, "from_pin")) ||
                    e->to_id != av_get_uint(av_obj_get(saved, "to")) ||
                    e->to_pin_id != av_get_uint(av_obj_get(saved, "to_pin")) ||
                    e->kind != (graph_edge_kind)av_get_uint(av_obj_get(saved, "kind"))) {
                    continue;
                }
                e->custom_handles = av_get_bool(av_obj_get(saved, "custom_handles"));
                e->from_handle_x = (float)av_get_num(av_obj_get(saved, "from_x"));
                e->from_handle_y = (float)av_get_num(av_obj_get(saved, "from_y"));
                e->to_handle_x = (float)av_get_num(av_obj_get(saved, "to_x"));
                e->to_handle_y = (float)av_get_num(av_obj_get(saved, "to_y"));
                av_value *points = av_obj_get(saved, "reroutes");
                e->reroute_count = (int)av_arr_size(points);
                for (int p = 0; p < e->reroute_count; p++) {
                    av_value *point = av_arr_get(points, (size_t)p);
                    e->reroutes[p].x = (float)av_get_num(av_obj_get(point, "x"));
                    e->reroutes[p].y = (float)av_get_num(av_obj_get(point, "y"));
                }
            }
        }
        av_value *boxes = av_obj_get(record, "comments");
        for (size_t j = 0; j < av_arr_size(boxes); j++) {
            av_value *saved = av_arr_get(boxes, j);
            const char *title = av_get_str(av_obj_get(saved, "title"));
            graph_comment_box *box = graph_add_comment_box(
                g,
                title ? title : "",
                (float)av_get_num(av_obj_get(saved, "x")),
                (float)av_get_num(av_obj_get(saved, "y")),
                (float)av_get_num(av_obj_get(saved, "width")),
                (float)av_get_num(av_obj_get(saved, "height"))
            );
            box->id = av_get_uint(av_obj_get(saved, "id"));
            box->color = (uint32_t)av_get_uint(av_obj_get(saved, "color"));
        }
        graph_reindex(g);
    }
    for (int i = 0; i < acount(asset->variables); i++) {
        behavior_variable *v = &asset->variables[i];
        v->id = mapped(map, v->id);
        for (size_t j = 0; j < av_arr_size(variables); j++) {
            av_value *record = av_arr_get(variables, j);
            if (av_get_uint(av_obj_get(record, "id")) != v->id) {
                continue;
            }
            const char *category = av_get_str(av_obj_get(record, "category"));
            if (category) {
                v->category = graph_own_str(asset->graph, category, strlen(category));
            }
        }
    }
    uint64_t *from = NULL, *to = NULL;
    for (int i = 0; i < acount(map); i++) {
        apush(from, map[i].from);
        apush(to, map[i].to);
    }
    if (remap_source) {
        code_intelligence_remap_native(source, from, to, acount(map));
        if (source->program) {
            code_program_remap(source->program, from, to, acount(map));
        }
    }
    afree(from);
    afree(to);
    uint64_t counter = av_get_uint(av_obj_get(root, "id_counter"));
    for (int i = 0; i < acount(asset->variables); i++) {
        if (asset->variables[i].id >= counter) {
            counter = asset->variables[i].id + 1;
        }
    }
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *g = asset->graphs[i];
        if (g->graph_id >= counter) {
            counter = g->graph_id + 1;
        }
        for (int j = 0; j < acount(g->nodes); j++) {
            const node *n = g->nodes[j];
            if (n->id >= counter) {
                counter = n->id + 1;
            }
            for (int k = 0; k < n->input_count; k++) {
                if (n->input_pins[k].id >= counter) {
                    counter = n->input_pins[k].id + 1;
                }
            }
            for (int k = 0; k < n->output_count; k++) {
                if (n->output_pins[k].id >= counter) {
                    counter = n->output_pins[k].id + 1;
                }
            }
        }
        for (int j = 0; j < acount(g->locals); j++) {
            if (g->locals[j].id >= counter) {
                counter = g->locals[j].id + 1;
            }
        }
        for (int j = 0; j < acount(g->comment_boxes); j++) {
            if (g->comment_boxes[j].id >= counter) {
                counter = g->comment_boxes[j].id + 1;
            }
        }
    }
    asset->graph->id_counter = counter;
    /* Generated function placement is presentation only. Keep authored scope
     * order intact because event dispatch can depend on it. */
    for (size_t i = 1; i < av_arr_size(graphs); i++) {
        uint64_t id = av_get_uint(av_obj_get(av_arr_get(graphs, i), "id"));
        int at = -1;
        for (int j = 1; j < acount(asset->graphs); j++) {
            const graph *g = asset->graphs[j];
            if (g->graph_id == id && g->kind == GRAPH_FUNCTION && g->math_expression) {
                at = j;
                break;
            }
        }
        if (at < 0) {
            continue;
        }
        int target = i < (size_t)acount(asset->graphs) ? (int)i
                                                       : acount(asset->graphs) - 1;
        graph *g = asset->graphs[at];
        if (at > target) {
            memmove(
                asset->graphs + target + 1,
                asset->graphs + target,
                (size_t)(at - target) * sizeof *asset->graphs
            );
        } else if (at < target) {
            memmove(
                asset->graphs + at,
                asset->graphs + at + 1,
                (size_t)(target - at) * sizeof *asset->graphs
            );
        }
        asset->graphs[target] = g;
    }
    const char *description = av_get_str(av_obj_get(root, "description"));
    if (description) {
        behavior_asset_set_description(asset, description);
    }
    afree(records);
    afree(map);
    return true;
}

static int clean_offset(const code_source *source, const char *clean, int offset)
{
    code_lexer old, next;
    code_lex_init(&old, source->text);
    code_lex_init(&next, clean);
    for (;;) {
        code_token a = code_lex_next(&old), b = code_lex_next(&next);
        if (a.kind == CODE_TOKEN_END || a.kind == CODE_TOKEN_ERROR ||
            a.start >= (size_t)offset) {
            return (int)b.start;
        }
    }
}

static void add_anchor(
    av_doc *doc,
    av_value *anchors,
    const code_source *source,
    const char *clean,
    int start,
    uint64_t id,
    const char *kind
)
{
    if (!id) {
        return;
    }
    av_value *item = av_arr_add_obj(doc, anchors);
    av_obj_add_uint(doc, item, "start", (uint64_t)clean_offset(source, clean, start));
    av_obj_add_uint(doc, item, "id", id);
    av_obj_add_str(doc, item, "kind", kind);
}

static void statement_anchors(
    av_doc *doc,
    av_value *anchors,
    const code_source *source,
    const char *clean,
    scope *owner,
    statement *body
)
{
    for (int i = 0; i < acount(body); i++) {
        node *n = code_program_statement_node(owner, &body[i]);
        if (n) {
            add_anchor(doc, anchors, source, clean, body[i].start, n->id, "node");
            if (body[i].native && n != body[i].native) {
                add_anchor(
                    doc,
                    anchors,
                    source,
                    clean,
                    body[i].start,
                    body[i].native->id,
                    "statement"
                );
            }
        }
        for (int j = 0; j < acount(body[i].arguments); j++) {
            const argument *a = &body[i].arguments[j];
            if (a->has_resolved && a->resolved.kind == CODE_CALL_OUTPUT &&
                a->resolved.source.node) {
                add_anchor(
                    doc,
                    anchors,
                    source,
                    clean,
                    a->start,
                    a->resolved.source.node->id,
                    "producer"
                );
            }
        }
        statement_anchors(doc, anchors, source, clean, owner, body[i].body);
        statement_anchors(doc, anchors, source, clean, owner, body[i].alternate);
    }
}

char *code_source_metadata_export_source(const code_source *source)
{
    char *json = code_source_metadata_export(source->asset);
    av_doc *doc = av_json_read(json);
    free(json);
    char *clean = code_source_persist(source);
    av_value *anchors = av_obj_add_arr(doc, av_doc_root(doc), "anchors");
    code_program *program = source->program;
    for (int i = 0; program && i < acount(program->states); i++) {
        const behavior_variable *state = behavior_asset_find_variable_by_name(
            source->asset,
            program->states[i].name
        );
        add_anchor(
            doc,
            anchors,
            source,
            clean,
            program->states[i].start,
            state ? state->id : program->states[i].native_id,
            "value"
        );
    }
    for (int i = 0; program && i < acount(program->scopes); i++) {
        scope *s = program->scopes[i];
        if (s->native != source->asset->graph && s->start < s->end) {
            add_anchor(doc, anchors, source, clean, s->start, s->native->graph_id, "graph");
        }
        for (int j = 0; j < acount(s->declarations); j++) {
            declaration *d = s->declarations[j];
            if (d->native && d->start < d->end) {
                add_anchor(doc, anchors, source, clean, d->start, d->native->id, "node");
            }
        }
        for (int j = 0; j < acount(s->locals); j++) {
            add_anchor(
                doc,
                anchors,
                source,
                clean,
                s->locals[j].start,
                s->locals[j].id,
                "value"
            );
        }
        for (int j = 0; j < acount(s->continuations); j++) {
            const continuation *c = &s->continuations[j];
            for (int k = 0; k < acount(c->aliases); k++) {
                const declaration *d = c->aliases[k];
                if (d->native && d->start < d->end) {
                    add_anchor(doc, anchors, source, clean, d->start, d->native->id, "node");
                }
            }
            bool derived = false;
            for (int k = 0; k < acount(source->asset->reactives); k++) {
                const behavior_reactive *r = &source->asset->reactives[k];
                if (r->graph_id != s->native->graph_id ||
                    r->kind != BEHAVIOR_REACTIVE_BIND) {
                    continue;
                }
                const node *event = graph_node_by_id(s->native, r->event_id);
                if (!event || !event->reference_name ||
                    strcmp(event->reference_name, c->source)) {
                    continue;
                }
                behavior_reactive_anchor *roles = behavior_reactive_anchors(s->native, r);
                for (int n = 0; n < acount(roles); n++) {
                    add_anchor(
                        doc,
                        anchors,
                        source,
                        clean,
                        c->start,
                        roles[n].id,
                        roles[n].kind
                    );
                }
                afree(roles);
                derived = r->kind == BEHAVIOR_REACTIVE_BIND;
                break;
            }
            if (!derived) {
                statement_anchors(doc, anchors, source, clean, s, c->body);
            }
        }
    }
    free(clean);
    json = av_json_write(av_doc_root(doc));
    av_doc_free(doc);
    return json;
}

static uint64_t anchored(const av_value *anchors, int start, const char *kind)
{
    for (size_t i = 0; i < av_arr_size(anchors); i++) {
        const av_value *item = av_arr_get(anchors, i);
        const char *saved_kind = av_get_str(av_obj_get(item, "kind"));
        if (saved_kind && !strcmp(saved_kind, kind) &&
            av_get_uint(av_obj_get(item, "start")) == (uint64_t)start) {
            return av_get_uint(av_obj_get(item, "id"));
        }
    }
    return 0;
}
char *code_source_metadata_reanchor(const char *json, const char *before, const char *after)
{
    av_doc *doc = av_json_read(json);
    if (!av_is_obj(av_doc_root(doc))) {
        av_doc_free(doc);
        return NULL;
    }
    av_obj_remove_key(av_doc_root(doc), "source_hash");
    av_value *anchors = av_obj_get(av_doc_root(doc), "anchors");
    for (size_t i = 0; i < av_arr_size(anchors); i++) {
        av_value *item = av_arr_get(anchors, i);
        uint64_t offset = av_get_uint(av_obj_get(item, "start"));
        code_lexer old, next;
        code_lex_init(&old, before);
        code_lex_init(&next, after);
        for (;;) {
            code_token a = code_lex_next(&old), b = code_lex_next(&next);
            if (a.kind == CODE_TOKEN_ERROR || b.kind == CODE_TOKEN_ERROR ||
                (a.kind == CODE_TOKEN_END) != (b.kind == CODE_TOKEN_END)) {
                av_doc_free(doc);
                return NULL;
            }
            if (a.start >= offset || a.kind == CODE_TOKEN_END) {
                av_obj_set(doc, item, "start", av_uint(doc, b.start));
                break;
            }
        }
    }
    char *result = av_json_write(av_doc_root(doc));
    av_doc_free(doc);
    return result;
}

static uint64_t source_hash(const char *text);

static bool restore_json(code_source *source, const av_value *root)
{
    source->metadata_anchors = av_obj_get(root, "anchors");
    bool valid = apply_metadata(source, root, true);
    source->metadata_anchors = NULL;
    return valid;
}

bool code_source_metadata_restore(code_source *source, const char *json)
{
    if (!source || !source->asset || !json) {
        return false;
    }
    av_doc *doc = av_json_read(json);
    av_value *root = av_doc_root(doc);
    if (!av_is_obj(root)) {
        av_doc_free(doc);
        return false;
    }
    const char *bound_hash = av_get_str(av_obj_get(root, "source_hash"));
    char expected_hash[32];
    snprintf(
        expected_hash,
        sizeof expected_hash,
        "%016llx",
        (unsigned long long)source_hash(source->text)
    );
    if (bound_hash && strcmp(bound_hash, expected_hash)) {
        av_doc_free(doc);
        return false;
    }
    /* Validate the actual model and its proven edit matches. A fresh parse
     * has different allocation IDs and cannot validate reordered statements.
     * The clone may expand aggregate fields, but never remaps the live AST. */
    code_source trial = *source;
    trial.asset = behavior_asset_clone(source->asset);
    trial.diagnostics = NULL;
    trial.metadata_anchors = av_obj_get(root, "anchors");
    bool valid = apply_metadata(&trial, root, false);
    behavior_asset_free(trial.asset);
    for (int i = 0; i < acount(trial.diagnostics); i++) {
        free(trial.diagnostics[i].asset_name);
        free(trial.diagnostics[i].graph_name);
    }
    afree(trial.diagnostics);
    if (valid) {
        valid = restore_json(source, root);
    }
    av_doc_free(doc);
    if (valid && source->document) {
        av_doc *next = av_doc_new();
        av_value *root = behavior_asset_to_value(next, source->asset);
        const av_value *query = av_obj_get(av_doc_root(source->document), "query");
        if (query) {
            av_obj_set(next, root, "query", av_clone(next, query));
        }
        av_doc_set_root(next, root);
        av_doc_free(source->document);
        source->document = next;
    }
    return valid;
}

static uint64_t source_hash(const char *text)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int i = 0; text[i]; i++) {
        hash ^= (unsigned char)text[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static char *sidecar_path(const char *path)
{
    size_t length = strlen(path) + sizeof ".meta";
    char *result = xmalloc(length);
    snprintf(result, length, "%s.meta", path);
    return result;
}

bool code_source_metadata_load(code_source *source, const char *path)
{
    char *file_path = sidecar_path(path);
    char *text = text_file_read(file_path);
    free(file_path);
    if (!text) {
        return false;
    }
    av_doc *doc = av_json_read(text);
    const char *hash = av_get_str(av_obj_get(av_doc_root(doc), "source_hash"));
    char expected[32];
    snprintf(
        expected,
        sizeof expected,
        "%016llx",
        (unsigned long long)source_hash(source->text)
    );
    bool valid = hash && !strcmp(hash, expected) &&
                 code_source_metadata_restore(source, text);
    av_doc_free(doc);
    free(text);
    return valid;
}

bool code_source_metadata_matches(const char *json, const char *text)
{
    av_doc *doc = av_json_read(json);
    const char *bound = av_get_str(av_obj_get(av_doc_root(doc), "source_hash"));
    char expected[32];
    snprintf(expected, sizeof expected, "%016llx", (unsigned long long)source_hash(text));
    bool valid = bound && !strcmp(bound, expected);
    av_doc_free(doc);
    return valid;
}

char *code_source_metadata_bind(const char *json, const char *text)
{
    av_doc *doc = av_json_read(json);
    if (!av_is_obj(av_doc_root(doc))) {
        av_doc_free(doc);
        return NULL;
    }
    char hash[32];
    snprintf(hash, sizeof hash, "%016llx", (unsigned long long)source_hash(text));
    av_obj_set(doc, av_doc_root(doc), "source_hash", av_str(doc, hash));
    char *result = av_json_write(av_doc_root(doc));
    av_doc_free(doc);
    return result;
}

bool code_source_metadata_save(const code_source *source, const char *path)
{
    if (!source || !source->asset) {
        return false;
    }
    char *clean = code_source_persist(source);
    char *json = code_source_metadata_export_source(source);
    char *bound = code_source_metadata_bind(json, clean);
    free(json);
    free(clean);
    json = bound;
    char *target = sidecar_path(path);
    size_t length = strlen(target) + sizeof ".tmp";
    char *temporary = xmalloc(length);
    snprintf(temporary, length, "%s.tmp", target);
#ifdef __EMSCRIPTEN__
    if (web_save_code_metadata(target, json)) {
        free(temporary);
        free(target);
        free(json);
        return false;
    }
#endif
    FILE *file = fopen(temporary, "wb");
    bool valid = file != NULL;
    if (file) {
        valid = fputs(json, file) >= 0;
        valid = fclose(file) == 0 && valid;
    }
    if (valid) {
        valid = rename(temporary, target) == 0;
    }
    if (!valid) {
        remove(temporary);
    }
    free(temporary);
    free(target);
    free(json);
    return valid;
}
