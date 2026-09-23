#include "editor/commands/internal.h"
#include "editor/panels/scene_properties.h"
#include "editor/ui/labels.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/serialize_internal.h"
#include "support/xalloc.h"

#include <SDL3/SDL.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *command_string(yyjson_val *object, const char *key)
{
    const char *value = yyjson_get_str(yyjson_obj_get(object, key));
    return value ? value : "";
}

yyjson_val *command_find(yyjson_val *array, const char *id)
{
    size_t i, count;
    yyjson_val *item;
    yyjson_arr_foreach(array, i, count, item)
    {
        if (!strcmp(command_string(item, "id"), id)) {
            return item;
        }
    }
    return NULL;
}

void command_hash(const char *json, char out[17])
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)json; *p; p++) {
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    }
    snprintf(out, 17, "%016llx", (unsigned long long)hash);
}

bool command_value(yyjson_val *json, pin_value old, pin_value *out)
{
    *out = old;
    if (old.type == PIN_BOOL) {
        out->b = yyjson_get_bool(json);
        return yyjson_is_bool(json);
    }
    if (old.type == PIN_NAME || old.type == PIN_STR) {
        out->str = yyjson_get_str(json);
        return yyjson_is_str(json) && yyjson_get_len(json) <= 4096;
    }
    if (!yyjson_is_num(json)) {
        return false;
    }
    double value = yyjson_get_num(json);
    if (!isfinite(value)) {
        return false;
    }
    if (old.type == PIN_F32) {
        out->f = (float)value;
        return fabs(value) <= FLT_MAX;
    }
    if (old.type == PIN_F64) {
        out->d = value;
        return true;
    }
    if (floor(value) != value) {
        return false;
    }
#define NUMBER(kind, member, minimum, maximum)                                 \
    case kind:                                                                 \
        if (value < (double)(minimum) || value > (double)(maximum))            \
            return false;                                                      \
        out->member = value;                                                   \
        return true
    switch (old.type) {
        NUMBER(PIN_I8, i8, INT8_MIN, INT8_MAX);
        NUMBER(PIN_U8, u8, 0, UINT8_MAX);
        NUMBER(PIN_I16, i16, INT16_MIN, INT16_MAX);
        NUMBER(PIN_U16, u16, 0, UINT16_MAX);
        NUMBER(PIN_I32, i, INT32_MIN, INT32_MAX);
        NUMBER(PIN_U32, u, 0, UINT32_MAX);
        NUMBER(PIN_ENUM, e, INT32_MIN, INT32_MAX);
        default: return false;
    }
#undef NUMBER
}

static const char *value_kind(pin_value value)
{
    if (value.container != PIN_CONTAINER_SINGLE) {
        return NULL;
    }
    switch (value.type) {
        case PIN_BOOL: return "bool";
        case PIN_NAME:
        case PIN_STR: return "text";
        case PIN_I8:
        case PIN_U8:
        case PIN_I16:
        case PIN_U16:
        case PIN_I32:
        case PIN_U32:
        case PIN_F32:
        case PIN_F64:
        case PIN_ENUM: return "number";
        default: return NULL;
    }
}

static yyjson_mut_val *add_object(
    yyjson_mut_doc *doc,
    yyjson_mut_val *objects,
    const char *id,
    const char *name,
    const char *kind,
    const char *document,
    bool selected
)
{
    yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, objects);
    yyjson_mut_obj_add_strcpy(doc, object, "id", id);
    yyjson_mut_obj_add_strcpy(doc, object, "name", name);
    yyjson_mut_obj_add_strcpy(doc, object, "kind", kind);
    yyjson_mut_obj_add_strcpy(doc, object, "document", document);
    yyjson_mut_obj_add_bool(doc, object, "selected", selected);
    return object;
}

