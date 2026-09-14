#include "render/widgets.h"

#include "runtime/generated.h"
#include "support/array.h"

#include <imgui/dcimgui.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    ecs_entity entity;
    int order;
} widget_row;
typedef struct
{
    ecs_world *world;
    ecs_entity entity;
} widget_drag;

static int compare_widgets(const void *left, const void *right)
{
    const widget_row *a = left, *b = right;
    if (a->order != b->order) {
        return (a->order > b->order) - (a->order < b->order);
    }
    uint64_t x = ecs_entity_to_u64(a->entity), y = ecs_entity_to_u64(b->entity);
    return (x > y) - (x < y);
}

static ImU32 color(unsigned rgba)
{
    return IM_COL32(rgba >> 24, (rgba >> 16) & 255, (rgba >> 8) & 255, rgba & 255);
}

static void label(const GameWidget *widget, game_widget_rect rect, float scale, bool center)
{
    const char *text = widget->text ? widget->text : "";
    float font = (widget->font_size > 0 ? widget->font_size : 18) * scale;
    ImVec2 position = { rect.min.x + 10 * scale, rect.min.y + 6 * scale };
    if (center) {
        ImVec2 measured = ImGui_CalcTextSize(text);
        float ratio = font / ImGui_GetFontSize();
        position = (ImVec2){
            rect.min.x + (rect.max.x - rect.min.x - measured.x * ratio) / 2,
            rect.min.y + (rect.max.y - rect.min.y - measured.y * ratio) / 2
        };
    }
    ImDrawList *draw = ImGui_GetWindowDrawList();
    ImDrawList_PushClipRect(
        draw,
        (ImVec2){ rect.min.x, rect.min.y },
        (ImVec2){ rect.max.x, rect.max.y },
        true
    );
    ImDrawList_AddTextImFontPtr(
        draw,
        ImGui_GetFont(),
        font,
        position,
        color(widget->color ? widget->color : 0xEAF1F8FF),
        text
    );
    ImDrawList_PopClipRect(draw);
}

static void drag_drop(ecs_world *world, game_entities *entities, ecs_entity target, const GameWidget *widget)
{
    if (widget->draggable && widget->drop_group && widget->drop_group[0] &&
        ImGui_BeginDragDropSource(ImGuiDragDropFlags_None)) {
        widget_drag payload = { world, target };
        ImGui_SetDragDropPayload("game.widget", &payload, sizeof payload, ImGuiCond_Once);
        ImGui_TextUnformatted(widget->text ? widget->text : "");
        ImGui_EndDragDropSource();
    }
    if (widget->drop_group && widget->drop_group[0] && ImGui_BeginDragDropTarget()) {
        const ImGuiPayload *payload = ImGui_GetDragDropPayload();
        if (payload && payload->DataSize == sizeof(widget_drag) &&
            ImGuiPayload_IsDataType(payload, "game.widget")) {
            const widget_drag *drag = payload->Data;
            const GameWidget *source = drag->world == world
                                         ? GAME_GameWidget_get_const(world, drag->entity)
                                         : NULL;
            if (source && source->drop_group &&
                !strcmp(source->drop_group, widget->drop_group)) {
                payload = ImGui_AcceptDragDropPayload("game.widget", ImGuiDragDropFlags_None);
            } else {
                payload = NULL;
            }
        } else {
            payload = NULL;
        }
        if (payload && payload->DataSize == sizeof(widget_drag)) {
            const widget_drag *drag = payload->Data;
            if (drag->world == world) {
                game_widget_interact(world, entities, target, GAME_WIDGET_DROP, drag->entity, 0);
            }
        }
        ImGui_EndDragDropTarget();
    }
}

static ImVec2 map_point(const GameFog *fog, game_widget_rect rect, CF_V2 position)
{
    return (ImVec2){ rect.min.x + (position.x - fog->origin.x) /
                                      (fog->columns * fog->cell_size) *
                                      (rect.max.x - rect.min.x),
                     rect.min.y + (position.y - fog->origin.y) /
                                      (fog->rows * fog->cell_size) *
                                      (rect.max.y - rect.min.y) };
}

