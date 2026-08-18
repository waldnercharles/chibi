//------------------------------------------------------------------------------
// ne.c — C23 port of imgui_node_editor.cpp + imgui_node_editor_internal.inl +
// imgui_node_editor_api.cpp (imgui-node-editor, Michal Cichon).
//
// Faithful translation of the editor core. C++ machinery becomes:
//   - class hierarchy / virtual methods  -> base-struct embedding + type tag +
//     dispatch functions (ne_object_*, ne_action_*).
//   - std::vector<T>                     -> ckit dynamic array (apush/apop/afree).
//   - std::string / crude_json           -> yyjson (Serialize/Parse) + snprintf.
//   - lambdas                            -> static functions + void* context.
//   - std::sort / std::find_if / ...     -> qsort / manual loops.
//   - ImGui::Foo                         -> ImGui_Foo (dcimgui).
//------------------------------------------------------------------------------
#include "ne_internal.h"
#include "third_party/ckit/ckit.h"
#include "third_party/yyjson/yyjson.h"

#include <float.h>  /* FLT_MAX */
#include <math.h>   /* fmodf, powf, sinf, fabsf, ceilf, floorf */
#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* malloc, free, strtoull, qsort */
#include <string.h> /* memcpy, memmove, memset, strlen */

//------------------------------------------------------------------------------
static const int c_background_channel_count = 1;
static const int c_link_channel_count = 4;
static const int c_user_layers_count = 5;

static const int c_user_layer_channel_start = 0;
static const int c_background_channel_start = c_user_layer_channel_start +
                                              c_user_layers_count;
static const int c_link_start_channel = c_background_channel_start +
                                        c_background_channel_count;
static const int c_node_start_channel = c_link_start_channel + c_link_channel_count;

static const int c_background_channel_selection_rect = c_background_channel_start + 0;

static const int c_user_channel_content = c_user_layer_channel_start + 1;
static const int c_user_channel_grid = c_user_layer_channel_start + 2;
static const int c_user_channel_hints_background = c_user_layer_channel_start + 3;
static const int c_user_channel_hints = c_user_layer_channel_start + 4;

static const int c_link_channel_selection = c_link_start_channel + 0;
static const int c_link_channel_links = c_link_start_channel + 1;
static const int c_link_channel_flow = c_link_start_channel + 2;
static const int c_link_channel_new_link = c_link_start_channel + 3;

static const int c_channels_per_node = 5;
static const int c_node_base_channel = 0;
static const int c_node_background_channel = 1;
static const int c_node_user_background_channel = 2;
static const int c_node_pin_channel = 3;
static const int c_node_content_channel = 4;

static const float c_group_select_thickness = 6.0f; /* canvas pixels */
static const float c_link_select_thickness = 5.0f;  /* canvas pixels */
static const float c_navigation_zoom_margin = 0.1f; /* percentage of visible bounds */
static const float c_mouse_zoom_duration = 0.15f;         /* seconds */
static const float c_selection_fade_out_duration = 0.15f; /* seconds */

static const float c_max_move_over_edge_speed = 10.0f;
static const float c_max_move_over_edge_distance = 300.0f;

#define NE_ALL_ROUND_CORNERS_FLAGS ImDrawFlags_RoundCornersAll

/* upstream ImDrawCallback_ImCanvas sentinel */
#define NE_IMDRAW_CALLBACK_IMCANVAS ((ImDrawCallback)(-2))

//------------------------------------------------------------------------------
// Small scalar math helpers (upstream ImMin/ImMax/ImClamp for floats, which the
// dcimgui C binding does not expose).
static float ne_imax_f(float a, float b) { return a > b ? a : b; }
static float ne_imin_f(float a, float b) { return a < b ? a : b; }
static float ne_iclamp_f(float v, float mn, float mx)
{ return v < mn ? mn : (v > mx ? mx : v); }

/* upstream ImEasing::EaseOutQuad */
static float ne_ease_out_quad_f(float b, float c, float t)
{ return b - c * (t * (t - 2)); }
static ImVec2 ne_ease_out_quad_v2(ImVec2 b, ImVec2 c, float t)
{ return ne_vec2_sub(b, ne_vec2_scale(c, t * (t - 2))); }

/* upstream FloorRect (IM_TRUNC) */
static void ne_floor_rect(ImRect *rect)
{
    rect->Min.x = IM_TRUNC(rect->Min.x);
    rect->Min.y = IM_TRUNC(rect->Min.y);
    rect->Max.x = IM_TRUNC(rect->Max.x);
    rect->Max.y = IM_TRUNC(rect->Max.y);
}

static bool ne_is_group(const ne_node *node)
{
    if (node && node->m_type == ne_node_type_group) return true;
    else return false;
}

/* upstream ed::Log — no-op unless _DEBUG && _WIN32 */
void ne_log(const char *fmt, ...) { (void)fmt; }

//------------------------------------------------------------------------------
// ImDrawListSplitter helpers (upstream ImDrawListSplitter_Grow/SwapChannels/
// SwapSplitter + ImDrawList_ChannelsGrow/SwapChannels/SwapSplitter).

static void ne_imvector_channel_reserve(ImVector_ImDrawChannel *v, int n)
{
    if (v->Capacity < n) {
        int nc = v->Capacity > 0 ? v->Capacity * 2 : 8;
        while (nc < n) nc *= 2;
        v->Data = (ImDrawChannel *)realloc(v->Data, (size_t)nc * sizeof(ImDrawChannel));
        v->Capacity = nc;
    }
}

static void ne_imvector_channel_resize(ImVector_ImDrawChannel *v, int n)
{
    ne_imvector_channel_reserve(v, n);
    for (int i = v->Size; i < n; ++i)
        memset(&v->Data[i], 0, sizeof(ImDrawChannel)); /* placement-new -> zero */
    v->Size = n;
}

static void ne_draw_list_splitter_grow(ImDrawList *draw_list, ImDrawListSplitter *splitter, int channels_count)
{
    IM_ASSERT(splitter != NULL);
    IM_ASSERT(splitter->_Count <= channels_count);

    if (splitter->_Count == 1) {
        ImDrawListSplitter_Split(splitter, draw_list, channels_count);
        return;
    }

    int old_channels_count = splitter->_Channels.Size;
    if (old_channels_count < channels_count) {
        ne_imvector_channel_reserve(&splitter->_Channels, channels_count);
        ne_imvector_channel_resize(&splitter->_Channels, channels_count);
    }
    int old_used_channels_count = splitter->_Count;
    splitter->_Count = channels_count;

    for (int i = old_used_channels_count; i < channels_count; i++) {
        if (i >= old_channels_count) {
            memset(&splitter->_Channels.Data[i], 0, sizeof(ImDrawChannel)); /* placement-new */
        } else {
            splitter->_Channels.Data[i]._CmdBuffer.Size = 0;
            splitter->_Channels.Data[i]._IdxBuffer.Size = 0;
        }
    }
}

static void ne_draw_list_channels_grow(ImDrawList *draw_list, int channels_count)
{
    ne_draw_list_splitter_grow(draw_list, &draw_list->_Splitter, channels_count);
}

static void ne_draw_list_splitter_swap_channels(ImDrawListSplitter *splitter, int left, int right)
{
    IM_ASSERT(left < splitter->_Count && right < splitter->_Count);
    if (left == right) return;

    int current_channel = splitter->_Current;

    ImDrawChannel tmp = splitter->_Channels.Data[left];
    splitter->_Channels.Data[left] = splitter->_Channels.Data[right];
    splitter->_Channels.Data[right] = tmp;

    if (current_channel == left) splitter->_Current = right;
    else if (current_channel == right) splitter->_Current = left;
}

static void ne_draw_list_swap_channels(ImDrawList *draw_list, int left, int right)
{ ne_draw_list_splitter_swap_channels(&draw_list->_Splitter, left, right); }

static void ne_draw_list_swap_splitter(ImDrawList *draw_list, ImDrawListSplitter *splitter)
{
    ImDrawListSplitter *current = &draw_list->_Splitter;

    int tmp = current->_Current;
    current->_Current = splitter->_Current;
    splitter->_Current = tmp;
    tmp = current->_Count;
    current->_Count = splitter->_Count;
    splitter->_Count = tmp;
    ImVector_ImDrawChannel tc = current->_Channels;
    current->_Channels = splitter->_Channels;
    splitter->_Channels = tc;
}

//------------------------------------------------------------------------------
// Bezier arrows (upstream ImDrawList_PathBezierOffset + ImDrawList_AddBezierWithArrows).

typedef struct ne_path_bezier_offset_ctx
{
    ImDrawList *draw_list;
    float offset;
} ne_path_bezier_offset_ctx;

static void ne_path_bezier_accept_point(const struct ne_cubic_bezier_subdivide_sample *r, void *user_pointer)
{
    ne_path_bezier_offset_ctx *ctx = (ne_path_bezier_offset_ctx *)user_pointer;
    ImVec2 n = ne_vec2_normalized((ImVec2){ -r->Tangent.y, r->Tangent.x });
    ImDrawList_PathLineTo(ctx->draw_list, ne_vec2_add(r->Point, ne_vec2_scale(n, ctx->offset)));
}

static void ne_draw_list_path_bezier_offset(
    ImDrawList *draw_list,
    float offset,
    ImVec2 p0,
    ImVec2 p1,
    ImVec2 p2,
    ImVec2 p3
)
{
    ne_path_bezier_offset_ctx ctx;
    ctx.draw_list = draw_list;
    ctx.offset = offset;
    ne_cubic_bezier_subdivide(ne_path_bezier_accept_point, &ctx, p0, p1, p2, p3, -1.0f, ne_cubic_bezier_subdivide_none);
}

static void ne_draw_list_add_bezier_with_arrows(
    ImDrawList *draw_list,
    struct ne_cubic_bezier_points curve,
    float thickness,
    float start_arrow_size,
    float start_arrow_width,
    float end_arrow_size,
    float end_arrow_width,
    bool fill,
    ImU32 color,
    float stroke_thickness,
    const ImVec2 *start_dir_hint,
    const ImVec2 *end_dir_hint
)
{
    if ((color >> 24) == 0) return;

    const float half_thickness = thickness * 0.5f;

    if (fill) {
        ImDrawList_AddBezierCubic(draw_list, curve.P0, curve.P1, curve.P2, curve.P3, color, thickness, 0);

        if (start_arrow_size > 0.0f) {
            ImVec2 start_dir = ne_vec2_normalized(
                start_dir_hint ? *start_dir_hint
                               : ne_cubic_bezier_tangent(
                                     curve.P0,
                                     curve.P1,
                                     curve.P2,
                                     curve.P3,
                                     0.0f
                                 )
            );
            ImVec2 start_n = (ImVec2){ -start_dir.y, start_dir.x };
            float half_width = start_arrow_width * 0.5f;
            ImVec2 tip = ne_vec2_sub(curve.P0, ne_vec2_scale(start_dir, start_arrow_size));

            ImDrawList_PathLineTo(
                draw_list,
                ne_vec2_sub(curve.P0, ne_vec2_scale(start_n, ne_imax_f(half_width, half_thickness)))
            );
            ImDrawList_PathLineTo(
                draw_list,
                ne_vec2_add(curve.P0, ne_vec2_scale(start_n, ne_imax_f(half_width, half_thickness)))
            );
            ImDrawList_PathLineTo(draw_list, tip);
            ImDrawList_PathFillConvex(draw_list, color);
        }

        if (end_arrow_size > 0.0f) {
            ImVec2 end_dir = ne_vec2_normalized(
                end_dir_hint ? ne_vec2_neg(*end_dir_hint)
                             : ne_cubic_bezier_tangent(
                                   curve.P0,
                                   curve.P1,
                                   curve.P2,
                                   curve.P3,
                                   1.0f
                               )
            );
            ImVec2 end_n = (ImVec2){ -end_dir.y, end_dir.x };
            float half_width = end_arrow_width * 0.5f;
            ImVec2 tip = ne_vec2_add(curve.P3, ne_vec2_scale(end_dir, end_arrow_size));

            ImDrawList_PathLineTo(
                draw_list,
                ne_vec2_add(curve.P3, ne_vec2_scale(end_n, ne_imax_f(half_width, half_thickness)))
            );
            ImDrawList_PathLineTo(
                draw_list,
                ne_vec2_sub(curve.P3, ne_vec2_scale(end_n, ne_imax_f(half_width, half_thickness)))
            );
            ImDrawList_PathLineTo(draw_list, tip);
            ImDrawList_PathFillConvex(draw_list, color);
        }
    } else {
        if (start_arrow_size > 0.0f) {
            ImVec2 start_dir = ne_vec2_normalized(
                ne_cubic_bezier_tangent(curve.P0, curve.P1, curve.P2, curve.P3, 0.0f)
            );
            ImVec2 start_n = (ImVec2){ -start_dir.y, start_dir.x };
            float half_width = start_arrow_width * 0.5f;
            ImVec2 tip = ne_vec2_sub(curve.P0, ne_vec2_scale(start_dir, start_arrow_size));

            if (half_width > half_thickness)
                ImDrawList_PathLineTo(
                    draw_list,
                    ne_vec2_sub(curve.P0, ne_vec2_scale(start_n, half_width))
                );
            ImDrawList_PathLineTo(draw_list, tip);
            if (half_width > half_thickness)
                ImDrawList_PathLineTo(
                    draw_list,
                    ne_vec2_add(curve.P0, ne_vec2_scale(start_n, half_width))
                );
        }

        ne_draw_list_path_bezier_offset(
            draw_list,
            half_thickness,
            curve.P0,
            curve.P1,
            curve.P2,
            curve.P3
        );

        if (end_arrow_size > 0.0f) {
            ImVec2 end_dir = ne_vec2_normalized(
                ne_cubic_bezier_tangent(curve.P0, curve.P1, curve.P2, curve.P3, 1.0f)
            );
            ImVec2 end_n = (ImVec2){ -end_dir.y, end_dir.x };
            float half_width = end_arrow_width * 0.5f;
            ImVec2 tip = ne_vec2_add(curve.P3, ne_vec2_scale(end_dir, end_arrow_size));

            if (half_width > half_thickness)
                ImDrawList_PathLineTo(draw_list, ne_vec2_add(curve.P3, ne_vec2_scale(end_n, half_width)));
            ImDrawList_PathLineTo(draw_list, tip);
            if (half_width > half_thickness)
                ImDrawList_PathLineTo(draw_list, ne_vec2_sub(curve.P3, ne_vec2_scale(end_n, half_width)));
        }

        ne_draw_list_path_bezier_offset(
            draw_list,
            half_thickness,
            curve.P3,
            curve.P2,
            curve.P1,
            curve.P0
        );

        ImDrawList_PathStroke(draw_list, color, ImDrawFlags_Closed, stroke_thickness);
    }
}

//------------------------------------------------------------------------------
// Object dispatch (upstream Object virtual methods).

ne_object_id ne_object_id_of(ne_object *object)
{
    switch (object->type) {
        case ne_object_type_pin:
            return (ne_object_id){ ((ne_pin *)object)->m_id, ne_object_type_pin };
        case ne_object_type_node:
            return (ne_object_id){ ((ne_node *)object)->m_id, ne_object_type_node };
        case ne_object_type_link:
            return (ne_object_id){ ((struct ne_link *)object)->m_id,
                                   ne_object_type_link };
        default: return (ne_object_id){ 0, ne_object_type_none };
    }
}

bool ne_object_is_visible(ne_object *object)
{
    if (!object->m_is_live) return false;

    ImRect bounds = ne_object_get_bounds(object);
    return ImGui_IsRectVisible(bounds.Min, bounds.Max);
}

void ne_object_reset(ne_object *object)
{
    if (object->type == ne_object_type_pin) {
        ne_pin *pin = (ne_pin *)object;
        pin->m_had_connection = pin->m_has_connection && pin->base.m_is_live;
        pin->m_has_connection = false;
    }
    object->m_is_live = false;
}

void ne_object_draw(ne_object *object, ImDrawList *draw_list, int flags)
{
    switch (object->type) {
        case ne_object_type_pin:
            ne_pin_draw((ne_pin *)object, draw_list, flags);
            break;
        case ne_object_type_node:
            ne_node_draw((ne_node *)object, draw_list, flags);
            break;
        case ne_object_type_link:
            ne_link_draw((struct ne_link *)object, draw_list, flags);
            break;
        default: break;
    }
}

bool ne_object_accept_drag(ne_object *object)
{
    switch (object->type) {
        case ne_object_type_node: return ne_node_accept_drag((ne_node *)object);
        case ne_object_type_link: return false;
        case ne_object_type_pin: return false;
        default: return false;
    }
}

void ne_object_update_drag(ne_object *object, ImVec2 offset)
{
    switch (object->type) {
        case ne_object_type_node:
            ne_node_update_drag((ne_node *)object, offset);
            break;
        default: (void)offset; break;
    }
}

bool ne_object_end_drag(ne_object *object)
{
    switch (object->type) {
        case ne_object_type_node: return ne_node_end_drag((ne_node *)object);
        default: return false;
    }
}

ImVec2 ne_object_drag_start_location(ne_object *object)
{
    switch (object->type) {
        case ne_object_type_node: return ((ne_node *)object)->m_drag_start;
        default: return ne_object_get_bounds(object).Min;
    }
}

bool ne_object_is_draggable(ne_object *object)
{
    bool result = ne_object_accept_drag(object);
    ne_object_end_drag(object);
    return result;
}

bool ne_object_is_selectable(ne_object *object)
{
    switch (object->type) {
        case ne_object_type_node: return true;
        case ne_object_type_link: return true;
        default: return false;
    }
}

bool ne_object_test_hit(ne_object *object, ImVec2 point, float extra_thickness)
{
    if (!object->m_is_live) return false;

    ImRect bounds = ne_object_get_bounds(object);
    if (extra_thickness > 0) ImRect_Expand(&bounds, extra_thickness);

    return ne_rect_contains(bounds, point);
}

bool ne_object_test_hit_rect(ne_object *object, ImRect rect, bool allow_intersect)
{
    if (!object->m_is_live) return false;

    ImRect bounds = ne_object_get_bounds(object);

    return !ne_rect_is_empty(bounds) &&
           (allow_intersect ? ImRect_Overlaps(&bounds, rect)
                            : ImRect_ContainsImRect(&rect, bounds));
}

ImRect ne_object_get_bounds(ne_object *object)
{
    switch (object->type) {
        case ne_object_type_pin: return ((ne_pin *)object)->m_bounds;
        case ne_object_type_node: return ((ne_node *)object)->m_bounds;
        case ne_object_type_link:
            return ne_link_get_bounds((struct ne_link *)object);
        default: return (ImRect){ { 0, 0 }, { 0, 0 } };
    }
}

ne_node *ne_object_as_node(ne_object *object)
{
    return object && object->type == ne_object_type_node ? (ne_node *)object : NULL;
}

ne_pin *ne_object_as_pin(ne_object *object)
{
    return object && object->type == ne_object_type_pin ? (ne_pin *)object : NULL;
}

struct ne_link *ne_object_as_link(ne_object *object)
{
    return object && object->type == ne_object_type_link ? (struct ne_link *)object
                                                         : NULL;
}

//------------------------------------------------------------------------------
// Pin (upstream Pin::Draw/GetClosestPoint/GetClosestLine).

void ne_pin_draw(ne_pin *pin, ImDrawList *draw_list, int flags)
{
    if (flags & ne_draw_flags_hovered) {
        ImDrawList_ChannelsSetCurrent(draw_list, pin->m_node->m_channel + c_node_pin_channel);

        ImDrawList_AddRectFilledEx(
            draw_list,
            pin->m_bounds.Min,
            pin->m_bounds.Max,
            pin->m_color,
            pin->m_rounding,
            pin->m_corners
        );

        if (pin->m_border_width > 0.0f) {
            float last_fringe = draw_list->_FringeScale;
            draw_list->_FringeScale = 1.0f; /* FringeScaleScope */
            ImDrawList_AddRectEx(
                draw_list,
                pin->m_bounds.Min,
                pin->m_bounds.Max,
                pin->m_border_color,
                pin->m_rounding,
                pin->m_corners,
                pin->m_border_width
            );
            draw_list->_FringeScale = last_fringe;
        }

        if (!ne_editor_context_is_selected(pin->base.editor, (ne_object *)pin->m_node))
            ne_node_draw(pin->m_node, draw_list, flags);
    }
}

ImVec2 ne_pin_get_closest_point(const ne_pin *pin, ImVec2 p)
{
    ImRect pivot = pin->m_pivot;
    float extent = pin->m_radius + pin->m_arrow_size;

    if (pin->m_snap_link_to_dir && extent > 0.0f) {
        pivot.Min = ne_vec2_add(pivot.Min, ne_vec2_scale(pin->m_dir, extent));
        pivot.Max = ne_vec2_add(pivot.Max, ne_vec2_scale(pin->m_dir, extent));

        extent = 0;
    }

    return ne_rect_closest_point_radius(pivot, p, true, extent);
}

ne_line ne_pin_get_closest_line(const ne_pin *pin, const ne_pin *other)
{
    ImRect pivot_a = pin->m_pivot;
    ImRect pivot_b = other->m_pivot;
    float extent_a = pin->m_radius + pin->m_arrow_size;
    float extent_b = other->m_radius + other->m_arrow_size;

    if (pin->m_snap_link_to_dir && extent_a > 0.0f) {
        pivot_a.Min = ne_vec2_add(pivot_a.Min, ne_vec2_scale(pin->m_dir, extent_a));
        pivot_a.Max = ne_vec2_add(pivot_a.Max, ne_vec2_scale(pin->m_dir, extent_a));
        extent_a = 0;
    }

    if (other->m_snap_link_to_dir && extent_b > 0.0f) {
        pivot_b.Min = ne_vec2_add(pivot_b.Min, ne_vec2_scale(other->m_dir, extent_b));
        pivot_b.Max = ne_vec2_add(pivot_b.Max, ne_vec2_scale(other->m_dir, extent_b));
        extent_b = 0;
    }

    return ne_rect_closest_line_radius(pivot_a, pivot_b, extent_a, extent_b);
}

//------------------------------------------------------------------------------
// Node (upstream Node::AcceptDrag/UpdateDrag/EndDrag/Draw/DrawBorder/GetGroupedNodes/GetRegionBounds/GetRegion).

bool ne_node_accept_drag(ne_node *node)
{
    node->m_drag_start = node->m_bounds.Min;
    return true;
}

void ne_node_update_drag(ne_node *node, ImVec2 offset)
{
    ImVec2 size = ImRect_GetSize(&node->m_bounds);
    node->m_bounds.Min = cImFloorImVec2(ne_vec2_add(node->m_drag_start, offset));
    node->m_bounds.Max = ne_vec2_add(node->m_bounds.Min, size);
}

bool ne_node_end_drag(ne_node *node)
{ return ne_vec2_ne(node->m_bounds.Min, node->m_drag_start); }

void ne_node_draw(ne_node *node, ImDrawList *draw_list, int flags)
{
    if (flags == ne_draw_flags_none) {
        ImDrawList_ChannelsSetCurrent(draw_list, node->m_channel + c_node_background_channel);

        ImDrawList_AddRectFilledEx(
            draw_list,
            node->m_bounds.Min,
            node->m_bounds.Max,
            node->m_color,
            node->m_rounding,
            0
        );

        if (ne_is_group(node)) {
            ImDrawList_AddRectFilledEx(
                draw_list,
                node->m_group_bounds.Min,
                node->m_group_bounds.Max,
                node->m_group_color,
                node->m_group_rounding,
                0
            );

            if (node->m_group_border_width > 0.0f) {
                float last_fringe = draw_list->_FringeScale;
                draw_list->_FringeScale = 1.0f; /* FringeScaleScope */

                ImDrawList_AddRectEx(
                    draw_list,
                    node->m_group_bounds.Min,
                    node->m_group_bounds.Max,
                    node->m_group_border_color,
                    node->m_group_rounding,
                    NE_ALL_ROUND_CORNERS_FLAGS,
                    node->m_group_border_width
                );

                draw_list->_FringeScale = last_fringe;
            }
        }

        ne_node_draw_border(node, draw_list, node->m_border_color, node->m_border_width, 0.0f);
    } else if (flags & ne_draw_flags_selected) {
        ImU32 border_color = ne_editor_context_get_color(node->base.editor, ne_style_color_sel_node_border);
        ne_style *editor_style = ne_get_style();

        ImDrawList_ChannelsSetCurrent(draw_list, node->m_channel + c_node_base_channel);

        ne_node_draw_border(
            node,
            draw_list,
            border_color,
            editor_style->selected_node_border_width,
            editor_style->selected_node_border_offset
        );
    } else if (!ne_is_group(node) && (flags & ne_draw_flags_hovered)) {
        ImU32 border_color = ne_editor_context_get_color(node->base.editor, ne_style_color_hov_node_border);
        ne_style *editor_style = ne_get_style();

        ImDrawList_ChannelsSetCurrent(draw_list, node->m_channel + c_node_base_channel);

        ne_node_draw_border(
            node,
            draw_list,
            border_color,
            editor_style->hovered_node_border_width,
            editor_style->hover_node_border_offset
        );
    }
}

void ne_node_draw_border(ne_node *node, ImDrawList *draw_list, ImU32 color, float thickness, float offset)
{
    if (thickness > 0.0f) {
        ImVec2 extra_offset = (ImVec2){ offset, offset };

        ImDrawList_AddRectEx(
            draw_list,
            ne_vec2_sub(node->m_bounds.Min, extra_offset),
            ne_vec2_add(node->m_bounds.Max, extra_offset),
            color,
            ne_imax_f(0.0f, node->m_rounding + offset),
            NE_ALL_ROUND_CORNERS_FLAGS,
            thickness
        );
    }
}

void ne_node_get_grouped_nodes(ne_node *node, ne_node ***result, bool append)
{
    if (!append) *result = NULL; /* result.resize(0) */

    if (!ne_is_group(node)) return;

    const int first_node_index = *result ? acount(*result) : 0;
    ne_editor_context_find_nodes_in_rect(node->base.editor, node->m_group_bounds, result, true, false);

    for (int index = first_node_index; index < acount(*result); ++index)
        ne_node_get_grouped_nodes((*result)[index], result, true);
}

ImRect ne_node_get_region_bounds(const ne_node *node, int region)
{
    if (node->m_type == ne_node_type_node) {
        if (region == ne_node_region_header) return node->m_bounds;
    } else if (node->m_type == ne_node_type_group) {
        const float active_area_minimum_size = ne_imax_f(
            ne_imax_f(
                ne_canvas_get_view(&node->base.editor->m_canvas).InvScale * c_group_select_thickness,
                node->m_group_border_width
            ),
            c_group_select_thickness
        );
        const float minimum_size = active_area_minimum_size * 5;

        ImRect bounds = node->m_bounds;
        if (ImRect_GetWidth(&bounds) < minimum_size)
            ImRect_ExpandImVec2(&bounds, (ImVec2){ minimum_size - ImRect_GetWidth(&bounds), 0.0f });
        if (ImRect_GetHeight(&bounds) < minimum_size)
            ImRect_ExpandImVec2(&bounds, (ImVec2){ 0.0f, minimum_size - ImRect_GetHeight(&bounds) });

        if (region == ne_node_region_top) {
            bounds.Max.y = bounds.Min.y + active_area_minimum_size;
            bounds.Min.x += active_area_minimum_size;
            bounds.Max.x -= active_area_minimum_size;
            return bounds;
        } else if (region == ne_node_region_bottom) {
            bounds.Min.y = bounds.Max.y - active_area_minimum_size;
            bounds.Min.x += active_area_minimum_size;
            bounds.Max.x -= active_area_minimum_size;
            return bounds;
        } else if (region == ne_node_region_left) {
            bounds.Max.x = bounds.Min.x + active_area_minimum_size;
            bounds.Min.y += active_area_minimum_size;
            bounds.Max.y -= active_area_minimum_size;
            return bounds;
        } else if (region == ne_node_region_right) {
            bounds.Min.x = bounds.Max.x - active_area_minimum_size;
            bounds.Min.y += active_area_minimum_size;
            bounds.Max.y -= active_area_minimum_size;
            return bounds;
        } else if (region == ne_node_region_top_left) {
            bounds.Max.x = bounds.Min.x + active_area_minimum_size * 2;
            bounds.Max.y = bounds.Min.y + active_area_minimum_size * 2;
            return bounds;
        } else if (region == ne_node_region_top_right) {
            bounds.Min.x = bounds.Max.x - active_area_minimum_size * 2;
            bounds.Max.y = bounds.Min.y + active_area_minimum_size * 2;
            return bounds;
        } else if (region == ne_node_region_bottom_right) {
            bounds.Min.x = bounds.Max.x - active_area_minimum_size * 2;
            bounds.Min.y = bounds.Max.y - active_area_minimum_size * 2;
            return bounds;
        } else if (region == ne_node_region_bottom_left) {
            bounds.Max.x = bounds.Min.x + active_area_minimum_size * 2;
            bounds.Min.y = bounds.Max.y - active_area_minimum_size * 2;
            return bounds;
        } else if (region == ne_node_region_header) {
            bounds.Min.x += active_area_minimum_size;
            bounds.Max.x -= active_area_minimum_size;
            bounds.Min.y += active_area_minimum_size;
            bounds.Max.y = ne_imax_f(
                bounds.Min.y + active_area_minimum_size,
                node->m_group_bounds.Min.y
            );
            return bounds;
        } else if (region == ne_node_region_center) {
            bounds.Max.x -= active_area_minimum_size;
            bounds.Min.y = ne_imax_f(
                bounds.Min.y + active_area_minimum_size,
                node->m_group_bounds.Min.y
            );
            bounds.Min.x += active_area_minimum_size;
            bounds.Max.y -= active_area_minimum_size;
            return bounds;
        }
    }

    return (ImRect){ { 0, 0 }, { 0, 0 } };
}

int ne_node_get_region(const ne_node *node, ImVec2 point)
{
    if (node->m_type == ne_node_type_node) {
        if (ne_rect_contains(node->m_bounds, point))
            return ne_node_region_header;
        else return ne_node_region_none;
    } else if (node->m_type == ne_node_type_group) {
        static const int c_regions[] = {
            ne_node_region_top_left,    ne_node_region_top_right,
            ne_node_region_bottom_left, ne_node_region_bottom_right,
            ne_node_region_header,      ne_node_region_top,
            ne_node_region_bottom,      ne_node_region_left,
            ne_node_region_right,       ne_node_region_center
        };

        for (size_t i = 0; i < sizeof(c_regions) / sizeof(*c_regions); ++i) {
            ImRect bounds = ne_node_get_region_bounds(node, c_regions[i]);
            if (ne_rect_contains(bounds, point)) return c_regions[i];
        }
    }

    return ne_node_region_none;
}

//------------------------------------------------------------------------------
// Link (upstream Link::Draw/Draw/UpdateEndpoints/GetCurve/TestHit/TestHit/GetBounds).

void ne_link_draw(struct ne_link *link, ImDrawList *draw_list, int flags)
{
    if (flags == ne_draw_flags_none) {
        ImDrawList_ChannelsSetCurrent(draw_list, c_link_channel_links);
        ne_link_draw_color(link, draw_list, link->m_color, 0.0f);
    } else if (flags & ne_draw_flags_selected) {
        ImU32 border_color = ne_editor_context_get_color(link->base.editor, ne_style_color_sel_link_border);
        ImDrawList_ChannelsSetCurrent(draw_list, c_link_channel_selection);
        ne_link_draw_color(link, draw_list, border_color, 4.5f);
    } else if (flags & ne_draw_flags_hovered) {
        ImU32 border_color = ne_editor_context_get_color(link->base.editor, ne_style_color_hov_link_border);
        ImDrawList_ChannelsSetCurrent(draw_list, c_link_channel_selection);
        ne_link_draw_color(link, draw_list, border_color, 2.0f);
    } else if (flags & ne_draw_flags_highlighted) {
        ImDrawList_ChannelsSetCurrent(draw_list, c_link_channel_selection);
        ne_link_draw_color(link, draw_list, link->m_highlight_color, 3.5f);
    }
}

void ne_link_draw_color(struct ne_link *link, ImDrawList *draw_list, ImU32 color, float extra_thickness)
{
    if (!link->base.m_is_live) return;

    struct ne_cubic_bezier_points curve = ne_link_get_curve(link);

    ne_draw_list_add_bezier_with_arrows(
        draw_list,
        curve,
        link->m_thickness + extra_thickness,
        link->m_start_pin && link->m_start_pin->m_arrow_size > 0.0f
            ? link->m_start_pin->m_arrow_size + extra_thickness
            : 0.0f,
        link->m_start_pin && link->m_start_pin->m_arrow_width > 0.0f
            ? link->m_start_pin->m_arrow_width + extra_thickness
            : 0.0f,
        link->m_end_pin && link->m_end_pin->m_arrow_size > 0.0f
            ? link->m_end_pin->m_arrow_size + extra_thickness
            : 0.0f,
        link->m_end_pin && link->m_end_pin->m_arrow_width > 0.0f
            ? link->m_end_pin->m_arrow_width + extra_thickness
            : 0.0f,
        true,
        color,
        1.0f,
        link->m_start_pin && link->m_start_pin->m_snap_link_to_dir
            ? &link->m_start_pin->m_dir
            : NULL,
        link->m_end_pin && link->m_end_pin->m_snap_link_to_dir
            ? &link->m_end_pin->m_dir
            : NULL
    );
}

