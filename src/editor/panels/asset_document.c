#include "editor/ui/ui_shell.h"
#include "editor/workspace/scene_internal.h"

#include <imgui/dcimgui.h>
#include <stdio.h>
#include <string.h>
#include <yyjson.h>

static const char *reference_kind_label(asset_index_reference_kind kind)
{
    switch (kind) {
        case ASSET_INDEX_REFERENCE: return "Reference";
        case ASSET_INDEX_EVENT_CONSUMER: return "Consumer";
        case ASSET_INDEX_EVENT_EMITTER: return "Emitter";
        case ASSET_INDEX_BEHAVIOR_ATTACHMENT: return "Attached behavior";
        case ASSET_INDEX_COMPONENT_BINDING: return "Component";
        case ASSET_INDEX_GRAPH_CALL: return "Call";
        case ASSET_INDEX_PROTOTYPE_SPAWN: return "Spawn";
        case ASSET_INDEX_VARIABLE_READ: return "Read";
        case ASSET_INDEX_VARIABLE_WRITE: return "Write";
    }
    return "Usage";
}

static void open_reference_source(
    app_scene_authoring *scene,
    const asset_index_reference *reference,
    int reference_position
)
{
    const asset_index *index = app_scene_authoring_asset_index(scene);
    const asset_index_asset *source = asset_index_asset_at(index, reference->source);
    if (!source || app_scene_authoring_open_asset(scene, reference->source) != 0) {
        return;
    }
    if (reference->node_id && scene->graph_editor &&
        (source->kind == ASSET_INDEX_BEHAVIOR ||
         source->kind == ASSET_INDEX_EVENT || source->kind == ASSET_INDEX_LIBRARY ||
         source->kind == ASSET_INDEX_MATERIAL || source->kind == ASSET_INDEX_FIELD ||
         source->kind == ASSET_INDEX_SHAPE || source->kind == ASSET_INDEX_SYSTEM)) {
        editor_reveal_node_by_id(
            scene->graph_editor,
            reference->graph_id,
            reference->node_id
        );
    }
    if (source->kind == ASSET_INDEX_SCENE && reference->actor) {
        for (int i = 0; i < acount(scene->actors); i++) {
            if (scene->actors[i].name &&
                !strcmp(scene->actors[i].name, reference->actor)) {
                select_instance(scene, i);
                break;
            }
        }
    }
    (void)reference_position;
}

static void draw_reference_row(app_scene_authoring *scene, const asset_index_reference *reference, int position)
{
    const asset_index *index = app_scene_authoring_asset_index(scene);
    const asset_index_asset *source = asset_index_asset_at(index, reference->source);
    if (!source) {
        return;
    }
    char label[256];
    snprintf(
        label,
        sizeof label,
        "%s  %s%s%s",
        source->name,
        reference_kind_label(reference->kind),
        reference->detail ? "  " : "",
        reference->detail ? reference->detail : ""
    );
    char id[96];
    snprintf(id, sizeof id, "asset.reference.%d", position);
    ImGui_PushID(id);
    if (ImGui_SelectableEx(label, false, 0, (ImVec2){ 0, 0 })) {
        open_reference_source(scene, reference, position);
    }
    ImGui_PopID();
    editor_ui_track_item(id);
    if (reference->component || reference->field || reference->actor) {
        ImGui_SameLine();
        ImGui_TextDisabled(
            "%s%s%s%s%s",
            reference->actor ? reference->actor : "",
            reference->actor && reference->component ? "  " : "",
            reference->component ? reference->component : "",
            reference->field ? "." : "",
            reference->field ? reference->field : ""
        );
    }
}

static void draw_reference_target(
    app_scene_authoring *scene,
    const asset_index_reference *reference,
    int position
)
{
    const asset_index *index = app_scene_authoring_asset_index(scene);
    const asset_index_asset *target = asset_index_asset_at(index, reference->target);
    if (!target) {
        return;
    }
    char label[256];
    snprintf(
        label,
        sizeof label,
        "%s  %s%s%s",
        target->name,
        reference_kind_label(reference->kind),
        reference->detail ? "  " : "",
        reference->detail ? reference->detail : ""
    );
    char id[96];
    snprintf(id, sizeof id, "asset.reference.target.%d", position);
    ImGui_PushID(id);
    if (ImGui_SelectableEx(label, false, 0, (ImVec2){ 0, 0 })) {
        app_scene_authoring_open_asset(scene, reference->target);
    }
    ImGui_PopID();
    editor_ui_track_item(id);
}

