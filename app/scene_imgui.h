#ifndef CBLUEPRINT_APP_SCENE_IMGUI_H
#define CBLUEPRINT_APP_SCENE_IMGUI_H

#include <stdbool.h>

/* The scene authoring panel uses this small C ABI surface. The host links the
 * full Dear ImGui bindings through cute_framework. */
typedef struct ImVec2_t
{
    float x;
    float y;
} ImVec2;

typedef int ImGuiInputTextFlags;
typedef int ImGuiSelectableFlags;
typedef int ImGuiWindowFlags;

bool ImGui_Begin(const char *name, bool *open, ImGuiWindowFlags flags);
void ImGui_End(void);
void ImGui_Text(const char *format, ...);
void ImGui_TextUnformatted(const char *text);
bool ImGui_Button(const char *label);
void ImGui_SameLine(void);
bool ImGui_Checkbox(const char *label, bool *value);
bool ImGui_InputFloatEx(
    const char *label,
    float *value,
    float step,
    float step_fast,
    const char *format,
    ImGuiInputTextFlags flags
);
bool ImGui_InputInt(const char *label, int *value);
void ImGui_Separator(void);
bool ImGui_SelectableEx(const char *label, bool selected, ImGuiSelectableFlags flags, ImVec2 size);

#endif