static void draw_map(
    ecs_world *world,
    const game_visibility *visibility,
    game_widget_rect rect,
    const GameWidget *widget
)
{
    GameFog fog;
    const unsigned char *cells = visibility ? game_visibility_cells(visibility, &fog)
                                            : NULL;
    if (!cells) {
        return;
    }
    ImDrawList *draw = ImGui_GetWindowDrawList();
    ImDrawList_PushClipRect(
        draw,
        (ImVec2){ rect.min.x, rect.min.y },
        (ImVec2){ rect.max.x, rect.max.y },
        true
    );
    unsigned background = widget->background ? widget->background : 0x080F17FF;
    unsigned terrain = widget->color ? widget->color : 0x74838CFF;
    unsigned visible = widget->accent ? widget->accent : 0x355965FF;
    unsigned explored = (((background & 0xFEFEFE00u) >> 1) +
                         ((visible & 0xFEFEFE00u) >> 1)) |
                        255u;
    float width = (rect.max.x - rect.min.x) / fog.columns,
          height = (rect.max.y - rect.min.y) / fog.rows;
    for (int y = 0; y < fog.rows; y++) {
        for (int x = 0; x < fog.columns; x++) {
            int index = (y * fog.columns + x) * 4;
            unsigned rgba = cells[index + 1] ? (cells[index + 2] ? terrain : explored)
                                             : background;
            if (cells[index] && !cells[index + 2]) {
                rgba = visible;
            }
            ImVec2 a = { rect.min.x + x * width, rect.min.y + y * height };
            ImDrawList_AddRectFilled(
                draw,
                a,
                (ImVec2){ a.x + width + 0.5f, a.y + height + 0.5f },
                color(rgba)
            );
        }
    }
    ecs_component_id component = GAME_COMPONENT_GameSensorContact.id;
    ecs_query query;
    if (ecs_world_component(world, component) &&
        ecs_query_begin(world, &query, &component, 1) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            const GameSensorContact *contact = GAME_GameSensorContact_get_const(world, entity);
            if (contact->remaining > 0 && isfinite(contact->position.x) &&
                isfinite(contact->position.y)) {
                ImVec2 point = map_point(&fog, rect, contact->position);
                ImDrawList_AddCircle(
                    draw,
                    point,
                    4,
                    color(contact->color ? contact->color : 0xFFC66DFF)
                );
            }
        }
        ecs_query_end(&query);
    }
    component = GAME_COMPONENT_GameMapMarker.id;
    if (ecs_world_component(world, component) &&
        ecs_query_begin(world, &query, &component, 1) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            const GameMapMarker *marker = GAME_GameMapMarker_get_const(world, entity);
            if (marker->disabled || ecs_entity_is_retiring(world, entity) ||
                !isfinite(marker->position.x) || !isfinite(marker->position.y) ||
                (!marker->always_visible &&
                 !game_visibility_at(visibility, marker->position).explored)) {
                continue;
            }
            ImVec2 point = map_point(&fog, rect, marker->position);
            ImU32 ink = color(marker->color ? marker->color : terrain);
            float radius = isfinite(marker->radius) && marker->radius > 0
                             ? marker->radius
                             : 3;
            ImDrawList_AddCircleFilled(draw, point, radius, ink, 12);
            if (marker->label && marker->label[0]) {
                ImDrawList_AddText(
                    draw,
                    (ImVec2){ point.x + radius + 3, point.y - ImGui_GetFontSize() / 2 },
                    ink,
                    marker->label
                );
            }
        }
        ecs_query_end(&query);
    }
    ecs_component_id required[] = { GAME_COMPONENT_GameVision.id,
                                    GAME_COMPONENT_GameTransform.id };
    if (ecs_query_begin(world, &query, required, 2) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            const GameVision *vision = GAME_GameVision_get_const(world, entity);
            if (!vision->disabled && vision->remember &&
                (vision->duration <= 0 || vision->age < vision->duration)) {
                ImDrawList_AddCircleFilled(
                    draw,
                    map_point(&fog, rect, *GAME_GameTransform_get_const(world, entity)),
                    3,
                    color(widget->accent ? widget->accent : 0xB5F6FFFF),
                    12
                );
            }
        }
        ecs_query_end(&query);
    }
    ImDrawList_PopClipRect(draw);
}

