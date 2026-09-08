#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor.h"

#include <imgui/dcimgui.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    int presses[3];
    bool focused[3];
    bool active[3];
    ImVec2 centers[3];
    bool inherited_disabled;
    bool disabled_interacted;
    bool show_header;
    bool header_open;
    ImVec2 header_center;
} keyboard_fixture;

static void keyboard_frame(keyboard_fixture *test, bool focus_first)
{
    ImGui_NewFrame();
    ImGui_SetNextWindowPos((ImVec2){ 20, 20 }, ImGuiCond_Always);
    ImGui_SetNextWindowSize((ImVec2){ 240, 120 }, ImGuiCond_Always);
    ImGui_Begin("Keyboard icons", NULL, ImGuiWindowFlags_NoSavedSettings);
    if (focus_first) {
        ImGui_SetWindowFocus();
        ImGui_SetKeyboardFocusHere();
    }
    const char *ids[] = { "first", "disabled", "last" };
    for (int i = 0; i < 3; i++) {
        if (i) {
            ImGui_SameLine();
        }
        ImGui_BeginDisabled(i == 0 && test->inherited_disabled);
        test->presses[i] += editor_ui_icon_button(ids[i], EDITOR_ICON_FA_PLAY, i != 1);
        test->focused[i] = ImGui_IsItemFocused();
        test->active[i] = ImGui_IsItemActive();
        ImVec2 min = ImGui_GetItemRectMin(), max = ImGui_GetItemRectMax();
        test->centers[i] = (ImVec2){ (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f };
        if (i == 1) {
            test->disabled_interacted |= test->focused[i] || test->active[i];
        }
        ImGui_EndDisabled();
    }
    if (test->show_header) {
        ImGui_NewLine();
        ImVec2 at = ImGui_GetCursorScreenPos();
        test->header_center = (ImVec2){ at.x + 45, at.y + 14 };
        bool removed = false;
        test->header_open = editor_ui_component_header("Component", "keyboard.header", &removed);
        if (test->header_open) {
            ImGui_TreePop();
        }
    }
    ImGui_End();
    ImGui_Render();
}

static void keyboard_key(keyboard_fixture *test, ImGuiKey key)
{
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), key, true);
    keyboard_frame(test, false);
    ImGuiIO_AddKeyEvent(ImGui_GetIO(), key, false);
    keyboard_frame(test, false);
    keyboard_frame(test, false);
}

static void keyboard_click(keyboard_fixture *test, int index)
{
    ImVec2 center = test->centers[index];
    ImGuiIO_AddMousePosEvent(ImGui_GetIO(), center.x, center.y);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, true);
    keyboard_frame(test, false);
    ImGuiIO_AddMouseButtonEvent(ImGui_GetIO(), 0, false);
    keyboard_frame(test, false);
}

static float opacity_frame(bool visible)
{
    ImGui_NewFrame();
    ImGui_Begin("Motion timing", NULL, ImGuiWindowFlags_NoSavedSettings);
    float opacity = editor_ui_controls_opacity("controls", visible);
    ImGui_End();
    ImGui_Render();
    return opacity;
}

