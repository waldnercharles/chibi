#include "editor/ui/choices.h"

#include "editor/ui/ui_shell.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <stdio.h>
#include <string.h>

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

bool editor_choices_draw_name(const char *id, const char **value, const editor_choices *choices)
{
    const char *preview = *value && (*value)[0] ? *value : "Choose...";
    for (int i = 0; i < choices->count; i++) {
        if (*value && !strcmp(choices->items[i].name, *value)) {
            preview = choices->items[i].label;
            break;
        }
    }
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
        assert(choice->name && choice->label && choice->label[0]);
        bool selected = *value && !strcmp(*value, choice->name);
        if (ImGui_SelectableEx(choice->label, selected, 0, (ImVec2){ 0, 0 })) {
            changed = !selected;
            *value = choice->name;
        }
        char item[256];
        snprintf(item, sizeof item, "choice.%s", choice->name);
        editor_ui_track_item(item);
    }
    ImGui_EndCombo();
    ImGui_PopID();
    return changed;
}
