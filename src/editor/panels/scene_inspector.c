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

static const char *inspection_state_name(flow_state state)
{
    switch (state) {
        case FLOW_IDLE: return "Idle";
        case FLOW_RUNNING: return "Running";
        case FLOW_SUSPENDED: return "Suspended";
        case FLOW_PAUSED: return "Paused";
        case FLOW_COMPLETED: return "Completed";
        case FLOW_FAILED: return "Failed";
    }
    return "Unknown";
}

static int revert_instance_components(app_scene_authoring *scene, scene_actor *actor)
{
    scene_field_edit *edits = NULL;
    for (int i = 0; i < acount(actor->base->components); i++) {
        const game_prototype_component *component = &actor->base->components[i];
        if (!ecs_component_has(scene->world, actor->entity, component->desc->id)) {
            continue;
        }
        for (int j = 0; j < component->desc->type->field_count; j++) {
            const type_field *field = &component->desc->type->fields[j];
            scene_field_edit edit = { .component = component->desc->id, .field = j };
            if (ecs_component_read_field(scene->world, actor->entity, edit.component, j, &edit.before) !=
                ECS_OK) {
                afree(edits);
                return -1;
            }
            pin_read_field(component->data, field, &edit.after);
            if (editable_scalar(&edit.before) && editable_scalar(&edit.after) &&
                !pin_value_equal(&edit.before, &edit.after)) {
                apush(edits, edit);
            }
        }
    }
    for (int i = 0; i < acount(edits); i++) {
        scene_field_edit *edit = &edits[i];
        if (ecs_component_write_field(scene->world, actor->entity, edit->component, edit->field, edit->after) !=
            ECS_OK) {
            for (int j = 0; j < i; j++) {
                ecs_component_write_field(
                    scene->world,
                    actor->entity,
                    edits[j].component,
                    edits[j].field,
                    edits[j].before
                );
            }
            afree(edits);
            return -1;
        }
    }
    bool changed = scene_history_push_fields(scene->history, actor->entity, edits, acount(edits));
    afree(edits);
    scene->scene_dirty |= changed;
    if (changed) {
        asset_index_invalidate(scene);
    }
    return changed;
}

