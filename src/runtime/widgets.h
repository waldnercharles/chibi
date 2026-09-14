#ifndef CHIBI_GAME_WIDGETS_H
#define CHIBI_GAME_WIDGETS_H

#include "runtime/behaviors/entities.h"
#include "runtime/components.h"

typedef struct
{
    CF_V2 min, max;
} game_widget_rect;
typedef enum
{
    GAME_WIDGET_ACTIVATE,
    GAME_WIDGET_CHANGE,
    GAME_WIDGET_DROP
} game_widget_action;

bool game_widget_bounds(const GameWidget *widget, CF_V2 origin, CF_V2 size, game_widget_rect *out);
bool game_widgets_capture(ecs_world *world, CF_V2 origin, CF_V2 size, CF_V2 pointer);
/* Writes the value and queues a copied event payload after validating the
 * same interaction rules used by the renderer and headless scenarios. */
int game_widget_interact(
    ecs_world *world,
    game_entities *entities,
    ecs_entity target,
    game_widget_action action,
    ecs_entity source,
    float value
);

#endif
