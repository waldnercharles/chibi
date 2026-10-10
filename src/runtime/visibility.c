#include "runtime/visibility.h"

#include "runtime/component_catalog.h"
#include "runtime/scenes/physics.h"
#include "runtime/transform_propagation.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct game_visibility
{
    ecs_world *world;
    game_entities *entities;
    game_ray_scene *rays;
    GameFog settings;
    unsigned char *cells;
    bool enabled, initialized;
};

game_visibility *game_visibility_new(ecs_world *world, game_entities *entities)
{
    game_visibility *visibility = xcalloc(1, sizeof *visibility);
    visibility->rays = game_ray_scene_new();
    visibility->world = world;
    visibility->entities = entities;
    return visibility;
}

void game_visibility_reset(game_visibility *visibility)
{
    afree(visibility->cells);
    visibility->settings = (GameFog){ 0 };
    visibility->initialized = false;
    visibility->enabled = false;
}

void game_visibility_free(game_visibility *visibility)
{
    game_visibility_reset(visibility);
    game_ray_scene_free(visibility->rays);
    free(visibility);
}

static bool valid_settings(const GameFog *fog)
{
    return fog->columns > 0 && fog->columns <= 256 && fog->rows > 0 &&
           fog->rows <= 256 && isfinite(fog->drift) &&
           fabsf(fog->drift) <= 100 && isfinite(fog->cell_size) &&
           fog->cell_size >= 0.01f && fog->cell_size <= 10000 &&
           isfinite(fog->origin.x) && isfinite(fog->origin.y) &&
           fabsf(fog->origin.x) + fog->columns * fog->cell_size <= 1000000 &&
           fabsf(fog->origin.y) + fog->rows * fog->cell_size <= 1000000;
}

static const char *configure(game_visibility *visibility)
{
    visibility->enabled = false;
    ecs_component_id component = GAME_COMPONENT_GameFog.id;
    if (!ecs_world_component(visibility->world, component)) {
        return NULL;
    }
    ecs_query query;
    if (ecs_query_begin(visibility->world, &query, &component, 1) != ECS_OK) {
        return "Could not query fog settings";
    }
    ecs_entity entity;
    const char *error = NULL;
    while (ecs_query_next(&query, &entity)) {
        const GameFog *fog = GAME_GameFog_get_const(visibility->world, entity);
        if (fog->disabled || ecs_entity_is_retiring(visibility->world, entity)) {
            continue;
        }
        if (visibility->enabled || !valid_settings(fog)) {
            error = "GameFog requires one enabled grid, positive cell size and "
                    "1-256 rows and columns";
            break;
        }
        const GameFog *old = &visibility->settings;
        if (old->columns != fog->columns || old->rows != fog->rows ||
            old->cell_size != fog->cell_size ||
            old->origin.x != fog->origin.x || old->origin.y != fog->origin.y) {
            asetlen(visibility->cells, fog->columns * fog->rows * 4);
            memset(visibility->cells, 0, (size_t)acount(visibility->cells));
        }
        visibility->settings = *fog;
        visibility->enabled = true;
    }
    ecs_query_end(&query);
    return error;
}

typedef struct
{
    CF_V2 position;
    GameVision vision;
    ecs_entity entity;
} observer;

static const char *collect_observers(game_visibility *visibility, float seconds, observer **out)
{
    ecs_component_id required[] = { GAME_COMPONENT_GameVision.id };
    ecs_query query;
    if (ecs_query_begin(visibility->world, &query, required, 1) != ECS_OK) {
        return NULL;
    }
    ecs_entity entity;
    const char *error = NULL;
    while (ecs_query_next(&query, &entity)) {
        GameVision *vision = GAME_GameVision_get(visibility->world, entity);
        if (vision->disabled || ecs_entity_is_retiring(visibility->world, entity)) {
            continue;
        }
        if (!GAME_GameTransform_has(visibility->world, entity)) {
            error = "GameVision requires a GameTransform";
            break;
        }
        CF_V2 position = game_transform_world_2d(visibility->world, entity).position;
        if (!isfinite(position.x) || !isfinite(position.y) ||
            !isfinite(vision->radius) || !isfinite(vision->age) ||
            !isfinite(vision->duration) || vision->radius < 0 ||
            vision->radius > 1000000 || fabsf(position.x) > 1000000 ||
            fabsf(position.y) > 1000000 || vision->age < 0) {
            error = "GameVision requires finite position, radius, age and "
                    "duration";
            break;
        }
        GameVision sample = *vision;
        if (vision->duration > 0) {
            vision->age = fminf(vision->duration, vision->age + seconds);
            sample.radius *= sqrtf(fmaxf(0, 1 - vision->age / vision->duration));
        }
        if (sample.radius > 0) {
            apush(*out, ((observer){ position, sample, entity }));
        }
    }
    ecs_query_end(&query);
    return error;
}