static bool draw_instance_field(
    app_scene_authoring *scene,
    scene_actor *actor,
    const game_prototype_component *component,
    const type_field *field
)
{
    pin_value value = { 0 };
    pin_value inherited = { 0 };
    if (ecs_component_read_field(
            scene->world,
            actor->entity,
            component->desc->id,
            (int)(field - component->desc->type->fields),
            &value
        ) != ECS_OK) {
        return false;
    }
    inherited = value;
    if (component->data) {
        pin_read_field(component->data, field, &inherited);
    }
    bool overridden = !pin_value_equal(&value, &inherited);
    char label[160];
    char field_label[128];
    property_label(component, field, field_label, sizeof field_label);
    snprintf(label, sizeof label, "%s%s", field_label, overridden ? " *" : "");
    pin_value before = value;
    int field_index = (int)(field - component->desc->type->fields);
    bool pending = scene->property_actor.slot == actor->entity.slot &&
                   ecs_entity_to_u64(scene->property_actor) ==
                       ecs_entity_to_u64(actor->entity) &&
                   scene->property_edit.component == component->desc->id &&
                   scene->property_edit.field == field_index;
    scene->property_seen |= pending;
    bool color_active = false;
    bool changed = false;
    bool reverted = false;
    bool position_field = component->desc->id == GAME_COMPONENT_GameTransform.id;
    char shader_text[8192];
    ImGui_BeginDisabled(!scene_can_move(scene));
    switch (value.type) {
        case PIN_BOOL:
            changed = property_checkbox(label, &value.b, overridden);
            break;
        case PIN_I8:
            changed = property_number(label, ImGuiDataType_S8, &value.i8, overridden);
            break;
        case PIN_U8:
            changed = property_number(label, ImGuiDataType_U8, &value.u8, overridden);
            break;
        case PIN_I16:
            changed = property_number(label, ImGuiDataType_S16, &value.i16, overridden);
            break;
        case PIN_U16:
            changed = property_number(label, ImGuiDataType_U16, &value.u16, overridden);
            break;
        case PIN_I32:
        case PIN_ENUM: {
            if (component->desc->id == GAME_COMPONENT_GameHRC2D.id &&
                field->offset == offsetof(GameHRC2D, trace_levels)) {
                editor_ui_property_label(label, overridden ? 28.0f : 0.0f);
                changed = ImGui_SliderIntEx("###field_value", &value.i, 1, 5, "%d", ImGuiSliderFlags_AlwaysClamp);
                break;
            }
            const editor_choices *choices = property_choices(component->desc->type, field);
            if (choices) {
                editor_ui_property_label(label, overridden ? 28.0f : 0.0f);
            }
            int *choice_value = value.type == PIN_ENUM ? &value.e : &value.i;
            changed = choices
                        ? editor_choices_draw("instance.shape", choice_value, choices)
                        : property_number(label, ImGuiDataType_S32, &value.i, overridden);
            break;
        }
        case PIN_U32:
            if (appearance_color_field(component->desc->type, field)) {
                bool first_change;
                char color_id[192];
                snprintf(
                    color_id,
                    sizeof color_id,
                    "instance.color.%s.%s",
                    component->desc->name,
                    field->name
                );
                editor_ui_property_label(label, overridden ? 28.0f : 0.0f);
                changed = editor_color_draw(
                    &scene->property_color,
                    color_id,
                    &value.u,
                    &first_change
                );
                color_active = editor_color_open(color_id);
                break;
            }
            if (collision_category_field(component->desc->type, field)) {
                editor_ui_property_label(label, overridden ? 28.0f : 0.0f);
                changed = editor_flags_draw("##field_value", &value.u, &COLLISION_CATEGORIES);
            } else {
                changed = property_number(label, ImGuiDataType_U32, &value.u, overridden);
            }
            break;
        case PIN_I64:
            changed = property_number(label, ImGuiDataType_S64, &value.i64, overridden);
            break;
        case PIN_U64:
            changed = property_number(label, ImGuiDataType_U64, &value.u64, overridden);
            break;
        case PIN_F32:
            if (component->desc->id == GAME_COMPONENT_GameEnvironment.id) {
                changed = property_environment(label, field, &value.f, overridden);
                break;
            }
            if (component->desc->id == GAME_COMPONENT_GameHRC2D.id &&
                field->offset == offsetof(GameHRC2D, scale)) {
                editor_ui_property_label(label, overridden ? 28.0f : 0.0f);
                changed = ImGui_SliderFloatEx("###field_value", &value.f, .1f, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                break;
            }
            changed = position_field
                        ? editor_ui_axis_float(
                              field_label,
                              &value.f,
                              field->offset == offsetof(GameTransform, x) ? 0 : 1,
                              overridden
                          )
                        : property_number(label, ImGuiDataType_Float, &value.f, overridden);
            break;
        case PIN_F64:
            changed = property_number(label, ImGuiDataType_Double, &value.d, overridden);
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
                draw_component_asset_links(scene, shader_text, "instance.mesh");
                break;
            }
            if (component->desc->id == GAME_COMPONENT_GameShader.id) {
                snprintf(
                    shader_text,
                    sizeof shader_text,
                    "%s",
                    value.str ? value.str : ""
                );
                changed = property_shader(
                    label,
                    GAME_GameShader_get_const(scene->world, actor->entity),
                    field->name,
                    shader_text,
                    sizeof shader_text
                );
                value.str = shader_text;
                break;
            }
            snprintf(shader_text, sizeof shader_text, "%s", value.str ? value.str : "");
            editor_ui_property_label(label, overridden ? 28 : 0);
            changed = ImGui_InputText("##text_value", shader_text, sizeof shader_text, 0);
            value.str = shader_text;
            break;
        case PIN_HANDLE:
            if (value.type_name && !strcmp(value.type_name, "ecs.entity")) {
                property_text(
                    label,
                    value.u64
                        ? runtime_entity_label(scene, ecs_entity_from_u64(value.u64))
                        : "None"
                );
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
            "instance.field.open.%s.%s",
            component->desc->name,
            field->name
        );
        editor_ui_track_item(open_id);
    }
    bool field_active = color_active || ImGui_IsItemActive();
    char item_id[192];
    snprintf(
        item_id,
        sizeof item_id,
        "instance.field.%s.%s",
        component->desc->name,
        field->name
    );
    editor_ui_track_item(item_id);
    if (position_field) {
        editor_ui_track_item(
            field->offset == offsetof(GameTransform, x) ? "instance.transform.x" : "instance.transform.y"
        );
    }
    if (overridden && editable_scalar(&before)) {
        ImGui_SameLine();
        reverted = editor_ui_icon_button_sized("Revert", EDITOR_ICON_FA_UNDO, true, 20.0f);
        snprintf(
            item_id,
            sizeof item_id,
            "instance.revert.%s.%s",
            component->desc->name,
            field->name
        );
        editor_ui_track_item(item_id);
        if (position_field) {
            editor_ui_track_item(
                field->offset == offsetof(GameTransform, x)
                    ? "instance.revert.transform.x"
                    : "instance.revert.transform.y"
            );
        }
    }
    ImGui_EndDisabled();
    if (position_field) {
        if ((changed || reverted) &&
            (!scene->edit_actor.slot || scene->edit_field != field_index)) {
            begin_transform_edit(scene, actor->entity);
            scene->edit_field = field_index;
        }
        if (scene->edit_actor.slot && !changed && !reverted && !field_active &&
            !scene->dragging_actor && scene->edit_field == field_index) {
            finish_transform_edit(scene, false);
        }
    }
    if (!changed && !reverted) {
        if (pending && !field_active) {
            finish_property_edit(scene);
        }
        return false;
    }
    if (reverted) {
        value = inherited;
    }
    if (!editable_scalar(&value)) {
        return false;
    }
    if (!position_field && !pending) {
        finish_transform_edit(scene, false);
        scene->property_actor = actor->entity;
        scene->property_edit = (scene_field_edit){ .component = component->desc->id,
                                                   .field = field_index,
                                                   .before = before,
                                                   .after = before };
        scene->property_dirty = scene->scene_dirty;
        scene->property_seen = true;
    }
    int result = ecs_component_write_field(
        scene->world,
        actor->entity,
        component->desc->id,
        (int)(field - component->desc->type->fields),
        value
    );
    if (result == ECS_OK) {
        ecs_component_read_field(scene->world, actor->entity, component->desc->id, field_index, &value);
        scene->scene_dirty = true;
        asset_index_invalidate(scene);
        if (!position_field) {
            scene->property_edit.after = value;
        }
    }
    if (position_field && !field_active) {
        finish_transform_edit(scene, false);
    }
    if (!position_field && !field_active) {
        finish_property_edit(scene);
    }
    return result == ECS_OK;
}