static bool draw_widget(
    ecs_world *world,
    game_entities *entities,
    const game_visibility *visibility,
    ecs_entity entity,
    game_widget_rect rect,
    float scale,
    bool enabled
)
{
    const GameWidget *widget = GAME_GameWidget_get_const(world, entity);
    ImVec2 size = { rect.max.x - rect.min.x, rect.max.y - rect.min.y };
    unsigned bg = widget->background ? widget->background : 0x172431EF;
    unsigned accent = widget->accent ? widget->accent : 0x5AB9CFFF;
    float radius = fmaxf(0, widget->rounding) * scale;
    ImGui_SetCursorScreenPos((ImVec2){ rect.min.x, rect.min.y });
    ImGui_PushIDInt((int)entity.slot);
    ImGui_PushIDInt((int)entity.generation);
    ImGui_BeginDisabled(!enabled || widget->disabled);
    ImGui_PushStyleVar(ImGuiStyleVar_FrameRounding, radius);
    ImGui_PushStyleColor(ImGuiCol_Button, color(bg));
    ImGui_PushStyleColor(ImGuiCol_ButtonHovered, color(accent));
    ImGui_PushStyleColor(ImGuiCol_ButtonActive, color(accent));
    ImGui_PushStyleColor(ImGuiCol_FrameBg, color(bg));
    ImGui_PushStyleColor(ImGuiCol_SliderGrab, color(accent));
    bool control = false, center = false;
    switch (widget->kind) {
        case GAME_WIDGET_BUTTON:
        case GAME_WIDGET_SLOT:
            if (ImGui_ButtonEx("##activate", size)) {
                game_widget_interact(world, entities, entity, GAME_WIDGET_ACTIVATE, (ecs_entity){ 0 }, 0);
            }
            control = center = true;
            break;
        case GAME_WIDGET_SLIDER: {
            float value = widget->value;
            ImGui_SetNextItemWidth(size.x);
            if (isfinite(widget->minimum) && isfinite(widget->maximum) &&
                widget->maximum > widget->minimum &&
                ImGui_SliderFloatEx("##value", &value, widget->minimum, widget->maximum, "", ImGuiSliderFlags_AlwaysClamp)) {
                game_widget_interact(world, entities, entity, GAME_WIDGET_CHANGE, entity, value);
            }
            control = center = true;
            break;
        }
        case GAME_WIDGET_PANEL:
        case GAME_WIDGET_PROGRESS: {
            ImDrawList *draw = ImGui_GetWindowDrawList();
            ImVec2 a = { rect.min.x, rect.min.y }, b = { rect.max.x, rect.max.y };
            ImDrawList_AddRectFilledEx(draw, a, b, color(bg), radius, 0);
            if (widget->kind == GAME_WIDGET_PROGRESS &&
                widget->maximum > widget->minimum) {
                float fraction = (widget->value - widget->minimum) /
                                 (widget->maximum - widget->minimum);
                b.x = a.x + size.x * fminf(1, fmaxf(0, fraction));
                ImDrawList_AddRectFilledEx(draw, a, b, color(accent), radius, 0);
                center = true;
            }
            break;
        }
        case GAME_WIDGET_MAP: draw_map(world, visibility, rect, widget); break;
        default: break;
    }
    if (!control) {
        ImGui_Dummy(size);
    }
    bool focused = control && ImGui_GetIO()->NavVisible && ImGui_IsItemFocused();
    if (control && enabled && !widget->disabled) {
        drag_drop(world, entities, entity, widget);
    }
    if (control && widget->tooltip && widget->tooltip[0] &&
        ImGui_IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui_SetTooltip("%s", widget->tooltip);
    }
    label(widget, rect, scale, center);
    ImGui_PopStyleColorEx(5);
    ImGui_PopStyleVar();
    ImGui_EndDisabled();
    ImGui_PopID();
    ImGui_PopID();
    return focused || (control && ImGui_IsItemActive());
}

bool render_widgets(
    ecs_world *world,
    game_entities *entities,
    const game_visibility *visibility,
    CF_V2 origin,
    CF_V2 size,
    bool enabled
)
{
    ecs_component_id component = GAME_COMPONENT_GameWidget.id;
    if (!ecs_world_component(world, component) || size.x <= 0 || size.y <= 0) {
        return false;
    }
    ecs_query query;
    if (ecs_query_begin(world, &query, &component, 1) != ECS_OK) {
        return false;
    }
    widget_row *rows = NULL;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        if (!ecs_entity_is_retiring(world, entity)) {
            apush(
                rows,
                ((widget_row){ entity, GAME_GameWidget_get_const(world, entity)->order })
            );
        }
    }
    ecs_query_end(&query);
    if (acount(rows) > 1) {
        qsort(rows, (size_t)acount(rows), sizeof *rows, compare_widgets);
    }
    ImVec2 cursor = ImGui_GetCursorScreenPos();
    ImGui_PushClipRect(
        (ImVec2){ origin.x, origin.y },
        (ImVec2){ origin.x + size.x, origin.y + size.y },
        true
    );
    bool focused = false;
    for (int i = 0; i < acount(rows); i++) {
        const GameWidget *widget = GAME_GameWidget_get_const(world, rows[i].entity);
        game_widget_rect rect;
        if (game_widget_bounds(widget, origin, size, &rect)) {
            focused |= draw_widget(world, entities, visibility, rows[i].entity, rect, size.y / 720, enabled);
        }
    }
    ImGui_PopClipRect();
    ImGui_SetCursorScreenPos(cursor);
    ImGui_Dummy((ImVec2){ 0, 0 });
    afree(rows);
    return focused;
}
