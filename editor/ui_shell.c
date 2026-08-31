#include "ui_shell.h"

#include <assert.h>
#include <stdio.h>
// pi-lens-ignore: fatal error
#include <imgui/dcimgui.h>

/* The generated internal header exposes one namespace-qualified stb type when
 * this namespace macro is defined. The shell only needs its C docking wrappers. */
#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>

#define UI_DOCKSPACE_ID ((ImGuiID)0x5e7a2d42u)
#define UI_LEFT_RATIO 0.18f
#define UI_RIGHT_RATIO 0.20f
#define UI_BOTTOM_RATIO 0.25f

enum
{
    UI_ASSET_GLYPH_PROTOTYPE = 0xf1b2,
    UI_ASSET_GLYPH_BEHAVIOR = 0xf126,
    UI_ASSET_GLYPH_FOLDER = 0xf07b,
    UI_ASSET_GLYPH_FILE = 0xf15b,
};

static const ImVec4 UI_TEXT = { 0.90f, 0.93f, 0.97f, 1.0f };
static const ImVec4 UI_TEXT_MUTED = { 0.52f, 0.58f, 0.67f, 1.0f };
static const ImVec4 UI_WINDOW = { 0.055f, 0.071f, 0.098f, 1.0f };
static const ImVec4 UI_CHILD = { 0.071f, 0.090f, 0.122f, 1.0f };
static const ImVec4 UI_POPUP = { 0.098f, 0.122f, 0.165f, 1.0f };
static const ImVec4 UI_BORDER = { 0.17f, 0.21f, 0.27f, 1.0f };
static const ImVec4 UI_FRAME = { 0.11f, 0.145f, 0.19f, 1.0f };
static const ImVec4 UI_FRAME_HOVER = { 0.15f, 0.21f, 0.29f, 1.0f };
static const ImVec4 UI_FRAME_ACTIVE = { 0.18f, 0.28f, 0.40f, 1.0f };
static const ImVec4 UI_TITLE = { 0.078f, 0.102f, 0.14f, 1.0f };
static const ImVec4 UI_TITLE_ACTIVE = { 0.105f, 0.17f, 0.24f, 1.0f };
static const ImVec4 UI_BUTTON = { 0.11f, 0.15f, 0.20f, 1.0f };
static const ImVec4 UI_BUTTON_HOVER = { 0.16f, 0.22f, 0.29f, 1.0f };
static const ImVec4 UI_BUTTON_ACTIVE = { 0.20f, 0.31f, 0.43f, 1.0f };
static const ImVec4 UI_HEADER = { 0.13f, 0.18f, 0.24f, 1.0f };
static const ImVec4 UI_HEADER_HOVER = { 0.18f, 0.25f, 0.33f, 1.0f };
static const ImVec4 UI_HEADER_ACTIVE = { 0.22f, 0.32f, 0.43f, 1.0f };
static const ImVec4 UI_TAB = { 0.090f, 0.12f, 0.16f, 1.0f };
static const ImVec4 UI_TAB_SELECTED = { 0.15f, 0.25f, 0.36f, 1.0f };
static const ImVec4 UI_TAB_HOVER = { 0.18f, 0.30f, 0.43f, 1.0f };
static const ImVec4 UI_ACCENT = { 0.45f, 0.67f, 1.0f, 1.0f };
static const ImVec4 UI_DOCK_PREVIEW = { 0.36f, 0.60f, 0.92f, 0.55f };
static const ImVec4 UI_DOCK_EMPTY = { 0.045f, 0.059f, 0.082f, 1.0f };
static const ImVec4 UI_ROW_HOVER = { 0.13f, 0.19f, 0.27f, 1.0f };
static const ImVec4 UI_ROW_SELECTED = { 0.14f, 0.23f, 0.34f, 1.0f };
static const ImVec4 UI_ROW_SECONDARY = { 0.08f, 0.11f, 0.16f, 1.0f };
static const ImVec4 UI_ICON_PURPLE = { 0.72f, 0.60f, 1.0f, 1.0f };
static const ImVec4 UI_ICON_PURPLE_BG = { 0.20f, 0.16f, 0.32f, 1.0f };
static const ImVec4 UI_ICON_BLUE = { 0.44f, 0.67f, 1.0f, 1.0f };
static const ImVec4 UI_ICON_BLUE_BG = { 0.11f, 0.21f, 0.36f, 1.0f };
static const ImVec4 UI_ICON_FOLDER = { 0.95f, 0.74f, 0.39f, 1.0f };
static const struct
{
    ImWchar glyph;
    ImVec4 color;
    ImVec4 background;
} UI_ASSET_STYLE[] = {
    [EDITOR_UI_ASSET_PROTOTYPE] = { UI_ASSET_GLYPH_PROTOTYPE, UI_ICON_PURPLE, UI_ICON_PURPLE_BG },
    [EDITOR_UI_ASSET_BEHAVIOR] = { UI_ASSET_GLYPH_BEHAVIOR, UI_ICON_BLUE, UI_ICON_BLUE_BG },
    [EDITOR_UI_ASSET_FOLDER] = { UI_ASSET_GLYPH_FOLDER, UI_ICON_FOLDER, UI_ICON_PURPLE_BG },
    [EDITOR_UI_ASSET_FILE] = { UI_ASSET_GLYPH_FILE, UI_TEXT_MUTED, UI_FRAME },
    [EDITOR_UI_ASSET_SYSTEM] = { UI_ASSET_GLYPH_FILE, UI_ACCENT, UI_ICON_BLUE_BG },
};
static const float UI_SECTION_HEIGHT = 28.0f;
static const float UI_SECTION_ACTION_SIZE = 22.0f;
static const float UI_SECTION_FONT_OFFSET = 1.0f;
static const float UI_SECTION_LABEL_OFFSET = 1.0f;
static const float UI_ASSET_ROW_HEIGHT = 44.0f;
static const float UI_ASSET_ICON_X = 8.0f;
static const float UI_ASSET_ICON_BACKGROUND_SIZE = 30.0f;
static const float UI_ASSET_ICON_BACKGROUND_ROUNDING = 6.0f;
static const float UI_ASSET_ICON_SIZE = 14.0f;
static const float UI_ASSET_TEXT_X = 45.0f;
static const float UI_ASSET_TEXT_TOP = 7.0f;
static const float UI_ASSET_DETAIL_OFFSET = 19.0f;
static const int UI_PRIMARY_MOUSE_BUTTON = 0;
static const float UI_ASSET_TEXT_RIGHT = 24.0f;
static const float UI_ASSET_CARD_PREVIEW_HEIGHT = 86.0f;
static const float UI_ASSET_CARD_ICON_BACKGROUND_SIZE = 54.0f;
static const float UI_ASSET_CARD_ICON_BACKGROUND_ROUNDING = 8.0f;
static const float UI_ASSET_CARD_ICON_SIZE = 30.0f;
static const float UI_ASSET_CARD_TEXT_INSET = 8.0f;
static const ImWchar UI_SEARCH_GLYPH = 0xf002;