void draw_asset_references(app_scene_authoring *scene, int asset_position)
{
    const asset_index *index = app_scene_authoring_asset_index(scene);
    int usage_count = 0;
    for (int i = 0; index && i < asset_index_reference_count(index); i++) {
        const asset_index_reference *reference = asset_index_reference_at(index, i);
        if (reference->target != asset_position) {
            continue;
        }
        if (!usage_count) {
            ImGui_SeparatorText("Used by");
        }
        draw_reference_row(scene, reference, i);
        usage_count++;
    }
    if (!usage_count) {
        ImGui_TextDisabled("No indexed usages");
        editor_ui_track_item("asset.usages.empty");
    }
}

static const char *binding_usage_label(const asset_index_binding_usage *usage, char *buffer, size_t capacity)
{
    if (usage->kind == PIN_BINDING_VARIABLE) {
        snprintf(buffer, capacity, "Variable.%s", usage->variable_name ? usage->variable_name : "?");
        return buffer;
    }
    if (usage->kind == PIN_BINDING_COMPONENT) {
        snprintf(buffer, capacity, "Self.%s", usage->component ? usage->component : "?");
        return buffer;
    }
    if (usage->kind == PIN_BINDING_COMPONENT_FIELD) {
        snprintf(
            buffer,
            capacity,
            "Self.%s.%s",
            usage->component ? usage->component : "?",
            usage->field ? usage->field : "?"
        );
        return buffer;
    }
    if (usage->kind == PIN_BINDING_EVENT_OUTPUT) {
        return "Event Output";
    }
    return "Delta Time";
}

void draw_asset_binding_usages(app_scene_authoring *scene, int asset_position)
{
    const asset_index *index = app_scene_authoring_asset_index(scene);
    int usage_count = 0;
    for (int i = 0; index && i < asset_index_binding_usage_count(index); i++) {
        const asset_index_binding_usage *usage = asset_index_binding_usage_at(index, i);
        if (!usage || usage->source != asset_position) {
            continue;
        }
        if (!usage_count) {
            ImGui_SeparatorText("Input bindings");
        }
        char label[256];
        char source[192];
        snprintf(
            label,
            sizeof label,
            "%s%s",
            binding_usage_label(usage, source, sizeof source),
            usage->dormant ? "  (dormant while wired)" : ""
        );
        char id[96];
        snprintf(id, sizeof id, "asset.binding_usage.%d", i);
        ImGui_PushID(id);
        if (ImGui_SelectableEx(label, false, 0, (ImVec2){ 0, 0 })) {
            if (app_scene_authoring_open_asset(scene, usage->source) == 0) {
                editor_reveal_node_by_id(
                    scene->graph_editor,
                    usage->graph_id,
                    usage->node_id
                );
            }
        }
        ImGui_PopID();
        editor_ui_track_item(id);
        ImGui_SameLine();
        ImGui_TextDisabled("pin %llu", (unsigned long long)usage->pin_id);
        usage_count++;
    }
    if (!usage_count) {
        return;
    }
}

