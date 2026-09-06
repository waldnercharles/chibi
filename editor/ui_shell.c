#include "ui_shell.h"

#include "../core/array.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
// pi-lens-ignore: fatal error
#include <imgui/dcimgui.h>

/* The generated internal header exposes one namespace-qualified stb type when
 * this namespace macro is defined. The shell only needs its C docking wrappers. */
#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>

#define UI_DOCKSPACE_ID ((ImGuiID)0x5e7a2d42u)
#define UI_BOTTOM_RATIO 0.25f
#define UI_GRAPH_HEIGHT_RATIO 0.68f

static const ImVec4 UI_TEXT = { 0.910f, 0.920f, 0.940f, 1.000f };
static const ImVec4 UI_TEXT_MUTED = { 0.590f, 0.610f, 0.650f, 1.000f };
static const ImVec4 UI_WINDOW = { 0.105f, 0.109f, 0.120f, 1.000f };
static const ImVec4 UI_CHILD = { 0.105f, 0.109f, 0.120f, 1.000f };
static const ImVec4 UI_POPUP = { 0.150f, 0.155f, 0.170f, 1.000f };
static const ImVec4 UI_BORDER = { 0.220f, 0.230f, 0.250f, 1.000f };
static const ImVec4 UI_FRAME = { 0.075f, 0.079f, 0.090f, 1.000f };
static const ImVec4 UI_FRAME_HOVER = { 0.190f, 0.200f, 0.220f, 1.000f };
static const ImVec4 UI_FRAME_ACTIVE = { 0.220f, 0.250f, 0.300f, 1.000f };
static const ImVec4 UI_TITLE = { 0.095f, 0.098f, 0.110f, 1.000f };
static const ImVec4 UI_TITLE_ACTIVE = { 0.130f, 0.140f, 0.160f, 1.000f };
static const ImVec4 UI_BUTTON = { 0.170f, 0.180f, 0.200f, 1.000f };
static const ImVec4 UI_BUTTON_HOVER = { 0.240f, 0.250f, 0.280f, 1.000f };
static const ImVec4 UI_BUTTON_ACTIVE = { 0.280f, 0.340f, 0.440f, 1.000f };
static const ImVec4 UI_HEADER = { 0.180f, 0.200f, 0.240f, 1.000f };
static const ImVec4 UI_HEADER_HOVER = { 0.230f, 0.250f, 0.290f, 1.000f };
static const ImVec4 UI_HEADER_ACTIVE = { 0.250f, 0.310f, 0.400f, 1.000f };
static const ImVec4 UI_TAB = { 0.105f, 0.109f, 0.120f, 1.000f };
static const ImVec4 UI_TAB_SELECTED = { 0.200f, 0.210f, 0.240f, 1.000f };
static const ImVec4 UI_TAB_HOVER = { 0.230f, 0.240f, 0.270f, 1.000f };
static const ImVec4 UI_ACCENT = { 0.45f, 0.67f, 1.0f, 1.0f };
static const ImVec4 UI_DOCK_PREVIEW = { 0.36f, 0.60f, 0.92f, 0.55f };
static const ImVec4 UI_DOCK_EMPTY = { 0.080f, 0.085f, 0.095f, 1.000f };
static const float UI_SECTION_HEIGHT = 28.0f;
static const float UI_SECTION_ACTION_SIZE = 22.0f;
static const float UI_SECTION_FONT_OFFSET = 1.0f;
static const float UI_SECTION_LABEL_OFFSET = 1.0f;
static const int UI_PRIMARY_MOUSE_BUTTON = 0;
static const ImWchar UI_SEARCH_GLYPH = 0xf002;

static ImGuiContext *ui_context;
static int ui_reset_layout;
static bool ui_visibility_initialized;
static bool ui_layout_built;
static bool ui_pane_visibility[EDITOR_UI_PANE_COUNT];
static bool ui_focus_mode;
static bool ui_scene_play_expanded;
static float ui_sidebar_width[2] = { 260.0f, 300.0f };
static float ui_sidebar_size[2];
static ImGuiViewport ui_workspace;
static bool ui_sidebar_initialized;
static float ui_menu_bottom;
static void ui_sidebar_begin(int side);
static void ui_sidebar_heading(int side);
static bool ui_focus_visibility[EDITOR_UI_PANE_COUNT];
static bool ui_reduce_motion;
static bool ui_navigator_outline;
static bool ui_pane_rect_valid[EDITOR_UI_PANE_COUNT];
static editor_ui_rect ui_pane_rects[EDITOR_UI_PANE_COUNT];
static ImGuiID ui_scene_central_graph;
static ImGuiID ui_scene_central_scene;
static editor_ui_focus_target ui_focus_request;
static editor_ui_pane ui_document_interaction = EDITOR_UI_PANE_COUNT;
static editor_ui_command_handler ui_command_handler;
static void *ui_command_user;
static bool ui_runtime_playing;
static bool ui_runtime_session;
static bool ui_can_undo;
static bool ui_can_redo;
static char ui_preview_label[64];
static char ui_runtime_label[128];
static char ui_preview_detail[256];
static bool ui_preview_restart;

static float ui_framebuffer_scale_x(void);
static float ui_framebuffer_scale_y(void);

static void ui_set_default_visibility(void)
{
    ui_scene_play_expanded = false;
    ui_focus_mode = false;
    ui_navigator_outline = false;
    ui_sidebar_width[0] = 260.0f;
    ui_sidebar_width[1] = 300.0f;
    ui_sidebar_initialized = false;
    for (int i = 0; i < EDITOR_UI_PANE_COUNT; i++) {
        ui_pane_visibility[i] = true;
    }
    ui_pane_visibility[EDITOR_UI_PANE_COMPILER_RESULTS] = false;
    ui_pane_visibility[EDITOR_UI_PANE_OUTPUT_LOG] = false;
    ui_visibility_initialized = true;
}

static void ui_ensure_visibility(void);

static bool ui_load_focus;
static const char *const UI_PANE_KEYS[EDITOR_UI_PANE_COUNT] = {
    "Content", "Outliner", "Behavior", "Inspector",
    "Graph",   "Scene",    "Compiler", "Log"
};

static void ui_settings_clear(ImGuiContext *, ImGuiSettingsHandler *)
{
    ui_focus_mode = false;
    ui_load_focus = false;
    ui_reduce_motion = false;
    ui_scene_central_graph = 0;
    ui_scene_central_scene = 0;
    ui_set_default_visibility();
}

