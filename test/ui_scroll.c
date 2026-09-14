#include <float.h>
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
    float grab_y;
    float grab_x;
} scroll_sample;

static void scroll_grab(ImDrawList *draw, scroll_sample *sample)
{
    ImU32 grab_color = ImGui_GetColorU32(ImGuiCol_ScrollbarGrab);
    for (int i = 0; i < draw->VtxBuffer.Size; i++) {
        ImDrawVert vertex = draw->VtxBuffer.Data[i];
        if (vertex.col == grab_color) {
            sample->grab_y = fminf(sample->grab_y, vertex.pos.y);
            sample->grab_x = vertex.pos.x;
        }
    }
}

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
    ImGui_BeginChild("content_tree", (ImVec2){ 0, 0 }, 0, 0);
    ImGui_Text("marker");
    sample->marker_y = ImGui_GetItemRectMin().y;
    ImGui_SetNextItemOpen(true, ImGuiCond_Always);
    if (ImGui_TreeNode("Expanded folder")) {
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_FramePadding, (ImVec2){ 6, 6 });
        for (int row = 0; row < SCROLL_ROWS; row++) {
            ImGui_PushIDInt(row);
            ImGui_TreeNodeEx(
                "Asset",
                ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                    ImGuiTreeNodeFlags_FramePadding
            );
            ImGui_PopID();
        }
        ImGui_PopStyleVar();
        ImGui_TreePop();
    }
    sample->scroll_target = ImGui_GetScrollY();
    sample->grab_y = FLT_MAX;
    scroll_grab(ImGui_GetWindowDrawList(), sample);
    ImGui_EndChild();
    scroll_grab(ImGui_GetWindowDrawList(), sample);
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
    for (int frame = 0; frame < 5; frame++) {
        scroll_frame(delta_time, 0.0f, &before);
    }
    scroll_frame(delta_time, -1.0f, &after);
    int frames = (int)roundf(0.1f / delta_time) - 1;
    for (int frame = 0; frame < frames; frame++) {
        scroll_frame(delta_time, 0.0f, &after);
    }

    float grab_moved = after.grab_y - before.grab_y;
    scroll_sample settled = { 0 };
    for (int frame = 0; frame < 180; frame++) {
        scroll_frame(delta_time, 0.0f, &settled);
    }
    float grab_total = settled.grab_y - before.grab_y;
    if (!(grab_moved > 0.0f && grab_moved < grab_total * 0.8f)) {
        printf("scrollbar jumped: animated=%g settled=%g\n", grab_moved, grab_total);
        ImGui_DestroyContext(context);
        return -1.0f;
    }
    float moved = before.marker_y - after.marker_y;
    bool target_is_reasonable = after.scroll_target > 12.0f &&
                                after.scroll_target < 14.0f;
    printf("expanded-tree wheel tick: %g pixels\n", after.scroll_target);
    bool moved_is_reasonable = moved > 0.0f && moved < after.scroll_target;
    float result = target_is_reasonable && moved_is_reasonable ? moved : -1.0f;
    ImGui_DestroyContext(context);
    return result;
}

static bool scroll_drag_and_reduced_motion()
{
    ImGuiContext *context = ImGui_CreateContext(NULL);
    ImGuiIO *io = ImGui_GetIO();
    io->DisplaySize = (ImVec2){ 640, 480 };
    ImGui_GetStyle()->ScrollSmooth = 10.0f;
    ImFontAtlas_Build(io->Fonts);
    ImGuiIO_AddMousePosEvent(io, 100.0f, 100.0f);
    scroll_sample before = { 0 };
    scroll_sample after = { 0 };
    for (int frame = 0; frame < 5; frame++) {
        scroll_frame(1.0f / 60.0f, 0.0f, &before);
    }
    scroll_frame(1.0f / 60.0f, -1.0f, &after);
    // Grab the visible thumb while wheel scrolling is still easing.
    ImGuiIO_AddMousePosEvent(io, after.grab_x, after.grab_y + 5.0f);
    ImGuiIO_AddMouseButtonEvent(io, 0, true);
    scroll_frame(1.0f / 60.0f, 0.0f, &after);
    ImGuiIO_AddMousePosEvent(io, io->MousePos.x, io->MousePos.y + 20.0f);
    scroll_frame(1.0f / 60.0f, 0.0f, &after);
    bool ok = after.scroll_target > 100.0f &&
              fabsf(before.marker_y - after.marker_y - after.scroll_target) < 1.0f;
    float dragged_y = after.marker_y;
    ImGuiIO_AddMouseButtonEvent(io, 0, false);
    scroll_frame(1.0f / 60.0f, 0.0f, &after);
    ok = ok && fabsf(after.marker_y - dragged_y) < 1.0f;
    ImGui_GetStyle()->ScrollSmooth = 1.0f;
    ImGuiIO_AddMousePosEvent(io, 100.0f, 100.0f);
    scroll_frame(1.0f / 60.0f, 0.0f, &after);
    scroll_frame(1.0f / 60.0f, -1.0f, &after);
    ok = ok && fabsf(before.marker_y - after.marker_y - after.scroll_target) < 1.0f;
    ImGui_DestroyContext(context);
    printf("ui-scroll-drag-and-reduced-motion %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

bool test_ui_scroll_smoothing()
{
    float at_60hz = scroll_after(1.0f / 60.0f);
    float at_120hz = scroll_after(1.0f / 120.0f);
    bool ok = at_60hz >= 0.0f && at_120hz >= 0.0f && fabsf(at_60hz - at_120hz) < 2.0f;
    printf("ui-scroll-smoothing %s: 60Hz=%g 120Hz=%g\n", ok ? "PASS" : "FAIL", at_60hz, at_120hz);
    return scroll_drag_and_reduced_motion() && ok;
}
