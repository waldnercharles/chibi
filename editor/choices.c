#include "choices.h"

#include "ui_shell.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <stdio.h>

void editor_choices_format(int value, const editor_choices *choices, char *buf, size_t cap)
{
    if (!cap) {
        return;
    }
    for (int i = 0; i < choices->count; i++) {
        const editor_choice *choice = &choices->items[i];
        assert(choice->label && choice->label[0]);
        if (choice->value == value) {
            snprintf(buf, cap, "%s", choice->label);
            return;
        }
    }
    snprintf(buf, cap, "Unknown (%d)", value);
}

bool editor_choices_draw(const char *id, int *value, const editor_choices *choices)
{
    char preview[256];
    editor_choices_format(*value, choices, preview, sizeof preview);
    ImGui_PushID(id);
    bool open = ImGui_BeginCombo("##value", preview, 0);
    editor_ui_track_item(id);
    if (!open) {
        ImGui_PopID();
        return false;
    }
    bool changed = false;
    for (int i = 0; i < choices->count; i++) {
        const editor_choice *choice = &choices->items[i];
        assert(choice->label && choice->label[0]);
        ImGui_PushIDInt(i);
        if (ImGui_SelectableEx(choice->label, *value == choice->value, 0, (ImVec2){ 0, 0 })) {
            changed = *value != choice->value;
            *value = choice->value;
        }
        char item[64];
        snprintf(item, sizeof item, "choice.%d", choice->value);
        editor_ui_track_item(item);
        ImGui_PopID();
    }
    ImGui_EndCombo();
    ImGui_PopID();
    return changed;
}