static void *ui_settings_open(ImGuiContext *, ImGuiSettingsHandler *handler, const char *name)
{
    return strcmp(name, "Workspace") == 0 ? handler : NULL;
}

static void ui_settings_read(ImGuiContext *, ImGuiSettingsHandler *, void *, const char *line)
{
    if (sscanf(line, "SceneCentralGraph=%x", &ui_scene_central_graph) == 1 ||
        sscanf(line, "SceneCentralScene=%x", &ui_scene_central_scene) == 1) {
        return;
    }
    float width;
    if (sscanf(line, "NavigatorWidth=%f", &width) == 1) {
        if (isfinite(width) && width >= 160.0f && width <= 800.0f) {
            ui_sidebar_width[0] = width;
        }
        return;
    }
    if (sscanf(line, "InspectorWidth=%f", &width) == 1) {
        if (isfinite(width) && width >= 160.0f && width <= 800.0f) {
            ui_sidebar_width[1] = width;
        }
        return;
    }
    char key[32];
    int value;
    if (sscanf(line, "%31[^=]=%d", key, &value) != 2 || (value != 0 && value != 1)) {
        return;
    }
    if (strcmp(key, "NavigatorOutline") == 0) {
        ui_navigator_outline = value;
        return;
    }
    if (strcmp(key, "Focus") == 0) {
        ui_load_focus = value;
        return;
    }
    if (strcmp(key, "ReduceMotion") == 0) {
        ui_reduce_motion = value;
        return;
    }
    for (int pane = 0; pane < EDITOR_UI_PANE_COUNT; pane++) {
        if (strcmp(key, UI_PANE_KEYS[pane]) == 0) {
            ui_pane_visibility[pane] = value;
            return;
        }
    }
}

static void ui_settings_apply(ImGuiContext *, ImGuiSettingsHandler *)
{
    editor_ui_set_focus_mode(ui_load_focus);
}

static void ui_settings_write(ImGuiContext *, ImGuiSettingsHandler *, ImGuiTextBuffer *out)
{
    ImGuiTextBuffer_appendf(out, "[Blueprints][Workspace]\nFocus=%d\nReduceMotion=%d\n", ui_focus_mode, ui_reduce_motion);
    ImGuiTextBuffer_appendf(out, "NavigatorOutline=%d\n", ui_navigator_outline);
    ImGuiTextBuffer_appendf(
        out,
        "NavigatorWidth=%.1f\nInspectorWidth=%.1f\n",
        ui_sidebar_width[0],
        ui_sidebar_width[1]
    );
    ImGuiTextBuffer_appendf(out, "SceneCentralGraph=%X\nSceneCentralScene=%X\n", ui_scene_central_graph, ui_scene_central_scene);
    for (int pane = 0; pane < EDITOR_UI_PANE_COUNT; pane++) {
        ImGuiTextBuffer_appendf(
            out,
            "%s=%d\n",
            UI_PANE_KEYS[pane],
            ui_focus_mode ? ui_focus_visibility[pane] : ui_pane_visibility[pane]
        );
    }
    ImGuiTextBuffer_append(out, "\n", NULL);
}

void editor_ui_initialize(bool persist)
{
    if (!persist) {
        ImGui_GetIO()->IniFilename = NULL;
    }
    if (ImGui_FindSettingsHandler("Blueprints")) {
        return;
    }
    ui_ensure_visibility();
    ImGuiSettingsHandler handler = {
        .TypeName = "Blueprints",
        .TypeHash = cImHashStr("Blueprints"),
        .ClearAllFn = ui_settings_clear,
        .ReadInitFn = ui_settings_clear,
        .ReadOpenFn = ui_settings_open,
        .ReadLineFn = ui_settings_read,
        .ApplyAllFn = ui_settings_apply,
        .WriteAllFn = ui_settings_write,
    };
    ImGui_AddSettingsHandler(&handler);
}

static void ui_settings_changed()
{
    if (ImGui_GetCurrentContext()) {
        ImGui_MarkIniSettingsDirty();
    }
}

bool editor_ui_focus_mode()
{
    return ui_focus_mode;
}

bool editor_ui_reduced_motion()
{
    return ui_reduce_motion;
}

void editor_ui_set_reduced_motion(bool enabled)
{
    ui_reduce_motion = enabled;
    ui_settings_changed();
}

static void ui_ensure_visibility(void)
{
    if (!ui_visibility_initialized) {
        ui_set_default_visibility();
    }
}

void editor_ui_set_focus_mode(bool enabled)
{
    ui_ensure_visibility();
    if (enabled == ui_focus_mode) {
        return;
    }
    editor_ui_set_scene_play_expanded(false);
    ui_focus_mode = enabled;
    ui_settings_changed();
    ui_focus_request = EDITOR_UI_FOCUS_NONE;
    for (int pane = 0; pane < EDITOR_UI_PANE_COUNT; pane++) {
        if (enabled) {
            ui_focus_visibility[pane] = ui_pane_visibility[pane];
            if (pane != EDITOR_UI_PANE_NODE_CANVAS && pane != EDITOR_UI_PANE_SCENE) {
                ui_pane_visibility[pane] = false;
            }
            continue;
        }
        ui_pane_visibility[pane] = ui_focus_visibility[pane];
    }
}

static void ui_assert_pane(editor_ui_pane pane)
{
    assert(pane >= EDITOR_UI_PANE_CONTENT && pane < EDITOR_UI_PANE_COUNT);
}

static editor_ui_focus_target ui_pane_focus(editor_ui_pane pane)
{
    switch (pane) {
        case EDITOR_UI_PANE_CONTENT: return EDITOR_UI_FOCUS_CONTENT_DRAWER;
        case EDITOR_UI_PANE_WORLD_OUTLINER:
            return EDITOR_UI_FOCUS_WORLD_OUTLINER;
        case EDITOR_UI_PANE_MY_BEHAVIOR_ASSET:
            return EDITOR_UI_FOCUS_MY_BEHAVIOR_ASSET;
        case EDITOR_UI_PANE_INSPECTOR: return EDITOR_UI_FOCUS_INSPECTOR;
        case EDITOR_UI_PANE_NODE_CANVAS: return EDITOR_UI_FOCUS_NODE_CANVAS;
        case EDITOR_UI_PANE_SCENE: return EDITOR_UI_FOCUS_SCENE;
        case EDITOR_UI_PANE_COMPILER_RESULTS:
            return EDITOR_UI_FOCUS_COMPILER_RESULTS;
        case EDITOR_UI_PANE_OUTPUT_LOG: return EDITOR_UI_FOCUS_OUTPUT_LOG;
        case EDITOR_UI_PANE_COUNT: break;
    }
    return EDITOR_UI_FOCUS_NONE;
}