void draw_component_asset_links(app_scene_authoring *scene, const char *json, const char *id)
{
    if (!scene || !json || !json[0]) {
        return;
    }
    yyjson_doc *document = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    if (!yyjson_is_arr(root)) {
        yyjson_doc_free(document);
        return;
    }
    const asset_index *index = app_scene_authoring_asset_index(scene);
    yyjson_arr_iter iterator = yyjson_arr_iter_with(root);
    yyjson_val *part;
    int number = 0;
    bool header = false;
    while ((part = yyjson_arr_iter_next(&iterator))) {
        yyjson_val *material = yyjson_is_obj(part) ? yyjson_obj_get(part, "material")
                                                   : NULL;
        if (!yyjson_is_str(material) || !yyjson_get_len(material)) {
            number++;
            continue;
        }
        if (!header) {
            ImGui_SeparatorText("Mesh materials");
            header = true;
        }
        const char *name = yyjson_get_str(material);
        int position = asset_index_find_name(index, ASSET_INDEX_MATERIAL, name);
        const asset_index_asset *asset = asset_index_asset_at(index, position);
        char label[192];
        snprintf(label, sizeof label, "Part %d  %s", number + 1, name);
        ImGui_TextUnformatted(label);
        ImGui_SameLine();
        char button[96];
        snprintf(button, sizeof button, "Open##%s.%d", id ? id : "mesh", number);
        bool enabled = asset && !asset->unresolved;
        ImGui_BeginDisabled(!enabled);
        if (ImGui_SmallButton(button) && enabled) {
            app_scene_authoring_open_asset(scene, position);
        }
        ImGui_EndDisabled();
        char track[128];
        snprintf(track, sizeof track, "%s.open.%d", id ? id : "mesh", number);
        editor_ui_track_item(track);
        number++;
    }
    yyjson_doc_free(document);
}

static int current_index_asset(app_scene_authoring *scene, asset_index_kind kind, const char *name)
{
    const asset_index *index = app_scene_authoring_asset_index(scene);
    return index ? asset_index_find_name(index, kind, name) : -1;
}

static void draw_event_overview(app_scene_authoring *scene, const behavior_asset *asset)
{
    int position = current_index_asset(scene, ASSET_INDEX_EVENT, asset->name);
    ImGui_TextUnformatted(asset->name);
    editor_ui_track_item("navigator.event.title");
    ImGui_SeparatorText("Signature");
    if (!acount(asset->variables)) {
        ImGui_TextDisabled("No payload fields");
        editor_ui_track_item("navigator.event.signature.empty");
    }
    for (int i = 0; i < acount(asset->variables); i++) {
        const behavior_variable *variable = &asset->variables[i];
        const char *type = variable->type_name ? variable->type_name
                                               : pin_type_name(variable->type);
        ImGui_Text("%s: %s", variable->name, type ? type : "value");
        char id[96];
        snprintf(id, sizeof id, "event.signature.%s", variable->name);
        editor_ui_track_item(id);
    }
    if (position >= 0) {
        ImGui_SeparatorText("Consumers and emitters");
        int references = 0;
        const asset_index *index = app_scene_authoring_asset_index(scene);
        for (int i = 0; index && i < asset_index_reference_count(index); i++) {
            const asset_index_reference *reference = asset_index_reference_at(index, i);
            if (reference->target != position) {
                continue;
            }
            draw_reference_row(scene, reference, i);
            references++;
            for (int j = 0; index && j < asset_index_reference_count(index); j++) {
                const asset_index_reference *attachment = asset_index_reference_at(index, j);
                if (attachment->target != reference->source ||
                    attachment->kind != ASSET_INDEX_BEHAVIOR_ATTACHMENT) {
                    continue;
                }
                const asset_index_asset
                    *owner = asset_index_asset_at(index, attachment->source);
                if (owner) {
                    ImGui_Text(
                        "  Attached by %s%s%s",
                        owner->name,
                        attachment->actor ? "  " : "",
                        attachment->actor ? attachment->actor : ""
                    );
                    editor_ui_track_item("event.reference.attachment");
                }
            }
        }
        if (!references) {
            ImGui_TextDisabled("No indexed consumers or emitters");
            editor_ui_track_item("event.references.empty");
        }
    }
}