static ImGuiContext *ui_context;
static int ui_reset_layout;
static bool ui_content_drawer_open;
static ImGuiID ui_left_pane_id;
static editor_ui_focus_target ui_focus_request;
static editor_ui_command_handler ui_command_handler;
static void *ui_command_user;

void editor_ui_request_focus(editor_ui_focus_target target)
{
    assert(
        target == EDITOR_UI_FOCUS_NONE || target == EDITOR_UI_FOCUS_SCENE ||
        target == EDITOR_UI_FOCUS_SCENE_RUNTIME || target == EDITOR_UI_FOCUS_NODE_CANVAS ||
        target == EDITOR_UI_FOCUS_CONTENT_DRAWER || target == EDITOR_UI_FOCUS_COMPONENTS ||
        target == EDITOR_UI_FOCUS_MY_BEHAVIOR_ASSET ||
        target == EDITOR_UI_FOCUS_INSPECTOR ||
        target == EDITOR_UI_FOCUS_COMPILER_RESULTS || target == EDITOR_UI_FOCUS_OUTPUT_LOG
    );
    ui_focus_request = target;
}

bool editor_ui_take_focus(editor_ui_focus_target target)
{
    assert(
        target == EDITOR_UI_FOCUS_SCENE || target == EDITOR_UI_FOCUS_SCENE_RUNTIME ||
        target == EDITOR_UI_FOCUS_NODE_CANVAS ||
        target == EDITOR_UI_FOCUS_CONTENT_DRAWER || target == EDITOR_UI_FOCUS_COMPONENTS ||
        target == EDITOR_UI_FOCUS_MY_BEHAVIOR_ASSET ||
        target == EDITOR_UI_FOCUS_INSPECTOR ||
        target == EDITOR_UI_FOCUS_COMPILER_RESULTS || target == EDITOR_UI_FOCUS_OUTPUT_LOG
    );
    if (ui_focus_request != target) {
        return false;
    }
    ui_focus_request = EDITOR_UI_FOCUS_NONE;
    return true;
}

bool editor_ui_content_drawer_open(void)
{
    return ui_content_drawer_open;
}

void editor_ui_set_content_drawer_open(bool open)
{
    if (open && !ui_content_drawer_open) {
        editor_ui_request_focus(EDITOR_UI_FOCUS_CONTENT_DRAWER);
    }
    ui_content_drawer_open = open;
}

void editor_ui_set_command_handler(editor_ui_command_handler handler, void *user)
{
    ui_command_handler = handler;
    ui_command_user = user;
}

static ImU32 ui_color(ImVec4 color)
{
    return ImGui_ColorConvertFloat4ToU32(color);
}

static ImVec2 ui_pixel_pos(ImVec2 pos)
{
    return (ImVec2){ (float)(int)(pos.x + 0.5f), (float)(int)(pos.y + 0.5f) };
}