static yyjson_mut_val *add_field(
    yyjson_mut_doc *doc,
    yyjson_mut_val *fields,
    const char *id,
    const char *name,
    const char *label,
    pin_value value,
    const char *kind,
    const editor_choices *choices
)
{
    if (!kind) {
        return NULL;
    }
    yyjson_mut_val *field = yyjson_mut_arr_add_obj(doc, fields);
    yyjson_mut_obj_add_strcpy(doc, field, "id", id);
    yyjson_mut_obj_add_strcpy(doc, field, "name", name);
    yyjson_mut_obj_add_strcpy(doc, field, "label", label);
    yyjson_mut_obj_add_str(doc, field, "type", choices ? "choice" : kind);
    yyjson_mut_obj_add_int(doc, field, "pin_type", value.type);
    yyjson_mut_obj_add_bool(doc, field, "integer", value.type != PIN_F32 && value.type != PIN_F64);
    serialize_write_value(doc, field, "value", value.type, value.type_name, &value);
    if (choices) {
        yyjson_mut_val *options = yyjson_mut_obj_add_arr(doc, field, "choices");
        int count = choices->enumeration ? choices->enumeration->value_count
                                         : choices->count;
        for (int i = 0; i < count; i++) {
            editor_choice item;
            if (choices->enumeration) {
                const enum_value_desc *value = &choices->enumeration->values[i];
                item = (editor_choice){ .value = value->value, .label = value->label };
            } else {
                item = choices->items[i];
            }
            const editor_choice *choice = &item;
            yyjson_mut_val *option = yyjson_mut_arr_add_obj(doc, options);
            yyjson_mut_obj_add_strcpy(doc, option, "label", choice->label);
            if (choice->name) {
                yyjson_mut_obj_add_strcpy(doc, option, "value", choice->name);
            } else {
                yyjson_mut_obj_add_int(doc, option, "value", choice->value);
            }
        }
    }
    return field;
}

static void component_fields(
    app_scene_authoring *scene,
    yyjson_mut_doc *doc,
    yyjson_mut_val *object,
    const game_prototype *prototype,
    const scene_actor *actor
)
{
    yyjson_mut_val *fields = yyjson_mut_obj_add_arr(doc, object, "fields");
    yyjson_mut_val *components = yyjson_mut_obj_add_arr(doc, object, "components");
    for (int c = 0; c < scene->registry.component_count; c++) {
        const ecs_component_desc *desc = scene->registry.components[c];
        const game_prototype_component *component = prototype_component_by_id(prototype, desc->id);
        if (actor ? !ecs_component_has(scene->world, actor->entity, desc->id)
                  : !component) {
            continue;
        }
        yyjson_mut_arr_add_strcpy(doc, components, desc->name);
        for (int f = 0; f < desc->type->field_count; f++) {
            const type_field *spec = &desc->type->fields[f];
            pin_value value, inherited;
            if (actor) {
                if (ecs_component_read_field(scene->world, actor->entity, desc->id, f, &value)) {
                    continue;
                }
            } else {
                pin_read_field(component->data, spec, &value);
            }
            const char *kind = appearance_color_field(desc->type, spec)
                                 ? "color"
                                 : value_kind(value);
            if (!kind || (component &&
                          !property_field_visible(desc, spec, component->data))) {
                continue;
            }
            char id[256], label[256], component_label[96], field_label[96];
            snprintf(id, sizeof id, "%s.%s", desc->key, spec->name);
            editor_component_label(desc->name, component_label, sizeof component_label);
            editor_identifier_label(spec->name, field_label, sizeof field_label);
            snprintf(label, sizeof label, "%s %s", component_label, field_label);
            yyjson_mut_val *field = add_field(
                doc,
                fields,
                id,
                spec->name,
                label,
                value,
                kind,
                property_choices(desc->type, spec)
            );
            asset_index_kind asset_kind;
            if (asset_index_component_kind(desc->name, spec->name, &asset_kind)) {
                yyjson_mut_obj_remove_key(field, "type");
                yyjson_mut_obj_add_str(doc, field, "type", "choice");
                yyjson_mut_obj_add_bool(doc, field, "asset_reference", true);
                yyjson_mut_val *choices = yyjson_mut_obj_add_arr(doc, field, "choices");
                for (int a = 0; a < asset_index_asset_count(scene->asset_index); a++) {
                    const asset_index_asset *asset = asset_index_asset_at(scene->asset_index, a);
                    if (asset->kind != asset_kind || asset->unresolved) {
                        continue;
                    }
                    yyjson_mut_val *choice = yyjson_mut_arr_add_obj(doc, choices);
                    yyjson_mut_obj_add_strcpy(doc, choice, "label", asset->name);
                    yyjson_mut_obj_add_strcpy(doc, choice, "value", asset->name);
                }
            }
            yyjson_mut_obj_add_str(doc, field, "component", desc->name);
            yyjson_mut_obj_add_str(doc, field, "component_key", desc->key);
            yyjson_mut_obj_add_int(doc, field, "field_index", f);
            if (actor && component) {
                pin_read_field(component->data, spec, &inherited);
                serialize_write_value(
                    doc,
                    field,
                    "inherited",
                    inherited.type,
                    inherited.type_name,
                    &inherited
                );
            }
        }
    }
}

