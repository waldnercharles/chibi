#include "editor/workspace/input.h"

#include "editor/ui/ui_shell.h"
#include "editor/workspace/editor_internal.h"
#include "support/math.h"

#include <imgui/dcimgui.h>

#undef IMGUI_STB_NAMESPACE
#include <imgui/dcimgui_internal.h>
#include <stdint.h>

void ed_center_initial_view(editor *e)
{
    if (e->canvas_size.x <= 0.0f || e->canvas_size.y <= 0.0f) {
        return;
    }
    if (e->reveal_target) {
        e->fit_mode = false;
        e->fit_resize_pending = false;
        node *n = e->reveal_target;
        float zoom = ed_max(e->zoom, ED_READABLE_ZOOM);
        ImVec2 size = ed_node_dimensions(e, e->active_graph, n);
        ImVec2 center = ed_add((ImVec2){ n->x, n->y }, ed_scale(size, 0.5f));
        ImVec2 pan = ed_sub(ed_scale(e->canvas_size, 0.5f), ed_scale(center, zoom));
        ed_camera_target(e, pan, zoom, e->view_initialized);
        e->reveal_target = NULL;
        e->view_initialized = 1;
        return;
    }
    if (!e->view_initialized) {
        ed_frame_initial_view(e);
    }
}

static int ed_control_down(const ImGuiIO *io)
{
    return io->KeyCtrl || ImGui_IsKeyDown(ImGuiKey_LeftCtrl) ||
           ImGui_IsKeyDown(ImGuiKey_RightCtrl);
}

int ed_selection_modifier(const ImGuiIO *io)
{
    return ed_control_down(io) || io->KeySuper ||
           ImGui_IsKeyDown(ImGuiKey_LeftSuper) ||
           ImGui_IsKeyDown(ImGuiKey_RightSuper);
}