static bool test_controls_opacity()
{
    ImGuiIO *io = ImGui_GetIO();
    float previous_dt = io->DeltaTime;
    io->DeltaTime = 0.01f;
    bool ok = true;
    for (int i = 0; i < 60; i++) {
        ok &= opacity_frame(false) == 0;
    }
    /* A slow frame before hover/focus must not consume the new reveal. */
    io->DeltaTime = 0.5f;
    ok &= opacity_frame(true) == 0;
    io->DeltaTime = 0.01f;
    float first = opacity_frame(true);
    float second = opacity_frame(true);
    ok &= first > 0 && second > first && second < 0.2f;
    float reversed = opacity_frame(false);
    ok &= reversed == second;
    float fading = opacity_frame(false);
    ok &= fading > 0 && fading < reversed;
    float resumed = opacity_frame(true);
    ok &= resumed == fading;
    for (int i = 0; i < 10; i++) {
        resumed = opacity_frame(true);
    }
    /* At half duration the toolbar must still be visibly between states. */
    ok &= resumed > 0.2f && resumed < 0.8f;
    for (int i = 0; i < 11; i++) {
        resumed = opacity_frame(true);
    }
    ok &= resumed == 1 && opacity_frame(false) == 1;
    float hidden = 1;
    for (int i = 0; i < 21; i++) {
        hidden = opacity_frame(false);
    }
    ok &= hidden == 0;
    editor_ui_set_reduced_motion(true);
    ok &= opacity_frame(true) == 0;
    float reduced = opacity_frame(true);
    ok &= reduced > 0 && reduced < 1;
    editor_ui_set_reduced_motion(false);
    ImGui_NewFrame();
    ImGui_Render();
    ok &= opacity_frame(true) == 0;
    io->DeltaTime = previous_dt;
    printf(
        "ui-controls-motion %s: hover/focus fades over 200ms, retargets "
        "current alpha, and never consumes a pre-reveal frame\n",
        ok ? "PASS" : "FAIL"
    );
    return ok;
}

typedef struct
{
    ImVec2 trigger;
    char text[16];
    bool active;
    bool open;
    float alpha;
} popup_fixture;

