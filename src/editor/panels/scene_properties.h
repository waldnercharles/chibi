#ifndef CHIBI_SCENE_PROPERTIES_H
#define CHIBI_SCENE_PROPERTIES_H
#include "assets/prototype.h"
#include "editor/ui/choices.h"
#include "editor/ui/flags.h"
#include "runtime/components.h"

#include <imgui/dcimgui.h>

bool advanced_material_field(const ecs_component_desc *component, const type_field *field);
bool appearance_color_field(const type_desc *type, const type_field *field);
const editor_choices *property_choices(const type_desc *type, const type_field *field);
bool property_field_visible(
    const ecs_component_desc *component,
    const type_field *field,
    const void *data
);
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
int property_vector_width(const char *type_name);
bool property_environment(const char *label, const type_field *field, float *value, bool overridden);
void property_text(const char *label, const char *value);
bool property_shader(const char *label, const GameShader *binding, const char *field, char *text, size_t size);
bool property_mesh_parts(char *text, size_t size);
extern const editor_flags COLLISION_CATEGORIES;
#endif
