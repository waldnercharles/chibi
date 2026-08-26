#ifndef CBLUEPRINT_APP_SCENE_IMGUI_H
#define CBLUEPRINT_APP_SCENE_IMGUI_H

#include <stdbool.h>
#include <stddef.h>

/* The scene authoring panel uses this small C ABI surface. The host links the
 * full Dear ImGui bindings through cute_framework. */
typedef struct ImVec2_t
{
    float x;
    float y;
} ImVec2;

typedef struct ImFont_t ImFont;

typedef int ImGuiChildFlags;
typedef int ImGuiHoveredFlags;
typedef int ImGuiInputTextFlags;
typedef int ImGuiSelectableFlags;
typedef int ImGuiTreeNodeFlags;
typedef int ImGuiWindowFlags;
typedef int ImGuiPopupFlags;
typedef int ImGuiCond;

enum
{
    ImGuiCond_Always = 1 << 0,
    ImGuiCond_Once = 1 << 1,
    ImGuiCond_FirstUseEver = 1 << 2,
    ImGuiTreeNodeFlags_Selected = 1 << 0,
    ImGuiTreeNodeFlags_Leaf = 1 << 8,
    ImGuiTreeNodeFlags_SpanAvailWidth = 1 << 11,
    ImGuiMouseCursor_Hand = 7,
};

void ImGui_PushFontFloat(ImFont *font, float font_size_base_unscaled);
void ImGui_PopFont(void);
void ImGui_SetNextWindowSize(ImVec2 size, ImGuiCond condition);
void ImGui_SetNextWindowFocus(void);
void ImGui_SetWindowFocusStr(const char *name);
ImVec2 ImGui_GetContentRegionAvail(void);
void ImGui_NewLine(void);
bool ImGui_Begin(const char *name, bool *open, ImGuiWindowFlags flags);
void ImGui_End(void);
bool ImGui_BeginChild(const char *id, ImVec2 size, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags);
void ImGui_EndChild(void);
bool ImGui_InputText(const char *label, char *buffer, size_t buffer_size, ImGuiInputTextFlags flags);
bool ImGui_Button(const char *label);
void ImGui_SameLine(void);
void ImGui_IndentEx(float width);
void ImGui_UnindentEx(float width);
bool ImGui_BeginPopup(const char *id, ImGuiWindowFlags flags);
bool ImGui_BeginPopupContextWindowEx(const char *id, ImGuiPopupFlags flags);
void ImGui_EndPopup(void);
void ImGui_OpenPopup(const char *id, ImGuiPopupFlags flags);
void ImGui_CloseCurrentPopup(void);
bool ImGui_MenuItem(const char *label);
bool ImGui_MenuItemEx(const char *label, const char *icon, bool selected, bool enabled);
void ImGui_TextUnformatted(const char *text);
bool ImGui_Checkbox(const char *label, bool *value);
void ImGui_Separator(void);
bool ImGui_SelectableEx(const char *label, bool selected, ImGuiSelectableFlags flags, ImVec2 size);
bool ImGui_TreeNode(const char *label);
bool ImGui_TreeNodeEx(const char *label, ImGuiTreeNodeFlags flags);
void ImGui_TreePop(void);
void ImGui_SetNextItemOpen(bool open, ImGuiCond condition);
bool ImGui_IsItemClicked(void);
bool ImGui_IsItemHovered(ImGuiHoveredFlags flags);
void ImGui_SetMouseCursor(int cursor);
bool ImGui_IsMouseReleased(int button);
bool ImGui_IsMouseDoubleClicked(int button);
void ImGui_PushIDInt(int int_id);
void ImGui_PopID(void);

#endif