static void ui_draw_selection(
    ImDrawList *draw,
    ImVec2 min,
    ImVec2 max,
    float rounding,
    editor_ui_asset_selection selection,
    bool hovered
)
{
    bool selected = selection != EDITOR_UI_ASSET_NOT_SELECTED;
    bool active = selection == EDITOR_UI_ASSET_ACTIVE;
    if (selected || hovered) {
        ImVec4 color = active   ? UI_ROW_SELECTED
                     : selected ? UI_ROW_SECONDARY
                                : UI_ROW_HOVER;
        ImDrawList_AddRectFilledEx(draw, min, max, ui_color(color), rounding, ImDrawFlags_RoundCornersAll);
    }
    if (active || selected) {
        ImDrawList_AddRectEx(
            draw,
            min,
            max,
            ui_color(active ? UI_ACCENT : UI_BORDER),
            rounding,
            ImDrawFlags_RoundCornersAll,
            1.0f
        );
    }
}

static void ui_draw_centered_icon(
    ImDrawList *draw,
    ImFont *font,
    float font_size,
    ImVec2 min,
    ImVec2 max,
    ImU32 color,
    ImWchar codepoint
)
{
    ImFontBaked *baked = ImFont_GetFontBaked(font, font_size);
    ImFontGlyph *glyph = ImFontBaked_FindGlyphNoFallback(baked, codepoint);
    assert(glyph);
    float scale = font_size / baked->Size;
    float width = (glyph->X1 - glyph->X0) * scale;
    float height = (glyph->Y1 - glyph->Y0) * scale;
    ImVec2 pos = { min.x + (max.x - min.x - width) * 0.5f - glyph->X0 * scale,
                   min.y + (max.y - min.y - height) * 0.5f - glyph->Y0 * scale };
    ImFont_RenderChar(font, draw, font_size, ui_pixel_pos(pos), color, codepoint);
}

