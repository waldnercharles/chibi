#include "editor/graph/eval_view.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/serialize.h"
#include "graph/shader/shader_graph.h"

#include <assert.h>
#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int editor_command_apply(editor *e, editor_command command)
{
    if (!e || !e->asset ||
        (e->active_graph->math_expression && command.kind != EDITOR_COMMAND_OPEN_GRAPH &&
         command.kind != EDITOR_COMMAND_RENAME_GRAPH)) {
        return -1;
    }
    switch (command.kind) {
        case EDITOR_COMMAND_CREATE_GRAPH: {
            if (!command.data.create_graph.name || !command.data.create_graph.name[0] ||
                !behavior_asset_mutable(e->asset) ||
                behavior_asset_find_graph_by_name(
                    e->asset,
                    command.data.create_graph.name
                )) {
                return -1;
            }
            ed_snapshot_before(e);
            graph *created = command.data.create_graph.graph_kind == GRAPH_FUNCTION
                               ? behavior_asset_add_function(
                                     e->asset,
                                     command.data.create_graph.name,
                                     0
                                 )
                           : command.data.create_graph.graph_kind == GRAPH_MACRO
                               ? behavior_asset_add_macro(
                                     e->asset,
                                     command.data.create_graph.name
                                 )
                               : behavior_asset_add_graph(
                                     e->asset,
                                     command.data.create_graph.graph_kind,
                                     command.data.create_graph.name
                                 );
            return created ? 0 : -1;
        }
        case EDITOR_COMMAND_RENAME_GRAPH: {
            const char *name = command.data.rename_graph.name;
            graph *target = behavior_asset_find_graph(
                e->asset,
                command.data.rename_graph.graph_id
            );
            graph *same = behavior_asset_find_graph_by_name(e->asset, name);
            if (!name || !name[0] || !behavior_asset_mutable(e->asset) || !target ||
                command.data.rename_graph.graph_id == 0 || (same && same != target)) {
                return -1;
            }
            ed_snapshot_before(e);
            return behavior_asset_rename_graph(
                e->asset,
                command.data.rename_graph.graph_id,
                name
            );
        }
        case EDITOR_COMMAND_OPEN_GRAPH:
            return editor_open_graph(e, command.data.open_graph.graph_id);
        case EDITOR_COMMAND_SPAWN_NODE:
            if (!command.data.spawn_node.desc || !e->active_graph ||
                !behavior_asset_mutable(e->asset)) {
                return -1;
            }
            return editor_spawn(
                       e,
                       command.data.spawn_node.desc,
                       command.data.spawn_node.x,
                       command.data.spawn_node.y
                   )
                     ? 0
                     : -1;
        case EDITOR_COMMAND_SET_PIN_DEFAULT: {
            node *n = graph_node_by_id(
                e->active_graph,
                command.data.set_pin_default.node_id
            );
            const pin_value *value = command.data.set_pin_default.value;
            if (!n || !value ||
                (command.data.set_pin_default.output != 0 &&
                 command.data.set_pin_default.output != 1) ||
                !behavior_asset_mutable(e->asset)) {
                return -1;
            }
            const pin_spec *pins = command.data.set_pin_default.output
                                     ? n->output_pins
                                     : n->input_pins;
            int pin_count = command.data.set_pin_default.output ? n->output_count
                                                                : n->input_count;
            int pin_index = command.data.set_pin_default.pin_index;
            if (pin_index < 0 || pin_index >= pin_count ||
                pins[pin_index].kind != PIN_DATA ||
                value->type != pins[pin_index].type) {
                return -1;
            }
            ed_snapshot_before(e);
            return graph_set_pin_default(
                e->active_graph,
                n,
                command.data.set_pin_default.output,
                pin_index,
                value
            );
        }
        case EDITOR_COMMAND_CONNECT_DATA:
        case EDITOR_COMMAND_CONNECT_EXEC: {
            node *from = graph_node_by_id(e->active_graph, command.data.connect.from_node);
            node *to = graph_node_by_id(e->active_graph, command.data.connect.to_node);
            if (!from || !to) {
                return -1;
            }
            if (command.data.connect.from_pin < 0 ||
                command.data.connect.from_pin >= from->output_count ||
                command.data.connect.to_pin < 0 ||
                command.data.connect.to_pin >= to->input_count) {
                return -1;
            }
            pin_kind expected_kind = command.kind == EDITOR_COMMAND_CONNECT_DATA
                                       ? PIN_DATA
                                       : PIN_EXEC;
            if (!behavior_asset_mutable(e->asset) ||
                !ed_connection_specs_valid(
                    &from->output_pins[command.data.connect.from_pin],
                    &to->input_pins[command.data.connect.to_pin],
                    expected_kind
                )) {
                return -1;
            }
            char *snapshot = ed_snapshot(e);
            int pending = e->edit_pending;
            ed_pin_view output = {
                from,
                ED_PIN_OUTPUT,
                command.data.connect.from_pin,
                { 0.0f, 0.0f },
            };
            ed_pin_view input = {
                to,
                ED_PIN_INPUT,
                command.data.connect.to_pin,
                { 0.0f, 0.0f },
            };
            int result = ed_connect(e, &output, &input) ? 0 : -1;
            if (result == 0) {
                e->edit_pending = 1;
                if (snapshot) {
                    ed_push_string(&e->undo, snapshot);
                }
                ed_free_string_stack(&e->redo);
                return 0;
            }
            char *current = ed_snapshot(e);
            if (snapshot && current && strcmp(current, snapshot) != 0) {
                if (ed_adopt_behavior_asset(e, snapshot) != 0) {
                    abort();
                }
                free(e->edit_revision_snapshot);
                e->edit_revision_snapshot = ed_snapshot(e);
                e->edit_revision_asset = e->asset;
                e->edit_revision_initialized = 1;
            }
            free(current);
            free(snapshot);
            e->edit_pending = pending;
            return result;
        }
        case EDITOR_COMMAND_COMPILE:
            ed_compile(e);
            return compile_report_blocked(&e->compile) ? -1 : 0;
        case EDITOR_COMMAND_SAVE:
            return command.data.file.path ? editor_save(e, command.data.file.path)
                                          : -1;
        case EDITOR_COMMAND_LOAD:
            return command.data.file.path ? editor_load(e, command.data.file.path)
                                          : -1;
    }
    return -1;
}

