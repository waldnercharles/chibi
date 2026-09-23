#include "editor/commands/internal.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/serialize.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *op;
    yyjson_val *object, *field, *json;
    ecs_entity entity;
    const ecs_component_desc *component;
    int index;
    pin_value before, after;
} command_edit;

static bool equal_value(pin_value a, pin_value b)
{
    if (a.type != b.type) {
        return false;
    }
    if (a.type == PIN_NAME || a.type == PIN_STR) {
        return !strcmp(a.str ? a.str : "", b.str ? b.str : "");
    }
    if (a.type == PIN_F32) {
        return a.f == b.f;
    }
    if (a.type == PIN_F64) {
        return a.d == b.d;
    }
    /* Values are read using the same reflected type. */
    switch (a.type) {
        case PIN_BOOL: return a.b == b.b;
        case PIN_U32: return a.u == b.u;
        case PIN_I32: return a.i == b.i;
        case PIN_U16: return a.u16 == b.u16;
        case PIN_I16: return a.i16 == b.i16;
        case PIN_U8: return a.u8 == b.u8;
        case PIN_I8: return a.i8 == b.i8;
        case PIN_ENUM: return a.e == b.e;
        default: return false;
    }
}

static int actor_index(app_scene_authoring *scene, ecs_entity entity)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        if (ecs_entity_to_u64(scene->actors[i].entity) == ecs_entity_to_u64(entity)) {
            return i;
        }
    }
    return -1;
}

static bool read_value(app_scene_authoring *scene, command_edit *edit, pin_value *value)
{
    const char *kind = command_string(edit->object, "kind");
    if (!strcmp(kind, "prototype")) {
        scene_prototype_asset *asset = find_prototype(scene, command_string(edit->object, "name"));
        return asset && !game_prototype_read_field(
                            asset->prototype,
                            command_string(edit->field, "component"),
                            command_string(edit->field, "name"),
                            value
                        );
    }
    if (!strcmp(kind, "entity")) {
        return edit->component && !ecs_component_read_field(
                                      scene->world,
                                      edit->entity,
                                      edit->component->id,
                                      edit->index,
                                      value
                                  );
    }
    if (!strcmp(kind, "node")) {
        node *n = graph_node_by_id(
            scene->graph_editor->active_graph,
            yyjson_get_uint(yyjson_obj_get(edit->object, "node"))
        );
        if (!n) {
            return false;
        }
        *value = (yyjson_get_bool(yyjson_obj_get(edit->field, "output"))
                      ? n->output_pins
                      : n->input_pins)[edit->index]
                     .default_value;
        return true;
    }
    return false;
}

