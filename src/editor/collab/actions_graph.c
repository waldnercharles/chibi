#include "assets/ui_events.h"
#include "editor/collab/actions_internal.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/events.h"
#include "graph/model/registry.h"
#include "yyjson.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t next_asset_id(const editor *e)
{
    uint64_t id = e->asset->graph->id_counter;
    return id ? id : 1;
}

static const char *document_key(const editor *e)
{
    return e->document_key && e->document_key[0] ? e->document_key : e->asset->name;
}

static yyjson_mut_doc *action_start(const editor *e, const char *op, uint64_t graph_id)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "version", 1);
    yyjson_mut_obj_add_strcpy(doc, root, "family", "graph");
    yyjson_mut_obj_add_strcpy(doc, root, "op", op);
    yyjson_mut_obj_add_strcpy(doc, root, "document", document_key(e));
    yyjson_mut_obj_add_uint(doc, root, "graph", graph_id);
    return doc;
}

static char *action_finish(yyjson_mut_doc *doc)
{
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

static yyjson_mut_val *action_root(yyjson_mut_doc *doc)
{
    return yyjson_mut_doc_get_root(doc);
}

static bool write_pin_value(yyjson_mut_doc *doc, yyjson_mut_val *root, const pin_value *value)
{
    if (!value) {
        return false;
    }
    yyjson_mut_val *item = yyjson_mut_obj_add_obj(doc, root, "value");
    yyjson_mut_obj_add_int(doc, item, "type", value->type);
    if (value->type_name) {
        yyjson_mut_obj_add_strcpy(doc, item, "type_name", value->type_name);
    }
    switch (value->type) {
        case PIN_BOOL:
            yyjson_mut_obj_add_bool(doc, item, "data", value->b);
            break;
        case PIN_I8:
            yyjson_mut_obj_add_int(doc, item, "data", value->i8);
            break;
        case PIN_U8:
            yyjson_mut_obj_add_uint(doc, item, "data", value->u8);
            break;
        case PIN_I16:
            yyjson_mut_obj_add_int(doc, item, "data", value->i16);
            break;
        case PIN_U16:
            yyjson_mut_obj_add_uint(doc, item, "data", value->u16);
            break;
        case PIN_I32:
            yyjson_mut_obj_add_int(doc, item, "data", value->i);
            break;
        case PIN_U32:
            yyjson_mut_obj_add_uint(doc, item, "data", value->u);
            break;
        case PIN_I64:
            yyjson_mut_obj_add_sint(doc, item, "data", value->i64);
            break;
        case PIN_U64:
            yyjson_mut_obj_add_uint(doc, item, "data", value->u64);
            break;
        case PIN_F32:
            if (!isfinite(value->f)) {
                return false;
            }
            yyjson_mut_obj_add_real(doc, item, "data", value->f);
            break;
        case PIN_F64:
            if (!isfinite(value->d)) {
                return false;
            }
            yyjson_mut_obj_add_real(doc, item, "data", value->d);
            break;
        case PIN_NAME:
        case PIN_STR:
            if (!value->str) {
                return false;
            }
            yyjson_mut_obj_add_strcpy(doc, item, "data", value->str);
            break;
        case PIN_ENUM:
            yyjson_mut_obj_add_int(doc, item, "data", value->e);
            break;
        case PIN_FLOW:
        case PIN_HANDLE:
        case PIN_STRUCT: return false;
    }
    return true;
}

static bool read_pin_value(yyjson_val *item, pin_value *out)
{
    yyjson_val *type = yyjson_obj_get(item, "type");
    yyjson_val *data = yyjson_obj_get(item, "data");
    const char *type_name = yyjson_get_str(yyjson_obj_get(item, "type_name"));
    if (!yyjson_is_obj(item) || !yyjson_is_int(type) || !data) {
        return false;
    }
    pin_value value = { .type = yyjson_get_int(type), .type_name = type_name };
    switch (value.type) {
        case PIN_BOOL:
            if (!yyjson_is_bool(data)) {
                return false;
            }
            value.b = yyjson_get_bool(data);
            break;
        case PIN_I8:
            if (!yyjson_is_int(data) || yyjson_get_sint(data) < INT8_MIN ||
                yyjson_get_sint(data) > INT8_MAX) {
                return false;
            }
            value.i8 = (int8_t)yyjson_get_sint(data);
            break;
        case PIN_U8:
            if (!yyjson_is_uint(data) || yyjson_get_uint(data) > UINT8_MAX) {
                return false;
            }
            value.u8 = (uint8_t)yyjson_get_uint(data);
            break;
        case PIN_I16:
            if (!yyjson_is_int(data) || yyjson_get_sint(data) < INT16_MIN ||
                yyjson_get_sint(data) > INT16_MAX) {
                return false;
            }
            value.i16 = (int16_t)yyjson_get_sint(data);
            break;
        case PIN_U16:
            if (!yyjson_is_uint(data) || yyjson_get_uint(data) > UINT16_MAX) {
                return false;
            }
            value.u16 = (uint16_t)yyjson_get_uint(data);
            break;
        case PIN_I32:
            if (!yyjson_is_int(data) || yyjson_get_sint(data) < INT32_MIN ||
                yyjson_get_sint(data) > INT32_MAX) {
                return false;
            }
            value.i = (int32_t)yyjson_get_sint(data);
            break;
        case PIN_U32:
            if (!yyjson_is_uint(data) || yyjson_get_uint(data) > UINT32_MAX) {
                return false;
            }
            value.u = (uint32_t)yyjson_get_uint(data);
            break;
        case PIN_I64:
            if (!yyjson_is_int(data)) {
                return false;
            }
            value.i64 = yyjson_get_sint(data);
            break;
        case PIN_U64:
            if (!yyjson_is_uint(data)) {
                return false;
            }
            value.u64 = yyjson_get_uint(data);
            break;
        case PIN_F32:
            if (!yyjson_is_num(data) || !isfinite(yyjson_get_real(data))) {
                return false;
            }
            value.f = (float)yyjson_get_real(data);
            if (!isfinite(value.f)) {
                return false;
            }
            break;
        case PIN_F64:
            if (!yyjson_is_num(data) || !isfinite(yyjson_get_real(data))) {
                return false;
            }
            value.d = yyjson_get_real(data);
            break;
        case PIN_NAME:
        case PIN_STR:
            if (!yyjson_is_str(data)) {
                return false;
            }
            value.str = yyjson_get_str(data);
            break;
        case PIN_ENUM:
            if (!yyjson_is_int(data)) {
                return false;
            }
            value.e = (int32_t)yyjson_get_sint(data);
            break;
        case PIN_FLOW:
        case PIN_HANDLE:
        case PIN_STRUCT: return false;
    }
    *out = value;
    return true;
}

char *graph_collab_action_for_command(const editor *e, editor_command command)
{
    if (!e || !e->asset || !e->active_graph) {
        return NULL;
    }
    yyjson_mut_doc *doc = NULL;
    yyjson_mut_val *root = NULL;
    switch (command.kind) {
        case EDITOR_COMMAND_CREATE_GRAPH:
            doc = action_start(e, "create", next_asset_id(e));
            root = action_root(doc);
            yyjson_mut_obj_add_int(doc, root, "kind", command.data.create_graph.graph_kind);
            if (command.data.create_graph.name) {
                yyjson_mut_obj_add_strcpy(
                    doc,
                    root,
                    "name",
                    command.data.create_graph.name
                );
            }
            break;
        case EDITOR_COMMAND_RENAME_GRAPH:
            doc = action_start(e, "rename", command.data.rename_graph.graph_id);
            root = action_root(doc);
            if (command.data.rename_graph.name) {
                yyjson_mut_obj_add_strcpy(
                    doc,
                    root,
                    "name",
                    command.data.rename_graph.name
                );
            }
            break;
        case EDITOR_COMMAND_SET_PIN_DEFAULT: {
            doc = action_start(e, "set_default", e->active_graph->graph_id);
            root = action_root(doc);
            yyjson_mut_obj_add_uint(
                doc,
                root,
                "node",
                command.data.set_pin_default.node_id
            );
            yyjson_mut_obj_add_int(
                doc,
                root,
                "output",
                command.data.set_pin_default.output
            );
            yyjson_mut_obj_add_int(
                doc,
                root,
                "pin",
                command.data.set_pin_default.pin_index
            );
            if (!write_pin_value(doc, root, command.data.set_pin_default.value)) {
                yyjson_mut_doc_free(doc);
                return NULL;
            }
            break;
        }
        case EDITOR_COMMAND_CONNECT_DATA:
        case EDITOR_COMMAND_CONNECT_EXEC:
            doc = action_start(e, "connect", e->active_graph->graph_id);
            root = action_root(doc);
            yyjson_mut_obj_add_int(
                doc,
                root,
                "kind",
                command.kind == EDITOR_COMMAND_CONNECT_DATA ? PIN_DATA : PIN_EXEC
            );
            yyjson_mut_obj_add_uint(doc, root, "from_node", command.data.connect.from_node);
            yyjson_mut_obj_add_int(doc, root, "from_pin", command.data.connect.from_pin);
            yyjson_mut_obj_add_uint(doc, root, "to_node", command.data.connect.to_node);
            yyjson_mut_obj_add_int(doc, root, "to_pin", command.data.connect.to_pin);
            break;
        default: return NULL;
    }
    return action_finish(doc);
}

char *graph_collab_action_open(const editor *e, uint64_t graph_id)
{
    if (!e || !e->asset) {
        return NULL;
    }
    yyjson_mut_doc *doc = action_start(e, "open", graph_id);
    return action_finish(doc);
}

char *graph_collab_action_spawn(const editor *e, const node_desc *desc, float x, float y)
{
    if (!e || !e->asset || !e->active_graph || !desc || !desc->name ||
        registry_node_by_name(desc->name) != desc || !isfinite(x) || !isfinite(y)) {
        return NULL;
    }
    yyjson_mut_doc *doc = action_start(e, "spawn", e->active_graph->graph_id);
    yyjson_mut_val *root = action_root(doc);
    yyjson_mut_obj_add_strcpy(doc, root, "descriptor", desc->name);
    yyjson_mut_obj_add_uint(doc, root, "node", next_asset_id(e));
    yyjson_mut_obj_add_real(doc, root, "x", x);
    yyjson_mut_obj_add_real(doc, root, "y", y);
    return action_finish(doc);
}

char *graph_collab_action_spawn_match(const editor *e, const ed_search_match *match, float x, float y)
{
    if (!e || !e->asset || !e->active_graph || !match || !match->desc ||
        !match->desc->name || !isfinite(x) || !isfinite(y)) {
        return NULL;
    }
    yyjson_mut_doc *doc = action_start(e, "spawn_match", e->active_graph->graph_id);
    yyjson_mut_val *root = action_root(doc);
    yyjson_mut_obj_add_strcpy(doc, root, "descriptor", match->desc->name);
    yyjson_mut_obj_add_uint(doc, root, "node", next_asset_id(e));
    yyjson_mut_obj_add_real(doc, root, "x", x);
    yyjson_mut_obj_add_real(doc, root, "y", y);
    if (match->function) {
        yyjson_mut_obj_add_uint(doc, root, "target_graph", match->function->graph_id);
        if (match->function->owner && match->function->owner != e->asset) {
            yyjson_mut_obj_add_strcpy(
                doc,
                root,
                "target_asset",
                match->function->owner->name
            );
        }
    }
    if (match->variable) {
        yyjson_mut_obj_add_uint(doc, root, "variable", match->variable->id);
    }
    if (match->prototype) {
        yyjson_mut_obj_add_strcpy(doc, root, "prototype", match->prototype);
    }
    if (match->event) {
        yyjson_mut_obj_add_strcpy(doc, root, "event", match->event->name);
    }
    if (match->host_event) {
        if (match->host_event > e->event_catalog_count || !e->event_catalog) {
            yyjson_mut_doc_free(doc);
            return NULL;
        }
        yyjson_mut_obj_add_strcpy(
            doc,
            root,
            "host_event",
            e->event_catalog[match->host_event - 1].name
        );
    }
    if (match->ui_event) {
        yyjson_mut_obj_add_strcpy(doc, root, "ui_event", match->ui_event->name);
    }
    if (match->setting) {
        yyjson_mut_obj_add_strcpy(doc, root, "setting", match->setting);
    }
    if (match->field) {
        yyjson_mut_obj_add_strcpy(doc, root, "field", match->field);
    }
    if (e->spawn_link_pending) {
        yyjson_mut_val *link = yyjson_mut_obj_add_obj(doc, root, "link");
        yyjson_mut_obj_add_uint(doc, link, "node", e->spawn_link_pin.n->id);
        yyjson_mut_obj_add_int(doc, link, "kind", e->spawn_link_pin.kind);
        yyjson_mut_obj_add_int(doc, link, "pin", e->spawn_link_pin.index);
    }
    return action_finish(doc);
}

char *graph_collab_action_connect(const editor *e, node *a, int a_kind, int a_pin, node *b, int b_kind, int b_pin)
{
    if (!e || !e->active_graph || !a || !b || a_kind == b_kind) {
        return NULL;
    }
    node *from = a_kind == ED_PIN_OUTPUT ? a : b;
    int from_pin = a_kind == ED_PIN_OUTPUT ? a_pin : b_pin;
    node *to = a_kind == ED_PIN_INPUT ? a : b;
    int to_pin = a_kind == ED_PIN_INPUT ? a_pin : b_pin;
    if (from_pin < 0 || from_pin >= from->output_count || to_pin < 0 ||
        to_pin >= to->input_count) {
        return NULL;
    }
    yyjson_mut_doc *doc = action_start(e, "connect", e->active_graph->graph_id);
    yyjson_mut_val *root = action_root(doc);
    yyjson_mut_obj_add_int(doc, root, "kind", from->output_pins[from_pin].kind);
    yyjson_mut_obj_add_uint(doc, root, "from_node", from->id);
    yyjson_mut_obj_add_int(doc, root, "from_pin", from_pin);
    yyjson_mut_obj_add_uint(doc, root, "to_node", to->id);
    yyjson_mut_obj_add_int(doc, root, "to_pin", to_pin);
    return action_finish(doc);
}

char *graph_collab_action_reset_pin(const editor *e, const node *n, int pin_index, const pin_value *value)
{
    if (!e || !e->active_graph || !n || pin_index < 0 ||
        pin_index >= n->input_count || !value) {
        return NULL;
    }
    yyjson_mut_doc *doc = action_start(e, "reset_pin", e->active_graph->graph_id);
    yyjson_mut_val *root = action_root(doc);
    yyjson_mut_obj_add_uint(doc, root, "node", n->id);
    yyjson_mut_obj_add_int(doc, root, "pin", pin_index);
    if (!write_pin_value(doc, root, value)) {
        yyjson_mut_doc_free(doc);
        return NULL;
    }
    return action_finish(doc);
}

static bool string_matches(yyjson_val *root, const char *key, const char *expected)
{
    yyjson_val *value = yyjson_obj_get(root, key);
    const char *actual = yyjson_get_str(value);
    return expected ? actual && !strcmp(actual, expected) : !value;
}

static const behavior_asset *find_asset(const editor *e, const char *name)
{
    if (!name || !name[0] || !strcmp(name, document_key(e)) ||
        !strcmp(name, e->asset->name)) {
        return e->asset;
    }
    const behavior_asset *found = NULL;
    for (int i = 0; i < acount(e->asset_catalog); i++) {
        const behavior_asset *asset = e->asset_catalog[i];
        if (!strcmp(asset->name, name)) {
            if (found) {
                return NULL;
            }
            found = asset;
        }
    }
    return found;
}

static bool match_is_action(const editor *e, const ed_search_match *match, yyjson_val *root)
{
    if (!match->desc || !string_matches(root, "descriptor", match->desc->name)) {
        return false;
    }
    yyjson_val *target_graph = yyjson_obj_get(root, "target_graph");
    const char *target_asset = yyjson_get_str(yyjson_obj_get(root, "target_asset"));
    yyjson_val *variable = yyjson_obj_get(root, "variable");
    const char *prototype = yyjson_get_str(yyjson_obj_get(root, "prototype"));
    const char *event = yyjson_get_str(yyjson_obj_get(root, "event"));
    const char *host_name = yyjson_get_str(yyjson_obj_get(root, "host_event"));
    const char *ui_name = yyjson_get_str(yyjson_obj_get(root, "ui_event"));
    if (target_graph) {
        const behavior_asset *asset = find_asset(e, target_asset);
        if (!asset || !match->function ||
            match->function->graph_id != yyjson_get_uint(target_graph) ||
            match->function->owner != asset) {
            return false;
        }
    } else if (match->function) {
        return false;
    }
    if (variable) {
        if (!match->variable || match->variable->id != yyjson_get_uint(variable)) {
            return false;
        }
    } else if (match->variable) {
        return false;
    }
    if (!string_matches(root, "prototype", match->prototype) ||
        !string_matches(root, "event", match->event ? match->event->name : NULL) ||
        !string_matches(root, "setting", match->setting) ||
        !string_matches(root, "field", match->field)) {
        return false;
    }
    if (host_name) {
        if (!match->host_event || match->host_event > e->event_catalog_count ||
            strcmp(host_name, e->event_catalog[match->host_event - 1].name)) {
            return false;
        }
    } else if (match->host_event) {
        return false;
    }
    if (ui_name) {
        if (!match->ui_event || strcmp(ui_name, match->ui_event->name)) {
            return false;
        }
    } else if (match->ui_event) {
        return false;
    }
    return true;
}

static bool replay_spawn_match(editor *e, yyjson_val *root, char *error, size_t error_size)
{
    const char *filter = "";
    ed_search_match *matches = ed_search_matches(e, filter);
    ed_search_match *match = NULL;
    for (int i = 0; i < acount(matches); i++) {
        if (match_is_action(e, &matches[i], root)) {
            match = &matches[i];
            break;
        }
    }
    if (!match) {
        afree(matches);
        snprintf(error, error_size, "Graph node descriptor or reference is unavailable");
        return false;
    }
    int pending_link = e->spawn_link_pending;
    ed_pin_view saved_link = e->spawn_link_pin;
    e->spawn_link_pending = 0;
    yyjson_val *link = yyjson_obj_get(root, "link");
    if (link) {
        yyjson_val *node_id = yyjson_obj_get(link, "node");
        yyjson_val *kind = yyjson_obj_get(link, "kind");
        yyjson_val *pin = yyjson_obj_get(link, "pin");
        node *source = yyjson_is_uint(node_id)
                         ? graph_node_by_id(e->active_graph, yyjson_get_uint(node_id))
                         : NULL;
        if (!source || !yyjson_is_int(kind) || !yyjson_is_int(pin)) {
            e->spawn_link_pending = pending_link;
            e->spawn_link_pin = saved_link;
            afree(matches);
            snprintf(error, error_size, "Graph spawn link endpoint is stale");
            return false;
        }
        int side = yyjson_get_int(kind), index = yyjson_get_int(pin);
        int count = side == ED_PIN_OUTPUT ? source->output_count : source->input_count;
        if ((side != ED_PIN_OUTPUT && side != ED_PIN_INPUT) || index < 0 ||
            index >= count) {
            e->spawn_link_pending = pending_link;
            e->spawn_link_pin = saved_link;
            afree(matches);
            snprintf(error, error_size, "Graph spawn link pin is invalid");
            return false;
        }
        e->spawn_link_pending = 1;
        e->spawn_link_pin = (ed_pin_view){ source, side, index };
    }
    float x = (float)yyjson_get_real(yyjson_obj_get(root, "x"));
    float y = (float)yyjson_get_real(yyjson_obj_get(root, "y"));
    yyjson_val *expected = yyjson_obj_get(root, "node");
    uint64_t made = yyjson_is_uint(expected) ? ed_spawn_match_at(e, match, x, y) : 0;
    uint64_t expected_id = yyjson_get_uint(expected);
    e->spawn_link_pending = pending_link;
    e->spawn_link_pin = saved_link;
    afree(matches);
    if (!made || made != expected_id) {
        snprintf(error, error_size, "Graph node creation diverged from its expected ID");
        return false;
    }
    return true;
}

bool graph_collab_apply_action(editor *e, const char *json, char *error, size_t error_size)
{
    if (error && error_size) {
        error[0] = 0;
    }
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = doc ? yyjson_doc_get_root(doc) : NULL;
    const char *op = yyjson_get_str(yyjson_obj_get(root, "op"));
    const char *key = yyjson_get_str(yyjson_obj_get(root, "document"));
    yyjson_val *graph_value = yyjson_obj_get(root, "graph");
    if (!e || !e->asset || !key || strcmp(key, document_key(e)) || !op ||
        !yyjson_is_uint(graph_value)) {
        yyjson_doc_free(doc);
        if (error && error_size) {
            snprintf(error, error_size, "Graph action does not match the active document");
        }
        return false;
    }
    uint64_t graph_id = yyjson_get_uint(graph_value);
    int result = -1;
    if (!strcmp(op, "open")) {
        result = editor_open_graph(e, graph_id);
    } else if (!strcmp(op, "create")) {
        yyjson_val *kind = yyjson_obj_get(root, "kind"), *id = graph_value;
        const char *name = yyjson_get_str(yyjson_obj_get(root, "name"));
        if (yyjson_is_int(kind) && yyjson_get_int(kind) >= GRAPH_EVENT &&
            yyjson_get_int(kind) <= GRAPH_MACRO && name) {
            result = editor_command_apply(
                e,
                (editor_command){
                    .kind = EDITOR_COMMAND_CREATE_GRAPH,
                    .data.create_graph = { (graph_kind)yyjson_get_int(kind), name } }
            );
            graph *created = behavior_asset_find_graph(e->asset, yyjson_get_uint(id));
            if (result == 0 && (!created || strcmp(created->name, name) ||
                                created->kind != (graph_kind)yyjson_get_int(kind))) {
                result = -1;
            }
        }
    } else if (!strcmp(op, "rename")) {
        const char *name = yyjson_get_str(yyjson_obj_get(root, "name"));
        if (name) {
            result = editor_command_apply(
                e,
                (editor_command){ .kind = EDITOR_COMMAND_RENAME_GRAPH,
                                  .data.rename_graph = { graph_id, name } }
            );
        }
    } else if (!strcmp(op, "spawn")) {
        const char *descriptor = yyjson_get_str(yyjson_obj_get(root, "descriptor"));
        yyjson_val *id = yyjson_obj_get(root, "node");
        const node_desc *desc = descriptor ? registry_node_by_name(descriptor) : NULL;
        float x = (float)yyjson_get_real(yyjson_obj_get(root, "x"));
        float y = (float)yyjson_get_real(yyjson_obj_get(root, "y"));
        node *made = desc && yyjson_is_uint(id) && e->active_graph->graph_id == graph_id
                       ? editor_spawn(e, desc, x, y)
                       : NULL;
        result = made && made->id == yyjson_get_uint(id) ? 0 : -1;
    } else if (!strcmp(op, "spawn_match")) {
        if (e->active_graph->graph_id == graph_id) {
            result = replay_spawn_match(e, root, error, error_size) ? 0 : -1;
        }
    } else if (!strcmp(op, "set_default")) {
        yyjson_val *node_id = yyjson_obj_get(root, "node"),
                   *output = yyjson_obj_get(root, "output"),
                   *pin = yyjson_obj_get(root, "pin");
        pin_value value = { 0 };
        if (e->active_graph->graph_id == graph_id && yyjson_is_uint(node_id) &&
            yyjson_is_int(output) && yyjson_is_int(pin) &&
            read_pin_value(yyjson_obj_get(root, "value"), &value)) {
            result = editor_command_apply(
                e,
                (editor_command){ .kind = EDITOR_COMMAND_SET_PIN_DEFAULT,
                                  .data.set_pin_default = { yyjson_get_uint(node_id),
                                                            yyjson_get_int(output),
                                                            yyjson_get_int(pin),
                                                            &value } }
            );
        }
    } else if (!strcmp(op, "reset_pin")) {
        yyjson_val *node_id = yyjson_obj_get(root, "node"),
                   *pin = yyjson_obj_get(root, "pin");
        pin_value value = { 0 };
        node *n = yyjson_is_uint(node_id)
                    ? graph_node_by_id(e->active_graph, yyjson_get_uint(node_id))
                    : NULL;
        if (e->active_graph->graph_id == graph_id && n && yyjson_is_int(pin) &&
            read_pin_value(yyjson_obj_get(root, "value"), &value) &&
            yyjson_get_int(pin) >= 0 && yyjson_get_int(pin) < n->input_count) {
            ed_snapshot_before(e);
            result = graph_set_pin_binding(e->active_graph, n, 0, yyjson_get_int(pin), NULL);
            if (result == 0) {
                result = graph_set_pin_default(e->active_graph, n, 0, yyjson_get_int(pin), &value);
            }
        }
    } else if (!strcmp(op, "connect")) {
        yyjson_val *kind = yyjson_obj_get(root, "kind"),
                   *from_node = yyjson_obj_get(root, "from_node"),
                   *from_pin = yyjson_obj_get(root, "from_pin"),
                   *to_node = yyjson_obj_get(root, "to_node"),
                   *to_pin = yyjson_obj_get(root, "to_pin");
        if (e->active_graph->graph_id == graph_id && yyjson_is_int(kind) &&
            yyjson_is_uint(from_node) && yyjson_is_int(from_pin) &&
            yyjson_is_uint(to_node) && yyjson_is_int(to_pin) &&
            (yyjson_get_int(kind) == PIN_DATA || yyjson_get_int(kind) == PIN_EXEC)) {
            result = editor_command_apply(
                e,
                (editor_command){ .kind = yyjson_get_int(kind) == PIN_DATA
                                            ? EDITOR_COMMAND_CONNECT_DATA
                                            : EDITOR_COMMAND_CONNECT_EXEC,
                                  .data.connect = { yyjson_get_uint(from_node),
                                                    yyjson_get_int(from_pin),
                                                    yyjson_get_uint(to_node),
                                                    yyjson_get_int(to_pin) } }
            );
        }
    }
    yyjson_doc_free(doc);
    if (result != 0) {
        if (error && error_size && !error[0]) {
            snprintf(error, error_size, "Graph action was refused by the editor");
        }
        return false;
    }
    return true;
}