void ed_handle_input(editor *e)
{
    ImGuiIO *io = ImGui_GetIO();
    ImVec2 mouse = io->MousePos;
    int hovered = ImGui_IsWindowHovered(0) &&
                  ed_point_in_rect(mouse, e->canvas_min, ed_add(e->canvas_min, e->canvas_size));
    /* Canvas gestures use the backend state directly. ImGui_IsMouseDown()
     * applies key ownership, which can belong to a value widget while the
     * canvas gesture is already in progress. */
    int control_down = ed_control_down(io);
    int control_secondary = control_down && io->MouseDown[1];
    int mouse_down = io->MouseDown[0] || control_secondary;
    int mouse_clicked = io->MouseClicked[0] || (control_down && io->MouseClicked[1]);
    int mouse_released = io->MouseReleased[0] || io->MouseReleased[1];

    /* Continue an active gesture before checking current-frame ImGui controls.
     * A marquee may pass over a value editor without becoming that editor's
     * interaction, and must keep receiving mouse movement. */
    int pan_held = io->MouseDown[2] || (ImGui_IsKeyDown(ImGuiKey_Space) && mouse_down);
    if (e->panning) {
        e->fit_mode = false;
        e->fit_resize_pending = false;
        e->camera_moving = false;
        if (pan_held) {
            e->pan = ed_add(e->pan, io->MouseDelta);
        } else {
            e->panning = 0;
        }
        return;
    }
    if (e->linking) {
        e->link_mouse = mouse;
        if (ImGui_IsKeyPressed(ImGuiKey_Escape)) {
            ed_cancel_snapshot(e);
            ed_clear_connect_error(e);
            e->linking = 0;
            return;
        }
        if (mouse_released) {
            ed_pin_view *target = ed_hit_pin(e, mouse);
            if (target && target->n != e->link_pin.n &&
                ed_connect_with_inline(e, &e->link_pin, target, editor_ui_inline_edge_nodes())) {
                ed_clear_connect_error(e);
                ed_commit_snapshot(e);
                e->linking = 0;
            } else if (!target) {
                ImVec2 world = ed_screen_to_world(e, mouse);
                e->popup_x = world.x;
                e->popup_y = world.y;
                e->spawn_link_pending = 1;
                e->spawn_link_pin = e->link_pin;
                e->linking = 0;
                ImGui_OpenPopup("spawn_menu", 0);
            } else {
                if (e->connect_error[0]) {
                    e->connect_error_pos = mouse;
                }
                ed_cancel_snapshot(e);
                e->linking = 0;
            }
        }
        return;
    }
    if (e->connect_error[0] && ImGui_IsKeyPressed(ImGuiKey_Escape)) {
        ed_clear_connect_error(e);
        return;
    }
    if (e->connect_error[0] && mouse_clicked) {
        ImVec2 min, max, close_min, close_max;
        if (ed_connect_error_rect(e, &min, &max, &close_min, &close_max) &&
            ed_point_in_rect(mouse, min, max)) {
            if (ed_point_in_rect(mouse, close_min, close_max)) {
                ed_clear_connect_error(e);
            }
            return;
        }
    }
    if (e->dragging) {
        if (!mouse_released) {
            ed_update_drag(e, mouse);
        } else {
            ImVec2 delta = ed_sub(mouse, e->drag_start_mouse);
            if (delta.x != 0.0f || delta.y != 0.0f) {
                ed_commit_snapshot(e);
            } else {
                ed_cancel_snapshot(e);
            }
            if (!delta.x && !delta.y && e->drag_toggle_selected) {
                if (acount(e->selected) > 1) {
                    ed_remove_selected(e, e->drag_toggle_node);
                } else {
                    ed_clear_selection(e);
                }
            }
            e->dragging = 0;
            e->drag_toggle_selected = 0;
            e->drag_toggle_node = NULL;
            aclear(e->drag_nodes);
        }
        return;
    }
    if (e->dragging_handle) {
        if (!mouse_released) {
            ed_update_handle_drag(e, mouse);
        } else {
            ed_commit_snapshot(e);
            e->dragging_handle = 0;
        }
        return;
    }
    if (e->selecting) {
        if (!mouse_released) {
            e->select_end = mouse;
            ed_select_marquee(e);
        } else {
            ed_select_marquee(e);
            e->selecting = 0;
        }
        return;
    }

    /* The canvas dummy is an ImGui item, so do not use global item-hover state
     * to gate gestures. Only real controls and active widgets block a new
     * canvas gesture. */
    if (ImGui_IsKeyPressed(ImGuiKey_Escape) && ImGui_IsAnyItemActive()) {
        ImGui_ClearActiveID();
        return;
    }
    if (hovered && mouse_clicked && !e->controls_hovered && ImGui_IsAnyItemActive()) {
        ImGui_ClearActiveID();
    }
    if (ImGui_IsAnyItemActive() || e->controls_hovered) {
        return;
    }

    if (hovered && io->MouseWheel != 0.0f) {
        e->fit_mode = false;
        e->fit_resize_pending = false;
        ImVec2 before = ed_screen_to_world(e, mouse);
        float zoom = e->camera_moving ? e->camera_zoom : e->zoom;
        zoom *= math_pow(1.1f, io->MouseWheel);
        zoom = ed_max(ED_MIN_ZOOM, ed_min(ED_MAX_ZOOM, zoom));
        ImVec2 pan = ed_sub(ed_sub(mouse, e->canvas_min), ed_scale(before, zoom));
        ed_camera_target(e, pan, zoom, true);
    }
    if (hovered && pan_held) {
        e->fit_mode = false;
        e->fit_resize_pending = false;
        e->camera_moving = false;
        e->panning = 1;
        e->pan = ed_add(e->pan, io->MouseDelta);
        return;
    }
    if (e->active_graph->math_expression) {
        if (hovered && mouse_clicked) {
            ed_node_view *view = ed_hit_node(e, mouse);
            if (view) {
                ed_select_node(e, view->n, ed_selection_modifier(io));
            } else {
                ed_clear_selection(e);
            }
        }
        return;
    }
    if (hovered && ImGui_IsMouseClicked(1) && !control_down) {
        ed_open_context_menu(e, mouse);
        return;
    }
    if (!hovered || !mouse_clicked) {
        return;
    }

    e->camera_moving = false;
    int handle_edge = -1, handle_side = -1;
    if (ed_hit_handle(e, mouse, &handle_edge, &handle_side)) {
        edge *ed = &e->active_graph->edges[handle_edge];
        if (io->KeyAlt) {
            ed_snapshot_before(e);
            for (int i = handle_side; i + 1 < ed->reroute_count; i++) {
                ed->reroutes[i] = ed->reroutes[i + 1];
            }
            ed->reroute_count--;
            return;
        }
        e->pending_snapshot = ed_snapshot(e);
        e->handle_edge = handle_edge;
        e->handle_side = handle_side;
        e->dragging_handle = 1;
        ed_update_handle_drag(e, mouse);
        return;
    }

    /* Double-clicking (or Alt-clicking) a wire inserts a reroute point into
     * the segment that was clicked. Alt-clicking an existing point removes it. */
    if (io->KeyAlt || io->MouseDoubleClicked[0]) {
        ed_link_ref link;
        int segment;
        if (ed_hit_link_segment(e, mouse, &link, &segment)) {
            int index = ed_edge_index(e, &link);
            edge *ed = &e->active_graph->edges[index];
            if (ed->reroute_count < GRAPH_EDGE_MAX_REROUTES) {
                e->pending_snapshot = ed_snapshot(e);
                for (int i = ed->reroute_count; i > segment; i--) {
                    ed->reroutes[i] = ed->reroutes[i - 1];
                }
                ImVec2 point = ed_screen_to_world(e, mouse);
                ed->reroutes[segment].x = point.x;
                ed->reroutes[segment].y = point.y;
                ed->reroute_count++;
                e->handle_edge = index;
                e->handle_side = segment;
                e->dragging_handle = 1;
                return;
            }
        }
    }

    ed_pin_view *pin = ed_hit_pin(e, mouse);
    if (pin) {
        ed_select_node(e, pin->n, ed_selection_modifier(io));
        e->linking = 1;
        ed_clear_connect_error(e);
        e->link_pin = *pin;
        e->link_mouse = mouse;
        e->pending_snapshot = ed_snapshot(e);
        return;
    }
    ed_node_view *v = ed_hit_node(e, mouse);
    if (v) {
        if (io->MouseDoubleClicked[0] && ed_is_graph_call(v->n)) {
            e->open_call_node = v->n->id;
            return;
        }
        int append = ed_selection_modifier(io);
        int already_selected = ed_node_selected(e, v->n);
        e->front_node = v->n;
        e->drag_toggle_selected = append && already_selected;
        e->drag_toggle_node = e->drag_toggle_selected ? v->n : NULL;
        if (!already_selected) {
            ed_select_node(e, v->n, append);
        }
        if (ed_node_selected(e, v->n)) {
            ed_begin_drag(e, mouse);
        }
        return;
    }
    ed_link_ref link;
    if (ed_hit_link(e, mouse, &link)) {
        int append = ed_selection_modifier(io);
        if (!append) {
            aclear(e->selected);
            aclear(e->selected_links);
        }
        if (append && ed_link_selected(e, &link)) {
            ed_remove_selected_link(e, &link);
        } else {
            ed_add_selected_link(e, &link);
        }
        return;
    }
    if (!ed_selection_modifier(io)) {
        ed_clear_selection(e);
    }
    e->selecting = 1;
    e->select_additive = ed_selection_modifier(io);
    e->select_start = mouse;
    e->select_end = mouse;
}