static const char *prepare(
    app_scene_authoring *scene,
    yyjson_val *catalog,
    yyjson_val *ops,
    command_edit **edits
)
{
    int count = yyjson_arr_size(ops);
    if (count < 1 || count > 64) {
        return "Expected one to 64 operations";
    }
    const char *document = NULL, *category = NULL;
    for (int i = 0; i < count; i++) {
        yyjson_val *json = yyjson_arr_get(ops, i);
        command_edit edit = { .op = command_string(json, "op"), .json = json };
        edit.object = command_find(yyjson_obj_get(catalog, "objects"), command_string(json, "target"));
        const char *kind = command_string(edit.object, "kind");
        bool property = !strcmp(edit.op, "set") || !strcmp(edit.op, "assert");
        bool mutation = property || !strcmp(edit.op, "duplicate") ||
                        !strcmp(edit.op, "connect");
        if (mutation) {
            if (!edit.object) {
                return "Target no longer exists";
            }
            const char *target_document = command_string(edit.object, "document");
            if (document && strcmp(document, target_document)) {
                return "Changes span multiple documents";
            }
            document = target_document;
            if (category && strcmp(category, kind)) {
                return "Changes use different document types";
            }
            category = kind;
            if (scene->runtime_snapshot_valid || scene->preview_playing) {
                return "Stop playback before editing";
            }
        }
        if (!strcmp(kind, "entity")) {
            edit.entity = ecs_entity_from_u64(
                yyjson_get_uint(yyjson_obj_get(edit.object, "entity"))
            );
            if (actor_index(scene, edit.entity) < 0) {
                return "Instance no longer exists";
            }
        }
        const char *reference = command_string(json, "result_target");
        if (*reference) {
            bool found = false;
            for (int p = 0; p < acount(*edits); p++) {
                command_edit *prior = &(*edits)[p];
                if (!strcmp(prior->op, "duplicate") &&
                    !strcmp(command_string(prior->json, "result"), reference) &&
                    prior->object == edit.object) {
                    found = true;
                }
            }
            if (!property || !found) {
                return "Invalid operation result reference";
            }
        }
        if (property) {
            edit.field = command_find(
                yyjson_obj_get(edit.object, "fields"),
                command_string(json, "field")
            );
            if (!edit.field) {
                return "Property no longer exists";
            }
            if (yyjson_obj_get(edit.field, "editable") &&
                !yyjson_get_bool(yyjson_obj_get(edit.field, "editable"))) {
                return "This graph pin is not editable";
            }
            edit.index = yyjson_get_int(
                yyjson_obj_get(edit.field, !strcmp(kind, "node") ? "index" : "field_index")
            );
            for (int c = 0; c < scene->registry.component_count; c++) {
                const ecs_component_desc *desc = scene->registry.components[c];
                if (!strcmp(desc->key, command_string(edit.field, "component_key"))) {
                    edit.component = desc;
                }
            }
            if (!read_value(scene, &edit, &edit.before) ||
                !command_value(yyjson_obj_get(json, "value"), edit.before, &edit.after)) {
                return "Invalid property value";
            }
            yyjson_val *choices = yyjson_obj_get(edit.field, "choices");
            if (choices) {
                bool found = false;
                for (int c = 0; c < (int)yyjson_arr_size(choices); c++) {
                    pin_value choice;
                    if (command_value(
                            yyjson_obj_get(yyjson_arr_get(choices, c), "value"),
                            edit.before,
                            &choice
                        ) &&
                        equal_value(choice, edit.after)) {
                        found = true;
                    }
                }
                if (!found) {
                    return "Choose a listed property value";
                }
            }
            if (!strcmp(edit.op, "assert") && !equal_value(edit.before, edit.after)) {
                return "A preserved property changed";
            }
            for (int p = 0; p < acount(*edits); p++) {
                command_edit *prior = &(*edits)[p];
                if (prior->field == edit.field && prior->object == edit.object &&
                    (!strcmp(prior->op, "set") || !strcmp(edit.op, "set"))) {
                    return "Conflicting property changes";
                }
            }
        } else if (!strcmp(edit.op, "duplicate")) {
            if (strcmp(kind, "entity")) {
                return "Only scene instances can be duplicated";
            }
            const scene_actor *actor = &scene->actors[actor_index(scene, edit.entity)];
            if (strlen(actor->prototype_name) > 230) {
                return "Instance name is too long to duplicate";
            }
            for (int p = 0; p < acount(*edits); p++) {
                if (*command_string(json, "result") &&
                    !strcmp(command_string((*edits)[p].json, "result"), command_string(json, "result"))) {
                    return "Duplicate operation result ID";
                }
            }
        } else if (!strcmp(edit.op, "connect")) {
            if (strcmp(kind, "node")) {
                return "Choose a graph node";
            }
        } else if (!strcmp(edit.op, "select") || !strcmp(edit.op, "frame")) {
            if (strcmp(kind, "entity")) {
                return "Choose a scene instance";
            }
        } else if (!strcmp(edit.op, "open")) {
            if (!edit.object || !yyjson_obj_get(edit.object, "asset_kind")) {
                return "Choose an asset";
            }
        } else if (!strcmp(edit.op, "panel")) {
            if (!command_find(yyjson_obj_get(catalog, "panels"), command_string(json, "target")) ||
                !yyjson_is_bool(yyjson_obj_get(json, "value"))) {
                return "Invalid panel";
            }
        } else if (!strcmp(edit.op, "save") || !strcmp(edit.op, "undo") || !strcmp(edit.op, "redo") || !strcmp(edit.op, "runtime")) {
            if (count != 1) {
                return "Run playback, history, and save commands separately";
            }
        } else {
            return "Unsupported operation";
        }
        if (!strcmp(edit.op, "runtime")) {
            const char *target = command_string(json, "target"),
                       *value = command_string(json, "value");
            if (strcmp(target, "scene") && strcmp(target, "preview")) {
                return "Invalid playback target";
            }
            if (strcmp(value, "play") && strcmp(value, "start") &&
                strcmp(value, "pause") && strcmp(value, "stop")) {
                return "Invalid playback command";
            }
        }
        apush(*edits, edit);
    }
    if (document) {
        for (int i = 0; i < acount(*edits); i++) {
            const char *op = (*edits)[i].op;
            if (strcmp(op, "set") && strcmp(op, "assert") &&
                strcmp(op, "duplicate") && strcmp(op, "connect")) {
                return "Run navigation separately from content changes";
            }
        }
    }
    return NULL;
}

