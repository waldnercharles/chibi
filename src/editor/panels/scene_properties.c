#include "editor/panels/scene_properties.h"

#include "editor/ui/labels.h"
#include "editor/ui/ui_shell.h"
#include "render/shader_catalog.h"
#include "runtime/generated.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

bool property_shader(const char *label, const GameShader *binding, const char *field, char *text, size_t size)
{
    if (strcmp(field, "parameters")) {
        bool shape = !strcmp(field, "shape");
        editor_ui_property_label(label, 0);
        bool changed = false;
        if (ImGui_BeginCombo("##shader_asset", *text ? text : "None", 0)) {
            if (ImGui_Selectable("None")) {
                text[0] = 0;
                changed = true;
            }
            for (int i = 1; i <= render_shader_count(); i++) {
                if (render_shader_is_shape(i) == shape &&
                    ImGui_Selectable(render_shader_name(i))) {
                    snprintf(text, size, "%s", render_shader_name(i));
                    changed = true;
                }
            }
            ImGui_EndCombo();
        }
        return changed;
    }
    /* Forward any parameter widget's active state to the property inspector. */
    ImGui_BeginGroup();
    yyjson_doc *read = yyjson_read(text, strlen(text), 0);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_val *old = read ? yyjson_doc_get_root(read) : NULL;
    yyjson_mut_val *object = yyjson_is_obj(old) ? yyjson_val_mut_copy(doc, old)
                                                : yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, object);
    bool changed = false;
    int indices[] = { render_shader_find(binding->material),
                      render_shader_find(binding->shape) };
    for (int a = 0; a < 2; a++) {
        float values[SHADER_PARAMETER_MAX][4];
        render_shader_parameters(indices[a], NULL, (ecs_entity){ 0 }, text, values);
        for (int slot = 0; slot < SHADER_PARAMETER_MAX; slot++) {
            const char *name = render_shader_parameter_name(indices[a], slot);
            if (!name || !*name) {
                continue;
            }
            ImGui_PushIDInt(a * SHADER_PARAMETER_MAX + slot);
            editor_ui_property_label(name, 0);
            int width = render_shader_parameter_width(indices[a], slot);
            bool edit = ImGui_InputScalarNEx(
                "##parameter",
                ImGuiDataType_Float,
                values[slot],
                width,
                NULL,
                NULL,
                "%.3f",
                0
            );
            if (edit) {
                bool finite = true;
                for (int k = 0; k < width; k++) {
                    finite &= isfinite(values[slot][k]);
                }
                if (finite) {
                    yyjson_mut_val *value = yyjson_mut_arr(doc);
                    for (int k = 0; k < width; k++) {
                        yyjson_mut_arr_add_real(doc, value, values[slot][k]);
                    }
                    yyjson_mut_obj_remove_key(object, name);
                    yyjson_mut_obj_add_val(doc, object, name, value);
                    changed = true;
                }
            }
            ImGui_PopID();
        }
    }
    if (changed) {
        char *json = yyjson_mut_write(doc, 0, NULL);
        if (json && strlen(json) < size) {
            snprintf(text, size, "%s", json);
        } else {
            changed = false;
        }
        free(json);
    }
    yyjson_mut_doc_free(doc);
    yyjson_doc_free(read);
    ImGui_EndGroup();
    return changed;
}

/* Collision bits authored by the Player, Enemy, and Projectile prototypes. */
static const editor_flag COLLISION_FLAGS[] = {
    { 1u, "Player" },
    { 2u, "Enemy" },
    { 4u, "Projectile" },
};
const editor_flags COLLISION_CATEGORIES = { COLLISION_FLAGS, 3 };

bool collision_category_field(const type_desc *type, const type_field *field)
{
    return type == GAME_COMPONENT_GameCollider.type && field &&
           field->type == PIN_U32 &&
           (field->offset == offsetof(GameCollider, category) ||
            field->offset == offsetof(GameCollider, mask));
}

