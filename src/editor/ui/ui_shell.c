#include "editor/ui/ui_shell.h"

#include "editor/workspace/editor.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static const float UI_HOVER_TIME_CONSTANT = 0.04f;
static const float UI_PRESS_DURATION = 0.1f;
static const float UI_POPUP_DURATION = 0.125f;
static const float UI_CONTROLS_DURATION = 0.2f;
static const int UI_PRIMARY_MOUSE_BUTTON = 0;
static const editor_icon UI_SEARCH_GLYPH = EDITOR_ICON_FA_SEARCH;

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
static float ui_menu_bottom;
static void ui_sidebar_begin(int side);
static bool ui_navigation_back;
static bool ui_navigation_forward;
static int ui_navigation_request;

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
static void (*ui_graph_debug_header)(void *);
static void *ui_graph_debug_user;
static bool ui_runtime_playing;
static bool ui_runtime_session;
static bool ui_can_undo;
static bool ui_can_redo;

static float ui_framebuffer_scale_x(void);
static float ui_framebuffer_scale_y(void);

static void ui_set_default_visibility(void)
{
    ui_scene_play_expanded = false;
    ui_focus_mode = false;
    ui_navigator_outline = false;
    ui_sidebar_width[0] = 260.0f;
    ui_sidebar_width[1] = 300.0f;
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
    editor_initialize_fonts();
    ImGui_GetIO()->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (!persist) {
        ImGui_GetIO()->IniFilename = NULL;
    }
    ImGui_RemoveSettingsHandler("Blueprints");
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
    bool visible = ImGui_Begin("Navigator", NULL, 0);
    if (!visible) {
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

void editor_ui_set_navigation_available(bool back, bool forward)
{
    ui_navigation_back = back;
    ui_navigation_forward = forward;
}

int editor_ui_take_navigation()
{
    int request = ui_navigation_request;
    ui_navigation_request = 0;
    return request;
}

editor_ui_pane editor_ui_navigator_tab(editor_ui_pane outline)
{
    return ui_navigator_outline ? outline : EDITOR_UI_PANE_CONTENT;
}

void editor_ui_restore_navigator_tab(editor_ui_pane tab)
{
    ui_navigator_outline = tab != EDITOR_UI_PANE_CONTENT;
    ui_settings_changed();
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

void editor_ui_set_runtime_target(const char *label, bool preview_available, bool locked)
{
    (void)label;
    (void)preview_available;
    (void)locked;
}

bool editor_ui_runtime_playing()
{
    return ui_runtime_playing;
}

bool editor_ui_runtime_session()
{
    return ui_runtime_session;
}

void editor_ui_set_graph_debug_header(void (*draw)(void *), void *user)
{
    ui_graph_debug_header = draw;
    ui_graph_debug_user = user;
}

void editor_ui_draw_graph_debug_header()
{
    if (ui_graph_debug_header) {
        ui_graph_debug_header(ui_graph_debug_user);
    }
}

void editor_ui_set_command_handler(editor_ui_command_handler handler, void *user)
{
    ui_command_handler = handler;
    ui_command_user = user;
    ui_runtime_playing = false;
    ui_runtime_session = false;
    editor_ui_set_runtime_target("Scene", false, false);
}

static ImU32 ui_color(ImVec4 color)
{
    color.w *= ImGui_GetStyle()->Alpha;
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

const char *editor_ui_asset_icon(editor_ui_asset_kind kind)
{
    switch (kind) {
        case EDITOR_UI_ASSET_FOLDER: return "\xef\x81\xbb";
        case EDITOR_UI_ASSET_PROTOTYPE: return "\xef\x86\xb2";
        case EDITOR_UI_ASSET_BEHAVIOR: return "\xef\x84\xa6";
        case EDITOR_UI_ASSET_SCENE: return "\xef\x81\xae";
        case EDITOR_UI_ASSET_SYSTEM: return "\xef\x82\x85";
        case EDITOR_UI_ASSET_DATA: return "\xef\x87\x80";
        case EDITOR_UI_ASSET_FILE: return "\xef\x85\x9b";
    }
    assert(false);
    return "";
}

void editor_ui_identity(const char *name, const char *detail, editor_ui_asset_kind kind)
{
    ImGui_BeginGroup();
    ImGui_AlignTextToFramePadding();
    ImGui_TextUnformatted(editor_ui_asset_icon(kind));
    ImGui_SameLine();
    ImGui_BeginGroup();
    ImGui_PushFontFloat(NULL, ImGui_GetFontSize() + 3.0f);
    ImGui_TextUnformatted(name);
    ImGui_PopFont();
    ImGui_TextDisabled("%s", detail);
    ImGui_EndGroup();
    ImGui_EndGroup();
}

void editor_ui_property_label(const char *label, float action_width)
{
    ImVec2 min = ImGui_GetCursorScreenPos();
    float width = ImGui_GetContentRegionAvail().x;
    float label_width = floorf(width * 0.38f);
    ImGui_AlignTextToFramePadding();
    ImGui_PushTextWrapPos(ImGui_GetCursorPosX() + fmaxf(1.0f, label_width - 8.0f));
    ImGui_TextUnformatted(label);
    ImGui_PopTextWrapPos();
    ImGui_SameLineEx(0.0f, 0.0f);
    ImGui_SetCursorScreenPos((ImVec2){ min.x + label_width, min.y });
    ImGui_SetNextItemWidth(fmaxf(1.0f, width - label_width - action_width));
}

/* Solve x before y. State changes use (.77, 0, .175, 1), entries and
 * presses use (.23, 1, .32, 1). */
static float ui_ease(float progress, bool state_change)
{
    if (progress <= 0 || progress >= 1) {
        return progress;
    }
    float lo = 0, hi = 1;
    float x1 = state_change ? 0.77f : 0.23f;
    float x2 = state_change ? 0.175f : 0.32f;
    for (int i = 0; i < 16; i++) {
        float t = (lo + hi) * 0.5f;
        float r = 1 - t;
        float x = 3 * r * r * t * x1 + 3 * r * t * t * x2 + t * t * t;
        if (x < progress) {
            lo = t;
        } else {
            hi = t;
        }
    }
    float r = 1 - (lo + hi) * 0.5f;
    return state_change ? (1 - r) * (1 - r) * (1 + 2 * r) : 1 - r * r * r;
}

static float ui_transition(const char *id, float target, float duration, bool immediate, bool state_change)
{
    ImGui_PushID(id);
    ImGuiID value_id = ImGui_GetID("value");
    ImGuiID target_id = ImGui_GetID("target");
    ImGuiID start_id = ImGui_GetID("start");
    ImGuiID elapsed_id = ImGui_GetID("elapsed");
    ImGuiID frame_id = ImGui_GetID("frame");
    ImGui_PopID();
    ImGuiStorage *storage = ImGui_GetStateStorage();
    float value = ImGuiStorage_GetFloat(storage, value_id, 0);
    float start = ImGuiStorage_GetFloat(storage, start_id, 0);
    float elapsed = ImGuiStorage_GetFloat(storage, elapsed_id, 0);
    int frame = ImGui_GetFrameCount();
    bool stale = ImGuiStorage_GetInt(storage, frame_id, -1) != frame - 1;
    if (stale) {
        value = start = elapsed = 0;
    }
    bool changed = stale || target != ImGuiStorage_GetFloat(storage, target_id, 0);
    if (changed) {
        start = value;
        elapsed = 0;
    }
    if (immediate) {
        elapsed = duration;
    } else if (!changed) {
        elapsed = fminf(duration, elapsed + ImGui_GetIO()->DeltaTime);
    }
    value = start + (target - start) * ui_ease(elapsed / duration, state_change);
    ImGuiStorage_SetInt(storage, frame_id, frame);
    ImGuiStorage_SetFloat(storage, value_id, value);
    ImGuiStorage_SetFloat(storage, target_id, target);
    ImGuiStorage_SetFloat(storage, start_id, start);
    ImGuiStorage_SetFloat(storage, elapsed_id, elapsed);
    return value;
}

static bool ui_keyboard_activation()
{
    ImGuiContext *context = ImGui_GetCurrentContext();
    return context->NavActivateId == ImGui_GetItemID();
}

float editor_ui_controls_opacity(const char *id, bool visible)
{
    return ui_transition(id, visible ? 1 : 0, UI_CONTROLS_DURATION, false, true);
}

void editor_ui_open_popup(const char *id)
{
    ImGui_PushID(id);
    ImGuiStorage *storage = ImGui_GetStateStorage();
    ImGuiStorage_SetFloat(storage, ImGui_GetID("popup.opened"), (float)ImGui_GetTime());
    ImGuiStorage_SetBool(storage, ImGui_GetID("popup.keyboard"), ui_keyboard_activation());
    ImGui_PopID();
    ImGui_OpenPopup(id, 0);
}

static bool ui_popup_keyboard_input()
{
    const ImGuiKey keys[] = { ImGuiKey_Tab,
                              ImGuiKey_Enter,
                              ImGuiKey_Space,
                              ImGuiKey_LeftArrow,
                              ImGuiKey_RightArrow,
                              ImGuiKey_UpArrow,
                              ImGuiKey_DownArrow,
                              ImGuiKey_GamepadFaceDown,
                              ImGuiKey_GamepadDpadLeft,
                              ImGuiKey_GamepadDpadRight,
                              ImGuiKey_GamepadDpadUp,
                              ImGuiKey_GamepadDpadDown };
    for (int i = 0; i < (int)(sizeof keys / sizeof *keys); i++) {
        if (ImGui_IsKeyPressed(keys[i])) {
            return true;
        }
    }
    return false;
}

bool editor_ui_begin_popup(const char *id)
{
    ImGui_PushID(id);
    ImGuiStorage *storage = ImGui_GetStateStorage();
    ImGuiID keyboard_id = ImGui_GetID("popup.keyboard");
    bool keyboard = ImGuiStorage_GetBool(storage, keyboard_id, true) ||
                    ui_popup_keyboard_input();
    ImGuiStorage_SetBool(storage, keyboard_id, keyboard);
    float opened = ImGuiStorage_GetFloat(storage, ImGui_GetID("popup.opened"), 0);
    ImGui_PopID();
    float elapsed = fmaxf(0, (float)ImGui_GetTime() - opened);
    /* ImGui skips input processing at Alpha == 0; keep the opening frame live. */
    float alpha = keyboard
                    ? 1
                    : fmaxf(1.0f / 255, ui_ease(fminf(1, elapsed / UI_POPUP_DURATION), false));
    ImGui_PushStyleVar(ImGuiStyleVar_Alpha, ImGui_GetStyle()->Alpha * alpha);
    if (ImGui_BeginPopup(id, 0)) {
        return true;
    }
    ImGui_PopStyleVar();
    return false;
}

void editor_ui_end_popup()
{
    ImGui_EndPopup();
    ImGui_PopStyleVar();
}

bool editor_ui_begin_attachment_picker(const char *label, const char *id, char *query, size_t query_size)
{
    ImGui_PushID(id);
    ImGui_AlignTextToFramePadding();
    ImGui_TextUnformatted(label);
    ImGui_SameLine();
    float button_width = ImGui_CalcTextSize("Manage \xef\x84\x87").x +
                         ImGui_GetStyle()->FramePadding.x * 2.0f;
    ImGui_SetCursorPosX(
        ImGui_GetCursorPosX() + fmaxf(0.0f, ImGui_GetContentRegionAvail().x - button_width)
    );
    if (ImGui_Button("Manage \xef\x84\x87")) {
        query[0] = '\0';
        editor_ui_open_popup("attachments");
    }
    editor_ui_track_item(id);
    ImVec2 space = ImGui_GetMainViewport()->WorkSize;
    ImGui_SetNextWindowSize((ImVec2){ fminf(330.0f, space.x - 20.0f), 0.0f }, ImGuiCond_Always);
    if (!editor_ui_begin_popup("attachments")) {
        ImGui_PopID();
        return false;
    }
    if (ImGui_IsWindowAppearing()) {
        ImGui_SetKeyboardFocusHere();
    }
    char search_id[192], hint[128];
    snprintf(search_id, sizeof search_id, "%s.search", id);
    snprintf(hint, sizeof hint, "Search %s", label);
    ImGui_SetNextItemWidth(-1.0f);
    editor_ui_search_field(search_id, query, query_size, hint);
    if (ImGui_IsKeyPressed(ImGuiKey_Escape)) {
        ImGui_CloseCurrentPopup();
    }
    ImGui_SetNextWindowSizeConstraints(
        (ImVec2){ 0.0f, ImGui_GetFrameHeightWithSpacing() },
        (ImVec2){ FLT_MAX, fmaxf(1.0f, fminf(224.0f, space.y - 140.0f)) },
        NULL,
        NULL
    );
    ImGui_BeginChild("results", (ImVec2){ 0.0f, 0.0f }, ImGuiChildFlags_AutoResizeY, 0);
    return true;
}

void editor_ui_end_attachment_picker(int selected)
{
    ImGui_EndChild();
    ImGui_Separator();
    ImGui_TextDisabled("%d selected", selected);
    ImGui_SameLine();
    float width = ImGui_CalcTextSize("Done").x + ImGui_GetStyle()->FramePadding.x * 2.0f;
    ImGui_SetCursorPosX(
        ImGui_GetCursorPosX() + fmaxf(0.0f, ImGui_GetContentRegionAvail().x - width)
    );
    if (ImGui_Button("Done")) {
        ImGui_CloseCurrentPopup();
    }
    editor_ui_end_popup();
    ImGui_PopID();
}

bool editor_ui_begin_vector(const char *label)
{
    if (!ImGui_BeginTable("vector", 3, ImGuiTableFlags_SizingStretchProp)) {
        return false;
    }
    ImGui_TableSetupColumnEx("label", 0, 0.28f, 0);
    ImGui_TableSetupColumnEx("x", 0, 0.36f, 0);
    ImGui_TableSetupColumnEx("y", 0, 0.36f, 0);
    ImGui_TableNextRow();
    ImGui_TableNextColumn();
    ImGui_AlignTextToFramePadding();
    ImGui_TextUnformatted(label);
    ImGui_TableNextColumn();
    return true;
}

void editor_ui_next_axis()
{
    ImGui_TableNextColumn();
}
void editor_ui_end_vector()
{
    ImGui_EndTable();
}

bool editor_ui_axis_float(const char *label, float *value, int axis, bool overridden)
{
    static const ImVec4 colors[] = {
        { 0.93f, 0.47f, 0.47f, 1.0f },
        { 0.47f, 0.79f, 0.58f, 1.0f },
        { 0.45f, 0.67f, 1.0f, 1.0f },
    };
    assert(axis >= 0 && axis < 3);
    ImGui_AlignTextToFramePadding();
    ImGui_TextUnformatted(label);
    ImGui_SameLineEx(0.0f, 4.0f);
    ImGui_SetNextItemWidth(
        fmaxf(1.0f, ImGui_GetContentRegionAvail().x - (overridden ? 28.0f : 0.0f))
    );
    ImGui_PushStyleVarImVec2(
        ImGuiStyleVar_FramePadding,
        (ImVec2){ 4.0f, ImGui_GetStyle()->FramePadding.y }
    );
    bool changed = ImGui_InputFloatEx("###field_value", value, 0.0f, 0.0f, "%.6g", 0);
    ImGui_PopStyleVar();
    ImVec2 min = ImGui_GetItemRectMin(), max = ImGui_GetItemRectMax();
    ImDrawList_AddLineEx(
        ImGui_GetWindowDrawList(),
        min,
        (ImVec2){ min.x, max.y },
        ui_color(colors[axis]),
        2.0f
    );
    return changed;
}

static bool ui_icon_button(const char *id, editor_icon glyph, bool enabled, ImVec2 size, float font_size)
{
    ImVec2 min = ImGui_GetCursorScreenPos();
    ImGui_BeginDisabled(!enabled);
    bool pressed = ImGui_InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
    enabled = !(ImGui_GetCurrentContext()->LastItemData.ItemFlags & ImGuiItemFlags_Disabled);
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
    float blend = ui_reduce_motion || !enabled
                    ? 1.0f
                    : 1.0f - expf(-ImGui_GetIO()->DeltaTime / UI_HOVER_TIME_CONSTANT);
    hover += (target - hover) * blend;
    ImGuiStorage_SetFloat(storage, animation_id, hover);
    background.x += (UI_BUTTON_HOVER.x - background.x) * hover;
    background.y += (UI_BUTTON_HOVER.y - background.y) * hover;
    background.z += (UI_BUTTON_HOVER.z - background.z) * hover;
    if (enabled && ImGui_IsItemActive()) {
        background = UI_BUTTON_ACTIVE;
    }
    ImGui_PushID(id);
    bool pointer_press = enabled && ImGui_IsItemActive() &&
                         ImGui_GetCurrentContext()->ActiveIdSource ==
                             ImGuiInputSource_Mouse;
    float compression = ui_transition(
        "press.animation",
        pointer_press ? 1 : 0,
        UI_PRESS_DURATION,
        ui_reduce_motion || !pointer_press,
        false
    );
    ImGui_PopID();
    float scale = 1 - compression * 0.03f;
    ImVec2 inset = { size.x * (1 - scale) * 0.5f, size.y * (1 - scale) * 0.5f };
    ImVec2 visual_min = { min.x + inset.x, min.y + inset.y };
    ImVec2 visual_max = { min.x + size.x - inset.x, min.y + size.y - inset.y };
    ImDrawList_AddRectFilledEx(draw, visual_min, visual_max, ui_color(background), 4.0f, ImDrawFlags_RoundCornersAll);
    if (hovered) {
        ImDrawList_AddRectEx(draw, visual_min, visual_max, ui_color(UI_BORDER), 4.0f, ImDrawFlags_RoundCornersAll, 1.0f);
    }
    ui_draw_centered_icon(
        draw,
        ImGui_GetFont(),
        font_size * scale,
        visual_min,
        visual_max,
        ui_color(enabled ? UI_TEXT : UI_TEXT_MUTED),
        (ImWchar)glyph
    );
    /* InvisibleButton draws its cursor before our background. Redraw it on top. */
    ImGui_RenderNavCursor((ImRect){ min, { min.x + size.x, min.y + size.y } }, ImGui_GetItemID());
    ImGui_EndDisabled();
    return pressed;
}

bool editor_ui_icon_button(const char *id, editor_icon glyph, bool enabled)
{
    return ui_icon_button(id, glyph, enabled, (ImVec2){ 26.0f, 24.0f }, ImGui_GetFontSize() - 1.0f);
}

bool editor_ui_icon_button_sized(const char *id, editor_icon glyph, bool enabled, float size)
{
    assert(isfinite(size) && size > 0);
    return ui_icon_button(id, glyph, enabled, (ImVec2){ size, size }, fminf(16.0f, size * 0.5f));
}

bool editor_ui_right_icon_button(const char *id, editor_icon glyph)
{
    const float width = 26.0f;
    ImGui_SameLine();
    ImGui_SetCursorPosX(ImGui_GetCursorPosX() + ImGui_GetContentRegionAvail().x - width);
    return editor_ui_icon_button(id, glyph, true);
}

bool editor_ui_search_field(const char *id, char *buffer, size_t buffer_size, const char *hint)
{
    ImGui_PushID(id);
    ImGuiStorage *storage = ImGui_GetStateStorage();
    ImGuiID focus_id = ImGui_GetID("focus");
    if (ImGuiStorage_GetBool(storage, focus_id, false)) {
        ImGui_SetKeyboardFocusHere();
        ImGuiStorage_SetBool(storage, focus_id, false);
    }
    const ImVec2 min = ImGui_GetCursorScreenPos();
    const float height = ImGui_GetFrameHeight();
    const float width = ImGui_CalcItemWidth();
    const ImVec2 max = { min.x + width, min.y + height };
    ImDrawList_AddRectFilledEx(ImGui_GetWindowDrawList(), min, max, ui_color(UI_FRAME), 5.0f, ImDrawFlags_RoundCornersAll);
    ImGui_BeginGroup();
    ImGui_SetNextItemWidth(fmaxf(1.0f, width - height));
    ImGui_PushStyleVarImVec2(
        ImGuiStyleVar_FramePadding,
        (ImVec2){ 30.0f, ImGui_GetStyle()->FramePadding.y }
    );
    bool changed = ImGui_InputTextWithHint("##input", hint, buffer, buffer_size, 0);
    ImGui_PopStyleVar();
    if (ImGui_IsItemActive() || ImGui_IsItemFocused()) {
        ImDrawList_AddRectEx(
            ImGui_GetWindowDrawList(),
            min,
            max,
            ui_color(UI_ACCENT),
            5.0f,
            ImDrawFlags_RoundCornersAll,
            1.0f
        );
    }
    ui_draw_centered_icon(
        ImGui_GetWindowDrawList(),
        ImGui_GetFont(),
        ImGui_GetFontSize() - 2.0f,
        (ImVec2){ min.x + 6.0f, min.y },
        (ImVec2){ min.x + 24.0f, min.y + height },
        ui_color(UI_TEXT_MUTED),
        UI_SEARCH_GLYPH
    );
    ImGui_SameLineEx(0.0f, 0.0f);
    if (buffer[0]) {
        ImVec2 clear_min = { max.x - height, min.y };
        if (ImGui_InvisibleButton("Clear search", (ImVec2){ height, height }, ImGuiButtonFlags_EnableNav)) {
            buffer[0] = '\0';
            changed = true;
            ImGuiStorage_SetBool(storage, focus_id, true);
        }
        char clear_id[192];
        snprintf(clear_id, sizeof clear_id, "%s.clear", id);
        editor_ui_track_item(clear_id);
        ui_draw_centered_icon(
            ImGui_GetWindowDrawList(),
            ImGui_GetFont(),
            ImGui_GetFontSize() - 3.0f,
            clear_min,
            max,
            ui_color(ImGui_IsItemHovered(0) ? UI_TEXT : UI_TEXT_MUTED),
            EDITOR_ICON_FA_CLOSE
        );
    } else {
        ImGui_Dummy((ImVec2){ height, height });
    }
    ImGui_EndGroup();
    editor_ui_track_item(id);
    ImGui_PopID();
    return changed;
}

static bool ui_disclosure(const char *label, const char *id, bool default_open, float width)
{
    ImGuiStorage *storage = ImGui_GetStateStorage();
    ImGuiID open_id = ImGui_GetID(id);
    bool open = ImGuiStorage_GetBool(storage, open_id, default_open);
    ImVec2 min = ImGui_GetCursorScreenPos();
    ImVec2 size = { fmaxf(1.0f, width), 28.0f };
    bool keyboard = false;
    if (ImGui_InvisibleButton(id, size, ImGuiButtonFlags_EnableNav)) {
        open = !open;
        keyboard = ui_keyboard_activation();
    }
    bool focused = ImGui_IsItemFocused();
    if (focused) {
        /* These arrows change disclosure state, not the neighboring focus. */
        ImGui_SetKeyOwner(ImGuiKey_LeftArrow, open_id, 0);
        ImGui_SetKeyOwner(ImGuiKey_RightArrow, open_id, 0);
    }
    if (focused && ImGui_IsKeyPressed(ImGuiKey_LeftArrow)) {
        open = false;
        keyboard = true;
    }
    if (focused && ImGui_IsKeyPressed(ImGuiKey_RightArrow)) {
        open = true;
        keyboard = true;
    }
    ImGuiStorage_SetBool(storage, open_id, open);
    ImGui_PushID(id);
    ImGuiID animation_id = ImGui_GetID("disclosure.angle");
    ImGui_PopID();
    float target = open ? 1.0f : 0.0f;
    float progress = ImGuiStorage_GetFloat(storage, animation_id, target);
    float blend = ui_reduce_motion || keyboard
                    ? 1.0f
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
                               min.y + size.y * 0.5f + points[i].x * s +
                                   points[i].y * c };
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
    return open;
}

bool editor_ui_disclosure(const char *label, const char *id)
{
    bool open = ui_disclosure(label, id, false, ImGui_GetContentRegionAvail().x);
    if (open) {
        ImGui_TreePush(id);
    }
    return open;
}

bool editor_ui_component_header(const char *label, const char *id, bool *removed)
{
    ImVec2 min = ImGui_GetCursorScreenPos();
    float width = ImGui_GetContentRegionAvail().x;
    float action_width = removed ? 28.0f : 0.0f;
    bool open = ui_disclosure(label, id, true, width - action_width);
    editor_ui_track_item(id);
    bool focused = ImGui_IsItemFocused();
    if (removed) {
        ImGui_SameLineEx(0, 0);
        char remove_id[192];
        snprintf(remove_id, sizeof remove_id, "%s.remove", id);
        *removed = ImGui_InvisibleButton(remove_id, (ImVec2){ action_width, 28 }, ImGuiButtonFlags_EnableNav);
        editor_ui_track_item(remove_id);
        bool hovered = ImGui_IsMouseHoveringRect(min, (ImVec2){ min.x + width, min.y + 28 });
        if (hovered || focused || ImGui_IsItemFocused()) {
            ui_draw_centered_icon(
                ImGui_GetWindowDrawList(),
                ImGui_GetFont(),
                ImGui_GetFontSize(),
                ImGui_GetItemRectMin(),
                ImGui_GetItemRectMax(),
                ui_color(ImGui_IsItemHovered(0) ? UI_TEXT : UI_TEXT_MUTED),
                EDITOR_ICON_FA_TRASH
            );
        }
    }
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
    ImGuiID navigator;
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
    float navigator_ratio = fminf(0.3f, ui_sidebar_width[0] / layout.WorkSize.x);
    ImGui_DockBuilderSplitNode(UI_DOCKSPACE_ID, ImGuiDir_Left, navigator_ratio, &navigator, &center);
    ImGui_DockBuilderDockWindow("Navigator", navigator);
    ImGui_DockBuilderSplitNode(center, ImGuiDir_Right, 0.23f, &inspector, &center);
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

void editor_ui_set_history_available(bool undo, bool redo)
{
    ui_can_undo = undo;
    ui_can_redo = redo;
}

void editor_ui_set_preview_status(const char *label, const char *detail, bool restart)
{
    (void)label;
    (void)detail;
    (void)restart;
}

void editor_ui_set_runtime_status(const char *label)
{
    (void)label;
}

static void ui_document_action(const char *label, const char *shortcut, const char *id, editor_ui_command command)
{
    bool enabled = ui_command_handler != NULL;
    if (command == EDITOR_UI_COMMAND_UNDO) {
        enabled &= ui_can_undo;
    }
    if (command == EDITOR_UI_COMMAND_REDO) {
        enabled &= ui_can_redo;
    }
    if (ImGui_MenuItemEx(label, shortcut, false, enabled)) {
        editor_ui_run_command(command);
    }
    editor_ui_track_item(id);
}

static void ui_draw_menu(void)
{
    if (!ImGui_BeginMainMenuBar()) {
        return;
    }
    ui_menu_bottom = ImGui_GetWindowPos().y + ImGui_GetWindowSize().y;
    ImGui_TextColoredUnformatted(UI_ACCENT, "Blueprints");
    bool open = ImGui_BeginMenu("File");
    editor_ui_track_item("file.menu");
    if (open) {
        ui_document_action("Save", "Ctrl+S", "file.save", EDITOR_UI_COMMAND_SAVE);
        ui_document_action("Save As", NULL, "file.save_as", EDITOR_UI_COMMAND_SAVE_AS);
        ui_document_action("Save All", NULL, "file.save_all", EDITOR_UI_COMMAND_SAVE_ALL);
        ui_document_action("Reload", NULL, "file.reload", EDITOR_UI_COMMAND_RELOAD);
        ImGui_EndMenu();
    }
    bool edit = ImGui_BeginMenu("Edit");
    editor_ui_track_item("edit.menu");
    if (edit) {
        ui_document_action("Undo", "Ctrl+Z", "edit.undo", EDITOR_UI_COMMAND_UNDO);
        ui_document_action("Redo", "Ctrl+Shift+Z", "edit.redo", EDITOR_UI_COMMAND_REDO);
        ImGui_EndMenu();
    }
    bool view = ImGui_BeginMenu("View");
    editor_ui_track_item("view.menu");
    if (view) {
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
        if (ui_framebuffer_target) {
            /* ImGui layouts use logical coordinates; the renderer applies the
             * framebuffer scale when drawing into the offscreen target. */
            size.x /= ui_framebuffer_scale_x();
            size.y /= ui_framebuffer_scale_y();
        }
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
    ImGuiWindow *window = ImGui_FindWindowByName("Navigator");
    float width = ui_navigator_visible() && window ? window->Size.x
                                                   : ui_sidebar_size[0];
    return width * ui_framebuffer_scale_x();
}

float editor_ui_left_pane_splitter_x(void)
{
    ImGuiWindow *window = ImGui_FindWindowByName("Navigator");
    float edge = ui_navigator_visible() && window
                   ? window->Pos.x + window->Size.x
                   : ui_workspace.WorkPos.x + ui_sidebar_size[0];
    return edge * ui_framebuffer_scale_x();
}

float editor_ui_left_pane_center_y(void)
{
    ImGuiWindow *window = ImGui_FindWindowByName("Navigator");
    float center = ui_navigator_visible() && window
                     ? window->Pos.y + window->Size.y * 0.5f
                     : ui_workspace.WorkPos.y + ui_workspace.WorkSize.y * 0.5f;
    return center * ui_framebuffer_scale_y();
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
    if (editor_ui_icon_button(
            "collapse",
            side ? EDITOR_ICON_CODICON_CHEVRON_LEFT : EDITOR_ICON_CODICON_CHEVRON_RIGHT,
            true
        )) {
        ui_sidebar_toggle(side);
    }
    editor_ui_track_item(side ? "inspector.collapse" : "navigator.collapse");
    if (!side) {
        ImGui_SameLine();
        if (editor_ui_icon_button("back", EDITOR_ICON_FA_ARROW_LEFT, ui_navigation_back)) {
            ui_navigation_request = -1;
        }
        editor_ui_track_item("navigator.back");
        ImGui_SameLine();
        if (editor_ui_icon_button("forward", EDITOR_ICON_FA_ARROW_RIGHT, ui_navigation_forward)) {
            ui_navigation_request = 1;
        }
        editor_ui_track_item("navigator.forward");
        ImGui_SameLine();
        ImGui_TextUnformatted("Navigator");
    }
    ImGui_Separator();
}

static void ui_prepare_sidebars()
{
    for (int side = 0; side < 2; side++) {
        bool open = ui_sidebar_open(side);
        /* Reopen rails have separate identities so they never overwrite the
         * movable panes' saved positions or docking assignments. */
        ui_sidebar_size[side] = open ? 0.0f : 32.0f;
        if (open) {
            continue;
        }
        ui_sidebar_begin(side);
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){ 4.0f, 8.0f });
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowMinSize, (ImVec2){ 24.0f, 24.0f });
        if (ImGui_Begin(
                side ? "Inspector Rail" : "Navigator Rail",
                NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking |
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
            )) {
            if (editor_ui_icon_button_sized(
                    "expand",
                    side ? EDITOR_ICON_CODICON_CHEVRON_UP : EDITOR_ICON_CODICON_CHEVRON_DOWN,
                    true,
                    24.0f
                )) {
                ui_sidebar_toggle(side);
            }
            editor_ui_track_item(side ? "inspector.expand" : "navigator.expand");
        }
        ImGui_End();
        ImGui_PopStyleVarEx(2);
    }
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
        ImGui_GetIO()->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ui_apply_theme();
        ui_context = context;
        ui_ensure_visibility();
        ui_layout_built = false;
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

char *editor_ui_checkpoint()
{
    return xstrdup(ImGui_SaveIniSettingsToMemory(NULL));
}

bool editor_ui_restore_checkpoint(const char *ini)
{
    if (!ini || !strstr(ini, "[Blueprints][Workspace]")) {
        return false;
    }
    ImGui_LoadIniSettingsFromMemory(ini, strlen(ini));
    return true;
}

void editor_ui_deactivate()
{
    ImGui_ClearActiveID();
    ImGui_ClosePopupsOverWindow(NULL, false);
    ImGui_RemoveSettingsHandler("Blueprints");
    ui_command_handler = NULL;
    ui_command_user = NULL;
    ui_graph_debug_header = NULL;
    ui_graph_debug_user = NULL;
    afree(ui_item_hits);
}
