#include "runtime/medium.h"

#include "runtime/behaviors/entities.h"
#include "runtime/generated.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

struct game_medium
{
    ecs_world *world;
    ecs_entity entity;
    GameMedium settings;
    float *density, *next;
};

game_medium *game_medium_new(ecs_world *world)
{
    game_medium *medium = xcalloc(1, sizeof *medium);
    medium->world = world;
    return medium;
}

void game_medium_reset(game_medium *medium)
{
    free(medium->density);
    free(medium->next);
    medium->density = medium->next = NULL;
    medium->entity = (ecs_entity){ 0 };
    medium->settings = (GameMedium){ 0 };
}

void game_medium_free(game_medium *medium)
{
    game_medium_reset(medium);
    free(medium);
}

static bool valid(GameMedium s)
{
    return s.columns > 0 && s.columns <= 512 && s.rows > 0 && s.rows <= 512 &&
           isfinite(
               s.origin.x + s.origin.y + s.cell_size + s.density + s.variation +
               s.detail_size + s.velocity.x + s.velocity.y + s.extinction + s.depth
           ) &&
           s.cell_size >= .01f && s.cell_size <= 1024 &&
           fabsf(s.origin.x) <= 1000000 && fabsf(s.origin.y) <= 1000000 &&
           s.density >= 0 && s.density <= 100 && s.variation >= 0 &&
           s.variation <= 1 && s.detail_size >= s.cell_size &&
           s.detail_size <= 1000000 && fabsf(s.velocity.x) <= 10000 &&
           fabsf(s.velocity.y) <= 10000 && s.extinction >= 0 &&
           s.extinction <= 10 && s.depth >= 0 && s.depth <= 10000;
}

static bool same_fill(GameMedium a, GameMedium b)
{
    return a.columns == b.columns && a.rows == b.rows &&
           a.cell_size == b.cell_size && a.origin.x == b.origin.x &&
           a.origin.y == b.origin.y && a.density == b.density &&
           a.variation == b.variation && a.detail_size == b.detail_size;
}

static void fill(game_medium *medium, GameMedium s)
{
    int count = s.columns * s.rows;
    medium->density = xrealloc(medium->density, (size_t)count * sizeof(float));
    medium->next = xrealloc(medium->next, (size_t)count * sizeof(float));
    /* Integer periods keep the authored broad variation continuous at the
     * periodic boundary. The minimum remains density * (1 - variation). */
    float kx = fmaxf(1, roundf(s.columns * s.cell_size / s.detail_size));
    float ky = fmaxf(1, roundf(s.rows * s.cell_size / s.detail_size));
    for (int y = 0; y < s.rows; y++) {
        for (int x = 0; x < s.columns; x++) {
            float u = 6.28318530718f * (x + .5f) / s.columns;
            float v = 6.28318530718f * (y + .5f) / s.rows;
            float noise = .6f * sinf(kx * u + .7f * sinf(v)) +
                          .4f * cosf(ky * v + .8f * sinf(u));
            medium->density[y * s.columns + x] = s.density * (1 + s.variation * noise);
        }
    }
}

static const char *configure(game_medium *medium)
{
    ecs_component_id id = GAME_COMPONENT_GameMedium.id;
    if (!ecs_world_component(medium->world, id)) {
        game_medium_reset(medium);
        return NULL;
    }
    ecs_query query;
    int result = ecs_query_begin(medium->world, &query, &id, 1);
    assert(result == ECS_OK);
    ecs_entity entity, selected = { 0 };
    GameMedium settings = { 0 };
    const char *error = NULL;
    while (ecs_query_next(&query, &entity)) {
        if (ecs_entity_is_retiring(medium->world, entity)) {
            continue;
        }
        settings = *GAME_GameMedium_get_const(medium->world, entity);
        if (ecs_entity_to_u64(selected) || !valid(settings)) {
            error = "GameMedium requires one grid of 1..512 rows/columns, cell "
                    "size "
                    ".01..1024, density 0..100, variation 0..1, detail at "
                    "least one "
                    "cell, extinction 0..10 and depth 0..10000";
            break;
        }
        selected = entity;
    }
    ecs_query_end(&query);
    if (error) {
        return error;
    }
    if (!ecs_entity_to_u64(selected)) {
        game_medium_reset(medium);
        return NULL;
    }
    if (ecs_entity_to_u64(selected) != ecs_entity_to_u64(medium->entity) ||
        !same_fill(settings, medium->settings)) {
        fill(medium, settings);
    }
    medium->entity = selected;
    medium->settings = settings;
    return NULL;
}

const char *game_medium_update(game_medium *medium, float seconds)
{
    assert(isfinite(seconds) && seconds >= 0);
    const char *error = configure(medium);
    GameMedium s = medium->settings;
    if (error || !medium->density || seconds == 0 ||
        (s.velocity.x == 0 && s.velocity.y == 0)) {
        return error;
    }
    /* Uniform periodic advection is a convex translation of the previous
     * density, preserving its sum and bounds. This is prescribed wind, not
     * a pressure solver or obstacle-coupled fluid simulation. */
    float dx = fmodf(s.velocity.x * seconds / s.cell_size, (float)s.columns);
    float dy = fmodf(s.velocity.y * seconds / s.cell_size, (float)s.rows);
    int ix = (int)floorf(-dx), iy = (int)floorf(-dy);
    float fx = -dx - ix, fy = -dy - iy;
    for (int y = 0; y < s.rows; y++) {
        int y0 = (y + iy + s.rows) % s.rows, y1 = (y0 + 1) % s.rows;
        for (int x = 0; x < s.columns; x++) {
            int x0 = (x + ix + s.columns) % s.columns, x1 = (x0 + 1) % s.columns;
            float a = medium->density[y0 * s.columns + x0];
            float b = medium->density[y0 * s.columns + x1];
            float c = medium->density[y1 * s.columns + x0];
            float d = medium->density[y1 * s.columns + x1];
            medium->next[y * s.columns + x] = (1 - fy) * (a + fx * (b - a)) +
                                              fy * (c + fx * (d - c));
        }
    }
    float *swap = medium->density;
    medium->density = medium->next;
    medium->next = swap;
    return NULL;
}

const float *game_medium_cells(const game_medium *medium, GameMedium *settings)
{
    if (!medium || !medium->density) {
        return NULL;
    }
    *settings = medium->settings;
    return medium->density;
}

game_medium_state game_medium_inspect(const game_medium *medium)
{
    game_medium_state state = { 0 };
    if (!medium || !medium->density) {
        return state;
    }
    state.cells = medium->settings.columns * medium->settings.rows;
    state.minimum = INFINITY;
    state.hash = UINT64_C(14695981039346656037);
    double sum = 0;
    for (int i = 0; i < state.cells; i++) {
        float d = medium->density[i];
        sum += d;
        state.minimum = fminf(state.minimum, d);
        state.maximum = fmaxf(state.maximum, d);
        const unsigned char *bytes = (const unsigned char *)&medium->density[i];
        for (int j = 0; j < (int)sizeof(float); j++) {
            state.hash = (state.hash ^ bytes[j]) * UINT64_C(1099511628211);
        }
    }
    state.mass = (float)(sum * medium->settings.cell_size * medium->settings.cell_size);
    return state;
}