bool appearance_color_field(const type_desc *type, const type_field *field)
{
    if (!field || field->type != PIN_U32) {
        return false;
    }
    if (type == GAME_COMPONENT_GameMaterial.type) {
        return true;
    }
    return type == GAME_COMPONENT_GameAppearance.type &&
           (field->offset == offsetof(GameAppearance, color) ||
            field->offset == offsetof(GameAppearance, border_color) ||
            field->offset == offsetof(GameAppearance, glow_color));
}

static const editor_choice SHAPES[] = {
    { GAME_SHAPE_BOX, "Box" },
    { GAME_SHAPE_CIRCLE, "Circle" },
    { GAME_SHAPE_DIAMOND, "Diamond" },
    { GAME_SHAPE_CHEVRON, "Chevron" },
    { GAME_SHAPE_STROKED_CHEVRON, "Chevron Outline" },
    { GAME_SHAPE_CAPSULE, "Capsule" },
};
const editor_choices APPEARANCE_SHAPES = { SHAPES, 6 };

bool appearance_shape_field(const type_desc *type, const type_field *field)
{
    return type == GAME_COMPONENT_GameAppearance.type && field &&
           field->type == PIN_I32 &&
           field->offset == offsetof(GameAppearance, shape);
}

bool fallback_appearance_field(const game_prototype_component *component, const type_field *field)
{
    return component->desc->id == GAME_COMPONENT_GameAppearance.id &&
           (appearance_color_field(component->desc->type, field) ||
            field->offset == offsetof(GameAppearance, glow_radius));
}

void property_label(
    const game_prototype_component *component,
    const type_field *field,
    char *label,
    size_t size
)
{
    if (component->desc->id == GAME_COMPONENT_GameMaterial.id) {
        if (field->offset == offsetof(GameMaterial, scene)) {
            snprintf(label, size, "Base Color");
            return;
        }
        if (field->offset == offsetof(GameMaterial, border_scene)) {
            snprintf(label, size, "Border Color");
            return;
        }
    }
    editor_identifier_label(field->name, label, size);
}

bool advanced_material_field(const ecs_component_desc *component, const type_field *field)
{
    if (component->id != GAME_COMPONENT_GameMaterial.id) {
        return false;
    }
    switch (field->offset) {
        case offsetof(GameMaterial, scene):
        case offsetof(GameMaterial, border_scene):
        case offsetof(GameMaterial, emission_color):
        case offsetof(GameMaterial, emission_intensity): return false;
        default: return true;
    }
}

bool begin_property_group(const ecs_component_desc *component, int group, const char *id)
{
    if (!group) {
        return true;
    }
    if (component->id != GAME_COMPONENT_GameMaterial.id) {
        return false;
    }
    bool open = editor_ui_disclosure("Advanced Lighting", "advanced_lighting");
    editor_ui_track_item(id);
    return open;
}

bool component_tree(const ecs_component_desc *component, const char *id, bool *removed)
{
    char name[128];
    editor_component_label(component->name, name, sizeof name);
    return editor_ui_component_header(name, id, removed);
}

bool property_number(const char *label, ImGuiDataType type, void *value, bool overridden)
{
    editor_ui_property_label(label, overridden ? 28.0f : 0.0f);
    return ImGui_InputScalar("###field_value", type, value);
}

static void property_align_value(float width)
{
    ImVec2 cursor = ImGui_GetCursorScreenPos();
    float available = ImGui_CalcItemWidth();
    ImGui_SetCursorScreenPos((ImVec2){ cursor.x + fmaxf(0.0f, available - width),
                                       cursor.y });
}

bool property_checkbox(const char *label, bool *value, bool overridden)
{
    editor_ui_property_label(label, overridden ? 28.0f : 0.0f);
    property_align_value(ImGui_GetFrameHeight());
    return ImGui_Checkbox("###field_value", value);
}

void property_text(const char *label, const char *value)
{
    editor_ui_property_label(label, 0.0f);
    property_align_value(ImGui_CalcTextSize(value).x);
    ImGui_TextDisabled("%s", value);
}
