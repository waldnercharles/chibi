#include "runtime/scatter.h"

#include "runtime/generated.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* New cells tried per update across every scatter, so a camera arriving over
 * a large region spreads the work across steps. */
#define SCATTER_CELLS_PER_UPDATE 256
/* Cell indices stay exact in floats and ints. */
#define SCATTER_CELLS_PER_AXIS_MAX 1048576.0f
/* Samples along a surface search, then halvings of the crossing. */
#define SURFACE_SAMPLES 16
#define SURFACE_HALVINGS 6
/* Central-difference spacing of a surface normal, in world units. */
#define NORMAL_SPACING 1.0f

typedef struct
{
    uint64_t source; /* the GameScatter entity; zero marks an empty slot */
    int x, y;
} cell_key;

struct game_scatter
{
    cell_key *tried; /* open addressing; capacity is a power of two */
    int capacity, count;
    char error[256];
};

/* One scatter's settings for this update. */
typedef struct
{
    game_scatter *scatter;
    ecs_world *world;
    game_entities *entities;
    game_materials *materials;
    ecs_entity source, terrain;
    GameScatter rule;
    int run_seed;
    int budget;
} scatter_pass;

game_scatter *game_scatter_new()
{
    return xcalloc(1, sizeof(game_scatter));
}

void game_scatter_free(game_scatter *scatter)
{
    free(scatter->tried);
    free(scatter);
}

void game_scatter_reset(game_scatter *scatter)
{
    free(scatter->tried);
    *scatter = (game_scatter){ 0 };
}

