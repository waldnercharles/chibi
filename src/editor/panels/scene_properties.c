#include "editor/panels/scene_properties.h"

#include "editor/ui/labels.h"
#include "editor/ui/ui_shell.h"
#include "render/arena_parts.h"
#include "render/particles.h"
#include "render/shader_catalog.h"
#include "runtime/generated.h"
#include "runtime/material_fluid.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

bool property_shader(const char *label, const GameShader *binding, const char *field, char *text, size_t size)
{
    if (strcmp(field, "parameters")) {
        bool shape = !strcmp(field, "shape");
        float open_width = ImGui_CalcTextSize("Open").x +
                           ImGui_GetStyle()->FramePadding.x * 2.0f +
                           ImGui_GetStyle()->ItemSpacing.x;
        editor_ui_property_label(label, open_width);
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
            bool edit = editor_ui_vector_float("parameter", values[slot], width, 0, 0);
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

/* Collision bits authored by gameplay prototypes. */
static const editor_flag COLLISION_FLAGS[] = {
    { 1u, "Player" },
    { 2u, "Enemy" },
    { 4u, "Projectile" },
    { 8u, "World" },
};
const editor_flags COLLISION_CATEGORIES = { COLLISION_FLAGS, 4 };

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
    if (type == GAME_COMPONENT_GamePointLight.type) {
        return field->offset == offsetof(GamePointLight, color);
    }
    if (type == GAME_COMPONENT_GameSurfaceInfluence.type) {
        return field->offset == offsetof(GameSurfaceInfluence, color);
    }
    if (type == GAME_COMPONENT_GameTrail.type) {
        return field->offset == offsetof(GameTrail, color);
    }
    if (type == GAME_COMPONENT_GameParticleEmitter.type) {
        return field->offset == offsetof(GameParticleEmitter, color) ||
               field->offset == offsetof(GameParticleEmitter, end_color);
    }
    if (type == GAME_COMPONENT_GameWidget.type) {
        return field->offset == offsetof(GameWidget, color) ||
               field->offset == offsetof(GameWidget, background) ||
               field->offset == offsetof(GameWidget, accent);
    }
    if (type == GAME_COMPONENT_GameFog.type) {
        return field->offset == offsetof(GameFog, color);
    }
    if (type == GAME_COMPONENT_GameMapMarker.type) {
        return field->offset == offsetof(GameMapMarker, color);
    }
    if (type == GAME_COMPONENT_GameSensorContact.type) {
        return field->offset == offsetof(GameSensorContact, color);
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
    { GAME_SHAPE_TRIANGLE, "Triangle" },
    { GAME_SHAPE_STROKED_TRIANGLE, "Triangle Outline" },
};
static const editor_choices APPEARANCE_SHAPES = { SHAPES, 8 };
static const editor_choice PRIMITIVES[] = {
    { GAME_PRIMITIVE_BOX, "Box" },
    { GAME_PRIMITIVE_SPHERE, "Sphere" },
    { GAME_PRIMITIVE_CAPSULE, "Capsule" },
    { GAME_PRIMITIVE_CONE, "Cone" },
    { GAME_PRIMITIVE_TRIANGLE_PRISM, "Triangle Prism" },
    { GAME_PRIMITIVE_OCTAHEDRON, "Octahedron" },
    { GAME_PRIMITIVE_TETRAHEDRON, "Tetrahedron" },
    { GAME_PRIMITIVE_CHEVRON_PYRAMID, "Chevron Pyramid" },
    { GAME_PRIMITIVE_BOX_FRAME, "Box Frame" },
    { GAME_PRIMITIVE_TRIANGLE_OUTLINE, "Triangle Outline" },
    { GAME_PRIMITIVE_TRIANGLE, "Triangle" },
};
static const editor_choices SPATIAL_PRIMITIVES = { PRIMITIVES, GAME_PRIMITIVE_COUNT };
static const editor_choice PARTICLE_SHAPES[] = {
    { GAME_PARTICLE_ROUND, "Round" },
    { GAME_PARTICLE_TRIANGLE, "Triangle" },
    { GAME_PARTICLE_TRIANGLE_OUTLINE, "Triangle Outline" },
    { GAME_PARTICLE_RING, "Ring" },
    { GAME_PARTICLE_STREAK, "Streak" },
};
static const editor_choices EMITTER_SHAPES = { PARTICLE_SHAPES,
                                               GAME_PARTICLE_SHAPE_COUNT };
static const editor_choice BODY_TYPES[] = {
    { GAME_BODY_STATIC, "Static" },
    { GAME_BODY_DYNAMIC, "Dynamic" },
    { GAME_BODY_KINEMATIC, "Kinematic" },
};
static const editor_choices PHYSICS_BODIES = { BODY_TYPES, 3 };
static const editor_choice COLLIDER_SHAPES[] = {
    { GAME_COLLIDER_CIRCLE, "Circle" },
    { GAME_COLLIDER_BOX, "Box" },
};
static const editor_choices PHYSICS_COLLIDERS = { COLLIDER_SHAPES, 2 };
static const editor_choice ENCOUNTER_STATES[] = {
    { GAME_ENCOUNTER_ACTIVE, "Active" },
    { GAME_ENCOUNTER_CLEARED, "Cleared" },
    { GAME_ENCOUNTER_DEFEATED, "Defeated" },
};
static const editor_choices ENCOUNTER_STATUS = { ENCOUNTER_STATES, 3 };
static const editor_choice ENCOUNTER_ROLES[] = {
    { GAME_ENCOUNTER_PLAYER, "Player" },
    { GAME_ENCOUNTER_ENEMY, "Enemy" },
};
static const editor_choices ENCOUNTER_MEMBERS = { ENCOUNTER_ROLES, 2 };
static const editor_choice FLUID_KINDS[] = {
    { FLUID_GAS, "Gas" },
    { FLUID_LIQUID, "Liquid" },
};
static const editor_choices MATERIAL_FLUID_KINDS = { FLUID_KINDS, 2 };
static const editor_choice HRC_LUMINANCE_MODES[] = {
    { 0, "Independent" },
    { 1, "Reuse transport" },
};
static const editor_choices HRC_LUMINANCE_MODE_CHOICES = { HRC_LUMINANCE_MODES, 2 };
static const editor_choice HRC_UPSCALE_MODES[] = {
    { 0, "Plain bilinear" },
    { 1, "Edge aware" },
    { 2, "Joint bilateral" },
    { 3, "Five tap cross" },
};
static const editor_choices HRC_UPSCALE_MODE_CHOICES = { HRC_UPSCALE_MODES, 4 };
static const editor_choice HRC_DEBUG_VIEWS[] = {
    { 0, "Normal" },
    { 1, "Scene" },
    { 2, "Emission" },
    { 3, "Absorption" },
    { 4, "Albedo" },
    { 5, "Fluence" },
    { 6, "Normals" },
};
static const editor_choices HRC_DEBUG_VIEW_CHOICES = { HRC_DEBUG_VIEWS, 7 };

static bool part_vector(const char *label, CF_V3 *value)
{
    editor_ui_property_label(label, 0);
    return editor_ui_vector_float("value", (float *)value, 3, 0, 0);
}

bool property_mesh_parts(char *text, size_t size)
{
    arena_parts parts;
    char error[160];
    if (!render_arena_parts_parse(text, &parts, error, sizeof error)) {
        ImGui_TextWrapped("%s", error);
        if (ImGui_SmallButton("Reset parts")) {
            snprintf(text, size, "[]");
            return true;
        }
        return false;
    }
    bool changed = false;
    ImGui_BeginGroup();
    for (int i = 0; i < parts.count; i++) {
        arena_part *part = &parts.parts[i];
        ImGui_PushIDInt(i);
        char label[48], id[64];
        snprintf(label, sizeof label, "Part %d", i + 1);
        bool removed = false;
        snprintf(id, sizeof id, "mesh.part.%d", i);
        if (editor_ui_component_header(label, id, &removed)) {
            editor_ui_property_label("Shape", 0);
            changed |= editor_choices_draw("##primitive", &part->primitive, &SPATIAL_PRIMITIVES);
            ImGui_PushID("size");
            changed |= part_vector("Size", &part->dimensions);
            snprintf(id, sizeof id, "mesh.part.%d.size", i);
            editor_ui_track_item(id);
            ImGui_PopID();
            ImGui_PushID("position");
            changed |= part_vector("Position", &part->position);
            ImGui_PopID();
            ImGui_PushID("rotation");
            CF_V3 degrees = cf_mul_v3_f(part->rotation, 180.0f / 3.141592654f);
            if (part_vector("Pitch / Yaw / Roll", &degrees)) {
                part->rotation = cf_mul_v3_f(degrees, 3.141592654f / 180.0f);
                changed = true;
            }
            snprintf(id, sizeof id, "mesh.part.%d.rotation", i);
            editor_ui_track_item(id);
            ImGui_PopID();
            editor_ui_property_label("Material", 0);
            if (ImGui_BeginCombo("##material", *part->material ? part->material : "Inherit", 0)) {
                if (ImGui_Selectable("Inherit")) {
                    part->material[0] = 0;
                    changed = true;
                }
                for (int material = 1; material <= render_shader_count(); material++) {
                    if (!render_shader_is_shape(material) &&
                        ImGui_Selectable(render_shader_name(material))) {
                        snprintf(part->material, sizeof part->material, "%s", render_shader_name(material));
                        changed = true;
                    }
                }
                ImGui_EndCombo();
            }
            ImGui_TreePop();
        }
        ImGui_PopID();
        if (removed) {
            memmove(part, part + 1, (size_t)(parts.count - i - 1) * sizeof *part);
            parts.count--;
            changed = true;
            break;
        }
    }
    if (parts.count < ARENA_PART_LIMIT) {
        bool add = ImGui_SmallButton("Add part");
        editor_ui_track_item("mesh.parts.add");
        if (add) {
            parts.parts[parts.count++] = (arena_part){ .primitive = GAME_PRIMITIVE_TRIANGLE_PRISM,
                                                       .dimensions = { 12, 4, 8 } };
            changed = true;
        }
    }
    if (changed) {
        char *json = render_arena_parts_write(&parts);
        changed = json && strlen(json) < size;
        if (changed) {
            snprintf(text, size, "%s", json);
        }
        free(json);
    }
    ImGui_EndGroup();
    return changed;
}

static const editor_choice WIDGET_TYPES[] = {
    { GAME_WIDGET_LABEL, "Label" },   { GAME_WIDGET_PANEL, "Panel" },
    { GAME_WIDGET_BUTTON, "Button" }, { GAME_WIDGET_PROGRESS, "Progress" },
    { GAME_WIDGET_SLIDER, "Slider" }, { GAME_WIDGET_SLOT, "Slot" },
    { GAME_WIDGET_MAP, "Map" },
};
static const editor_choices WIDGET_KINDS = { WIDGET_TYPES, 7 };

const editor_choices *property_choices(const type_desc *type, const type_field *field)
{
    if (!field || (field->type != PIN_I32 && field->type != PIN_ENUM)) {
        return NULL;
    }
    if (type == GAME_COMPONENT_GameWidget.type &&
        field->offset == offsetof(GameWidget, kind)) {
        return &WIDGET_KINDS;
    }
    if (type == GAME_COMPONENT_GameAppearance.type &&
        field->offset == offsetof(GameAppearance, shape)) {
        return &APPEARANCE_SHAPES;
    }
    if (type == GAME_COMPONENT_GameGeometry3D.type &&
        field->offset == offsetof(GameGeometry3D, primitive)) {
        return &SPATIAL_PRIMITIVES;
    }
    if (type == GAME_COMPONENT_GameParticleEmitter.type &&
        field->offset == offsetof(GameParticleEmitter, shape)) {
        return &EMITTER_SHAPES;
    }
    if (type == GAME_COMPONENT_GameFluidVolume.type &&
        field->offset == offsetof(GameFluidVolume, kind)) {
        return &MATERIAL_FLUID_KINDS;
    }
    if (type == GAME_COMPONENT_GameParticleEmitter.type &&
        field->offset == offsetof(GameParticleEmitter, fluid_kind)) {
        return &MATERIAL_FLUID_KINDS;
    }
    if (type == GAME_COMPONENT_GameBody.type &&
        field->offset == offsetof(GameBody, type)) {
        return &PHYSICS_BODIES;
    }
    if (type == GAME_COMPONENT_GameCollider.type &&
        field->offset == offsetof(GameCollider, shape)) {
        return &PHYSICS_COLLIDERS;
    }
    if (type == GAME_COMPONENT_GameEncounter.type &&
        field->offset == offsetof(GameEncounter, status)) {
        return &ENCOUNTER_STATUS;
    }
    if (type == GAME_COMPONENT_GameEncounterMember.type &&
        field->offset == offsetof(GameEncounterMember, role)) {
        return &ENCOUNTER_MEMBERS;
    }
    if (type == GAME_COMPONENT_GameHRC2D.type) {
        if (field->offset == offsetof(GameHRC2D, luminance_mode)) {
            return &HRC_LUMINANCE_MODE_CHOICES;
        }
        if (field->offset == offsetof(GameHRC2D, upscale_mode)) {
            return &HRC_UPSCALE_MODE_CHOICES;
        }
        if (field->offset == offsetof(GameHRC2D, debug_view)) {
            return &HRC_DEBUG_VIEW_CHOICES;
        }
    }
    return field->type == PIN_ENUM ? editor_choices_for_enum(field->type_name) : NULL;
}

bool property_field_visible(const ecs_component_desc *component, const type_field *field, const void *data)
{
    if (component->id != GAME_COMPONENT_GameCollider.id) {
        return true;
    }
    const GameCollider *collider = data;
    if (field->offset == offsetof(GameCollider, radius)) {
        return collider->shape != GAME_COLLIDER_BOX;
    }
    if (field->offset == offsetof(GameCollider, width) ||
        field->offset == offsetof(GameCollider, height)) {
        return collider->shape != GAME_COLLIDER_CIRCLE;
    }
    return true;
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

int property_vector_width(const char *type_name)
{
    if (!type_name) {
        return 0;
    }
    const char *names[] = { "CF_V2", "CF_V3", "CF_V4" };
    for (int i = 0; i < 3; i++) {
        if (!strcmp(type_name, names[i])) {
            return i + 2;
        }
    }
    return 0;
}

bool property_environment(const char *label, const type_field *field, float *value, bool)
{
    float minimum = 0, maximum = 0, scale = 1;
    const char *format = "%.2f";
    switch (field->offset) {
        case offsetof(GameEnvironment, exposure):
            minimum = -8;
            maximum = 8;
            format = "%.2f EV";
            break;
        case offsetof(GameEnvironment, bloom_strength): maximum = 2; break;
        case offsetof(GameEnvironment, bloom_threshold): maximum = 32; break;
        case offsetof(GameEnvironment, bloom_radius):
            maximum = 5;
            scale = 100;
            format = "%.2f%% height";
            break;
        default: assert(false); return false;
    }
    editor_ui_property_label(label, 28.0f);
    float shown = *value * scale;
    bool changed = ImGui_SliderFloatEx("###field_value", &shown, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
    if (changed) {
        *value = shown / scale;
    }
    return changed;
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