void ne_link_update_endpoints(struct ne_link *link)
{
    ne_line line = ne_pin_get_closest_line(link->m_start_pin, link->m_end_pin);
    link->m_start = line.A;
    link->m_end = line.B;
}

static float ne_link_ease_link_strength(ImVec2 a, ImVec2 b, float strength)
{
    const float distance_x = b.x - a.x;
    const float distance_y = b.y - a.y;
    const float distance = sqrtf(distance_x * distance_x + distance_y * distance_y);
    const float half_distance = distance * 0.5f;

    if (half_distance < strength)
        strength = strength * sinf(NE_PI * 0.5f * half_distance / strength);

    return strength;
}

struct ne_cubic_bezier_points ne_link_get_curve(const struct ne_link *link)
{
    const float start_strength = ne_link_ease_link_strength(
        link->m_start,
        link->m_end,
        link->m_start_pin->m_strength
    );
    const float end_strength = ne_link_ease_link_strength(
        link->m_start,
        link->m_end,
        link->m_end_pin->m_strength
    );
    const ImVec2 cp0 = ne_vec2_add(
        link->m_start,
        ne_vec2_scale(link->m_start_pin->m_dir, start_strength)
    );
    const ImVec2 cp1 = ne_vec2_add(link->m_end, ne_vec2_scale(link->m_end_pin->m_dir, end_strength));

    struct ne_cubic_bezier_points result;
    result.P0 = link->m_start;
    result.P1 = cp0;
    result.P2 = cp1;
    result.P3 = link->m_end;

    return result;
}

bool ne_link_test_hit(struct ne_link *link, ImVec2 point, float extra_thickness)
{
    if (!link->base.m_is_live) return false;

    ImRect bounds = ne_link_get_bounds(link);
    if (extra_thickness > 0.0f) ImRect_Expand(&bounds, extra_thickness);

    if (!ne_rect_contains(bounds, point)) return false;

    struct ne_cubic_bezier_points bezier = ne_link_get_curve(link);
    struct ne_project_result result = ne_project_on_cubic_bezier(
        point,
        bezier.P0,
        bezier.P1,
        bezier.P2,
        bezier.P3,
        50
    );

    return result.Distance <= link->m_thickness + extra_thickness;
}

bool ne_link_test_hit_rect(struct ne_link *link, ImRect rect, bool allow_intersect)
{
    if (!link->base.m_is_live) return false;

    ImRect bounds = ne_link_get_bounds(link);

    if (ImRect_ContainsImRect(&rect, bounds)) return true;

    if (!allow_intersect || !ImRect_Overlaps(&rect, bounds)) return false;

    struct ne_cubic_bezier_points bezier = ne_link_get_curve(link);

    const ImVec2 p0 = ImRect_GetTL(&rect);
    const ImVec2 p1 = ImRect_GetTR(&rect);
    const ImVec2 p2 = ImRect_GetBR(&rect);
    const ImVec2 p3 = ImRect_GetBL(&rect);

    if (ne_cubic_bezier_line_intersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p0, p1)
            .Count > 0)
        return true;
    if (ne_cubic_bezier_line_intersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p1, p2)
            .Count > 0)
        return true;
    if (ne_cubic_bezier_line_intersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p2, p3)
            .Count > 0)
        return true;
    if (ne_cubic_bezier_line_intersect(bezier.P0, bezier.P1, bezier.P2, bezier.P3, p3, p0)
            .Count > 0)
        return true;

    return false;
}

ImRect ne_link_get_bounds(const struct ne_link *link)
{
    if (link->base.m_is_live) {
        struct ne_cubic_bezier_points curve = ne_link_get_curve(link);
        ImRect bounds = ne_cubic_bezier_bounding_rect(
            curve.P0,
            curve.P1,
            curve.P2,
            curve.P3
        );

        if (ImRect_GetWidth(&bounds) == 0.0f) {
            bounds.Min.x -= 0.5f;
            bounds.Max.x += 0.5f;
        }

        if (ImRect_GetHeight(&bounds) == 0.0f) {
            bounds.Min.y -= 0.5f;
            bounds.Max.y += 0.5f;
        }

        if (link->m_start_pin->m_arrow_size) {
            ImVec2 start_dir = ne_vec2_normalized(
                ne_cubic_bezier_tangent(curve.P0, curve.P1, curve.P2, curve.P3, 0.0f)
            );
            ImVec2 pa = curve.P0;
            ImVec2 pb = ne_vec2_sub(
                curve.P0,
                ne_vec2_scale(start_dir, link->m_start_pin->m_arrow_size)
            );
            ImVec2 mn = ne_vec2_min(pa, pb);
            ImVec2 mx = ne_vec2_max(pa, pb);
            ImRect arrow_bounds = (ImRect){
                mn,
                ne_vec2_max(mx, ne_vec2_add(mn, (ImVec2){ 1, 1 }))
            };
            ImRect_AddImRect(&bounds, arrow_bounds);
        }

        if (link->m_end_pin->m_arrow_size) {
            ImVec2 end_dir = ne_vec2_normalized(
                ne_cubic_bezier_tangent(curve.P0, curve.P1, curve.P2, curve.P3, 1.0f)
            );
            ImVec2 pa = curve.P3;
            ImVec2 pb = ne_vec2_add(
                curve.P3,
                ne_vec2_scale(end_dir, link->m_end_pin->m_arrow_size)
            );
            ImVec2 mn = ne_vec2_min(pa, pb);
            ImVec2 mx = ne_vec2_max(pa, pb);
            ImRect arrow_bounds = (ImRect){
                mn,
                ne_vec2_max(mx, ne_vec2_add(mn, (ImVec2){ 1, 1 }))
            };
            ImRect_AddImRect(&bounds, arrow_bounds);
        }

        return bounds;
    } else return (ImRect){ { 0, 0 }, { 0, 0 } };
}

//------------------------------------------------------------------------------
// Editor Context
//------------------------------------------------------------------------------
static ImU32 ne_color_v4_to_u32(ImVec4 c)
{
    return IM_COL32((int)(c.x * 255.0f), (int)(c.y * 255.0f), (int)(c.z * 255.0f), (int)(c.w * 255.0f));
}

/* upstream resetAndCollect lambda */
#define NE_RESET_AND_COLLECT(arr)                                              \
    do {                                                                       \
        int _n = acount(arr);                                                  \
        int _w = 0;                                                            \
        for (int _i = 0; _i < _n; ++_i) {                                      \
            ne_object *_o = (ne_object *)(arr)[_i];                            \
            if (_o->m_delete_on_new_frame) {                                   \
                free(_o);                                                      \
            } else {                                                           \
                ne_object_reset(_o);                                           \
                (arr)[_w++] = (arr)[_i];                                       \
            }                                                                  \
        }                                                                      \
        if (arr) AHDR(arr)->size = _w;                                         \
    } while (0)

void ne_editor_context_init(ne_editor_context *editor, const ne_config *config)
{
    memset(editor, 0, sizeof(*editor));

    ne_config_default(&editor->m_config);
    if (config) editor->m_config = *config;

    editor->m_shortcuts_enabled = true;
    editor->m_selection_id = 1;
    editor->m_background_click_button_index = -1;
    editor->m_background_double_click_button_index = -1;

    ne_style_ctx_init(&editor->m_style);
    ne_node_builder_init(&editor->m_node_builder, editor);
    ne_hint_builder_init(&editor->m_hint_builder, editor);
    ne_navigate_action_init(&editor->m_navigate_action, editor, &editor->m_canvas);
    ne_size_action_init(&editor->m_size_action, editor);
    ne_drag_action_init(&editor->m_drag_action, editor);
    ne_select_action_init(&editor->m_select_action, editor);
    ne_context_menu_action_init(&editor->m_context_menu_action, editor);
    ne_shortcut_action_init(&editor->m_shortcut_action, editor);
    ne_create_item_action_init(&editor->m_create_item_action, editor);
    ne_delete_items_action_init(&editor->m_delete_items_action, editor);

    ne_flow_animation_controller_init(&editor->m_flow_animation_controller, editor);
    apush(editor->m_animation_controllers, (ne_animation_controller *)&editor->m_flow_animation_controller);
}

void ne_editor_context_free(ne_editor_context *editor)
{
    if (editor->m_is_initialized) ne_editor_context_save_settings(editor);

    for (int i = 0; i < acount(editor->m_links); ++i) free(editor->m_links[i]);
    for (int i = 0; i < acount(editor->m_pins); ++i) free(editor->m_pins[i]);
    for (int i = 0; i < acount(editor->m_nodes); ++i) free(editor->m_nodes[i]);

    ImDrawListSplitter_ClearFreeMemory(&editor->m_splitter);

    ne_flow_animation_controller_free(&editor->m_flow_animation_controller);

    afree(editor->m_animation_controllers);
    afree(editor->m_nodes);
    afree(editor->m_pins);
    afree(editor->m_links);
    afree(editor->m_selected_objects);
    afree(editor->m_last_selected_objects);
    afree(editor->m_live_animations);
    afree(editor->m_last_live_animations);
    afree(editor->m_settings.m_nodes);
    afree(editor->m_settings.m_selection);
    afree(editor->m_drag_action.m_objects);
    afree(editor->m_select_action.m_candidate_objects);
    afree(editor->m_select_action.m_selected_objects_at_start);
    afree(editor->m_shortcut_action.m_context);
    afree(editor->m_delete_items_action.m_manually_deleted_objects);
    afree(editor->m_delete_items_action.m_candidate_objects);
    afree(editor->m_style.color_stack);
    afree(editor->m_style.var_stack);
}

void ne_editor_context_begin(ne_editor_context *editor, const char *id, ImVec2 size)
{
    editor->m_editor_active_id = ImGui_GetID(id);
    ImGui_PushID(id);

    ImVec2 available_content_size = ImGui_GetContentRegionAvail();
    ImVec2 canvas_size = cImFloorImVec2(size);
    if (canvas_size.x <= 0.0f)
        canvas_size.x = ne_imax_f(4.0f, available_content_size.x);
    if (canvas_size.y <= 0.0f)
        canvas_size.y = ne_imax_f(4.0f, available_content_size.y);

    if (!editor->m_is_initialized) {
        /* Cycle canvas, so it has a chance to initialize its size before settings are loaded */
        if (ne_canvas_begin(&editor->m_canvas, id, canvas_size))
            ne_canvas_end(&editor->m_canvas);

        ne_editor_context_load_settings(editor);
        editor->m_is_initialized = true;
    }

    NE_RESET_AND_COLLECT(editor->m_nodes);
    NE_RESET_AND_COLLECT(editor->m_pins);
    NE_RESET_AND_COLLECT(editor->m_links);

    editor->m_draw_list = ImGui_GetWindowDrawList();

    ne_draw_list_swap_splitter(editor->m_draw_list, &editor->m_splitter);
    editor->m_external_channel = editor->m_draw_list->_Splitter._Current;

    if (editor->m_current_action && ne_action_is_dragging(editor->m_current_action) &&
        ne_navigate_action_move_over_edge(&editor->m_navigate_action, canvas_size)) {
        ImGuiIO *io = ImGui_GetIO();
        ImVec2 offset = ne_navigate_action_get_move_screen_offset(&editor->m_navigate_action);
        for (int i = 0; i < 5; ++i)
            io->MouseClickedPos[i] = ne_vec2_sub(io->MouseClickedPos[i], offset);
    } else ne_navigate_action_stop_move_over_edge(&editor->m_navigate_action);

    ImVec2 previous_size = ne_vec2_sub(
        ne_canvas_rect(&editor->m_canvas).Max,
        ne_canvas_rect(&editor->m_canvas).Min
    );
    ImRect previous_visible_rect = ne_canvas_view_rect(&editor->m_canvas);
    editor->m_is_canvas_visible = ne_canvas_begin(&editor->m_canvas, id, canvas_size);

    editor->m_is_focused = ImGui_IsWindowFocused(0);

    ne_navigate_action_set_window(
        &editor->m_navigate_action,
        ne_canvas_view_rect(&editor->m_canvas).Min,
        ne_vec2_sub(
            ne_canvas_view_rect(&editor->m_canvas).Max,
            ne_canvas_view_rect(&editor->m_canvas).Min
        )
    );

    /* Handle canvas size change. Scale to Y axis, center on X. */
    if (!ne_rect_is_empty(previous_visible_rect) &&
        ne_vec2_ne(previous_size, canvas_size)) {
        ne_navigate_action_finish_navigation(&editor->m_navigate_action);

        float center_x = (previous_visible_rect.Max.x +
                          previous_visible_rect.Min.x) *
                         0.5f;
        float center_y = (previous_visible_rect.Max.y +
                          previous_visible_rect.Min.y) *
                         0.5f;
        ImRect current_visible_rect = ne_canvas_view_rect(&editor->m_canvas);
        float current_aspect_ratio = ImRect_GetHeight(&current_visible_rect)
                                       ? (ImRect_GetWidth(&current_visible_rect) /
                                          ImRect_GetHeight(&current_visible_rect))
                                       : 0.0f;
        float width = ImRect_GetWidth(&previous_visible_rect);
        float height = ImRect_GetHeight(&previous_visible_rect);

        if (editor->m_config.canvas_size_mode == ne_canvas_size_mode_fit_vertical_view) {
            height = ImRect_GetHeight(&previous_visible_rect);
            width = height * current_aspect_ratio;
        } else if (editor->m_config.canvas_size_mode == ne_canvas_size_mode_fit_horizontal_view) {
            width = ImRect_GetWidth(&previous_visible_rect);
            height = width / current_aspect_ratio;
        } else if (editor->m_config.canvas_size_mode == ne_canvas_size_mode_center_only) {
            width = ImRect_GetWidth(&current_visible_rect);
            height = ImRect_GetHeight(&current_visible_rect);
        }

        previous_visible_rect.Min.x = center_x - 0.5f * width;
        previous_visible_rect.Max.x = center_x + 0.5f * width;
        previous_visible_rect.Min.y = center_y - 0.5f * height;
        previous_visible_rect.Max.y = center_y + 0.5f * height;

        ne_navigate_action_navigate_to(
            &editor->m_navigate_action,
            previous_visible_rect,
            ne_zoom_mode_exact,
            0.0f,
            ne_navigation_reason_unknown
        );
    }

    ne_canvas_set_view_view(
        &editor->m_canvas,
        ne_navigate_action_get_view(&editor->m_navigate_action)
    );

    /* Reserve channels for background and links */
    ne_draw_list_channels_grow(editor->m_draw_list, c_node_start_channel);

    if (ne_editor_context_has_selection_changed(editor))
        ++editor->m_selection_id;

    editor->m_last_selected_objects = NULL;
    for (int i = 0; i < acount(editor->m_selected_objects); ++i)
        apush(editor->m_last_selected_objects, editor->m_selected_objects[i]);
}

void ne_editor_context_end(ne_editor_context *editor)
{
    ne_control control = ne_editor_context_build_control(
        editor,
        editor->m_current_action && ne_action_is_dragging(editor->m_current_action)
    );

    editor->m_hovered_node = control.hot_node && editor->m_current_action == NULL
                               ? control.hot_node->m_id
                               : 0;
    editor->m_hovered_pin = control.hot_pin && editor->m_current_action == NULL
                              ? control.hot_pin->m_id
                              : 0;
    editor->m_hovered_link = control.hot_link && editor->m_current_action == NULL
                               ? control.hot_link->m_id
                               : 0;
    editor->m_double_clicked_node = control.double_clicked_node
                                      ? control.double_clicked_node->m_id
                                      : 0;
    editor->m_double_clicked_pin = control.double_clicked_pin
                                     ? control.double_clicked_pin->m_id
                                     : 0;
    editor->m_double_clicked_link = control.double_clicked_link
                                      ? control.double_clicked_link->m_id
                                      : 0;
    editor->m_background_click_button_index = control.background_click_button_index;
    editor->m_background_double_click_button_index = control.background_double_click_button_index;

    const bool is_selecting = editor->m_current_action &&
                              ne_action_as_select(editor->m_current_action) != NULL;
    const bool is_dragging = editor->m_current_action &&
                             ne_action_as_drag(editor->m_current_action) != NULL;

    /* Draw nodes */
    for (int i = 0; i < acount(editor->m_nodes); ++i)
        if (editor->m_nodes[i]->base.m_is_live &&
            ne_object_is_visible((ne_object *)editor->m_nodes[i]))
            ne_node_draw(editor->m_nodes[i], editor->m_draw_list, ne_draw_flags_none);

    /* Draw links */
    for (int i = 0; i < acount(editor->m_links); ++i)
        if (editor->m_links[i]->base.m_is_live &&
            ne_object_is_visible((ne_object *)editor->m_links[i]))
            ne_link_draw(editor->m_links[i], editor->m_draw_list, ne_draw_flags_none);

    /* Highlight selected objects */
    {
        ne_object **selected_objects = editor->m_selected_objects;
        ne_select_action *select_action = editor->m_current_action
                                            ? ne_action_as_select(editor->m_current_action)
                                            : NULL;
        if (select_action)
            selected_objects = select_action->m_candidate_objects;

        for (int i = 0; i < acount(selected_objects); ++i) {
            if (ne_object_is_visible(selected_objects[i]))
                ne_object_draw(selected_objects[i], editor->m_draw_list, ne_draw_flags_selected);
        }

        /* Highlight adjacent links */
        for (int i = 0; i < acount(editor->m_links); ++i) {
            struct ne_link *link = editor->m_links[i];
            if (!link->base.m_is_live || !ne_object_is_visible((ne_object *)link))
                continue;

            bool is_link_highlighted = (link->m_start_pin->m_node->m_highlight_connected_links &&
                                        link->m_start_pin->m_node->base.m_is_selected) ||
                                       (link->m_end_pin->m_node->m_highlight_connected_links &&
                                        link->m_end_pin->m_node->base.m_is_selected);
            if (!is_link_highlighted) continue;

            ne_link_draw(link, editor->m_draw_list, ne_draw_flags_highlighted);
        }
    }

    if (!is_selecting) {
        ne_object *hovered_object = control.hot_object;
        ne_drag_action *drag_action = editor->m_current_action
                                        ? ne_action_as_drag(editor->m_current_action)
                                        : NULL;
        if (drag_action) hovered_object = drag_action->m_dragged_object;
        ne_size_action *size_action = editor->m_current_action
                                        ? ne_action_as_size(editor->m_current_action)
                                        : NULL;
        if (size_action)
            hovered_object = (ne_object *)size_action->m_sized_node;

        if (hovered_object && !ne_editor_context_is_selected(editor, hovered_object) &&
            ne_object_is_visible(hovered_object))
            ne_object_draw(hovered_object, editor->m_draw_list, ne_draw_flags_hovered);
    }

    /* Draw animations */
    for (int i = 0; i < acount(editor->m_animation_controllers); ++i)
        editor->m_animation_controllers[i]
            ->draw(editor->m_animation_controllers[i], editor->m_draw_list);

    if (editor->m_current_action &&
        !ne_action_process(editor->m_current_action, &control))
        editor->m_current_action = NULL;

    if (editor->m_navigate_action.m_is_active)
        ne_navigate_action_process(&editor->m_navigate_action, &control);
    else ne_navigate_action_accept(&editor->m_navigate_action, &control);

    if (NULL == editor->m_current_action) {
        ne_editor_action *possible_action = NULL;

        ne_editor_action *actions[8];
        int action_count = 0;
        actions[action_count++] = (ne_editor_action *)&editor->m_context_menu_action;
        actions[action_count++] = (ne_editor_action *)&editor->m_shortcut_action;
        actions[action_count++] = (ne_editor_action *)&editor->m_size_action;
        actions[action_count++] = (ne_editor_action *)&editor->m_drag_action;
        actions[action_count++] = (ne_editor_action *)&editor->m_create_item_action;
        actions[action_count++] = (ne_editor_action *)&editor->m_delete_items_action;
        actions[action_count++] = (ne_editor_action *)&editor->m_select_action;

        for (int i = 0; i < action_count; ++i) {
            ne_accept_result result = ne_action_accept(actions[i], &control);

            if (result == ne_accept_true) {
                editor->m_current_action = actions[i];
                break;
            } else if (result == ne_accept_possible) {
                if (possible_action == NULL) possible_action = actions[i];
                else ne_action_reject(actions[i]);
            }
        }

        if (possible_action)
            ImGui_SetMouseCursor(ne_action_get_cursor(possible_action));

        if (editor->m_current_action && possible_action)
            ne_action_reject(possible_action);
    }

    if (editor->m_current_action)
        ImGui_SetMouseCursor(ne_action_get_cursor(editor->m_current_action));

    /* Draw selection rectangle */
    ne_select_action_draw(&editor->m_select_action, editor->m_draw_list);

    bool sort_groups = false;
    if (control.active_node) {
        if (!ne_is_group(control.active_node)) {
            /* Bring active node to front */
            int active_index = -1;
            for (int i = 0; i < acount(editor->m_nodes); ++i)
                if (editor->m_nodes[i] == control.active_node) {
                    active_index = i;
                    break;
                }
            if (active_index >= 0) {
                ne_node *active = editor->m_nodes[active_index];
                for (int i = active_index; i + 1 < acount(editor->m_nodes); ++i)
                    editor->m_nodes[i] = editor->m_nodes[i + 1];
                editor->m_nodes[acount(editor->m_nodes) - 1] = active;
            }
        } else if (
            !is_dragging && editor->m_current_action &&
            ne_action_as_drag(editor->m_current_action)
        ) {
            /* Bring content of dragged group to front */
            ne_node **nodes = NULL;
            ne_node_get_grouped_nodes(control.active_node, &nodes, false);

            /* stable_partition: non-grouped first, grouped last */
            ne_node **tmp = NULL;
            for (int i = 0; i < acount(editor->m_nodes); ++i) {
                bool in_group = false;
                for (int j = 0; j < acount(nodes); ++j)
                    if (editor->m_nodes[i] == nodes[j]) {
                        in_group = true;
                        break;
                    }
                if (!in_group) apush(tmp, editor->m_nodes[i]);
            }
            for (int i = 0; i < acount(editor->m_nodes); ++i) {
                bool in_group = false;
                for (int j = 0; j < acount(nodes); ++j)
                    if (editor->m_nodes[i] == nodes[j]) {
                        in_group = true;
                        break;
                    }
                if (in_group) apush(tmp, editor->m_nodes[i]);
            }
            afree(editor->m_nodes);
            editor->m_nodes = tmp;

            afree(nodes);
            sort_groups = true;
        }
    }

    /* Sort nodes if bounds of node changed */
    if (sort_groups ||
        ((editor->m_settings.m_dirty_reason &
          (ne_save_reason_position | ne_save_reason_size)) != ne_save_reason_none)) {
        /* Bring all groups before regular nodes */
        ne_node **tmp = NULL;
        for (int i = 0; i < acount(editor->m_nodes); ++i)
            if (ne_is_group(editor->m_nodes[i])) apush(tmp, editor->m_nodes[i]);
        int groups_end = acount(tmp);
        for (int i = 0; i < acount(editor->m_nodes); ++i)
            if (!ne_is_group(editor->m_nodes[i]))
                apush(tmp, editor->m_nodes[i]);

        /* Sort groups by area (descending) */
        for (int i = 0; i + 1 < groups_end; ++i)
            for (int j = i + 1; j < groups_end; ++j) {
                ImVec2 lhs_size = (tmp[i] == editor->m_size_action.m_sized_node)
                                    ? ImRect_GetSize(&editor->m_size_action.m_start_group_bounds)
                                    : ImRect_GetSize(&tmp[i]->m_group_bounds);
                ImVec2 rhs_size = (tmp[j] == editor->m_size_action.m_sized_node)
                                    ? ImRect_GetSize(&editor->m_size_action.m_start_group_bounds)
                                    : ImRect_GetSize(&tmp[j]->m_group_bounds);
                float lhs_area = lhs_size.x * lhs_size.y;
                float rhs_area = rhs_size.x * rhs_size.y;
                if (lhs_area < rhs_area) {
                    ne_node *t = tmp[i];
                    tmp[i] = tmp[j];
                    tmp[j] = t;
                }
            }

        afree(editor->m_nodes);
        editor->m_nodes = tmp;
    }

    /* Apply Z order */
    for (int i = 0; i + 1 < acount(editor->m_nodes); ++i)
        for (int j = i + 1; j < acount(editor->m_nodes); ++j)
            if (editor->m_nodes[i]->m_z_position > editor->m_nodes[j]->m_z_position) {
                ne_node *t = editor->m_nodes[i];
                editor->m_nodes[i] = editor->m_nodes[j];
                editor->m_nodes[j] = t;
            }

    /* Grow channel list to hold twice as much channels and place them in node drawing order. */
    {
        int live_node_count = 0;
        for (int i = 0; i < acount(editor->m_nodes); ++i)
            if (editor->m_nodes[i]->base.m_is_live) ++live_node_count;

        int node_channel_count = editor->m_draw_list->_Splitter._Count;
        ne_draw_list_channels_grow(
            editor->m_draw_list,
            editor->m_draw_list->_Splitter._Count +
                c_channels_per_node * live_node_count + c_link_channel_count
        );

        int target_channel = node_channel_count;

        /* Copy group nodes */
        int groups_end = 0;
        while (groups_end < acount(editor->m_nodes) &&
               ne_is_group(editor->m_nodes[groups_end]))
            ++groups_end;

        for (int i = 0; i < groups_end; ++i) {
            ne_node *node = editor->m_nodes[i];
            if (!node->base.m_is_live) continue;
            for (int c = 0; c < c_channels_per_node; ++c)
                ne_draw_list_swap_channels(editor->m_draw_list, node->m_channel + c, target_channel + c);
            node->m_channel = target_channel;
            target_channel += c_channels_per_node;
        }

        /* Copy links */
        for (int i = 0; i < c_link_channel_count; ++i, ++target_channel)
            ne_draw_list_swap_channels(editor->m_draw_list, c_link_start_channel + i, target_channel);

        /* Copy normal nodes */
        for (int i = groups_end; i < acount(editor->m_nodes); ++i) {
            ne_node *node = editor->m_nodes[i];
            if (!node->base.m_is_live) continue;
            for (int c = 0; c < c_channels_per_node; ++c)
                ne_draw_list_swap_channels(editor->m_draw_list, node->m_channel + c, target_channel + c);
            node->m_channel = target_channel;
            target_channel += c_channels_per_node;
        }
    }

    /* Draw grid */
    {
        ImDrawList_ChannelsSetCurrent(editor->m_draw_list, c_user_channel_grid);

        ImVec2 offset = ne_vec2_scale(
            ne_canvas_view_origin(&editor->m_canvas),
            1.0f / ne_canvas_view_scale(&editor->m_canvas)
        );
        ImU32 grid_color = ne_editor_context_get_color_alpha(
            editor,
            ne_style_color_grid,
            ne_iclamp_f(
                ne_canvas_view_scale(&editor->m_canvas) *
                    ne_canvas_view_scale(&editor->m_canvas),
                0.0f,
                1.0f
            )
        );
        const float grid_sx = 32.0f;
        const float grid_sy = 32.0f;
        ImVec2 view_pos = ne_canvas_view_rect(&editor->m_canvas).Min;
        ImVec2 view_size = ne_vec2_sub(
            ne_canvas_view_rect(&editor->m_canvas).Max,
            ne_canvas_view_rect(&editor->m_canvas).Min
        );

        ImDrawList_AddRectFilled(
            editor->m_draw_list,
            view_pos,
            ne_vec2_add(view_pos, view_size),
            ne_editor_context_get_color(editor, ne_style_color_bg)
        );

        for (float x = fmodf(offset.x, grid_sx); x < view_size.x; x += grid_sx)
            ImDrawList_AddLine(
                editor->m_draw_list,
                ne_vec2_add((ImVec2){ x, 0.0f }, view_pos),
                ne_vec2_add((ImVec2){ x, view_size.y }, view_pos),
                grid_color
            );
        for (float y = fmodf(offset.y, grid_sy); y < view_size.y; y += grid_sy)
            ImDrawList_AddLine(
                editor->m_draw_list,
                ne_vec2_add((ImVec2){ 0.0f, y }, view_pos),
                ne_vec2_add((ImVec2){ view_size.x, y }, view_pos),
                grid_color
            );
    }

    /* Move user and hint channels to top */
    {
        ImDrawList_ChannelsSetCurrent(editor->m_draw_list, 0);

        int channel_count = editor->m_draw_list->_Splitter._Count;
        ne_draw_list_channels_grow(editor->m_draw_list, channel_count + 3);
        ne_draw_list_swap_channels(editor->m_draw_list, c_user_channel_hints_background, channel_count + 0);
        ne_draw_list_swap_channels(editor->m_draw_list, c_user_channel_hints, channel_count + 1);
        ne_draw_list_swap_channels(editor->m_draw_list, c_user_channel_content, channel_count + 2);

        /* preTransformClipRect */
        for (int c = 0; c < 3; ++c) {
            ImDrawChannel *channel = &editor->m_draw_list->_Splitter._Channels
                                          .Data[channel_count + c];
            for (int i = 0; i < channel->_CmdBuffer.Size; ++i) {
                ImDrawCmd *cmd = &channel->_CmdBuffer.Data[i];
                ImVec2 a = ne_editor_context_to_canvas(
                    editor,
                    (ImVec2){ cmd->ClipRect.x, cmd->ClipRect.y }
                );
                ImVec2 b = ne_editor_context_to_canvas(
                    editor,
                    (ImVec2){ cmd->ClipRect.z, cmd->ClipRect.w }
                );
                cmd->ClipRect = (ImVec4){ a.x, a.y, b.x, b.y };
            }
        }
    }

    ne_editor_context_update_animations(editor);

    ImDrawList_ChannelsMerge(editor->m_draw_list);

    if (editor->m_is_canvas_visible) ne_canvas_end(&editor->m_canvas);

    ne_draw_list_swap_splitter(editor->m_draw_list, &editor->m_splitter);

    /* Draw border */
    {
        ImGuiStyle *style = ImGui_GetStyle();
        ImVec4 border_shadow_color = style->Colors[ImGuiCol_BorderShadow];
        ImVec4 border_color = style->Colors[ImGuiCol_Border];
        ImDrawList_AddRect(
            editor->m_draw_list,
            ne_vec2_add(ne_canvas_rect(&editor->m_canvas).Min, (ImVec2){ 1, 1 }),
            ne_vec2_sub(ne_canvas_rect(&editor->m_canvas).Max, (ImVec2){ 1, 1 }),
            ne_color_v4_to_u32(border_shadow_color)
        );
        ImDrawList_AddRect(
            editor->m_draw_list,
            ne_canvas_rect(&editor->m_canvas).Min,
            ne_canvas_rect(&editor->m_canvas).Max,
            ne_color_v4_to_u32(border_color)
        );
    }

    ImGui_PopID();

    if (!editor->m_current_action && editor->m_is_first_frame &&
        acount(editor->m_settings.m_selection) > 0) {
        ne_editor_context_clear_selection(editor);
        for (int i = 0; i < acount(editor->m_settings.m_selection); ++i) {
            ne_object *object = ne_editor_context_find_object(
                editor,
                editor->m_settings.m_selection[i]
            );
            if (object) ne_editor_context_select_object(editor, object);
        }
    }

    if (ne_editor_context_has_selection_changed(editor))
        ne_editor_context_make_dirty(editor, ne_save_reason_selection);

    if (editor->m_settings.m_is_dirty && !editor->m_current_action)
        ne_editor_context_save_settings(editor);

    editor->m_draw_list = NULL;
    editor->m_is_first_frame = false;
}

bool ne_editor_context_do_link(
    ne_editor_context *editor,
    ne_link_id id,
    ne_pin_id start_pin_id,
    ne_pin_id end_pin_id,
    ImU32 color,
    float thickness
)
{
    ne_pin *start_pin = ne_editor_context_find_pin(editor, start_pin_id);
    ne_pin *end_pin = ne_editor_context_find_pin(editor, end_pin_id);

    if (!start_pin || !start_pin->base.m_is_live || !end_pin || !end_pin->base.m_is_live)
        return false;

    start_pin->m_has_connection = true;
    end_pin->m_has_connection = true;

    struct ne_link *link = ne_editor_context_get_link(editor, id);
    link->m_start_pin = start_pin;
    link->m_end_pin = end_pin;
    link->m_color = color;
    link->m_highlight_color = ne_editor_context_get_color(editor, ne_style_color_highlight_link_border);
    link->m_thickness = thickness;
    link->base.m_is_live = true;

    ne_link_update_endpoints(link);

    return true;
}