static uint64_t mix(uint64_t x)
{
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

static uint64_t slot_of(cell_key key)
{
    return mix(key.source ^ mix((uint64_t)(uint32_t)key.x << 32 | (uint32_t)key.y));
}

static bool same_cell(cell_key a, cell_key b)
{
    return a.source == b.source && a.x == b.x && a.y == b.y;
}

/* The slot holding key, or the empty slot where it belongs. */
static cell_key *slot(game_scatter *scatter, cell_key key)
{
    uint64_t mask = (uint64_t)scatter->capacity - 1;
    uint64_t at = slot_of(key) & mask;
    while (scatter->tried[at].source && !same_cell(scatter->tried[at], key)) {
        at = (at + 1) & mask;
    }
    return &scatter->tried[at];
}

static void grow(game_scatter *scatter)
{
    cell_key *old = scatter->tried;
    int capacity = scatter->capacity;
    scatter->capacity = capacity ? capacity * 2 : 1024;
    scatter->tried = xcalloc((size_t)scatter->capacity, sizeof *scatter->tried);
    for (int i = 0; i < capacity; i++) {
        if (old[i].source) {
            *slot(scatter, old[i]) = old[i];
        }
    }
    free(old);
}

/* Records the cell; false when it was already tried this run. */
static bool first_try(game_scatter *scatter, cell_key key)
{
    if ((scatter->count + 1) * 2 > scatter->capacity) {
        grow(scatter);
    }
    cell_key *at = slot(scatter, key);
    if (at->source) {
        return false;
    }
    *at = key;
    scatter->count++;
    return true;
}

/* The cell's k-th uniform number from 0 up to 1, fixed by run seed and salt. */
static float cell_unit(const scatter_pass *pass, int x, int y, int k)
{
    uint64_t run = (uint64_t)(uint32_t)pass->run_seed << 32 |
                   (uint32_t)pass->rule.salt;
    uint64_t cell = (uint64_t)(uint32_t)x << 32 | (uint32_t)y;
    uint64_t bits = mix(mix(run) ^ mix(cell) ^ (uint64_t)k * UINT64_C(0x9e3779b97f4a7c15));
    return (float)(bits >> 40) / (float)(UINT64_C(1) << 24);
}

static const char *terrain_at(const scatter_pass *pass, CF_V2 p, game_terrain_sample *out)
{
    return game_materials_terrain_at(pass->materials, pass->terrain, (fluid_vec){ p.x, p.y }, out);
}

/* The first rock along dir within reach of air point p: its distance along
 * dir in *hit, or a negative *hit when none. */
static const char *surface(const scatter_pass *pass, CF_V2 p, CF_V2 dir, float reach, float *hit)
{
    *hit = -1;
    float air = 0;
    for (int i = 1; i <= SURFACE_SAMPLES; i++) {
        float t = reach * (float)i / SURFACE_SAMPLES;
        game_terrain_sample sample;
        const char *error = terrain_at(pass, cf_add(p, cf_mul(dir, t)), &sample);
        if (error) {
            return error;
        }
        if (!sample.solid) {
            air = t;
            continue;
        }
        float rock = t;
        for (int j = 0; j < SURFACE_HALVINGS; j++) {
            float mid = (air + rock) * .5f;
            if ((error = terrain_at(pass, cf_add(p, cf_mul(dir, mid)), &sample))) {
                return error;
            }
            if (sample.solid) {
                rock = mid;
            } else {
                air = mid;
            }
        }
        *hit = rock;
        return NULL;
    }
    return NULL;
}

/* The direction out of the rock at p, from the terrain's distance gradient. */
static const char *normal_at(const scatter_pass *pass, CF_V2 p, CF_V2 fallback, CF_V2 *out)
{
    CF_V2 offsets[] = { { NORMAL_SPACING, 0 },
                        { -NORMAL_SPACING, 0 },
                        { 0, NORMAL_SPACING },
                        { 0, -NORMAL_SPACING } };
    float d[4];
    for (int i = 0; i < 4; i++) {
        game_terrain_sample sample;
        const char *error = terrain_at(pass, cf_add(p, offsets[i]), &sample);
        if (error) {
            return error;
        }
        d[i] = sample.distance;
    }
    CF_V2 gradient = { d[0] - d[1], d[2] - d[3] };
    float length = cf_len(gradient);
    *out = length > 0 && isfinite(length) ? cf_mul(gradient, 1 / length) : fallback;
    return NULL;
}

/* Where a candidate lands: *fits, the pose, and whether it has a heading. */
typedef struct
{
    bool fits, oriented;
    CF_V2 position;
    float heading;
} landing;

/* Where a surface placement looks for rock; +y is down. */
static int search_directions(GameScatterPlacement placement, CF_V2 out[2])
{
    switch (placement) {
        case GAME_SCATTER_FLOOR: out[0] = (CF_V2){ 0, 1 }; return 1;
        case GAME_SCATTER_CEILING: out[0] = (CF_V2){ 0, -1 }; return 1;
        case GAME_SCATTER_WALL:
            out[0] = (CF_V2){ -1, 0 };
            out[1] = (CF_V2){ 1, 0 };
            return 2;
        case GAME_SCATTER_AIR:
        case GAME_SCATTER_EMBEDDED: break;
    }
    assert(false);
    return 0;
}

static const char *land(const scatter_pass *pass, CF_V2 p, landing *out)
{
    *out = (landing){ .position = p };
    float clearance = pass->rule.clearance;
    if (pass->rule.placement == GAME_SCATTER_AIR && !pass->terrain.slot) {
        out->fits = true;
        return NULL;
    }
    game_terrain_sample here;
    const char *error = terrain_at(pass, p, &here);
    if (error) {
        return error;
    }
    switch (pass->rule.placement) {
        case GAME_SCATTER_AIR:
            out->fits = here.distance >= clearance;
            return NULL;
        case GAME_SCATTER_EMBEDDED:
            out->fits = here.distance <= -clearance;
            return NULL;
        default: break;
    }
    if (here.solid) {
        return NULL;
    }
    CF_V2 directions[2];
    int count = search_directions(pass->rule.placement, directions);
    float nearest = -1;
    CF_V2 dir = directions[0];
    for (int i = 0; i < count; i++) {
        float hit;
        if ((error = surface(pass, p, directions[i], clearance, &hit))) {
            return error;
        }
        if (hit >= 0 && (nearest < 0 || hit < nearest)) {
            nearest = hit;
            dir = directions[i];
        }
    }
    if (nearest < 0) {
        return NULL;
    }
    CF_V2 normal;
    out->position = cf_add(p, cf_mul(dir, nearest));
    if ((error = normal_at(pass, out->position, cf_neg(dir), &normal))) {
        return error;
    }
    out->fits = out->oriented = true;
    out->heading = atan2f(normal.y, normal.x);
    return NULL;
}

static const char *try_cell(scatter_pass *pass, int x, int y)
{
    const GameScatter *rule = &pass->rule;
    if (cell_unit(pass, x, y, 0) >= rule->chance) {
        return NULL;
    }
    CF_V2 p = { rule->min.x + ((float)x + cell_unit(pass, x, y, 1)) * rule->cell,
                rule->min.y + ((float)y + cell_unit(pass, x, y, 2)) * rule->cell };
    if (p.x > rule->max.x || p.y > rule->max.y) {
        return NULL;
    }
    landing spot;
    const char *error = land(pass, p, &spot);
    if (error || !spot.fits) {
        return error;
    }
    ecs_entity spawned;
    if (game_entities_spawn_at(
            pass->entities,
            rule->prototype,
            spot.position,
            spot.oriented ? &spot.heading : NULL,
            &spawned,
            pass->scatter->error,
            sizeof pass->scatter->error
        ) != ECS_OK) {
        return pass->scatter->error;
    }
    return NULL;
}

/* Tries the untried cells of [lo, hi] in row order while budget remains. */
static const char *cover(scatter_pass *pass, CF_V2 lo, CF_V2 hi)
{
    const GameScatter *rule = &pass->rule;
    lo = cf_max(lo, rule->min);
    hi = cf_min(hi, rule->max);
    if (lo.x > hi.x || lo.y > hi.y) {
        return NULL;
    }
    /* Cells cover [min, max): the last starts before max. */
    int columns = (int)ceilf((rule->max.x - rule->min.x) / rule->cell);
    int rows = (int)ceilf((rule->max.y - rule->min.y) / rule->cell);
    int x0 = (int)floorf((lo.x - rule->min.x) / rule->cell);
    int x1 = (int)fminf(floorf((hi.x - rule->min.x) / rule->cell), (float)columns - 1);
    int y0 = (int)floorf((lo.y - rule->min.y) / rule->cell);
    int y1 = (int)fminf(floorf((hi.y - rule->min.y) / rule->cell), (float)rows - 1);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (pass->budget <= 0) {
                return NULL;
            }
            if (!first_try(pass->scatter, (cell_key){ ecs_entity_to_u64(pass->source), x, y })) {
                continue;
            }
            pass->budget--;
            const char *error = try_cell(pass, x, y);
            if (error) {
                return error;
            }
        }
    }
    return NULL;
}

