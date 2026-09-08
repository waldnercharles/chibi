#include "editor/workspace/editor.h"

#include <imgui/dcimgui.h>
#include <stdio.h>

static void gameplay_keyboard_frame(bool edit_text)
{
    static char text[32];
    ImGui_NewFrame();
    ImGui_SetNextWindowPos((ImVec2){ 20, 20 }, ImGuiCond_Always);
    ImGui_SetNextWindowSize((ImVec2){ 400, 240 }, ImGuiCond_Always);
    ImGui_Begin("Scene input", NULL, ImGuiWindowFlags_NoSavedSettings);
    ImGui_SetWindowFocus();
    ImGui_Button("Play");
    if (edit_text) {
        ImGui_SetKeyboardFocusHere();
    }
    ImGui_InputText("Name", text, sizeof text, 0);
    ImGui_End();
    ImGui_Render();
}

bool test_gameplay_keyboard()
{
    ImGuiContext *previous = ImGui_GetCurrentContext();
    ImGuiContext *context = ImGui_CreateContext(NULL);
    ImGuiIO *io = ImGui_GetIO();
    io->DisplaySize = (ImVec2){ 640, 480 };
    io->DeltaTime = 1.0f / 60;
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    bool ok = ImFontAtlas_Build(io->Fonts);
    gameplay_keyboard_frame(false);
    gameplay_keyboard_frame(false);
    ImGuiKey keys[] = { ImGuiKey_W, ImGuiKey_A, ImGuiKey_S, ImGuiKey_D };
    for (int i = 0; i < 4; i++) {
        ImGuiIO_AddKeyEvent(io, keys[i], true);
        gameplay_keyboard_frame(false);
        /* This is the same gate editor/workspace/application.c uses before reading CF keys. */
        ok &= io->WantCaptureKeyboard && io->NavActive && !io->WantTextInput &&
              !editor_host_captures_keyboard();
        ImGuiIO_AddKeyEvent(io, keys[i], false);
        gameplay_keyboard_frame(false);
    }
    gameplay_keyboard_frame(true);
    gameplay_keyboard_frame(false);
    ImGuiIO_AddKeyEvent(io, ImGuiKey_W, true);
    gameplay_keyboard_frame(false);
    ok &= io->WantTextInput && editor_host_captures_keyboard();
    ImGuiIO_AddKeyEvent(io, ImGuiKey_W, false);
    gameplay_keyboard_frame(false);
    ImGui_DestroyContext(context);
    ImGui_SetCurrentContext(previous);
    printf("gameplay keyboard %s: navigation permits WASD; text editing captures it\n", ok ? "PASS" : "FAIL");
    return ok;
}
