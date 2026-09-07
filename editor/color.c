#include "color.h"

#include "ui_shell.h"

#include <imgui/dcimgui.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool editor_color_draw(editor_color_state *state, const char *id, unsigned *value, bool *first_change)
{
    *first_change = false;
    float rgba[4];
    for (int i = 0; i < 4; i++) {
        rgba[i] = ((*value >> (24 - 8 * i)) & 255u) / 255.0f;
    }
    ImVec4 color = { rgba[0], rgba[1], rgba[2], rgba[3] };
    ImGui_PushID(id);
    if (ImGui_ColorButtonEx("##swatch", color, 0, (ImVec2){ ImGui_CalcItemWidth(), ImGui_GetFrameHeight() })) {
        state->changed = false;
        snprintf(state->hex, sizeof state->hex, "%08X", *value);
        ImGui_OpenPopup("picker", 0);
    }
    editor_ui_track_item(id);
    if (!ImGui_BeginPopup("picker", 0)) {
        ImGui_PopID();
        return false;
    }
    unsigned original = *value;
    ImGui_SetNextItemWidth(240.0f);
    bool picker_changed = ImGui_ColorPicker4(
        "##rgba",
        rgba,
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs,
        NULL
    );
    editor_ui_track_item("color.picker");
    if (picker_changed) {
        unsigned packed = 0;
        for (int i = 0; i < 4; i++) {
            unsigned channel = (unsigned)(fmaxf(0.0f, fminf(1.0f, rgba[i])) * 255.0f +
                                          0.5f);
            packed |= channel << (24 - 8 * i);
        }
        *value = packed;
        snprintf(state->hex, sizeof state->hex, "%08X", packed);
    }
    if (ImGui_InputText("Hex", state->hex, sizeof state->hex, ImGuiInputTextFlags_CharsHexadecimal) &&
        strlen(state->hex) == 8) {
        *value = (unsigned)strtoul(state->hex, NULL, 16);
    }
    editor_ui_track_item("color.hex");
    if (ImGui_Button("Done")) {
        ImGui_CloseCurrentPopup();
    }
    editor_ui_track_item("color.done");
    ImGui_EndPopup();
    ImGui_PopID();
    bool changed = *value != original;
    *first_change = changed && !state->changed;
    state->changed |= changed;
    return changed;
}

bool editor_color_open(const char *id)
{
    ImGui_PushID(id);
    bool open = ImGui_IsPopupOpen("picker", 0);
    ImGui_PopID();
    return open;
}
