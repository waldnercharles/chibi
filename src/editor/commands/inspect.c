#include "editor/commands/internal.h"
#include "graph/document/serialize_internal.h"
#include "graph/model/registry.h"

#include <stdlib.h>
#include <string.h>

static bool requested(yyjson_val *kinds, const char *kind)
{
    for (int i = 0; i < (int)yyjson_arr_size(kinds); i++) {
        const char *value = yyjson_get_str(yyjson_arr_get(kinds, i));
        if (value && !strcmp(value, kind)) {
            return true;
        }
    }
    return false;
}

static bool matches(const char *name, const char *query)
{
    return !*query || (name && strcasestr(name, query));
}

static void pins(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, const pin_spec *specs, int count)
{
    yyjson_mut_val *values = yyjson_mut_obj_add_arr(doc, object, key);
    for (int i = 0; i < count; i++) {
        const pin_spec *pin = &specs[i];
        yyjson_mut_val *field = yyjson_mut_arr_add_obj(doc, values);
        yyjson_mut_obj_add_str(doc, field, "name", pin->name ? pin->name : "");
        yyjson_mut_obj_add_int(doc, field, "index", i);
        yyjson_mut_obj_add_str(doc, field, "kind", pin->kind == PIN_EXEC ? "exec" : "data");
        yyjson_mut_obj_add_int(doc, field, "type", pin->type);
        if (pin->type_name) {
            yyjson_mut_obj_add_str(doc, field, "type_name", pin->type_name);
        }
    }
}

char *command_inspect(app_scene_authoring *scene, const char *query, yyjson_val *kinds)
{
    char *catalog = editor_commands_catalog(scene);
    yyjson_doc *read = yyjson_read(catalog, strlen(catalog), 0);
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(read, NULL);
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    if (requested(kinds, "nodes")) {
        yyjson_mut_val *nodes = yyjson_mut_obj_add_arr(doc, root, "node_types");
        for (int i = 0; i < registry_node_count(); i++) {
            const node_desc *node = registry_node(i);
            if (!matches(node->name, query)) {
                continue;
            }
            yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, nodes);
            yyjson_mut_obj_add_str(doc, object, "name", node->name);
            pins(doc, object, "inputs", node->inputs, node->input_count);
            pins(doc, object, "outputs", node->outputs, node->output_count);
        }
    }
    if (requested(kinds, "components")) {
        yyjson_mut_val *components = yyjson_mut_obj_add_arr(doc, root, "component_types");
        for (int i = 0; i < scene->registry.component_count; i++) {
            const ecs_component_desc *component = scene->registry.components[i];
            if (!matches(component->name, query)) {
                continue;
            }
            yyjson_mut_val *object = yyjson_mut_arr_add_obj(doc, components);
            yyjson_mut_obj_add_str(doc, object, "name", component->name);
            yyjson_mut_obj_add_str(doc, object, "key", component->key);
            yyjson_mut_val *fields = yyjson_mut_obj_add_arr(doc, object, "fields");
            for (int f = 0; f < component->type->field_count; f++) {
                const type_field *spec = &component->type->fields[f];
                yyjson_mut_val *field = yyjson_mut_arr_add_obj(doc, fields);
                yyjson_mut_obj_add_str(doc, field, "name", spec->name);
                yyjson_mut_obj_add_int(doc, field, "type", spec->type);
                if (spec->type_name) {
                    yyjson_mut_obj_add_str(doc, field, "type_name", spec->type_name);
                }
            }
        }
    }
    yyjson_mut_val *authoring = yyjson_mut_obj_add_obj(doc, root, "authoring");
    yyjson_mut_obj_add_str(doc, authoring, "create_project", "name: unique folder under projects/. Creates Main scene. Native editor only; reopen with --project-root.");
    yyjson_mut_obj_add_str(doc, authoring, "create_asset", "name, kind, definition. Prototype definition: components and behaviors. Scene definition: entities with name, prototype, components, behaviors; optional physics and presentation. Graph definition: owner_type and operations. Creates and opens a new saved asset; existing assets are never replaced. Asset creation has no document Undo.");
    yyjson_mut_obj_add_str(doc, authoring, "edit_graph", "target: observed asset ID of open graph, definition: graph_id or graph_name and operations. Semantic operations are validated and arranged, with one Undo step; save separately.");
    yyjson_mut_obj_add_str(doc, authoring, "graph_operations", "add_node uses type (reflected node name), ref (batch identity), inputs (pin names to literals); Event nodes also use event_name. connect uses from,from_pin,to,to_pin and kind exec/data. add_variable uses name,type,default. set_pin uses node,side (input/output),pin,type and optional type_name; inspect pin types before connecting. bind_input uses node,pin,kind (component/variable/delta_time), with component key/field or variable name. Max 128 operations per graph edit.");
    yyjson_mut_obj_add_str(doc, authoring, "set_start_scene", "target: observed scene asset ID. Saves this scene as the project startup scene in project.json.");
    yyjson_mut_obj_add_str(doc, authoring, "inspect", "kinds:[nodes] or [components], optional query filters reflected names. Default returns current objects, values and revision.");
    yyjson_mut_obj_add_str(doc, authoring, "input", "action: enabled GameActionBinding action name, pressed: boolean, frames: optional 1-600 (default 1). Holds the bound control for that many input samples, then releases it. Requires scene playback. Stop clears held input.");
    yyjson_mut_obj_add_str(doc, authoring, "capture", "path: local PNG filename. Captures the rendered editor including the playing scene.");
    yyjson_mut_obj_add_str(doc, authoring, "transactions", "Submit one authoring operation per transaction, with the latest inspect revision. dry_run validates without mutation. Asset definitions are validated before publication.");
    char *result = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    yyjson_doc_free(read);
    free(catalog);
    return result;
}