void editor_ui_request_focus(editor_ui_focus_target target)
{
    assert(
        target == EDITOR_UI_FOCUS_NONE || target == EDITOR_UI_FOCUS_SCENE ||
        target == EDITOR_UI_FOCUS_WORLD_OUTLINER ||
        target == EDITOR_UI_FOCUS_NODE_CANVAS || target == EDITOR_UI_FOCUS_CONTENT_DRAWER ||
        target == EDITOR_UI_FOCUS_MY_BEHAVIOR_ASSET ||
        target == EDITOR_UI_FOCUS_INSPECTOR ||
        target == EDITOR_UI_FOCUS_COMPILER_RESULTS || target == EDITOR_UI_FOCUS_OUTPUT_LOG
    );
    for (int pane = EDITOR_UI_PANE_CONTENT; pane < EDITOR_UI_PANE_COUNT; pane++) {
        if (target == ui_pane_focus((editor_ui_pane)pane) &&
            !editor_ui_pane_visible((editor_ui_pane)pane)) {
            return;
        }
    }
    ui_focus_request = target;
}

bool editor_ui_take_focus(editor_ui_focus_target target)
{
    assert(
        target == EDITOR_UI_FOCUS_SCENE || target == EDITOR_UI_FOCUS_WORLD_OUTLINER ||
        target == EDITOR_UI_FOCUS_NODE_CANVAS || target == EDITOR_UI_FOCUS_CONTENT_DRAWER ||
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
    return editor_ui_pane_visible(EDITOR_UI_PANE_CONTENT);
}

void editor_ui_set_content_drawer_open(bool open)
{
    editor_ui_set_pane_visible(EDITOR_UI_PANE_CONTENT, open);
}

bool editor_ui_pane_visible(editor_ui_pane pane)
{
    ui_assert_pane(pane);
    ui_ensure_visibility();
    return ui_pane_visibility[pane];
}

void editor_ui_set_scene_play_expanded(bool enabled)
{
    ui_ensure_visibility();
    if (ui_scene_play_expanded == enabled) {
        return;
    }
    ui_scene_play_expanded = enabled;
    ui_focus_request = EDITOR_UI_FOCUS_NONE;
    if (enabled) {
        editor_ui_request_focus(EDITOR_UI_FOCUS_SCENE);
    }
}

void editor_ui_set_pane_visible(editor_ui_pane pane, bool visible)
{
    ui_assert_pane(pane);
    ui_ensure_visibility();
    editor_ui_focus_target focus = ui_pane_focus(pane);
    bool was_visible = editor_ui_pane_visible(pane);
    bool was_preferred = ui_focus_mode ? ui_focus_visibility[pane]
                                       : ui_pane_visibility[pane];
    if (!visible && ui_focus_request == focus) {
        ui_focus_request = EDITOR_UI_FOCUS_NONE;
    }
    ui_pane_visibility[pane] = visible;
    if (ui_focus_mode) {
        ui_focus_visibility[pane] = visible;
    }
    if (was_preferred != visible) {
        ui_settings_changed();
    }
    if (visible && !was_visible) {
        editor_ui_request_focus(focus);
    }
}

static void ui_capture_pane_rect(editor_ui_pane pane)
{
    ImVec2 pos = ImGui_GetWindowPos();
    ImVec2 size = ImGui_GetWindowSize();
    ui_pane_rects[pane] = (editor_ui_rect){
        pos.x * ui_framebuffer_scale_x(),
        pos.y * ui_framebuffer_scale_y(),
        size.x * ui_framebuffer_scale_x(),
        size.y * ui_framebuffer_scale_y(),
    };
    ui_pane_rect_valid[pane] = true;
}

bool editor_ui_begin_pane(editor_ui_pane pane, const char *name, int flags)
{
    ui_assert_pane(pane);
    ui_ensure_visibility();
    assert(editor_ui_pane_visible(pane));
    bool open = true;
    bool visible = ImGui_Begin(name, pane == EDITOR_UI_PANE_INSPECTOR ? NULL : &open, flags);
    if (visible && pane == EDITOR_UI_PANE_INSPECTOR) {
        ui_sidebar_heading(1);
    }
    if (!open) {
        editor_ui_set_pane_visible(pane, false);
    }
    if (visible && open) {
        ui_capture_pane_rect(pane);
        if ((pane == EDITOR_UI_PANE_NODE_CANVAS || pane == EDITOR_UI_PANE_SCENE ||
             pane == EDITOR_UI_PANE_WORLD_OUTLINER) &&
            ImGui_IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            (ImGui_IsMouseClicked(0) || ImGui_IsMouseClicked(1) ||
             ImGui_IsMouseClicked(2))) {
            ui_document_interaction = pane;
        }
    }
    return visible && open;
}

static bool ui_navigator_visible()
{
    return editor_ui_pane_visible(EDITOR_UI_PANE_CONTENT) ||
           editor_ui_pane_visible(EDITOR_UI_PANE_WORLD_OUTLINER) ||
           editor_ui_pane_visible(EDITOR_UI_PANE_MY_BEHAVIOR_ASSET);
}

static void ui_set_navigator_visible(bool visible)
{
    editor_ui_set_pane_visible(EDITOR_UI_PANE_CONTENT, visible);
    editor_ui_set_pane_visible(EDITOR_UI_PANE_WORLD_OUTLINER, visible);
    editor_ui_set_pane_visible(EDITOR_UI_PANE_MY_BEHAVIOR_ASSET, visible);
    if (visible) {
        editor_ui_request_focus(
            ui_navigator_outline ? EDITOR_UI_FOCUS_WORLD_OUTLINER : EDITOR_UI_FOCUS_CONTENT_DRAWER
        );
    }
}

bool editor_ui_begin_navigator(editor_ui_pane *outline, bool has_document, editor_ui_pane *selected)
{
    assert(outline && selected);
    assert(*outline == EDITOR_UI_PANE_WORLD_OUTLINER || *outline == EDITOR_UI_PANE_MY_BEHAVIOR_ASSET);
    if (!ui_navigator_visible()) {
        return false;
    }
    bool previous = ui_navigator_outline;
    bool focus = editor_ui_take_focus(EDITOR_UI_FOCUS_CONTENT_DRAWER);
    if (focus) {
        ui_navigator_outline = false;
    }
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_WORLD_OUTLINER)) {
        *outline = EDITOR_UI_PANE_WORLD_OUTLINER;
        ui_navigator_outline = true;
        focus = true;
    }
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_MY_BEHAVIOR_ASSET)) {
        *outline = EDITOR_UI_PANE_MY_BEHAVIOR_ASSET;
        ui_navigator_outline = true;
        focus = true;
    }
    bool files = editor_ui_pane_visible(EDITOR_UI_PANE_CONTENT);
    bool scene = editor_ui_pane_visible(EDITOR_UI_PANE_WORLD_OUTLINER);
    bool document = has_document &&
                    editor_ui_pane_visible(EDITOR_UI_PANE_MY_BEHAVIOR_ASSET);
    if (!files && !scene && !document) {
        return false;
    }
    if ((*outline == EDITOR_UI_PANE_WORLD_OUTLINER && !scene) ||
        (*outline == EDITOR_UI_PANE_MY_BEHAVIOR_ASSET && !document)) {
        *outline = scene ? EDITOR_UI_PANE_WORLD_OUTLINER
                         : EDITOR_UI_PANE_MY_BEHAVIOR_ASSET;
    }
    if (!scene && !document) {
        ui_navigator_outline = false;
    }
    if (!files) {
        ui_navigator_outline = true;
    }
    if (focus) {
        ImGui_SetNextWindowFocus();
    }
    ui_sidebar_begin(0);
    bool open = true;
    bool visible = ImGui_Begin(
        "Navigator",
        NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking
    );
    if (!open) {
        ui_set_navigator_visible(false);
    }
    if (!visible || !open) {
        ImGui_End();
        return false;
    }
    ui_sidebar_heading(0);
    editor_ui_pane chosen = ui_navigator_outline ? *outline : EDITOR_UI_PANE_CONTENT;
    const struct
    {
        const char *label;
        const char *id;
        editor_ui_pane pane;
        bool visible;
    } tabs[] = {
        { "Files", "navigator.files", EDITOR_UI_PANE_CONTENT, files },
        { "Scene", "navigator.scene", EDITOR_UI_PANE_WORLD_OUTLINER, scene },
        { "Document", "navigator.document", EDITOR_UI_PANE_MY_BEHAVIOR_ASSET, document },
    };
    if (ImGui_BeginTabBar("navigator_tabs", 0)) {
        for (int i = 0; i < 3; i++) {
            if (!tabs[i].visible) {
                continue;
            }
            int flags = chosen == tabs[i].pane ? ImGuiTabItemFlags_SetSelected : 0;
            bool active = ImGui_BeginTabItem(tabs[i].label, NULL, flags);
            editor_ui_track_item(tabs[i].id);
            if (ImGui_IsItemClicked()) {
                ui_navigator_outline = tabs[i].pane != EDITOR_UI_PANE_CONTENT;
                if (ui_navigator_outline) {
                    *outline = tabs[i].pane;
                }
            }
            if (active) {
                ImGui_EndTabItem();
            }
        }
        ImGui_EndTabBar();
    }
    if (previous != ui_navigator_outline) {
        ui_settings_changed();
    }
    *selected = ui_navigator_outline ? *outline : EDITOR_UI_PANE_CONTENT;
    ui_capture_pane_rect(*selected);
    return true;
}

