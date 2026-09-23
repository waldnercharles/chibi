#include "editor/ui/ui_shell.h"

#include <imgui/dcimgui.h>
#include <stdio.h>

typedef struct
{
    editor_ui_command commands[16];
    int command_count;
    int native_presses;
    bool focus_native;
    bool native_focused;
} rmlui_menu_fixture;

static rmlui_menu_fixture *active_fixture;

static int menu_command(editor_ui_command command, void *user)
{
    rmlui_menu_fixture *fixture = user;
    if (fixture->command_count <
        (int)(sizeof fixture->commands / sizeof *fixture->commands)) {
        fixture->commands[fixture->command_count] = command;
    }
    fixture->command_count++;
    return 0;
}

static void menu_frame()
{
    ImGui_NewFrame();
    editor_ui_prepare();
    ImGui_SetNextWindowPos((ImVec2){ 940, 100 }, ImGuiCond_Always);
    ImGui_SetNextWindowSize((ImVec2){ 220, 100 }, ImGuiCond_Always);
    ImGui_Begin(
        "Native keyboard target",
        NULL,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings
    );
    if (active_fixture->focus_native) {
        ImGui_SetWindowFocus();
        ImGui_SetKeyboardFocusHere();
        active_fixture->focus_native = false;
    }
    if (ImGui_Button("Native button")) {
        active_fixture->native_presses++;
    }
    active_fixture->native_focused = ImGui_IsItemFocused();
    ImGui_End();
    ImGui_Render();
}

static bool item_center(const char *id, ImVec2 *center)
{
    editor_ui_rect rect;
    if (!editor_ui_item_rect(id, &rect) || rect.width <= 0 || rect.height <= 0) {
        return false;
    }
    *center = (ImVec2){ rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f };
    return true;
}

static bool click_at(ImVec2 center)
{
    ImGuiIO *io = ImGui_GetIO();
    ImGuiIO_AddMousePosEvent(io, center.x, center.y);
    menu_frame();
    ImGuiIO_AddMouseButtonEvent(io, 0, true);
    menu_frame();
    ImGuiIO_AddMouseButtonEvent(io, 0, false);
    menu_frame();
    return true;
}

static bool click_item(const char *id)
{
    ImVec2 center;
    return item_center(id, &center) && click_at(center);
}

static void menu_key(ImGuiKey key)
{
    ImGuiIO *io = ImGui_GetIO();
    ImGuiIO_AddKeyEvent(io, key, true);
    menu_frame();
    ImGuiIO_AddKeyEvent(io, key, false);
    menu_frame();
}

static void dismiss_menu()
{
    menu_key(ImGuiKey_Escape);
    if (editor_ui_captures_keyboard()) {
        menu_key(ImGuiKey_Escape);
    }
}

static bool command_is(const rmlui_menu_fixture *fixture, int index, editor_ui_command command)
{
    return index >= 0 && index < fixture->command_count &&
           index < (int)(sizeof fixture->commands / sizeof *fixture->commands) &&
           fixture->commands[index] == command;
}

static bool click_command(const char *trigger, const char *item, editor_ui_command command, rmlui_menu_fixture *fixture)
{
    dismiss_menu();
    int before = fixture->command_count;
    bool opened = click_item(trigger);
    bool visible = item_center(item, &(ImVec2){ 0 });
    bool clicked = click_item(item);
    bool dispatched = command_is(fixture, before, command);
    dismiss_menu();
    return opened && visible && clicked && dispatched;
}

static bool toggle_pane(editor_ui_pane pane, const char *item)
{
    bool before = editor_ui_pane_visible(pane);
    dismiss_menu();
    bool opened = click_item("view.menu");
    bool visible = item_center(item, &(ImVec2){ 0 });
    bool clicked = click_item(item);
    bool toggled = editor_ui_pane_visible(pane) != before;
    menu_key(ImGuiKey_Escape);
    editor_ui_set_pane_visible(pane, before);
    return opened && visible && clicked && toggled &&
           editor_ui_pane_visible(pane) == before;
}

