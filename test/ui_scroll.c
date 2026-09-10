#include <imgui/dcimgui.h>
#include <math.h>
#include <stdio.h>

enum
{
    SCROLL_WINDOW_WIDTH = 320,
    SCROLL_WINDOW_HEIGHT = 160,
    SCROLL_ROWS = 80,
};

typedef struct
{
    float marker_y;
    float scroll_target;
} scroll_sample;

static void scroll_frame(float delta_time, float wheel_y, scroll_sample *sample)
{
    ImGuiIO *io = ImGui_GetIO();
    io->DeltaTime = delta_time;
    if (wheel_y != 0.0f) {
        ImGuiIO_AddMouseWheelEvent(io, 0.0f, wheel_y);
    }
    ImGui_NewFrame();
    ImGui_SetNextWindowPos((ImVec2){ 20, 20 }, ImGuiCond_Always);
    ImGui_SetNextWindowSize((ImVec2){ SCROLL_WINDOW_WIDTH, SCROLL_WINDOW_HEIGHT }, ImGuiCond_Always);
    ImGui_Begin("Scroll speed", NULL, ImGuiWindowFlags_NoSavedSettings);
    ImGui_Text("marker");
    sample->marker_y = ImGui_GetItemRectMin().y;
    for (int row = 0; row < SCROLL_ROWS; row++) {
        ImGui_Text("row %d", row);
    }
    sample->scroll_target = ImGui_GetScrollY();
    ImGui_End();
    ImGui_Render();
}

static float scroll_after(float delta_time)
{
    ImGuiContext *context = ImGui_CreateContext(NULL);
    ImGuiIO *io = ImGui_GetIO();
    io->DisplaySize = (ImVec2){ 640, 480 };
    ImGui_GetStyle()->ScrollSmooth = 10.0f;
    ImFontAtlas_Build(io->Fonts);
    ImGuiIO_AddMousePosEvent(io, 100.0f, 100.0f);

    scroll_sample before = { 0 };
    scroll_sample after = { 0 };
    scroll_frame(delta_time, 0.0f, &before);
    scroll_frame(delta_time, 0.0f, &before);
    scroll_frame(delta_time, -1.0f, &after);
    int frames = (int)roundf(0.1f / delta_time) - 1;
    for (int frame = 0; frame < frames; frame++) {
        scroll_frame(delta_time, 0.0f, &after);
    }

    float moved = before.marker_y - after.marker_y;
    bool target_is_reasonable = after.scroll_target > 60.0f &&
                                after.scroll_target < 70.0f;
    bool moved_is_reasonable = moved > 0.0f && moved < after.scroll_target;
    float result = target_is_reasonable && moved_is_reasonable ? moved : -1.0f;
    ImGui_DestroyContext(context);
    return result;
}

bool test_ui_scroll_smoothing()
{
    float at_60hz = scroll_after(1.0f / 60.0f);
    float at_120hz = scroll_after(1.0f / 120.0f);
    bool ok = at_60hz >= 0.0f && at_120hz >= 0.0f && fabsf(at_60hz - at_120hz) < 2.0f;
    printf("ui-scroll-smoothing %s: 60Hz=%g 120Hz=%g\n", ok ? "PASS" : "FAIL", at_60hz, at_120hz);
    return ok;
}