bool editor_ui_icon_button(const char *id, int glyph, bool enabled)
{
    const ImVec2 size = { 26.0f, 24.0f };
    ImVec2 min = ImGui_GetCursorScreenPos();
    bool pressed = ImGui_InvisibleButton(id, size, ImGuiButtonFlags_None);
    bool hovered = enabled && ImGui_IsItemHovered(ImGuiHoveredFlags_None);
    if (hovered) {
        ImGui_SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList *draw = ImGui_GetWindowDrawList();
    ImVec4 background = enabled ? UI_BUTTON : UI_FRAME;
    if (hovered) {
        background = UI_BUTTON_HOVER;
    }
    ImDrawList_AddRectFilledEx(
        draw,
        min,
        (ImVec2){ min.x + size.x, min.y + size.y },
        ui_color(background),
        4.0f,
        ImDrawFlags_RoundCornersAll
    );
    if (hovered) {
        ImDrawList_AddRectEx(
            draw,
            min,
            (ImVec2){ min.x + size.x, min.y + size.y },
            ui_color(UI_BORDER),
            4.0f,
            ImDrawFlags_RoundCornersAll,
            1.0f
        );
    }
    ui_draw_centered_icon(
        draw,
        ImGui_GetFont(),
        ImGui_GetFontSize() - 1.0f,
        min,
        (ImVec2){ min.x + size.x, min.y + size.y },
        ui_color(enabled ? UI_TEXT : UI_TEXT_MUTED),
        (ImWchar)glyph
    );
    return enabled && pressed;
}

bool editor_ui_right_icon_button(const char *id, int glyph)
{
    const float width = 26.0f;
    ImGui_SameLine();
    ImGui_SetCursorPosX(ImGui_GetCursorPosX() + ImGui_GetContentRegionAvail().x - width);
    return editor_ui_icon_button(id, glyph, true);
}

void editor_ui_breadcrumb_separator(int glyph)
{
    const float height = ImGui_GetFrameHeight();
    const ImVec2 min = ImGui_GetCursorScreenPos();
    ImGui_Dummy((ImVec2){ 16.0f, height });
    ui_draw_centered_icon(
        ImGui_GetWindowDrawList(),
        ImGui_GetFont(),
        ImGui_GetFontSize() - 2.0f,
        min,
        (ImVec2){ min.x + 16.0f, min.y + height },
        ui_color(UI_TEXT_MUTED),
        (ImWchar)glyph
    );
}

bool editor_ui_link(const char *id, const char *label)
{
    ImVec2 min = ImGui_GetCursorScreenPos();
    ImVec2 text_size = ImGui_CalcTextSize(label);
    const float height = ImGui_GetFrameHeight();
    bool pressed = ImGui_InvisibleButton(id, (ImVec2){ text_size.x, height }, ImGuiButtonFlags_None);
    bool hovered = ImGui_IsItemHovered(ImGuiHoveredFlags_None);
    if (hovered) {
        ImGui_SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    min.y += (height - text_size.y) * 0.5f;
    min = ui_pixel_pos(min);
    ImDrawList *draw = ImGui_GetWindowDrawList();
    ImVec4 color = UI_TEXT;
    ImDrawList_AddTextImFontPtr(draw, ImGui_GetFont(), ImGui_GetFontSize(), min, ui_color(color), label);
    if (hovered) {
        ImDrawList_AddLine(
            draw,
            (ImVec2){ min.x, min.y + text_size.y + 1.0f },
            (ImVec2){ min.x + text_size.x, min.y + text_size.y + 1.0f },
            ui_color(UI_TEXT)
        );
    }
    return pressed;
}

bool editor_ui_search_field(const char *id, char *buffer, size_t buffer_size, const char *hint)
{
    const ImVec2 min = ImGui_GetCursorScreenPos();
    const float height = ImGui_GetFrameHeight();
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_FramePadding, (ImVec2){ 30.0f, 4.0f });
    char label[64];
    snprintf(label, sizeof label, "##%s", id);
    bool changed = ImGui_InputTextWithHint(label, hint, buffer, buffer_size, 0);
    ImGui_PopStyleVar();
    ui_draw_centered_icon(
        ImGui_GetWindowDrawList(),
        ImGui_GetFont(),
        ImGui_GetFontSize() - 2.0f,
        (ImVec2){ min.x + 6.0f, min.y },
        (ImVec2){ min.x + 24.0f, min.y + height },
        ui_color(UI_TEXT_MUTED),
        UI_SEARCH_GLYPH
    );
    return changed;
}

bool editor_ui_section_header(const char *label, const char *action_id)
{
    const ImGuiStyle *style = ImGui_GetStyle();
    float font_size = ImGui_GetFontSize() + UI_SECTION_FONT_OFFSET;
    ImVec2 min = ImGui_GetCursorScreenPos();
    ImVec2 next = { min.x, min.y + UI_SECTION_HEIGHT + style->ItemSpacing.y };
    float width = ImGui_GetContentRegionAvail().x;
    if (width < UI_SECTION_ACTION_SIZE) {
        width = UI_SECTION_ACTION_SIZE;
    }
    ImGui_Dummy((ImVec2){ width, UI_SECTION_HEIGHT });
    ImGui_SetCursorScreenPos((ImVec2){
        min.x + width - UI_SECTION_ACTION_SIZE,
        min.y + (UI_SECTION_HEIGHT - UI_SECTION_ACTION_SIZE) * 0.5f,
    });
    bool pressed = ImGui_InvisibleButton(
        action_id,
        (ImVec2){ UI_SECTION_ACTION_SIZE, UI_SECTION_ACTION_SIZE },
        ImGuiButtonFlags_None
    );
    bool hovered = ImGui_IsItemHovered(ImGuiHoveredFlags_None);
    if (hovered) {
        ImGui_SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList *draw = ImGui_GetWindowDrawList();
    ImDrawList_AddTextImFontPtr(
        draw,
        ImGui_GetFont(),
        font_size,
        ui_pixel_pos((ImVec2){
            min.x,
            min.y + (UI_SECTION_HEIGHT - font_size) * 0.5f - UI_SECTION_LABEL_OFFSET,
        }),
        ui_color(UI_TEXT),
        label
    );
    ImVec2 action_min = { min.x + width - UI_SECTION_ACTION_SIZE,
                          min.y + (UI_SECTION_HEIGHT - UI_SECTION_ACTION_SIZE) * 0.5f };
    ImVec2 action_max = { min.x + width,
                          min.y + (UI_SECTION_HEIGHT + UI_SECTION_ACTION_SIZE) * 0.5f };
    ImDrawList_AddRectFilledEx(
        draw,
        action_min,
        action_max,
        ui_color(hovered ? UI_BUTTON_HOVER : UI_BUTTON),
        style->FrameRounding,
        ImDrawFlags_RoundCornersAll
    );
    if (hovered) {
        ImDrawList_AddRectEx(
            draw,
            action_min,
            action_max,
            ui_color(UI_BORDER),
            style->FrameRounding,
            ImDrawFlags_RoundCornersAll,
            1.0f
        );
    }
    const char *plus = "+";
    ImVec2 plus_size = ImGui_CalcTextSize(plus);
    ImDrawList_AddTextImFontPtr(
        draw,
        ImGui_GetFont(),
        ImGui_GetFontSize(),
        ui_pixel_pos((ImVec2){
            min.x + width - (UI_SECTION_ACTION_SIZE + plus_size.x) * 0.5f,
            min.y + (UI_SECTION_HEIGHT - plus_size.y) * 0.5f,
        }),
        ui_color(UI_TEXT),
        plus
    );
    ImGui_SetCursorScreenPos(next);
    return pressed;
}

bool editor_ui_asset_row(
    const char *id,
    const char *name,
    const char *detail,
    editor_ui_asset_kind kind,
    editor_ui_asset_selection selection
)
{
    assert((unsigned)kind < sizeof UI_ASSET_STYLE / sizeof UI_ASSET_STYLE[0]);
    const ImGuiStyle *style = ImGui_GetStyle();
    float width = ImGui_GetContentRegionAvail().x;
    if (width < 1.0f) {
        width = 1.0f;
    }
    ImVec2 min = ImGui_GetCursorScreenPos();
    ImVec2 max = { min.x + width, min.y + UI_ASSET_ROW_HEIGHT };
    bool pressed = ImGui_InvisibleButton(id, (ImVec2){ width, UI_ASSET_ROW_HEIGHT }, ImGuiButtonFlags_None);
    bool hovered = ImGui_IsItemHovered(ImGuiHoveredFlags_None);
    if (hovered) {
        ImGui_SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList *draw = ImGui_GetWindowDrawList();
    ui_draw_selection(draw, min, max, style->FrameRounding, selection, hovered);

    ImVec2 icon_min = { min.x + UI_ASSET_ICON_X,
                        min.y + (UI_ASSET_ROW_HEIGHT - UI_ASSET_ICON_BACKGROUND_SIZE) *
                                    0.5f };
    ImVec2 icon_max = { icon_min.x + UI_ASSET_ICON_BACKGROUND_SIZE,
                        icon_min.y + UI_ASSET_ICON_BACKGROUND_SIZE };
    ImDrawList_AddRectFilledEx(
        draw,
        icon_min,
        icon_max,
        ui_color(UI_ASSET_STYLE[kind].background),
        UI_ASSET_ICON_BACKGROUND_ROUNDING,
        ImDrawFlags_RoundCornersAll
    );
    ui_draw_centered_icon(
        draw,
        ImGui_GetFont(),
        UI_ASSET_ICON_SIZE,
        icon_min,
        icon_max,
        ui_color(UI_ASSET_STYLE[kind].color),
        UI_ASSET_STYLE[kind].glyph
    );

    float name_size = ImGui_GetFontSize() + UI_SECTION_FONT_OFFSET;
    bool has_detail = detail && detail[0];
    ImVec2 text_min = ui_pixel_pos((ImVec2){
        min.x + UI_ASSET_TEXT_X,
        min.y + (has_detail ? UI_ASSET_TEXT_TOP : (UI_ASSET_ROW_HEIGHT - name_size) * 0.5f),
    });
    ImVec2 text_max = { max.x - UI_ASSET_TEXT_RIGHT, max.y };
    ImDrawList_PushClipRect(draw, text_min, text_max, true);
    ImDrawList_AddTextImFontPtr(draw, ImGui_GetFont(), name_size, text_min, ui_color(UI_TEXT), name);
    if (has_detail) {
        ImDrawList_AddTextImFontPtr(
            draw,
            ImGui_GetFont(),
            ImGui_GetFontSize() - 3.0f,
            ui_pixel_pos((ImVec2){ text_min.x, text_min.y + UI_ASSET_DETAIL_OFFSET }),
            ui_color(UI_TEXT_MUTED),
            detail
        );
    }
    ImDrawList_PopClipRect(draw);
    return pressed;
}

bool editor_ui_asset_card(const char *id, const char *name, editor_ui_asset_kind kind, editor_ui_asset_selection selection)
{
    assert((unsigned)kind < sizeof UI_ASSET_STYLE / sizeof UI_ASSET_STYLE[0]);
    const ImGuiStyle *style = ImGui_GetStyle();
    const float width = EDITOR_UI_ASSET_CARD_WIDTH;
    const float height = EDITOR_UI_ASSET_CARD_HEIGHT;
    ImVec2 min = ImGui_GetCursorScreenPos();
    ImVec2 max = { min.x + width, min.y + height };
    bool pressed = ImGui_InvisibleButton(id, (ImVec2){ width, height }, ImGuiButtonFlags_None);
    bool hovered = ImGui_IsItemHovered(ImGuiHoveredFlags_None);
    if (hovered) {
        ImGui_SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList *draw = ImGui_GetWindowDrawList();
    ui_draw_selection(draw, min, max, style->FrameRounding, selection, hovered);

    ImVec2 preview_min = { min.x + 6.0f, min.y + 6.0f };
    ImVec2 preview_max = { max.x - 6.0f, preview_min.y + UI_ASSET_CARD_PREVIEW_HEIGHT };
    ImDrawList_AddRectFilledEx(
        draw,
        preview_min,
        preview_max,
        ui_color(UI_FRAME),
        UI_ASSET_CARD_ICON_BACKGROUND_ROUNDING,
        ImDrawFlags_RoundCornersAll
    );
    ImVec2 icon_min = {
        min.x + (width - UI_ASSET_CARD_ICON_BACKGROUND_SIZE) * 0.5f,
        preview_min.y +
            (UI_ASSET_CARD_PREVIEW_HEIGHT - UI_ASSET_CARD_ICON_BACKGROUND_SIZE) * 0.5f,
    };
    ImVec2 icon_max = {
        icon_min.x + UI_ASSET_CARD_ICON_BACKGROUND_SIZE,
        icon_min.y + UI_ASSET_CARD_ICON_BACKGROUND_SIZE,
    };
    ImDrawList_AddRectFilledEx(
        draw,
        icon_min,
        icon_max,
        ui_color(UI_ASSET_STYLE[kind].background),
        UI_ASSET_CARD_ICON_BACKGROUND_ROUNDING,
        ImDrawFlags_RoundCornersAll
    );
    ui_draw_centered_icon(
        draw,
        ImGui_GetFont(),
        UI_ASSET_CARD_ICON_SIZE,
        icon_min,
        icon_max,
        ui_color(UI_ASSET_STYLE[kind].color),
        UI_ASSET_STYLE[kind].glyph
    );

    float name_size = ImGui_GetFontSize() + UI_SECTION_FONT_OFFSET;
    ImVec2 text_min = ui_pixel_pos((ImVec2){
        min.x + UI_ASSET_CARD_TEXT_INSET,
        preview_max.y + 8.0f,
    });
    ImVec2 text_max = { max.x - UI_ASSET_CARD_TEXT_INSET, max.y };
    ImDrawList_PushClipRect(draw, text_min, text_max, true);
    ImDrawList_AddTextImFontPtr(draw, ImGui_GetFont(), name_size, text_min, ui_color(UI_TEXT), name);
    ImDrawList_PopClipRect(draw);
    return pressed;
}

static void ui_apply_theme(void)
{
    ImGui_StyleColorsDark(NULL);
    ImGuiStyle *style = ImGui_GetStyle();
    style->WindowPadding = (ImVec2){ 10.0f, 8.0f };
    style->WindowRounding = 4.0f;
    style->WindowBorderSize = 1.0f;
    style->ChildRounding = 3.0f;
    style->ChildBorderSize = 1.0f;
    style->PopupRounding = 4.0f;
    style->PopupBorderSize = 1.0f;
    style->FramePadding = (ImVec2){ 8.0f, 5.0f };
    style->FrameRounding = 3.0f;
    style->FrameBorderSize = 1.0f;
    style->ItemSpacing = (ImVec2){ 8.0f, 6.0f };
    style->ItemInnerSpacing = (ImVec2){ 6.0f, 4.0f };
    style->IndentSpacing = 18.0f;
    style->ScrollbarSize = 12.0f;
    style->ScrollbarRounding = 4.0f;
    style->GrabMinSize = 10.0f;
    style->GrabRounding = 3.0f;
    style->TabRounding = 3.0f;
    style->TabBorderSize = 1.0f;
    style->DockingSeparatorSize = 2.0f;
    style->Colors[ImGuiCol_Text] = UI_TEXT;
    style->Colors[ImGuiCol_TextDisabled] = UI_TEXT_MUTED;
    style->Colors[ImGuiCol_WindowBg] = UI_WINDOW;
    style->Colors[ImGuiCol_ChildBg] = UI_CHILD;
    style->Colors[ImGuiCol_PopupBg] = UI_POPUP;
    style->Colors[ImGuiCol_Border] = UI_BORDER;
    style->Colors[ImGuiCol_BorderShadow] = (ImVec4){ 0, 0, 0, 0 };
    style->Colors[ImGuiCol_FrameBg] = UI_FRAME;
    style->Colors[ImGuiCol_FrameBgHovered] = UI_FRAME_HOVER;
    style->Colors[ImGuiCol_FrameBgActive] = UI_FRAME_ACTIVE;
    style->Colors[ImGuiCol_TitleBg] = UI_TITLE;
    style->Colors[ImGuiCol_TitleBgActive] = UI_TITLE_ACTIVE;
    style->Colors[ImGuiCol_TitleBgCollapsed] = UI_TITLE;
    style->Colors[ImGuiCol_MenuBarBg] = UI_TITLE;
    style->Colors[ImGuiCol_ScrollbarBg] = UI_WINDOW;
    style->Colors[ImGuiCol_ScrollbarGrab] = UI_BORDER;
    style->Colors[ImGuiCol_ScrollbarGrabHovered] = UI_FRAME_HOVER;
    style->Colors[ImGuiCol_ScrollbarGrabActive] = UI_ACCENT;
    style->Colors[ImGuiCol_CheckMark] = UI_ACCENT;
    style->Colors[ImGuiCol_SliderGrab] = UI_ACCENT;
    style->Colors[ImGuiCol_SliderGrabActive] = UI_ACCENT;
    style->Colors[ImGuiCol_Button] = UI_BUTTON;
    style->Colors[ImGuiCol_ButtonHovered] = UI_BUTTON_HOVER;
    style->Colors[ImGuiCol_ButtonActive] = UI_BUTTON_ACTIVE;
    style->Colors[ImGuiCol_Header] = UI_HEADER;
    style->Colors[ImGuiCol_HeaderHovered] = UI_HEADER_HOVER;
    style->Colors[ImGuiCol_HeaderActive] = UI_HEADER_ACTIVE;
    style->Colors[ImGuiCol_Separator] = UI_BORDER;
    style->Colors[ImGuiCol_SeparatorHovered] = UI_ACCENT;
    style->Colors[ImGuiCol_SeparatorActive] = UI_ACCENT;
    style->Colors[ImGuiCol_Tab] = UI_TAB;
    style->Colors[ImGuiCol_TabHovered] = UI_TAB_HOVER;
    style->Colors[ImGuiCol_TabSelected] = UI_TAB_SELECTED;
    style->Colors[ImGuiCol_TabSelectedOverline] = UI_ACCENT;
    style->Colors[ImGuiCol_TabDimmed] = UI_TAB;
    style->Colors[ImGuiCol_TabDimmedSelected] = UI_TAB_SELECTED;
    style->Colors[ImGuiCol_DockingPreview] = UI_DOCK_PREVIEW;
    style->Colors[ImGuiCol_DockingEmptyBg] = UI_DOCK_EMPTY;
    style->Colors[ImGuiCol_NavCursor] = UI_ACCENT;
}

static bool ui_framebuffer_target;

static float ui_framebuffer_scale_x(void)
{
    float scale = ImGui_GetIO()->DisplayFramebufferScale.x;
    return scale > 0.0f ? scale : 1.0f;
}

static float ui_framebuffer_scale_y(void)
{
    float scale = ImGui_GetIO()->DisplayFramebufferScale.y;
    return scale > 0.0f ? scale : 1.0f;
}

static ImGuiViewport ui_layout_viewport(void)
{
    ImGuiViewport view = *ImGui_GetMainViewport();
    if (view.Size.x <= 0.0f || view.Size.y <= 0.0f) {
        view.Pos = (ImVec2){ 0.0f, 0.0f };
        view.Size = ImGui_GetIO()->DisplaySize;
        view.WorkPos = view.Pos;
        view.WorkSize = view.Size;
    }
    if (ui_framebuffer_target) {
        /* cute_framework reports an offscreen target's pixel size as
         * DisplaySize while retaining the window's framebuffer scale. */
        float scale_x = ui_framebuffer_scale_x();
        float scale_y = ui_framebuffer_scale_y();
        view.Pos.x /= scale_x;
        view.Pos.y /= scale_y;
        view.Size.x /= scale_x;
        view.Size.y /= scale_y;
        view.WorkPos.x /= scale_x;
        view.WorkPos.y /= scale_y;
        view.WorkSize.x /= scale_x;
        view.WorkSize.y /= scale_y;
    }
    return view;
}

static void ui_build_default_layout(void)
{
    ImGuiViewport layout = ui_layout_viewport();
    ImGuiID left;
    ImGuiID rest;
    ImGuiID right;
    ImGuiID center;
    ImGuiID bottom;
    ImGuiID canvas;

    if (ImGui_DockBuilderGetNode(UI_DOCKSPACE_ID)) {
        ImGui_DockBuilderRemoveNode(UI_DOCKSPACE_ID);
    }
    ImGui_DockBuilderAddNodeEx(UI_DOCKSPACE_ID, ImGuiDockNodeFlags_DockSpace);
    ImGui_DockBuilderSetNodePos(UI_DOCKSPACE_ID, layout.WorkPos);
    ImGui_DockBuilderSetNodeSize(UI_DOCKSPACE_ID, layout.WorkSize);
    ImGui_DockBuilderSplitNode(UI_DOCKSPACE_ID, ImGuiDir_Left, UI_LEFT_RATIO, &left, &rest);
    ui_left_pane_id = left;
    ImGui_DockBuilderSplitNode(rest, ImGuiDir_Right, UI_RIGHT_RATIO, &right, &center);
    ImGui_DockBuilderSplitNode(center, ImGuiDir_Down, UI_BOTTOM_RATIO, &bottom, &canvas);
    ImGui_DockBuilderDockWindow("World Outliner", left);
    ImGui_DockBuilderDockWindow("Inspector", right);
    ImGui_DockBuilderDockWindow("Output Log", bottom);
    ImGui_DockBuilderDockWindow("Compiler Results", bottom);
    ImGui_DockBuilderDockWindow("Node Canvas", canvas);
    ImGui_DockBuilderDockWindow("Scene", canvas);
    editor_ui_set_content_drawer_open(false);
    ImGui_DockBuilderFinish(UI_DOCKSPACE_ID);
}

static void ui_draw_menu(void)
{
    if (!ImGui_BeginMainMenuBar()) {
        return;
    }
    ImGui_TextColoredUnformatted(UI_ACCENT, "C Behavior Assets");
    ImGui_SameLine();
    ImGui_TextDisabledUnformatted("Document Workspace");
    ImGui_EndMainMenuBar();
}

void editor_ui_reset_layout(void)
{
    ui_reset_layout = 1;
}

void editor_ui_set_framebuffer_target(bool target)
{
    ui_framebuffer_target = target;
}

void editor_ui_set_framebuffer_size(int width, int height)
{
    assert(width > 0 && height > 0);
    ImGuiContext *context = ImGui_GetCurrentContext();
    if (context) {
        ImVec2 size = { (float)width, (float)height };
        ImGui_GetIO()->DisplaySize = size;
        ImGuiViewport *view = ImGui_GetMainViewport();
        view->Pos = (ImVec2){ 0.0f, 0.0f };
        view->Size = size;
        view->WorkPos = view->Pos;
        view->WorkSize = size;
    }
}

void editor_ui_set_mouse_input(float framebuffer_x, float framebuffer_y, bool primary_down)
{
    ImGuiContext *context = ImGui_GetCurrentContext();
    if (!context) {
        return;
    }
    ImGuiIO *io = ImGui_GetIO();
    float scale_x = ui_framebuffer_scale_x();
    float scale_y = ui_framebuffer_scale_y();
    ImVec2 position = { framebuffer_x / scale_x, framebuffer_y / scale_y };
    ImGuiIO_AddMousePosEvent(io, position.x, position.y);
    ImGuiIO_AddMouseButtonEvent(io, UI_PRIMARY_MOUSE_BUTTON, primary_down);
}

static ImGuiDockNode *ui_left_pane(void)
{
    return ui_left_pane_id ? ImGui_DockBuilderGetNode(ui_left_pane_id) : NULL;
}

float editor_ui_left_pane_width(void)
{
    ImGuiDockNode *pane = ui_left_pane();
    return pane ? pane->Size.x * ui_framebuffer_scale_x() : 0.0f;
}

float editor_ui_left_pane_splitter_x(void)
{
    ImGuiDockNode *pane = ui_left_pane();
    if (!pane) {
        return 0.0f;
    }
    return (pane->Pos.x + pane->Size.x) * ui_framebuffer_scale_x();
}

float editor_ui_left_pane_center_y(void)
{
    ImGuiDockNode *pane = ui_left_pane();
    if (!pane) {
        return 0.0f;
    }
    return (pane->Pos.y + pane->Size.y * 0.5f) * ui_framebuffer_scale_y();
}

void editor_ui_draw_recording_cursor(
    float framebuffer_cursor_x,
    float framebuffer_cursor_y,
    float framebuffer_click_x,
    float framebuffer_click_y,
    bool click_active,
    float click_radius
)
{
    ImGuiContext *context = ImGui_GetCurrentContext();
    if (!context) {
        return;
    }
    float scale_x = ui_framebuffer_scale_x();
    float scale_y = ui_framebuffer_scale_y();
    ImVec2 cursor = { framebuffer_cursor_x / scale_x, framebuffer_cursor_y / scale_y };
    ImVec2 click = { framebuffer_click_x / scale_x, framebuffer_click_y / scale_y };
    float cursor_width = 18.0f / scale_x;
    float cursor_height = 18.0f / scale_y;
    ImDrawList *draw = ImGui_GetForegroundDrawList();
    ImU32 color = IM_COL32(255, 190, 25, 255);
    float ring_scale = scale_x < scale_y ? scale_x : scale_y;
    ImDrawList_AddLineEx(
        draw,
        (ImVec2){ cursor.x - cursor_width * 0.5f, cursor.y },
        (ImVec2){ cursor.x + cursor_width * 0.5f, cursor.y },
        color,
        3.0f / scale_x
    );
    ImDrawList_AddLineEx(
        draw,
        (ImVec2){ cursor.x, cursor.y - cursor_height * 0.5f },
        (ImVec2){ cursor.x, cursor.y + cursor_height * 0.5f },
        color,
        3.0f / scale_y
    );
    if (click_active) {
        ImDrawList_AddCircleEx(draw, click, click_radius / ring_scale, color, 0, 3.0f / ring_scale);
    }
}

void editor_ui_prepare(void)
{
    ImGuiContext *context = ImGui_GetCurrentContext();
    if (!context) {
        return;
    }
    if (context != ui_context) {
        ui_apply_theme();
        ui_context = context;
    }

    ui_draw_menu();
    ImGuiIO *io = ImGui_GetIO();
    ImGuiViewport layout = ui_layout_viewport();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io->ConfigWindowsMoveFromTitleBarOnly = true;
    (void)ImGui_DockSpaceOverViewportEx(UI_DOCKSPACE_ID, &layout, ImGuiDockNodeFlags_None, NULL);
    ImGuiDockNode *root = ImGui_DockBuilderGetNode(UI_DOCKSPACE_ID);
    bool empty = root && !root->ChildNodes[0] && !root->ChildNodes[1] &&
                 root->Windows.Size == 0;
    if ((ui_reset_layout || empty) && layout.WorkSize.x > 0.0f &&
        layout.WorkSize.y > 0.0f) {
        ui_build_default_layout();
        ui_reset_layout = 0;
    }
}
