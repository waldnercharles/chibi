//------------------------------------------------------------------------------
// ne_canvas.c — C23 port of imgui_canvas.cpp (imgui-node-editor, Michal Cichon).
//
// Faithful translation of ImGuiEx::Canvas. Version-gated branches from upstream
// are resolved to the pinned Dear ImGui (1.92.7):
//   - IsClippedEx(bb, id)      (upstream > 18415 branch)
//   - SetNextItemAllowOverlap() in Begin(); the < 18967 SetItemAllowOverlap()
//     call in End() is compiled out upstream and dropped here.
//   - viewport->WorkPos/WorkSize (upstream > 18002 branch)
//------------------------------------------------------------------------------
#include "ne_canvas.h"

#include <string.h> /* memmove */

/* upstream ImDrawCallback_ImCanvas sentinel */
#define NE_IMDRAW_CALLBACK_IMCANVAS ((ImDrawCallback)(-2))

/* upstream ImSelectPositive(lhs, rhs) */
static ImVec2 ne_select_positive(ImVec2 lhs, ImVec2 rhs)
{
    return (ImVec2){ lhs.x > 0.0f ? lhs.x : rhs.x, lhs.y > 0.0f ? lhs.y : rhs.y };
}

/* upstream ImDrawList::CmdBuffer.erase(Data + index) */
static void ne_cmd_buffer_erase(ImVector_ImDrawCmd *buf, int index)
{
    if (index + 1 < buf->Size)
        memmove(
            buf->Data + index,
            buf->Data + index + 1,
            (size_t)(buf->Size - index - 1) * sizeof(ImDrawCmd)
        );
    buf->Size--;
}

/* Internal helpers */
static void ne_canvas_update_view_transform_position(ne_canvas *canvas);
static void ne_canvas_save_input_state(ne_canvas *canvas);
static void ne_canvas_restore_input_state(ne_canvas *canvas);
static void ne_canvas_save_viewport_state(ne_canvas *canvas);
static void ne_canvas_restore_viewport_state(ne_canvas *canvas);
static void ne_canvas_enter_local_space(ne_canvas *canvas);
static void ne_canvas_leave_local_space(ne_canvas *canvas);

//------------------------------------------------------------------------------

bool ne_canvas_begin(ne_canvas *canvas, const char *id, ImVec2 size)
{ return ne_canvas_begin_id(canvas, ImGui_GetID(id), size); }

bool ne_canvas_begin_id(ne_canvas *canvas, ImGuiID id, ImVec2 size)
{
    IM_ASSERT(canvas->m_InBeginEnd == false);

    canvas->m_WidgetPosition = ImGui_GetCursorScreenPos();
    canvas->m_WidgetSize = ne_select_positive(size, ImGui_GetContentRegionAvail());
    canvas->m_WidgetRect = (ImRect){
        canvas->m_WidgetPosition,
        ne_vec2_add(canvas->m_WidgetPosition, canvas->m_WidgetSize)
    };
    canvas->m_DrawList = ImGui_GetWindowDrawList();

    ne_canvas_update_view_transform_position(canvas);

    if (ImGui_IsClippedEx(canvas->m_WidgetRect, id)) return false;

    /* Save current channel, so we can assert when user calls canvas API with a
       different one. */
    canvas->m_ExpectedChannel = canvas->m_DrawList->_Splitter._Current;

    ImGui_SetCursorScreenPos((ImVec2){ 0.0f, 0.0f });

    ne_canvas_save_input_state(canvas);
    ne_canvas_save_viewport_state(canvas);

    /* Record cursor max to prevent scrollbars from appearing. */
    canvas->m_WindowCursorMaxBackup = ImGui_GetCurrentWindow()->DC.CursorMaxPos;

    ne_canvas_enter_local_space(canvas);

    ImGui_SetNextItemAllowOverlap();

    /* Emit dummy widget matching bounds of the canvas. */
    ImGui_SetCursorScreenPos(canvas->m_ViewRect.Min);
    ImGui_Dummy(ne_vec2_sub(canvas->m_ViewRect.Max, canvas->m_ViewRect.Min));

    ImGui_SetCursorScreenPos((ImVec2){ 0.0f, 0.0f });

    canvas->m_InBeginEnd = true;

    return true;
}

