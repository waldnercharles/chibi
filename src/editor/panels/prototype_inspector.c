#include "editor/panels/scene_properties.h"
#include "editor/ui/labels.h"
#include "editor/ui/ui_shell.h"
#include "editor/workspace/scene_internal.h"
#include "graph/model/pin_fmt.h"
#include "graph/model/registry.h"
#include "graph/runtime/value_array.h"
#include "runtime/generated.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool property_lighting(const char *label, const type_field *field, float *value)
{
    float maximum = 1;
    switch (field->offset) {
        case offsetof(GameHRC2D, intensity): maximum = 10; break;
        case offsetof(GameHRC2D, ambient):
        case offsetof(GameHRC2D, fog):
        case offsetof(GameHRC2D, shadow_floor): break;
        default: return false;
    }
    editor_ui_property_label(label, 0);
    return ImGui_SliderFloatEx("###field_value", value, 0, maximum, "%.2f", ImGuiSliderFlags_AlwaysClamp);
}

static bool draw_prototype_field(
    app_scene_authoring *scene,
    scene_prototype_asset *asset,
    const game_prototype_component *component,
    const type_field *field
)
{
    pin_value value = { 0 };
    pin_read_field(component->data, field, &value);
    char label[160];
    property_label(component, field, label, sizeof label);
    bool changed = false;
    bool color = appearance_color_field(component->desc->type, field);
    bool first_color_change = false;
    char shader_text[8192];
    switch (value.type) {
        case PIN_BOOL:
            changed = property_checkbox(label, &value.b, false);
            break;
        case PIN_I8:
            changed = property_number(label, ImGuiDataType_S8, &value.i8, false);
            break;
        case PIN_U8:
            changed = property_number(label, ImGuiDataType_U8, &value.u8, false);
            break;
        case PIN_I16:
            changed = property_number(label, ImGuiDataType_S16, &value.i16, false);
            break;
        case PIN_U16:
            changed = property_number(label, ImGuiDataType_U16, &value.u16, false);
            break;
        case PIN_I32:
        case PIN_ENUM: {
            const editor_choices *choices = property_choices(component->desc->type, field);
            if (choices) {
                editor_ui_property_label(label, 0.0f);
            }
            changed = choices
                        ? editor_choices_draw("prototype.shape", &value.i, choices)
                        : property_number(label, ImGuiDataType_S32, &value.i, false);
            break;
        }
        case PIN_U32:
            if (color) {
                char color_id[192];
                snprintf(
                    color_id,
                    sizeof color_id,
                    "prototype.color.%s.%s",
                    component->desc->name,
                    field->name
                );
                editor_ui_property_label(label, 0.0f);
                changed = editor_color_draw(
                    &scene->property_color,
                    color_id,
                    &value.u,
                    &first_color_change
                );
                break;
            }
            if (collision_category_field(component->desc->type, field)) {
                editor_ui_property_label(label, 0.0f);
                changed = editor_flags_draw("##field_value", &value.u, &COLLISION_CATEGORIES);
            } else {
                changed = property_number(label, ImGuiDataType_U32, &value.u, false);
            }
            break;
        case PIN_I64:
            changed = property_number(label, ImGuiDataType_S64, &value.i64, false);
            break;
        case PIN_U64:
            changed = property_number(label, ImGuiDataType_U64, &value.u64, false);
            break;
        case PIN_F32:
            if (component->desc->id == GAME_COMPONENT_GameEnvironment.id) {
                changed = property_environment(label, field, &value.f, false);
                break;
            }
            if (component->desc->id == GAME_COMPONENT_GameHRC2D.id) {
                changed = property_lighting(label, field, &value.f);
                break;
            }
            changed = component->desc->id == GAME_COMPONENT_GameTransform.id
                        ? editor_ui_axis_float(
                              label,
                              &value.f,
                              field->offset == offsetof(GameTransform, x) ? 0 : 1,
                              false
                          )
                        : property_number(label, ImGuiDataType_Float, &value.f, false);
            break;
        case PIN_F64:
            changed = property_number(label, ImGuiDataType_Double, &value.d, false);
            break;
        case PIN_NAME:
        case PIN_STR:
            if (component->desc->id == GAME_COMPONENT_GameMeshParts.id) {
                snprintf(
                    shader_text,
                    sizeof shader_text,
                    "%s",
                    value.str ? value.str : ""
                );
                changed = property_mesh_parts(shader_text, sizeof shader_text);
                value.str = shader_text;
                draw_component_asset_links(scene, shader_text, "prototype.mesh");
                break;
            }
            if (component->desc->id == GAME_COMPONENT_GameShader.id) {
                snprintf(
                    shader_text,
                    sizeof shader_text,
                    "%s",
                    value.str ? value.str : ""
                );
                changed = property_shader(label, component->data, field->name, shader_text, sizeof shader_text);
                value.str = shader_text;
                break;
            }
            property_text(label, pin_value_to_string(&value));
            break;
        case PIN_HANDLE:
            if (value.type_name && !strcmp(value.type_name, "ecs.entity")) {
                property_text(label, value.u64 ? "Runtime entity" : "None");
                break;
            }
            property_text(label, pin_value_to_string(&value));
            break;
        case PIN_STRUCT:
        case PIN_FLOW: property_text(label, pin_value_to_string(&value)); break;
    }
    if (component->desc->id == GAME_COMPONENT_GameShader.id &&
        (!strcmp(field->name, "material") || !strcmp(field->name, "shape")) &&
        value.str && value.str[0]) {
        asset_index_kind kind = !strcmp(field->name, "material")
                                  ? ASSET_INDEX_MATERIAL
                                  : ASSET_INDEX_SHAPE;
        const asset_index *index = app_scene_authoring_asset_index(scene);
        int position = asset_index_find_name(index, kind, value.str);
        const asset_index_asset *indexed = asset_index_asset_at(index, position);
        ImGui_SameLine();
        bool enabled = indexed && !indexed->unresolved;
        ImGui_BeginDisabled(!enabled);
        if (ImGui_SmallButton("Open") && enabled) {
            app_scene_authoring_open_asset(scene, position);
        }
        ImGui_EndDisabled();
        char open_id[192];
        snprintf(
            open_id,
            sizeof open_id,
            "prototype.field.open.%s.%s",
            component->desc->name,
            field->name
        );
        editor_ui_track_item(open_id);
    }
    if (!changed) {
        return false;
    }
    if (color) {
        if (first_color_change) {
            prototype_history_before(scene, asset->prototype);
        }
        return write_prototype_field(scene, asset, component->desc->name, field->name, value) ==
               0;
    }
    return app_scene_authoring_set_prototype_field(
               scene,
               asset->prototype->name,
               component->desc->name,
               field->name,
               value
           ) == 0;
}