void editor_query_state(const editor *e, editor_query *out)
{
    assert(out);
    *out = (editor_query){ 0 };
    if (!e || !e->asset) {
        return;
    }
    out->active_graph = e->active_graph;
    out->graph_count = acount(e->asset->graphs);
    out->graph_visible = e->graph_visible &&
                         e->ui_context != EDITOR_UI_CONTEXT_SCENE &&
                         editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS);
    out->compile = &e->compile;
    out->inspection_instance = e->inspection_instance;
    out->inspection_blocked = ed_inspection_blocked(e);
    out->preview_error = eval_view_error(e->ev);
    out->can_go_back = ed_can_go_back(e);
    out->zoom = e->zoom;
    out->pan_x = e->pan.x;
    out->pan_y = e->pan.y;
    out->camera_moving = e->camera_moving;
    if (e->edit_revision_initialized && !e->edit_pending &&
        e->edit_revision_asset == e->asset && e->edit_revision_snapshot) {
        out->dirty = !e->saved_snapshot ||
                     strcmp(e->edit_revision_snapshot, e->saved_snapshot) != 0;
        return;
    }
    char *json = behavior_asset_save_json(e->asset);
    out->dirty = !json || !e->saved_snapshot || strcmp(json, e->saved_snapshot) != 0;
    free(json);
}

static void ed_record_math_edit(editor *e, char *before)
{
    e->edit_pending = 1;
    ed_push_string(&e->undo, before);
    ed_free_string_stack(&e->redo);
}

node *editor_add_math_expression(editor *e, const char *source, float x, float y, math_expression_error *error)
{
    if (!e || e->active_graph->math_expression) {
        return NULL;
    }
    char *before = ed_snapshot(e);
    node *n = math_expression_add(e->asset, e->active_graph, source, error);
    if (!n) {
        free(before);
        return NULL;
    }
    n->x = x;
    n->y = y;
    ed_record_math_edit(e, before);
    ed_select_node(e, n, 0);
    return n;
}

int editor_set_math_expression(editor *e, node *call, const char *source, math_expression_error *error)
{
    if (!e || e->active_graph->math_expression) {
        return -1;
    }
    const char *current = math_expression_source(e->asset, call);
    if (source && current && !strcmp(source, current)) {
        return 0;
    }
    char *before = ed_snapshot(e);
    if (math_expression_set(e->asset, call, source, error) != 0) {
        free(before);
        return -1;
    }
    ed_record_math_edit(e, before);
    if (e->fit_mode) {
        ed_zoom_fit(e, true);
    }
    return 0;
}

node *editor_spawn(editor *e, const node_desc *desc, float x, float y)
{
    if (e->active_graph->math_expression) {
        return NULL;
    }
    ed_snapshot_before(e);
    node *n = graph_add_node(e->active_graph, desc);
    if (!n) {
        return NULL;
    }
    if (!shader_graph_init_node(e->active_graph, n) ||
        ed_name_spawned_event(e, n) != 0) {
        graph_remove_node(e->active_graph, n);
        ed_cancel_snapshot(e);
        return NULL;
    }
    n->x = x;
    n->y = y;
    return n;
}

int editor_save(editor *e, const char *path)
{
    if (!e || !path) {
        return -1;
    }
    char *json = behavior_asset_save_json(e->asset);
    if (!json) {
        return -1;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(json);
        return -1;
    }
    int ok = fputs(json, f) >= 0;
    if (fclose(f) != 0) {
        ok = 0;
    }
    if (!ok) {
        free(json);
        return -1;
    }
    free(e->saved_snapshot);
    e->saved_snapshot = json;
    e->edit_revision_initialized = 0;
    return 0;
}

static int ed_read_file(const char *path, char **out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) || ftell(f) < 0) {
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return -1;
    }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read] = '\0';
    *out = buf;
    return 0;
}

int editor_load(editor *e, const char *path)
{
    if (!e || !path) {
        return -1;
    }
    char *json = NULL;
    if (ed_read_file(path, &json)) {
        return -1;
    }
    ed_snapshot_before(e);
    int result = ed_adopt_behavior_asset_with_identity(e, json, 0);
    if (result == 0) {
        aclear(e->graph_callers);
        aclear(e->graph_views);
        ed_load_graph_view(e);
        e->arrange_pending = true;
        free(e->saved_snapshot);
        e->saved_snapshot = behavior_asset_save_json(e->asset);
        e->edit_revision_initialized = 0;
    }
    free(json);
    return result;
}