void ne_canvas_end(ne_canvas *canvas)
{
    /* If you're here your call to Begin() returned false, or Begin() wasn't
       called at all. */
    IM_ASSERT(canvas->m_InBeginEnd == true);

    /* If you're here, please make sure you do not interleave channel splitter
       with canvas. Always call canvas function with using same channel. */
    IM_ASSERT(canvas->m_DrawList->_Splitter._Current == canvas->m_ExpectedChannel);

    /* Check: Unmatched calls to Suspend() / Resume(). Please check your code. */
    IM_ASSERT(canvas->m_SuspendCounter == 0);

    ne_canvas_leave_local_space(canvas);

    ImGui_GetCurrentWindow()->DC.CursorMaxPos = canvas->m_WindowCursorMaxBackup;

    /* Emit dummy widget matching bounds of the canvas. */
    ImGui_SetCursorScreenPos(canvas->m_WidgetPosition);
    ImGui_Dummy(canvas->m_WidgetSize);

    canvas->m_InBeginEnd = false;
}

void ne_canvas_set_view(ne_canvas *canvas, ImVec2 origin, float scale)
{ ne_canvas_set_view_view(canvas, ne_canvas_view_make(origin, scale)); }

void ne_canvas_set_view_view(ne_canvas *canvas, ne_canvas_view view)
{
    if (canvas->m_InBeginEnd) ne_canvas_leave_local_space(canvas);

    if (canvas->m_View.Origin.x != view.Origin.x ||
        canvas->m_View.Origin.y != view.Origin.y) {
        canvas->m_View.Origin = view.Origin;

        ne_canvas_update_view_transform_position(canvas);
    }

    if (canvas->m_View.Scale != view.Scale) {
        canvas->m_View.Scale = view.Scale;
        canvas->m_View.InvScale = view.InvScale;
    }

    if (canvas->m_InBeginEnd) ne_canvas_enter_local_space(canvas);
}

void ne_canvas_center_view(ne_canvas *canvas, ImVec2 canvas_point)
{
    ne_canvas_set_view_view(canvas, ne_canvas_calc_center_view(canvas, canvas_point));
}

ne_canvas_view ne_canvas_calc_center_view(ne_canvas *canvas, ImVec2 canvas_point)
{
    const ImVec2 local_center = ne_canvas_to_local(
        canvas,
        ne_vec2_add(canvas->m_WidgetPosition, ne_vec2_scale(canvas->m_WidgetSize, 0.5f))
    );
    const ImVec2 local_offset = ne_vec2_sub(canvas_point, local_center);
    const ImVec2 offset = ne_canvas_from_local_v(canvas, local_offset);

    return ne_canvas_view_make(
        ne_vec2_sub(canvas->m_View.Origin, offset),
        canvas->m_View.Scale
    );
}

void ne_canvas_center_view_rect(ne_canvas *canvas, ImRect canvas_rect)
{
    ne_canvas_set_view_view(canvas, ne_canvas_calc_center_view_rect(canvas, canvas_rect));
}

ne_canvas_view ne_canvas_calc_center_view_rect(ne_canvas *canvas, ImRect canvas_rect)
{
    const ImVec2 canvas_rect_size = ne_vec2_sub(canvas_rect.Max, canvas_rect.Min);

    if (canvas_rect_size.x <= 0.0f || canvas_rect_size.y <= 0.0f)
        return canvas->m_View;

    const float widget_aspect_ratio = canvas->m_WidgetSize.y > 0.0f
                                        ? canvas->m_WidgetSize.x /
                                              canvas->m_WidgetSize.y
                                        : 0.0f;
    const float canvas_rect_aspect_ratio = canvas_rect_size.y > 0.0f
                                             ? canvas_rect_size.x /
                                                   canvas_rect_size.y
                                             : 0.0f;

    if (widget_aspect_ratio <= 0.0f || canvas_rect_aspect_ratio <= 0.0f)
        return canvas->m_View;

    ImVec2 new_origin = canvas->m_View.Origin;
    float new_scale = canvas->m_View.Scale;
    if (canvas_rect_aspect_ratio > widget_aspect_ratio) {
        /* width span across view */
        new_scale = canvas->m_WidgetSize.x / canvas_rect_size.x;
        new_origin = ne_vec2_scale(canvas_rect.Min, -new_scale);
        new_origin.y += (canvas->m_WidgetSize.y - canvas_rect_size.y * new_scale) * 0.5f;
    } else {
        /* height span across view */
        new_scale = canvas->m_WidgetSize.y / canvas_rect_size.y;
        new_origin = ne_vec2_scale(canvas_rect.Min, -new_scale);
        new_origin.x += (canvas->m_WidgetSize.x - canvas_rect_size.x * new_scale) * 0.5f;
    }

    return ne_canvas_view_make(new_origin, new_scale);
}