static void draw_prototype_preview_controls(app_scene_authoring *scene, scene_prototype_asset *asset)
{
    if (!scene->still_preview) {
        scene->still_preview = prototype_still_new(&scene->registry);
    }
    char error[192] = { 0 };
    int updated = prototype_still_update(scene->still_preview, asset->prototype, error, sizeof error);
    ImGui_TextUnformatted("Prototype Preview");
    if (updated < 0) {
        ImGui_TextWrapped("%s", error[0] ? error : "Could not create preview.");
        return;
    }
    ecs_world *world = prototype_still_world(scene->still_preview);
    ecs_entity entity = prototype_still_entity(scene->still_preview);
    bool perspective = GAME_GameGeometry3D_has(world, entity);
    if ((!perspective && !GAME_GameAppearance_has(world, entity)) ||
        !GAME_GameTransform_get_const(world, entity)) {
        ImGui_TextDisabled("No visual component");
        return;
    }
    engine_viewport *viewport = prototype_still_viewport(scene->still_preview);
    engine_viewport_set_perspective(viewport, perspective);
    if (updated) {
        fit_scene_view(viewport, world, entity);
    }
    float width = ImGui_GetContentRegionAvail().x;
    engine_viewport_image(viewport, width, fminf(144, width * 0.6f));
    editor_ui_track_item("prototype.preview.image");
    bool active = scene->active_runtime_target == APP_RUNTIME_PREVIEW &&
                  previewed_prototype(scene) == asset;
    ImGui_BeginDisabled(runtime_session(scene) && !active);
    if (!active && ImGui_Button("Simulate")) {
        app_scene_authoring_preview_prototype(scene, asset->prototype->name);
    }
    if (!active) {
        editor_ui_track_item("prototype.preview.simulate");
    }
    ImGui_EndDisabled();
    if (active) {
        ImGui_PushID("preview.loop");
        property_checkbox("Loop", &scene->preview_loop, false);
        editor_ui_track_item("prototype.preview.loop");
        ImGui_PopID();
        ImGui_PushID("preview.follow");
        property_checkbox("Follow", &scene->preview_follow, false);
        editor_ui_track_item("prototype.preview.follow");
        ImGui_PopID();
    }
}