void editor_ui_end_navigator()
{
    ImGui_End();
}

editor_ui_pane editor_ui_take_document_interaction()
{
    editor_ui_pane pane = ui_document_interaction;
    ui_document_interaction = EDITOR_UI_PANE_COUNT;
    return pane;
}

void editor_ui_cancel_document_interaction(editor_ui_pane pane)
{
    ui_assert_pane(pane);
    if (ui_document_interaction == pane) {
        ui_document_interaction = EDITOR_UI_PANE_COUNT;
    }
}

bool editor_ui_pane_rect(editor_ui_pane pane, editor_ui_rect *out)
{
    ui_assert_pane(pane);
    assert(out);
    if (!ui_pane_rect_valid[pane]) {
        return false;
    }
    *out = ui_pane_rects[pane];
    return true;
}

void editor_ui_show_graph_and_scene(void)
{
    ui_ensure_visibility();
    editor_ui_set_pane_visible(EDITOR_UI_PANE_NODE_CANVAS, true);
    editor_ui_set_pane_visible(EDITOR_UI_PANE_SCENE, true);
    editor_ui_request_focus(EDITOR_UI_FOCUS_NODE_CANVAS);
    ui_reset_layout = 1;
}

int editor_ui_run_command(editor_ui_command command)
{
    return ui_command_handler ? ui_command_handler(command, ui_command_user) : -1;
}

void editor_ui_set_runtime_state(bool playing, bool session)
{
    ui_runtime_playing = playing;
    ui_runtime_session = session;
}

bool editor_ui_runtime_playing()
{
    return ui_runtime_playing;
}

bool editor_ui_runtime_session()
{
    return ui_runtime_session;
}

void editor_ui_set_command_handler(editor_ui_command_handler handler, void *user)
{
    ui_command_handler = handler;
    ui_command_user = user;
    ui_runtime_playing = false;
    ui_runtime_session = false;
}

static ImU32 ui_color(ImVec4 color)
{
    return ImGui_ColorConvertFloat4ToU32(color);
}

