//------------------------------------------------------------------------------
// ne_canvas.h — C23 port of imgui_canvas.h (imgui-node-editor, Michal Cichon).
//
// Canvas widget: a view over infinite virtual space with pan/zoom navigation.
// The upstream C++ class ImGuiEx::Canvas becomes the C struct ne_canvas with
// method-style free functions (ne_canvas_begin, ne_canvas_to_local, ...).
// Member names keep the upstream m_ prefix so a diff stays mechanical.
//------------------------------------------------------------------------------
#ifndef NE_CANVAS_H
#define NE_CANVAS_H

#include "ne_math.h"

#include <dcimgui.h>
/* dcimgui_internal.h uses IMGUI_STB_NAMESPACE::stbrp_node (C++ '::'); we don't
   touch that type, so undef the namespace to get the plain-C branch. */
#undef IMGUI_STB_NAMESPACE
#include <dcimgui_internal.h> /* ImRect, ImGuiWindow, ImGui_IsClippedEx */

typedef struct ne_canvas_view
{
    ImVec2 Origin;
    float Scale;
    float InvScale;
} ne_canvas_view;

/* upstream CanvasView(origin, scale) */
static inline ne_canvas_view ne_canvas_view_make(ImVec2 origin, float scale)
{
    ne_canvas_view view;
    view.Origin = origin;
    view.Scale = scale;
    view.InvScale = scale ? 1.0f / scale : 0.0f;
    return view;
}

/* upstream CanvasView::Set(origin, scale) */
static inline void ne_canvas_view_set(ne_canvas_view *view, ImVec2 origin, float scale)
{ *view = ne_canvas_view_make(origin, scale); }

typedef struct ne_canvas
{
    bool m_InBeginEnd;

    ImVec2 m_WidgetPosition;
    ImVec2 m_WidgetSize;
    ImRect m_WidgetRect;

    ImDrawList *m_DrawList;
    int m_ExpectedChannel;

    int m_DrawListFirstCommandIndex;
    int m_DrawListCommadBufferSize;
    int m_DrawListStartVertexIndex;

    ne_canvas_view m_View;
    ImRect m_ViewRect;

    ImVec2 m_ViewTransformPosition;

    int m_SuspendCounter;

    float m_LastFringeScale;

    ImVec2 m_MousePosBackup;
    ImVec2 m_MousePosPrevBackup;
    ImVec2 m_MouseClickedPosBackup[5]; /* IM_ARRAYSIZE(ImGuiIO::MouseClickedPos) */
    ImVec2 m_WindowCursorMaxBackup;

#if defined(IMGUI_HAS_VIEWPORT)
    ImVec2 m_WindowPosBackup;
    ImVec2 m_ViewportPosBackup;
    ImVec2 m_ViewportSizeBackup;
    ImVec2 m_ViewportWorkPosBackup;
    ImVec2 m_ViewportWorkSizeBackup;
#endif
} ne_canvas;

/* Begin/End */
bool ne_canvas_begin(ne_canvas *canvas, const char *id, ImVec2 size);
bool ne_canvas_begin_id(ne_canvas *canvas, ImGuiID id, ImVec2 size);
void ne_canvas_end(ne_canvas *canvas);

/* View */
void ne_canvas_set_view(ne_canvas *canvas, ImVec2 origin, float scale);
void ne_canvas_set_view_view(ne_canvas *canvas, ne_canvas_view view);
void ne_canvas_center_view(ne_canvas *canvas, ImVec2 canvas_point);
ne_canvas_view ne_canvas_calc_center_view(ne_canvas *canvas, ImVec2 canvas_point);
void ne_canvas_center_view_rect(ne_canvas *canvas, ImRect canvas_rect);
ne_canvas_view ne_canvas_calc_center_view_rect(ne_canvas *canvas, ImRect canvas_rect);

/* Suspend/Resume */
void ne_canvas_suspend(ne_canvas *canvas);
void ne_canvas_resume(ne_canvas *canvas);

/* World <-> screen transforms */
ImVec2 ne_canvas_from_local(ne_canvas *canvas, ImVec2 point);
ImVec2 ne_canvas_from_local_view(ne_canvas *canvas, ImVec2 point, ne_canvas_view view);
ImVec2 ne_canvas_from_local_v(ne_canvas *canvas, ImVec2 vector);
ImVec2 ne_canvas_from_local_v_view(ne_canvas *canvas, ImVec2 vector, ne_canvas_view view);
ImVec2 ne_canvas_to_local(ne_canvas *canvas, ImVec2 point);
ImVec2 ne_canvas_to_local_view(ne_canvas *canvas, ImVec2 point, ne_canvas_view view);
ImVec2 ne_canvas_to_local_v(ne_canvas *canvas, ImVec2 vector);
ImVec2 ne_canvas_to_local_v_view(ne_canvas *canvas, ImVec2 vector, ne_canvas_view view);

/* Accessors */
ImRect ne_canvas_rect(const ne_canvas *canvas);
ImRect ne_canvas_view_rect(const ne_canvas *canvas);
ImRect ne_canvas_calc_view_rect(const ne_canvas *canvas, ne_canvas_view view);
ne_canvas_view ne_canvas_get_view(const ne_canvas *canvas);
ImVec2 ne_canvas_view_origin(const ne_canvas *canvas);
float ne_canvas_view_scale(const ne_canvas *canvas);
bool ne_canvas_is_suspended(const ne_canvas *canvas);

#endif /* NE_CANVAS_H */