static void reveal(game_visibility *visibility, const observer *observer)
{
    const GameFog *fog = &visibility->settings;
    CF_V2 position = observer->position;
    float radius = observer->vision.radius;
    int left = (int)fmaxf(0, floorf((position.x - radius - fog->origin.x) / fog->cell_size));
    int right = (int)
        fminf(fog->columns - 1, floorf((position.x + radius - fog->origin.x) / fog->cell_size));
    int top = (int)fmaxf(0, floorf((position.y - radius - fog->origin.y) / fog->cell_size));
    int bottom = (int)
        fminf(fog->rows - 1, floorf((position.y + radius - fog->origin.y) / fog->cell_size));
    for (int y = top; y <= bottom; y++) {
        for (int x = left; x <= right; x++) {
            CF_V2 point = { fog->origin.x + (x + 0.5f) * fog->cell_size,
                            fog->origin.y + (y + 0.5f) * fog->cell_size };
            float distance = hypotf(point.x - position.x, point.y - position.y);
            if (distance > radius) {
                continue;
            }
            game_ray_hit hit;
            game_ray_scene_cast(
                visibility->rays,
                position,
                point,
                observer->vision.occluders,
                observer->entity,
                false,
                &hit
            );
            /* The first obstacle's surface is visible. A cell beyond it is not. */
            if (hit.found && hit.distance + fog->cell_size * 0.5f < distance) {
                continue;
            }
            int index = (y * fog->columns + x) * 4;
            visibility->cells[index] = 255;
            if (observer->vision.remember) {
                visibility->cells[index + 1] = 255;
                visibility->cells[index + 2] = hit.found ? 255 : 0;
            }
        }
    }
}

static void age_contacts(game_visibility *visibility, float seconds)
{
    ecs_component_id component = GAME_COMPONENT_GameSensorContact.id;
    ecs_query query;
    if (ecs_world_component(visibility->world, component) &&
        ecs_query_begin(visibility->world, &query, &component, 1) == ECS_OK) {
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            GameSensorContact *contact = GAME_GameSensorContact_get(visibility->world, entity);
            contact->remaining = fmaxf(0, contact->remaining - seconds);
        }
        ecs_query_end(&query);
    }
}

const char *game_visibility_update(game_visibility *visibility, float seconds)
{
    visibility->initialized = true;
    age_contacts(visibility, seconds);
    const char *error = configure(visibility);
    if (error || !visibility->enabled) {
        return error;
    }
    if (game_ray_scene_refresh(visibility->rays, visibility->world, visibility->entities) <
        0) {
        return "Invalid collider while computing visibility";
    }
    observer *observers = NULL;
    error = collect_observers(visibility, seconds, &observers);
    for (int i = 0; i < acount(visibility->cells); i += 4) {
        visibility->cells[i] = 0;
        visibility->cells[i + 3] = 255;
    }
    for (int i = 0; !error && i < acount(observers); i++) {
        reveal(visibility, &observers[i]);
    }
    afree(observers);
    return error;
}

game_visibility_sample game_visibility_at(const game_visibility *visibility, CF_V2 point)
{
    if (!visibility->enabled) {
        return (game_visibility_sample){ .visible = true };
    }
    const GameFog *fog = &visibility->settings;
    float x = floorf((point.x - fog->origin.x) / fog->cell_size);
    float y = floorf((point.y - fog->origin.y) / fog->cell_size);
    if (!isfinite(x) || !isfinite(y) || x < 0 || x >= fog->columns || y < 0 ||
        y >= fog->rows) {
        return (game_visibility_sample){ 0 };
    }
    int index = ((int)y * fog->columns + (int)x) * 4;
    return (game_visibility_sample){ visibility->cells[index] != 0,
                                     visibility->cells[index + 1] != 0,
                                     visibility->cells[index + 2] != 0 };
}

const unsigned char *game_visibility_cells(const game_visibility *visibility, GameFog *settings)
{
    *settings = visibility->settings;
    return visibility->enabled ? visibility->cells : NULL;
}

const char *game_visibility_sample_values(void *service, const pin_value *in, pin_value *out)
{
    game_visibility *visibility = service;
    if (!visibility || !in[0].ptr) {
        return "Visibility At requires a game world and position";
    }
    if (!visibility->initialized) {
        const char *error = game_visibility_update(visibility, 0);
        if (error) {
            return error;
        }
    }
    game_visibility_sample
        sample = game_visibility_at(visibility, *(const CF_V2 *)in[0].ptr);
    out[0].b = sample.visible;
    out[1].b = sample.explored;
    out[2].b = sample.obstacle;
    return NULL;
}
const node_host_call GAME_VISIBILITY_SAMPLE_CALL = {
    .service = "game.visibility",
    .invoke = game_visibility_sample_values
};
const node_access GAME_VISIBILITY_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