static ImVec2 ui_pixel_pos(ImVec2 pos)
{
    return (ImVec2){ (float)(int)(pos.x + 0.5f), (float)(int)(pos.y + 0.5f) };
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

static bool ui_icon_button(const char *id, int glyph, bool enabled, ImVec2 size, float font_size)
{
    ImVec2 min = ImGui_GetCursorScreenPos();
    bool pressed = ImGui_InvisibleButton(id, size, ImGuiButtonFlags_None);
    bool hovered = enabled && ImGui_IsItemHovered(ImGuiHoveredFlags_None);
    if (hovered) {
        ImGui_SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImDrawList *draw = ImGui_GetWindowDrawList();
    ImVec4 background = enabled ? UI_BUTTON : UI_FRAME;
    ImGui_PushID(id);
    ImGuiID animation_id = ImGui_GetID("hover.animation");
    ImGui_PopID();
    ImGuiStorage *storage = ImGui_GetStateStorage();
    float target = hovered ? 1.0f : 0.0f;
    float hover = ImGuiStorage_GetFloat(storage, animation_id, 0.0f);
    float blend = ui_reduce_motion
                    ? 1.0f
                    : 1.0f - expf(-ImGui_GetIO()->DeltaTime / 0.075f);
    hover += (target - hover) * blend;
    ImGuiStorage_SetFloat(storage, animation_id, hover);
    background.x += (UI_BUTTON_HOVER.x - background.x) * hover;
    background.y += (UI_BUTTON_HOVER.y - background.y) * hover;
    background.z += (UI_BUTTON_HOVER.z - background.z) * hover;
    if (enabled && ImGui_IsItemActive()) {
        background = UI_BUTTON_ACTIVE;
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
        font_size,
        min,
        (ImVec2){ min.x + size.x, min.y + size.y },
        ui_color(enabled ? UI_TEXT : UI_TEXT_MUTED),
        (ImWchar)glyph
    );
    return enabled && pressed;
}

bool editor_ui_icon_button(const char *id, int glyph, bool enabled)
{
    return ui_icon_button(id, glyph, enabled, (ImVec2){ 26.0f, 24.0f }, ImGui_GetFontSize() - 1.0f);
}

bool editor_ui_icon_button_sized(const char *id, int glyph, bool enabled, float size)
{
    assert(isfinite(size) && size > 0);
    return ui_icon_button(id, glyph, enabled, (ImVec2){ size, size }, fminf(16.0f, size * 0.5f));
}

bool editor_ui_right_icon_button(const char *id, int glyph)
{
    const float width = 26.0f;
    ImGui_SameLine();
    ImGui_SetCursorPosX(ImGui_GetCursorPosX() + ImGui_GetContentRegionAvail().x - width);
    return editor_ui_icon_button(id, glyph, true);
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

bool editor_ui_disclosure(const char *label, const char *id)
{
    ImGuiStorage *storage = ImGui_GetStateStorage();
    ImGuiID open_id = ImGui_GetID(id);
    bool open = ImGuiStorage_GetBool(storage, open_id, false);
    ImVec2 min = ImGui_GetCursorScreenPos();
    ImVec2 size = { fmaxf(1.0f, ImGui_GetContentRegionAvail().x), 32.0f };
    if (ImGui_InvisibleButton(id, size, ImGuiButtonFlags_EnableNav)) {
        open = !open;
    }
    bool focused = ImGui_IsItemFocused();
    if (focused && ImGui_IsKeyPressed(ImGuiKey_LeftArrow)) {
        open = false;
    }
    if (focused && ImGui_IsKeyPressed(ImGuiKey_RightArrow)) {
        open = true;
    }
    ImGuiStorage_SetBool(storage, open_id, open);
    ImGui_PushID(id);
    ImGuiID animation_id = ImGui_GetID("disclosure.angle");
    ImGui_PopID();
    float target = open ? 1.0f : 0.0f;
    float progress = ImGuiStorage_GetFloat(storage, animation_id, target);
    float blend = ui_reduce_motion ? 1.0f
                                   : 1.0f - expf(-ImGui_GetIO()->DeltaTime / 0.04f);
    progress += (target - progress) * blend;
    ImGuiStorage_SetFloat(storage, animation_id, progress);
    ImDrawList *draw = ImGui_GetWindowDrawList();
    ImVec2 max = { min.x + size.x, min.y + size.y };
    if (ImGui_IsItemHovered(0) || focused) {
        ImDrawList_AddRectFilledEx(
            draw,
            min,
            max,
            ui_color(ImGui_IsItemActive() ? UI_BUTTON_ACTIVE : UI_BUTTON_HOVER),
            5.0f,
            ImDrawFlags_RoundCornersAll
        );
    }
    if (focused) {
        ImDrawList_AddRectEx(draw, min, max, ui_color(UI_BORDER), 5.0f, ImDrawFlags_RoundCornersAll, 1.0f);
    }
    float angle = progress * 1.57079632679f;
    float c = cosf(angle), s = sinf(angle);
    const ImVec2 points[] = { { -2, -4 }, { 2, 0 }, { -2, 4 } };
    ImVec2 chevron[3];
    for (int i = 0; i < 3; i++) {
        chevron[i] = (ImVec2){ min.x + 12 + points[i].x * c - points[i].y * s,
                               min.y + 16 + points[i].x * s + points[i].y * c };
    }
    ImDrawList_AddLineEx(draw, chevron[0], chevron[1], ui_color(UI_TEXT_MUTED), 1.5f);
    ImDrawList_AddLineEx(draw, chevron[1], chevron[2], ui_color(UI_TEXT_MUTED), 1.5f);
    ImVec4 clip = { min.x + 26, min.y, fmaxf(min.x + 26, max.x - 6), max.y };
    ImDrawList_AddTextImFontPtrEx(
        draw,
        ImGui_GetFont(),
        ImGui_GetFontSize(),
        (ImVec2){ min.x + 26, min.y + (size.y - ImGui_GetFontSize()) * 0.5f },
        ui_color(UI_TEXT),
        label,
        NULL,
        0,
        &clip
    );
    if (open) {
        ImGui_TreePush(id);
    }
    return open;
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

typedef struct
{
    ImGuiID id;
    editor_ui_rect rect;
} ui_item_hit;

static ui_item_hit *ui_item_hits;

bool editor_ui_item_center(const char *id, float *x, float *y)
{
    editor_ui_rect rect;
    if (!editor_ui_item_rect(id, &rect)) {
        return false;
    }
    *x = rect.x + rect.width * 0.5f;
    *y = rect.y + rect.height * 0.5f;
    return true;
}

bool editor_ui_item_rect(const char *id, editor_ui_rect *out)
{
    ImGuiID hash = cImHashStr(id);
    for (int i = 0; i < acount(ui_item_hits); i++) {
        if (ui_item_hits[i].id == hash) {
            *out = ui_item_hits[i].rect;
            return true;
        }
    }
    return false;
}

void editor_ui_track_item(const char *id)
{
    if (!ImGui_IsItemVisible()) {
        return;
    }
    ImVec2 min = ImGui_GetItemRectMin();
    ImVec2 max = ImGui_GetItemRectMax();
    apush(
        ui_item_hits,
        ((ui_item_hit){ cImHashStr(id), { min.x, min.y, max.x - min.x, max.y - min.y } })
    );
}

static void ui_apply_theme(void)
{
    ImGui_StyleColorsDark(NULL);
    ImGuiStyle *style = ImGui_GetStyle();
    style->WindowPadding = (ImVec2){ 10.0f, 8.0f };
    style->WindowRounding = 8.0f;
    style->WindowBorderSize = 1.0f;
    style->ChildRounding = 6.0f;
    style->ChildBorderSize = 1.0f;
    style->PopupRounding = 9.0f;
    style->PopupBorderSize = 1.0f;
    style->FramePadding = (ImVec2){ 8.0f, 5.0f };
    style->FrameRounding = 5.0f;
    style->FrameBorderSize = 0.0f;
    style->ItemSpacing = (ImVec2){ 8.0f, 6.0f };
    style->ItemInnerSpacing = (ImVec2){ 6.0f, 4.0f };
    style->IndentSpacing = 18.0f;
    style->ScrollbarSize = 12.0f;
    style->ScrollbarRounding = 4.0f;
    style->GrabMinSize = 10.0f;
    style->GrabRounding = 3.0f;
    style->TabRounding = 5.0f;
    style->TabBorderSize = 0.0f;
    style->DockingSeparatorSize = 5.0f;
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
    style->Colors[ImGuiCol_Separator] = (ImVec4){ 0.14f, 0.15f, 0.17f, 1.0f };
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
    ui_scene_central_graph = 0;
    ui_scene_central_scene = 0;
    ImGuiViewport layout = ui_workspace;
    layout.WorkPos.x += ui_sidebar_size[0];
    layout.WorkSize.x -= ui_sidebar_size[0] + ui_sidebar_size[1];
    ImGuiID center;
    ImGuiID inspector;
    ImGuiID document;
    ImGuiID bottom;
    ImGuiID graph;
    ImGuiID scene;

    if (ImGui_DockBuilderGetNode(UI_DOCKSPACE_ID)) {
        ImGui_DockBuilderRemoveNode(UI_DOCKSPACE_ID);
    }
    ImGui_DockBuilderAddNodeEx(UI_DOCKSPACE_ID, ImGuiDockNodeFlags_DockSpace);
    ImGui_DockBuilderSetNodePos(UI_DOCKSPACE_ID, layout.WorkPos);
    ImGui_DockBuilderSetNodeSize(UI_DOCKSPACE_ID, layout.WorkSize);
    ImGui_DockBuilderSplitNode(UI_DOCKSPACE_ID, ImGuiDir_Right, 0.23f, &inspector, &center);
    ImGui_DockBuilderDockWindow("Inspector", inspector);
    ImGui_DockBuilderSplitNode(center, ImGuiDir_Down, UI_BOTTOM_RATIO, &bottom, &document);
    /* The central remainder receives space released by hidden diagnostics.
     * Keep that remainder in Graph and size Scene for the visible workspace,
     * rather than for the temporary document above the hidden bottom node. */
    ImGui_DockBuilderSplitNode(document, ImGuiDir_Down, 1.0f - UI_GRAPH_HEIGHT_RATIO, &scene, &graph);
    if (!ui_pane_visibility[EDITOR_UI_PANE_OUTPUT_LOG] &&
        !ui_pane_visibility[EDITOR_UI_PANE_COMPILER_RESULTS]) {
        ImVec2 scene_size = ImGui_DockBuilderGetNode(scene)->Size;
        scene_size.y = ImGui_DockBuilderGetNode(center)->Size.y *
                       (1.0f - UI_GRAPH_HEIGHT_RATIO);
        ImGui_DockBuilderSetNodeSize(scene, scene_size);
    }
    ImGui_DockBuilderDockWindow("Output Log", bottom);
    ImGui_DockBuilderDockWindow("Compiler Results", bottom);
    ImGui_DockBuilderDockWindow("Node Canvas", graph);
    ImGui_DockBuilderDockWindow("Scene", scene);
    ImGui_DockBuilderFinish(UI_DOCKSPACE_ID);
}

static void ui_pane_menu_item(const char *label, const char *id, editor_ui_pane pane)
{
    bool visible = editor_ui_pane_visible(pane);
    if (ImGui_MenuItemEx(label, NULL, visible, true)) {
        editor_ui_set_pane_visible(pane, !visible);
    }
    editor_ui_track_item(id);
}

static void ui_document_action(const char *label, const char *id, editor_ui_command command)
{
    bool enabled = ui_command_handler != NULL;
    if (command == EDITOR_UI_COMMAND_UNDO) {
        enabled &= ui_can_undo;
    }
    if (command == EDITOR_UI_COMMAND_REDO) {
        enabled &= ui_can_redo;
    }
    if (ImGui_MenuItemEx(label, NULL, false, enabled)) {
        editor_ui_run_command(command);
    }
    editor_ui_track_item(id);
}

void editor_ui_set_history_available(bool undo, bool redo)
{
    ui_can_undo = undo;
    ui_can_redo = redo;
}

void editor_ui_set_preview_status(const char *label, const char *detail, bool restart)
{
    snprintf(ui_preview_label, sizeof ui_preview_label, "%s", label);
    snprintf(ui_preview_detail, sizeof ui_preview_detail, "%s", detail);
    ui_preview_restart = restart;
}

void editor_ui_set_runtime_status(const char *label)
{
    snprintf(ui_runtime_label, sizeof ui_runtime_label, "%s", label);
}

static void ui_draw_menu(void)
{
    if (!ImGui_BeginMainMenuBar()) {
        return;
    }
    ui_menu_bottom = ImGui_GetWindowPos().y + ImGui_GetWindowSize().y;
    ImGui_TextColoredUnformatted(UI_ACCENT, "Blueprints");
    bool open = ImGui_BeginMenu("Document");
    editor_ui_track_item("document.menu");
    if (open) {
        ui_document_action("Save", "document.save", EDITOR_UI_COMMAND_SAVE);
        ui_document_action("Save As", "document.save_as", EDITOR_UI_COMMAND_SAVE_AS);
        ui_document_action("Save All", "document.save_all", EDITOR_UI_COMMAND_SAVE_ALL);
        ui_document_action("Reload", "document.reload", EDITOR_UI_COMMAND_RELOAD);
        ImGui_Separator();
        ui_document_action("Undo", "document.undo", EDITOR_UI_COMMAND_UNDO);
        ui_document_action("Redo", "document.redo", EDITOR_UI_COMMAND_REDO);
        ImGui_EndMenu();
    }
    bool view = ImGui_BeginMenu("View");
    editor_ui_track_item("view.menu");
    if (view) {
        if (ImGui_MenuItemEx("Focus workspace", NULL, ui_focus_mode, true)) {
            editor_ui_set_focus_mode(!ui_focus_mode);
        }
        editor_ui_track_item("view.focus");
        if (ImGui_MenuItemEx("Reduce motion", NULL, ui_reduce_motion, true)) {
            editor_ui_set_reduced_motion(!ui_reduce_motion);
        }
        ImGui_Separator();
        if (ImGui_MenuItemEx("Reset Workspace", NULL, false, true)) {
            editor_ui_show_graph_and_scene();
        }
        editor_ui_track_item("view.graph_and_scene");
        ImGui_Separator();
        if (ImGui_MenuItemEx("Navigator", NULL, ui_navigator_visible(), true)) {
            ui_set_navigator_visible(!ui_navigator_visible());
        }
        editor_ui_track_item("view.navigator");
        ui_pane_menu_item("Inspector", "view.inspector", EDITOR_UI_PANE_INSPECTOR);
        ui_pane_menu_item("Node Canvas", "view.node_canvas", EDITOR_UI_PANE_NODE_CANVAS);
        ui_pane_menu_item("Scene", "view.scene", EDITOR_UI_PANE_SCENE);
        ImGui_Separator();
        ui_pane_menu_item("Compiler Results", "view.compiler_results", EDITOR_UI_PANE_COMPILER_RESULTS);
        ui_pane_menu_item("Output Log", "view.output_log", EDITOR_UI_PANE_OUTPUT_LOG);
        ImGui_Separator();
        if (ImGui_MenuItemEx("Reset Layout", NULL, false, true)) {
            editor_ui_reset_layout();
        }
        editor_ui_track_item("view.reset_layout");
        ImGui_EndMenu();
    }
    if (ui_runtime_label[0]) {
        ImGui_TextDisabled("%s", ui_runtime_label);
        editor_ui_track_item("runtime.status");
    }
    if (ui_preview_label[0] && ImGui_BeginMenu(ui_preview_label)) {
        ImGui_TextWrapped("%s", ui_preview_detail);
        if (ui_preview_restart) {
            ImGui_Separator();
            ui_document_action("Restart behavior instances", "preview.restart", EDITOR_UI_COMMAND_RESTART_PREVIEW);
        }
        ImGui_EndMenu();
    }
    ImGui_EndMainMenuBar();
}

void editor_ui_reset_layout(void)
{
    editor_ui_set_focus_mode(false);
    ui_set_default_visibility();
    ui_settings_changed();
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

float editor_ui_left_pane_width(void)
{
    return ui_sidebar_size[0] * ui_framebuffer_scale_x();
}

float editor_ui_left_pane_splitter_x(void)
{
    return (ui_workspace.WorkPos.x + ui_sidebar_size[0]) * ui_framebuffer_scale_x();
}

float editor_ui_left_pane_center_y(void)
{
    return (ui_workspace.WorkPos.y + ui_workspace.WorkSize.y * 0.5f) *
           ui_framebuffer_scale_y();
}

static ImGuiDockNode *ui_window_dock(const char *name)
{
    ImGuiWindow *window = ImGui_FindWindowByName(name);
    if (window && window->DockNode) {
        return window->DockNode;
    }
    ImGuiWindowSettings *settings = ImGui_FindWindowSettingsByID(cImHashStr(name));
    return settings && settings->DockId ? ImGui_DockBuilderGetNode(settings->DockId)
                                        : NULL;
}

static void ui_update_scene_central_node()
{
    bool scene_only = !editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS) &&
                      editor_ui_pane_visible(EDITOR_UI_PANE_SCENE);
    if (ui_scene_central_graph) {
        ImGuiDockNode *graph = ImGui_DockBuilderGetNode(ui_scene_central_graph);
        ImGuiDockNode *scene = ImGui_DockBuilderGetNode(ui_scene_central_scene);
        bool same_split = graph && scene && graph->ParentNode &&
                          graph->ParentNode == scene->ParentNode;
        if (scene_only && same_split && ImGuiDockNode_IsCentralNode(scene)) {
            return;
        }
        if (graph && scene && ImGuiDockNode_IsCentralNode(scene)) {
            ImGuiDockNode_SetLocalFlags(scene, scene->LocalFlags & ~ImGuiDockNodeFlags_CentralNode);
            ImGuiDockNode_SetLocalFlags(graph, graph->LocalFlags | ImGuiDockNodeFlags_CentralNode);
        }
        ui_scene_central_graph = 0;
        ui_scene_central_scene = 0;
        ui_settings_changed();
    }
    if (!scene_only) {
        return;
    }
    ImGuiDockNode *graph = ui_window_dock("Node Canvas");
    ImGuiDockNode *scene = ui_window_dock("Scene");
    if (!graph || !scene || graph == scene || !graph->ParentNode ||
        graph->ParentNode != scene->ParentNode ||
        !ImGuiDockNode_IsCentralNode(graph)) {
        return;
    }
    /* Empty central nodes stay visible in ImGui. Move that role temporarily,
     * preserving both dock IDs and SizeRef so the authored split restores. */
    ui_scene_central_graph = graph->ID;
    ui_scene_central_scene = scene->ID;
    ImGuiDockNode_SetLocalFlags(graph, graph->LocalFlags & ~ImGuiDockNodeFlags_CentralNode);
    ImGuiDockNode_SetLocalFlags(scene, scene->LocalFlags | ImGuiDockNodeFlags_CentralNode);
    ui_settings_changed();
}

static bool ui_sidebar_open(int side)
{
    return side ? editor_ui_pane_visible(EDITOR_UI_PANE_INSPECTOR)
                : ui_navigator_visible();
}

static void ui_sidebar_toggle(int side)
{
    if (side) {
        editor_ui_set_pane_visible(EDITOR_UI_PANE_INSPECTOR, !ui_sidebar_open(side));
        return;
    }
    ui_set_navigator_visible(!ui_sidebar_open(side));
}

static void ui_sidebar_begin(int side)
{
    float x = ui_workspace.WorkPos.x;
    if (side) {
        x += ui_workspace.WorkSize.x - ui_sidebar_size[side];
    }
    ImGui_SetNextWindowSizeConstraints((ImVec2){ 24.0f, 24.0f }, (ImVec2){ 10000.0f, 10000.0f }, NULL, NULL);
    ImGui_SetNextWindowDockID(0, ImGuiCond_Always);
    ImGui_SetNextWindowPos((ImVec2){ x, ui_workspace.WorkPos.y }, ImGuiCond_Always);
    ImGui_SetNextWindowSize(
        (ImVec2){ ui_sidebar_size[side], ui_workspace.WorkSize.y },
        ImGuiCond_Always
    );
}

static void ui_sidebar_heading(int side)
{
    if (editor_ui_icon_button("collapse", side ? 0xf105 : 0xf104, true)) {
        ui_sidebar_toggle(side);
    }
    editor_ui_track_item(side ? "inspector.collapse" : "navigator.collapse");
    if (ImGui_IsItemHovered(0)) {
        ImGui_SetTooltip("Collapse %s", side ? "Inspector" : "Navigator");
    }
    if (!side) {
        ImGui_SameLine();
        ImGui_TextUnformatted("Navigator");
    }
    ImGui_Separator();
}

static void ui_prepare_sidebars()
{
    for (int side = 0; side < 2; side++) {
        bool open = ui_sidebar_open(side);
        if (side) {
            /* Inspector owns its normal floating/docked geometry. The separate
             * reopen rail must never overwrite its saved window or DockId. */
            ui_sidebar_size[side] = open ? 0.0f : 32.0f;
            if (open) {
                continue;
            }
        }
        float limit = fmaxf(32.0f, ui_workspace.WorkSize.x * 0.3f);
        float target = open ? fminf(ui_sidebar_width[side], limit) : 32.0f;
        if (side || !ui_sidebar_initialized || ui_reduce_motion) {
            ui_sidebar_size[side] = target;
        } else {
            /* Retarget the presentation value: repeated clicks never restart the motion. */
            float blend = 1.0f - expf(-30.0f * ImGui_GetIO()->DeltaTime);
            ui_sidebar_size[side] += (target - ui_sidebar_size[side]) * blend;
            if (fabsf(target - ui_sidebar_size[side]) < 0.25f) {
                ui_sidebar_size[side] = target;
            }
        }
        if (open) {
            float x = ui_workspace.WorkPos.x + ui_sidebar_size[0] - 2.0f;
            ImGui_SetNextWindowPos((ImVec2){ x, ui_workspace.WorkPos.y }, ImGuiCond_Always);
            ImGui_SetNextWindowSize((ImVec2){ 4.0f, ui_workspace.WorkSize.y }, ImGuiCond_Always);
            ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){ 0, 0 });
            ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowMinSize, (ImVec2){ 1, 1 });
            if (ImGui_Begin(
                    "##navigator_resize",
                    NULL,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking |
                        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings
                )) {
                ImGui_BringWindowToDisplayFront(ImGui_GetCurrentWindow());
                ImGui_InvisibleButton(
                    "resize",
                    (ImVec2){ 4.0f, ui_workspace.WorkSize.y },
                    0
                );
                if (ImGui_IsItemHovered(0) || ImGui_IsItemActive()) {
                    ImGui_SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                }
                if (ImGui_IsItemActive()) {
                    ui_sidebar_width[side] = fminf(
                        limit,
                        fmaxf(
                            160.0f,
                            ui_sidebar_size[side] + ImGui_GetIO()->MouseDelta.x
                        )
                    );
                    ui_sidebar_size[side] = ui_sidebar_width[side];
                    ui_settings_changed();
                }
            }
            ImGui_End();
            ImGui_PopStyleVarEx(2);
            continue;
        }
        ui_sidebar_begin(side);
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){ 4.0f, 8.0f });
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowMinSize, (ImVec2){ 24.0f, 24.0f });
        if (ImGui_Begin(
                side ? "Inspector Rail" : "Navigator",
                NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking |
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
            )) {
            if (editor_ui_icon_button_sized("expand", side ? 0xf104 : 0xf105, true, 24.0f)) {
                ui_sidebar_toggle(side);
            }
            editor_ui_track_item(side ? "inspector.expand" : "navigator.expand");
            if (ImGui_IsItemHovered(0)) {
                ImGui_SetTooltip("Open %s", side ? "Inspector" : "Navigator");
            }
        }
        ImGui_End();
        ImGui_PopStyleVarEx(2);
    }
    ui_sidebar_initialized = true;
}

