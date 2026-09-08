#ifndef CBLUEPRINT_SCENE_PROPERTIES_H
#define CBLUEPRINT_SCENE_PROPERTIES_H
#include "assets/prototype.h"
#include "editor/ui/choices.h"
#include "editor/ui/flags.h"
#include "runtime/components.h"

#include <imgui/dcimgui.h>

bool advanced_material_field(const ecs_component_desc *component, const type_field *field);
bool appearance_color_field(const type_desc *type, const type_field *field);
bool appearance_shape_field(const type_desc *type, const type_field *field);
bool begin_property_group(const ecs_component_desc *component, int group, const char *id);
bool collision_category_field(const type_desc *type, const type_field *field);
bool component_tree(const ecs_component_desc *component, const char *id, bool *removed);
bool fallback_appearance_field(const game_prototype_component *component, const type_field *field);
bool property_checkbox(const char *label, bool *value, bool overridden);
void property_label(
    const game_prototype_component *component,
    const type_field *field,
    char *label,
    size_t size
);
bool property_number(const char *label, ImGuiDataType type, void *value, bool overridden);
void property_text(const char *label, const char *value);
bool property_shader(const char *label, const GameShader *binding, const char *field, char *text, size_t size);
extern const editor_choices APPEARANCE_SHAPES;
extern const editor_flags COLLISION_CATEGORIES;
#endif