void ne_editor_context_set_node_position(ne_editor_context *editor, ne_node_id node_id, ImVec2 position)
{
    ne_node *node = ne_editor_context_find_node(editor, node_id);
    if (!node) {
        node = ne_editor_context_create_node(editor, node_id);
        node->base.m_is_live = false;
    }

    if (ne_vec2_ne(node->m_bounds.Min, position)) {
        ImRect_Translate(&node->m_bounds, ne_vec2_sub(position, node->m_bounds.Min));
        ne_floor_rect(&node->m_bounds);
        ne_editor_context_make_dirty_node(editor, ne_save_reason_position, node);
    }
}

void ne_editor_context_set_group_size(ne_editor_context *editor, ne_node_id node_id, ImVec2 size)
{
    ne_node *node = ne_editor_context_find_node(editor, node_id);
    if (!node) {
        node = ne_editor_context_create_node(editor, node_id);
        node->base.m_is_live = false;
    }

    node->m_type = ne_node_type_group;

    if (ne_vec2_ne(ImRect_GetSize(&node->m_group_bounds), size)) {
        node->m_group_bounds.Min = node->m_bounds.Min;
        node->m_group_bounds.Max = ne_vec2_add(node->m_bounds.Min, size);
        ne_floor_rect(&node->m_group_bounds);
        ne_editor_context_make_dirty_node(editor, ne_save_reason_size, node);
    }
}

ImVec2 ne_editor_context_get_node_position(ne_editor_context *editor, ne_node_id node_id)
{
    ne_node *node = ne_editor_context_find_node(editor, node_id);
    if (!node) return (ImVec2){ FLT_MAX, FLT_MAX };

    return node->m_bounds.Min;
}

ImVec2 ne_editor_context_get_node_size(ne_editor_context *editor, ne_node_id node_id)
{
    ne_node *node = ne_editor_context_find_node(editor, node_id);
    if (!node) return (ImVec2){ 0, 0 };

    return ImRect_GetSize(&node->m_bounds);
}

void ne_editor_context_set_node_z_position(ne_editor_context *editor, ne_node_id node_id, float z)
{
    ne_node *node = ne_editor_context_find_node(editor, node_id);
    if (!node) {
        node = ne_editor_context_create_node(editor, node_id);
        node->base.m_is_live = false;
    }

    node->m_z_position = z;
}

float ne_editor_context_get_node_z_position(ne_editor_context *editor, ne_node_id node_id)
{
    ne_node *node = ne_editor_context_find_node(editor, node_id);
    if (!node) return 0.0f;

    return node->m_z_position;
}

void ne_editor_context_mark_node_to_restore_state(ne_editor_context *editor, ne_node *node)
{
    (void)editor;
    node->m_restore_state = true;
}

void ne_editor_context_update_node_state(ne_editor_context *editor, ne_node *node)
{
    bool try_load_state = node->m_restore_state;

    node->m_restore_state = false;

    ne_node_settings *settings = ne_settings_find_node(&editor->m_settings, node->m_id);
    if (!settings) return;

    if (!try_load_state && settings->m_was_used) return;

    if (!settings->m_was_used) {
        ne_editor_context_make_dirty_node(editor, ne_save_reason_add_node, node);
        settings->m_was_used = true;
    }

    if (try_load_state) {
        ne_node_settings new_settings = *settings;
        char *data = ne_config_load_node(&editor->m_config, node->m_id);
        if (data && ne_node_settings_parse_str(data, &new_settings))
            *settings = new_settings;
        free(data);
    }

    node->m_bounds.Min = settings->m_location;
    node->m_bounds.Max = ne_vec2_add(node->m_bounds.Min, settings->m_size);
    ne_floor_rect(&node->m_bounds);
    node->m_group_bounds.Min = settings->m_location;
    node->m_group_bounds.Max = ne_vec2_add(node->m_group_bounds.Min, settings->m_group_size);
    ne_floor_rect(&node->m_group_bounds);
}

void ne_editor_context_remove_settings(ne_editor_context *editor, ne_object *object)
{
    ne_node *node = ne_object_as_node(object);
    if (node) {
        ne_settings_remove_node(&editor->m_settings, node->m_id);
        ne_editor_context_make_dirty_node(editor, ne_save_reason_remove_node, node);
    }
}

void ne_editor_context_clear_selection(ne_editor_context *editor)
{
    for (int i = 0; i < acount(editor->m_selected_objects); ++i)
        editor->m_selected_objects[i]->m_is_selected = false;

    afree(editor->m_selected_objects);
    editor->m_selected_objects = NULL;
}

void ne_editor_context_select_object(ne_editor_context *editor, ne_object *object)
{
    apush(editor->m_selected_objects, object);
    object->m_is_selected = true;
}

void ne_editor_context_deselect_object(ne_editor_context *editor, ne_object *object)
{
    int index = -1;
    for (int i = 0; i < acount(editor->m_selected_objects); ++i)
        if (editor->m_selected_objects[i] == object) {
            index = i;
            break;
        }
    if (index < 0) return;

    object->m_is_selected = false;
    for (int i = index; i + 1 < acount(editor->m_selected_objects); ++i)
        editor->m_selected_objects[i] = editor->m_selected_objects[i + 1];
    AHDR(editor->m_selected_objects)->size--;
}

void ne_editor_context_set_selected_object(ne_editor_context *editor, ne_object *object)
{
    ne_editor_context_clear_selection(editor);
    ne_editor_context_select_object(editor, object);
}

void ne_editor_context_toggle_object_selection(ne_editor_context *editor, ne_object *object)
{
    if (ne_editor_context_is_selected(editor, object))
        ne_editor_context_deselect_object(editor, object);
    else ne_editor_context_select_object(editor, object);
}

bool ne_editor_context_is_selected(ne_editor_context *editor, ne_object *object)
{
    (void)editor;
    return object && object->m_is_selected;
}

bool ne_editor_context_is_any_node_selected(ne_editor_context *editor)
{
    for (int i = 0; i < acount(editor->m_selected_objects); ++i)
        if (ne_object_as_node(editor->m_selected_objects[i])) return true;
    return false;
}

bool ne_editor_context_is_any_link_selected(ne_editor_context *editor)
{
    for (int i = 0; i < acount(editor->m_selected_objects); ++i)
        if (ne_object_as_link(editor->m_selected_objects[i])) return true;
    return false;
}

bool ne_editor_context_has_selection_changed(ne_editor_context *editor)
{
    if (acount(editor->m_last_selected_objects) != acount(editor->m_selected_objects))
        return true;
    for (int i = 0; i < acount(editor->m_selected_objects); ++i)
        if (editor->m_last_selected_objects[i] != editor->m_selected_objects[i])
            return true;
    return false;
}

ne_node *ne_editor_context_find_node_at(ne_editor_context *editor, ImVec2 p)
{
    for (int i = 0; i < acount(editor->m_nodes); ++i)
        if (ne_object_test_hit((ne_object *)editor->m_nodes[i], p, 0.0f))
            return editor->m_nodes[i];

    return NULL;
}

void ne_editor_context_find_nodes_in_rect(
    ne_editor_context *editor,
    ImRect r,
    ne_node ***result,
    bool append,
    bool include_intersecting
)
{
    if (!append) *result = NULL;

    if (ne_rect_is_empty(r)) return;

    for (int i = 0; i < acount(editor->m_nodes); ++i)
        if (ne_object_test_hit_rect((ne_object *)editor->m_nodes[i], r, include_intersecting))
            apush(*result, editor->m_nodes[i]);
}

void ne_editor_context_find_links_in_rect(
    ne_editor_context *editor,
    ImRect r,
    struct ne_link ***result,
    bool append
)
{
    if (!append) *result = NULL;

    if (ne_rect_is_empty(r)) return;

    for (int i = 0; i < acount(editor->m_links); ++i)
        if (ne_object_test_hit_rect((ne_object *)editor->m_links[i], r, true))
            apush(*result, editor->m_links[i]);
}

bool ne_editor_context_has_any_links_node(ne_editor_context *editor, ne_node_id node_id)
{
    for (int i = 0; i < acount(editor->m_links); ++i) {
        struct ne_link *link = editor->m_links[i];
        if (!link->base.m_is_live) continue;
        if (link->m_start_pin->m_node->m_id == node_id ||
            link->m_end_pin->m_node->m_id == node_id)
            return true;
    }
    return false;
}

bool ne_editor_context_has_any_links_pin(ne_editor_context *editor, ne_pin_id pin_id)
{
    for (int i = 0; i < acount(editor->m_links); ++i) {
        struct ne_link *link = editor->m_links[i];
        if (!link->base.m_is_live) continue;
        if (link->m_start_pin->m_id == pin_id || link->m_end_pin->m_id == pin_id)
            return true;
    }
    return false;
}

int ne_editor_context_break_links_node(ne_editor_context *editor, ne_node_id node_id)
{
    int result = 0;
    for (int i = 0; i < acount(editor->m_links); ++i) {
        struct ne_link *link = editor->m_links[i];
        if (!link->base.m_is_live) continue;
        if (link->m_start_pin->m_node->m_id == node_id ||
            link->m_end_pin->m_node->m_id == node_id) {
            if (ne_delete_items_action_add(&editor->m_delete_items_action, (ne_object *)link))
                ++result;
        }
    }
    return result;
}

int ne_editor_context_break_links_pin(ne_editor_context *editor, ne_pin_id pin_id)
{
    int result = 0;
    for (int i = 0; i < acount(editor->m_links); ++i) {
        struct ne_link *link = editor->m_links[i];
        if (!link->base.m_is_live) continue;
        if (link->m_start_pin->m_id == pin_id || link->m_end_pin->m_id == pin_id) {
            if (ne_delete_items_action_add(&editor->m_delete_items_action, (ne_object *)link))
                ++result;
        }
    }
    return result;
}

void ne_editor_context_find_links_for_node(
    ne_editor_context *editor,
    ne_node_id node_id,
    struct ne_link ***result,
    bool add
)
{
    if (!add) *result = NULL;

    for (int i = 0; i < acount(editor->m_links); ++i) {
        struct ne_link *link = editor->m_links[i];
        if (!link->base.m_is_live) continue;
        if (link->m_start_pin->m_node->m_id == node_id ||
            link->m_end_pin->m_node->m_id == node_id)
            apush(*result, link);
    }
}

bool ne_editor_context_pin_had_any_links(ne_editor_context *editor, ne_pin_id pin_id)
{
    ne_pin *pin = ne_editor_context_find_pin(editor, pin_id);
    if (!pin || !pin->base.m_is_live) return false;

    return pin->m_has_connection || pin->m_had_connection;
}

ImVec2 ne_editor_context_to_canvas(ne_editor_context *editor, ImVec2 point)
{ return ne_canvas_to_local(&editor->m_canvas, point); }

ImVec2 ne_editor_context_to_screen(ne_editor_context *editor, ImVec2 point)
{ return ne_canvas_from_local(&editor->m_canvas, point); }

void ne_editor_context_notify_link_deleted(ne_editor_context *editor, struct ne_link *link)
{
    if (editor->m_last_active_link == link) editor->m_last_active_link = NULL;
}

void ne_editor_context_suspend(ne_editor_context *editor, ne_suspend_flags flags)
{
    IM_ASSERT(editor->m_draw_list != NULL && "Suspend was called outside of Begin/End.");
    int last_channel = editor->m_draw_list->_Splitter._Current;
    ImDrawList_ChannelsSetCurrent(editor->m_draw_list, editor->m_external_channel);
    if (editor->m_is_canvas_visible) ne_canvas_suspend(&editor->m_canvas);
    ImDrawList_ChannelsSetCurrent(editor->m_draw_list, last_channel);
    if ((flags & ne_suspend_flags_keep_splitter) != ne_suspend_flags_keep_splitter)
        ne_draw_list_swap_splitter(editor->m_draw_list, &editor->m_splitter);
}

void ne_editor_context_resume(ne_editor_context *editor, ne_suspend_flags flags)
{
    IM_ASSERT(editor->m_draw_list != NULL && "Resume was called outside of Begin/End.");
    if ((flags & ne_suspend_flags_keep_splitter) != ne_suspend_flags_keep_splitter)
        ne_draw_list_swap_splitter(editor->m_draw_list, &editor->m_splitter);
    int last_channel = editor->m_draw_list->_Splitter._Current;
    ImDrawList_ChannelsSetCurrent(editor->m_draw_list, editor->m_external_channel);
    if (editor->m_is_canvas_visible) ne_canvas_resume(&editor->m_canvas);
    ImDrawList_ChannelsSetCurrent(editor->m_draw_list, last_channel);
}

bool ne_editor_context_is_suspended(ne_editor_context *editor)
{ return ne_canvas_is_suspended(&editor->m_canvas); }

bool ne_editor_context_is_focused(ne_editor_context *editor)
{ return editor->m_is_focused; }

bool ne_editor_context_is_hovered(ne_editor_context *editor)
{ return editor->m_is_hovered; }

bool ne_editor_context_is_hovered_without_overlapp(ne_editor_context *editor)
{ return editor->m_is_hovered_without_overlapp; }

bool ne_editor_context_can_accept_user_input(ne_editor_context *editor)
{ return editor->m_is_focused && editor->m_is_hovered; }

void ne_editor_context_make_dirty(ne_editor_context *editor, ne_save_reason_flags reason)
{ ne_settings_make_dirty(&editor->m_settings, reason, NULL); }

void ne_editor_context_make_dirty_node(ne_editor_context *editor, ne_save_reason_flags reason, ne_node *node)
{ ne_settings_make_dirty(&editor->m_settings, reason, node); }

int ne_editor_context_count_live_nodes(ne_editor_context *editor)
{
    int result = 0;
    for (int i = 0; i < acount(editor->m_nodes); ++i)
        if (editor->m_nodes[i]->base.m_is_live) ++result;
    return result;
}

int ne_editor_context_count_live_pins(ne_editor_context *editor)
{
    int result = 0;
    for (int i = 0; i < acount(editor->m_pins); ++i)
        if (editor->m_pins[i]->base.m_is_live) ++result;
    return result;
}

int ne_editor_context_count_live_links(ne_editor_context *editor)
{
    int result = 0;
    for (int i = 0; i < acount(editor->m_links); ++i)
        if (editor->m_links[i]->base.m_is_live) ++result;
    return result;
}

ne_pin *ne_editor_context_create_pin(ne_editor_context *editor, ne_pin_id id, ne_pin_kind kind)
{
    IM_ASSERT(
        NULL == ne_editor_context_find_object(editor, (ne_object_id){ id, ne_object_type_pin })
    );
    ne_pin *pin = (ne_pin *)calloc(1, sizeof(ne_pin));
    pin->base.type = ne_object_type_pin;
    pin->base.editor = editor;
    pin->base.m_is_live = true;
    pin->m_id = id;
    pin->m_kind = kind;
    pin->m_color = IM_COL32_WHITE;
    pin->m_border_color = IM_COL32_BLACK;
    pin->m_snap_link_to_dir = true;
    apush(editor->m_pins, pin);
    return pin;
}

ne_node *ne_editor_context_create_node(ne_editor_context *editor, ne_node_id id)
{
    IM_ASSERT(
        NULL == ne_editor_context_find_object(editor, (ne_object_id){ id, ne_object_type_node })
    );
    ne_node *node = (ne_node *)calloc(1, sizeof(ne_node));
    node->base.type = ne_object_type_node;
    node->base.editor = editor;
    node->base.m_is_live = true;
    node->m_id = id;
    node->m_type = ne_node_type_node;
    node->m_color = IM_COL32_WHITE;
    node->m_border_color = IM_COL32_BLACK;
    apush(editor->m_nodes, node);

    ne_node_settings *settings = ne_settings_find_node(&editor->m_settings, id);
    if (!settings) settings = ne_settings_add_node(&editor->m_settings, id);

    ne_editor_context_update_node_state(editor, node);

    if (settings->m_group_size.x > 0 || settings->m_group_size.y > 0)
        node->m_type = ne_node_type_group;

    node->base.m_is_live = false;

    return node;
}

struct ne_link *ne_editor_context_create_link(ne_editor_context *editor, ne_link_id id)
{
    IM_ASSERT(
        NULL == ne_editor_context_find_object(editor, (ne_object_id){ id, ne_object_type_link })
    );
    struct ne_link *link = (struct ne_link *)calloc(1, sizeof(struct ne_link));
    link->base.type = ne_object_type_link;
    link->base.editor = editor;
    link->base.m_is_live = true;
    link->m_id = id;
    link->m_color = IM_COL32_WHITE;
    link->m_thickness = 1.0f;
    apush(editor->m_links, link);

    return link;
}

ne_node *ne_editor_context_find_node(ne_editor_context *editor, ne_node_id id)
{
    for (int i = 0; i < acount(editor->m_nodes); ++i)
        if (editor->m_nodes[i]->m_id == id) return editor->m_nodes[i];
    return NULL;
}

ne_pin *ne_editor_context_find_pin(ne_editor_context *editor, ne_pin_id id)
{
    for (int i = 0; i < acount(editor->m_pins); ++i)
        if (editor->m_pins[i]->m_id == id) return editor->m_pins[i];
    return NULL;
}

struct ne_link *ne_editor_context_find_link(ne_editor_context *editor, ne_link_id id)
{
    for (int i = 0; i < acount(editor->m_links); ++i)
        if (editor->m_links[i]->m_id == id) return editor->m_links[i];
    return NULL;
}

ne_object *ne_editor_context_find_object(ne_editor_context *editor, ne_object_id id)
{
    if (id.type == ne_object_type_node)
        return (ne_object *)ne_editor_context_find_node(editor, (ne_node_id)id.id);
    else if (id.type == ne_object_type_link)
        return (ne_object *)ne_editor_context_find_link(editor, (ne_link_id)id.id);
    else if (id.type == ne_object_type_pin)
        return (ne_object *)ne_editor_context_find_pin(editor, (ne_pin_id)id.id);
    else return NULL;
}

ne_node *ne_editor_context_get_node(ne_editor_context *editor, ne_node_id id)
{
    ne_node *node = ne_editor_context_find_node(editor, id);
    if (!node) node = ne_editor_context_create_node(editor, id);
    return node;
}

ne_pin *ne_editor_context_get_pin(ne_editor_context *editor, ne_pin_id id, ne_pin_kind kind)
{
    ne_pin *pin = ne_editor_context_find_pin(editor, id);
    if (pin) {
        pin->m_kind = kind;
        return pin;
    } else return ne_editor_context_create_pin(editor, id, kind);
}

struct ne_link *ne_editor_context_get_link(ne_editor_context *editor, ne_link_id id)
{
    struct ne_link *link = ne_editor_context_find_link(editor, id);
    if (link) return link;
    else return ne_editor_context_create_link(editor, id);
}

struct ne_link *ne_editor_context_find_link_at(ne_editor_context *editor, ImVec2 p)
{
    for (int i = 0; i < acount(editor->m_links); ++i)
        if (ne_link_test_hit(editor->m_links[i], p, c_link_select_thickness))
            return editor->m_links[i];

    return NULL;
}

static ImRect ne_editor_context_get_bounds_objects(ne_object **objects, int count)
{
    ImRect bounds = (ImRect){ { FLT_MAX, FLT_MAX }, { -FLT_MAX, -FLT_MAX } };

    for (int i = 0; i < count; ++i)
        if (objects[i]->m_is_live)
            ImRect_AddImRect(&bounds, ne_object_get_bounds(objects[i]));

    if (ne_rect_is_empty(bounds)) bounds = (ImRect){ { 0, 0 }, { 0, 0 } };

    return bounds;
}

ImRect ne_editor_context_get_selection_bounds(ne_editor_context *editor)
{
    return ne_editor_context_get_bounds_objects(
        editor->m_selected_objects,
        acount(editor->m_selected_objects)
    );
}

ImRect ne_editor_context_get_content_bounds(ne_editor_context *editor)
{
    ImRect bounds = (ImRect){ { FLT_MAX, FLT_MAX }, { -FLT_MAX, -FLT_MAX } };

    for (int i = 0; i < acount(editor->m_nodes); ++i)
        if (editor->m_nodes[i]->base.m_is_live)
            ImRect_AddImRect(&bounds, editor->m_nodes[i]->m_bounds);

    if (ne_rect_is_empty(bounds)) bounds = (ImRect){ { 0, 0 }, { 0, 0 } };

    return bounds;
}

ImU32 ne_editor_context_get_color(ne_editor_context *editor, ne_style_color color_index)
{
    ImVec4 c = editor->m_style.base.colors[color_index];
    return IM_COL32((int)c.x, (int)c.y, (int)c.z, (int)c.w);
}

ImU32 ne_editor_context_get_color_alpha(ne_editor_context *editor, ne_style_color color_index, float alpha)
{
    ImVec4 c = editor->m_style.base.colors[color_index];
    return IM_COL32((int)c.x, (int)c.y, (int)c.z, (int)(c.w * alpha));
}

int ne_editor_context_get_node_ids(ne_editor_context *editor, ne_node_id *nodes, int size)
{
    if (size <= 0) return 0;

    int result = 0;
    for (int i = 0; i < acount(editor->m_nodes); ++i) {
        ne_node *node = editor->m_nodes[i];
        if (!node->base.m_is_live) continue;

        *nodes++ = node->m_id;
        ++result;
        if (--size == 0) break;
    }

    return result;
}

void ne_editor_context_navigate_to(ne_editor_context *editor, ImRect bounds, bool zoom_in, float duration)
{
    ne_zoom_mode zoom_mode = zoom_in ? ne_zoom_mode_with_margin : ne_zoom_mode_none;
    ne_navigate_action_navigate_to(&editor->m_navigate_action, bounds, zoom_mode, duration, ne_navigation_reason_unknown);
}

void ne_editor_context_register_animation(ne_editor_context *editor, ne_animation *animation)
{ apush(editor->m_live_animations, animation); }

void ne_editor_context_unregister_animation(ne_editor_context *editor, ne_animation *animation)
{
    int index = -1;
    for (int i = 0; i < acount(editor->m_live_animations); ++i)
        if (editor->m_live_animations[i] == animation) {
            index = i;
            break;
        }
    if (index < 0) return;
    for (int i = index; i + 1 < acount(editor->m_live_animations); ++i)
        editor->m_live_animations[i] = editor->m_live_animations[i + 1];
    AHDR(editor->m_live_animations)->size--;
}

void ne_editor_context_flow(ne_editor_context *editor, struct ne_link *link, ne_flow_direction direction)
{
    ne_flow_animation_controller_flow(&editor->m_flow_animation_controller, link, direction);
}

void ne_editor_context_set_user_context(ne_editor_context *editor, bool global_space)
{
    (void)global_space;
    ImVec2 mouse_pos = ImGui_GetMousePos();

    ImGui_SetCursorScreenPos(ne_canvas_from_local(&editor->m_canvas, mouse_pos));
    /* Newer ImGui asserts when SetCursorScreenPos extends the window
     * boundaries without a following item; upstream predates that check. */
    ImGui_Dummy((ImVec2){ 0.0f, 0.0f });

    if (!ne_editor_context_is_suspended(editor)) {
        ImDrawList_ChannelsSetCurrent(editor->m_draw_list, c_user_channel_content);
    }
}

void ne_editor_context_enable_shortcuts(ne_editor_context *editor, bool enable)
{ editor->m_shortcuts_enabled = enable; }

bool ne_editor_context_are_shortcuts_enabled(ne_editor_context *editor)
{ return editor->m_shortcuts_enabled; }

float ne_editor_context_align_point_to_grid(ne_editor_context *editor, float p)
{
    (void)editor;
    if (!ImGui_GetIO()->KeyAlt) return p - fmodf(p, 16.0f);
    else return p;
}

ImVec2 ne_editor_context_align_point_to_grid_v2(ne_editor_context *editor, ImVec2 p)
{
    return (ImVec2){ ne_editor_context_align_point_to_grid(editor, p.x),
                     ne_editor_context_align_point_to_grid(editor, p.y) };
}

ImDrawList *ne_editor_context_get_draw_list(ne_editor_context *editor)
{ return editor->m_draw_list; }

//------------------------------------------------------------------------------
// BuildControl (upstream EditorContext::BuildControl + invisibleButtonEx lambda).

static int ne_invisible_button_ex(const char *str_id, ImVec2 size_arg, ImGuiButtonFlags extra_flags)
{
    ImGuiWindow *window = ImGui_GetCurrentWindow();
    if (window->SkipItems) return -1;

    if (size_arg.x == 0.0f || size_arg.y == 0.0f) return 0;

    const ImGuiID id = ImGui_GetID(str_id);
    ImVec2 size = ImGui_CalcItemSize(size_arg, 0.0f, 0.0f);
    const ImRect bb = (ImRect){ window->DC.CursorPos,
                                ne_vec2_add(window->DC.CursorPos, size) };
    ImGui_ItemSize(size);
    if (!ImGui_ItemAdd(bb, id)) return -1;

    int button_index = ImGui_GetCurrentContext()->ActiveIdMouseButton;

    bool hovered, held;
    bool pressed = ImGui_ButtonBehavior(bb, id, &hovered, &held, extra_flags);

    return pressed ? button_index : -1;
}

typedef struct ne_build_control_ctx
{
    ne_editor_context *editor;
    ImGuiID *active_id;
    ne_object **hot_object;
    ne_object **active_object;
    ne_object **clicked_object;
    ne_object **double_clicked_object;
    ImGuiButtonFlags extra_flags;
    ImGuiButtonFlags background_extra_flags;
} ne_build_control_ctx;

static int ne_emit_interactive_area_ex(ne_build_control_ctx *ctx, ne_object_id id, ImRect rect, ImGuiButtonFlags eflags)
{
    char id_string[33] = { 0 };
    snprintf(id_string, 32, "%p", (void *)(uintptr_t)id.id);
    ImGui_SetCursorScreenPos(rect.Min);

    int button_index = ne_invisible_button_ex(id_string, ImRect_GetSize(&rect), eflags);

    if (ImGui_IsItemActive()) *ctx->active_id = ImGui_GetActiveID();

    return button_index;
}

