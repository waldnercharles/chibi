#include "editor/panels/shader_details.h"
#include "editor/panels/shader_inline.h"
#include "editor/workspace/editor_internal.h"
#include "graph/shader/shader_graph.h"

#include <imgui/dcimgui.h>
#include <stdio.h>
#include <string.h>

static void parameter_type(editor *e, node *n)
{
    const char *names[] = { "Scalar",   "Vector 2", "Vector 3",
                            "Vector 4", "Color",    "Boolean" };
    const char *types[] = { NULL,         "ShaderVec2", "ShaderVec3",
                            "ShaderVec4", "ShaderVec3", NULL };
    pin_spec *pin = &n->output_pins[0];
    int current = pin->type == PIN_BOOL ? 5 : 0;
    for (int i = 1; i < 4; i++) {
        if (pin->type_name && !strcmp(pin->type_name, types[i])) {
            current = i;
        }
    }
    if ((pin->flags & PIN_FLAG_COLOR) && current >= 2) {
        current = 4;
    }
    char preview[64];
    snprintf(preview, sizeof preview, "Type: %s", names[current]);
    bool open = ImGui_BeginCombo("##Type", preview, 0);
    ed_shader_track_item("shader.parameter.type");
    if (!open) {
        return;
    }
    for (int i = 0; i < 6; i++) {
        bool selected = ImGui_SelectableEx(names[i], i == current, 0, (ImVec2){ 0 });
        char id[64];
        snprintf(id, sizeof id, "shader.parameter.type.%d", i);
        ed_shader_track_item(id);
        if (!selected || i == current) {
            continue;
        }
        float components[4] = { 0 };
        if (!current) {
            components[0] = pin->default_value.f;
        } else if (current == 5) {
            components[0] = pin->default_value.b ? 1 : 0;
        } else if (pin->default_value.ptr) {
            memcpy(components, pin->default_value.ptr, (size_t)ed_vector_width(pin) * sizeof(float));
        }
        ed_snapshot_before(e);
        pin_type type = i == 5 ? PIN_BOOL : i ? PIN_STRUCT : PIN_F32;
        graph_set_pin_type(e->active_graph, n, 1, 0, type, types[i]);
        pin->flags = (pin->flags & ~PIN_FLAG_COLOR) | (i == 4 ? PIN_FLAG_COLOR : 0);
        pin_value value = { .type = type, .type_name = types[i] };
        if (i == 5) {
            value.b = components[0] != 0;
        } else if (i) {
            value.ptr = components;
        } else {
            value.f = components[0];
        }
        graph_set_pin_default(e->active_graph, n, 1, 0, &value);
    }
    ImGui_EndCombo();
}
static void setting_choice(editor *e, node *n, int input, const char *label, const char *const *names, int count)
{
    int selected = n->input_pins[input].default_value.i;
    const char *current = selected >= 0 && selected < count ? names[selected] : "Invalid";
    char preview[128], combo_id[96];
    snprintf(preview, sizeof preview, "%s: %s", label, current);
    snprintf(combo_id, sizeof combo_id, "##%s", label);
    bool open = ImGui_BeginCombo(combo_id, preview, 0);
    char id[80];
    snprintf(id, sizeof id, "shader.setting.%d", input);
    ed_shader_track_item(id);
    if (!open) {
        return;
    }
    for (int i = 0; i < count; i++) {
        bool chosen = ImGui_SelectableEx(names[i], selected == i, 0, (ImVec2){ 0 });
        snprintf(id, sizeof id, "shader.setting.%d.%d", input, i);
        ed_shader_track_item(id);
        if (chosen && i != selected) {
            ed_snapshot_before(e);
            pin_value value = { .type = PIN_I32, .i = i };
            graph_set_pin_default(e->active_graph, n, 0, input, &value);
        }
    }
    ImGui_EndCombo();
}

void ed_draw_shader_controls(editor *e, node *n)
{
    if (!strcmp(n->desc->name, "Shader Parameter") ||
        !strcmp(n->desc->name, "Shader Constant")) {
        if (!strcmp(n->desc->name, "Shader Parameter")) {
            char name[128];
            snprintf(name, sizeof name, "%s", n->reference_name ? n->reference_name : "");
            if (ImGui_InputTextWithHint("##parameter_name", "Parameter name", name, sizeof name, 0)) {
                ed_details_begin_mutation(e);
                graph_set_node_reference(e->active_graph, n, n->reference_id, name);
            }
            ed_shader_track_item("shader.parameter.name");
            ed_details_finish_mutation(e, ImGui_IsItemEdited());
        }
        parameter_type(e, n);
    } else if (!strcmp(n->desc->name, "Shader Color Ramp")) {
        ed_draw_shader_ramp_controls(e, n);
    } else if (!strcmp(n->desc->name, "Shader Gradient")) {
        const char *const names[] = { "Linear X", "Linear Y", "Radial", "Angular" };
        setting_choice(e, n, 1, "Type", names, 4);
    } else if (!strcmp(n->desc->name, "Shader Image Texture")) {
        ed_draw_shader_image(e, n);
        const char *const spaces[] = { "sRGB", "Linear" };
        const char *const filters[] = { "Nearest", "Linear" };
        const char *const wraps[] = { "Repeat", "Clamp" };
        setting_choice(e, n, SHADER_IMAGE_INPUT_COLOR_SPACE, "Color space", spaces, 2);
        setting_choice(e, n, SHADER_IMAGE_INPUT_FILTER, "Filter", filters, 2);
        setting_choice(e, n, SHADER_IMAGE_INPUT_WRAP, "Wrap", wraps, 2);
    }
}