static void draw_library_overview(app_scene_authoring *scene, const behavior_asset *asset)
{
    int position = current_index_asset(scene, ASSET_INDEX_LIBRARY, asset->name);
    ImGui_TextUnformatted(asset->name);
    editor_ui_track_item("navigator.library.title");
    ImGui_SeparatorText("Functions");
    int functions = 0;
    for (int i = 0; i < acount(asset->graphs); i++) {
        const graph *graph = asset->graphs[i];
        if (!graph || graph->kind != GRAPH_FUNCTION || graph->math_expression) {
            continue;
        }
        char id[96];
        snprintf(
            id,
            sizeof id,
            "library.function.%llu",
            (unsigned long long)graph->graph_id
        );
        if (ImGui_SelectableEx(graph->name, false, 0, (ImVec2){ 0, 0 })) {
            editor_open_graph(scene->graph_editor, graph->graph_id);
        }
        editor_ui_track_item(id);
        functions++;
    }
    if (!functions) {
        ImGui_TextDisabled("No authored functions");
        editor_ui_track_item("library.functions.empty");
    }
    if (position < 0) {
        return;
    }
    const asset_index *index = app_scene_authoring_asset_index(scene);
    ImGui_SeparatorText("Callers");
    int callers = 0;
    for (int i = 0; index && i < asset_index_reference_count(index); i++) {
        const asset_index_reference *reference = asset_index_reference_at(index, i);
        if (reference->target != position || reference->kind != ASSET_INDEX_GRAPH_CALL) {
            continue;
        }
        draw_reference_row(scene, reference, i);
        callers++;
    }
    if (!callers) {
        ImGui_TextDisabled("No indexed callers");
        editor_ui_track_item("library.callers.empty");
    }
    ImGui_SeparatorText("Imports");
    int imports = 0;
    for (int i = 0; index && i < asset_index_reference_count(index); i++) {
        const asset_index_reference *reference = asset_index_reference_at(index, i);
        if (reference->source != position || reference->kind != ASSET_INDEX_GRAPH_CALL) {
            continue;
        }
        const asset_index_asset *target = asset_index_asset_at(index, reference->target);
        if (target) {
            draw_reference_target(scene, reference, i);
            imports++;
        }
    }
    if (!imports) {
        ImGui_TextDisabled("No indexed imports");
        editor_ui_track_item("library.imports.empty");
    }
}

static void draw_asset_overview(app_scene_authoring *scene, const behavior_asset *asset)
{
    switch (asset->domain) {
        case BEHAVIOR_DOMAIN_EVENT_DEFINITION:
            draw_event_overview(scene, asset);
            return;
        case BEHAVIOR_DOMAIN_LIBRARY:
        case BEHAVIOR_DOMAIN_SHADER_LIBRARY:
            draw_library_overview(scene, asset);
            return;
        case BEHAVIOR_DOMAIN_MATERIAL: {
            ImGui_TextUnformatted(asset->name);
            editor_ui_track_item("navigator.material.title");
            draw_asset_references(
                scene,
                current_index_asset(scene, ASSET_INDEX_MATERIAL, asset->name)
            );
            return;
        }
        case BEHAVIOR_DOMAIN_SHAPE: {
            ImGui_TextUnformatted(asset->name);
            editor_ui_track_item("navigator.shape.title");
            draw_asset_references(
                scene,
                current_index_asset(scene, ASSET_INDEX_SHAPE, asset->name)
            );
            return;
        }
        case BEHAVIOR_DOMAIN_FIELD:
        case BEHAVIOR_DOMAIN_EVENT: break;
    }
    ImGui_TextUnformatted(asset->name);
    editor_ui_track_item("navigator.document.outline");
    editor_draw_outline(scene->graph_editor);
}

bool draw_asset_document_overview(app_scene_authoring *scene)
{
    if (!scene || !scene->graph_editor ||
        !editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS)) {
        return false;
    }
    const behavior_asset *asset = editor_behavior_asset(scene->graph_editor);
    if (!asset || (asset->domain != BEHAVIOR_DOMAIN_EVENT_DEFINITION &&
                   asset->domain != BEHAVIOR_DOMAIN_LIBRARY &&
                   asset->domain != BEHAVIOR_DOMAIN_SHADER_LIBRARY)) {
        return false;
    }
    const graph *active = editor_active_graph(scene->graph_editor);
    if (asset->domain != BEHAVIOR_DOMAIN_EVENT_DEFINITION && active &&
        active->graph_id != 0) {
        editor_set_graph_visible(scene->graph_editor, true);
        return false;
    }
    editor_set_graph_visible(scene->graph_editor, false);
    char title[256];
    snprintf(title, sizeof title, "%s###Node Canvas", asset->name);
    if (editor_ui_begin_pane(EDITOR_UI_PANE_NODE_CANVAS, title, 0)) {
        draw_asset_overview(scene, asset);
    }
    ImGui_End();
    return true;
}
