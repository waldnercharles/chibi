#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "graph/document/extract.h"

#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>
#include <math.h>
#include <stdint.h>

static void ed_draw_transport_button(int index, int columns)
{
    const char *ids[] = { "graph.play", "graph.stop", "graph.step", "graph.restart" };
    bool playing = editor_ui_runtime_playing();
    bool session = editor_ui_runtime_session();
    editor_icon glyphs[] = { playing ? EDITOR_ICON_FA_PAUSE : EDITOR_ICON_FA_PLAY,
                             EDITOR_ICON_FA_STOP,
                             EDITOR_ICON_FA_FAST_FORWARD,
                             EDITOR_ICON_FA_REFRESH };
    editor_ui_command commands[] = { EDITOR_UI_COMMAND_RUNTIME_PLAY_PAUSE,
                                     EDITOR_UI_COMMAND_RUNTIME_STOP,
                                     EDITOR_UI_COMMAND_RUNTIME_STEP,
                                     EDITOR_UI_COMMAND_RUNTIME_RESTART };
    bool enabled = (index != 1 && index != 3) || session;
    enabled &= index != 2 || !playing;
    if (editor_ui_icon_button_sized(ids[index], glyphs[index], enabled, 32.0f)) {
        editor_ui_run_command(commands[index]);
    }
    editor_ui_track_item(ids[index]);
    if ((index + 1) % columns) {
        ImGui_SameLine();
    }
}

void ed_draw_toolbar(editor *e)
{
    bool show = ImGui_IsWindowHovered(ImGuiHoveredFlags_ChildWindows) ||
                ImGui_IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    float alpha = editor_ui_controls_opacity("graph.controls.opacity", show);
    if (!show && alpha == 0) {
        return;
    }
    ImVec2 saved = ImGui_GetCursorScreenPos();
    int buttons = e->ui_context == EDITOR_UI_CONTEXT_SYSTEM ? 7 : 8;
    int columns = (int)((e->canvas_size.x - 34.0f) / 38.0f);
    if (columns < 1) {
        columns = 1;
    }
    if (columns > buttons) {
        columns = buttons;
    }
    int rows = (buttons + columns - 1) / columns;
    float width = columns * 32.0f + (columns - 1) * 6.0f + 16.0f;
    float height = rows * 32.0f + (rows - 1) * 6.0f + 16.0f;
    bool overflow = height + 24.0f > e->canvas_size.y || columns < 2;
    if (overflow) {
        width = 86.0f;
        height = 48.0f;
    }
    ImVec2 min = { e->canvas_min.x + ed_max(8.0f, e->canvas_size.x - width - 12.0f),
                   e->canvas_min.y + 12.0f };
    ImGui_SetCursorScreenPos(min);
    ImGui_PushStyleVar(ImGuiStyleVar_Alpha, ImGui_GetStyle()->Alpha * fmaxf(alpha, 1.0f / 255));
    ImGui_PushStyleVar(ImGuiStyleVar_ChildRounding, 9.0f);
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding, (ImVec2){ 8.0f, 8.0f });
    ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing, (ImVec2){ 6.0f, 6.0f });
    ImGui_PushStyleColorImVec4(ImGuiCol_ChildBg, (ImVec4){ 0.12f, 0.13f, 0.15f, 0.97f });
    bool visible = ImGui_BeginChild(
        "graph_floating_controls",
        (ImVec2){ width, height },
        ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
            (show ? 0 : ImGuiWindowFlags_NoInputs)
    );
    if (visible) {
        bool controls = true;
        if (overflow) {
            ed_draw_transport_button(0, 2);
            if (editor_ui_icon_button_sized("graph_more_button", EDITOR_ICON_FA_ELLIPSIS_V, true, 32.0f)) {
                editor_ui_open_popup("graph_more");
            }
            editor_ui_track_item("graph.more");
            controls = editor_ui_begin_popup("graph_more");
            columns = 1;
        }
        if (controls) {
            for (int i = overflow ? 1 : 0; i < 4; i++) {
                ed_draw_transport_button(i, columns);
            }
            if (editor_ui_icon_button_sized(
                    "graph.arrange",
                    EDITOR_ICON_FA_SITEMAP,
                    acount(e->active_graph->nodes) > 0,
                    32.0f
                )) {
                ed_auto_arrange(e);
            }
            editor_ui_track_item("graph.arrange");
            if (ImGui_IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui_SetTooltip("Arrange nodes");
            }
            if (5 % columns) {
                ImGui_SameLine();
            }
            if (editor_ui_icon_button_sized("graph_actions_button", EDITOR_ICON_FA_ELLIPSIS_V, true, 32.0f)) {
                editor_ui_open_popup("graph_actions");
            }
            editor_ui_track_item("graph.actions");
            if (editor_ui_begin_popup("graph_actions")) {
                const char *reason = behavior_asset_can_extract_function(
                    e->asset,
                    e->active_graph,
                    e->selected,
                    acount(e->selected)
                );
                if (ImGui_MenuItemEx("Extract Function...", NULL, false, !reason && e->ui_context != EDITOR_UI_CONTEXT_SYSTEM)) {
                    ed_request_extraction(e);
                }
                editor_ui_track_item("graph.extract");
                ImGui_Separator();
                if (ImGui_MenuItem("Compile")) {
                    ed_compile(e);
                }
                if (ImGui_MenuItemEx(
                        "Fit graph",
                        NULL,
                        false,
                        acount(e->active_graph->nodes) > 0
                    )) {
                    ed_zoom_fit(e, true);
                }
                if (ImGui_MenuItemEx(
                        "Optimize graph",
                        NULL,
                        false,
                        acount(e->active_graph->nodes) > 0 && ed_binding_scope_available(e)
                    )) {
                    (void)ed_optimize_graph(e);
                }
                ImGui_Separator();
                if (ImGui_MenuItemEx("Undo", NULL, false, acount(e->undo) > 0)) {
                    ed_undo(e);
                }
                editor_ui_track_item("graph.undo");
                if (ImGui_MenuItemEx("Redo", NULL, false, acount(e->redo) > 0)) {
                    ed_redo(e);
                }
                editor_ui_track_item("graph.redo");
                editor_ui_end_popup();
            }
            if (6 % columns) {
                ImGui_SameLine();
            }
            if (editor_ui_icon_button_sized(
                    "graph_fit_button",
                    EDITOR_ICON_FA_EXPAND,
                    acount(e->active_graph->nodes) > 0,
                    32.0f
                )) {
                ed_zoom_fit(e, true);
            }
            editor_ui_track_item("graph.fit");
            if (e->ui_context != EDITOR_UI_CONTEXT_SYSTEM) {
                if (7 % columns) {
                    ImGui_SameLine();
                }
                if (editor_ui_icon_button_sized("graph_debug_button", EDITOR_ICON_FA_BUG, true, 32.0f)) {
                    editor_ui_open_popup("graph_debug");
                }
                editor_ui_track_item("graph.debug");
                if (editor_ui_begin_popup("graph_debug")) {
                    editor_ui_draw_graph_debug_header();
                    ImGui_Separator();
                    ed_draw_debug_menu(e);
                    editor_ui_end_popup();
                }
            }
            if (overflow) {
                editor_ui_end_popup();
            }
        }
    }
    e->controls_hovered |= show &&
                           (ImGui_IsWindowHovered(ImGuiHoveredFlags_ChildWindows) ||
                            ed_point_in_rect(
                                ImGui_GetIO()->MousePos,
                                min,
                                ed_add(min, (ImVec2){ width, height })
                            ));
    ImGui_EndChild();
    ImGui_PopStyleColor();
    ImGui_PopStyleVarEx(4);
    ImGui_SetCursorScreenPos(saved);
}