static const char *graph_apply(app_scene_authoring *scene, yyjson_val *catalog, command_edit *edits, bool dry)
{
    editor *live = scene->graph_editor;
    if (!behavior_asset_mutable(live->asset) || live->active_graph->math_expression) {
        return "Graph is read-only";
    }
    char *before = ed_snapshot(live);
    behavior_asset *asset = behavior_asset_load_json(before);
    if (!asset) {
        free(before);
        return "Could not stage graph";
    }
    editor *stage = editor_new(asset);
    editor_open_graph(stage, live->active_graph->graph_id);
    const char *error = NULL;
    for (int i = 0; i < acount(edits) && !error; i++) {
        command_edit *edit = &edits[i];
        if (!strcmp(edit->op, "assert")) {
            continue;
        }
        editor_command command = { 0 };
        if (!strcmp(edit->op, "set")) {
            command.kind = EDITOR_COMMAND_SET_PIN_DEFAULT;
            command.data.set_pin_default.node_id = yyjson_get_uint(
                yyjson_obj_get(edit->object, "node")
            );
            command.data.set_pin_default.output = yyjson_get_bool(
                yyjson_obj_get(edit->field, "output")
            );
            command.data.set_pin_default.pin_index = edit->index;
            command.data.set_pin_default.value = &edit->after;
        } else {
            yyjson_val *from = command_find(
                yyjson_obj_get(edit->object, "fields"),
                command_string(edit->json, "field")
            );
            yyjson_val *destination = command_find(
                yyjson_obj_get(catalog, "objects"),
                command_string(edit->json, "destination")
            );
            yyjson_val *to = command_find(
                yyjson_obj_get(destination, "fields"),
                command_string(edit->json, "pin")
            );
            if (!from || !to ||
                strcmp(command_string(destination, "document"), command_string(edit->object, "document")) ||
                !yyjson_get_bool(yyjson_obj_get(from, "output")) ||
                yyjson_get_bool(yyjson_obj_get(to, "output"))) {
                error = "Invalid connection endpoints";
                break;
            }
            command.kind = yyjson_get_int(yyjson_obj_get(from, "pin_kind")) == PIN_EXEC
                             ? EDITOR_COMMAND_CONNECT_EXEC
                             : EDITOR_COMMAND_CONNECT_DATA;
            command.data.connect.from_node = yyjson_get_uint(
                yyjson_obj_get(edit->object, "node")
            );
            command.data.connect.to_node = yyjson_get_uint(
                yyjson_obj_get(destination, "node")
            );
            command.data.connect.from_pin = yyjson_get_int(yyjson_obj_get(from, "index"));
            command.data.connect.to_pin = yyjson_get_int(yyjson_obj_get(to, "index"));
        }
        if (editor_command_apply(stage, command)) {
            error = "Graph edit failed validation";
        }
    }
    if (!error && !dry) {
        ed_arrange_in_transaction(stage);
        char *after = ed_snapshot(stage);
        if (strcmp(before, after)) {
            int result = ed_adopt_behavior_asset(live, after);
            assert(!result);
            ed_push_string(&live->undo, before);
            before = NULL;
            ed_free_string_stack(&live->redo);
        }
        free(after);
    }
    editor_free(stage);
    free(before);
    return error;
}