void ne_canvas_suspend(ne_canvas *canvas)
{
    IM_ASSERT(canvas->m_DrawList->_Splitter._Current == canvas->m_ExpectedChannel);

    if (canvas->m_SuspendCounter == 0) ne_canvas_leave_local_space(canvas);

    ++canvas->m_SuspendCounter;
}

void ne_canvas_resume(ne_canvas *canvas)
{
    IM_ASSERT(canvas->m_DrawList->_Splitter._Current == canvas->m_ExpectedChannel);

    /* Check: Number of calls to Resume() do not match calls to Suspend(). */
    IM_ASSERT(canvas->m_SuspendCounter > 0);
    if (--canvas->m_SuspendCounter == 0) ne_canvas_enter_local_space(canvas);
}

ImVec2 ne_canvas_from_local(ne_canvas *canvas, ImVec2 point)
{
    return ne_vec2_add(ne_vec2_scale(point, canvas->m_View.Scale), canvas->m_ViewTransformPosition);
}

ImVec2 ne_canvas_from_local_view(ne_canvas *canvas, ImVec2 point, ne_canvas_view view)
{
    return ne_vec2_add(
        ne_vec2_add(ne_vec2_scale(point, view.Scale), view.Origin),
        canvas->m_WidgetPosition
    );
}

ImVec2 ne_canvas_from_local_v(ne_canvas *canvas, ImVec2 vector)
{ return ne_vec2_scale(vector, canvas->m_View.Scale); }

ImVec2 ne_canvas_from_local_v_view(ne_canvas *canvas, ImVec2 vector, ne_canvas_view view)
{
    (void)canvas;
    return ne_vec2_scale(vector, view.Scale);
}

ImVec2 ne_canvas_to_local(ne_canvas *canvas, ImVec2 point)
{
    return ne_vec2_scale(
        ne_vec2_sub(point, canvas->m_ViewTransformPosition),
        canvas->m_View.InvScale
    );
}

ImVec2 ne_canvas_to_local_view(ne_canvas *canvas, ImVec2 point, ne_canvas_view view)
{
    return ne_vec2_scale(
        ne_vec2_sub(ne_vec2_sub(point, view.Origin), canvas->m_WidgetPosition),
        view.InvScale
    );
}

ImVec2 ne_canvas_to_local_v(ne_canvas *canvas, ImVec2 vector)
{ return ne_vec2_scale(vector, canvas->m_View.InvScale); }

ImVec2 ne_canvas_to_local_v_view(ne_canvas *canvas, ImVec2 vector, ne_canvas_view view)
{
    (void)canvas;
    return ne_vec2_scale(vector, view.InvScale);
}

ImRect ne_canvas_rect(const ne_canvas *canvas) { return canvas->m_WidgetRect; }

ImRect ne_canvas_view_rect(const ne_canvas *canvas)
{ return canvas->m_ViewRect; }

ImRect ne_canvas_calc_view_rect(const ne_canvas *canvas, ne_canvas_view view)
{
    ImRect result;
    result.Min = ne_vec2_scale((ImVec2){ -view.Origin.x, -view.Origin.y }, view.InvScale);
    result.Max = ne_vec2_scale(ne_vec2_sub(canvas->m_WidgetSize, view.Origin), view.InvScale);
    return result;
}

ne_canvas_view ne_canvas_get_view(const ne_canvas *canvas)
{ return canvas->m_View; }