static void ed_draw_log(editor *e)
{
    ImGui_BeginDisabled(e->log_count == 0);
    int clear = ImGui_Button("Clear");
    ImGui_EndDisabled();
    if (clear) {
        e->log_count = 0;
        ed_rebuild_log_text(e);
    }
    int visible = ImGui_BeginChild("output_log", (ImVec2){ 0.0f, 0.0f }, 0, 0);
    if (visible) {
        int follow = ImGui_GetScrollY() >= ImGui_GetScrollMaxY() - 2.0f;
        if (e->log_count) {
            int lines = 1;
            for (const char *text = e->log_text; *text; text++) {
                lines += *text == '\n';
            }
            float height = ImGui_GetFontSize() * lines +
                           ImGui_GetStyle()->FramePadding.y * 2.0f;
            ImGui_PushStyleColorImVec4(ImGuiCol_FrameBg, (ImVec4){ 0 });
            ImGui_PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui_InputTextMultilineEx(
                "##output_log_text",
                e->log_text,
                e->log_text_capacity,
                (ImVec2){ 0.0f, height },
                ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo,
                NULL,
                NULL
            );
            ImGui_PopStyleVar();
            ImGui_PopStyleColor();
        }
        if (follow && e->log_count) {
            ImGui_SetScrollHereY(1.0f);
        }
    }
    ImGui_EndChild();
}

void ed_draw_output_log(editor *e)
{
    if (!editor_ui_pane_visible(EDITOR_UI_PANE_OUTPUT_LOG)) {
        return;
    }
    ImGui_SetNextWindowSize((ImVec2){ 520.0f, 220.0f }, ImGuiCond_FirstUseEver);
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_OUTPUT_LOG)) {
        ImGui_SetNextWindowFocus();
    }
    if (!editor_ui_begin_pane(EDITOR_UI_PANE_OUTPUT_LOG, "Output Log", 0)) {
        ImGui_End();
        return;
    }
    ed_draw_log(e);
    ImGui_End();
}

void ed_draw_compiler_results(editor *e)
{
    if (!editor_ui_pane_visible(EDITOR_UI_PANE_COMPILER_RESULTS)) {
        return;
    }
    ImGui_SetNextWindowSize((ImVec2){ 520.0f, 220.0f }, ImGuiCond_FirstUseEver);
    if (editor_ui_take_focus(EDITOR_UI_FOCUS_COMPILER_RESULTS)) {
        ImGui_SetNextWindowFocus();
    }
    if (!editor_ui_begin_pane(EDITOR_UI_PANE_COMPILER_RESULTS, "Compiler Results", 0)) {
        ImGui_End();
        return;
    }
    ImGui_Text("Errors: %d  Warnings: %d", e->compile.error_count, e->compile.warning_count);
    if (!e->compile.error_count && !e->compile.warning_count) {
        ImGui_TextUnformatted("No compiler diagnostics.");
    }
    if (e->compile_text && e->compile_text[0]) {
        int lines = 1;
        for (const char *text = e->compile_text; *text; text++) {
            lines += *text == '\n';
        }
        float height = ImGui_GetFontSize() * lines +
                       ImGui_GetStyle()->FramePadding.y * 2.0f;
        ImGui_PushStyleColorImVec4(ImGuiCol_FrameBg, (ImVec4){ 0 });
        ImGui_PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui_InputTextMultilineEx(
            "##compiler_results_text",
            e->compile_text,
            e->compile_text_capacity,
            (ImVec2){ 0.0f, height },
            ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_NoUndoRedo,
            NULL,
            NULL
        );
        ImGui_PopStyleVar();
        ImGui_PopStyleColor();
    }
    ImGui_End();
}