static bool valid_rule(const GameScatter *rule)
{
    return rule->prototype && rule->prototype[0] && isfinite(rule->cell) &&
           rule->cell > 0 &&
           isfinite(rule->min.x + rule->min.y + rule->max.x + rule->max.y) &&
           rule->min.x <= rule->max.x && rule->min.y <= rule->max.y &&
           (rule->max.x - rule->min.x) / rule->cell < SCATTER_CELLS_PER_AXIS_MAX &&
           (rule->max.y - rule->min.y) / rule->cell < SCATTER_CELLS_PER_AXIS_MAX &&
           isfinite(rule->chance) && isfinite(rule->clearance) &&
           rule->clearance >= 0 && isfinite(rule->radius) &&
           rule->radius >= 0 && rule->placement >= GAME_SCATTER_AIR &&
           rule->placement <= GAME_SCATTER_EMBEDDED;
}

/* The entity published under a GameGlobalBinding name, or none. */
static ecs_entity bound(ecs_world *world, const char *name)
{
    ecs_component_id id = GAME_COMPONENT_GameGlobalBinding.id;
    ecs_entity found = { 0 }, entity;
    ecs_query query;
    int result = ecs_query_begin(world, &query, &id, 1);
    assert(result == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        const char *bound_name = GAME_GameGlobalBinding_get_const(world, entity)->name;
        if (bound_name && !strcmp(bound_name, name)) {
            found = entity;
        }
    }
    ecs_query_end(&query);
    return found;
}