void editor_ui_prepare(void)
{
    afree(ui_item_hits);
    for (int i = 0; i < EDITOR_UI_PANE_COUNT; i++) {
        ui_pane_rect_valid[i] = false;
    }
    ImGuiContext *context = ImGui_GetCurrentContext();
    if (!context) {
        return;
    }
    if (context != ui_context) {
        ui_apply_theme();
        ui_context = context;
        ui_ensure_visibility();
        ui_layout_built = false;
        ui_sidebar_initialized = false;
    }

    ui_draw_menu();
    ImGuiIO *io = ImGui_GetIO();
    ui_workspace = ui_layout_viewport();
    /* Framebuffer targets reset WorkPos before BeginMainMenuBar reserves space
     * for the next frame. Keep this frame's anchored panes below the menu. */
    if (ui_workspace.WorkPos.y < ui_menu_bottom) {
        float inset = ui_menu_bottom - ui_workspace.WorkPos.y;
        ui_workspace.WorkPos.y += inset;
        ui_workspace.WorkSize.y = fmaxf(1.0f, ui_workspace.WorkSize.y - inset);
    }
    ui_prepare_sidebars();
    ImGuiViewport layout = ui_workspace;
    layout.WorkPos.x += ui_sidebar_size[0];
    layout.WorkSize.x -= ui_sidebar_size[0] + ui_sidebar_size[1];
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io->ConfigWindowsMoveFromTitleBarOnly = true;
    ImGuiDockNode *root = ImGui_DockBuilderGetNode(UI_DOCKSPACE_ID);
    bool empty = !root || (!root->ChildNodes[0] && !root->ChildNodes[1] &&
                           root->Windows.Size == 0);
    if ((ui_reset_layout || (!ui_layout_built && empty)) &&
        layout.WorkSize.x > 0.0f && layout.WorkSize.y > 0.0f) {
        ui_build_default_layout();
        ui_reset_layout = 0;
        ui_layout_built = true;
    } else if (!empty) {
        ui_layout_built = true;
    }
    ui_update_scene_central_node();
    (void)ImGui_DockSpaceOverViewportEx(UI_DOCKSPACE_ID, &layout, ImGuiDockNodeFlags_None, NULL);
}