const ecs_world *app_scene_authoring_still_preview(const app_scene_authoring *scene, ecs_entity *entity)
{
    if (!scene->still_preview) {
        *entity = (ecs_entity){ 0 };
        return NULL;
    }
    *entity = prototype_still_entity(scene->still_preview);
    return prototype_still_world(scene->still_preview);
}

static void draw_prototype_components(app_scene_authoring *scene, scene_prototype_asset *asset)
{
    ImGui_BeginDisabled(scene->runtime_snapshot_valid);
    if (editor_ui_begin_attachment_picker(
            "Components",
            "prototype.components",
            scene->component_search,
            sizeof scene->component_search
        )) {
        int matches = 0;
        for (int i = 0; i < scene->registry.component_count; i++) {
            const ecs_component_desc *desc = scene->registry.components[i];
            char label[128], id[192];
            editor_component_label(desc->name, label, sizeof label);
            if (!scene_search_matches(scene->component_search, label)) {
                continue;
            }
            matches++;
            bool attached = prototype_has_component_id(asset->prototype, desc->id);
            ImGui_PushID(desc->name);
            if (ImGui_Checkbox(label, &attached)) {
                if (attached) {
                    app_scene_authoring_add_prototype_component(
                        scene,
                        asset->prototype->name,
                        desc->name
                    );
                } else {
                    app_scene_authoring_remove_prototype_component(
                        scene,
                        asset->prototype->name,
                        desc->name
                    );
                }
            }
            snprintf(id, sizeof id, "prototype.components.option.%s", desc->name);
            editor_ui_track_item(id);
            ImGui_PopID();
        }
        if (!matches) {
            ImGui_TextUnformatted("No matches");
        }
        editor_ui_end_attachment_picker(acount(asset->prototype->components));
    }
    ImGui_EndDisabled();
    for (int i = 0; i < scene->registry.component_count; i++) {
        const game_prototype_component *component = prototype_component_by_id(
            asset->prototype,
            scene->registry.components[i]->id
        );
        if (!component) {
            continue;
        }
        ImGui_PushID(component->desc->name);
        char item_id[192];
        snprintf(
            item_id,
            sizeof item_id,
            "prototype.component.%s",
            component->desc->name
        );
        bool remove = false;
        bool open = component_tree(component->desc, item_id, scene->runtime_snapshot_valid ? NULL : &remove);
        if (open) {
            bool material = prototype_has_component_id(
                asset->prototype,
                GAME_COMPONENT_GameMaterial.id
            );
            for (int group = 0; group < 2; group++) {
                if (!begin_property_group(component->desc, group, "prototype.material.advanced")) {
                    continue;
                }
                bool axes = component->desc->id == GAME_COMPONENT_GameTransform.id;
                if (axes && !editor_ui_begin_vector("Position")) {
                    continue;
                }
                for (int field = 0; field < component->desc->type->field_count;
                     field++) {
                    if (!property_field_visible(
                            component->desc,
                            &component->desc->type->fields[field],
                            component->data
                        )) {
                        continue;
                    }
                    if (advanced_material_field(
                            component->desc,
                            &component->desc->type->fields[field]
                        ) != (group == 1)) {
                        continue;
                    }
                    if (material && fallback_appearance_field(
                                        component,
                                        &component->desc->type->fields[field]
                                    )) {
                        continue;
                    }
                    ImGui_PushIDInt(field);
                    if (axes && field > 0) {
                        editor_ui_next_axis();
                    }
                    draw_prototype_field(
                        scene,
                        asset,
                        component,
                        &component->desc->type->fields[field]
                    );
                    snprintf(
                        item_id,
                        sizeof item_id,
                        "prototype.field.%s.%s",
                        component->desc->name,
                        component->desc->type->fields[field].name
                    );
                    editor_ui_track_item(item_id);
                    ImGui_PopID();
                }
                if (axes) {
                    editor_ui_end_vector();
                }
                if (group) {
                    ImGui_TreePop();
                }
            }
            ImGui_TreePop();
        }
        ImGui_PopID();
        if (remove) {
            app_scene_authoring_remove_prototype_component(
                scene,
                asset->prototype->name,
                component->desc->name
            );
            break;
        }
    }
}

