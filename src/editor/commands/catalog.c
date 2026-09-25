#include "editor/commands/internal.h"
#include "editor/panels/scene_properties.h"
#include "editor/ui/labels.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/serialize_internal.h"
#include "graph/model/registry.h"
#include "support/xalloc.h"

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
    /* Enums are spelled by label, as in documents. */
    if (old.type == PIN_ENUM && yyjson_is_str(json)) {
        int value;
        if (!registry_enum_value(old.type_name, yyjson_get_str(json), &value)) {
            return false;
        }
        out->e = value;
        return true;
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
    if (!choices) {
        return field;
    }
    yyjson_mut_val *options = yyjson_mut_obj_add_arr(doc, field, "choices");
    for (int i = 0; i < editor_choices_count(choices); i++) {
        editor_choice choice = editor_choice_at(choices, i);
        const char *spelled = choice.name;
        if (value.type == PIN_ENUM) {
            spelled = registry_enum_label(value.type_name, choice.value);
        }
        yyjson_mut_val *option = yyjson_mut_arr_add_obj(doc, options);
        yyjson_mut_obj_add_strcpy(doc, option, "label", choice.label);
        if (spelled) {
            yyjson_mut_obj_add_strcpy(doc, option, "value", spelled);
        } else {
            yyjson_mut_obj_add_int(doc, option, "value", choice.value);
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
                yyjson_mut_obj_remove_key(field, "choices");
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

static char *entity_id(const char *scene_id, ecs_entity entity)
{
    size_t size = strlen(scene_id) + 32;
    char *id = xmalloc(size);
    snprintf(id, size, "entity:%s:%llu", scene_id, (unsigned long long)ecs_entity_to_u64(entity));
    return id;
}

static void add_capabilities(app_scene_authoring *scene, yyjson_mut_doc *doc, yyjson_mut_val *root)
{
    static const char *const verbs[] = { "open",    "select",    "set",
                                         "assert",  "duplicate", "connect",
                                         "runtime", "save" };
    static const char *const ui_verbs[] = { "panel", "frame", "undo", "redo" };
    yyjson_mut_val *capabilities = yyjson_mut_obj_add_arr(doc, root, "capabilities");
    for (size_t i = 0; i < sizeof verbs / sizeof verbs[0]; i++) {
        yyjson_mut_arr_add_str(doc, capabilities, verbs[i]);
    }
    for (size_t i = 0;
         command_hooks(scene) && i < sizeof ui_verbs / sizeof ui_verbs[0];
         i++) {
        yyjson_mut_arr_add_str(doc, capabilities, ui_verbs[i]);
    }
}

static void add_assets(app_scene_authoring *scene, yyjson_mut_doc *doc, yyjson_mut_val *objects)
{
    static const char *kinds[] = { "scene", "prototype", "behavior",
                                   "event", "material",  "shape",
                                   "field", "library",   "system" };
    scene_prototype_asset *open = scene->document.kind == APP_EDITOR_DOCUMENT_PROTOTYPE
                                    ? selected_prototype(scene)
                                    : NULL;
    for (int i = 0; i < asset_index_asset_count(scene->asset_index); i++) {
        const asset_index_asset *asset = asset_index_asset_at(scene->asset_index, i);
        if (asset->unresolved) {
            continue;
        }
        bool selected = asset->kind == ASSET_INDEX_PROTOTYPE && open &&
                        !strcmp(open->prototype->name, asset->name);
        yyjson_mut_val *object = add_object(
            doc,
            objects,
            asset->identity,
            asset->name,
            kinds[asset->kind],
            asset->identity,
            selected
        );
        yyjson_mut_obj_add_strcpy(doc, object, "path", asset->path ? asset->path : "");
        yyjson_mut_obj_add_int(doc, object, "asset_kind", asset->kind);
        scene_prototype_asset *prototype = asset->kind == ASSET_INDEX_PROTOTYPE
                                             ? find_prototype(scene, asset->name)
                                             : NULL;
        if (prototype) {
            component_fields(scene, doc, object, prototype->prototype, NULL);
        }
    }
}

static void add_actors(
    app_scene_authoring *scene,
    yyjson_mut_doc *doc,
    yyjson_mut_val *objects,
    const char *scene_id
)
{
    bool scene_open = scene->document.kind == APP_EDITOR_DOCUMENT_SCENE;
    for (int i = 0; i < acount(scene->actors); i++) {
        const scene_actor *actor = &scene->actors[i];
        char *id = entity_id(scene_id, actor->entity);
        yyjson_mut_val *object = add_object(
            doc,
            objects,
            id,
            actor->name,
            "entity",
            scene_id,
            scene_open && (actor->marked || scene->selected == i)
        );
        free(id);
        yyjson_mut_obj_add_strcpy(doc, object, "prototype", actor->prototype_name);
        yyjson_mut_obj_add_uint(doc, object, "entity", ecs_entity_to_u64(actor->entity));
        component_fields(scene, doc, object, actor->base, actor);
    }
}

/* Pins are keyed by name per side, as in documents; a repeated name gains its
 * occurrence. */
static void pin_field_id(const node *n, bool output, int p, char *out, size_t size)
{
    const pin_spec *pins = output ? n->output_pins : n->input_pins;
    int repeat = 0;
    for (int i = 0; i < p; i++) {
        repeat += !strcmp(pins[i].name, pins[p].name);
    }
    int written = snprintf(out, size, "%s.%s", output ? "out" : "in", pins[p].name);
    if (repeat && written > 0 && (size_t)written < size) {
        snprintf(out + written, size - (size_t)written, "#%d", repeat + 1);
    }
}

static void add_pin_fields(editor *editor, yyjson_mut_doc *doc, yyjson_mut_val *object, node *n)
{
    yyjson_mut_val *fields = yyjson_mut_obj_add_arr(doc, object, "fields");
    for (int output = 0; output < 2; output++) {
        const pin_spec *pins = output ? n->output_pins : n->input_pins;
        int count = output ? n->output_count : n->input_count;
        for (int p = 0; p < count; p++) {
            const pin_spec *pin = &pins[p];
            char id[160];
            pin_field_id(n, output, p, id, sizeof id);
            const char *kind = pin->kind == PIN_EXEC ? "flow"
                             : ed_pin_color_input(editor, n, output, p)
                                 ? "color"
                                 : value_kind(pin->default_value);
            yyjson_mut_val *field = add_field(
                doc,
                fields,
                id,
                pin->name,
                pin->name,
                pin->default_value,
                kind,
                ed_pin_choices(editor, n, output, p)
            );
            if (!field) {
                continue;
            }
            int side = output ? ED_PIN_OUTPUT : ED_PIN_INPUT;
            yyjson_mut_obj_add_bool(
                doc,
                field,
                "editable",
                ed_pin_editable(editor->active_graph, n, side, p)
            );
            yyjson_mut_obj_add_int(doc, field, "index", p);
            yyjson_mut_obj_add_bool(doc, field, "output", output);
            yyjson_mut_obj_add_int(doc, field, "pin_kind", pin->kind);
        }
    }
}

/* The graph open in the graph editor while a graph document is active. */
static void add_graph(app_scene_authoring *scene, yyjson_mut_doc *doc, yyjson_mut_val *root, yyjson_mut_val *objects)
{
    editor *editor = scene->graph_editor;
    graph *graph = scene->graph_document_id ? editor->active_graph : NULL;
    if (!graph) {
        return;
    }
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
        node *n = graph->nodes[i];
        char id[96], title[256];
        snprintf(id, sizeof id, "%s:node:%llu", graph_id, (unsigned long long)n->id);
        yyjson_mut_val *object = add_object(
            doc,
            objects,
            id,
            ed_node_title(editor, n, title, sizeof title),
            "node",
            graph_id,
            ed_node_selected(editor, n)
        );
        yyjson_mut_obj_add_uint(doc, object, "node", n->id);
        add_pin_fields(editor, doc, object, n);
    }
}

static void add_relationships(
    app_scene_authoring *scene,
    yyjson_mut_doc *doc,
    yyjson_mut_val *root,
    const char *scene_id
)
{
    const asset_index *index = scene->asset_index;
    yyjson_mut_val *references = yyjson_mut_obj_add_arr(doc, root, "relationships");
    for (int i = 0; i < asset_index_reference_count(index); i++) {
        const asset_index_reference *reference = asset_index_reference_at(index, i);
        const char *detail = reference->detail ? reference->detail : "reference";
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, references);
        yyjson_mut_obj_add_strcpy(
            doc,
            item,
            "source",
            asset_index_asset_at(index, reference->source)->identity
        );
        yyjson_mut_obj_add_strcpy(
            doc,
            item,
            "target",
            asset_index_asset_at(index, reference->target)->identity
        );
        yyjson_mut_obj_add_strcpy(doc, item, "label", detail);
    }
    for (int i = 0; i < acount(scene->actors); i++) {
        const scene_actor *actor = &scene->actors[i];
        int prototype = asset_index_find_name(
            index,
            ASSET_INDEX_PROTOTYPE,
            actor->prototype_name
        );
        if (prototype < 0) {
            continue;
        }
        char *id = entity_id(scene_id, actor->entity);
        yyjson_mut_val *item = yyjson_mut_arr_add_obj(doc, references);
        yyjson_mut_obj_add_strcpy(doc, item, "source", id);
        yyjson_mut_obj_add_strcpy(
            doc,
            item,
            "target",
            asset_index_asset_at(index, prototype)->identity
        );
        yyjson_mut_obj_add_str(doc, item, "label", "prototype");
        free(id);
    }
}

static void add_panels(app_scene_authoring *scene, yyjson_mut_doc *doc, yyjson_mut_val *root)
{
    const editor_command_hooks *hooks = command_hooks(scene);
    yyjson_mut_val *panels = yyjson_mut_obj_add_arr(doc, root, "panels");
    for (int i = 0; hooks && i < hooks->panel_count; i++) {
        yyjson_mut_val *panel = yyjson_mut_arr_add_obj(doc, panels);
        char id[32];
        snprintf(id, sizeof id, "panel:%d", i);
        yyjson_mut_obj_add_strcpy(doc, panel, "id", id);
        yyjson_mut_obj_add_str(doc, panel, "name", hooks->panels[i]);
        yyjson_mut_obj_add_bool(
            doc,
            panel,
            "visible",
            hooks->panel_visible(hooks->user, i)
        );
    }
}

char *editor_commands_catalog(app_scene_authoring *scene)
{
    app_scene_authoring_refresh_asset_index(scene);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_int(doc, root, "contract", 2);
    yyjson_mut_obj_add_uint(doc, root, "incarnation", command_incarnation(scene));
    add_capabilities(scene, doc, root);
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
    bool playing = scene->runtime_snapshot_valid || scene->preview_playing;
    yyjson_mut_obj_add_bool(doc, root, "playing", playing);
    yyjson_mut_val *objects = yyjson_mut_obj_add_arr(doc, root, "objects");
    add_assets(scene, doc, objects);
    add_actors(scene, doc, objects, scene_id);
    add_graph(scene, doc, root, objects);
    add_relationships(scene, doc, root, scene_id);
    add_panels(scene, doc, root);
    char *json = yyjson_mut_write(doc, 0, NULL), revision[17];
    command_hash(json, revision);
    free(json);
    yyjson_mut_obj_add_strcpy(doc, root, "revision", revision);
    json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}