static void ne_check_interactions_in_area(
    ne_build_control_ctx *ctx,
    ne_object_id id,
    ImRect rect,
    ne_object *object
)
{
    if (ne_emit_interactive_area_ex(ctx, id, rect, ctx->extra_flags) >= 0)
        *ctx->clicked_object = object;
    if (!*ctx->double_clicked_object &&
        ImGui_IsMouseDoubleClicked(ctx->editor->m_config.drag_button_index) &&
        ImGui_IsItemHovered(0))
        *ctx->double_clicked_object = object;
    if (!*ctx->hot_object &&
        ImGui_IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        *ctx->hot_object = object;
    if (ImGui_IsItemActive()) *ctx->active_object = object;
}

static int ne_is_mouse_double_click_over_background(ne_build_control_ctx *ctx)
{
    if (*ctx->double_clicked_object) return -1;
    if (!ImGui_IsItemHovered(0)) return -1;
    if ((ctx->background_extra_flags & ImGuiButtonFlags_MouseButtonLeft) &&
        ImGui_IsMouseDoubleClicked(ImGuiMouseButton_Left))
        return ImGuiButtonFlags_MouseButtonLeft;
    if ((ctx->background_extra_flags & ImGuiButtonFlags_MouseButtonRight) &&
        ImGui_IsMouseDoubleClicked(ImGuiMouseButton_Right))
        return ImGuiButtonFlags_MouseButtonRight;
    if ((ctx->background_extra_flags & ImGuiButtonFlags_MouseButtonMiddle) &&
        ImGui_IsMouseDoubleClicked(ImGuiMouseButton_Middle))
        return ImGuiButtonFlags_MouseButtonMiddle;
    return -1;
}

ne_control ne_editor_context_build_control(ne_editor_context *editor, bool allow_offscreen)
{
    editor->m_is_hovered = false;
    editor->m_is_hovered_without_overlapp = false;

    const bool window_hovered = ImGui_IsWindowHovered(0);
    ImRect view_rect = ne_canvas_view_rect(&editor->m_canvas);
    const bool
        widget_hovered = ImGui_IsMouseHoveringRect(view_rect.Min, view_rect.Max);

    if (!allow_offscreen && !window_hovered && !widget_hovered)
        return ne_control_make(NULL, NULL, NULL, NULL, false, false, -1, -1);

    const ImVec2 mouse_pos = ImGui_GetMousePos();

    ImRect editor_rect = ne_canvas_view_rect(&editor->m_canvas);
    bool is_mouse_offscreen = allow_offscreen &&
                              !ne_rect_contains(editor_rect, mouse_pos);
    if (is_mouse_offscreen) {
        ImRect_Add(&editor_rect, cImFloorImVec2(mouse_pos));
        ImRect_Add(&editor_rect, (ImVec2){ ceilf(mouse_pos.x), ceilf(mouse_pos.y) });

        ImGui_PushClipRect(editor_rect.Min, editor_rect.Max, false);
    }

    ImGuiID active_id = 0;
    ne_object *hot_object = NULL;
    ne_object *active_object = NULL;
    ne_object *clicked_object = NULL;
    ne_object *double_clicked_object = NULL;

    ImGuiButtonFlags extra_flags = ImGuiButtonFlags_None;
    extra_flags |= ImGuiButtonFlags_MouseButtonLeft;
    extra_flags |= ImGuiButtonFlags_MouseButtonRight;
    extra_flags |= ImGuiButtonFlags_MouseButtonMiddle;

    ne_build_control_ctx ctx;
    ctx.editor = editor;
    ctx.active_id = &active_id;
    ctx.hot_object = &hot_object;
    ctx.active_object = &active_object;
    ctx.clicked_object = &clicked_object;
    ctx.double_clicked_object = &double_clicked_object;
    ctx.extra_flags = extra_flags;
    ctx.background_extra_flags = 0;

    /* Process live nodes and pins (reverse order). */
    for (int node_index = acount(editor->m_nodes) - 1; node_index >= 0; --node_index) {
        ne_node *node = editor->m_nodes[node_index];

        if (!node->base.m_is_live) continue;

        for (ne_pin *pin = node->m_last_pin; pin; pin = pin->m_previous_pin) {
            if (!pin->base.m_is_live) continue;

            ne_check_interactions_in_area(
                &ctx,
                (ne_object_id){ pin->m_id, ne_object_type_pin },
                pin->m_bounds,
                (ne_object *)pin
            );
        }

        if (node->m_type == ne_node_type_group) {
            ImGui_PushIDPtr((void *)(uintptr_t)node->m_id);

            static const int c_regions[] = {
                ne_node_region_top_left,    ne_node_region_top_right,
                ne_node_region_bottom_left, ne_node_region_bottom_right,
                ne_node_region_top,         ne_node_region_bottom,
                ne_node_region_left,        ne_node_region_right,
                ne_node_region_header,
            };

            for (size_t r = 0; r < sizeof(c_regions) / sizeof(*c_regions); ++r) {
                ImRect bounds = ne_node_get_region_bounds(node, c_regions[r]);
                if (ne_rect_is_empty(bounds)) continue;
                ne_check_interactions_in_area(
                    &ctx,
                    (ne_object_id){ (uintptr_t)c_regions[r], ne_object_type_none },
                    bounds,
                    (ne_object *)node
                );
            }

            ImGui_PopID();
        } else
            ne_check_interactions_in_area(
                &ctx,
                (ne_object_id){ node->m_id, ne_object_type_node },
                node->m_bounds,
                (ne_object *)node
            );
    }

    /* Links are just over background. */
    if (NULL == hot_object)
        hot_object = (ne_object *)ne_editor_context_find_link_at(editor, mouse_pos);

    ImGuiButtonFlags background_extra_flags = ImGuiButtonFlags_None;
    if (editor->m_config.drag_button_index == 0 ||
        editor->m_config.select_button_index == 0 ||
        editor->m_config.navigate_button_index == 0)
        background_extra_flags |= ImGuiButtonFlags_MouseButtonLeft;
    if (editor->m_config.drag_button_index == 1 ||
        editor->m_config.select_button_index == 1 ||
        editor->m_config.navigate_button_index == 1)
        background_extra_flags |= ImGuiButtonFlags_MouseButtonRight;
    if (editor->m_config.drag_button_index == 2 ||
        editor->m_config.select_button_index == 2 ||
        editor->m_config.navigate_button_index == 2)
        background_extra_flags |= ImGuiButtonFlags_MouseButtonMiddle;

    ctx.background_extra_flags = background_extra_flags;

    /* Check for interaction with background. */
    int background_click_button_index = ne_emit_interactive_area_ex(
        &ctx,
        (ne_object_id){ 0, ne_object_type_none },
        editor_rect,
        background_extra_flags
    );
    int background_double_click_button_index = ne_is_mouse_double_click_over_background(&ctx);
    bool is_background_active = ImGui_IsItemActive();
    bool is_background_hot = !hot_object;
    bool is_dragging = ImGui_IsMouseDragging(0, 1) ||
                       ImGui_IsMouseDragging(1, 1) || ImGui_IsMouseDragging(2, 1);

    if (background_double_click_button_index >= 0)
        background_click_button_index = -1;

    if (is_mouse_offscreen) ImGui_PopClipRect();

    struct ne_link *hot_link = hot_object ? ne_object_as_link(hot_object) : NULL;

    if (!is_dragging && is_background_active && hot_link && !editor->m_last_active_link)
        editor->m_last_active_link = hot_link;
    if (is_background_active && editor->m_last_active_link) {
        active_object = (ne_object *)editor->m_last_active_link;
        is_background_active = false;
    } else if (!is_background_active && editor->m_last_active_link)
        editor->m_last_active_link = NULL;

    if (!is_dragging && background_click_button_index >= 0 && hot_link) {
        clicked_object = (ne_object *)hot_link;
        background_click_button_index = -1;
    }

    if (!is_dragging && background_double_click_button_index >= 0 && hot_link) {
        double_clicked_object = (ne_object *)hot_link;
        background_double_click_button_index = -1;
    }

    if (active_id) editor->m_editor_active_id = active_id;

    if (ImGui_IsAnyItemActive() && ImGui_GetActiveID() != editor->m_editor_active_id)
        return ne_control_make(NULL, NULL, NULL, NULL, false, false, -1, -1);

    editor->m_is_hovered = ImGui_IsItemHovered(ImGuiHoveredFlags_RectOnly);
    editor->m_is_hovered_without_overlapp = ImGui_IsItemHovered(0);
    if (!allow_offscreen && !editor->m_is_hovered)
        return ne_control_make(NULL, NULL, NULL, NULL, false, false, -1, -1);

    if (editor->m_is_hovered_without_overlapp)
        ImGui_SetItemKeyOwner(ImGuiKey_MouseWheelY);

    return ne_control_make(
        hot_object,
        active_object,
        clicked_object,
        double_clicked_object,
        is_background_hot,
        is_background_active,
        background_click_button_index,
        background_double_click_button_index
    );
}

//------------------------------------------------------------------------------
// Control (upstream Control constructor).
//------------------------------------------------------------------------------
ne_control ne_control_make(
    ne_object *hot_object,
    ne_object *active_object,
    ne_object *clicked_object,
    ne_object *double_clicked_object,
    bool background_hot,
    bool background_active,
    int background_click_button_index,
    int background_double_click_button_index
)
{
    ne_control control;
    memset(&control, 0, sizeof(control));

    control.hot_object = hot_object;
    control.active_object = active_object;
    control.clicked_object = clicked_object;
    control.double_clicked_object = double_clicked_object;
    control.background_hot = background_hot;
    control.background_active = background_active;
    control.background_click_button_index = background_click_button_index;
    control.background_double_click_button_index = background_double_click_button_index;

    if (hot_object) {
        control.hot_node = ne_object_as_node(hot_object);
        control.hot_pin = ne_object_as_pin(hot_object);
        control.hot_link = ne_object_as_link(hot_object);
        if (control.hot_pin) control.hot_node = control.hot_pin->m_node;
    }
    if (active_object) {
        control.active_node = ne_object_as_node(active_object);
        control.active_pin = ne_object_as_pin(active_object);
        control.active_link = ne_object_as_link(active_object);
    }
    if (clicked_object) {
        control.clicked_node = ne_object_as_node(clicked_object);
        control.clicked_pin = ne_object_as_pin(clicked_object);
        control.clicked_link = ne_object_as_link(clicked_object);
    }
    if (double_clicked_object) {
        control.double_clicked_node = ne_object_as_node(double_clicked_object);
        control.double_clicked_pin = ne_object_as_pin(double_clicked_object);
        control.double_clicked_link = ne_object_as_link(double_clicked_object);
    }

    return control;
}

//------------------------------------------------------------------------------
// Node Settings / Settings (upstream NodeSettings/Settings; crude_json -> yyjson).
//------------------------------------------------------------------------------
void ne_node_settings_clear_dirty(ne_node_settings *settings)
{
    settings->m_is_dirty = false;
    settings->m_dirty_reason = ne_save_reason_none;
}

void ne_node_settings_make_dirty(ne_node_settings *settings, ne_save_reason_flags reason)
{
    settings->m_is_dirty = true;
    settings->m_dirty_reason = settings->m_dirty_reason | reason;
}

ne_node_settings *ne_settings_add_node(ne_settings *settings, ne_node_id id)
{
    ne_node_settings node;
    memset(&node, 0, sizeof(node));
    node.m_id = id;
    apush(settings->m_nodes, node);
    return &settings->m_nodes[acount(settings->m_nodes) - 1];
}

ne_node_settings *ne_settings_find_node(ne_settings *settings, ne_node_id id)
{
    for (int i = 0; i < acount(settings->m_nodes); ++i)
        if (settings->m_nodes[i].m_id == id) return &settings->m_nodes[i];
    return NULL;
}

void ne_settings_remove_node(ne_settings *settings, ne_node_id id)
{
    ne_node_settings *node = ne_settings_find_node(settings, id);
    if (!node) return;

    ne_node_settings reset;
    memset(&reset, 0, sizeof(reset));
    reset.m_id = id;
    *node = reset;
}

void ne_settings_clear_dirty(ne_settings *settings, ne_node *node)
{
    if (node) {
        ne_node_settings *node_settings = ne_settings_find_node(settings, node->m_id);
        IM_ASSERT(node_settings);
        ne_node_settings_clear_dirty(node_settings);
    } else {
        settings->m_is_dirty = false;
        settings->m_dirty_reason = ne_save_reason_none;

        for (int i = 0; i < acount(settings->m_nodes); ++i)
            ne_node_settings_clear_dirty(&settings->m_nodes[i]);
    }
}

void ne_settings_make_dirty(ne_settings *settings, ne_save_reason_flags reason, ne_node *node)
{
    settings->m_is_dirty = true;
    settings->m_dirty_reason = settings->m_dirty_reason | reason;

    if (node) {
        ne_node_settings *node_settings = ne_settings_find_node(settings, node->m_id);
        IM_ASSERT(node_settings);
        ne_node_settings_make_dirty(node_settings, reason);
    }
}

/* upstream serializeObjectId lambda */
static void ne_serialize_object_id(char *out, size_t out_size, ne_object_id id)
{
    if (id.type == ne_object_type_node)
        snprintf(out, out_size, "node:%lu", (unsigned long)id.id);
    else if (id.type == ne_object_type_link)
        snprintf(out, out_size, "link:%lu", (unsigned long)id.id);
    else if (id.type == ne_object_type_pin)
        snprintf(out, out_size, "pin:%lu", (unsigned long)id.id);
    else snprintf(out, out_size, "%lu", (unsigned long)id.id);
}

/* upstream deserializeObjectId lambda */
static ne_object_id ne_deserialize_object_id(const char *str)
{
    const char *separator = strchr(str, ':');
    const char *id_start = separator ? separator + 1 : str;
    uintptr_t id = (uintptr_t)strtoull(id_start, NULL, 10);

    if (separator && strncmp(str, "node", (size_t)(separator - str)) == 0)
        return (ne_object_id){ id, ne_object_type_node };
    else if (separator && strncmp(str, "link", (size_t)(separator - str)) == 0)
        return (ne_object_id){ id, ne_object_type_link };
    else if (separator && strncmp(str, "pin", (size_t)(separator - str)) == 0)
        return (ne_object_id){ id, ne_object_type_pin };
    else
        return (ne_object_id){ id, ne_object_type_node }; /* fallback to old format */
}

/* upstream tryParseVector lambda (NodeSettings + Settings) */
static bool ne_try_parse_vector(yyjson_val *v, ImVec2 *result)
{
    if (yyjson_is_obj(v)) {
        yyjson_val *x_value = yyjson_obj_get(v, "x");
        yyjson_val *y_value = yyjson_obj_get(v, "y");

        if (yyjson_is_real(x_value) && yyjson_is_real(y_value)) {
            result->x = (float)yyjson_get_real(x_value);
            result->y = (float)yyjson_get_real(y_value);
            return true;
        }
    }

    return false;
}

static yyjson_mut_val *ne_node_settings_to_mut(const ne_node_settings *settings, yyjson_mut_doc *doc)
{
    yyjson_mut_val *root = yyjson_mut_obj(doc);

    yyjson_mut_val *location = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_real(doc, location, "x", settings->m_location.x);
    yyjson_mut_obj_add_real(doc, location, "y", settings->m_location.y);
    yyjson_mut_obj_add_val(doc, root, "location", location);

    if (settings->m_group_size.x > 0 || settings->m_group_size.y > 0) {
        yyjson_mut_val *group_size = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_real(doc, group_size, "x", settings->m_group_size.x);
        yyjson_mut_obj_add_real(doc, group_size, "y", settings->m_group_size.y);
        yyjson_mut_obj_add_val(doc, root, "group_size", group_size);
    }

    return root;
}

char *ne_node_settings_serialize(const ne_node_settings *settings)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(doc, ne_node_settings_to_mut(settings, doc));
    size_t len = 0;
    char *str = yyjson_mut_write(doc, 0, &len);
    yyjson_mut_doc_free(doc);
    return str;
}

static bool ne_node_settings_parse_val(yyjson_val *data, ne_node_settings *result)
{
    if (!yyjson_is_obj(data)) return false;

    if (!ne_try_parse_vector(yyjson_obj_get(data, "location"), &result->m_location))
        return false;

    yyjson_val *group_size = yyjson_obj_get(data, "group_size");
    if (group_size && !ne_try_parse_vector(group_size, &result->m_group_size))
        return false;

    return true;
}

bool ne_node_settings_parse_str(const char *str, ne_node_settings *settings)
{
    yyjson_doc *doc = yyjson_read(str, strlen(str), 0);
    if (!doc) return false;

    bool ok = ne_node_settings_parse_val(yyjson_doc_get_root(doc), settings);
    yyjson_doc_free(doc);
    return ok;
}

char *ne_settings_serialize(const ne_settings *settings)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_val *nodes = yyjson_mut_obj(doc);
    for (int i = 0; i < acount(settings->m_nodes); ++i) {
        ne_node_settings *node = &settings->m_nodes[i];
        if (node->m_was_used) {
            char key[64];
            ne_serialize_object_id(key, sizeof(key), (ne_object_id){ node->m_id, ne_object_type_node });
            yyjson_mut_obj_add_val(doc, nodes, key, ne_node_settings_to_mut(node, doc));
        }
    }
    yyjson_mut_obj_add_val(doc, root, "nodes", nodes);

    yyjson_mut_val *selection = yyjson_mut_arr(doc);
    for (int i = 0; i < acount(settings->m_selection); ++i) {
        char key[64];
        ne_serialize_object_id(key, sizeof(key), settings->m_selection[i]);
        yyjson_mut_arr_add_str(doc, selection, key);
    }
    yyjson_mut_obj_add_val(doc, root, "selection", selection);

    yyjson_mut_val *view = yyjson_mut_obj(doc);
    yyjson_mut_val *scroll = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_real(doc, scroll, "x", settings->m_view_scroll.x);
    yyjson_mut_obj_add_real(doc, scroll, "y", settings->m_view_scroll.y);
    yyjson_mut_obj_add_val(doc, view, "scroll", scroll);
    yyjson_mut_obj_add_real(doc, view, "zoom", settings->m_view_zoom);
    yyjson_mut_val *visible_rect = yyjson_mut_obj(doc);
    yyjson_mut_val *vr_min = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_real(doc, vr_min, "x", settings->m_visible_rect.Min.x);
    yyjson_mut_obj_add_real(doc, vr_min, "y", settings->m_visible_rect.Min.y);
    yyjson_mut_obj_add_val(doc, visible_rect, "min", vr_min);
    yyjson_mut_val *vr_max = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_real(doc, vr_max, "x", settings->m_visible_rect.Max.x);
    yyjson_mut_obj_add_real(doc, vr_max, "y", settings->m_visible_rect.Max.y);
    yyjson_mut_obj_add_val(doc, visible_rect, "max", vr_max);
    yyjson_mut_obj_add_val(doc, view, "visible_rect", visible_rect);
    yyjson_mut_obj_add_val(doc, root, "view", view);

    size_t len = 0;
    char *str = yyjson_mut_write(doc, 0, &len);
    yyjson_mut_doc_free(doc);
    return str;
}

bool ne_settings_parse_str(const char *str, ne_settings *settings)
{
    /* Deep copy (upstream: Settings result = settings; std::vector deep copy). */
    ne_settings result;
    memset(&result, 0, sizeof(result));
    for (int i = 0; i < acount(settings->m_nodes); ++i)
        apush(result.m_nodes, settings->m_nodes[i]);
    for (int i = 0; i < acount(settings->m_selection); ++i)
        apush(result.m_selection, settings->m_selection[i]);
    result.m_is_dirty = settings->m_is_dirty;
    result.m_dirty_reason = settings->m_dirty_reason;
    result.m_view_scroll = settings->m_view_scroll;
    result.m_view_zoom = settings->m_view_zoom;
    result.m_visible_rect = settings->m_visible_rect;

    yyjson_doc *doc = yyjson_read(str, strlen(str), 0);
    if (!doc) {
        afree(result.m_nodes);
        afree(result.m_selection);
        return false;
    }

    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        afree(result.m_nodes);
        afree(result.m_selection);
        return false;
    }

    yyjson_val *nodes_value = yyjson_obj_get(root, "nodes");
    if (yyjson_is_obj(nodes_value)) {
        yyjson_obj_iter iter = yyjson_obj_iter_with(nodes_value);
        yyjson_val *key;
        while ((key = yyjson_obj_iter_next(&iter))) {
            yyjson_val *val = yyjson_obj_iter_get_val(key);
            ne_node_id id = ne_deserialize_object_id(yyjson_get_str(key)).id;

            ne_node_settings *node_settings = ne_settings_find_node(&result, id);
            if (!node_settings)
                node_settings = ne_settings_add_node(&result, id);

            ne_node_settings_parse_val(val, node_settings);
        }
    }

    yyjson_val *selection_value = yyjson_obj_get(root, "selection");
    if (yyjson_is_arr(selection_value)) {
        afree(result.m_selection);
        result.m_selection = NULL;
        size_t n = yyjson_arr_size(selection_value);
        for (size_t i = 0; i < n; ++i) {
            yyjson_val *selection = yyjson_arr_get(selection_value, i);
            if (yyjson_is_str(selection))
                apush(result.m_selection, ne_deserialize_object_id(yyjson_get_str(selection)));
        }
    }

    yyjson_val *view_value = yyjson_obj_get(root, "view");
    if (yyjson_is_obj(view_value)) {
        yyjson_val *view_scroll_value = yyjson_obj_get(view_value, "scroll");
        yyjson_val *view_zoom_value = yyjson_obj_get(view_value, "zoom");

        if (!ne_try_parse_vector(view_scroll_value, &result.m_view_scroll))
            result.m_view_scroll = (ImVec2){ 0, 0 };

        result.m_view_zoom = yyjson_is_real(view_zoom_value)
                               ? (float)yyjson_get_real(view_zoom_value)
                               : 1.0f;

        yyjson_val *visible_rect = yyjson_obj_get(view_value, "visible_rect");
        if (!visible_rect ||
            !ne_try_parse_vector(
                yyjson_obj_get(visible_rect, "min"),
                &result.m_visible_rect.Min
            ) ||
            !ne_try_parse_vector(
                yyjson_obj_get(visible_rect, "max"),
                &result.m_visible_rect.Max
            ))
            result.m_visible_rect = (ImRect){ { 0, 0 }, { 0, 0 } };
    }

    yyjson_doc_free(doc);

    afree(settings->m_nodes);
    afree(settings->m_selection);
    settings->m_nodes = result.m_nodes;
    settings->m_selection = result.m_selection;
    settings->m_is_dirty = result.m_is_dirty;
    settings->m_dirty_reason = result.m_dirty_reason;
    settings->m_view_scroll = result.m_view_scroll;
    settings->m_view_zoom = result.m_view_zoom;
    settings->m_visible_rect = result.m_visible_rect;

    return true;
}

//------------------------------------------------------------------------------
// Animation (upstream Animation / NavigateAnimation / FlowAnimation /
// FlowAnimationController).
//------------------------------------------------------------------------------
bool ne_animation_is_playing(const ne_animation *animation)
{ return animation->state == ne_animation_playing; }

float ne_animation_get_progress(const ne_animation *animation)
{ return animation->m_time / animation->m_duration; }

void ne_animation_play(ne_animation *animation, float duration)
{
    if (ne_animation_is_playing(animation)) ne_animation_stop(animation);

    animation->state = ne_animation_playing;
    if (duration < 0) duration = 0.0f;

    animation->m_time = 0.0f;
    animation->m_duration = duration;

    if (animation->on_play) animation->on_play(animation);

    ne_editor_context_register_animation(animation->editor, animation);

    if (duration == 0.0f) ne_animation_finish(animation);
}

void ne_animation_stop(ne_animation *animation)
{
    if (!ne_animation_is_playing(animation)) return;

    animation->state = ne_animation_stopped;

    ne_editor_context_unregister_animation(animation->editor, animation);

    if (animation->on_stop) animation->on_stop(animation);
}

void ne_animation_finish(ne_animation *animation)
{
    if (!ne_animation_is_playing(animation)) return;

    if (animation->on_finish) animation->on_finish(animation);

    ne_animation_stop(animation);
}

void ne_animation_update(ne_animation *animation)
{
    if (!ne_animation_is_playing(animation)) return;

    animation->m_time += ne_imax_f(0.0f, ImGui_GetIO()->DeltaTime);
    if (animation->m_time < animation->m_duration) {
        const float progress = ne_animation_get_progress(animation);
        if (animation->on_update) animation->on_update(animation, progress);
    } else {
        if (animation->on_finish) animation->on_finish(animation);
        ne_animation_stop(animation);
    }
}

// Navigate animation.
static void ne_navigate_animation_on_update(ne_animation *base, float progress)
{
    ne_navigate_animation *animation = (ne_navigate_animation *)base;
    ImRect current;
    current.Min = ne_ease_out_quad_v2(
        animation->m_start.Min,
        ne_vec2_sub(animation->m_target.Min, animation->m_start.Min),
        progress
    );
    current.Max = ne_ease_out_quad_v2(
        animation->m_start.Max,
        ne_vec2_sub(animation->m_target.Max, animation->m_start.Max),
        progress
    );
    ne_navigate_action_set_view_rect(animation->action, current);
}

static void ne_navigate_animation_on_stop(ne_animation *base)
{
    ne_navigate_animation *animation = (ne_navigate_animation *)base;
    ne_editor_context_make_dirty(animation->base.editor, ne_save_reason_navigation);
}

static void ne_navigate_animation_on_finish(ne_animation *base)
{
    ne_navigate_animation *animation = (ne_navigate_animation *)base;
    ne_navigate_action_set_view_rect(animation->action, animation->m_target);
    ne_editor_context_make_dirty(animation->base.editor, ne_save_reason_navigation);
}

void ne_navigate_animation_init(
    ne_navigate_animation *animation,
    ne_editor_context *editor,
    ne_navigate_action *action
)
{
    memset(animation, 0, sizeof(*animation));
    animation->base.state = ne_animation_stopped;
    animation->base.editor = editor;
    animation->base.on_update = ne_navigate_animation_on_update;
    animation->base.on_stop = ne_navigate_animation_on_stop;
    animation->base.on_finish = ne_navigate_animation_on_finish;
    animation->action = action;
}

static void ne_navigate_animation_navigate_to(ne_navigate_animation *animation, ImRect target, float duration)
{
    ne_animation_stop(&animation->base);

    animation->m_start = ne_navigate_action_get_view_rect(animation->action);
    animation->m_target = target;

    ImVec2 min_offset = ne_vec2_sub(animation->m_target.Min, animation->m_start.Min);
    ImVec2 max_offset = ne_vec2_sub(animation->m_target.Max, animation->m_start.Max);
    const float epsilon = 1e-4f;
    if (fabsf(min_offset.x) < epsilon && fabsf(min_offset.y) < epsilon &&
        fabsf(max_offset.x) < epsilon && fabsf(max_offset.y) < epsilon)
        duration = 0;

    ne_animation_play(&animation->base, duration);
}

// Flow animation.
static bool ne_flow_animation_is_link_valid(const ne_flow_animation *animation)
{ return animation->m_link && animation->m_link->base.m_is_live; }

static bool ne_flow_animation_is_path_valid(const ne_flow_animation *animation)
{
    return acount(animation->m_path) > 1 && animation->m_path_length > 0.0f &&
           ne_vec2_eq(animation->m_link->m_start, animation->m_last_start) &&
           ne_vec2_eq(animation->m_link->m_end, animation->m_last_end);
}

static void ne_flow_animation_clear_path(ne_flow_animation *animation)
{
    afree(animation->m_path);
    animation->m_path = NULL;
    animation->m_path_length = 0.0f;
}

typedef struct ne_flow_animation_collect_ctx
{
    ne_flow_animation *animation;
} ne_flow_animation_collect_ctx;

static void ne_flow_animation_collect_point(
    struct ne_cubic_bezier_fixed_step_sample *result,
    void *user_pointer
)
{
    ne_flow_animation *animation = ((ne_flow_animation_collect_ctx *)user_pointer)
                                       ->animation;
    ne_curve_point point;
    point.distance = result->Length;
    point.point = result->Point;
    apush(animation->m_path, point);
}

static void ne_flow_animation_update_path(ne_flow_animation *animation)
{
    if (!ne_flow_animation_is_link_valid(animation)) {
        ne_flow_animation_clear_path(animation);
        return;
    }

    struct ne_cubic_bezier_points curve = ne_link_get_curve(animation->m_link);

    animation->m_last_start = animation->m_link->m_start;
    animation->m_last_end = animation->m_link->m_end;
    animation->m_path_length = ne_cubic_bezier_length(
        curve.P0,
        curve.P1,
        curve.P2,
        curve.P3
    );

    const float step = ne_imax_f(animation->m_marker_distance * 0.5f, 15.0f);

    ne_flow_animation_clear_path(animation);
    ne_flow_animation_collect_ctx ctx;
    ctx.animation = animation;
    ne_cubic_bezier_fixed_step(
        ne_flow_animation_collect_point,
        &ctx,
        curve.P0,
        curve.P1,
        curve.P2,
        curve.P3,
        step,
        false,
        0.5f,
        0.001f
    );
}

static ImVec2 ne_flow_animation_sample_path(const ne_flow_animation *animation, float distance)
{
    int end_index = -1;
    for (int i = 0; i < acount(animation->m_path); ++i)
        if (distance < animation->m_path[i].distance) {
            end_index = i;
            break;
        }

    if (end_index < 0) end_index = acount(animation->m_path) - 1;
    else if (end_index == 0) end_index = 1;

    const ne_curve_point *start = &animation->m_path[end_index - 1];
    const ne_curve_point *end = &animation->m_path[end_index];
    const float t = (distance - start->distance) / (end->distance - start->distance);

    return ne_vec2_add(start->point, ne_vec2_scale(ne_vec2_sub(end->point, start->point), t));
}

void ne_flow_animation_draw(ne_flow_animation *animation, ImDrawList *draw_list)
{
    if (!ne_animation_is_playing(&animation->base) ||
        !ne_flow_animation_is_link_valid(animation) ||
        !ne_object_is_visible((ne_object *)animation->m_link))
        return;

    if (!ne_flow_animation_is_path_valid(animation))
        ne_flow_animation_update_path(animation);

    animation->m_offset = fmodf(animation->m_offset, animation->m_marker_distance);
    if (animation->m_offset < 0)
        animation->m_offset += animation->m_marker_distance;

    const float progress = ne_animation_get_progress(&animation->base);

    const float flow_alpha = 1.0f - progress * progress;
    const ImU32 flow_color = ne_editor_context_get_color_alpha(
        animation->base.editor,
        ne_style_color_flow,
        flow_alpha
    );

    ne_link_draw_color(animation->m_link, draw_list, flow_color, 2.0f);

    if (ne_flow_animation_is_path_valid(animation)) {
        const float marker_alpha = powf(1.0f - progress, 0.35f);
        const float marker_radius = 4.0f * (1.0f - progress) + 2.0f;
        const ImU32 marker_color = ne_editor_context_get_color_alpha(
            animation->base.editor,
            ne_style_color_flow_marker,
            marker_alpha
        );

        for (float d = animation->m_offset; d < animation->m_path_length;
             d += animation->m_marker_distance)
            ImDrawList_AddCircleFilled(
                draw_list,
                ne_flow_animation_sample_path(animation, d),
                marker_radius,
                marker_color,
                0
            );
    }
}

static void ne_flow_animation_on_update(ne_animation *base, float progress);
static void ne_flow_animation_on_stop(ne_animation *base);

void ne_flow_animation_init(ne_flow_animation *animation, ne_flow_animation_controller *controller)
{
    memset(animation, 0, sizeof(*animation));
    animation->base.state = ne_animation_stopped;
    animation->base.editor = controller->base.editor;
    animation->base.on_update = ne_flow_animation_on_update;
    animation->base.on_stop = ne_flow_animation_on_stop;
    animation->controller = controller;
}

void ne_flow_animation_flow(
    ne_flow_animation *animation,
    struct ne_link *link,
    float marker_distance,
    float speed,
    float duration
)
{
    ne_animation_stop(&animation->base);

    if (animation->m_link != link) {
        animation->m_offset = 0.0f;
        ne_flow_animation_clear_path(animation);
    }

    if (animation->m_marker_distance != marker_distance)
        ne_flow_animation_clear_path(animation);

    animation->m_marker_distance = marker_distance;
    animation->m_speed = speed;
    animation->m_link = link;

    ne_animation_play(&animation->base, duration);
}

static void ne_flow_animation_on_update(ne_animation *base, float progress)
{
    (void)progress;
    ne_flow_animation *animation = (ne_flow_animation *)base;
    animation->m_offset += animation->m_speed * ImGui_GetIO()->DeltaTime;
}

static void ne_flow_animation_on_stop(ne_animation *base)
{
    ne_flow_animation *animation = (ne_flow_animation *)base;
    /* upstream Controller->Release(this) is a no-op */
    (void)animation;
}

// Flow animation controller.
static void ne_flow_animation_controller_draw_cb(ne_animation_controller *base, ImDrawList *draw_list);

void ne_flow_animation_controller_init(ne_flow_animation_controller *controller, ne_editor_context *editor)
{
    memset(controller, 0, sizeof(*controller));
    controller->base.editor = editor;
    controller->base.draw = ne_flow_animation_controller_draw_cb;
}

void ne_flow_animation_controller_free(ne_flow_animation_controller *controller)
{
    for (int i = 0; i < acount(controller->m_animations); ++i)
        free(controller->m_animations[i]);
    afree(controller->m_animations);
    afree(controller->m_free_pool);
}

static ne_flow_animation *ne_flow_animation_controller_get_or_create(
    ne_flow_animation_controller *controller,
    struct ne_link *link
)
{
    for (int i = 0; i < acount(controller->m_animations); ++i)
        if (controller->m_animations[i]->m_link == link)
            return controller->m_animations[i];

    if (acount(controller->m_free_pool) > 0) {
        ne_flow_animation *animation = controller->m_free_pool[acount(controller->m_free_pool) - 1];
        AHDR(controller->m_free_pool)->size--;
        return animation;
    }

    ne_flow_animation *animation = (ne_flow_animation *)malloc(sizeof(ne_flow_animation));
    ne_flow_animation_init(animation, controller);
    apush(controller->m_animations, animation);

    return animation;
}

void ne_flow_animation_controller_flow(
    ne_flow_animation_controller *controller,
    struct ne_link *link,
    ne_flow_direction direction
)
{
    if (!link || !link->base.m_is_live) return;

    ne_style *editor_style = &controller->base.editor->m_style.base;

    ne_flow_animation *animation = ne_flow_animation_controller_get_or_create(controller, link);

    float speed_direction = 1.0f;
    if (direction == ne_flow_direction_backward) speed_direction = -1.0f;

    ne_flow_animation_flow(
        animation,
        link,
        editor_style->flow_marker_distance,
        editor_style->flow_speed * speed_direction,
        editor_style->flow_duration
    );
}

static void ne_flow_animation_controller_draw_cb(ne_animation_controller *base, ImDrawList *draw_list)
{
    ne_flow_animation_controller *controller = (ne_flow_animation_controller *)base;

    if (acount(controller->m_animations) == 0) return;

    ImDrawList_ChannelsSetCurrent(draw_list, c_link_channel_flow);

    for (int i = 0; i < acount(controller->m_animations); ++i)
        ne_flow_animation_draw(controller->m_animations[i], draw_list);
}

//------------------------------------------------------------------------------
// Action dispatch (upstream EditorAction virtual methods + As* casts).
//------------------------------------------------------------------------------
const char *ne_action_get_name(ne_editor_action *action)
{ return action->get_name(action); }
ne_accept_result ne_action_accept(ne_editor_action *action, const ne_control *control)
{ return action->accept(action, control); }
bool ne_action_process(ne_editor_action *action, const ne_control *control)
{ return action->process(action, control); }
void ne_action_reject(ne_editor_action *action) { action->reject(action); }
int ne_action_get_cursor(ne_editor_action *action)
{ return action->get_cursor(action); }
bool ne_action_is_dragging(ne_editor_action *action)
{ return action->is_dragging(action); }
void ne_action_show_metrics(ne_editor_action *action)
{ action->show_metrics(action); }

ne_navigate_action *ne_action_as_navigate(ne_editor_action *action)
{
    return action && action->type == ne_action_type_navigate
             ? (ne_navigate_action *)action
             : NULL;
}
ne_size_action *ne_action_as_size(ne_editor_action *action)
{
    return action && action->type == ne_action_type_size ? (ne_size_action *)action
                                                         : NULL;
}
ne_drag_action *ne_action_as_drag(ne_editor_action *action)
{
    return action && action->type == ne_action_type_drag ? (ne_drag_action *)action
                                                         : NULL;
}
ne_select_action *ne_action_as_select(ne_editor_action *action)
{
    return action && action->type == ne_action_type_select ? (ne_select_action *)action
                                                           : NULL;
}
ne_create_item_action *ne_action_as_create_item(ne_editor_action *action)
{
    return action && action->type == ne_action_type_create_item
             ? (ne_create_item_action *)action
             : NULL;
}
ne_delete_items_action *ne_action_as_delete_items(ne_editor_action *action)
{
    return action && action->type == ne_action_type_delete_items
             ? (ne_delete_items_action *)action
             : NULL;
}
ne_context_menu_action *ne_action_as_context_menu(ne_editor_action *action)
{
    return action && action->type == ne_action_type_context_menu
             ? (ne_context_menu_action *)action
             : NULL;
}
ne_shortcut_action *ne_action_as_shortcut(ne_editor_action *action)
{
    return action && action->type == ne_action_type_shortcut
             ? (ne_shortcut_action *)action
             : NULL;
}

static bool ne_size_action_is_dragging(ne_editor_action *action);
static bool ne_drag_action_is_dragging(ne_editor_action *action);
static bool ne_select_action_is_dragging(ne_editor_action *action);
static bool ne_create_item_action_is_dragging(ne_editor_action *action);

static const char *ne_get_object_name(ne_object *object)
{
    if (!object) return "";
    else if (ne_object_as_node(object)) return "Node";
    else if (ne_object_as_pin(object)) return "Pin";
    else if (ne_object_as_link(object)) return "Link";
    else return "";
}

//------------------------------------------------------------------------------
// Navigate Action.
//------------------------------------------------------------------------------
static const float ne_default_zoom_levels[] = {
    0.1f,  0.15f, 0.20f, 0.25f, 0.33f, 0.5f, 0.75f, 1.0f, 1.25f,
    1.50f, 2.0f,  2.5f,  3.0f,  4.0f,  5.0f, 6.0f,  7.0f, 8.0f
};
static const int ne_default_zoom_level_count = (int)(sizeof(ne_default_zoom_levels) /
                                                     sizeof(*ne_default_zoom_levels));

static const char *ne_navigate_action_get_name(ne_editor_action *action)
{
    (void)action;
    return "Navigate";
}

static float ne_navigate_action_match_zoom_index(ne_navigate_action *action, int direction);

