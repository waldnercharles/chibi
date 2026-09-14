#include "runtime/widgets.h"

#include "runtime/generated.h"

#include <math.h>
#include <string.h>

bool game_widget_bounds(const GameWidget *widget, CF_V2 origin, CF_V2 size, game_widget_rect *out)
{
    float scale = size.y / 720;
    CF_V2 extent = { widget->size.x * scale, widget->size.y * scale };
    out->min = (CF_V2){ origin.x + widget->anchor.x * size.x +
                            widget->offset.x * scale - widget->pivot.x * extent.x,
                        origin.y + widget->anchor.y * size.y +
                            widget->offset.y * scale - widget->pivot.y * extent.y };
    out->max = (CF_V2){ out->min.x + extent.x, out->min.y + extent.y };
    return !widget->hidden && extent.x > 0 && extent.y > 0 && isfinite(out->min.x) &&
           isfinite(out->min.y) && isfinite(out->max.x) && isfinite(out->max.y);
}

static bool interactive(const GameWidget *widget)
{
    return widget->kind == GAME_WIDGET_BUTTON ||
           widget->kind == GAME_WIDGET_SLIDER || widget->kind == GAME_WIDGET_SLOT;
}

bool game_widgets_capture(ecs_world *world, CF_V2 origin, CF_V2 size, CF_V2 pointer)
{
    ecs_component_id component = GAME_COMPONENT_GameWidget.id;
    if (!ecs_world_component(world, component)) {
        return false;
    }
    ecs_query query;
    if (ecs_query_begin(world, &query, &component, 1) != ECS_OK) {
        return false;
    }
    bool captured = false;
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GameWidget *widget = GAME_GameWidget_get_const(world, entity);
        game_widget_rect bounds;
        if (!ecs_entity_is_retiring(world, entity) &&
            (interactive(widget) || widget->block_input) &&
            game_widget_bounds(widget, origin, size, &bounds) &&
            pointer.x >= bounds.min.x && pointer.x < bounds.max.x &&
            pointer.y >= bounds.min.y && pointer.y < bounds.max.y) {
            captured = true;
            break;
        }
    }
    ecs_query_end(&query);
    return captured;
}

int game_widget_interact(
    ecs_world *world,
    game_entities *entities,
    ecs_entity target,
    game_widget_action action,
    ecs_entity source,
    float value
)
{
    GameWidget *widget = GAME_GameWidget_get(world, target);
    if (!widget || widget->hidden || widget->disabled ||
        ecs_entity_is_retiring(world, target) || !interactive(widget) ||
        !widget->event || !widget->event[0]) {
        return FLOW_ERR_ARGUMENT;
    }
    if (action == GAME_WIDGET_DROP) {
        const GameWidget *from = GAME_GameWidget_get_const(world, source);
        if (!from || from->hidden || from->disabled || !from->draggable ||
            ecs_entity_is_retiring(world, source) || !from->drop_group ||
            !from->drop_group[0] || !widget->drop_group ||
            strcmp(from->drop_group, widget->drop_group) ||
            ecs_entity_to_u64(source) == ecs_entity_to_u64(target)) {
            return FLOW_ERR_ARGUMENT;
        }
        value = from->value;
    } else if (action == GAME_WIDGET_CHANGE) {
        if (widget->kind != GAME_WIDGET_SLIDER || !isfinite(value) ||
            !isfinite(widget->minimum) || !isfinite(widget->maximum) ||
            widget->maximum <= widget->minimum || value < widget->minimum ||
            value > widget->maximum) {
            return FLOW_ERR_ARGUMENT;
        }
        source = target;
    } else if (action == GAME_WIDGET_ACTIVATE && widget->kind != GAME_WIDGET_SLIDER) {
        source = target;
        value = widget->value;
    } else {
        return FLOW_ERR_ARGUMENT;
    }
    if (!isfinite(value)) {
        return FLOW_ERR_ARGUMENT;
    }
    pin_value payload[] = { { .type = PIN_HANDLE,
                              .type_name = "ecs.entity",
                              .u64 = ecs_entity_to_u64(source) },
                            { .type = PIN_F32, .type_name = "float", .f = value } };
    int result = game_entities_request_event(
        entities,
        target,
        widget->event,
        (flow_inputs){ payload, 2, NULL }
    );
    if (result >= 0 && action == GAME_WIDGET_CHANGE) {
        widget->value = value;
    }
    return result;
}
