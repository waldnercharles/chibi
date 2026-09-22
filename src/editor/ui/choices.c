#include "editor/ui/choices.h"

#include "editor/ui/search.h"
#include "editor/ui/ui_shell.h"
#include "graph/model/registry.h"

#include <assert.h>
#include <imgui/dcimgui.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int editor_choices_count(const editor_choices *choices)
{
    return choices->enumeration ? choices->enumeration->value_count : choices->count;
}

static editor_choice editor_choice_at(const editor_choices *choices, int index)
{
    if (!choices->enumeration) {
        return choices->items[index];
    }
    const enum_value_desc *value = &choices->enumeration->values[index];
    return (editor_choice){ .value = value->value,
                            .label = value->label,
                            .name = value->name };
}

void editor_choices_format(int value, const editor_choices *choices, char *buf, size_t cap)
{
    if (!cap) {
        return;
    }
    for (int i = 0; i < editor_choices_count(choices); i++) {
        editor_choice choice = editor_choice_at(choices, i);
        assert(choice.label && choice.label[0]);
        if (choice.value == value) {
            snprintf(buf, cap, "%s", choice.label);
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
    bool searchable = editor_choices_count(choices) > 10;
    bool open = ImGui_BeginCombo("##value", preview, searchable ? ImGuiComboFlags_HeightLarge : 0);
    editor_ui_track_item(id);
    if (!open) {
        ImGui_PopID();
        return false;
    }
    static char query[128];
    static int focused;
    int matches = 0;
    bool choose = false;
    if (searchable) {
        if (ImGui_IsWindowAppearing()) {
            query[0] = '\0';
            focused = 0;
            ImGui_SetKeyboardFocusHere();
        }
        ImGui_SetNextItemWidth(-1);
        if (editor_ui_search_field("choice.search", query, sizeof query, "Search options")) {
            focused = 0;
        }
        for (int i = 0; i < editor_choices_count(choices); i++) {
            editor_choice choice = editor_choice_at(choices, i);
            matches += editor_search_score(query, choice.label, NULL) >= 0;
        }
        if (matches && ImGui_IsKeyPressed(ImGuiKey_DownArrow)) {
            focused = (focused + 1) % matches;
        }
        if (matches && ImGui_IsKeyPressed(ImGuiKey_UpArrow)) {
            focused = (focused + matches - 1) % matches;
        }
        choose = ImGui_IsKeyPressed(ImGuiKey_Enter);
        if (ImGui_IsKeyPressed(ImGuiKey_Escape)) {
            ImGui_CloseCurrentPopup();
        }
        float height = fminf(
            ImGui_GetMainViewport()->WorkSize.y - 140,
            ImGui_GetTextLineHeightWithSpacing() * 8
        );
        ImGui_BeginChild("choice.results", (ImVec2){ 0, fmaxf(height, 1) }, 0, 0);
    }
    bool changed = false;
    int row = 0;
    for (int i = 0; i < editor_choices_count(choices); i++) {
        editor_choice choice = editor_choice_at(choices, i);
        assert(choice.label && choice.label[0]);
        if (searchable && editor_search_score(query, choice.label, NULL) < 0) {
            continue;
        }
        ImGui_PushIDInt(i);
        bool highlighted = searchable ? row == focused : *value == choice.value;
        if (ImGui_SelectableEx(choice.label, highlighted, 0, (ImVec2){ 0, 0 }) ||
            (choose && highlighted)) {
            changed = *value != choice.value;
            *value = choice.value;
            ImGui_CloseCurrentPopup();
        }
        if (searchable && highlighted &&
            (ImGui_IsKeyPressed(ImGuiKey_UpArrow) ||
             ImGui_IsKeyPressed(ImGuiKey_DownArrow))) {
            ImGui_SetScrollHereY(0.5f);
        }
        char item[64];
        snprintf(item, sizeof item, "choice.%d", choice.value);
        editor_ui_track_item(item);
        ImGui_PopID();
        row++;
    }
    if (searchable) {
        if (!matches) {
            ImGui_TextDisabled("No matching options");
        }
        ImGui_EndChild();
    }
    ImGui_EndCombo();
    ImGui_PopID();
    return changed;
}

bool editor_choices_draw_name(const char *id, const char **value, const editor_choices *choices)
{
    const char *preview = *value && (*value)[0] ? *value : "Choose...";
    for (int i = 0; i < editor_choices_count(choices); i++) {
        editor_choice choice = editor_choice_at(choices, i);
        if (*value && !strcmp(choice.name, *value)) {
            preview = choice.label;
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
    for (int i = 0; i < editor_choices_count(choices); i++) {
        editor_choice choice = editor_choice_at(choices, i);
        assert(choice.name && choice.label && choice.label[0]);
        bool selected = *value && !strcmp(*value, choice.name);
        if (ImGui_SelectableEx(choice.label, selected, 0, (ImVec2){ 0, 0 })) {
            changed = !selected;
            *value = choice.name;
        }
        char item[256];
        snprintf(item, sizeof item, "choice.%s", choice.name);
        editor_ui_track_item(item);
    }
    ImGui_EndCombo();
    ImGui_PopID();
    return changed;
}

const editor_choices *editor_choices_for_enum(const char *type_name)
{
    static editor_choices choices;
    const enum_desc *enumeration = registry_enum_by_name(type_name);
    if (!enumeration) {
        return NULL;
    }
    choices = (editor_choices){ .enumeration = enumeration };
    return &choices;
}