ImVec2 ne_canvas_view_origin(const ne_canvas *canvas)
{ return canvas->m_View.Origin; }

float ne_canvas_view_scale(const ne_canvas *canvas)
{ return canvas->m_View.Scale; }

bool ne_canvas_is_suspended(const ne_canvas *canvas)
{ return canvas->m_SuspendCounter > 0; }

//------------------------------------------------------------------------------

static void ne_canvas_update_view_transform_position(ne_canvas *canvas)
{
    canvas->m_ViewTransformPosition = ne_vec2_add(canvas->m_View.Origin, canvas->m_WidgetPosition);
}

static void ne_canvas_save_input_state(ne_canvas *canvas)
{
    ImGuiIO *io = ImGui_GetIO();
    canvas->m_MousePosBackup = io->MousePos;
    canvas->m_MousePosPrevBackup = io->MousePosPrev;
    for (int i = 0; i < 5; ++i) /* IM_ARRAYSIZE(ImGuiIO::MouseClickedPos) */
        canvas->m_MouseClickedPosBackup[i] = io->MouseClickedPos[i];
}

static void ne_canvas_restore_input_state(ne_canvas *canvas)
{
    ImGuiIO *io = ImGui_GetIO();
    io->MousePos = canvas->m_MousePosBackup;
    io->MousePosPrev = canvas->m_MousePosPrevBackup;
    for (int i = 0; i < 5; ++i)
        io->MouseClickedPos[i] = canvas->m_MouseClickedPosBackup[i];
}

static void ne_canvas_save_viewport_state(ne_canvas *canvas)
{
#if defined(IMGUI_HAS_VIEWPORT)
    ImGuiWindow *window = ImGui_GetCurrentWindow();
    ImGuiViewport *viewport = ImGui_GetWindowViewport();

    canvas->m_WindowPosBackup = window->Pos;
    canvas->m_ViewportPosBackup = viewport->Pos;
    canvas->m_ViewportSizeBackup = viewport->Size;
    canvas->m_ViewportWorkPosBackup = viewport->WorkPos;
    canvas->m_ViewportWorkSizeBackup = viewport->WorkSize;
#endif
}

static void ne_canvas_restore_viewport_state(ne_canvas *canvas)
{
#if defined(IMGUI_HAS_VIEWPORT)
    ImGuiWindow *window = ImGui_GetCurrentWindow();
    ImGuiViewport *viewport = ImGui_GetWindowViewport();

    window->Pos = canvas->m_WindowPosBackup;
    viewport->Pos = canvas->m_ViewportPosBackup;
    viewport->Size = canvas->m_ViewportSizeBackup;
    viewport->WorkPos = canvas->m_ViewportWorkPosBackup;
    viewport->WorkSize = canvas->m_ViewportWorkSizeBackup;
#endif
}