static void draw_instance_components(app_scene_authoring *scene, scene_actor *actor)
{
    ImGui_BeginDisabled(scene->runtime_snapshot_valid);
    if (editor_ui_begin_attachment_picker(
            "Components",
            "instance.components",
            scene->component_search,
            sizeof scene->component_search
        )) {
        int matches = 0, selected = 0;
        for (int i = 0; i < scene->registry.component_count; i++) {
            const ecs_component_desc *desc = scene->registry.components[i];
            bool attached = ecs_component_has(scene->world, actor->entity, desc->id);
            selected += attached;
            char label[128], id[192];
            editor_component_label(desc->name, label, sizeof label);
            if (!scene_search_matches(scene->component_search, label)) {
                continue;
            }
            matches++;
            if (ImGui_Checkbox(label, &attached)) {
                app_scene_authoring_set_instance_component(scene, desc->name, attached);
                selected += attached ? 1 : -1;
            }
            snprintf(id, sizeof id, "instance.components.option.%s", desc->name);
            editor_ui_track_item(id);
        }
        if (!matches) {
            ImGui_TextUnformatted("No matches");
        }
        editor_ui_end_attachment_picker(selected);
    }
    ImGui_EndDisabled();
    ImGui_BeginDisabled(!scene_can_move(scene));
    if (ImGui_Button("Revert All")) {
        finish_transform_edit(scene, false);
        int result = revert_instance_components(scene, actor);
        scene->scene_dirty |= result > 0;
        if (result > 0) {
            asset_index_invalidate(scene);
        }
        if (result < 0) {
            snprintf(
                scene->message,
                sizeof scene->message,
                "Could not revert %s.",
                actor->name
            );
        } else if (result > 0) {
            snprintf(
                scene->message,
                sizeof scene->message,
                "Reverted %s to prototype defaults.",
                actor->name
            );
        }
    }
    editor_ui_track_item("instance.revert_all");
    ImGui_EndDisabled();
    for (int i = 0; i < scene->registry.component_count; i++) {
        const ecs_component_desc *desc = scene->registry.components[i];
        if (!ecs_component_has(scene->world, actor->entity, desc->id)) {
            continue;
        }
        game_prototype_component added = { .desc = desc };
        const game_prototype_component *component = &added;
        for (int j = 0; j < acount(actor->base->components); j++) {
            if (actor->base->components[j].desc->id == desc->id) {
                component = &actor->base->components[j];
                break;
            }
        }
        ImGui_PushIDInt(i);
        char component_id[192];
        snprintf(
            component_id,
            sizeof component_id,
            "instance.component.%s",
            component->desc->name
        );
        bool remove = false;
        bool open = component_tree(
            component->desc,
            component_id,
            scene->runtime_snapshot_valid ? NULL : &remove
        );
        if (open) {
            bool material = GAME_GameMaterial_has(scene->world, actor->entity);
            for (int group = 0; group < 2; group++) {
                if (!begin_property_group(component->desc, group, "instance.material.advanced")) {
                    continue;
                }
                bool axes = component->desc->id == GAME_COMPONENT_GameTransform.id;
                if (axes && !editor_ui_begin_vector("Position")) {
                    continue;
                }
                for (int field = 0; field < component->desc->type->field_count;
                     field++) {
                    const void *data = ecs_component_data_const(
                        scene->world,
                        actor->entity,
                        component->desc->id
                    );
                    if (!property_field_visible(
                            component->desc,
                            &component->desc->type->fields[field],
                            data
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
                    draw_instance_field(
                        scene,
                        actor,
                        component,
                        &component->desc->type->fields[field]
                    );
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
            app_scene_authoring_set_instance_component(scene, desc->name, false);
        }
    }
}

static void draw_runtime_components(app_scene_authoring *scene, ecs_entity entity)
{
    ImGui_SeparatorText("Components");
    for (int i = 0; i < scene->registry.component_count; i++) {
        const ecs_component_desc *desc = scene->registry.components[i];
        if (!ecs_component_has(scene->world, entity, desc->id) || !desc->type) {
            continue;
        }
        ImGui_PushID(desc->name);
        if (component_tree(desc, desc->name, NULL)) {
            for (int group = 0; group < 2; group++) {
                if (!begin_property_group(desc, group, "runtime.material.advanced")) {
                    continue;
                }
                for (int j = 0; j < desc->type->field_count; j++) {
                    if (advanced_material_field(desc, &desc->type->fields[j]) !=
                        (group == 1)) {
                        continue;
                    }
                    pin_value value = { 0 };
                    if (ecs_component_read_field(scene->world, entity, desc->id, j, &value) !=
                        ECS_OK) {
                        continue;
                    }
                    char name[128];
                    editor_identifier_label(desc->type->fields[j].name, name, sizeof name);
                    property_text(name, pin_value_to_string(&value));
                    char id[192];
                    snprintf(
                        id,
                        sizeof id,
                        "runtime.field.%s.%s",
                        desc->name,
                        desc->type->fields[j].name
                    );
                    editor_ui_track_item(id);
                }
                if (group) {
                    ImGui_TreePop();
                }
            }
            ImGui_TreePop();
        }
        ImGui_PopID();
    }
}

static void draw_instance_behaviors(app_scene_authoring *scene, scene_actor *actor)
{
    ImGui_BeginDisabled(scene->runtime_snapshot_valid);
    if (editor_ui_begin_attachment_picker(
            "Behaviors",
            "instance.behavior.add",
            scene->behavior_search,
            sizeof scene->behavior_search
        )) {
        int matches = 0;
        game_behavior_world *world = scene->live->behaviors;
        for (int i = 0; i < game_behavior_asset_count(world); i++) {
            const char *name = game_behavior_asset_name_at(world, i);
            if (!scene_search_matches(scene->behavior_search, name)) {
                continue;
            }
            matches++;
            bool attached = game_behavior_instance(world, actor->entity, name) != NULL;
            if (ImGui_Checkbox(name, &attached)) {
                app_scene_authoring_set_instance_behavior(scene, name, attached);
            }
            char id[256];
            snprintf(id, sizeof id, "instance.behavior.attach.%s", name);
            editor_ui_track_item(id);
        }
        if (!matches) {
            ImGui_TextUnformatted("No matches");
        }
        editor_ui_end_attachment_picker(
            game_behavior_attachment_count_for_entity(world, actor->entity)
        );
    }
    ImGui_EndDisabled();
    char **names = instance_behavior_names(scene, actor->entity);
    for (int i = 0; i < acount(names); i++) {
        ImGui_PushID(names[i]);
        char label[192], id[256];
        editor_identifier_label(names[i], label, sizeof label);
        float width = fmaxf(1, ImGui_GetContentRegionAvail().x - 32);
        ImGui_AlignTextToFramePadding();
        if (ImGui_SelectableEx(label, false, 0, (ImVec2){ width, 0 })) {
            app_scene_authoring_open_behavior(scene, names[i]);
        }
        snprintf(id, sizeof id, "instance.behavior.open.%s", names[i]);
        editor_ui_track_item(id);
        ImGui_SameLineEx(0, 4);
        ImGui_BeginDisabled(scene->runtime_snapshot_valid);
        if (editor_ui_icon_button("remove_behavior", EDITOR_ICON_FA_UNLINK, true)) {
            app_scene_authoring_set_instance_behavior(scene, names[i], false);
        }
        snprintf(id, sizeof id, "instance.behavior.remove.%s", names[i]);
        editor_ui_track_item(id);
        ImGui_EndDisabled();
        ImGui_PopID();
    }
    free_behavior_names(names);
}

static bool physics_count(const char *id, int *value, int minimum, int maximum)
{
    char label[32];
    snprintf(label, sizeof label, "%d", *value);
    bool changed = false;
    if (ImGui_BeginCombo(id, *value ? label : "Auto", 0)) {
        for (int i = minimum; i <= maximum; i++) {
            snprintf(label, sizeof label, "%d", i);
            if (ImGui_SelectableEx(i ? label : "Auto", i == *value, 0, (ImVec2){ 0 })) {
                *value = i;
                changed = true;
            }
        }
        ImGui_EndCombo();
    }
    return changed;
}

static void draw_physics_settings(app_scene_authoring *scene)
{
    bool open = ImGui_CollapsingHeader("Scene Physics", 0);
    editor_ui_track_item("scene.physics");
    if (!open) {
        return;
    }
    game_physics_settings settings = game_physics_configuration(scene->live->physics);
    bool stopped = !scene->runtime_snapshot_valid &&
                   !game_prototype_preview_entity(scene->prototype_preview).slot;
    ImGui_BeginDisabled(!stopped);
    bool changed = false;
    editor_ui_property_label("Workers", 0);
    changed |= physics_count("##physics.workers", &settings.workers, 0, GAME_PHYSICS_WORKER_MAX);
    editor_ui_track_item("scene.physics.workers");
    editor_ui_property_label("Substeps", 0);
    changed |= physics_count("##physics.substeps", &settings.substeps, 1, GAME_PHYSICS_SUBSTEP_MAX);
    editor_ui_track_item("scene.physics.substeps");
    changed |= ImGui_Checkbox("Allow sleeping", &settings.sleep);
    editor_ui_track_item("scene.physics.sleep");
    changed |= ImGui_Checkbox("Solid continuous collision", &settings.continuous);
    editor_ui_track_item("scene.physics.continuous");
    changed |= ImGui_Checkbox("Drive with forces", &settings.force_drive);
    editor_ui_track_item("scene.physics.force_drive");
    editor_ui_property_label("Contact Hz", 0);
    changed |= ImGui_InputFloatEx("##physics.hertz", &settings.contact_hertz, 0, 0, "%.1f Hz", 0);
    editor_ui_track_item("scene.physics.contact_hertz");
    editor_ui_property_label("Damping", 0);
    changed |= ImGui_InputFloatEx("##physics.damping", &settings.contact_damping, 0, 0, "%.2f", 0);
    editor_ui_track_item("scene.physics.contact_damping");
    if (ImGui_Button("Reset physics defaults")) {
        settings = game_physics_defaults();
        changed = true;
    }
    editor_ui_track_item("scene.physics.defaults");
    ImGui_EndDisabled();
    if (changed) {
        if (app_scene_authoring_set_physics(scene, settings) != 0) {
            snprintf(scene->message, sizeof scene->message, "Physics: contact frequency must be 0..240 Hz and damping ratio 0..100.");
        }
    }
    ImGui_TextDisabled(
        "Available worker budget: %d",
        game_physics_worker_budget(scene->live->physics)
    );
    ImGui_Separator();
}

static scene_actor *radiance_actor(app_scene_authoring *scene)
{
    for (int i = 0; i < acount(scene->actors); i++) {
        if (GAME_GameHRC2D_has(scene->world, scene->actors[i].entity)) {
            return &scene->actors[i];
        }
    }
    return NULL;
}

static bool radiance_field(const type_field *field)
{
    return field->offset == offsetof(GameHRC2D, scale) ||
           field->offset == offsetof(GameHRC2D, trace_levels) ||
           field->offset == offsetof(GameHRC2D, feedback);
}

static void draw_radiance_settings(app_scene_authoring *scene)
{
    if (scene->document.kind != APP_EDITOR_DOCUMENT_SCENE) {
        return;
    }
    scene_actor *actor = radiance_actor(scene);
    game_prototype_component added = { .desc = &GAME_COMPONENT_GameHRC2D };
    const game_prototype_component *component = actor
                                                  ? prototype_component_by_id(
                                                        actor->base,
                                                        GAME_COMPONENT_GameHRC2D.id
                                                    )
                                                  : NULL;
    if (!component && actor) {
        component = &added;
    }
    if (!component) {
        return;
    }
    bool open = ImGui_CollapsingHeader("Radiance Cascades", 0);
    editor_ui_track_item("scene.radiance");
    if (!open) {
        return;
    }
    for (int i = 0; i < component->desc->type->field_count; i++) {
        const type_field *field = &component->desc->type->fields[i];
        if (!radiance_field(field)) {
            continue;
        }
        ImGui_PushIDInt(i);
        draw_instance_field(scene, actor, component, field);
        ImGui_PopID();
    }
    ImGui_Separator();
}

void draw_scene_inspector(app_scene_authoring *scene)
{
    if (!editor_ui_pane_visible(EDITOR_UI_PANE_INSPECTOR)) {
        return;
    }
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_INSPECTOR)) {
        ImGui_SetNextWindowFocus();
    }
    if (!editor_ui_begin_pane(EDITOR_UI_PANE_INSPECTOR, "Inspector", 0)) {
        ImGui_End();
        return;
    }

    if (scene->document.kind == APP_EDITOR_DOCUMENT_SCENE) {
        draw_radiance_settings(scene);
        draw_physics_settings(scene);
    }
    scene_actor *actor = selected_actor(scene);
    ecs_entity runtime_entity = scene->runtime_selected.slot
                                  ? selected_scene_entity(scene)
                                  : (ecs_entity){ 0 };
    if (!actor && !runtime_entity.slot) {
        ImGui_TextUnformatted("No Selection");
        ImGui_End();
        return;
    }
    if (runtime_entity.slot) {
        editor_ui_identity(
            runtime_entity_label(scene, runtime_entity),
            "Runtime instance · Read-only",
            EDITOR_UI_ASSET_PROTOTYPE
        );
        editor_ui_track_item("inspector.spawned");
        draw_runtime_components(scene, runtime_entity);
    } else {
        char identity_detail[192];
        snprintf(identity_detail, sizeof identity_detail, "%s instance", actor->prototype_name);
        editor_ui_identity(actor->name, identity_detail, EDITOR_UI_ASSET_PROTOTYPE);
        editor_ui_track_item("inspector.scene");
        int selection_count = app_scene_authoring_selection_count(scene);
        if (selection_count > 1) {
            ImGui_TextDisabled(
                "%d selected · Showing %s",
                selection_count,
                actor->name
            );
        }
        if (scene->runtime_snapshot_valid) {
            ImGui_TextDisabled("Simulation");
        }
        draw_instance_components(scene, actor);
        draw_instance_behaviors(scene, actor);
    }
    game_behavior_attachment_info info;
    int has_attachment = scene->document.kind == APP_EDITOR_DOCUMENT_BEHAVIOR
                           ? selected_behavior_attachment(scene, &info)
                           : selected_attachment(scene, &info);
    if (!has_attachment) {
        ImGui_End();
        return;
    }
    ImGui_SeparatorText("Runtime");
    ImGui_TextUnformatted(behavior_backend_name(behavior_instance_backend(info.instance)));
    const behavior_instance_failure *failure = behavior_instance_last_failure(info.instance);
    if (failure) {
        ImGui_TextWrapped("Last failure: %s", failure->message);
        ImGui_TextDisabled("%s / %s", failure->event_name, failure->graph_name);
        ImGui_TextDisabled(
            "Node %llu, pin %d, steps %lu",
            (unsigned long long)failure->node_id,
            failure->pin,
            failure->steps
        );
        if (ImGui_Button("Clear failure")) {
            behavior_instance_clear_failure(info.instance);
        }
    }

    game_behavior_world *behaviors = scene->live->behaviors;
    int attachment_count = game_behavior_attachment_count_for_entity(behaviors, info.entity);
    if (ImGui_Button(info.asset_name)) {
        ImGui_OpenPopup("runtime_behavior_popup", 0);
    }
    if (ImGui_BeginPopup("runtime_behavior_popup", 0)) {
        for (int i = 0; i < attachment_count; i++) {
            game_behavior_attachment_info candidate;
            if (game_behavior_attachment_info_at(behaviors, info.entity, i, &candidate) !=
                0) {
                continue;
            }
            if (ImGui_SelectableEx(
                    candidate.asset_name,
                    i == scene->selected_attachment,
                    0,
                    (ImVec2){ 0.0f, 0.0f }
                )) {
                scene->selected_attachment = i;
                ImGui_CloseCurrentPopup();
            }
        }
        ImGui_EndPopup();
    }

    flow_run *run = NULL;
    for (int i = acount(info.instance->activations) - 1; i >= 0; i--) {
        flow_state state = flow_run_status(info.instance->activations[i]);
        if (state != FLOW_COMPLETED && state != FLOW_FAILED) {
            run = info.instance->activations[i];
            break;
        }
    }

    char line[256];
    if (run) {
        snprintf(line, sizeof line, "State: %s", inspection_state_name(flow_run_status(run)));
        ImGui_TextUnformatted(line);
        snprintf(line, sizeof line, "Steps: %lu", flow_run_steps(run));
        ImGui_TextUnformatted(line);
        const node *current = flow_run_current_node(run);
        char current_label[128] = "None";
        if (current) {
            editor_node_label(current->desc, current_label, sizeof current_label);
        }
        snprintf(line, sizeof line, "Current node: %s", current_label);
        ImGui_TextUnformatted(line);
        if (flow_run_status(run) == FLOW_RUNNING ||
            flow_run_status(run) == FLOW_SUSPENDED ||
            flow_run_status(run) == FLOW_PAUSED) {
            if (ImGui_Button("Pause")) {
                flow_run_pause(run);
            }
            ImGui_SameLine();
            if (ImGui_Button("Step")) {
                flow_run_step(run);
            }
            ImGui_SameLine();
            if (ImGui_Button("Resume")) {
                flow_run_resume(run);
            }
        }
        if (ImGui_Button("Stop")) {
            flow_run_stop(run);
        }
        const flow_error *error = flow_run_error(run);
        if (error && error->message) {
            snprintf(line, sizeof line, "Error: %s", error->message);
            ImGui_TextUnformatted(line);
        }
        int stack_count = 0;
        const flow_call_frame *stack = flow_run_call_stack(run, &stack_count);
        ImGui_Separator();
        snprintf(line, sizeof line, "Call stack: %d", stack_count);
        ImGui_TextUnformatted(line);
        for (int i = 0; i < stack_count && i < 8; i++) {
            snprintf(
                line,
                sizeof line,
                "  %d: node %llu",
                i,
                (unsigned long long)stack[i].node_id
            );
            ImGui_TextUnformatted(line);
        }
    } else {
        ImGui_TextDisabled("Idle");
    }

    int trace_count = 0;
    const behavior_instance_trace_entry *trace = behavior_instance_trace(info.instance, &trace_count);
    ImGui_Separator();
    if (!ImGui_TreeNodeEx("Execution trace", 0)) {
        ImGui_End();
        return;
    }
    snprintf(line, sizeof line, "Execution trace: %d", trace_count);
    ImGui_TextUnformatted(line);
    for (int i = trace_count - 1; i >= 0 && i >= trace_count - 12; i--) {
        const behavior_instance_trace_entry *entry = &trace[i];
        const flow_traversal *traversal = &entry->traversal;
        snprintf(
            line,
            sizeof line,
            "%s  %s %llu -> %llu",
            entry->event_name[0] ? entry->event_name : "(event)",
            traversal->kind == GRAPH_EDGE_EXEC ? "EXEC" : "DATA",
            (unsigned long long)traversal->from_id,
            (unsigned long long)traversal->to_id
        );
        ImGui_TextUnformatted(line);
    }
    ImGui_TreePop();
    ImGui_End();
}
