#include "components.h"

void game_component_probe(void) {}

GameTransform game_move_transform(GameTransform transform, float x, float y)
{
    transform.x += x;
    transform.y += y;
    return transform;
}

CF_V2 game_add_v2(CF_V2 a, CF_V2 b)
{
    return (CF_V2){ a.x + b.x, a.y + b.y };
}

CF_V2 game_transform_to_cf(GameTransform transform)
{
    return transform;
}

GameTransform game_transform_from_cf(CF_V2 value)
{
    return value;
}