static void draw_prototype_behaviors(app_scene_authoring *scene, scene_prototype_asset *asset)
{
    ImGui_BeginDisabled(scene->runtime_snapshot_valid);
    if (editor_ui_begin_attachment_picker(
            "Behaviors",
            "prototype.behaviors",
            scene->behavior_search,
            sizeof scene->behavior_search
        )) {
        int matches = 0;
        for (int i = 0; i < game_behavior_asset_count(scene->live->behaviors); i++) {
            const char *name = game_behavior_asset_name_at(scene->live->behaviors, i);
            if (!scene_search_matches(scene->behavior_search, name)) {
                continue;
            }
            matches++;
            bool attached = game_prototype_has_graph(asset->prototype, name);
            if (ImGui_Checkbox(name, &attached)) {
                if (attached) {
                    app_scene_authoring_attach_prototype_behavior(
                        scene,
                        asset->prototype->name,
                        name
                    );
                } else {
                    app_scene_authoring_detach_prototype_behavior(
                        scene,
                        asset->prototype->name,
                        name
                    );
                }
            }
            char id[192];
            snprintf(id, sizeof id, "prototype.behaviors.option.%s", name);
            editor_ui_track_item(id);
        }
        if (!matches) {
            ImGui_TextUnformatted("No matches");
        }
        editor_ui_end_attachment_picker(acount(asset->prototype->graphs));
    }
    ImGui_EndDisabled();
    for (int i = 0; i < acount(asset->prototype->graphs); i++) {
        const char *name = asset->prototype->graphs[i];
        if (ImGui_SelectableEx(name, false, 0, (ImVec2){ 0.0f, 0.0f })) {
            focus_prototype_behavior(scene, name);
        }
        char id[192];
        snprintf(id, sizeof id, "prototype.behavior.%s", name);
        editor_ui_track_item(id);
    }
}

void draw_prototype_inspector(app_scene_authoring *scene)
{
    if (!editor_ui_pane_visible(EDITOR_UI_PANE_INSPECTOR)) {
        return;
    }
    ImGui_SetNextWindowSize((ImVec2){ 340.0f, 520.0f }, ImGuiCond_FirstUseEver);
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_INSPECTOR)) {
        ImGui_SetNextWindowFocus();
    }
    if (!editor_ui_begin_pane(EDITOR_UI_PANE_INSPECTOR, "Inspector", 0)) {
        ImGui_End();
        return;
    }
    scene_prototype_asset *asset = selected_prototype(scene);
    if (!asset || !asset->prototype) {
        ImGui_TextUnformatted("Prototype");
        ImGui_Separator();
        ImGui_TextUnformatted("No Selection");
        ImGui_End();
        return;
    }
    editor_ui_identity(asset->prototype->name, "Prototype", EDITOR_UI_ASSET_PROTOTYPE);
    if (asset->dirty) {
        ImGui_SameLine();
        ImGui_TextUnformatted("Modified");
    }
    ImGui_PushID(asset->prototype->name);
    draw_prototype_preview_controls(scene, asset);
    draw_prototype_components(scene, asset);
    draw_prototype_behaviors(scene, asset);
    ImGui_PopID();
    ImGui_End();
}