static const char *apply(app_scene_authoring *scene, yyjson_val *catalog, command_edit *edits, bool dry)
{
    command_edit *first = &edits[0];
    const char *kind = command_string(first->object, "kind");
    bool content = !strcmp(first->op, "set") || !strcmp(first->op, "assert") ||
                   !strcmp(first->op, "duplicate") || !strcmp(first->op, "connect");
    if (content && !strcmp(kind, "node")) {
        return graph_apply(scene, catalog, edits, dry);
    }
    if (dry) {
        return NULL;
    }
    if (content && !strcmp(kind, "prototype")) {
        app_prototype_field_change *changes = NULL;
        for (int i = 0; i < acount(edits); i++) {
            command_edit *edit = &edits[i];
            if (strcmp(edit->op, "set") || equal_value(edit->before, edit->after)) {
                continue;
            }
            apush(
                changes,
                ((app_prototype_field_change){ command_string(edit->field, "component"),
                                               command_string(edit->field, "name"),
                                               edit->after })
            );
        }
        const char *name = command_string(first->object, "name");
        int result = acount(changes)
                       ? app_scene_authoring_set_prototype_fields(scene, name, changes, acount(changes))
                       : 0;
        afree(changes);
        if (result) {
            return "Could not apply prototype changes";
        }
        app_scene_authoring_open_prototype(scene, name);
    } else if (content) {
        /* Validate the entire batch before entering one history scope. Field writes
         * cannot fail after this validation on the editor frame thread. */
        char *history = scene_history_checkpoint(scene->history);
        uint64_t *handles = NULL;
        for (int i = 0; i < acount(scene->actors); i++) {
            apush(handles, ecs_entity_to_u64(scene->actors[i].entity));
        }
        bool dirty = scene->scene_dirty, failed = false, changed = false;
        scene_history_begin_group(scene->history);
        for (int i = 0; i < acount(edits); i++) {
            command_edit *edit = &edits[i];
            if (!strcmp(edit->op, "duplicate")) {
                select_instance(scene, actor_index(scene, edit->entity));
                int result = app_scene_authoring_duplicate_instance(scene);
                if (result) {
                    failed = true;
                    break;
                }
                changed = true;
                ecs_entity copy = scene->actors[acount(scene->actors) - 1].entity;
                select_instance(scene, acount(scene->actors) - 1);
                const char *reference = command_string(edit->json, "result");
                for (int j = i + 1; *reference && j < acount(edits); j++) {
                    if (!strcmp(command_string(edits[j].json, "result_target"), reference)) {
                        edits[j].entity = copy;
                    }
                }
            }
            if (strcmp(edit->op, "set") || equal_value(edit->before, edit->after)) {
                continue;
            }
            int result = ecs_component_write_field(
                scene->world,
                edit->entity,
                edit->component->id,
                edit->index,
                edit->after
            );
            if (result) {
                failed = true;
                break;
            }
            changed = true;
            bool recorded = scene_history_push_field(
                scene->history,
                edit->entity,
                edit->component->id,
                edit->index,
                edit->before,
                edit->after
            );
            assert(recorded);
            scene->scene_dirty = true;
        }
        scene_history_end_group(scene->history);
        if (failed) {
            if (changed) {
                int restored = replay_scene_edit(scene, false);
                assert(!restored);
            }
            bool restored = scene_history_restore_checkpoint(
                scene->history,
                history,
                handles,
                handles,
                acount(handles)
            );
            assert(restored);
            scene->scene_dirty = dirty;
            free(history);
            afree(handles);
            return "Scene changes were rolled back";
        }
        free(history);
        afree(handles);
        scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    } else {
        bool selecting = !strcmp(first->op, "select");
        if (selecting) {
            for (int a = 0; a < acount(scene->actors); a++) {
                scene->actors[a].marked = false;
            }
        }
        for (int i = 0; i < acount(edits); i++) {
            command_edit *edit = &edits[i];
            if (!strcmp(edit->op, "select") || !strcmp(edit->op, "frame")) {
                int index = actor_index(scene, edit->entity);
                select_instance(scene, index);

                if (!strcmp(edit->op, "frame")) {
                    fit_scene_view(scene->render_viewport, scene->world, edit->entity);
                }
            } else if (!strcmp(edit->op, "open")) {
                for (int a = 0; a < asset_index_asset_count(scene->asset_index); a++) {
                    if (!strcmp(
                            asset_index_asset_at(scene->asset_index, a)->identity,
                            command_string(edit->object, "id")
                        ) &&
                        app_scene_authoring_open_asset(scene, a)) {
                        return "Could not open asset";
                    }
                }
            } else if (!strcmp(edit->op, "panel")) {
                int pane = -1;
                sscanf(command_string(edit->json, "target"), "panel:%d", &pane);
                editor_ui_set_pane_visible(pane, yyjson_get_bool(yyjson_obj_get(edit->json, "value")));
            } else if (!strcmp(edit->op, "runtime")) {
                const char *value = command_string(edit->json, "value"),
                           *target = command_string(edit->json, "target");
                if (strcmp(target, "scene") && strcmp(target, "preview")) {
                    return "Invalid playback target";
                }
                app_runtime_command cmd;
                if (!strcmp(value, "play") || !strcmp(value, "start")) {
                    cmd = APP_RUNTIME_PLAY;
                } else if (!strcmp(value, "pause")) {
                    cmd = APP_RUNTIME_PAUSE;
                } else if (!strcmp(value, "stop")) {
                    cmd = APP_RUNTIME_STOP;
                } else {
                    return "Invalid playback command";
                }
                if (app_scene_authoring_runtime_command(
                        scene,
                        !strcmp(target, "scene") ? APP_RUNTIME_SCENE : APP_RUNTIME_PREVIEW,
                        cmd
                    )) {
                    return "Playback command failed";
                }
            } else if (!strcmp(edit->op, "save")) {
                if (save_active_document(scene)) {
                    return "Save failed";
                }
            } else if (editor_ui_run_command(!strcmp(edit->op, "undo") ? EDITOR_UI_COMMAND_UNDO : EDITOR_UI_COMMAND_REDO)) {
                return "History command failed";
            }
        }
    }
    if (!strcmp(first->op, "select")) {
        for (int i = 0; i < acount(edits); i++) {
            scene->actors[actor_index(scene, edits[i].entity)].marked = true;
        }
        scene_authoring_select_document(scene, APP_EDITOR_DOCUMENT_SCENE, APP_SCENE_DOCUMENT_ID);
    }
    for (int i = 0; i < acount(edits); i++) {
        command_edit *edit = &edits[i];
        if (!edit->field) {
            continue;
        }
        pin_value readback;
        bool verified = read_value(scene, edit, &readback) &&
                        equal_value(readback, edit->after);
        assert(verified);
    }
    return NULL;
}