static void ne_canvas_enter_local_space(ne_canvas *canvas)
{
    /* Prepare ImDrawList for drawing in local coordinate system:
       - determine visible part of the canvas
       - start unique draw command
       - add clip rect matching canvas size
       - record current command index
       - record current vertex write index */

    ImGui_PushClipRect(
        canvas->m_WidgetPosition,
        ne_vec2_add(canvas->m_WidgetPosition, canvas->m_WidgetSize),
        true
    );
    ImVec4 clipped_clip_rect = canvas->m_DrawList->_ClipRectStack
                                   .Data[canvas->m_DrawList->_ClipRectStack.Size - 1];
    ImGui_PopClipRect();

    canvas->m_DrawListCommadBufferSize = canvas->m_DrawList->CmdBuffer.Size > 0
                                           ? canvas->m_DrawList->CmdBuffer.Size
                                           : 0;
    /* upstream: _VtxCurrentIdx + ImVtxOffsetRef(); for 1.92.7 the SFINAE
       resolves to _CmdHeader.VtxOffset. */
    canvas->m_DrawListStartVertexIndex = (int)(canvas->m_DrawList->_VtxCurrentIdx +
                                               canvas->m_DrawList->_CmdHeader.VtxOffset);

    /* Make sure we do not share draw command with anyone. */
    if ((canvas->m_DrawList->CmdBuffer.Size != 0 &&
         canvas->m_DrawList->CmdBuffer
                 .Data[canvas->m_DrawList->CmdBuffer.Size - 1]
                 .ElemCount > 0) ||
        canvas->m_DrawList->_Splitter._Count > 1)
        ImDrawList_AddCallback(canvas->m_DrawList, NE_IMDRAW_CALLBACK_IMCANVAS, NULL);

    canvas->m_DrawListFirstCommandIndex = canvas->m_DrawList->CmdBuffer.Size - 1 > 0
                                            ? canvas->m_DrawList->CmdBuffer.Size - 1
                                            : 0;

#if defined(IMGUI_HAS_VIEWPORT)
    ImGuiWindow *window = ImGui_GetCurrentWindow();
    window->Pos = (ImVec2){ 0.0f, 0.0f };

    ImVec2 viewport_min = canvas->m_ViewportPosBackup;
    ImVec2 viewport_max = ne_vec2_add(canvas->m_ViewportPosBackup, canvas->m_ViewportSizeBackup);

    viewport_min.x = (viewport_min.x - canvas->m_ViewTransformPosition.x) *
                     canvas->m_View.InvScale;
    viewport_min.y = (viewport_min.y - canvas->m_ViewTransformPosition.y) *
                     canvas->m_View.InvScale;
    viewport_max.x = (viewport_max.x - canvas->m_ViewTransformPosition.x) *
                     canvas->m_View.InvScale;
    viewport_max.y = (viewport_max.y - canvas->m_ViewTransformPosition.y) *
                     canvas->m_View.InvScale;

    ImGuiViewport *viewport = ImGui_GetWindowViewport();
    viewport->Pos = viewport_min;
    viewport->Size = ne_vec2_sub(viewport_max, viewport_min);

    viewport->WorkPos = ne_vec2_scale(
        canvas->m_ViewportWorkPosBackup,
        canvas->m_View.InvScale
    );
    viewport->WorkSize = ne_vec2_scale(
        canvas->m_ViewportWorkSizeBackup,
        canvas->m_View.InvScale
    );
#endif

    /* Clip rectangle in parent canvas space and move it to local space. */
    clipped_clip_rect.x = (clipped_clip_rect.x - canvas->m_ViewTransformPosition.x) *
                          canvas->m_View.InvScale;
    clipped_clip_rect.y = (clipped_clip_rect.y - canvas->m_ViewTransformPosition.y) *
                          canvas->m_View.InvScale;
    clipped_clip_rect.z = (clipped_clip_rect.z - canvas->m_ViewTransformPosition.x) *
                          canvas->m_View.InvScale;
    clipped_clip_rect.w = (clipped_clip_rect.w - canvas->m_ViewTransformPosition.y) *
                          canvas->m_View.InvScale;
    ImGui_PushClipRect(
        (ImVec2){ clipped_clip_rect.x, clipped_clip_rect.y },
        (ImVec2){ clipped_clip_rect.z, clipped_clip_rect.w },
        false
    );

    /* Transform mouse position to local space. */
    ImGuiIO *io = ImGui_GetIO();
    io->MousePos = ne_vec2_scale(
        ne_vec2_sub(canvas->m_MousePosBackup, canvas->m_ViewTransformPosition),
        canvas->m_View.InvScale
    );
    io->MousePosPrev = ne_vec2_scale(
        ne_vec2_sub(canvas->m_MousePosPrevBackup, canvas->m_ViewTransformPosition),
        canvas->m_View.InvScale
    );
    for (int i = 0; i < 5; ++i)
        io->MouseClickedPos[i] = ne_vec2_scale(
            ne_vec2_sub(canvas->m_MouseClickedPosBackup[i], canvas->m_ViewTransformPosition),
            canvas->m_View.InvScale
        );

    canvas->m_ViewRect = ne_canvas_calc_view_rect(canvas, canvas->m_View);

    canvas->m_LastFringeScale = canvas->m_DrawList->_FringeScale;
    canvas->m_DrawList->_FringeScale *= canvas->m_View.InvScale;
}