void ne_navigate_action_init(ne_navigate_action *action, ne_editor_context *editor, ne_canvas *canvas)
{
    memset(action, 0, sizeof(*action));
    action->base.type = ne_action_type_navigate;
    action->base.editor = editor;
    action->base.get_name = ne_navigate_action_get_name;
    action->base.accept = (ne_accept_result (*)(ne_editor_action *, const ne_control *))ne_navigate_action_accept;
    action->base.process = (bool (*)(ne_editor_action *, const ne_control *))ne_navigate_action_process;
    action->base.reject = NULL;
    action->base.get_cursor = NULL;
    action->base.is_dragging = NULL;
    action->base.show_metrics = (void (*)(ne_editor_action *))ne_navigate_action_show_metrics;

    action->m_zoom = 1.0f;
    action->m_canvas = canvas;
    action->m_reason = ne_navigation_reason_unknown;
    action->m_zoom_levels = acount(editor->m_config.custom_zoom_levels) > 0
                              ? editor->m_config.custom_zoom_levels
                              : ne_default_zoom_levels;
    action->m_zoom_level_count = acount(editor->m_config.custom_zoom_levels) > 0
                                   ? acount(editor->m_config.custom_zoom_levels)
                                   : ne_default_zoom_level_count;

    ne_navigate_animation_init(&action->m_animation, editor, action);
}

static float ne_navigate_action_get_next_zoom(ne_navigate_action *action, float steps);

static bool ne_navigate_action_handle_zoom(ne_navigate_action *action, const ne_control *control)
{
    (void)control;

    const ne_editor_action *current_action = action->base.editor->m_current_action;
    const bool allow_offscreen = current_action &&
                                 ne_action_is_dragging((ne_editor_action *)current_action);

    ImGuiIO *io = ImGui_GetIO();

    if (!io->MouseWheel ||
        (!allow_offscreen &&
         !ne_editor_context_is_hovered_without_overlapp(action->base.editor)))
        return false;

    const ImVec2 saved_scroll = action->m_scroll;
    const float saved_zoom = action->m_zoom;

    ne_animation_finish(&action->m_animation.base);

    const ImVec2 mouse_pos = io->MousePos;
    const float new_zoom = ne_navigate_action_get_next_zoom(action, io->MouseWheel);

    ne_canvas_view old_view = ne_navigate_action_get_view(action);
    action->m_zoom = new_zoom;
    ne_canvas_view new_view = ne_navigate_action_get_view(action);

    ImVec2 screen_pos = ne_canvas_from_local_view(action->m_canvas, mouse_pos, old_view);
    ImVec2 canvas_pos = ne_canvas_to_local_view(action->m_canvas, screen_pos, new_view);

    ImVec2 offset = ne_vec2_scale(ne_vec2_sub(canvas_pos, mouse_pos), action->m_zoom);
    ImVec2 target_scroll = ne_vec2_sub(action->m_scroll, offset);

    ImRect visible_rect = ne_navigate_action_get_view_rect(action);

    if (ne_vec2_ne(action->m_scroll, saved_scroll) || action->m_zoom != saved_zoom ||
        ne_vec2_ne(action->m_visible_rect.Min, visible_rect.Min) ||
        ne_vec2_ne(action->m_visible_rect.Max, visible_rect.Max)) {
        action->m_scroll = saved_scroll;
        action->m_zoom = saved_zoom;
        action->m_visible_rect = visible_rect;

        ne_editor_context_make_dirty(action->base.editor, ne_save_reason_navigation);
    }

    ImRect target_rect = ne_canvas_calc_view_rect(
        action->m_canvas,
        ne_canvas_view_make(ne_vec2_neg(target_scroll), new_zoom)
    );

    ne_navigate_action_navigate_to(action, target_rect, ne_zoom_mode_exact, c_mouse_zoom_duration, ne_navigation_reason_mouse_zoom);

    return true;
}

ne_accept_result ne_navigate_action_accept(ne_navigate_action *action, const ne_control *control)
{
    IM_ASSERT(!action->m_is_active);

    if (action->m_is_active) return ne_accept_false;

    if (ne_editor_context_can_accept_user_input(action->base.editor) &&
        ImGui_IsMouseDragging(action->base.editor->m_config.navigate_button_index, 0.0f)) {
        action->m_is_active = true;
        action->m_scroll_start = action->m_scroll;
        action->m_scroll_delta = ImGui_GetMouseDragDelta(
            action->base.editor->m_config.navigate_button_index,
            0.0f
        );
        action->m_scroll = ne_vec2_sub(
            action->m_scroll_start,
            ne_vec2_scale(action->m_scroll_delta, action->m_zoom)
        );
    }

    ImGuiIO *io = ImGui_GetIO();

    if (ne_editor_context_can_accept_user_input(action->base.editor) &&
        ImGui_IsKeyPressed(ImGuiKey_F) &&
        ne_editor_context_are_shortcuts_enabled(action->base.editor)) {
        const ne_zoom_mode zoom_mode = io->KeyShift ? ne_zoom_mode_with_margin
                                                    : ne_zoom_mode_none;

        ne_object *hot_object = NULL;
        if (control->hot_object) {
            ne_pin *pin = ne_object_as_pin(control->hot_object);
            if (pin) hot_object = (ne_object *)pin->m_node;
            else hot_object = control->hot_object;
        } else if (control->background_hot) {
            ne_node *node = ne_editor_context_find_node_at(action->base.editor, io->MousePos);
            if (ne_is_group(node)) hot_object = (ne_object *)node;
        }

        bool navigate_to_content = false;
        if (acount(action->base.editor->m_selected_objects) > 0) {
            if (action->m_reason != ne_navigation_reason_selection ||
                action->m_last_selection_id != action->base.editor->m_selection_id ||
                (zoom_mode != ne_zoom_mode_none)) {
                action->m_last_selection_id = action->base.editor->m_selection_id;
                ne_navigate_action_navigate_to(
                    action,
                    ne_editor_context_get_selection_bounds(action->base.editor),
                    zoom_mode,
                    -1.0f,
                    ne_navigation_reason_selection
                );
            } else navigate_to_content = true;
        } else if (hot_object) {
            if (action->m_reason != ne_navigation_reason_object ||
                action->m_last_object != hot_object ||
                (zoom_mode != ne_zoom_mode_none)) {
                action->m_last_object = hot_object;
                ne_navigate_action_navigate_to(
                    action,
                    ne_object_get_bounds(hot_object),
                    zoom_mode,
                    -1.0f,
                    ne_navigation_reason_object
                );
            } else navigate_to_content = true;
        } else navigate_to_content = true;

        if (navigate_to_content)
            ne_navigate_action_navigate_to(
                action,
                ne_editor_context_get_content_bounds(action->base.editor),
                ne_zoom_mode_with_margin,
                -1.0f,
                ne_navigation_reason_content
            );
    }

    ImRect visible_rect = ne_navigate_action_get_view_rect(action);
    if (ne_vec2_ne(action->m_visible_rect.Min, visible_rect.Min) ||
        ne_vec2_ne(action->m_visible_rect.Max, visible_rect.Max)) {
        action->m_visible_rect = visible_rect;
        ne_editor_context_make_dirty(action->base.editor, ne_save_reason_navigation);
    }

    if (ne_navigate_action_handle_zoom(action, control)) return ne_accept_true;

    return action->m_is_active ? ne_accept_true : ne_accept_false;
}

bool ne_navigate_action_process(ne_navigate_action *action, const ne_control *control)
{
    (void)control;

    if (!action->m_is_active) return false;

    if (ImGui_IsMouseDragging(action->base.editor->m_config.navigate_button_index, 0.0f)) {
        action->m_scroll_delta = ImGui_GetMouseDragDelta(
            action->base.editor->m_config.navigate_button_index,
            0.0f
        );
        action->m_scroll = ne_vec2_sub(
            action->m_scroll_start,
            ne_vec2_scale(action->m_scroll_delta, action->m_zoom)
        );
        action->m_visible_rect = ne_navigate_action_get_view_rect(action);
    } else {
        if (ne_vec2_ne(action->m_scroll, action->m_scroll_start))
            ne_editor_context_make_dirty(action->base.editor, ne_save_reason_navigation);

        action->m_is_active = false;
    }

    return action->m_is_active;
}

void ne_navigate_action_show_metrics(ne_navigate_action *action)
{
    ImGui_Text("%s:", ne_action_get_name(&action->base));
    ImGui_Text("    Active: %s", action->m_is_active ? "yes" : "no");
    ImGui_Text("    Scroll: { x=%g y=%g }", action->m_scroll.x, action->m_scroll.y);
    ImGui_Text("    Zoom: %g", action->m_zoom);
    ImGui_Text(
        "    Visible Rect: { l=%g t=%g, r=%g b=%g w=%g h=%g }",
        action->m_visible_rect.Min.x,
        action->m_visible_rect.Min.y,
        action->m_visible_rect.Max.x,
        action->m_visible_rect.Max.y,
        action->m_visible_rect.Max.x - action->m_visible_rect.Min.x,
        action->m_visible_rect.Max.y - action->m_visible_rect.Min.y
    );
}

void ne_navigate_action_navigate_to(
    ne_navigate_action *action,
    ImRect bounds,
    ne_zoom_mode zoom_mode,
    float duration,
    ne_navigation_reason reason
)
{
    if (ne_rect_is_empty(bounds)) return;

    if (duration < 0.0f)
        duration = action->base.editor->m_style.base.scroll_duration;

    if (zoom_mode == ne_zoom_mode_none) {
        ImRect view_rect = ne_navigate_action_get_view_rect(action);
        ImVec2 view_rect_center = ImRect_GetCenter(&view_rect);
        ImVec2 target_center = ImRect_GetCenter(&bounds);

        ImRect_Translate(&view_rect, ne_vec2_sub(target_center, view_rect_center));

        ne_navigate_animation_navigate_to(&action->m_animation, view_rect, duration);
        action->m_reason = reason;
    } else {
        ImRect rect = bounds;

        if (zoom_mode == ne_zoom_mode_with_margin) {
            float extend = ne_imax_f(ImRect_GetWidth(&rect), ImRect_GetHeight(&rect));
            ImRect_Expand(&rect, extend * c_navigation_zoom_margin * 0.5f);
        }

        ne_navigate_animation_navigate_to(&action->m_animation, rect, duration);
        action->m_reason = reason;
    }
}

void ne_navigate_action_stop_navigation(ne_navigate_action *action)
{ ne_animation_stop(&action->m_animation.base); }

void ne_navigate_action_finish_navigation(ne_navigate_action *action)
{ ne_animation_finish(&action->m_animation.base); }

bool ne_navigate_action_move_over_edge(ne_navigate_action *action, ImVec2 canvas_size)
{
    if (ne_animation_is_playing(&action->m_animation.base)) return false;

    ImGuiIO *io = ImGui_GetIO();

    const ImVec2 screen_mouse_pos = io->MousePos;
    const ImRect screen_rect = (ImRect){ ImGui_GetCursorScreenPos(),
                                         ne_vec2_add(ImGui_GetCursorScreenPos(), canvas_size) };

    if (ne_rect_contains(screen_rect, screen_mouse_pos)) return false;

    if (screen_mouse_pos.x <= -FLT_MAX || screen_mouse_pos.y <= -FLT_MAX)
        return false;

    const ImVec2 min_distance = (ImVec2){ -c_max_move_over_edge_distance,
                                          -c_max_move_over_edge_distance };
    const ImVec2 max_distance = (ImVec2){ c_max_move_over_edge_distance,
                                          c_max_move_over_edge_distance };

    const ImVec2 screen_point_on_edge = ne_rect_closest_point(screen_rect, screen_mouse_pos, true);
    const ImVec2 offset = ne_vec2_min(
        ne_vec2_max(ne_vec2_sub(screen_point_on_edge, screen_mouse_pos), min_distance),
        max_distance
    );
    const ImVec2 relative_offset = ne_vec2_scale(ne_vec2_neg(offset), io->DeltaTime * c_max_move_over_edge_speed);

    action->m_scroll = ne_vec2_add(action->m_scroll, relative_offset);

    action->m_move_screen_offset = relative_offset;
    action->m_moving_over_edge = true;

    return true;
}

void ne_navigate_action_stop_move_over_edge(ne_navigate_action *action)
{
    if (action->m_moving_over_edge) {
        ne_editor_context_make_dirty(action->base.editor, ne_save_reason_navigation);

        action->m_move_screen_offset = (ImVec2){ 0, 0 };
        action->m_moving_over_edge = false;
    }
}

void ne_navigate_action_set_window(ne_navigate_action *action, ImVec2 position, ImVec2 size)
{
    action->m_window_screen_pos = position;
    action->m_window_screen_size = size;
}

ne_canvas_view ne_navigate_action_get_view(const ne_navigate_action *action)
{ return ne_canvas_view_make(ne_vec2_neg(action->m_scroll), action->m_zoom); }

ImRect ne_navigate_action_get_view_rect(const ne_navigate_action *action)
{
    return ne_canvas_calc_view_rect(action->m_canvas, ne_navigate_action_get_view(action));
}

void ne_navigate_action_set_view_rect(ne_navigate_action *action, ImRect rect)
{
    ne_canvas_view view = ne_canvas_calc_center_view_rect(action->m_canvas, rect);
    action->m_scroll = ne_vec2_neg(view.Origin);
    action->m_zoom = view.Scale;
}

ImVec2 ne_navigate_action_get_move_screen_offset(const ne_navigate_action *action)
{ return action->m_move_screen_offset; }

bool ne_navigate_action_is_moving_over_edge(const ne_navigate_action *action)
{ return action->m_moving_over_edge; }

static float ne_navigate_action_match_smooth_zoom(ne_navigate_action *action, float steps)
{
    const float power = action->base.editor->m_config.smooth_zoom_power;

    const float new_zoom = action->m_zoom * powf(power, steps);
    if (new_zoom < action->m_zoom_levels[0]) return action->m_zoom_levels[0];
    else if (new_zoom > action->m_zoom_levels[action->m_zoom_level_count - 1])
        return action->m_zoom_levels[action->m_zoom_level_count - 1];
    else return new_zoom;
}

static float ne_navigate_action_match_zoom(ne_navigate_action *action, int steps, float fallback_zoom)
{
    int current_zoom_index = ne_navigate_action_match_zoom_index(action, steps);
    if (current_zoom_index < 0) return fallback_zoom;

    float current_zoom = action->m_zoom_levels[current_zoom_index];
    if (fabsf(current_zoom - action->m_zoom) > 0.001f) return current_zoom;

    int new_index = current_zoom_index + steps;
    if (new_index >= 0 && new_index < action->m_zoom_level_count)
        return action->m_zoom_levels[new_index];
    else return fallback_zoom;
}

static float ne_navigate_action_match_zoom_index(ne_navigate_action *action, int direction)
{
    int best_index = -1;
    float best_distance = 0.0f;

    for (int i = 0; i < action->m_zoom_level_count; ++i) {
        float distance = fabsf(action->m_zoom_levels[i] - action->m_zoom);
        if (distance < best_distance || best_index < 0) {
            best_distance = distance;
            best_index = i;
        }
    }

    if (best_distance > 0.001f) {
        if (direction > 0) {
            ++best_index;
            if (best_index >= action->m_zoom_level_count)
                best_index = action->m_zoom_level_count - 1;
        } else if (direction < 0) {
            --best_index;
            if (best_index < 0) best_index = 0;
        }
    }

    return (float)best_index;
}

static float ne_navigate_action_get_next_zoom(ne_navigate_action *action, float steps)
{
    if (action->base.editor->m_config.enable_smooth_zoom)
        return ne_navigate_action_match_smooth_zoom(action, steps);
    else {
        int fixed_steps = (int)steps;
        return ne_navigate_action_match_zoom(
            action,
            fixed_steps,
            action->m_zoom_levels[fixed_steps < 0 ? 0 : action->m_zoom_level_count - 1]
        );
    }
}

//------------------------------------------------------------------------------
// Size Action.
//------------------------------------------------------------------------------
static const char *ne_size_action_get_name(ne_editor_action *action)
{
    (void)action;
    return "Size";
}

void ne_size_action_init(ne_size_action *action, ne_editor_context *editor)
{
    memset(action, 0, sizeof(*action));
    action->base.type = ne_action_type_size;
    action->base.editor = editor;
    action->base.get_name = ne_size_action_get_name;
    action->base.accept = (ne_accept_result (*)(ne_editor_action *, const ne_control *))ne_size_action_accept;
    action->base.process = (bool (*)(ne_editor_action *, const ne_control *))ne_size_action_process;
    action->base.reject = NULL;
    action->base.get_cursor = NULL;
    action->base.is_dragging = (bool (*)(ne_editor_action *))ne_size_action_is_dragging;
    action->base.show_metrics = (void (*)(ne_editor_action *))ne_size_action_show_metrics;
    action->m_cursor = ImGuiMouseCursor_Arrow;
}

static bool ne_size_action_is_dragging(ne_editor_action *action)
{ return ((ne_size_action *)action)->m_is_active; }

static int ne_size_action_get_region(ne_node *node)
{ return ne_node_get_region(node, ImGui_GetMousePos()); }

static int ne_size_action_choose_cursor(int region)
{
    switch (region) {
        default:
        case ne_node_region_center: return ImGuiMouseCursor_Arrow;
        case ne_node_region_top:
        case ne_node_region_bottom: return ImGuiMouseCursor_ResizeNS;
        case ne_node_region_left:
        case ne_node_region_right: return ImGuiMouseCursor_ResizeEW;
        case ne_node_region_top_left:
        case ne_node_region_bottom_right: return ImGuiMouseCursor_ResizeNWSE;
        case ne_node_region_top_right:
        case ne_node_region_bottom_left: return ImGuiMouseCursor_ResizeNESW;
    }
}

ne_accept_result ne_size_action_accept(ne_size_action *action, const ne_control *control)
{
    IM_ASSERT(!action->m_is_active);

    if (action->m_is_active) return ne_accept_false;

    if (control->active_node && ne_is_group(control->active_node) &&
        ImGui_IsMouseDragging(action->base.editor->m_config.drag_button_index, 1)) {
        int pivot = ne_size_action_get_region(control->active_node);
        if (pivot != ne_node_region_header && pivot != ne_node_region_center) {
            action->m_start_bounds = control->active_node->m_bounds;
            action->m_start_group_bounds = control->active_node->m_group_bounds;
            action->m_last_size = ImRect_GetSize(&control->active_node->m_bounds);
            action->m_minimum_size = (ImVec2){ 0, 0 };
            action->m_last_drag_offset = (ImVec2){ 0, 0 };
            action->m_pivot = pivot;
            action->m_cursor = ne_size_action_choose_cursor(action->m_pivot);
            action->m_sized_node = control->active_node;
            action->m_is_active = true;
        }
    } else if (control->hot_node && ne_is_group(control->hot_node)) {
        action->m_cursor = ne_size_action_choose_cursor(
            ne_size_action_get_region(control->hot_node)
        );
        return ne_accept_possible;
    }

    return action->m_is_active ? ne_accept_true : ne_accept_false;
}

bool ne_size_action_process(ne_size_action *action, const ne_control *control)
{
    if (action->m_clean) {
        action->m_clean = false;

        if (ne_vec2_ne(
                action->m_sized_node->m_bounds.Min,
                action->m_start_bounds.Min
            ) ||
            ne_vec2_ne(
                action->m_sized_node->m_group_bounds.Min,
                action->m_start_group_bounds.Min
            ))
            ne_editor_context_make_dirty_node(
                action->base.editor,
                ne_save_reason_position | ne_save_reason_user,
                action->m_sized_node
            );

        if (ne_vec2_ne(
                ImRect_GetSize(&action->m_sized_node->m_bounds),
                ImRect_GetSize(&action->m_start_bounds)
            ) ||
            ne_vec2_ne(
                ImRect_GetSize(&action->m_sized_node->m_group_bounds),
                ImRect_GetSize(&action->m_start_group_bounds)
            ))
            ne_editor_context_make_dirty_node(
                action->base.editor,
                ne_save_reason_size | ne_save_reason_user,
                action->m_sized_node
            );

        action->m_sized_node = NULL;
    }

    if (!action->m_is_active) return false;

    if (control->active_node == action->m_sized_node) {
        ImVec2 drag_offset = (control->active_node == action->m_sized_node)
                               ? ImGui_GetMouseDragDelta(0, 0.0f)
                               : action->m_last_drag_offset;
        action->m_last_drag_offset = drag_offset;

        if (action->m_minimum_size.x == 0.0f &&
            action->m_last_size.x != ImRect_GetWidth(&action->m_sized_node->m_bounds))
            action->m_minimum_size.x = ImRect_GetWidth(&action->m_sized_node->m_bounds);
        if (action->m_minimum_size.y == 0.0f &&
            action->m_last_size.y != ImRect_GetHeight(&action->m_sized_node->m_bounds))
            action->m_minimum_size.y = ImRect_GetHeight(&action->m_sized_node->m_bounds);

        ImVec2 minimum_size = ne_vec2_max(
            action->m_minimum_size,
            ne_vec2_sub(
                ImRect_GetSize(&action->m_start_bounds),
                ImRect_GetSize(&action->m_start_group_bounds)
            )
        );

        ImRect new_bounds = action->m_start_bounds;

        if ((action->m_pivot & ne_node_region_top) == ne_node_region_top)
            new_bounds.Min.y = ne_imin_f(
                new_bounds.Max.y - minimum_size.y,
                ne_editor_context_align_point_to_grid(
                    action->base.editor,
                    new_bounds.Min.y + drag_offset.y
                )
            );
        if ((action->m_pivot & ne_node_region_bottom) == ne_node_region_bottom)
            new_bounds.Max.y = ne_imax_f(
                new_bounds.Min.y + minimum_size.y,
                ne_editor_context_align_point_to_grid(
                    action->base.editor,
                    new_bounds.Max.y + drag_offset.y
                )
            );
        if ((action->m_pivot & ne_node_region_left) == ne_node_region_left)
            new_bounds.Min.x = ne_imin_f(
                new_bounds.Max.x - minimum_size.x,
                ne_editor_context_align_point_to_grid(
                    action->base.editor,
                    new_bounds.Min.x + drag_offset.x
                )
            );
        if ((action->m_pivot & ne_node_region_right) == ne_node_region_right)
            new_bounds.Max.x = ne_imax_f(
                new_bounds.Min.x + minimum_size.x,
                ne_editor_context_align_point_to_grid(
                    action->base.editor,
                    new_bounds.Max.x + drag_offset.x
                )
            );

        ne_floor_rect(&new_bounds);

        action->m_last_size = ImRect_GetSize(&new_bounds);

        action->m_sized_node->m_bounds = new_bounds;
        action->m_sized_node->m_group_bounds = new_bounds;
        action->m_sized_node->m_group_bounds.Min
            .x -= action->m_start_bounds.Min.x -
                  action->m_start_group_bounds.Min.x;
        action->m_sized_node->m_group_bounds.Min
            .y -= action->m_start_bounds.Min.y -
                  action->m_start_group_bounds.Min.y;
        action->m_sized_node->m_group_bounds.Max
            .x -= action->m_start_bounds.Max.x -
                  action->m_start_group_bounds.Max.x;
        action->m_sized_node->m_group_bounds.Max
            .y -= action->m_start_bounds.Max.y -
                  action->m_start_group_bounds.Max.y;
    } else if (!control->active_node) {
        action->m_clean = true;
        action->m_is_active = false;
        return true;
    }

    return action->m_is_active;
}

void ne_size_action_show_metrics(ne_size_action *action)
{
    ImGui_Text("%s:", ne_action_get_name(&action->base));
    ImGui_Text("    Active: %s", action->m_is_active ? "yes" : "no");
    ImGui_Text(
        "    Node: %s (%p)",
        ne_get_object_name((ne_object *)action->m_sized_node),
        action->m_sized_node ? (void *)(uintptr_t)action->m_sized_node->m_id : NULL
    );
    if (action->m_sized_node && action->m_is_active) {
        ImGui_Text(
            "    Bounds: { x=%g y=%g w=%g h=%g }",
            action->m_sized_node->m_bounds.Min.x,
            action->m_sized_node->m_bounds.Min.y,
            ImRect_GetWidth(&action->m_sized_node->m_bounds),
            ImRect_GetHeight(&action->m_sized_node->m_bounds)
        );
        ImGui_Text(
            "    Group Bounds: { x=%g y=%g w=%g h=%g }",
            action->m_sized_node->m_group_bounds.Min.x,
            action->m_sized_node->m_group_bounds.Min.y,
            ImRect_GetWidth(&action->m_sized_node->m_group_bounds),
            ImRect_GetHeight(&action->m_sized_node->m_group_bounds)
        );
        ImGui_Text(
            "    Start Bounds: { x=%g y=%g w=%g h=%g }",
            action->m_start_bounds.Min.x,
            action->m_start_bounds.Min.y,
            ImRect_GetWidth(&action->m_start_bounds),
            ImRect_GetHeight(&action->m_start_bounds)
        );
        ImGui_Text(
            "    Start Group Bounds: { x=%g y=%g w=%g h=%g }",
            action->m_start_group_bounds.Min.x,
            action->m_start_group_bounds.Min.y,
            ImRect_GetWidth(&action->m_start_group_bounds),
            ImRect_GetHeight(&action->m_start_group_bounds)
        );
        ImGui_Text(
            "    Minimum Size: { w=%g h=%g }",
            action->m_minimum_size.x,
            action->m_minimum_size.y
        );
        ImGui_Text(
            "    Last Size: { w=%g h=%g }",
            action->m_last_size.x,
            action->m_last_size.y
        );
    }
}

//------------------------------------------------------------------------------
// Drag Action.
//------------------------------------------------------------------------------
static const char *ne_drag_action_get_name(ne_editor_action *action)
{
    (void)action;
    return "Drag";
}

void ne_drag_action_init(ne_drag_action *action, ne_editor_context *editor)
{
    memset(action, 0, sizeof(*action));
    action->base.type = ne_action_type_drag;
    action->base.editor = editor;
    action->base.get_name = ne_drag_action_get_name;
    action->base.accept = (ne_accept_result (*)(ne_editor_action *, const ne_control *))ne_drag_action_accept;
    action->base.process = (bool (*)(ne_editor_action *, const ne_control *))ne_drag_action_process;
    action->base.reject = NULL;
    action->base.get_cursor = NULL;
    action->base.is_dragging = (bool (*)(ne_editor_action *))ne_drag_action_is_dragging;
    action->base.show_metrics = (void (*)(ne_editor_action *))ne_drag_action_show_metrics;
}

static bool ne_drag_action_is_dragging(ne_editor_action *action)
{ return ((ne_drag_action *)action)->m_is_active; }

static bool ne_object_ptr_in(ne_object **arr, ne_object *object)
{
    for (int i = 0; i < acount(arr); ++i)
        if (arr[i] == object) return true;
    return false;
}

ne_accept_result ne_drag_action_accept(ne_drag_action *action, const ne_control *control)
{
    IM_ASSERT(!action->m_is_active);

    if (action->m_is_active) return ne_accept_false;

    if (ne_editor_context_can_accept_user_input(action->base.editor) &&
        control->active_object &&
        ImGui_IsMouseDragging(action->base.editor->m_config.drag_button_index, 1)) {
        if (!ne_object_accept_drag(control->active_object))
            return ne_accept_false;

        action->m_dragged_object = control->active_object;

        afree(action->m_objects);
        action->m_objects = NULL;
        apush(action->m_objects, action->m_dragged_object);

        if (ne_editor_context_is_selected(action->base.editor, action->m_dragged_object)) {
            for (int i = 0; i < acount(action->base.editor->m_selected_objects); ++i) {
                ne_object *selected_object = action->base.editor->m_selected_objects[i];
                ne_node *selected_node = ne_object_as_node(selected_object);
                if (selected_node && selected_node != (ne_node *)action->m_dragged_object &&
                    ne_object_accept_drag(selected_object))
                    apush(action->m_objects, selected_object);
            }
        }

        ImGuiIO *io = ImGui_GetIO();
        if (!io->KeyShift) {
            ne_node **grouped_nodes = NULL;
            for (int i = 0; i < acount(action->m_objects); ++i) {
                ne_node *node = ne_object_as_node(action->m_objects[i]);
                if (node) ne_node_get_grouped_nodes(node, &grouped_nodes, true);
            }

            for (int i = 0; i < acount(grouped_nodes); ++i)
                if (!ne_object_ptr_in(action->m_objects, (ne_object *)grouped_nodes[i]) &&
                    ne_object_accept_drag((ne_object *)grouped_nodes[i]))
                    apush(action->m_objects, (ne_object *)grouped_nodes[i]);

            afree(grouped_nodes);
        }

        action->m_is_active = true;
    } else if (
        control->hot_node && ne_is_group(control->hot_node) &&
        ne_node_get_region(control->hot_node, ImGui_GetMousePos()) == ne_node_region_header
    ) {
        return ne_accept_possible;
    }

    return action->m_is_active ? ne_accept_true : ne_accept_false;
}

bool ne_drag_action_process(ne_drag_action *action, const ne_control *control)
{
    if (action->m_clear) {
        action->m_clear = false;

        for (int i = 0; i < acount(action->m_objects); ++i) {
            ne_object *object = action->m_objects[i];
            if (ne_object_end_drag(object))
                ne_editor_context_make_dirty_node(
                    action->base.editor,
                    ne_save_reason_position | ne_save_reason_user,
                    ne_object_as_node(object)
                );
        }

        afree(action->m_objects);
        action->m_objects = NULL;

        action->m_dragged_object = NULL;
    }

    if (!action->m_is_active) return false;

    if (control->active_object == action->m_dragged_object) {
        ImVec2 drag_offset = ImGui_GetMouseDragDelta(
            action->base.editor->m_config.drag_button_index,
            0.0f
        );

        ImVec2 dragged_origin = ne_object_drag_start_location(action->m_dragged_object);
        ImVec2 align_pivot = (ImVec2){ 0, 0 };

        if (ne_object_as_node(action->m_dragged_object)) {
            ne_node *dragged_node = ne_object_as_node(action->m_dragged_object);
            float x = FLT_MAX;
            float y = FLT_MAX;

            /* testPivot lambda -> inline */
            for (ne_pin *pin = dragged_node->m_last_pin; pin; pin = pin->m_previous_pin) {
                ImVec2 pivot = ne_vec2_sub(
                    ImRect_GetCenter(&pin->m_pivot),
                    dragged_node->m_bounds.Min
                );

                ImVec2 initial = ne_vec2_add(ne_vec2_add(dragged_origin, drag_offset), pivot);
                ImVec2 candidate = ne_vec2_sub(
                    ne_editor_context_align_point_to_grid_v2(action->base.editor, initial),
                    ne_vec2_add(dragged_origin, pivot)
                );

                if (fabsf(candidate.x) < fabsf(ne_imin_f(x, FLT_MAX))) {
                    x = candidate.x;
                    align_pivot.x = pivot.x;
                }
                if (fabsf(candidate.y) < fabsf(ne_imin_f(y, FLT_MAX))) {
                    y = candidate.y;
                    align_pivot.y = pivot.y;
                }
            }
        }

        ImVec2 aligned_offset = ne_vec2_sub(
            ne_editor_context_align_point_to_grid_v2(
                action->base.editor,
                ne_vec2_add(ne_vec2_add(dragged_origin, drag_offset), align_pivot)
            ),
            ne_vec2_add(dragged_origin, align_pivot)
        );

        if (!ImGui_GetIO()->KeyAlt) drag_offset = aligned_offset;

        for (int i = 0; i < acount(action->m_objects); ++i)
            ne_object_update_drag(action->m_objects[i], drag_offset);
    } else if (!control->active_object) {
        action->m_clear = true;

        action->m_is_active = false;
        return true;
    }

    return action->m_is_active;
}

void ne_drag_action_show_metrics(ne_drag_action *action)
{
    ImGui_Text("%s:", ne_action_get_name(&action->base));
    ImGui_Text("    Active: %s", action->m_is_active ? "yes" : "no");
    ImGui_Text(
        "    Node: %s (%p)",
        ne_get_object_name(action->m_dragged_object),
        action->m_dragged_object
            ? (void *)ne_object_id_of(action->m_dragged_object).id
            : NULL
    );
}

//------------------------------------------------------------------------------
// Select Action.
//------------------------------------------------------------------------------
static const char *ne_select_action_get_name(ne_editor_action *action)
{
    (void)action;
    return "Select";
}

void ne_select_action_init(ne_select_action *action, ne_editor_context *editor)
{
    memset(action, 0, sizeof(*action));
    action->base.type = ne_action_type_select;
    action->base.editor = editor;
    action->base.get_name = ne_select_action_get_name;
    action->base.accept = (ne_accept_result (*)(ne_editor_action *, const ne_control *))ne_select_action_accept;
    action->base.process = (bool (*)(ne_editor_action *, const ne_control *))ne_select_action_process;
    action->base.reject = NULL;
    action->base.get_cursor = NULL;
    action->base.is_dragging = (bool (*)(ne_editor_action *))ne_select_action_is_dragging;
    action->base.show_metrics = (void (*)(ne_editor_action *))ne_select_action_show_metrics;

    memset(&action->m_animation, 0, sizeof(action->m_animation));
    action->m_animation.state = ne_animation_stopped;
    action->m_animation.editor = editor;
}