int editor_commands_apply(
    app_scene_authoring *scene,
    const char *catalog_json,
    const char *proposal_json,
    char *error,
    int size
)
{
    yyjson_doc *catalog_doc = yyjson_read(catalog_json, strlen(catalog_json), 0);
    yyjson_doc *proposal_doc = yyjson_read(proposal_json, strlen(proposal_json), 0);
    char *fresh = editor_commands_catalog(scene);
    yyjson_doc *fresh_doc = yyjson_read(fresh, strlen(fresh), 0);
    yyjson_val *catalog = yyjson_doc_get_root(catalog_doc),
               *proposal = yyjson_doc_get_root(proposal_doc);
    const char *message = NULL;
    command_edit *edits = NULL;
    yyjson_val *current = yyjson_doc_get_root(fresh_doc);
    yyjson_val *ops = yyjson_obj_get(proposal, "operations");
    bool playback = yyjson_arr_size(ops) == 1 &&
                    !strcmp(command_string(yyjson_arr_get(ops, 0), "op"), "runtime");
    bool revision_matches = !strcmp(
        command_string(catalog, "revision"),
        command_string(current, "revision")
    );
    /* Playback controls tolerate simulation ticks, but never a different editor
     * session or active document. Content edits require the complete observation. */
    if (playback) {
        revision_matches = yyjson_get_uint(yyjson_obj_get(catalog, "incarnation")) ==
                               yyjson_get_uint(yyjson_obj_get(current, "incarnation")) &&
                           !strcmp(command_string(catalog, "document"), command_string(current, "document"));
    }
    if (!catalog_doc || !proposal_doc) {
        message = "Invalid command JSON";
    } else if (!revision_matches || strcmp(command_string(catalog, "revision"), command_string(proposal, "revision"))) {
        message = "Editor changed. Run the command again.";
    } else {
        message = prepare(scene, catalog, ops, &edits);
    }
    if (!message) {
        message = apply(scene, catalog, edits, yyjson_get_bool(yyjson_obj_get(proposal, "dry_run")));
    }
    snprintf(error, size, "%s", message ? message : "Applied");
    afree(edits);
    yyjson_doc_free(catalog_doc);
    yyjson_doc_free(proposal_doc);
    yyjson_doc_free(fresh_doc);
    free(fresh);
    return message ? -1 : 0;
}
