#include "runtime/component_catalog.h"
#include "runtime/material_ecs.h"
#include "runtime/particles.h"
#include "runtime/transform_propagation.h"
#include "support/array.h"

#include <math.h>

/* A resident terrain mask and where its local origin sits in the world. */
struct material_debris_tile
{
    const material_mask *mask;
    float x, y;
};

/* Reach of the shove a crater gives nearby loose chunks, in world units. */
static const float CRATER_BLAST_RADIUS = 30;

/* Deterministic value in [0, 1) for a crater's chunks. */
static float chunk_random(fluid_vec point, int index, int salt)
{
    unsigned h = (unsigned)(int)floorf(point.x * 8) * 0x27d4eb2du ^
                 (unsigned)(int)floorf(point.y * 8) * 0x165667b1u ^
                 (unsigned)(index * 8 + salt) * 0x9e3779b9u;
    h = (h ^ (h >> 15)) * 0x2c1b3c6du;
    h = (h ^ (h >> 12)) * 0x297a2d39u;
    return ((h ^ (h >> 15)) & 0xffffff) / 16777216.0f;
}

void material_ecs_throw_chunks(
    materials *materials,
    fluid_vec point,
    fluid_vec normal,
    int cells,
    uint64_t source,
    float heat,
    MaterialBreakProfile profile
)
{
    /* The strike blasts loose rock already lying around the crater. */
    material_debris_push(
        materials->debris,
        (material_debris_blast){ point, CRATER_BLAST_RADIUS, profile.chunk_speed_max * .5f }
    );
    int count = (int)(cells / profile.chunk_cells + .5f);
    count = count < profile.chunk_limit ? count : profile.chunk_limit;
    for (int i = 0; i < count; i++) {
        float radius = profile.chunk_size_min +
                       (profile.chunk_size_max - profile.chunk_size_min) *
                           chunk_random(point, i, 0);
        /* The striker's heat of a hot shot makes chunks glow white hot. */
        material_chunk chunk = { .radius = radius,
                                 .source = source,
                                 .heat = fminf(1, heat * .4f) };
        material_chunk_shape(&chunk, (unsigned)(chunk_random(point, i, 1) * 16777216.0f));
        float spread = (chunk_random(point, i, 3) - .5f) * 1.2f;
        float c = cosf(spread), s = sinf(spread);
        fluid_vec direction = { c * normal.x - s * normal.y,
                                s * normal.x + c * normal.y };
        float speed = profile.chunk_speed_min +
                      (profile.chunk_speed_max - profile.chunk_speed_min) *
                          chunk_random(point, i, 4);
        chunk.position = (fluid_vec){ point.x + normal.x * (radius + 1),
                                      point.y + normal.y * (radius + 1) };
        chunk.origin = chunk.position;
        chunk.velocity = (fluid_vec){ direction.x * speed, direction.y * speed };
        chunk.angle = 0;
        chunk.spin = (chunk_random(point, i, 5) - .5f) * 14;
        chunk.lifetime = profile.chunk_lifetime *
                         (.7f + .6f * chunk_random(point, i, 6));
        material_debris_add(materials->debris, chunk);
    }
}

/* Bilinear signed distance from a terrain mask's cell-center samples. */
static float sample_mask(const material_mask *mask, float x, float y)
{
    float fx = x / mask->cell_size + mask->width * .5f - .5f;
    float fy = y / mask->cell_size + mask->height * .5f - .5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float tx = fx - x0, ty = fy - y0;
    int x1 = x0 + 1, y1 = y0 + 1;
    x0 = x0 < 0 ? 0 : x0 >= mask->width ? mask->width - 1 : x0;
    x1 = x1 < 0 ? 0 : x1 >= mask->width ? mask->width - 1 : x1;
    y0 = y0 < 0 ? 0 : y0 >= mask->height ? mask->height - 1 : y0;
    y1 = y1 < 0 ? 0 : y1 >= mask->height ? mask->height - 1 : y1;
    const float *d = mask->distance;
    float top = d[y0 * mask->width + x0] +
                (d[y0 * mask->width + x1] - d[y0 * mask->width + x0]) * tx;
    float bottom = d[y1 * mask->width + x0] +
                   (d[y1 * mask->width + x1] - d[y1 * mask->width + x0]) * tx;
    /* Negative inside solid cells, positive in open space. */
    return top + (bottom - top) * ty;
}