static void ne_canvas_leave_local_space(ne_canvas *canvas)
{
    IM_ASSERT(canvas->m_DrawList->_Splitter._Current == canvas->m_ExpectedChannel);

    /* Move vertices to screen space. */
    ImDrawVert *vertex = canvas->m_DrawList->VtxBuffer.Data +
                         canvas->m_DrawListStartVertexIndex;
    ImDrawVert *vertexEnd = canvas->m_DrawList->VtxBuffer.Data +
                            (canvas->m_DrawList->_VtxCurrentIdx +
                             canvas->m_DrawList->_CmdHeader.VtxOffset);

    /* If canvas view is not scaled take a faster path. */
    if (canvas->m_View.Scale != 1.0f) {
        while (vertex < vertexEnd) {
            vertex->pos.x = vertex->pos.x * canvas->m_View.Scale +
                            canvas->m_ViewTransformPosition.x;
            vertex->pos.y = vertex->pos.y * canvas->m_View.Scale +
                            canvas->m_ViewTransformPosition.y;
            ++vertex;
        }

        /* Move clip rectangles to screen space. */
        for (int i = canvas->m_DrawListFirstCommandIndex;
             i < canvas->m_DrawList->CmdBuffer.Size;
             ++i) {
            ImDrawCmd *command = &canvas->m_DrawList->CmdBuffer.Data[i];
            command->ClipRect.x = command->ClipRect.x * canvas->m_View.Scale +
                                  canvas->m_ViewTransformPosition.x;
            command->ClipRect.y = command->ClipRect.y * canvas->m_View.Scale +
                                  canvas->m_ViewTransformPosition.y;
            command->ClipRect.z = command->ClipRect.z * canvas->m_View.Scale +
                                  canvas->m_ViewTransformPosition.x;
            command->ClipRect.w = command->ClipRect.w * canvas->m_View.Scale +
                                  canvas->m_ViewTransformPosition.y;
        }
    } else {
        while (vertex < vertexEnd) {
            vertex->pos.x = vertex->pos.x + canvas->m_ViewTransformPosition.x;
            vertex->pos.y = vertex->pos.y + canvas->m_ViewTransformPosition.y;
            ++vertex;
        }

        /* Move clip rectangles to screen space. */
        for (int i = canvas->m_DrawListFirstCommandIndex;
             i < canvas->m_DrawList->CmdBuffer.Size;
             ++i) {
            ImDrawCmd *command = &canvas->m_DrawList->CmdBuffer.Data[i];
            command->ClipRect.x = command->ClipRect.x +
                                  canvas->m_ViewTransformPosition.x;
            command->ClipRect.y = command->ClipRect.y +
                                  canvas->m_ViewTransformPosition.y;
            command->ClipRect.z = command->ClipRect.z +
                                  canvas->m_ViewTransformPosition.x;
            command->ClipRect.w = command->ClipRect.w +
                                  canvas->m_ViewTransformPosition.y;
        }
    }

    /* Remove sentinel draw command if present. */
    if (canvas->m_DrawListCommadBufferSize > 0) {
        if (canvas->m_DrawList->CmdBuffer.Size > canvas->m_DrawListCommadBufferSize &&
            canvas->m_DrawList->CmdBuffer
                    .Data[canvas->m_DrawListCommadBufferSize]
                    .UserCallback == NE_IMDRAW_CALLBACK_IMCANVAS)
            ne_cmd_buffer_erase(&canvas->m_DrawList->CmdBuffer, canvas->m_DrawListCommadBufferSize);
        else if (
            canvas->m_DrawList->CmdBuffer.Size >= canvas->m_DrawListCommadBufferSize &&
            canvas->m_DrawList->CmdBuffer
                    .Data[canvas->m_DrawListCommadBufferSize - 1]
                    .UserCallback == NE_IMDRAW_CALLBACK_IMCANVAS
        )
            ne_cmd_buffer_erase(&canvas->m_DrawList->CmdBuffer, canvas->m_DrawListCommadBufferSize - 1);
    }

    canvas->m_DrawList->_FringeScale = canvas->m_LastFringeScale;

    /* And pop \o/ */
    ImGui_PopClipRect();

    ne_canvas_restore_input_state(canvas);
    ne_canvas_restore_viewport_state(canvas);
}