static void popup_frame(popup_fixture *test)
{
    ImGui_NewFrame();
    ImGui_SetNextWindowPos((ImVec2){ 300, 30 }, ImGuiCond_Always);
    ImGui_SetNextWindowSize((ImVec2){ 250, 180 }, ImGuiCond_Always);
    ImGui_Begin("Popup motion", NULL, ImGuiWindowFlags_NoSavedSettings);
    if (ImGui_Button("Manage")) {
        editor_ui_open_popup("entry");
    }
    ImVec2 min = ImGui_GetItemRectMin(), max = ImGui_GetItemRectMax();
    test->trigger = (ImVec2){ (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f };
    test->open = editor_ui_begin_popup("entry");
    test->active = false;
    if (test->open) {
        test->alpha = ImGui_GetStyle()->Alpha;
        if (ImGui_IsWindowAppearing()) {
            ImGui_SetKeyboardFocusHere();
        }
        ImGui_InputText("Search", test->text, sizeof test->text, 0);
        test->active = ImGui_IsItemActive();
        editor_ui_end_popup();
    }
    ImGui_End();
    ImGui_Render();
}

static bool test_popup_entry()
{
    ImGuiIO *io = ImGui_GetIO();
    float previous_dt = io->DeltaTime;
    io->DeltaTime = 0.01f;
    popup_fixture test = { 0 };
    popup_frame(&test);
    popup_frame(&test);
    ImGuiIO_AddMousePosEvent(io, test.trigger.x, test.trigger.y);
    ImGuiIO_AddMouseButtonEvent(io, 0, true);
    popup_frame(&test);
    ImGuiIO_AddMouseButtonEvent(io, 0, false);
    popup_frame(&test);
    popup_frame(&test);
    popup_frame(&test);
    bool focus = test.open && test.active && test.alpha > 0 && test.alpha < 1;
    ImGuiIO_AddInputCharactersUTF8(io, "q");
    popup_frame(&test);
    printf(test.text, test.open, test.active, test.alpha);
    bool ok = focus && strcmp(test.text, "q") == 0 && test.alpha < 1;
    ImGuiIO_AddKeyEvent(io, ImGuiKey_Tab, true);
    popup_frame(&test);
    ok &= test.alpha == 1;
    ImGuiIO_AddKeyEvent(io, ImGuiKey_Tab, false);
    popup_frame(&test);
    ImGuiIO_AddKeyEvent(io, ImGuiKey_Escape, true);
    popup_frame(&test);
    ImGuiIO_AddKeyEvent(io, ImGuiKey_Escape, false);
    popup_frame(&test);
    io->DeltaTime = previous_dt;
    printf(
        "ui-popup-motion %s: pointer entry accepts text before fade finishes; "
        "keyboard navigation snaps opaque\n",
        ok ? "PASS" : "FAIL"
    );
    return ok;
}

static bool test_sidebar_glyphs()
{
    ImGui_NewFrame();
    ImFontBaked *font = ImFont_GetFontBaked(ImGui_GetFont(), ImGui_GetFontSize());
    const ImWchar glyphs[] = { EDITOR_ICON_CODICON_CHEVRON_DOWN,
                               EDITOR_ICON_CODICON_CHEVRON_RIGHT,
                               EDITOR_ICON_CODICON_CHEVRON_UP,
                               EDITOR_ICON_CODICON_CHEVRON_LEFT,
                               EDITOR_ICON_MD_ARROW_TOP_RIGHT };
    bool ok = true;
    for (int i = 0; i < (int)(sizeof glyphs / sizeof *glyphs); i++) {
        ok &= ImFontBaked_FindGlyphNoFallback(font, glyphs[i]) != NULL;
    }
    ImGui_Render();
    printf("ui-sidebar-glyphs %s: Codicons and Material icons resolve in the merged font\n", ok ? "PASS" : "FAIL");
    return ok;
}

bool test_ui_keyboard()
{
    ImGuiContext *previous = ImGui_GetCurrentContext();
    ImGuiContext *context = ImGui_CreateContext(NULL);
    editor_ui_initialize(false);
    ImGuiIO *io = ImGui_GetIO();
    io->DisplaySize = (ImVec2){ 640, 480 };
    io->DeltaTime = 1.0f / 60.0f;
    editor *editor = editor_new(behavior_asset_new("Keyboard fixture"));
    bool ok = editor && ImFontAtlas_Build(io->Fonts);
    if (ok) {
        keyboard_fixture test = { 0 };
        keyboard_frame(&test, false);
        keyboard_frame(&test, false);
        keyboard_key(&test, ImGuiKey_Tab);
        bool focused = test.focused[0] && io->NavVisible;
        keyboard_key(&test, ImGuiKey_Enter);
        bool enter = test.presses[0] == 1;
        keyboard_key(&test, ImGuiKey_Tab);
        bool tab = test.focused[2] && !test.focused[1];
        keyboard_key(&test, ImGuiKey_Space);
        bool space = test.presses[2] == 1;
        keyboard_click(&test, 1);
        bool disabled = !test.presses[1] && !test.active[1] && !test.disabled_interacted;
        test.inherited_disabled = true;
        keyboard_frame(&test, false);
        keyboard_click(&test, 0);
        bool inherited = test.presses[0] == 1 && !test.active[0];
        keyboard_frame(&test, true);
        keyboard_key(&test, ImGuiKey_Enter);
        inherited &= test.presses[0] == 1 && !test.active[0] && !test.focused[0];
        test.show_header = true;
        keyboard_frame(&test, false);
        ImGuiIO_AddMousePosEvent(io, test.header_center.x, test.header_center.y);
        ImGuiIO_AddMouseButtonEvent(io, 0, true);
        keyboard_frame(&test, false);
        ImGuiIO_AddMouseButtonEvent(io, 0, false);
        keyboard_frame(&test, false);
        bool disclosure = !test.header_open;
        keyboard_key(&test, ImGuiKey_RightArrow);
        disclosure &= test.header_open;
        keyboard_key(&test, ImGuiKey_LeftArrow);
        disclosure &= !test.header_open;
        printf(
            "ui-keyboard-disclosure %s: mouse collapse, Right expand, Left "
            "collapse retain header focus\n",
            disclosure ? "PASS" : "FAIL"
        );
        ok = focused && enter && tab && space && disabled && inherited && disclosure;
        printf(
            "ui-keyboard %s: initial_focus=%d Enter=%d Tab_skips_disabled=%d "
            "Space=%d disabled_mouse=%d inherited_disabled=%d\n",
            ok ? "PASS" : "FAIL",
            focused,
            enter,
            tab,
            space,
            disabled,
            inherited
        );
    }
    if (ok) {
        bool motion = test_controls_opacity();
        bool popup = test_popup_entry();
        bool glyphs = test_sidebar_glyphs();
        ok = motion && popup && glyphs;
    }
    editor_free(editor);
    ImGui_DestroyContext(context);
    ImGui_SetCurrentContext(previous);
    return ok;
}