bool test_rmlui_menu()
{
    bool panes_before[EDITOR_UI_PANE_COUNT];
    for (int pane = 0; pane < EDITOR_UI_PANE_COUNT; pane++) {
        panes_before[pane] = editor_ui_pane_visible((editor_ui_pane)pane);
    }
    ImGuiContext *previous = ImGui_GetCurrentContext();
    ImGuiContext *context = ImGui_CreateContext(NULL);
    editor_ui_initialize(false);
    ImGuiIO *io = ImGui_GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io->DisplaySize = (ImVec2){ 1200, 800 };
    io->DisplayFramebufferScale = (ImVec2){ 1, 1 };
    io->DeltaTime = 1.0f / 60.0f;
    rmlui_menu_fixture fixture = { .focus_native = true };
    active_fixture = &fixture;
    bool ok = context && ImFontAtlas_Build(io->Fonts);
    editor_ui_set_command_handler(menu_command, &fixture);

    bool file_actions = true;
    bool undo_redo = true;
    bool state_actions = true;
    bool input_routing = true;
    bool scale_resize = true;
    bool lifecycle = true;
    if (ok) {
        menu_frame();
        const struct
        {
            const char *item;
            editor_ui_command command;
        } file_commands[] = {
            { "workspace.quick_open", EDITOR_UI_COMMAND_QUICK_OPEN },
            { "file.save", EDITOR_UI_COMMAND_SAVE },
            { "file.save_as", EDITOR_UI_COMMAND_SAVE_AS },
            { "file.save_all", EDITOR_UI_COMMAND_SAVE_ALL },
            { "file.reload", EDITOR_UI_COMMAND_RELOAD },
        };
        for (int i = 0; i < (int)(sizeof file_commands / sizeof *file_commands); i++) {
            file_actions &= click_command(
                "file.menu",
                file_commands[i].item,
                file_commands[i].command,
                &fixture
            );
        }

        int before_disabled = fixture.command_count;
        editor_ui_set_command_handler(NULL, NULL);
        menu_key(ImGuiKey_Escape);
        bool file_open = click_item("file.menu");
        bool save_visible = item_center("file.save", &(ImVec2){ 0 });
        bool save_clicked = click_item("file.save");
        bool save_disabled = fixture.command_count == before_disabled;
        dismiss_menu();
        editor_ui_set_command_handler(menu_command, &fixture);
        file_actions &= file_open && save_visible && save_clicked && save_disabled;

        editor_ui_set_history_available(false, false);
        dismiss_menu();
        bool edit_open = click_item("edit.menu");
        bool undo_visible = item_center("edit.undo", &(ImVec2){ 0 });
        bool redo_visible = item_center("edit.redo", &(ImVec2){ 0 });
        int before_history = fixture.command_count;
        bool undo_disabled_clicked = click_item("edit.undo");
        bool redo_disabled_clicked = click_item("edit.redo");
        bool history_disabled = fixture.command_count == before_history;
        dismiss_menu();
        editor_ui_set_history_available(true, true);
        int before_undo = fixture.command_count;
        bool undo_enabled = click_command("edit.menu", "edit.undo", EDITOR_UI_COMMAND_UNDO, &fixture);
        bool redo_enabled = click_command("edit.menu", "edit.redo", EDITOR_UI_COMMAND_REDO, &fixture);
        undo_redo = edit_open && undo_visible && redo_visible &&
                    undo_disabled_clicked && redo_disabled_clicked &&
                    history_disabled && undo_enabled && redo_enabled &&
                    command_is(&fixture, before_undo, EDITOR_UI_COMMAND_UNDO);

        state_actions &= toggle_pane(EDITOR_UI_PANE_INSPECTOR, "view.inspector");
        state_actions &= toggle_pane(EDITOR_UI_PANE_NODE_CANVAS, "view.node_canvas");
        state_actions &= toggle_pane(EDITOR_UI_PANE_SCENE, "view.scene");
        state_actions &= toggle_pane(EDITOR_UI_PANE_COMPILER_RESULTS, "view.compiler_results");
        state_actions &= toggle_pane(EDITOR_UI_PANE_OUTPUT_LOG, "view.output_log");
        state_actions &= toggle_pane(EDITOR_UI_PANE_PERFORMANCE, "view.performance");

        dismiss_menu();
        bool view_open = click_item("view.menu");
        bool navigator_visible = item_center("view.navigator", &(ImVec2){ 0 });
        bool nav_before = editor_ui_navigator_visible();
        bool navigator_clicked = click_item("view.navigator");
        bool nav_toggled = editor_ui_navigator_visible() != nav_before;
        dismiss_menu();
        if (nav_toggled) {
            (void)click_item("view.menu");
            (void)click_item("view.navigator");
            dismiss_menu();
        }
        bool nav_restored = editor_ui_navigator_visible() == nav_before;
        state_actions &= view_open && navigator_visible && navigator_clicked &&
                         nav_toggled && nav_restored;

        bool inline_before = editor_ui_inline_edge_nodes();
        dismiss_menu();
        view_open = click_item("view.menu");
        bool inline_visible = item_center("view.inline_edge_nodes", &(ImVec2){ 0 });
        bool inline_clicked = click_item("view.inline_edge_nodes");
        bool inline_toggled = editor_ui_inline_edge_nodes() != inline_before;
        dismiss_menu();
        editor_ui_set_inline_edge_nodes(inline_before);
        state_actions &= view_open && inline_visible && inline_clicked && inline_toggled;

        bool motion_before = editor_ui_reduced_motion();
        dismiss_menu();
        view_open = click_item("view.menu");
        bool motion_visible = item_center("view.reduced_motion", &(ImVec2){ 0 });
        bool motion_clicked = click_item("view.reduced_motion");
        bool motion_toggled = editor_ui_reduced_motion() != motion_before;
        dismiss_menu();
        editor_ui_set_reduced_motion(motion_before);
        state_actions &= view_open && motion_visible && motion_clicked && motion_toggled;

        bool focus_before = editor_ui_focus_mode();
        editor_ui_set_focus_mode(true);
        menu_key(ImGuiKey_Escape);
        view_open = click_item("view.menu");
        bool reset_layout_visible = item_center("view.reset_layout", &(ImVec2){ 0 });
        bool reset_layout_clicked = click_item("view.reset_layout");
        bool focus_reset = !editor_ui_focus_mode();
        state_actions &= view_open && reset_layout_visible &&
                         reset_layout_clicked && focus_reset;
        editor_ui_set_focus_mode(focus_before);

        bool graph_before = editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS);
        bool scene_before = editor_ui_pane_visible(EDITOR_UI_PANE_SCENE);
        editor_ui_set_pane_visible(EDITOR_UI_PANE_NODE_CANVAS, false);
        editor_ui_set_pane_visible(EDITOR_UI_PANE_SCENE, false);
        dismiss_menu();
        view_open = click_item("view.menu");
        bool graph_scene_visible = item_center("view.graph_and_scene", &(ImVec2){ 0 });
        bool graph_scene_clicked = click_item("view.graph_and_scene");
        bool graph_scene_restored = editor_ui_pane_visible(EDITOR_UI_PANE_NODE_CANVAS) &&
                                    editor_ui_pane_visible(EDITOR_UI_PANE_SCENE);
        state_actions &= view_open && graph_scene_visible &&
                         graph_scene_clicked && graph_scene_restored;
        editor_ui_set_pane_visible(EDITOR_UI_PANE_NODE_CANVAS, graph_before);
        editor_ui_set_pane_visible(EDITOR_UI_PANE_SCENE, scene_before);

        dismiss_menu();
        bool outside_opened = click_item("file.menu");
        int before_outside = fixture.command_count;
        ImGuiIO_AddMousePosEvent(io, 700, 600);
        menu_frame();
        ImGuiIO_AddMouseButtonEvent(io, 0, true);
        menu_frame();
        ImGuiIO_AddMouseButtonEvent(io, 0, false);
        menu_frame();
        bool outside_closed = !item_center("file.save", &(ImVec2){ 0 });
        bool outside_no_action = fixture.command_count == before_outside;
        input_routing &= outside_opened && outside_closed && outside_no_action;

        dismiss_menu();
        fixture.focus_native = true;
        menu_frame();
        menu_frame();
        bool native_focus_before = fixture.native_focused;
        bool mouse_opened = click_item("file.menu");
        int native_before = fixture.native_presses;
        bool keyboard_captured = editor_ui_captures_keyboard();
        menu_key(ImGuiKey_Space);
        bool native_suppressed = fixture.native_presses == native_before;
        menu_key(ImGuiKey_DownArrow);
        bool popup_reopened = item_center("file.save", &(ImVec2){ 0 });
        menu_key(ImGuiKey_Escape);
        bool first_escape_keeps_bar_focus = editor_ui_captures_keyboard();
        menu_key(ImGuiKey_Escape);
        bool keyboard_released = !editor_ui_captures_keyboard();
        bool native_focus_retained = fixture.native_focused;
        menu_key(ImGuiKey_Space);
        bool native_released = fixture.native_presses > native_before;

        int before_keyboard_command = fixture.command_count;
        menu_key(ImGuiKey_F10);
        bool f10_open = editor_ui_captures_keyboard();
        menu_key(ImGuiKey_DownArrow);
        menu_key(ImGuiKey_DownArrow);
        menu_key(ImGuiKey_Enter);
        bool keyboard_command = command_is(&fixture, before_keyboard_command, EDITOR_UI_COMMAND_SAVE);
        dismiss_menu();
        editor_ui_set_history_available(false, true);
        int before_disabled_skip = fixture.command_count;
        menu_key(ImGuiKey_F10);
        menu_key(ImGuiKey_RightArrow);
        menu_key(ImGuiKey_DownArrow);
        menu_key(ImGuiKey_Enter);
        bool disabled_skip = command_is(&fixture, before_disabled_skip, EDITOR_UI_COMMAND_REDO);
        dismiss_menu();
        editor_ui_set_history_available(true, true);
        input_routing &= mouse_opened && native_focus_before &&
                         keyboard_captured && native_suppressed && popup_reopened &&
                         first_escape_keeps_bar_focus && keyboard_released &&
                         native_focus_retained && native_released && f10_open &&
                         keyboard_command && disabled_skip;

        io->DisplayFramebufferScale = (ImVec2){ 2, 2 };
        editor_ui_set_framebuffer_target(true);
        editor_ui_set_framebuffer_size(1600, 1200);
        menu_frame();
        editor_ui_rect scaled;
        bool scaled_rect = editor_ui_item_rect("file.menu", &scaled) &&
                           scaled.x >= 0 && scaled.y >= 0 && scaled.width > 0 &&
                           scaled.height > 0 && scaled.x + scaled.width <= 800.5f &&
                           scaled.y + scaled.height <= 600.5f;
        io->DisplayFramebufferScale = (ImVec2){ 1, 1 };
        editor_ui_set_framebuffer_target(false);
        editor_ui_set_framebuffer_size(900, 620);
        menu_frame();
        editor_ui_rect resized;
        bool resized_rect = editor_ui_item_rect("file.menu", &resized) &&
                            resized.width > 0 && resized.height > 0 &&
                            resized.x + resized.width <= 900.5f &&
                            resized.y + resized.height <= 620.5f;
        scale_resize &= scaled_rect && resized_rect;

        editor_ui_deactivate();
        ImGui_DestroyContext(context);
        context = NULL;
        ImGui_SetCurrentContext(previous);
        active_fixture = NULL;

        for (int i = 0; i < 3; i++) {
            context = ImGui_CreateContext(NULL);
            if (!context) {
                lifecycle = false;
                break;
            }
            editor_ui_initialize(false);
            io = ImGui_GetIO();
            io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io->DisplaySize = (ImVec2){ 720 + i * 40, 480 + i * 20 };
            io->DisplayFramebufferScale = (ImVec2){ 1, 1 };
            io->DeltaTime = 1.0f / 60.0f;
            fixture.focus_native = false;
            active_fixture = &fixture;
            lifecycle &= context && ImFontAtlas_Build(io->Fonts);
            if (context) {
                menu_frame();
                editor_ui_rect rect;
                lifecycle &= editor_ui_item_rect("file.menu", &rect) &&
                             rect.width > 0 && rect.height > 0 &&
                             rect.x + rect.width <= io->DisplaySize.x + 0.5f &&
                             rect.y + rect.height <= io->DisplaySize.y + 0.5f;
                editor_ui_deactivate();
                ImGui_DestroyContext(context);
                context = NULL;
            }
        }
        ImGui_SetCurrentContext(previous);
        active_fixture = NULL;
        ok &= file_actions && undo_redo && state_actions && input_routing &&
              scale_resize && lifecycle;
    }

    if (context) {
        editor_ui_deactivate();
        ImGui_DestroyContext(context);
    }
    ImGui_SetCurrentContext(previous);
    for (int pane = 0; pane < EDITOR_UI_PANE_COUNT; pane++) {
        editor_ui_set_pane_visible((editor_ui_pane)pane, panes_before[pane]);
    }
    printf(
        "rmlui-menu %s: file=%d undo/redo=%d view-state=%d keyboard=%d "
        "scale/resize=%d lifecycle=%d\n",
        ok ? "PASS" : "FAIL",
        file_actions,
        undo_redo,
        state_actions,
        input_routing,
        scale_resize,
        lifecycle
    );
    return ok;
}
