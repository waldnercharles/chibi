#include "editor/panels/shader_details.h"
#include "editor/panels/shader_inline.h"
#include "editor/workspace/editor_internal.h"
#include "graph/shader/shader_graph.h"

#include <imgui/dcimgui.h>
#include <stdio.h>
#include <string.h>

static void setting_choice(editor *e, node *n, int input, const char *label, const char *const *names, int count)
{
    int selected = n->input_pins[input].default_value.i;
    const char *current = selected >= 0 && selected < count ? names[selected] : "Invalid";
    bool open = ImGui_BeginCombo(label, current, 0);
    char id[80];
    snprintf(id, sizeof id, "shader.setting.%d", input);
    editor_ui_track_item(id);
    if (!open) {
        return;
    }
    for (int i = 0; i < count; i++) {
        bool chosen = ImGui_SelectableEx(names[i], selected == i, 0, (ImVec2){ 0 });
        snprintf(id, sizeof id, "shader.setting.%d.%d", input, i);
        editor_ui_track_item(id);
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
    if (!strcmp(n->desc->name, "Shader Color Ramp")) {
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