static bool ne_select_action_is_dragging(ne_editor_action *action)
{ return ((ne_select_action *)action)->m_is_active; }

/* upstream ImGui_GetMouseClickPos */
static ImVec2 ne_imgui_get_mouse_click_pos(int button_index)
{
    if (ImGui_IsMouseDown(button_index))
        return ImGui_GetIO()->MouseClickedPos[button_index];
    else return ImGui_GetMousePos();
}

ne_accept_result ne_select_action_accept(ne_select_action *action, const ne_control *control)
{
    IM_ASSERT(!action->m_is_active);

    if (action->m_is_active) return ne_accept_false;

    ImGuiIO *io = ImGui_GetIO();
    action->m_select_groups = io->KeyShift;
    action->m_select_link_mode = io->KeyAlt;

    afree(action->m_selected_objects_at_start);
    action->m_selected_objects_at_start = NULL;

    if (ne_editor_context_can_accept_user_input(action->base.editor) &&
        control->background_hot &&
        ImGui_IsMouseDragging(action->base.editor->m_config.select_button_index, 1)) {
        action->m_is_active = true;
        action->m_start_point = ne_imgui_get_mouse_click_pos(
            action->base.editor->m_config.select_button_index
        );
        action->m_end_point = action->m_start_point;

        if ((action->m_select_link_mode &&
             ne_editor_context_is_any_node_selected(action->base.editor)) ||
            (!action->m_select_link_mode &&
             ne_editor_context_is_any_link_selected(action->base.editor))) {
            ne_editor_context_clear_selection(action->base.editor);
        }

        if (io->KeyCtrl)
            for (int i = 0; i < acount(action->base.editor->m_selected_objects); ++i)
                apush(action->m_selected_objects_at_start, action->base.editor->m_selected_objects[i]);
    } else if (
        control->background_click_button_index == action->base.editor->m_config.select_button_index
    ) {
        ne_editor_context_clear_selection(action->base.editor);
    } else {
        ne_object *clicked_object = control->clicked_node
                                      ? (ne_object *)control->clicked_node
                                      : (ne_object *)control->clicked_link;

        if (clicked_object) {
            if ((ne_object_as_link(clicked_object) &&
                 ne_editor_context_is_any_node_selected(action->base.editor)) ||
                (ne_object_as_node(clicked_object) &&
                 ne_editor_context_is_any_link_selected(action->base.editor))) {
                ne_editor_context_clear_selection(action->base.editor);
            }

            if (io->KeyCtrl)
                ne_editor_context_toggle_object_selection(action->base.editor, clicked_object);
            else
                ne_editor_context_set_selected_object(action->base.editor, clicked_object);
        }
    }

    if (action->m_is_active) ne_animation_stop(&action->m_animation);

    return action->m_is_active ? ne_accept_true : ne_accept_false;
}

static int ne_cmp_object_ptr(const void *a, const void *b)
{
    ne_object *pa = *(ne_object *const *)a;
    ne_object *pb = *(ne_object *const *)b;
    return (pa < pb) ? -1 : (pa > pb) ? 1 : 0;
}

bool ne_select_action_process(ne_select_action *action, const ne_control *control)
{
    (void)control;

    if (action->m_commit_selection) {
        ne_editor_context_clear_selection(action->base.editor);
        for (int i = 0; i < acount(action->m_candidate_objects); ++i)
            ne_editor_context_select_object(action->base.editor, action->m_candidate_objects[i]);

        afree(action->m_candidate_objects);
        action->m_candidate_objects = NULL;

        action->m_commit_selection = false;
    }

    if (!action->m_is_active) return false;

    if (ImGui_IsMouseDragging(action->base.editor->m_config.select_button_index, 0)) {
        action->m_end_point = ImGui_GetMousePos();

        ImVec2 top_left = (ImVec2){
            ne_imin_f(action->m_start_point.x, action->m_end_point.x),
            ne_imin_f(action->m_start_point.y, action->m_end_point.y)
        };
        ImVec2 bottom_right = (ImVec2){
            ne_imax_f(action->m_start_point.x, action->m_end_point.x),
            ne_imax_f(action->m_start_point.y, action->m_end_point.y)
        };
        ImRect rect = (ImRect){ top_left, bottom_right };
        if (ImRect_GetWidth(&rect) <= 0) rect.Max.x = rect.Min.x + 1;
        if (ImRect_GetHeight(&rect) <= 0) rect.Max.y = rect.Min.y + 1;

        afree(action->m_candidate_objects);
        action->m_candidate_objects = NULL;

        if (action->m_select_link_mode) {
            struct ne_link **links = NULL;
            ne_editor_context_find_links_in_rect(action->base.editor, rect, &links, false);
            for (int i = 0; i < acount(links); ++i)
                apush(action->m_candidate_objects, (ne_object *)links[i]);
            afree(links);
        } else {
            ne_node **nodes = NULL;
            ne_editor_context_find_nodes_in_rect(action->base.editor, rect, &nodes, false, true);
            for (int i = 0; i < acount(nodes); ++i) {
                if (action->m_select_groups == ne_is_group(nodes[i]))
                    apush(action->m_candidate_objects, (ne_object *)nodes[i]);
            }
            afree(nodes);
        }

        for (int i = 0; i < acount(action->m_selected_objects_at_start); ++i)
            apush(action->m_candidate_objects, action->m_selected_objects_at_start[i]);

        /* sort + unique */
        qsort(
            action->m_candidate_objects,
            acount(action->m_candidate_objects),
            sizeof(ne_object *),
            ne_cmp_object_ptr
        );
        int w = 0;
        for (int i = 0; i < acount(action->m_candidate_objects); ++i)
            if (i == 0 || action->m_candidate_objects[i] !=
                              action->m_candidate_objects[w - 1])
                action->m_candidate_objects[w++] = action->m_candidate_objects[i];
        if (action->m_candidate_objects)
            AHDR(action->m_candidate_objects)->size = w;
    } else {
        action->m_is_active = false;

        ne_animation_play(&action->m_animation, c_selection_fade_out_duration);

        action->m_commit_selection = true;

        return true;
    }

    return action->m_is_active;
}

void ne_select_action_show_metrics(ne_select_action *action)
{
    ImGui_Text("%s:", ne_action_get_name(&action->base));
    ImGui_Text("    Active: %s", action->m_is_active ? "yes" : "no");
}

void ne_select_action_draw(ne_select_action *action, ImDrawList *draw_list)
{
    if (!action->m_is_active && !ne_animation_is_playing(&action->m_animation))
        return;

    const float alpha = ne_animation_is_playing(&action->m_animation)
                          ? ne_ease_out_quad_f(
                                1.0f,
                                -1.0f,
                                ne_animation_get_progress(&action->m_animation)
                            )
                          : 1.0f;

    const ImU32 fill_color = ne_editor_context_get_color_alpha(
        action->base.editor,
        action->m_select_link_mode ? ne_style_color_link_sel_rect
                                   : ne_style_color_node_sel_rect,
        alpha
    );
    const ImU32 outline_color = ne_editor_context_get_color_alpha(
        action->base.editor,
        action->m_select_link_mode ? ne_style_color_link_sel_rect_border
                                   : ne_style_color_node_sel_rect_border,
        alpha
    );

    ImDrawList_ChannelsSetCurrent(draw_list, c_background_channel_selection_rect);

    ImVec2 min = (ImVec2){
        ne_imin_f(action->m_start_point.x, action->m_end_point.x),
        ne_imin_f(action->m_start_point.y, action->m_end_point.y)
    };
    ImVec2 max = (ImVec2){
        ne_imax_f(action->m_start_point.x, action->m_end_point.x),
        ne_imax_f(action->m_start_point.y, action->m_end_point.y)
    };

    ImDrawList_AddRectFilled(draw_list, min, max, fill_color);
    ImDrawList_AddRect(draw_list, min, max, outline_color);
}

//------------------------------------------------------------------------------
// Context Menu Action.
//------------------------------------------------------------------------------
static const char *ne_context_menu_action_get_name(ne_editor_action *action)
{
    (void)action;
    return "Context Menu";
}

void ne_context_menu_action_init(ne_context_menu_action *action, ne_editor_context *editor)
{
    memset(action, 0, sizeof(*action));
    action->base.type = ne_action_type_context_menu;
    action->base.editor = editor;
    action->base.get_name = ne_context_menu_action_get_name;
    action->base.accept = (ne_accept_result (*)(ne_editor_action *, const ne_control *))
        ne_context_menu_action_accept;
    action->base.process = (bool (*)(ne_editor_action *, const ne_control *))ne_context_menu_action_process;
    action->base.reject = (void (*)(ne_editor_action *))ne_context_menu_action_reject;
    action->base.get_cursor = NULL;
    action->base.is_dragging = NULL;
    action->base.show_metrics = (void (*)(ne_editor_action *))ne_context_menu_action_show_metrics;
}

ne_accept_result ne_context_menu_action_accept(ne_context_menu_action *action, const ne_control *control)
{
    const bool is_pressed = ImGui_IsMouseClicked(
        action->base.editor->m_config.context_menu_button_index
    );
    const bool is_released = ImGui_IsMouseReleased(
        action->base.editor->m_config.context_menu_button_index
    );
    const bool is_dragging = ImGui_IsMouseDragging(action->base.editor->m_config.context_menu_button_index, 1);

    if (is_pressed || is_released || is_dragging) {
        int candidate_menu = ne_context_menu_none;
        ne_object_id context_id = (ne_object_id){ 0, ne_object_type_none };

        if (control->hot_object) {
            if (ne_object_as_node(control->hot_object))
                candidate_menu = ne_context_menu_node;
            else if (ne_object_as_pin(control->hot_object))
                candidate_menu = ne_context_menu_pin;
            else if (ne_object_as_link(control->hot_object))
                candidate_menu = ne_context_menu_link;

            if (candidate_menu != ne_context_menu_none)
                context_id = ne_object_id_of(control->hot_object);
        } else if (control->background_hot)
            candidate_menu = ne_context_menu_background;

        if (is_pressed) {
            action->m_candidate_menu = candidate_menu;
            action->m_context_id = context_id;
            return ne_accept_possible;
        } else if (
            is_released && action->m_candidate_menu == candidate_menu &&
            action->m_context_id.id == context_id.id &&
            action->m_context_id.type == context_id.type
        ) {
            action->m_current_menu = action->m_candidate_menu;
            action->m_candidate_menu = ne_context_menu_none;
            return ne_accept_true;
        } else {
            action->m_candidate_menu = ne_context_menu_none;
            action->m_current_menu = ne_context_menu_none;
            action->m_context_id = (ne_object_id){ 0, ne_object_type_none };
            return ne_accept_false;
        }
    }

    return ne_accept_false;
}

bool ne_context_menu_action_process(ne_context_menu_action *action, const ne_control *control)
{
    (void)control;

    action->m_candidate_menu = ne_context_menu_none;
    action->m_current_menu = ne_context_menu_none;
    action->m_context_id = (ne_object_id){ 0, ne_object_type_none };
    return false;
}

void ne_context_menu_action_reject(ne_context_menu_action *action)
{
    action->m_candidate_menu = ne_context_menu_none;
    action->m_current_menu = ne_context_menu_none;
    action->m_context_id = (ne_object_id){ 0, ne_object_type_none };
}

void ne_context_menu_action_show_metrics(ne_context_menu_action *action)
{
    ImGui_Text("%s:", ne_action_get_name(&action->base));
    ImGui_Text(
        "    Menu: %s",
        action->m_current_menu == ne_context_menu_node         ? "Node"
        : action->m_current_menu == ne_context_menu_pin        ? "Pin"
        : action->m_current_menu == ne_context_menu_link       ? "Link"
        : action->m_current_menu == ne_context_menu_background ? "Background"
                                                               : "None"
    );
}

bool ne_context_menu_action_show_node(ne_context_menu_action *action, ne_node_id *node_id)
{
    if (action->m_current_menu != ne_context_menu_node) return false;

    *node_id = (ne_node_id)action->m_context_id.id;
    ne_editor_context_set_user_context(action->base.editor, false);
    return true;
}

bool ne_context_menu_action_show_pin(ne_context_menu_action *action, ne_pin_id *pin_id)
{
    if (action->m_current_menu != ne_context_menu_pin) return false;

    *pin_id = (ne_pin_id)action->m_context_id.id;
    ne_editor_context_set_user_context(action->base.editor, false);
    return true;
}

bool ne_context_menu_action_show_link(ne_context_menu_action *action, ne_link_id *link_id)
{
    if (action->m_current_menu != ne_context_menu_link) return false;

    *link_id = (ne_link_id)action->m_context_id.id;
    ne_editor_context_set_user_context(action->base.editor, false);
    return true;
}

bool ne_context_menu_action_show_background(ne_context_menu_action *action)
{
    if (action->m_current_menu != ne_context_menu_background) return false;

    ne_editor_context_set_user_context(action->base.editor, false);
    return true;
}

//------------------------------------------------------------------------------
// Shortcut Action (Cut/Copy/Paste).
//------------------------------------------------------------------------------
static const char *ne_shortcut_action_get_name(ne_editor_action *action)
{
    (void)action;
    return "Shortcut";
}

void ne_shortcut_action_init(ne_shortcut_action *action, ne_editor_context *editor)
{
    memset(action, 0, sizeof(*action));
    action->base.type = ne_action_type_shortcut;
    action->base.editor = editor;
    action->base.get_name = ne_shortcut_action_get_name;
    action->base.accept = (ne_accept_result (*)(ne_editor_action *, const ne_control *))ne_shortcut_action_accept;
    action->base.process = (bool (*)(ne_editor_action *, const ne_control *))ne_shortcut_action_process;
    action->base.reject = (void (*)(ne_editor_action *))ne_shortcut_action_reject;
    action->base.get_cursor = NULL;
    action->base.is_dragging = NULL;
    action->base.show_metrics = (void (*)(ne_editor_action *))ne_shortcut_action_show_metrics;
}

ne_accept_result ne_shortcut_action_accept(ne_shortcut_action *action, const ne_control *control)
{
    if (!ne_editor_context_is_focused(action->base.editor) ||
        !ne_editor_context_are_shortcuts_enabled(action->base.editor))
        return ne_accept_false;

    int candidate_action = ne_shortcut_action_none;

    ImGuiIO *io = ImGui_GetIO();
    if (io->KeyCtrl && !io->KeyShift && !io->KeyAlt && ImGui_IsKeyPressed(ImGuiKey_X))
        candidate_action = ne_shortcut_action_cut;
    if (io->KeyCtrl && !io->KeyShift && !io->KeyAlt && ImGui_IsKeyPressed(ImGuiKey_C))
        candidate_action = ne_shortcut_action_copy;
    if (io->KeyCtrl && !io->KeyShift && !io->KeyAlt && ImGui_IsKeyPressed(ImGuiKey_V))
        candidate_action = ne_shortcut_action_paste;
    if (io->KeyCtrl && !io->KeyShift && !io->KeyAlt && ImGui_IsKeyPressed(ImGuiKey_D))
        candidate_action = ne_shortcut_action_duplicate;
    if (!io->KeyCtrl && !io->KeyShift && !io->KeyAlt && ImGui_IsKeyPressed(ImGuiKey_Space))
        candidate_action = ne_shortcut_action_create_node;

    if (candidate_action != ne_shortcut_action_none) {
        if (candidate_action != ne_shortcut_action_paste &&
            candidate_action != ne_shortcut_action_create_node) {
            afree(action->m_context);
            action->m_context = NULL;

            if (acount(action->base.editor->m_selected_objects) > 0) {
                for (int i = 0; i < acount(action->base.editor->m_selected_objects); ++i)
                    apush(action->m_context, action->base.editor->m_selected_objects[i]);

                /* Expand groups */
                ne_node **extra = NULL;
                for (int i = 0; i < acount(action->m_context); ++i) {
                    ne_node *node = ne_object_as_node(action->m_context[i]);
                    if (ne_is_group(node))
                        ne_node_get_grouped_nodes(node, &extra, true);
                }

                /* Apply groups and remove duplicates */
                if (acount(extra) > 0) {
                    for (int i = 0; i < acount(extra); ++i)
                        apush(action->m_context, (ne_object *)extra[i]);
                    afree(extra);

                    qsort(action->m_context, acount(action->m_context), sizeof(ne_object *), ne_cmp_object_ptr);
                    int w = 0;
                    for (int i = 0; i < acount(action->m_context); ++i)
                        if (i == 0 || action->m_context[i] != action->m_context[w - 1])
                            action->m_context[w++] = action->m_context[i];
                    AHDR(action->m_context)->size = w;
                }
            } else if (
                control->hot_object && ne_object_is_selectable(control->hot_object) &&
                !ne_is_group(ne_object_as_node(control->hot_object))
            ) {
                apush(action->m_context, control->hot_object);
            }

            if (acount(action->m_context) == 0) return ne_accept_false;

            /* If no links are selected, pick all links between nodes within context */
            bool has_any_links = false;
            for (int i = 0; i < acount(action->m_context); ++i)
                if (ne_object_as_link(action->m_context[i])) {
                    has_any_links = true;
                    break;
                }

            if (!has_any_links && acount(action->m_context) > 1) {
                /* isNodeInContext (linear scan) */
                /* Collect links connected to nodes and drop those reaching out of context */
                struct ne_link **links = NULL;
                for (int i = 0; i < acount(action->m_context); ++i) {
                    ne_node *node = ne_object_as_node(action->m_context[i]);
                    if (node)
                        ne_editor_context_find_links_for_node(action->base.editor, node->m_id, &links, true);
                }

                /* Remove duplicates */
                qsort(links, acount(links), sizeof(struct ne_link *), ne_cmp_object_ptr);
                int w = 0;
                for (int i = 0; i < acount(links); ++i)
                    if (i == 0 || links[i] != links[w - 1])
                        links[w++] = links[i];
                if (links) AHDR(links)->size = w;

                /* Drop out-of-context links */
                int w2 = 0;
                for (int i = 0; i < acount(links); ++i) {
                    struct ne_link *link = links[i];
                    bool start_in = ne_object_ptr_in(
                        action->m_context,
                        (ne_object *)link->m_start_pin->m_node
                    );
                    bool end_in = ne_object_ptr_in(
                        action->m_context,
                        (ne_object *)link->m_end_pin->m_node
                    );
                    if (start_in && end_in) links[w2++] = link;
                }
                if (links) AHDR(links)->size = w2;

                /* Append links */
                for (int i = 0; i < acount(links); ++i)
                    apush(action->m_context, (ne_object *)links[i]);
                afree(links);
            }
        } else {
            afree(action->m_context);
            action->m_context = NULL;
        }

        action->m_is_active = true;
        action->m_current_action = candidate_action;

        return ne_accept_true;
    }

    return ne_accept_false;
}

bool ne_shortcut_action_process(ne_shortcut_action *action, const ne_control *control)
{
    (void)control;

    action->m_is_active = false;
    action->m_current_action = ne_shortcut_action_none;
    afree(action->m_context);
    action->m_context = NULL;
    return false;
}

void ne_shortcut_action_reject(ne_shortcut_action *action)
{
    action->m_is_active = false;
    action->m_current_action = ne_shortcut_action_none;
    afree(action->m_context);
    action->m_context = NULL;
}

void ne_shortcut_action_show_metrics(ne_shortcut_action *action)
{
    ImGui_Text("%s:", ne_action_get_name(&action->base));
    ImGui_Text(
        "    Action: %s",
        action->m_current_action == ne_shortcut_action_cut         ? "Cut"
        : action->m_current_action == ne_shortcut_action_copy      ? "Copy"
        : action->m_current_action == ne_shortcut_action_paste     ? "Paste"
        : action->m_current_action == ne_shortcut_action_duplicate ? "Duplicate"
        : action->m_current_action == ne_shortcut_action_create_node
            ? "CreateNode"
            : "None"
    );
}

bool ne_shortcut_action_begin(ne_shortcut_action *action)
{
    if (action->m_is_active) action->m_in_action = true;
    return action->m_is_active;
}

void ne_shortcut_action_end(ne_shortcut_action *action)
{
    if (action->m_is_active) action->m_in_action = false;
}

bool ne_shortcut_action_accept_cut(ne_shortcut_action *action)
{
    IM_ASSERT(action->m_in_action);
    return action->m_current_action == ne_shortcut_action_cut;
}

bool ne_shortcut_action_accept_copy(ne_shortcut_action *action)
{
    IM_ASSERT(action->m_in_action);
    return action->m_current_action == ne_shortcut_action_copy;
}

bool ne_shortcut_action_accept_paste(ne_shortcut_action *action)
{
    IM_ASSERT(action->m_in_action);
    return action->m_current_action == ne_shortcut_action_paste;
}

bool ne_shortcut_action_accept_duplicate(ne_shortcut_action *action)
{
    IM_ASSERT(action->m_in_action);
    return action->m_current_action == ne_shortcut_action_duplicate;
}

bool ne_shortcut_action_accept_create_node(ne_shortcut_action *action)
{
    IM_ASSERT(action->m_in_action);
    return action->m_current_action == ne_shortcut_action_create_node;
}

//------------------------------------------------------------------------------
// Create Item Action.
//------------------------------------------------------------------------------
static const char *ne_create_item_action_get_name(ne_editor_action *action)
{
    (void)action;
    return "Create Item";
}

void ne_create_item_action_init(ne_create_item_action *action, ne_editor_context *editor)
{
    memset(action, 0, sizeof(*action));
    action->base.type = ne_action_type_create_item;
    action->base.editor = editor;
    action->base.get_name = ne_create_item_action_get_name;
    action->base.accept = (ne_accept_result (*)(ne_editor_action *, const ne_control *))
        ne_create_item_action_accept;
    action->base.process = (bool (*)(ne_editor_action *, const ne_control *))ne_create_item_action_process;
    action->base.reject = NULL;
    action->base.get_cursor = NULL;
    action->base.is_dragging = (bool (*)(ne_editor_action *))ne_create_item_action_is_dragging;
    action->base.show_metrics = (void (*)(ne_editor_action *))ne_create_item_action_show_metrics;

    action->m_link_color = IM_COL32_WHITE;
    action->m_link_thickness = 1.0f;
    action->m_last_channel = -1;
}

static bool ne_create_item_action_is_dragging(ne_editor_action *action)
{ return ((ne_create_item_action *)action)->m_is_active; }

static void ne_create_item_action_drag_start(ne_create_item_action *action, ne_pin *start_pin)
{
    IM_ASSERT(!action->m_in_active);

    action->m_next_stage = ne_create_stage_possible;
    action->m_link_start = start_pin;
    action->m_link_end = NULL;
}

static void ne_create_item_action_drag_end(ne_create_item_action *action)
{
    IM_ASSERT(!action->m_in_active);

    if (action->m_current_stage == ne_create_stage_possible &&
        action->m_user_action == ne_create_action_user_accept) {
        action->m_next_stage = ne_create_stage_create;
    } else {
        action->m_next_stage = ne_create_stage_none;
        action->m_item_type = ne_create_item_none;
        action->m_link_start = NULL;
        action->m_link_end = NULL;
    }
}

static void ne_create_item_action_drop_pin(ne_create_item_action *action, ne_pin *end_pin)
{
    IM_ASSERT(!action->m_in_active);

    action->m_item_type = ne_create_item_link;
    action->m_link_end = end_pin;
}

static void ne_create_item_action_drop_node(ne_create_item_action *action)
{
    IM_ASSERT(!action->m_in_active);

    action->m_item_type = ne_create_item_node;
    action->m_link_end = NULL;
}

static void ne_create_item_action_drop_nothing(ne_create_item_action *action)
{
    IM_ASSERT(!action->m_in_active);

    action->m_item_type = ne_create_item_none;
    action->m_link_end = NULL;
}

ne_accept_result ne_create_item_action_accept(ne_create_item_action *action, const ne_control *control)
{
    IM_ASSERT(!action->m_is_active);

    if (action->m_is_active) return ne_accept_false;

    if (control->active_pin &&
        ImGui_IsMouseDragging(action->base.editor->m_config.drag_button_index, 1)) {
        action->m_dragged_pin = control->active_pin;
        ne_create_item_action_drag_start(action, action->m_dragged_pin);

        ne_editor_context_clear_selection(action->base.editor);
    } else if (control->hot_pin) {
        return ne_accept_possible;
    } else return ne_accept_false;

    action->m_is_active = true;

    return ne_accept_true;
}

bool ne_create_item_action_process(ne_create_item_action *action, const ne_control *control)
{
    IM_ASSERT(action->m_is_active);

    if (!action->m_is_active) return false;

    if (action->m_dragged_pin && control->active_pin == action->m_dragged_pin &&
        (action->m_current_stage == ne_create_stage_possible)) {
        const bool dragging_from_source = (action->m_dragged_pin->m_kind == ne_pin_kind_output);

        ne_pin cursor_pin;
        memset(&cursor_pin, 0, sizeof(cursor_pin));
        cursor_pin.base.type = ne_object_type_pin;
        cursor_pin.base.editor = action->base.editor;
        cursor_pin.base.m_is_live = true;
        cursor_pin.m_kind = dragging_from_source ? ne_pin_kind_input : ne_pin_kind_output;
        cursor_pin.m_color = IM_COL32_WHITE;
        cursor_pin.m_border_color = IM_COL32_BLACK;
        cursor_pin.m_snap_link_to_dir = true;
        cursor_pin.m_pivot = (ImRect){ ImGui_GetMousePos(), ImGui_GetMousePos() };
        cursor_pin.m_dir = ne_vec2_neg(action->m_dragged_pin->m_dir);
        cursor_pin.m_strength = action->m_dragged_pin->m_strength;

        struct ne_link candidate;
        memset(&candidate, 0, sizeof(candidate));
        candidate.base.type = ne_object_type_link;
        candidate.base.editor = action->base.editor;
        candidate.base.m_is_live = true;
        candidate.m_color = action->m_link_color;
        candidate.m_thickness = 1.0f;
        candidate.m_start_pin = dragging_from_source ? action->m_dragged_pin
                                                     : &cursor_pin;
        candidate.m_end_pin = dragging_from_source ? &cursor_pin : action->m_dragged_pin;

        ne_pin *free_pin = dragging_from_source ? candidate.m_end_pin
                                                : candidate.m_start_pin;
        (void)free_pin;

        if (control->hot_pin) {
            ne_create_item_action_drop_pin(action, control->hot_pin);

            if (action->m_user_action == ne_create_action_user_accept)
                free_pin = control->hot_pin;
        } else if (control->background_hot)
            ne_create_item_action_drop_node(action);
        else ne_create_item_action_drop_nothing(action);

        ImDrawList *draw_list = ne_editor_context_get_draw_list(action->base.editor);
        ImDrawList_ChannelsSetCurrent(draw_list, c_link_channel_new_link);

        ne_link_update_endpoints(&candidate);
        ne_link_draw_color(&candidate, draw_list, action->m_link_color, action->m_link_thickness);
    } else if (action->m_current_stage == ne_create_stage_possible || !control->active_pin) {
        if (!ne_editor_context_can_accept_user_input(action->base.editor)) {
            action->m_dragged_pin = NULL;
            ne_create_item_action_drop_nothing(action);
        }

        ne_create_item_action_drag_end(action);
        action->m_is_active = false;
    }

    return action->m_is_active;
}

void ne_create_item_action_show_metrics(ne_create_item_action *action)
{
    ImGui_Text("%s:", ne_action_get_name(&action->base));
    ImGui_Text(
        "    Stage: %s",
        action->m_current_stage == ne_create_stage_possible ? "Possible"
        : action->m_current_stage == ne_create_stage_create ? "Create"
                                                            : "None"
    );
    ImGui_Text(
        "    User Action: %s",
        action->m_user_action == ne_create_action_user_reject   ? "Reject"
        : action->m_user_action == ne_create_action_user_accept ? "Accept"
                                                                : "Unknown"
    );
    ImGui_Text(
        "    Item Type: %s",
        action->m_item_type == ne_create_item_node   ? "Node"
        : action->m_item_type == ne_create_item_link ? "Link"
                                                     : "None"
    );
}

void ne_create_item_action_set_style(ne_create_item_action *action, ImU32 color, float thickness)
{
    action->m_link_color = color;
    action->m_link_thickness = thickness;
}

bool ne_create_item_action_begin(ne_create_item_action *action)
{
    IM_ASSERT(false == action->m_in_active);

    action->m_in_active = true;
    action->m_current_stage = action->m_next_stage;
    action->m_user_action = ne_create_action_unknown;
    action->m_link_color = IM_COL32_WHITE;
    action->m_link_thickness = 1.0f;

    if (action->m_current_stage == ne_create_stage_none) return false;

    action->m_last_channel = ne_editor_context_get_draw_list(action->base.editor)
                                 ->_Splitter._Current;

    return true;
}

void ne_create_item_action_end(ne_create_item_action *action)
{
    IM_ASSERT(action->m_in_active);

    if (action->m_is_in_global_space) {
        ImGui_PopClipRect();
        ne_editor_context_resume(action->base.editor, ne_suspend_flags_keep_splitter);

        int current_channel = ne_editor_context_get_draw_list(action->base.editor)
                                  ->_Splitter._Current;
        if (current_channel != action->m_last_channel)
            ImDrawList_ChannelsSetCurrent(
                ne_editor_context_get_draw_list(action->base.editor),
                action->m_last_channel
            );

        action->m_is_in_global_space = false;
    }

    action->m_in_active = false;
}

int ne_create_item_action_reject_item(ne_create_item_action *action)
{
    IM_ASSERT(action->m_in_active);

    if (!action->m_in_active || action->m_current_stage == ne_create_stage_none ||
        action->m_item_type == ne_create_item_none)
        return ne_create_result_indeterminate;

    action->m_user_action = ne_create_action_user_reject;

    return ne_create_result_true;
}

int ne_create_item_action_accept_item(ne_create_item_action *action)
{
    IM_ASSERT(action->m_in_active);

    if (!action->m_in_active || action->m_current_stage == ne_create_stage_none ||
        action->m_item_type == ne_create_item_none)
        return ne_create_result_indeterminate;

    action->m_user_action = ne_create_action_user_accept;

    if (action->m_current_stage == ne_create_stage_create) {
        action->m_next_stage = ne_create_stage_none;
        action->m_item_type = ne_create_item_none;
        action->m_link_start = NULL;
        action->m_link_end = NULL;
        return ne_create_result_true;
    } else return ne_create_result_false;
}

int ne_create_item_action_query_link(ne_create_item_action *action, ne_pin_id *start_id, ne_pin_id *end_id)
{
    IM_ASSERT(action->m_in_active);

    if (!action->m_in_active || action->m_current_stage == ne_create_stage_none ||
        action->m_item_type != ne_create_item_link)
        return ne_create_result_indeterminate;

    *start_id = action->m_link_start->m_id;
    *end_id = action->m_link_end->m_id;

    ne_editor_context_set_user_context(action->base.editor, true);

    if (!action->m_is_in_global_space) {
        ne_editor_context_suspend(action->base.editor, ne_suspend_flags_keep_splitter);

        ImRect rect = ne_canvas_rect(&action->base.editor->m_canvas);
        ImGui_PushClipRect(
            ne_vec2_add(rect.Min, (ImVec2){ 1, 1 }),
            ne_vec2_sub(rect.Max, (ImVec2){ 1, 1 }),
            false
        );
        action->m_is_in_global_space = true;
    }

    return ne_create_result_true;
}

int ne_create_item_action_query_node(ne_create_item_action *action, ne_pin_id *pin_id)
{
    IM_ASSERT(action->m_in_active);

    if (!action->m_in_active || action->m_current_stage == ne_create_stage_none ||
        action->m_item_type != ne_create_item_node)
        return ne_create_result_indeterminate;

    *pin_id = action->m_link_start ? action->m_link_start->m_id : 0;

    ne_editor_context_set_user_context(action->base.editor, true);

    if (!action->m_is_in_global_space) {
        ne_editor_context_suspend(action->base.editor, ne_suspend_flags_keep_splitter);

        ImRect rect = ne_canvas_rect(&action->base.editor->m_canvas);
        ImGui_PushClipRect(
            ne_vec2_add(rect.Min, (ImVec2){ 1, 1 }),
            ne_vec2_sub(rect.Max, (ImVec2){ 1, 1 }),
            false
        );
        action->m_is_in_global_space = true;
    }

    return ne_create_result_true;
}

//------------------------------------------------------------------------------
// Delete Items Action.
//------------------------------------------------------------------------------
static const char *ne_delete_items_action_get_name(ne_editor_action *action)
{
    (void)action;
    return "Delete Items";
}