void ed_handle_shortcuts(editor *e)
{
    if (e->active_graph->math_expression) {
        return;
    }
    if (!ImGui_IsWindowFocused(0)) {
        return;
    }
    if (editor_ui_captures_keyboard() || ImGui_GetIO()->WantTextInput) {
        return;
    }
    bool modifier = app_input_down(APP_INPUT_COMMAND) ||
                    app_input_down(APP_INPUT_CONTROL);
    if (modifier && app_input_pressed(APP_INPUT_UNDO)) {
        if (app_input_down(APP_INPUT_SHIFT)) {
            ed_redo(e);
        } else {
            ed_undo(e);
        }
    } else if (modifier && app_input_pressed(APP_INPUT_REDO)) {
        ed_redo(e);
    } else if (modifier && app_input_pressed(APP_INPUT_COPY)) {
        ed_copy_selected(e);
    } else if (modifier && app_input_pressed(APP_INPUT_CUT)) {
        ed_cut_selected(e);
    } else if (modifier && app_input_pressed(APP_INPUT_PASTE)) {
        ed_paste(e);
    } else if (modifier && app_input_pressed(APP_INPUT_DUPLICATE)) {
        ed_duplicate_selected(e);
    } else if (modifier && app_input_pressed(APP_INPUT_SAVE)) {
        if (e->save_handler) {
            e->save_handler(e, e->save_user);
        } else {
            editor_save(e, "behavior_asset.json");
        }
    } else if (app_input_pressed(APP_INPUT_DELETE)) {
        ed_delete_selected(e);
    }
}