static char *entity_identity(const char *scene_id, ecs_entity entity)
{
    size_t size = strlen(scene_id) + 32;
    char *id = xmalloc(size);
    snprintf(id, size, "entity:%s:%llu", scene_id, (unsigned long long)ecs_entity_to_u64(entity));
    return id;
}

char *editor_commands_catalog(app_scene_authoring *scene)
{
    app_scene_authoring_refresh_asset_index(scene);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "contract", 2);
    if (!scene->command_incarnation) {
        scene->command_incarnation = SDL_GetPerformanceCounter();
    }
    yyjson_mut_obj_add_uint(doc, root, "incarnation", scene->command_incarnation);
    yyjson_mut_val *capabilities = yyjson_mut_obj_add_arr(doc, root, "capabilities");
    const char *verbs[] = { "open",
                            "select",
                            "set",
                            "assert",
                            "duplicate",
                            "connect",
                            "panel",
                            "frame",
                            "runtime",
                            "save",
                            "undo",
                            "redo",
                            "set_start_scene",
                            "create_project",
                            "create_asset",
                            "edit_graph",
                            "input",
                            "capture" };
    for (int i = 0; i < (int)(sizeof verbs / sizeof verbs[0]); i++) {
        yyjson_mut_arr_add_str(doc, capabilities, verbs[i]);
    }
    const char *scene_id = scene->current_scene_tab_key ? scene->current_scene_tab_key
                                                        : APP_SCENE_DOCUMENT_ID;
    const char *document_id = scene->document.kind == APP_EDITOR_DOCUMENT_SCENE ? scene_id
                            : scene->document.id ? scene->document.id
                                                 : "";
    size_t document_size = strlen(document_id) + 32;
    char *document = xmalloc(document_size);
    snprintf(document, document_size, "%d:%s", scene->document.kind, document_id);
    yyjson_mut_obj_add_strcpy(doc, root, "document", document);
    free(document);
    yyjson_mut_obj_add_strcpy(doc, root, "project", scene->content_root);
    yyjson_mut_obj_add_bool(
        doc,
        root,
        "playing",
        scene->runtime_snapshot_valid || scene->preview_playing
    );
    yyjson_mut_val *objects = yyjson_mut_obj_add_arr(doc, root, "objects");
    static const char *kinds[] = { "scene", "prototype", "behavior",
                                   "event", "material",  "shape",
                                   "field", "library",   "system" };
    for (int i = 0; i < asset_index_asset_count(scene->asset_index); i++) {
        const asset_index_asset *asset = asset_index_asset_at(scene->asset_index, i);
        if (asset->unresolved) {
            continue;
        }
        yyjson_mut_val *object = add_object(
            doc,
            objects,
            asset->identity,
            asset->name,
            kinds[asset->kind],
            asset->identity,
            asset->kind == ASSET_INDEX_PROTOTYPE && selected_prototype(scene) &&
                !strcmp(selected_prototype(scene)->prototype->name, asset->name) &&
                scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE
        );
        yyjson_mut_obj_add_strcpy(doc, object, "path", asset->path ? asset->path : "");
        yyjson_mut_obj_add_int(doc, object, "asset_kind", asset->kind);
        if (asset->kind == ASSET_INDEX_PROTOTYPE) {
            scene_prototype_asset *prototype = find_prototype(scene, asset->name);
            if (prototype) {
                component_fields(scene, doc, object, prototype->prototype, NULL);
            }
        }
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        const scene_actor *actor = &scene->actors[i];
        char *id = entity_identity(scene_id, actor->entity);
        yyjson_mut_val *object = add_object(
            doc,
            objects,
            id,
            actor->name,
            "entity",
            scene_id,
            scene->document.kind == APP_EDITOR_DOCUMENT_SCENE &&
                (actor->marked || scene->selected == i)
        );
        yyjson_mut_obj_add_strcpy(doc, object, "prototype", actor->prototype_name);
        yyjson_mut_obj_add_uint(doc, object, "entity", ecs_entity_to_u64(actor->entity));
        component_fields(scene, doc, object, actor->base, actor);
        free(id);
    }
    editor *editor = scene->graph_editor;
    graph *graph = editor ? editor->active_graph : NULL;
    if (graph) {
        char graph_id[64];
        snprintf(
            graph_id,
            sizeof graph_id,
            "graph:%llu:%llu",
            (unsigned long long)editor->asset->identity,
            (unsigned long long)graph->graph_id
        );
        /* Pending edits must affect the revision before the next editor tick.
         * Serialized content also covers connections absent from field values. */
        char *snapshot = ed_snapshot(editor), hash[17];
        command_hash(snapshot, hash);
        free(snapshot);
        yyjson_mut_obj_add_strcpy(doc, root, "graph_hash", hash);
        for (int i = 0; i < acount(graph->nodes); i++) {
            node *node = graph->nodes[i];
            char id[96], title[256];
            snprintf(
                id,
                sizeof id,
                "%s:node:%llu",
                graph_id,
                (unsigned long long)node->id
            );
            const char *name = ed_node_title(editor, node, title, sizeof title);
            yyjson_mut_val *object = add_object(
                doc,
                objects,
                id,
                name,
                "node",
                graph_id,
                ed_node_selected(editor, node)
            );
            yyjson_mut_obj_add_uint(doc, object, "node", node->id);
            yyjson_mut_val *fields = yyjson_mut_obj_add_arr(doc, object, "fields");
            for (int output = 0; output < 2; output++) {
                const pin_spec *pins = output ? node->output_pins : node->input_pins;
                int count = output ? node->output_count : node->input_count;
                for (int p = 0; p < count; p++) {
                    const pin_spec *pin = &pins[p];
                    snprintf(id, sizeof id, "pin:%llu", (unsigned long long)pin->id);
                    yyjson_mut_val *field = add_field(
                        doc,
                        fields,
                        id,
                        pin->name,
                        pin->name,
                        pin->default_value,
                        pin->kind == PIN_EXEC ? "flow"
                        : ed_pin_color_input(editor, node, output, p)
                            ? "color"
                            : value_kind(pin->default_value),
                        ed_pin_choices(editor, node, output, p)
                    );
                    if (!field) {
                        continue;
                    }
                    yyjson_mut_obj_add_bool(
                        doc,
                        field,
                        "editable",
                        ed_pin_editable(graph, node, output ? ED_PIN_OUTPUT : ED_PIN_INPUT, p)
                    );
                    yyjson_mut_obj_add_int(doc, field, "index", p);
                    yyjson_mut_obj_add_uint(doc, field, "pin", pin->id);
                    yyjson_mut_obj_add_bool(doc, field, "output", output);
                    yyjson_mut_obj_add_int(doc, field, "pin_kind", pin->kind);
                }
            }
        }
    }
    yyjson_mut_val *references = yyjson_mut_obj_add_arr(doc, root, "relationships");
    for (int i = 0; i < asset_index_reference_count(scene->asset_index); i++) {
        const asset_index_reference *reference = asset_index_reference_at(scene->asset_index, i);
        const asset_index_asset
            *source = asset_index_asset_at(scene->asset_index, reference->source);
        const asset_index_asset
            *target = asset_index_asset_at(scene->asset_index, reference->target);
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, references);
        yyjson_mut_obj_add_strcpy(doc, item, "source", source->identity);
        yyjson_mut_obj_add_strcpy(doc, item, "target", target->identity);
        yyjson_mut_obj_add_strcpy(
            doc,
            item,
            "label",
            reference->detail ? reference->detail : "reference"
        );
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        const scene_actor *actor = &scene->actors[i];
        for (int a = 0; a < asset_index_asset_count(scene->asset_index); a++) {
            const asset_index_asset *asset = asset_index_asset_at(scene->asset_index, a);
            if (asset->kind != ASSET_INDEX_PROTOTYPE ||
                strcmp(asset->name, actor->prototype_name)) {
                continue;
            }
            char *id = entity_identity(scene_id, actor->entity);
            yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, references);
            yyjson_mut_obj_add_strcpy(doc, item, "source", id);
            yyjson_mut_obj_add_strcpy(doc, item, "target", asset->identity);
            yyjson_mut_obj_add_str(doc, item, "label", "prototype");
            free(id);
        }
    }
    yyjson_mut_val *panels = yyjson_mut_obj_add_arr(doc, root, "panels");
    static const char *names[] = { "Content",   "Outliner", "Behavior",
                                   "Inspector", "Graph",    "Scene",
                                   "Compiler",  "Output",   "Performance" };
    for (int i = 0; i < EDITOR_UI_PANE_COUNT; i++) {
        yyjson_mut_val *panel = yyjson_mut_arr_add_obj(doc, panels);
        char id[32];
        snprintf(id, sizeof id, "panel:%d", i);
        yyjson_mut_obj_add_strcpy(doc, panel, "id", id);
        yyjson_mut_obj_add_str(doc, panel, "name", names[i]);
        yyjson_mut_obj_add_bool(doc, panel, "visible", editor_ui_pane_visible(i));
    }
    char *json = yyjson_mut_write(doc, 0, NULL), revision[17];
    command_hash(json, revision);
    free(json);
    yyjson_mut_obj_add_strcpy(doc, root, "revision", revision);
    json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}
