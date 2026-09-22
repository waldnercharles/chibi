#include "render/scene_optics.h"

#include "render/optics.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

struct render_scene_optics
{
    render_optics *transport;
    render_optical_light *lights;
};
render_scene_optics *render_scene_optics_new()
{
    return xcalloc(1, sizeof(render_scene_optics));
}
void render_scene_optics_free(render_scene_optics *r)
{
    if (!r) {
        return;
    }
    render_optics_free(r->transport);
    afree(r->lights);
    free(r);
}

static const char *collect(render_scene_optics *r, ecs_world *world, render_optical_budget *budget)
{
    aclear(r->lights);
    ecs_component_id id = GAME_COMPONENT_GameOptics2D.id;
    ecs_query query;
    ecs_entity entity;
    if (ecs_world_component(world, id) &&
        ecs_query_begin(world, &query, &id, 1) == ECS_OK) {
        int found = 0;
        while (ecs_query_next(&query, &entity)) {
            if (ecs_entity_is_retiring(world, entity)) {
                continue;
            }
            const GameOptics2D *s = GAME_GameOptics2D_get_const(world, entity);
            *budget = (render_optical_budget){ s->max_size, s->rays, s->events };
            found++;
        }
        ecs_query_end(&query);
        if (found > 1) {
            return "GameOptics2D requires one settings entity";
        }
    }
    id = GAME_COMPONENT_GameLight2D.id;
    if (!ecs_world_component(world, id)) {
        return NULL;
    }
    int result = ecs_query_begin(world, &query, &id, 1);
    assert(result == ECS_OK);
    const char *error = NULL;
    while (ecs_query_next(&query, &entity)) {
        if (ecs_entity_is_retiring(world, entity)) {
            continue;
        }
        const GameLight2D *s = GAME_GameLight2D_get_const(world, entity);
        const GameTransform *p = GAME_GameTransform_get_const(world, entity);
        const GameOrientation *o = GAME_GameOrientation_get_const(world, entity);
        float angle = o ? o->radians : 0;
        if (!p || !isfinite(p->x) || !isfinite(p->y) || !isfinite(angle) ||
            !isfinite(s->power) || !isfinite(s->width) ||
            !isfinite(s->spread) || s->power < 0 || s->power > 1e6f ||
            s->width < 0 || s->spread < 0 || s->spread > 6.283186f) {
            error = "GameLight2D needs a finite transform, power 0..1000000, "
                    "nonnegative aperture and spread 0..2pi";
            break;
        }
        render_optical_light light = { .position = { p->x, p->y },
                                       .direction = { cosf(angle), sinf(angle) },
                                       .color = { ((s->color >> 24) & 255) / 255.f,
                                                  ((s->color >> 16) & 255) / 255.f,
                                                  ((s->color >> 8) & 255) / 255.f },
                                       .power = s->power,
                                       .width = s->width,
                                       .spread = s->spread };
        apush(r->lights, light);
    }
    ecs_query_end(&query);
    return error;
}

const char *render_scene_optics_draw(
    render_scene_optics *r,
    ecs_world *world,
    const render_material_entity *entities,
    int count,
    render_view_2d view,
    int width,
    int height,
    render_radiance_inputs *inputs
)
{
    render_optical_budget budget = { 480, 2048, 4 };
    const char *error = collect(r, world, &budget);
    if (error || !acount(r->lights)) {
        return error;
    }
    for (int i = 0; i < count; i++) {
        const float *o = entities[i].optical;
        if (!isfinite(o[0]) || (entities[i].optical_surface && o[0] < 1) ||
            o[0] > 4 || !isfinite(o[1]) || o[1] < 0 || o[1] > 1 || !isfinite(o[2]) ||
            !isfinite(o[3]) || !isfinite(o[4]) || o[2] < 0 || o[3] < 0 || o[4] < 0) {
            return "GameOpticalSurface requires IOR 1..4, mirror 0..1 and "
                   "finite nonnegative RGB absorption";
        }
    }
    if (!r->transport) {
        r->transport = render_optics_new();
    }
    if (!render_optics_draw(
            r->transport,
            entities,
            count,
            r->lights,
            acount(r->lights),
            budget,
            inputs->medium,
            inputs->medium_scattering,
            view,
            width,
            height
        )) {
        return render_optics_error(r->transport);
    }
    render_optical_field field = render_optics_field(r->transport);
    inputs->optical_direct = field.direct;
    inputs->optical_source = field.source;
    inputs->optical_surface = field.surface;
    return NULL;
}