static bool terrain_distance(void *user, fluid_vec point, float *distance)
{
    const materials *materials = user;
    for (int i = 0; i < acount(materials->debris_tiles); i++) {
        struct material_debris_tile tile = materials->debris_tiles[i];
        float half_w = tile.mask->width * tile.mask->cell_size * .5f;
        float half_h = tile.mask->height * tile.mask->cell_size * .5f;
        float x = point.x - tile.x, y = point.y - tile.y;
        if (fabsf(x) > half_w || fabsf(y) > half_h) {
            continue;
        }
        *distance = sample_mask(tile.mask, x, y);
        return true;
    }
    return false;
}

void material_ecs_tick_debris(
    materials *materials,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
)
{
    aclear(materials->debris_tiles);
    aclear(materials->debris_blasts);
    if (!materials->debris) {
        return;
    }
    for (int i = 0; i < acount(materials->records); i++) {
        material_record *record = &materials->records[i];
        if (record->fragment || !ecs_entity_to_u64(record->terrain_source) ||
            !record->mask || !record->mask->width ||
            !ecs_entity_is_alive(materials->world, record->entity)) {
            continue;
        }
        CF_V2 p = transform_world_2d(materials->world, record->entity).position;
        apush(materials->debris_tiles, ((struct material_debris_tile){ record->mask, p.x, p.y }));
    }
    if (materials->fluid) {
        int count;
        const fluid_blast *events = material_fluid_blasts(materials->fluid, &count);
        for (int i = 0; i < count; i++) {
            float density = fminf(1, events[i].ignitions / 8.f);
            apush(
                materials->debris_blasts,
                ((material_debris_blast){
                    events[i].position,
                    materials->combustion.blast_radius * (.35f + .65f * density),
                    materials->combustion.blast_speed * density })
            );
        }
    }
    material_debris_tick(
        materials->debris,
        delta,
        terrain_distance,
        materials,
        bodies,
        body_count,
        obstacles,
        obstacle_count,
        materials->debris_blasts,
        acount(materials->debris_blasts)
    );
    aclear(materials->debris_blasts);
    aclear(materials->debris_tiles);
    /* A spent chunk leaves a small puff of its material's dust. */
    int count;
    const material_chunk *expired = material_debris_expired(materials->debris, &count);
    for (int i = 0; i < count && materials->particles; i++) {
        const material_chunk *chunk = &expired[i];
        ecs_entity source = ecs_entity_from_u64(chunk->source);
        const MaterialBreakProfile
            *profile = ecs_entity_is_alive(materials->world, source)
                         ? MaterialBreakProfile_get_const(materials->world, source)
                         : NULL;
        unsigned color = profile && profile->smoke_color ? profile->smoke_color
                                                         : 0x8c8378b0u;
        particle puff = {
            .position = { chunk->position.x, chunk->position.y },
            .velocity = { chunk->velocity.x * .3f, chunk->velocity.y * .3f },
            .color = color,
            .end_color = color & 0xffffff00u,
            .lifetime = .5f + .3f * chunk_random(chunk->position, i, 0),
            .size = chunk->radius * .4f,
            .size_end = chunk->radius * 1.3f,
            .size_exponent = .5f,
            .drag = 4,
            .drag_end = 4,
            .antialias = chunk->radius * .5f,
            .antialias_end = chunk->radius,
            .antialias_exponent = 1,
            .shape = PARTICLE_ROUND,
        };
        particles_emit_particle(materials->particles, &puff);
    }
}

const material_chunk *materials_chunks(const materials *materials, int *count)
{
    if (!materials || !materials->debris) {
        *count = 0;
        return NULL;
    }
    return material_debris_chunks(materials->debris, count);
}