typedef struct
{
    CF_V2 lo, hi;
} view_rect;

static view_rect *camera_views(ecs_world *world)
{
    view_rect *views = NULL;
    ecs_component_id ids[] = { GAME_COMPONENT_GameCamera.id,
                               GAME_COMPONENT_GameTransform.id };
    ecs_query query;
    int result = ecs_query_begin(world, &query, ids, 2);
    assert(result == ECS_OK);
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GameCamera *camera = GAME_GameCamera_get_const(world, entity);
        if (camera->disabled) {
            continue;
        }
        CF_V2
        center = cf_add(*GAME_GameTransform_get_const(world, entity), camera->offset);
        CF_V2 half = { camera->width * .5f, camera->height * .5f };
        apush(views, ((view_rect){ cf_sub(center, half), cf_add(center, half) }));
    }
    ecs_query_end(&query);
    return views;
}

static const char *scatter_one(scatter_pass *pass, const view_rect *views)
{
    const GameScatter *rule = &pass->rule;
    if (!valid_rule(rule)) {
        return "Scatter requires a prototype, positive cell, min at most max, "
               "and finite nonnegative clearance and radius";
    }
    pass->terrain = rule->terrain && rule->terrain[0]
                      ? bound(pass->world, rule->terrain)
                      : (ecs_entity){ 0 };
    if ((rule->terrain && rule->terrain[0] && !pass->terrain.slot) ||
        (!pass->terrain.slot && rule->placement != GAME_SCATTER_AIR)) {
        return "Scatter terrain must name the GameGlobalBinding of a "
               "MaterialTerrain entity";
    }
    CF_V2 reach = { rule->radius, rule->radius };
    for (int i = 0; i < acount(views); i++) {
        const char
            *error = cover(pass, cf_sub(views[i].lo, reach), cf_add(views[i].hi, reach));
        if (error) {
            return error;
        }
    }
    return NULL;
}

const char *game_scatter_update(
    game_scatter *scatter,
    ecs_world *world,
    game_entities *entities,
    game_materials *materials,
    int run_seed
)
{
    ecs_component_id id = GAME_COMPONENT_GameScatter.id;
    if (!ecs_world_component(world, id) ||
        !ecs_component_has_entities(world, id, NULL, 0)) {
        return NULL;
    }
    view_rect *views = camera_views(world);
    ecs_entity *sources = NULL, entity;
    ecs_query query;
    int result = ecs_query_begin(world, &query, &id, 1);
    assert(result == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        if (!game_entities_is_retiring(entities, entity)) {
            apush(sources, entity);
        }
    }
    ecs_query_end(&query);
    scatter_pass pass = { .scatter = scatter,
                          .world = world,
                          .entities = entities,
                          .materials = materials,
                          .run_seed = run_seed,
                          .budget = SCATTER_CELLS_PER_UPDATE };
    const char *error = NULL;
    for (int i = 0; i < acount(sources) && !error && pass.budget > 0; i++) {
        pass.source = sources[i];
        /* Spawning grows component storage: copy the rule first. */
        pass.rule = *GAME_GameScatter_get_const(world, sources[i]);
        error = scatter_one(&pass, views);
    }
    afree(sources);
    afree(views);
    return error;
}
