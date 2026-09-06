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
typedef struct ImDrawList_t ImDrawList;
typedef struct ImVec4_t
{
    float x, y, z, w;
} ImVec4;
typedef unsigned int ImU32;
typedef int ImGuiCol;
typedef int ImGuiStyleVar;
typedef int ImDrawFlags;

typedef int ImGuiChildFlags;
typedef int ImGuiHoveredFlags;
typedef int ImGuiInputTextFlags;
typedef int ImGuiSelectableFlags;
typedef int ImGuiTreeNodeFlags;
typedef int ImGuiWindowFlags;
typedef int ImGuiPopupFlags;
typedef int ImGuiCond;
typedef int ImGuiDataType;

enum
{
    ImGuiCond_Always = 1 << 0,
    ImGuiCond_Once = 1 << 1,
    ImGuiCond_FirstUseEver = 1 << 2,
    ImGuiTreeNodeFlags_Selected = 1 << 0,
    ImGuiTreeNodeFlags_NoTreePushOnOpen = 1 << 3,
    ImGuiTreeNodeFlags_Leaf = 1 << 8,
    ImGuiTreeNodeFlags_FramePadding = 1 << 10,
    ImGuiCol_Text = 0,
    ImGuiCol_TextDisabled = 1,
    ImGuiCol_Header = 24,
    ImGuiCol_HeaderHovered = 25,
    ImGuiCol_HeaderActive = 26,
    ImGuiStyleVar_FramePadding = 11,
    ImDrawFlags_RoundCornersAll = 0xF0,
    ImGuiTreeNodeFlags_SpanAvailWidth = 1 << 11,
    ImGuiMouseCursor_Hand = 7,
    ImGuiDataType_S8 = 0,
    ImGuiDataType_U8 = 1,
    ImGuiDataType_S16 = 2,
    ImGuiDataType_U16 = 3,
    ImGuiDataType_S32 = 4,
    ImGuiDataType_U32 = 5,
    ImGuiDataType_S64 = 6,
    ImGuiDataType_U64 = 7,
    ImGuiDataType_Float = 8,
    ImGuiDataType_Double = 9,
    ImGuiDataType_Bool = 10,
    ImGuiDataType_String = 11,
    ImGuiDataType_COUNT = 12,
};

ImDrawList *ImGui_GetWindowDrawList(void);
ImVec2 ImGui_GetCursorScreenPos(void);
ImVec2 ImGui_GetItemRectMin(void);
ImVec2 ImGui_GetItemRectMax(void);
ImU32 ImGui_GetColorU32(ImGuiCol index);
void ImGui_PushStyleColorImVec4(ImGuiCol index, ImVec4 color);
void ImGui_PopStyleColorEx(int count);
void ImGui_PushStyleVarImVec2(ImGuiStyleVar index, ImVec2 value);
void ImGui_PopStyleVar(void);
float ImGui_GetTreeNodeToLabelSpacing(void);
void ImDrawList_ChannelsSplit(ImDrawList *draw, int count);
void ImDrawList_ChannelsSetCurrent(ImDrawList *draw, int index);
void ImDrawList_ChannelsMerge(ImDrawList *draw);
void ImDrawList_AddRectFilledEx(
    ImDrawList *draw,
    ImVec2 min,
    ImVec2 max,
    ImU32 color,
    float rounding,
    ImDrawFlags flags
);
void ImDrawList_AddText(ImDrawList *draw, ImVec2 position, ImU32 color, const char *text);
void ImDrawList_PushClipRect(ImDrawList *draw, ImVec2 min, ImVec2 max, bool intersect);
void ImDrawList_PopClipRect(ImDrawList *draw);
double ImGui_GetTime(void);
bool ImGui_BeginDragDropSource(int flags);
bool ImGui_SetDragDropPayload(const char *type, const void *data, size_t size, ImGuiCond condition);
void ImGui_EndDragDropSource(void);
void ImGui_PushFontFloat(ImFont *font, float font_size_base_unscaled);
void ImGui_PopFont(void);
float ImGui_GetFontSize(void);
void ImGui_SetTooltip(const char *format, ...);
void ImGui_PushID(const char *id);
void ImGui_SetNextWindowSize(ImVec2 size, ImGuiCond condition);
void ImGui_SetNextWindowFocus(void);
void ImGui_SetNextItemWidth(float width);
void ImGui_SetWindowFocusStr(const char *name);
ImVec2 ImGui_GetContentRegionAvail(void);
void ImGui_NewLine(void);
bool ImGui_Begin(const char *name, bool *open, ImGuiWindowFlags flags);
void ImGui_End(void);
bool ImGui_BeginChild(const char *id, ImVec2 size, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags);
void ImGui_EndChild(void);
bool ImGui_InputText(const char *label, char *buffer, size_t buffer_size, ImGuiInputTextFlags flags);
bool ImGui_InputScalar(const char *label, ImGuiDataType data_type, void *data);
bool ImGui_InputInt(const char *label, int *value);
bool ImGui_InputFloatEx(
    const char *label,
    float *value,
    float step,
    float step_fast,
    const char *format,
    ImGuiInputTextFlags flags
);
bool ImGui_InputDoubleEx(
    const char *label,
    double *value,
    double step,
    double step_fast,
    const char *format,
    ImGuiInputTextFlags flags
);
bool ImGui_Button(const char *label);
void ImGui_SameLine(void);
void ImGui_SameLineEx(float offset_from_start_x, float spacing);
void ImGui_AlignTextToFramePadding(void);
bool ImGui_ButtonEx(const char *label, ImVec2 size);
void ImGui_IndentEx(float width);
void ImGui_UnindentEx(float width);
bool ImGui_BeginPopup(const char *id, ImGuiWindowFlags flags);
bool ImGui_BeginPopupContextWindowEx(const char *id, ImGuiPopupFlags flags);
bool ImGui_BeginPopupContextItemEx(const char *id, ImGuiPopupFlags flags);
void ImGui_EndPopup(void);
void ImGui_OpenPopup(const char *id, ImGuiPopupFlags flags);
void ImGui_CloseCurrentPopup(void);
bool ImGui_MenuItem(const char *label);
bool ImGui_MenuItemEx(const char *label, const char *icon, bool selected, bool enabled);
void ImGui_Text(const char *format, ...);
void ImGui_TextDisabled(const char *format, ...);
void ImGui_TextUnformatted(const char *text);
void ImGui_SeparatorText(const char *label);
bool ImGui_Checkbox(const char *label, bool *value);
void ImGui_Separator(void);
bool ImGui_SelectableEx(const char *label, bool selected, ImGuiSelectableFlags flags, ImVec2 size);
bool ImGui_TreeNode(const char *label);
bool ImGui_TreeNodeEx(const char *label, ImGuiTreeNodeFlags flags);
void ImGui_TreePop(void);
void ImGui_SetNextItemOpen(bool open, ImGuiCond condition);
bool ImGui_IsItemClicked(void);
bool ImGui_IsItemActive(void);
void ImGui_BeginDisabled(bool disabled);
void ImGui_EndDisabled(void);
bool ImGui_IsItemHovered(ImGuiHoveredFlags flags);
void ImGui_SetMouseCursor(int cursor);
bool ImGui_IsMouseReleased(int button);
bool ImGui_IsMouseDoubleClicked(int button);
void ImGui_PushIDInt(int int_id);
void ImGui_PopID(void);

#endif