void ne_delete_items_action_init(ne_delete_items_action *action, ne_editor_context *editor)
{
    memset(action, 0, sizeof(*action));
    action->base.type = ne_action_type_delete_items;
    action->base.editor = editor;
    action->base.get_name = ne_delete_items_action_get_name;
    action->base.accept = (ne_accept_result (*)(ne_editor_action *, const ne_control *))
        ne_delete_items_action_accept;
    action->base.process = (bool (*)(ne_editor_action *, const ne_control *))ne_delete_items_action_process;
    action->base.reject = NULL;
    action->base.get_cursor = NULL;
    action->base.is_dragging = NULL;
    action->base.show_metrics = (void (*)(ne_editor_action *))ne_delete_items_action_show_metrics;

    action->m_current_item_type = ne_delete_iterator_unknown;
    action->m_user_action = ne_delete_user_undetermined;
}

static void ne_delete_items_action_delete_dead_links(ne_delete_items_action *action, ne_node_id node_id)
{
    struct ne_link **links = NULL;
    ne_editor_context_find_links_for_node(action->base.editor, node_id, &links, true);
    for (int i = 0; i < acount(links); ++i) {
        struct ne_link *link = links[i];
        link->base.m_delete_on_new_frame = true;

        if (ne_object_ptr_in(action->m_candidate_objects, (ne_object *)link))
            continue;

        apush(action->m_candidate_objects, (ne_object *)link);
    }
    afree(links);
}

static void ne_delete_items_action_delete_dead_pins(ne_delete_items_action *action, ne_node_id node_id)
{
    ne_node *node = ne_editor_context_find_node(action->base.editor, node_id);
    if (!node) return;

    for (ne_pin *pin = node->m_last_pin; pin; pin = pin->m_previous_pin)
        pin->base.m_delete_on_new_frame = true;
}

ne_accept_result ne_delete_items_action_accept(ne_delete_items_action *action, const ne_control *control)
{
    IM_ASSERT(!action->m_is_active);

    if (action->m_is_active) return ne_accept_false;

    ImGuiIO *io = ImGui_GetIO();
    if (ne_editor_context_can_accept_user_input(action->base.editor) &&
        ImGui_IsKeyPressed(ImGuiKey_Delete) &&
        ne_editor_context_are_shortcuts_enabled(action->base.editor)) {
        if (acount(action->base.editor->m_selected_objects) > 0) {
            afree(action->m_candidate_objects);
            action->m_candidate_objects = NULL;
            for (int i = 0; i < acount(action->base.editor->m_selected_objects); ++i)
                apush(action->m_candidate_objects, action->base.editor->m_selected_objects[i]);
            action->m_is_active = true;
            return ne_accept_true;
        }
    } else if (control->clicked_link && io->KeyAlt) {
        afree(action->m_candidate_objects);
        action->m_candidate_objects = NULL;
        apush(action->m_candidate_objects, (ne_object *)control->clicked_link);
        action->m_is_active = true;
        return ne_accept_true;
    } else if (acount(action->m_manually_deleted_objects) > 0) {
        afree(action->m_candidate_objects);
        action->m_candidate_objects = action->m_manually_deleted_objects;
        action->m_manually_deleted_objects = NULL;
        action->m_is_active = true;
        return ne_accept_true;
    }

    return action->m_is_active ? ne_accept_true : ne_accept_false;
}

bool ne_delete_items_action_process(ne_delete_items_action *action, const ne_control *control)
{
    (void)control;

    if (!action->m_is_active) return false;

    action->m_is_active = false;
    return true;
}

void ne_delete_items_action_show_metrics(ne_delete_items_action *action)
{
    ImGui_Text("%s:", ne_action_get_name(&action->base));
    ImGui_Text("    Active: %s", action->m_is_active ? "yes" : "no");
}

bool ne_delete_items_action_add(ne_delete_items_action *action, ne_object *object)
{
    if (action->base.editor->m_current_action != NULL) return false;

    apush(action->m_manually_deleted_objects, object);

    return true;
}

bool ne_delete_items_action_begin(ne_delete_items_action *action)
{
    if (!action->m_is_active) return false;

    IM_ASSERT(!action->m_in_interaction);
    action->m_in_interaction = true;

    action->m_current_item_type = ne_delete_iterator_unknown;
    action->m_user_action = ne_delete_user_undetermined;

    return action->m_is_active;
}

void ne_delete_items_action_end(ne_delete_items_action *action)
{
    if (!action->m_is_active) return;

    IM_ASSERT(action->m_in_interaction);
    action->m_in_interaction = false;
}

static bool ne_delete_items_action_query_item(
    ne_delete_items_action *action,
    ne_object_id *item_id,
    int item_type
)
{
    if (!action->m_in_interaction) return false;

    if (action->m_current_item_type != item_type) {
        action->m_current_item_type = item_type;
        action->m_candidate_item_index = 0;
    } else if (action->m_user_action == ne_delete_user_undetermined) {
        ne_delete_items_action_reject_item(action);
    }

    action->m_user_action = ne_delete_user_undetermined;

    int item_count = acount(action->m_candidate_objects);
    while (action->m_candidate_item_index < item_count) {
        ne_object *item = action->m_candidate_objects[action->m_candidate_item_index];
        if (item_type == ne_delete_iterator_node) {
            ne_node *node = ne_object_as_node(item);
            if (node) {
                *item_id = (ne_object_id){ node->m_id, ne_object_type_node };
                return true;
            }
        } else if (item_type == ne_delete_iterator_link) {
            struct ne_link *link = ne_object_as_link(item);
            if (link) {
                *item_id = (ne_object_id){ link->m_id, ne_object_type_link };
                return true;
            }
        }

        ++action->m_candidate_item_index;
    }

    if (action->m_candidate_item_index == item_count)
        action->m_current_item_type = ne_delete_iterator_unknown;

    return false;
}

static ne_object *ne_delete_items_action_drop_current_item(ne_delete_items_action *action)
{
    ne_object *item = action->m_candidate_objects[action->m_candidate_item_index];
    for (int i = action->m_candidate_item_index;
         i + 1 < acount(action->m_candidate_objects);
         ++i)
        action->m_candidate_objects[i] = action->m_candidate_objects[i + 1];
    AHDR(action->m_candidate_objects)->size--;

    return item;
}

static void ne_delete_items_action_remove_item(ne_delete_items_action *action, bool delete_dependencies)
{
    ne_object *item = ne_delete_items_action_drop_current_item(action);

    ne_editor_context_deselect_object(action->base.editor, item);

    ne_editor_context_remove_settings(action->base.editor, item);

    item->m_delete_on_new_frame = true;

    if (delete_dependencies && action->m_current_item_type == ne_delete_iterator_node) {
        ne_node_id node_id = ne_object_id_of(item).id;
        ne_delete_items_action_delete_dead_links(action, node_id);
        ne_delete_items_action_delete_dead_pins(action, node_id);
    }

    if (action->m_current_item_type == ne_delete_iterator_link)
        ne_editor_context_notify_link_deleted(action->base.editor, ne_object_as_link(item));
}

bool ne_delete_items_action_accept_item(ne_delete_items_action *action, bool delete_dependencies)
{
    if (!action->m_in_interaction) return false;

    action->m_user_action = ne_delete_user_accepted;

    ne_delete_items_action_remove_item(action, delete_dependencies);

    return true;
}

void ne_delete_items_action_reject_item(ne_delete_items_action *action)
{
    if (!action->m_in_interaction) return;

    action->m_user_action = ne_delete_user_rejected;

    ne_delete_items_action_drop_current_item(action);
}

bool ne_delete_items_action_query_link(
    ne_delete_items_action *action,
    ne_link_id *link_id,
    ne_pin_id *start_id,
    ne_pin_id *end_id
)
{
    ne_object_id object_id;
    if (!ne_delete_items_action_query_item(action, &object_id, ne_delete_iterator_link))
        return false;

    if (object_id.type == ne_object_type_link)
        *link_id = (ne_link_id)object_id.id;
    else return false;

    if (start_id || end_id) {
        struct ne_link *link = ne_editor_context_find_link(action->base.editor, *link_id);
        if (start_id) *start_id = link->m_start_pin->m_id;
        if (end_id) *end_id = link->m_end_pin->m_id;
    }

    return true;
}

bool ne_delete_items_action_query_node(ne_delete_items_action *action, ne_node_id *node_id)
{
    ne_object_id object_id;
    if (!ne_delete_items_action_query_item(action, &object_id, ne_delete_iterator_node))
        return false;

    if (object_id.type == ne_object_type_node)
        *node_id = (ne_node_id)object_id.id;
    else return false;

    return true;
}

//------------------------------------------------------------------------------
// Node Builder.
//------------------------------------------------------------------------------
void ne_node_builder_init(ne_node_builder *builder, ne_editor_context *editor)
{
    memset(builder, 0, sizeof(*builder));
    builder->editor = editor;
}

void ne_node_builder_begin(ne_node_builder *builder, ne_node_id node_id)
{
    IM_ASSERT(NULL == builder->m_current_node);

    builder->m_current_node = ne_editor_context_get_node(builder->editor, node_id);

    ne_editor_context_update_node_state(builder->editor, builder->m_current_node);

    if (builder->m_current_node->m_center_on_screen) {
        ImRect bounds = ne_canvas_view_rect(&builder->editor->m_canvas);
        ImVec2 offset = ne_vec2_sub(
            ImRect_GetCenter(&bounds),
            ImRect_GetCenter(&builder->m_current_node->m_bounds)
        );

        if (ne_vec2_length_sq(offset) > 0) {
            if (ne_is_group(builder->m_current_node)) {
                ne_node **grouped_nodes = NULL;
                ne_node_get_grouped_nodes(builder->m_current_node, &grouped_nodes, false);
                apush(grouped_nodes, builder->m_current_node);

                for (int i = 0; i < acount(grouped_nodes); ++i) {
                    ImRect_Translate(&grouped_nodes[i]->m_bounds, cImFloorImVec2(offset));
                    ImRect_Translate(&grouped_nodes[i]->m_group_bounds, cImFloorImVec2(offset));
                    ne_editor_context_make_dirty_node(
                        builder->editor,
                        ne_save_reason_position | ne_save_reason_user,
                        grouped_nodes[i]
                    );
                }
                afree(grouped_nodes);
            } else {
                ImRect_Translate(&builder->m_current_node->m_bounds, cImFloorImVec2(offset));
                ImRect_Translate(&builder->m_current_node->m_group_bounds, cImFloorImVec2(offset));
                ne_editor_context_make_dirty_node(
                    builder->editor,
                    ne_save_reason_position | ne_save_reason_user,
                    builder->m_current_node
                );
            }
        }

        builder->m_current_node->m_center_on_screen = false;
    }

    ImGui_SetCursorScreenPos(builder->m_current_node->m_bounds.Min);

    ne_style *editor_style = &builder->editor->m_style.base;

    const float alpha = ImGui_GetStyle()->Alpha;

    builder->m_current_node->base.m_is_live = true;
    builder->m_current_node->m_last_pin = NULL;
    builder->m_current_node->m_color = ne_editor_context_get_color_alpha(
        builder->editor,
        ne_style_color_node_bg,
        alpha
    );
    builder->m_current_node->m_border_color = ne_editor_context_get_color_alpha(
        builder->editor,
        ne_style_color_node_border,
        alpha
    );
    builder->m_current_node->m_border_width = editor_style->node_border_width;
    builder->m_current_node->m_rounding = editor_style->node_rounding;
    builder->m_current_node->m_group_color = ne_editor_context_get_color_alpha(
        builder->editor,
        ne_style_color_group_bg,
        alpha
    );
    builder->m_current_node->m_group_border_color = ne_editor_context_get_color_alpha(
        builder->editor,
        ne_style_color_group_border,
        alpha
    );
    builder->m_current_node->m_group_border_width = editor_style->group_border_width;
    builder->m_current_node->m_group_rounding = editor_style->group_rounding;
    builder->m_current_node->m_highlight_connected_links = editor_style->highlight_connected_links !=
                                                           0.0f;

    builder->m_is_group = false;

    if (ne_editor_context_get_draw_list(builder->editor)) {
        ImDrawList *draw_list = ne_editor_context_get_draw_list(builder->editor);
        builder->m_current_node->m_channel = draw_list->_Splitter._Count;
        ne_draw_list_channels_grow(draw_list, draw_list->_Splitter._Count + c_channels_per_node);
        ImDrawList_ChannelsSetCurrent(draw_list, builder->m_current_node->m_channel + c_node_content_channel);

        ImDrawListSplitter_Clear(&builder->m_splitter);
        ne_draw_list_swap_splitter(draw_list, &builder->m_splitter);
    }

    ImGui_BeginGroup();

    if (editor_style->node_padding.x != 0 || editor_style->node_padding.y != 0 ||
        editor_style->node_padding.z != 0 || editor_style->node_padding.w != 0) {
        ImGui_SetCursorPos(ne_vec2_add(
            ImGui_GetCursorPos(),
            (ImVec2){ editor_style->node_padding.x, editor_style->node_padding.y }
        ));
        ImGui_BeginGroup();
    }
}

void ne_node_builder_end(ne_node_builder *builder)
{
    IM_ASSERT(NULL != builder->m_current_node);

    if (ne_editor_context_get_draw_list(builder->editor)) {
        ImDrawList *draw_list = ne_editor_context_get_draw_list(builder->editor);
        IM_ASSERT(draw_list->_Splitter._Count == 1);
        ne_draw_list_swap_splitter(draw_list, &builder->m_splitter);
    }

    ne_style *editor_style = &builder->editor->m_style.base;
    if (editor_style->node_padding.x != 0 || editor_style->node_padding.y != 0 ||
        editor_style->node_padding.z != 0 || editor_style->node_padding.w != 0) {
        ImGui_EndGroup();
        ImGui_SameLineEx(0, editor_style->node_padding.z);
        ImGui_PushStyleVarImVec2(ImGuiStyleVar_ItemSpacing, (ImVec2){ 0.0f, 0.0f });
        ImGui_Dummy((ImVec2){ 0, 0 });
        ImGui_Dummy((ImVec2){ 0, editor_style->node_padding.w });
        ImGui_PopStyleVar();
    }

    ImGui_EndGroup();

    builder->m_node_rect = (ImRect){ ImGui_GetItemRectMin(), ImGui_GetItemRectMax() };
    ne_floor_rect(&builder->m_node_rect);

    if (ne_vec2_ne(
            ImRect_GetSize(&builder->m_current_node->m_bounds),
            ImRect_GetSize(&builder->m_node_rect)
        )) {
        builder->m_current_node->m_bounds.Max = ne_vec2_add(
            builder->m_current_node->m_bounds.Min,
            ImRect_GetSize(&builder->m_node_rect)
        );
        ne_editor_context_make_dirty_node(
            builder->editor,
            ne_save_reason_size,
            builder->m_current_node
        );
    }

    if (builder->m_is_group) {
        for (ne_pin *pin = builder->m_current_node->m_last_pin; pin;
             pin = pin->m_previous_pin)
            ne_object_reset((ne_object *)pin);

        builder->m_current_node->m_type = ne_node_type_group;
        builder->m_current_node->m_group_bounds = builder->m_group_bounds;
        builder->m_current_node->m_last_pin = NULL;
    } else builder->m_current_node->m_type = ne_node_type_node;

    builder->m_current_node = NULL;
}

void ne_node_builder_begin_pin(ne_node_builder *builder, ne_pin_id pin_id, ne_pin_kind kind)
{
    IM_ASSERT(NULL != builder->m_current_node);
    IM_ASSERT(NULL == builder->m_current_pin);
    IM_ASSERT(false == builder->m_is_group);

    ne_style *editor_style = &builder->editor->m_style.base;

    builder->m_current_pin = ne_editor_context_get_pin(builder->editor, pin_id, kind);
    builder->m_current_pin->m_node = builder->m_current_node;

    builder->m_current_pin->base.m_is_live = true;
    builder->m_current_pin->m_color = ne_editor_context_get_color(builder->editor, ne_style_color_pin_rect);
    builder->m_current_pin->m_border_color = ne_editor_context_get_color(builder->editor, ne_style_color_pin_rect_border);
    builder->m_current_pin->m_border_width = editor_style->pin_border_width;
    builder->m_current_pin->m_rounding = editor_style->pin_rounding;
    builder->m_current_pin->m_corners = (int)editor_style->pin_corners;
    builder->m_current_pin->m_radius = editor_style->pin_radius;
    builder->m_current_pin->m_arrow_size = editor_style->pin_arrow_size;
    builder->m_current_pin->m_arrow_width = editor_style->pin_arrow_width;
    builder->m_current_pin->m_dir = kind == ne_pin_kind_output
                                      ? editor_style->source_direction
                                      : editor_style->target_direction;
    builder->m_current_pin->m_strength = editor_style->link_strength;
    builder->m_current_pin->m_snap_link_to_dir = editor_style->snap_link_to_pin_dir !=
                                                 0.0f;

    builder->m_current_pin->m_previous_pin = builder->m_current_node->m_last_pin;
    builder->m_current_node->m_last_pin = builder->m_current_pin;

    builder->m_pivot_alignment = editor_style->pivot_alignment;
    builder->m_pivot_size = editor_style->pivot_size;
    builder->m_pivot_scale = editor_style->pivot_scale;
    builder->m_resolve_pin_rect = true;
    builder->m_resolve_pivot = true;

    if (ne_editor_context_get_draw_list(builder->editor)) {
        ImDrawList *draw_list = ne_editor_context_get_draw_list(builder->editor);
        ImDrawListSplitter_Clear(&builder->m_pin_splitter);
        ne_draw_list_swap_splitter(draw_list, &builder->m_pin_splitter);
    }

    ImGui_BeginGroup();
}

void ne_node_builder_end_pin(ne_node_builder *builder)
{
    IM_ASSERT(NULL != builder->m_current_pin);

    if (ne_editor_context_get_draw_list(builder->editor)) {
        ImDrawList *draw_list = ne_editor_context_get_draw_list(builder->editor);
        IM_ASSERT(draw_list->_Splitter._Count == 1);
        ne_draw_list_swap_splitter(draw_list, &builder->m_pin_splitter);
    }

    ImGui_EndGroup();

    if (builder->m_resolve_pin_rect)
        builder->m_current_pin->m_bounds = (ImRect){ ImGui_GetItemRectMin(),
                                                     ImGui_GetItemRectMax() };

    if (builder->m_resolve_pivot) {
        ImRect *pin_rect = &builder->m_current_pin->m_bounds;

        if (builder->m_pivot_size.x < 0)
            builder->m_pivot_size.x = ImRect_GetWidth(pin_rect);
        if (builder->m_pivot_size.y < 0)
            builder->m_pivot_size.y = ImRect_GetHeight(pin_rect);

        builder->m_current_pin->m_pivot.Min = ne_vec2_add(
            pin_rect->Min,
            ne_vec2_mul(ImRect_GetSize(pin_rect), builder->m_pivot_alignment)
        );
        builder->m_current_pin->m_pivot.Max = ne_vec2_add(
            builder->m_current_pin->m_pivot.Min,
            ne_vec2_mul(builder->m_pivot_size, builder->m_pivot_scale)
        );
    }

    builder->m_current_pin = NULL;
}

void ne_node_builder_pin_rect(ne_node_builder *builder, ImVec2 a, ImVec2 b)
{
    IM_ASSERT(NULL != builder->m_current_pin);

    builder->m_current_pin->m_bounds = (ImRect){ a, b };
    ne_floor_rect(&builder->m_current_pin->m_bounds);
    builder->m_resolve_pin_rect = false;
}

void ne_node_builder_pin_pivot_rect(ne_node_builder *builder, ImVec2 a, ImVec2 b)
{
    IM_ASSERT(NULL != builder->m_current_pin);

    builder->m_current_pin->m_pivot = (ImRect){ a, b };
    builder->m_resolve_pivot = false;
}

void ne_node_builder_pin_pivot_size(ne_node_builder *builder, ImVec2 size)
{
    IM_ASSERT(NULL != builder->m_current_pin);

    builder->m_pivot_size = size;
    builder->m_resolve_pivot = true;
}

void ne_node_builder_pin_pivot_scale(ne_node_builder *builder, ImVec2 scale)
{
    IM_ASSERT(NULL != builder->m_current_pin);

    builder->m_pivot_scale = scale;
    builder->m_resolve_pivot = true;
}

void ne_node_builder_pin_pivot_alignment(ne_node_builder *builder, ImVec2 alignment)
{
    IM_ASSERT(NULL != builder->m_current_pin);

    builder->m_pivot_alignment = alignment;
    builder->m_resolve_pivot = true;
}

void ne_node_builder_group(ne_node_builder *builder, ImVec2 size)
{
    IM_ASSERT(NULL != builder->m_current_node);
    IM_ASSERT(NULL == builder->m_current_pin);
    IM_ASSERT(false == builder->m_is_group);

    builder->m_is_group = true;

    if (ne_is_group(builder->m_current_node))
        ImGui_Dummy(ImRect_GetSize(&builder->m_current_node->m_group_bounds));
    else ImGui_Dummy(size);

    builder->m_group_bounds = (ImRect){ ImGui_GetItemRectMin(),
                                        ImGui_GetItemRectMax() };
    ne_floor_rect(&builder->m_group_bounds);
}

ImDrawList *ne_node_builder_get_user_background_draw_list(ne_node_builder *builder)
{
    return ne_node_builder_get_user_background_draw_list_node(builder, builder->m_current_node);
}

ImDrawList *ne_node_builder_get_user_background_draw_list_node(ne_node_builder *builder, ne_node *node)
{
    if (node && node->base.m_is_live) {
        ImDrawList *draw_list = ne_editor_context_get_draw_list(builder->editor);
        ImDrawList_ChannelsSetCurrent(draw_list, node->m_channel + c_node_user_background_channel);
        return draw_list;
    } else return NULL;
}

//------------------------------------------------------------------------------
// Hint Builder.
//------------------------------------------------------------------------------
void ne_hint_builder_init(ne_hint_builder *builder, ne_editor_context *editor)
{
    memset(builder, 0, sizeof(*builder));
    builder->editor = editor;
}

bool ne_hint_builder_begin(ne_hint_builder *builder, ne_node_id node_id)
{
    IM_ASSERT(NULL == builder->m_current_node);

    ne_canvas_view view = ne_canvas_get_view(&builder->editor->m_canvas);
    ImRect rect = ne_canvas_rect(&builder->editor->m_canvas);

    const float c_min_zoom = 0.75f;
    const float c_max_zoom = 0.50f;

    if (view.Scale > 0.75f) return false;

    ne_node *node = ne_editor_context_find_node(builder->editor, node_id);
    if (!ne_is_group(node)) return false;

    builder->m_current_node = node;

    builder->m_last_channel = ne_editor_context_get_draw_list(builder->editor)
                                  ->_Splitter._Current;

    ne_editor_context_suspend(builder->editor, ne_suspend_flags_keep_splitter);

    const float alpha = ne_imax_f(
        0.0f,
        ne_imin_f(1.0f, (view.Scale - c_min_zoom) / (c_max_zoom - c_min_zoom))
    );

    ImDrawList_ChannelsSetCurrent(ne_editor_context_get_draw_list(builder->editor), c_user_channel_hints_background);
    ImGui_PushClipRect(
        ne_vec2_add(rect.Min, (ImVec2){ 1, 1 }),
        ne_vec2_sub(rect.Max, (ImVec2){ 1, 1 }),
        false
    );

    ImDrawList_ChannelsSetCurrent(ne_editor_context_get_draw_list(builder->editor), c_user_channel_hints);
    ImGui_PushClipRect(
        ne_vec2_add(rect.Min, (ImVec2){ 1, 1 }),
        ne_vec2_sub(rect.Max, (ImVec2){ 1, 1 }),
        false
    );

    ImGui_PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    builder->m_is_active = true;

    return true;
}

void ne_hint_builder_end(ne_hint_builder *builder)
{
    if (!builder->m_is_active) return;

    ImGui_PopStyleVar();

    ImDrawList_ChannelsSetCurrent(ne_editor_context_get_draw_list(builder->editor), c_user_channel_hints);
    ImGui_PopClipRect();

    ImDrawList_ChannelsSetCurrent(ne_editor_context_get_draw_list(builder->editor), c_user_channel_hints_background);
    ImGui_PopClipRect();

    ImDrawList_ChannelsSetCurrent(
        ne_editor_context_get_draw_list(builder->editor),
        builder->m_last_channel
    );

    ne_editor_context_resume(builder->editor, ne_suspend_flags_keep_splitter);

    builder->m_is_active = false;
    builder->m_current_node = NULL;
}

ImVec2 ne_hint_builder_get_group_min(ne_hint_builder *builder)
{
    IM_ASSERT(NULL != builder->m_current_node);
    return ne_editor_context_to_screen(
        builder->editor,
        builder->m_current_node->m_bounds.Min
    );
}

ImVec2 ne_hint_builder_get_group_max(ne_hint_builder *builder)
{
    IM_ASSERT(NULL != builder->m_current_node);
    return ne_editor_context_to_screen(
        builder->editor,
        builder->m_current_node->m_bounds.Max
    );
}

ImDrawList *ne_hint_builder_get_foreground_draw_list(ne_hint_builder *builder)
{
    IM_ASSERT(NULL != builder->m_current_node);
    ImDrawList *draw_list = ne_editor_context_get_draw_list(builder->editor);
    ImDrawList_ChannelsSetCurrent(draw_list, c_user_channel_hints);
    return draw_list;
}

ImDrawList *ne_hint_builder_get_background_draw_list(ne_hint_builder *builder)
{
    IM_ASSERT(NULL != builder->m_current_node);
    ImDrawList *draw_list = ne_editor_context_get_draw_list(builder->editor);
    ImDrawList_ChannelsSetCurrent(draw_list, c_user_channel_hints_background);
    return draw_list;
}

//------------------------------------------------------------------------------
// Style.
//------------------------------------------------------------------------------
static float *ne_style_get_var_float_addr(ne_style *style, int idx)
{
    switch (idx) {
        case ne_style_var_node_rounding: return &style->node_rounding;
        case ne_style_var_node_border_width: return &style->node_border_width;
        case ne_style_var_hovered_node_border_width:
            return &style->hovered_node_border_width;
        case ne_style_var_selected_node_border_width:
            return &style->selected_node_border_width;
        case ne_style_var_pin_rounding: return &style->pin_rounding;
        case ne_style_var_pin_border_width: return &style->pin_border_width;
        case ne_style_var_link_strength: return &style->link_strength;
        case ne_style_var_scroll_duration: return &style->scroll_duration;
        case ne_style_var_flow_marker_distance:
            return &style->flow_marker_distance;
        case ne_style_var_flow_speed: return &style->flow_speed;
        case ne_style_var_flow_duration: return &style->flow_duration;
        case ne_style_var_pin_corners: return &style->pin_corners;
        case ne_style_var_pin_radius: return &style->pin_radius;
        case ne_style_var_pin_arrow_size: return &style->pin_arrow_size;
        case ne_style_var_pin_arrow_width: return &style->pin_arrow_width;
        case ne_style_var_group_rounding: return &style->group_rounding;
        case ne_style_var_group_border_width: return &style->group_border_width;
        case ne_style_var_highlight_connected_links:
            return &style->highlight_connected_links;
        case ne_style_var_snap_link_to_pin_dir:
            return &style->snap_link_to_pin_dir;
        case ne_style_var_hovered_node_border_offset:
            return &style->hover_node_border_offset;
        case ne_style_var_selected_node_border_offset:
            return &style->selected_node_border_offset;
        default: return NULL;
    }
}

static ImVec2 *ne_style_get_var_vec2_addr(ne_style *style, int idx)
{
    switch (idx) {
        case ne_style_var_source_direction: return &style->source_direction;
        case ne_style_var_target_direction: return &style->target_direction;
        case ne_style_var_pivot_alignment: return &style->pivot_alignment;
        case ne_style_var_pivot_size: return &style->pivot_size;
        case ne_style_var_pivot_scale: return &style->pivot_scale;
        default: return NULL;
    }
}

static ImVec4 *ne_style_get_var_vec4_addr(ne_style *style, int idx)
{
    switch (idx) {
        case ne_style_var_node_padding: return &style->node_padding;
        default: return NULL;
    }
}

void ne_style_default(ne_style *style)
{
    memset(style, 0, sizeof(*style));

    style->node_padding = (ImVec4){ 8, 8, 8, 8 };
    style->node_rounding = 12.0f;
    style->node_border_width = 1.5f;
    style->hovered_node_border_width = 3.5f;
    style->hover_node_border_offset = 0.0f;
    style->selected_node_border_width = 3.5f;
    style->selected_node_border_offset = 0.0f;
    style->pin_rounding = 4.0f;
    style->pin_border_width = 0.0f;
    style->link_strength = 100.0f;
    style->source_direction = (ImVec2){ 1.0f, 0.0f };
    style->target_direction = (ImVec2){ -1.0f, 0.0f };
    style->scroll_duration = 0.35f;
    style->flow_marker_distance = 30.0f;
    style->flow_speed = 150.0f;
    style->flow_duration = 2.0f;
    style->pivot_alignment = (ImVec2){ 0.5f, 0.5f };
    style->pivot_size = (ImVec2){ 0.0f, 0.0f };
    style->pivot_scale = (ImVec2){ 1, 1 };
    style->pin_corners = (float)ImDrawFlags_RoundCornersAll;
    style->pin_radius = 0.0f;
    style->pin_arrow_size = 0.0f;
    style->pin_arrow_width = 0.0f;
    style->group_rounding = 6.0f;
    style->group_border_width = 1.0f;
    style->highlight_connected_links = 0.0f;
    style->snap_link_to_pin_dir = 0.0f;

    style->colors[ne_style_color_bg] = (ImVec4){ 60, 60, 70, 200 };
    style->colors[ne_style_color_grid] = (ImVec4){ 120, 120, 120, 40 };
    style->colors[ne_style_color_node_bg] = (ImVec4){ 32, 32, 32, 200 };
    style->colors[ne_style_color_node_border] = (ImVec4){ 255, 255, 255, 96 };
    style->colors[ne_style_color_hov_node_border] = (ImVec4){ 50, 176, 255, 255 };
    style->colors[ne_style_color_sel_node_border] = (ImVec4){ 255, 176, 50, 255 };
    style->colors[ne_style_color_node_sel_rect] = (ImVec4){ 5, 130, 255, 64 };
    style->colors[ne_style_color_node_sel_rect_border] = (ImVec4){ 5, 130, 255, 128 };
    style->colors[ne_style_color_hov_link_border] = (ImVec4){ 50, 176, 255, 255 };
    style->colors[ne_style_color_sel_link_border] = (ImVec4){ 255, 176, 50, 255 };
    style->colors[ne_style_color_highlight_link_border] = (ImVec4){ 204, 105, 0, 255 };
    style->colors[ne_style_color_link_sel_rect] = (ImVec4){ 5, 130, 255, 64 };
    style->colors[ne_style_color_link_sel_rect_border] = (ImVec4){ 5, 130, 255, 128 };
    style->colors[ne_style_color_pin_rect] = (ImVec4){ 60, 180, 255, 100 };
    style->colors[ne_style_color_pin_rect_border] = (ImVec4){ 60, 180, 255, 128 };
    style->colors[ne_style_color_flow] = (ImVec4){ 255, 128, 64, 255 };
    style->colors[ne_style_color_flow_marker] = (ImVec4){ 255, 128, 64, 255 };
    style->colors[ne_style_color_group_bg] = (ImVec4){ 0, 0, 0, 160 };
    style->colors[ne_style_color_group_border] = (ImVec4){ 255, 255, 255, 32 };
}

void ne_style_ctx_init(ne_style_ctx *style)
{
    memset(style, 0, sizeof(*style));
    ne_style_default(&style->base);
}

void ne_style_ctx_push_color(ne_style_ctx *style, ne_style_color color_index, ImVec4 color)
{
    ne_style_color_modifier modifier;
    modifier.index = color_index;
    modifier.value = style->base.colors[color_index];
    apush(style->color_stack, modifier);
    style->base.colors[color_index] = color;
}

void ne_style_ctx_pop_color(ne_style_ctx *style, int count)
{
    while (count > 0) {
        ne_style_color_modifier *modifier = &style->color_stack[acount(style->color_stack) - 1];
        style->base.colors[modifier->index] = modifier->value;
        AHDR(style->color_stack)->size--;
        --count;
    }
}

void ne_style_ctx_push_var_f(ne_style_ctx *style, ne_style_var var_index, float value)
{
    float *var = ne_style_get_var_float_addr(&style->base, var_index);
    IM_ASSERT(var != NULL);
    ne_style_var_modifier modifier;
    modifier.index = var_index;
    modifier.value = (ImVec4){ *var, 0, 0, 0 };
    *var = value;
    apush(style->var_stack, modifier);
}

void ne_style_ctx_push_var_v2(ne_style_ctx *style, ne_style_var var_index, ImVec2 value)
{
    ImVec2 *var = ne_style_get_var_vec2_addr(&style->base, var_index);
    IM_ASSERT(var != NULL);
    ne_style_var_modifier modifier;
    modifier.index = var_index;
    modifier.value = (ImVec4){ var->x, var->y, 0, 0 };
    *var = value;
    apush(style->var_stack, modifier);
}

void ne_style_ctx_push_var_v4(ne_style_ctx *style, ne_style_var var_index, ImVec4 value)
{
    ImVec4 *var = ne_style_get_var_vec4_addr(&style->base, var_index);
    IM_ASSERT(var != NULL);
    ne_style_var_modifier modifier;
    modifier.index = var_index;
    modifier.value = *var;
    *var = value;
    apush(style->var_stack, modifier);
}

void ne_style_ctx_pop_var(ne_style_ctx *style, int count)
{
    while (count > 0) {
        ne_style_var_modifier *modifier = &style->var_stack[acount(style->var_stack) - 1];
        float *float_value = ne_style_get_var_float_addr(&style->base, modifier->index);
        if (float_value) *float_value = modifier->value.x;
        else {
            ImVec2
                *vec2_value = ne_style_get_var_vec2_addr(&style->base, modifier->index);
            if (vec2_value)
                *vec2_value = (ImVec2){ modifier->value.x, modifier->value.y };
            else {
                ImVec4 *vec4_value = ne_style_get_var_vec4_addr(
                    &style->base,
                    modifier->index
                );
                if (vec4_value) *vec4_value = modifier->value;
            }
        }
        AHDR(style->var_stack)->size--;
        --count;
    }
}

const char *ne_style_ctx_get_color_name(const ne_style_ctx *style, ne_style_color color_index)
{
    (void)style;
    switch (color_index) {
        case ne_style_color_bg: return "Bg";
        case ne_style_color_grid: return "Grid";
        case ne_style_color_node_bg: return "NodeBg";
        case ne_style_color_node_border: return "NodeBorder";
        case ne_style_color_hov_node_border: return "HoveredNodeBorder";
        case ne_style_color_sel_node_border: return "SelNodeBorder";
        case ne_style_color_node_sel_rect: return "NodeSelRect";
        case ne_style_color_node_sel_rect_border: return "NodeSelRectBorder";
        case ne_style_color_hov_link_border: return "HoveredLinkBorder";
        case ne_style_color_sel_link_border: return "SelLinkBorder";
        case ne_style_color_highlight_link_border: return "HighlightLinkBorder";
        case ne_style_color_link_sel_rect: return "LinkSelRect";
        case ne_style_color_link_sel_rect_border: return "LinkSelRectBorder";
        case ne_style_color_pin_rect: return "PinRect";
        case ne_style_color_pin_rect_border: return "PinRectBorder";
        case ne_style_color_flow: return "Flow";
        case ne_style_color_flow_marker: return "FlowMarker";
        case ne_style_color_group_bg: return "GroupBg";
        case ne_style_color_group_border: return "GroupBorder";
        case ne_style_color_count: break;
    }

    IM_ASSERT(0);
    return "Unknown";
}

//------------------------------------------------------------------------------
// Config (upstream Detail::Config; file I/O via stdio).
//------------------------------------------------------------------------------
void ne_config_default(ne_config *config)
{
    memset(config, 0, sizeof(*config));
    config->settings_file = "NodeEditor.json";
    config->canvas_size_mode = ne_canvas_size_mode_fit_vertical_view;
    config->drag_button_index = 0;
    config->select_button_index = 0;
    config->navigate_button_index = 1;
    config->context_menu_button_index = 1;
    config->enable_smooth_zoom = false;
#ifdef __APPLE__
    config->smooth_zoom_power = 1.1f;
#else
    config->smooth_zoom_power = 1.3f;
#endif
}

char *ne_config_load(ne_config *config)
{
    char *data = NULL;

    if (config->load_settings) {
        size_t size = config->load_settings(NULL, config->user_pointer);
        if (size > 0) {
            data = (char *)malloc(size + 1);
            config->load_settings(data, config->user_pointer);
            data[size] = 0;
        }
    } else if (config->settings_file) {
        FILE *file = fopen(config->settings_file, "rb");
        if (file) {
            fseek(file, 0, SEEK_END);
            long size = ftell(file);
            fseek(file, 0, SEEK_SET);

            if (size > 0) {
                data = (char *)malloc((size_t)size + 1);
                size_t n = fread(data, 1, (size_t)size, file);
                data[n] = 0;
            }

            fclose(file);
        }
    }

    return data;
}

char *ne_config_load_node(ne_config *config, ne_node_id node_id)
{
    char *data = NULL;

    if (config->load_node_settings) {
        size_t size = config->load_node_settings(node_id, NULL, config->user_pointer);
        if (size > 0) {
            data = (char *)malloc(size + 1);
            config->load_node_settings(node_id, data, config->user_pointer);
            data[size] = 0;
        }
    }

    return data;
}

void ne_config_begin_save(ne_config *config)
{
    if (config->begin_save_session)
        config->begin_save_session(config->user_pointer);
}

bool ne_config_save(ne_config *config, const char *data, ne_save_reason_flags flags)
{
    if (config->save_settings) {
        return config->save_settings(data, strlen(data), flags, config->user_pointer);
    } else if (config->settings_file) {
        FILE *file = fopen(config->settings_file, "wb");
        if (file) {
            fwrite(data, 1, strlen(data), file);
            fclose(file);
            return true;
        }
        return false;
    }

    return false;
}

bool ne_config_save_node(ne_config *config, ne_node_id node_id, const char *data, ne_save_reason_flags flags)
{
    if (config->save_node_settings)
        return config
            ->save_node_settings(node_id, data, strlen(data), flags, config->user_pointer);

    return false;
}

void ne_config_end_save(ne_config *config)
{
    if (config->end_save_session)
        config->end_save_session(config->user_pointer);
}

//------------------------------------------------------------------------------
// Editor context settings load/save + animations + metrics.
//------------------------------------------------------------------------------
void ne_editor_context_load_settings(ne_editor_context *editor)
{
    char *data = ne_config_load(&editor->m_config);
    if (data) {
        ne_settings_parse_str(data, &editor->m_settings);
        free(data);
    }

    if (ne_rect_is_empty(editor->m_settings.m_visible_rect)) {
        editor->m_navigate_action.m_scroll = editor->m_settings.m_view_scroll;
        editor->m_navigate_action.m_zoom = editor->m_settings.m_view_zoom;
    } else {
        ne_navigate_action_navigate_to(
            &editor->m_navigate_action,
            editor->m_settings.m_visible_rect,
            ne_zoom_mode_exact,
            0.0f,
            ne_navigation_reason_unknown
        );
    }
}

void ne_editor_context_save_settings(ne_editor_context *editor)
{
    ne_config_begin_save(&editor->m_config);

    for (int i = 0; i < acount(editor->m_nodes); ++i) {
        ne_node *node = editor->m_nodes[i];
        ne_node_settings *settings = ne_settings_find_node(&editor->m_settings, node->m_id);
        settings->m_location = node->m_bounds.Min;
        settings->m_size = ImRect_GetSize(&node->m_bounds);
        if (ne_is_group(node))
            settings->m_group_size = ImRect_GetSize(&node->m_group_bounds);

        if (!node->m_restore_state && settings->m_is_dirty &&
            editor->m_config.save_node_settings) {
            char *data = ne_node_settings_serialize(settings);
            if (ne_config_save_node(
                    &editor->m_config,
                    node->m_id,
                    data,
                    settings->m_dirty_reason
                ))
                ne_node_settings_clear_dirty(settings);
            free(data);
        }
    }

    afree(editor->m_settings.m_selection);
    editor->m_settings.m_selection = NULL;
    for (int i = 0; i < acount(editor->m_selected_objects); ++i)
        apush(editor->m_settings.m_selection, ne_object_id_of(editor->m_selected_objects[i]));

    editor->m_settings.m_view_scroll = editor->m_navigate_action.m_scroll;
    editor->m_settings.m_view_zoom = editor->m_navigate_action.m_zoom;
    editor->m_settings.m_visible_rect = editor->m_navigate_action.m_visible_rect;

    char *data = ne_settings_serialize(&editor->m_settings);
    if (ne_config_save(&editor->m_config, data, editor->m_settings.m_dirty_reason))
        ne_settings_clear_dirty(&editor->m_settings, NULL);
    free(data);

    ne_config_end_save(&editor->m_config);
}

void ne_editor_context_update_animations(ne_editor_context *editor)
{
    afree(editor->m_last_live_animations);
    editor->m_last_live_animations = NULL;
    for (int i = 0; i < acount(editor->m_live_animations); ++i)
        apush(editor->m_last_live_animations, editor->m_live_animations[i]);

    for (int i = 0; i < acount(editor->m_last_live_animations); ++i) {
        ne_animation *animation = editor->m_last_live_animations[i];
        bool is_live = false;
        for (int j = 0; j < acount(editor->m_live_animations); ++j)
            if (editor->m_live_animations[j] == animation) {
                is_live = true;
                break;
            }

        if (is_live) ne_animation_update(animation);
    }
}

void ne_editor_context_show_metrics(ne_editor_context *editor, const ne_control *control)
{
    ImGuiIO *io = ImGui_GetIO();

    ImVec2 local_mouse_pos = ne_canvas_to_local(&editor->m_canvas, io->MousePos);
    ImVec2 global_mouse_pos = io->MousePos;

    ImGui_SetCursorScreenPos(
        ne_vec2_add(ne_canvas_rect(&editor->m_canvas).Min, (ImVec2){ 5, 5 })
    );
    ImGui_BeginGroup();
    ImGui_Text("Is Focused: %s", editor->m_is_focused ? "true" : "false");
    ImGui_Text("Is Hovered: %s", editor->m_is_hovered ? "true" : "false");
    ImGui_Text("Is Hovered (without overlapp): %s", editor->m_is_hovered_without_overlapp ? "true" : "false");
    ImGui_Text("Accept Input: %s", ne_editor_context_can_accept_user_input(editor) ? "true" : "false");
    ImGui_Text(
        "View Position: { x=%g y=%g }",
        ne_canvas_view_rect(&editor->m_canvas).Min.x,
        ne_canvas_view_rect(&editor->m_canvas).Min.y
    );
    ImGui_Text(
        "View Size: { w=%g h=%g }",
        ne_canvas_view_rect(&editor->m_canvas).Max.x -
            ne_canvas_view_rect(&editor->m_canvas).Min.x,
        ne_canvas_view_rect(&editor->m_canvas).Max.y -
            ne_canvas_view_rect(&editor->m_canvas).Min.y
    );
    ImGui_Text(
        "Canvas Size: { w=%g h=%g }",
        ne_canvas_rect(&editor->m_canvas).Max.x -
            ne_canvas_rect(&editor->m_canvas).Min.x,
        ne_canvas_rect(&editor->m_canvas).Max.y -
            ne_canvas_rect(&editor->m_canvas).Min.y
    );
    ImGui_Text(
        "Mouse: { x=%.0f y=%.0f } global: { x=%g y=%g }",
        local_mouse_pos.x,
        local_mouse_pos.y,
        global_mouse_pos.x,
        global_mouse_pos.y
    );
    ImGui_Text("Live Nodes: %d", ne_editor_context_count_live_nodes(editor));
    ImGui_Text("Live Pins: %d", ne_editor_context_count_live_pins(editor));
    ImGui_Text("Live Links: %d", ne_editor_context_count_live_links(editor));
    ImGui_Text(
        "Hot Object: %s (%p)",
        ne_get_object_name(control->hot_object),
        control->hot_object ? (void *)ne_object_id_of(control->hot_object).id : NULL
    );
    ImGui_Text(
        "Active Object: %s (%p)",
        ne_get_object_name(control->active_object),
        control->active_object ? (void *)ne_object_id_of(control->active_object).id : NULL
    );
    ImGui_Text(
        "Action: %s",
        editor->m_current_action ? ne_action_get_name(editor->m_current_action) : "<none>"
    );
    ImGui_Text(
        "Action Is Dragging: %s",
        editor->m_current_action && ne_action_is_dragging(editor->m_current_action) ? "Yes" : "No"
    );
    ne_navigate_action_show_metrics(&editor->m_navigate_action);
    ne_size_action_show_metrics(&editor->m_size_action);
    ne_drag_action_show_metrics(&editor->m_drag_action);
    ne_select_action_show_metrics(&editor->m_select_action);
    ne_context_menu_action_show_metrics(&editor->m_context_menu_action);
    ne_create_item_action_show_metrics(&editor->m_create_item_action);
    ne_delete_items_action_show_metrics(&editor->m_delete_items_action);
    ImGui_EndGroup();
}

//------------------------------------------------------------------------------
// Public API (upstream imgui_node_editor_api.cpp).
//------------------------------------------------------------------------------
static ne_editor_context *s_editor = NULL;

ne_editor_context *ne_create_editor(const ne_config *config)
{
    ne_editor_context *editor = (ne_editor_context *)malloc(sizeof(ne_editor_context));
    ne_editor_context_init(editor, config);
    return editor;
}

void ne_destroy_editor(ne_editor_context *ctx)
{
    ne_editor_context *last_context = ne_get_current_editor();

    if (last_context != ctx) ne_set_current_editor(ctx);

    ne_editor_context_free(ctx);
    free(ctx);

    if (last_context != ctx) ne_set_current_editor(last_context);
}

const ne_config *ne_get_config(ne_editor_context *ctx)
{
    if (ctx == NULL) ctx = ne_get_current_editor();

    if (ctx) return &ctx->m_config;
    else {
        static ne_config s_empty_config;
        static bool s_empty_config_init = false;
        if (!s_empty_config_init) {
            ne_config_default(&s_empty_config);
            s_empty_config_init = true;
        }
        return &s_empty_config;
    }
}

void ne_set_current_editor(ne_editor_context *ctx) { s_editor = ctx; }

ne_editor_context *ne_get_current_editor(void) { return s_editor; }

ne_style *ne_get_style(void) { return &s_editor->m_style.base; }

const char *ne_get_style_color_name(ne_style_color color_index)
{ return ne_style_ctx_get_color_name(&s_editor->m_style, color_index); }

void ne_push_style_color(ne_style_color color_index, const ImVec4 *color)
{ ne_style_ctx_push_color(&s_editor->m_style, color_index, *color); }

void ne_pop_style_color(int count)
{ ne_style_ctx_pop_color(&s_editor->m_style, count); }

void ne_push_style_var_f(ne_style_var var_index, float value)
{ ne_style_ctx_push_var_f(&s_editor->m_style, var_index, value); }

void ne_push_style_var_v2(ne_style_var var_index, ImVec2 value)
{ ne_style_ctx_push_var_v2(&s_editor->m_style, var_index, value); }

void ne_push_style_var_v4(ne_style_var var_index, ImVec4 value)
{ ne_style_ctx_push_var_v4(&s_editor->m_style, var_index, value); }

void ne_pop_style_var(int count)
{ ne_style_ctx_pop_var(&s_editor->m_style, count); }

void ne_begin(const char *id, ImVec2 size)
{ ne_editor_context_begin(s_editor, id, size); }

void ne_end(void) { ne_editor_context_end(s_editor); }

void ne_begin_node(ne_node_id id)
{ ne_node_builder_begin(&s_editor->m_node_builder, id); }

void ne_begin_pin(ne_pin_id id, ne_pin_kind kind)
{ ne_node_builder_begin_pin(&s_editor->m_node_builder, id, kind); }

void ne_pin_rect(ImVec2 a, ImVec2 b)
{ ne_node_builder_pin_rect(&s_editor->m_node_builder, a, b); }

void ne_pin_pivot_rect(ImVec2 a, ImVec2 b)
{ ne_node_builder_pin_pivot_rect(&s_editor->m_node_builder, a, b); }

void ne_pin_pivot_size(ImVec2 size)
{ ne_node_builder_pin_pivot_size(&s_editor->m_node_builder, size); }

void ne_pin_pivot_scale(ImVec2 scale)
{ ne_node_builder_pin_pivot_scale(&s_editor->m_node_builder, scale); }

void ne_pin_pivot_alignment(ImVec2 alignment)
{ ne_node_builder_pin_pivot_alignment(&s_editor->m_node_builder, alignment); }

void ne_end_pin(void) { ne_node_builder_end_pin(&s_editor->m_node_builder); }

void ne_group(ImVec2 size)
{ ne_node_builder_group(&s_editor->m_node_builder, size); }

void ne_end_node(void) { ne_node_builder_end(&s_editor->m_node_builder); }

bool ne_begin_group_hint(ne_node_id node_id)
{ return ne_hint_builder_begin(&s_editor->m_hint_builder, node_id); }

ImVec2 ne_get_group_min(void)
{ return ne_hint_builder_get_group_min(&s_editor->m_hint_builder); }

ImVec2 ne_get_group_max(void)
{ return ne_hint_builder_get_group_max(&s_editor->m_hint_builder); }

ImDrawList *ne_get_hint_foreground_draw_list(void)
{ return ne_hint_builder_get_foreground_draw_list(&s_editor->m_hint_builder); }

ImDrawList *ne_get_hint_background_draw_list(void)
{ return ne_hint_builder_get_background_draw_list(&s_editor->m_hint_builder); }

void ne_end_group_hint(void) { ne_hint_builder_end(&s_editor->m_hint_builder); }

ImDrawList *ne_get_node_background_draw_list(ne_node_id node_id)
{
    ne_node *node = ne_editor_context_find_node(s_editor, node_id);
    if (node)
        return ne_node_builder_get_user_background_draw_list_node(&s_editor->m_node_builder, node);
    else return NULL;
}

bool ne_link(ne_link_id id, ne_pin_id start_pin_id, ne_pin_id end_pin_id, ImVec4 color, float thickness)
{
    ImU32 c = IM_COL32(
        (int)(color.x * 255.0f),
        (int)(color.y * 255.0f),
        (int)(color.z * 255.0f),
        (int)(color.w * 255.0f)
    );
    return ne_editor_context_do_link(s_editor, id, start_pin_id, end_pin_id, c, thickness);
}

void ne_flow(ne_link_id link_id, ne_flow_direction direction)
{
    struct ne_link *link = ne_editor_context_find_link(s_editor, link_id);
    if (link) ne_editor_context_flow(s_editor, link, direction);
}

bool ne_begin_create(ImVec4 color, float thickness)
{
    ne_create_item_action *context = &s_editor->m_create_item_action;

    if (ne_create_item_action_begin(context)) {
        ne_create_item_action_set_style(
            context,
            IM_COL32(
                (int)(color.x * 255.0f),
                (int)(color.y * 255.0f),
                (int)(color.z * 255.0f),
                (int)(color.w * 255.0f)
            ),
            thickness
        );
        return true;
    } else return false;
}

bool ne_query_new_link(ne_pin_id *start_id, ne_pin_id *end_id)
{
    return ne_create_item_action_query_link(&s_editor->m_create_item_action, start_id, end_id) ==
           ne_create_result_true;
}

bool ne_query_new_link_ex(ne_pin_id *start_id, ne_pin_id *end_id, ImVec4 color, float thickness)
{
    ne_create_item_action *context = &s_editor->m_create_item_action;

    int result = ne_create_item_action_query_link(context, start_id, end_id);
    if (result != ne_create_result_indeterminate)
        ne_create_item_action_set_style(
            context,
            IM_COL32(
                (int)(color.x * 255.0f),
                (int)(color.y * 255.0f),
                (int)(color.z * 255.0f),
                (int)(color.w * 255.0f)
            ),
            thickness
        );

    return result == ne_create_result_true;
}

bool ne_query_new_node(ne_pin_id *pin_id)
{
    return ne_create_item_action_query_node(&s_editor->m_create_item_action, pin_id) ==
           ne_create_result_true;
}

bool ne_query_new_node_ex(ne_pin_id *pin_id, ImVec4 color, float thickness)
{
    ne_create_item_action *context = &s_editor->m_create_item_action;

    int result = ne_create_item_action_query_node(context, pin_id);
    if (result != ne_create_result_indeterminate)
        ne_create_item_action_set_style(
            context,
            IM_COL32(
                (int)(color.x * 255.0f),
                (int)(color.y * 255.0f),
                (int)(color.z * 255.0f),
                (int)(color.w * 255.0f)
            ),
            thickness
        );

    return result == ne_create_result_true;
}

bool ne_accept_new_item(void)
{
    return ne_create_item_action_accept_item(&s_editor->m_create_item_action) ==
           ne_create_result_true;
}

bool ne_accept_new_item_ex(ImVec4 color, float thickness)
{
    ne_create_item_action *context = &s_editor->m_create_item_action;

    int result = ne_create_item_action_accept_item(context);
    if (result != ne_create_result_indeterminate)
        ne_create_item_action_set_style(
            context,
            IM_COL32(
                (int)(color.x * 255.0f),
                (int)(color.y * 255.0f),
                (int)(color.z * 255.0f),
                (int)(color.w * 255.0f)
            ),
            thickness
        );

    return result == ne_create_result_true;
}

void ne_reject_new_item(void)
{ ne_create_item_action_reject_item(&s_editor->m_create_item_action); }

void ne_reject_new_item_ex(ImVec4 color, float thickness)
{
    ne_create_item_action *context = &s_editor->m_create_item_action;

    if (ne_create_item_action_reject_item(context) != ne_create_result_indeterminate)
        ne_create_item_action_set_style(
            context,
            IM_COL32(
                (int)(color.x * 255.0f),
                (int)(color.y * 255.0f),
                (int)(color.z * 255.0f),
                (int)(color.w * 255.0f)
            ),
            thickness
        );
}

void ne_end_create(void)
{ ne_create_item_action_end(&s_editor->m_create_item_action); }

bool ne_begin_delete(void)
{ return ne_delete_items_action_begin(&s_editor->m_delete_items_action); }

bool ne_query_deleted_link(ne_link_id *link_id, ne_pin_id *start_id, ne_pin_id *end_id)
{
    return ne_delete_items_action_query_link(&s_editor->m_delete_items_action, link_id, start_id, end_id);
}

bool ne_query_deleted_node(ne_node_id *node_id)
{
    return ne_delete_items_action_query_node(&s_editor->m_delete_items_action, node_id);
}

bool ne_accept_deleted_item(bool delete_dependencies)
{
    return ne_delete_items_action_accept_item(&s_editor->m_delete_items_action, delete_dependencies);
}

void ne_reject_deleted_item(void)
{ ne_delete_items_action_reject_item(&s_editor->m_delete_items_action); }

void ne_end_delete(void)
{ ne_delete_items_action_end(&s_editor->m_delete_items_action); }

void ne_set_node_position(ne_node_id node_id, ImVec2 position)
{ ne_editor_context_set_node_position(s_editor, node_id, position); }

void ne_set_group_size(ne_node_id node_id, ImVec2 size)
{ ne_editor_context_set_group_size(s_editor, node_id, size); }

ImVec2 ne_get_node_position(ne_node_id node_id)
{ return ne_editor_context_get_node_position(s_editor, node_id); }

ImVec2 ne_get_node_size(ne_node_id node_id)
{ return ne_editor_context_get_node_size(s_editor, node_id); }

void ne_center_node_on_screen(ne_node_id node_id)
{
    ne_node *node = ne_editor_context_find_node(s_editor, node_id);
    if (node) node->m_center_on_screen = true;
}

void ne_set_node_z_position(ne_node_id node_id, float z)
{ ne_editor_context_set_node_z_position(s_editor, node_id, z); }

float ne_get_node_z_position(ne_node_id node_id)
{ return ne_editor_context_get_node_z_position(s_editor, node_id); }

void ne_restore_node_state(ne_node_id node_id)
{
    ne_node *node = ne_editor_context_find_node(s_editor, node_id);
    if (node) ne_editor_context_mark_node_to_restore_state(s_editor, node);
}

void ne_suspend(void)
{ ne_editor_context_suspend(s_editor, ne_suspend_flags_none); }

void ne_resume(void)
{ ne_editor_context_resume(s_editor, ne_suspend_flags_none); }

bool ne_is_suspended(void) { return ne_editor_context_is_suspended(s_editor); }

bool ne_is_active(void) { return ne_editor_context_is_focused(s_editor); }

bool ne_has_selection_changed(void)
{ return ne_editor_context_has_selection_changed(s_editor); }

int ne_get_selected_object_count(void)
{ return acount(s_editor->m_selected_objects); }

int ne_get_selected_nodes(ne_node_id *nodes, int size)
{
    int count = 0;
    for (int i = 0; i < acount(s_editor->m_selected_objects) && size > 0; ++i) {
        ne_node *node = ne_object_as_node(s_editor->m_selected_objects[i]);
        if (node) {
            nodes[count] = node->m_id;
            ++count;
            --size;
        }
    }
    return count;
}

int ne_get_selected_links(ne_link_id *links, int size)
{
    int count = 0;
    for (int i = 0; i < acount(s_editor->m_selected_objects) && size > 0; ++i) {
        struct ne_link *link = ne_object_as_link(s_editor->m_selected_objects[i]);
        if (link) {
            links[count] = link->m_id;
            ++count;
            --size;
        }
    }
    return count;
}

bool ne_is_node_selected(ne_node_id node_id)
{
    ne_node *node = ne_editor_context_find_node(s_editor, node_id);
    if (node) return ne_editor_context_is_selected(s_editor, (ne_object *)node);
    else return false;
}

bool ne_is_link_selected(ne_link_id link_id)
{
    struct ne_link *link = ne_editor_context_find_link(s_editor, link_id);
    if (link) return ne_editor_context_is_selected(s_editor, (ne_object *)link);
    else return false;
}

void ne_clear_selection(void) { ne_editor_context_clear_selection(s_editor); }

void ne_select_node(ne_node_id node_id, bool append)
{
    ne_node *node = ne_editor_context_find_node(s_editor, node_id);
    if (node) {
        if (append)
            ne_editor_context_select_object(s_editor, (ne_object *)node);
        else ne_editor_context_set_selected_object(s_editor, (ne_object *)node);
    }
}

void ne_select_link(ne_link_id link_id, bool append)
{
    struct ne_link *link = ne_editor_context_find_link(s_editor, link_id);
    if (link) {
        if (append)
            ne_editor_context_select_object(s_editor, (ne_object *)link);
        else ne_editor_context_set_selected_object(s_editor, (ne_object *)link);
    }
}

void ne_deselect_node(ne_node_id node_id)
{
    ne_node *node = ne_editor_context_find_node(s_editor, node_id);
    if (node) ne_editor_context_deselect_object(s_editor, (ne_object *)node);
}

void ne_deselect_link(ne_link_id link_id)
{
    struct ne_link *link = ne_editor_context_find_link(s_editor, link_id);
    if (link) ne_editor_context_deselect_object(s_editor, (ne_object *)link);
}

bool ne_delete_node(ne_node_id node_id)
{
    ne_node *node = ne_editor_context_find_node(s_editor, node_id);
    if (node)
        return ne_delete_items_action_add(&s_editor->m_delete_items_action, (ne_object *)node);
    else return false;
}

bool ne_delete_link(ne_link_id link_id)
{
    struct ne_link *link = ne_editor_context_find_link(s_editor, link_id);
    if (link)
        return ne_delete_items_action_add(&s_editor->m_delete_items_action, (ne_object *)link);
    else return false;
}

bool ne_has_any_links_node(ne_node_id node_id)
{ return ne_editor_context_has_any_links_node(s_editor, node_id); }

bool ne_has_any_links_pin(ne_pin_id pin_id)
{ return ne_editor_context_has_any_links_pin(s_editor, pin_id); }

int ne_break_links_node(ne_node_id node_id)
{ return ne_editor_context_break_links_node(s_editor, node_id); }

int ne_break_links_pin(ne_pin_id pin_id)
{ return ne_editor_context_break_links_pin(s_editor, pin_id); }

void ne_navigate_to_content(float duration)
{
    ne_editor_context_navigate_to(s_editor, ne_editor_context_get_content_bounds(s_editor), true, duration);
}

void ne_navigate_to_selection(bool zoom_in, float duration)
{
    ne_editor_context_navigate_to(s_editor, ne_editor_context_get_selection_bounds(s_editor), zoom_in, duration);
}

bool ne_show_node_context_menu(ne_node_id *node_id)
{
    return ne_context_menu_action_show_node(&s_editor->m_context_menu_action, node_id);
}

bool ne_show_pin_context_menu(ne_pin_id *pin_id)
{
    return ne_context_menu_action_show_pin(&s_editor->m_context_menu_action, pin_id);
}

bool ne_show_link_context_menu(ne_link_id *link_id)
{
    return ne_context_menu_action_show_link(&s_editor->m_context_menu_action, link_id);
}

bool ne_show_background_context_menu(void)
{
    return ne_context_menu_action_show_background(&s_editor->m_context_menu_action);
}

void ne_enable_shortcuts(bool enable)
{ ne_editor_context_enable_shortcuts(s_editor, enable); }

bool ne_are_shortcuts_enabled(void)
{ return ne_editor_context_are_shortcuts_enabled(s_editor); }

bool ne_begin_shortcut(void)
{ return ne_shortcut_action_begin(&s_editor->m_shortcut_action); }

bool ne_accept_cut(void)
{ return ne_shortcut_action_accept_cut(&s_editor->m_shortcut_action); }

bool ne_accept_copy(void)
{ return ne_shortcut_action_accept_copy(&s_editor->m_shortcut_action); }

bool ne_accept_paste(void)
{ return ne_shortcut_action_accept_paste(&s_editor->m_shortcut_action); }

bool ne_accept_duplicate(void)
{ return ne_shortcut_action_accept_duplicate(&s_editor->m_shortcut_action); }

bool ne_accept_create_node(void)
{ return ne_shortcut_action_accept_create_node(&s_editor->m_shortcut_action); }

int ne_get_action_context_size(void)
{ return acount(s_editor->m_shortcut_action.m_context); }

int ne_get_action_context_nodes(ne_node_id *nodes, int size)
{
    int count = 0;
    for (int i = 0; i < acount(s_editor->m_shortcut_action.m_context) && size > 0; ++i) {
        ne_node *node = ne_object_as_node(s_editor->m_shortcut_action.m_context[i]);
        if (node) {
            nodes[count] = node->m_id;
            ++count;
            --size;
        }
    }
    return count;
}

int ne_get_action_context_links(ne_link_id *links, int size)
{
    int count = 0;
    for (int i = 0; i < acount(s_editor->m_shortcut_action.m_context) && size > 0; ++i) {
        struct ne_link *link = ne_object_as_link(s_editor->m_shortcut_action.m_context[i]);
        if (link) {
            links[count] = link->m_id;
            ++count;
            --size;
        }
    }
    return count;
}

void ne_end_shortcut(void)
{ ne_shortcut_action_end(&s_editor->m_shortcut_action); }

float ne_get_current_zoom(void)
{ return ne_canvas_get_view(&s_editor->m_canvas).InvScale; }

ne_node_id ne_get_hovered_node(void) { return s_editor->m_hovered_node; }
ne_pin_id ne_get_hovered_pin(void) { return s_editor->m_hovered_pin; }
ne_link_id ne_get_hovered_link(void) { return s_editor->m_hovered_link; }
ne_node_id ne_get_double_clicked_node(void)
{ return s_editor->m_double_clicked_node; }
ne_pin_id ne_get_double_clicked_pin(void)
{ return s_editor->m_double_clicked_pin; }
ne_link_id ne_get_double_clicked_link(void)
{ return s_editor->m_double_clicked_link; }
bool ne_is_background_clicked(void)
{ return s_editor->m_background_click_button_index >= 0; }
bool ne_is_background_double_clicked(void)
{ return s_editor->m_background_double_click_button_index >= 0; }
int ne_get_background_click_button_index(void)
{ return s_editor->m_background_click_button_index; }
int ne_get_background_double_click_button_index(void)
{ return s_editor->m_background_double_click_button_index; }

bool ne_get_link_pins(ne_link_id link_id, ne_pin_id *start_pin_id, ne_pin_id *end_pin_id)
{
    struct ne_link *link = ne_editor_context_find_link(s_editor, link_id);
    if (!link) return false;

    if (start_pin_id) *start_pin_id = link->m_start_pin->m_id;
    if (end_pin_id) *end_pin_id = link->m_end_pin->m_id;

    return true;
}

bool ne_pin_had_any_links(ne_pin_id pin_id)
{ return ne_editor_context_pin_had_any_links(s_editor, pin_id); }

ImVec2 ne_get_screen_size(void)
{
    return ne_vec2_sub(
        ne_canvas_rect(&s_editor->m_canvas).Max,
        ne_canvas_rect(&s_editor->m_canvas).Min
    );
}

ImVec2 ne_screen_to_canvas(ImVec2 pos)
{ return ne_editor_context_to_canvas(s_editor, pos); }

ImVec2 ne_canvas_to_screen(ImVec2 pos)
{ return ne_editor_context_to_screen(s_editor, pos); }

int ne_get_node_count(void)
{ return ne_editor_context_count_live_nodes(s_editor); }

int ne_get_ordered_node_ids(ne_node_id *nodes, int size)
{ return ne_editor_context_get_node_ids(s_editor, nodes, size); }
