#include "render/particles.h"
#include "runtime/generated.h"
#include "runtime/material_ecs.h"
#include "support/array.h"

#include <math.h>

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
    game_materials *materials,
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

void material_ecs_tick_debris(game_materials *materials, const fluid_step *step)
{
    if (!materials->debris) {
        return;
    }
    material_debris_blast *blasts = NULL;
    if (materials->fluid) {
        int count;
        const fluid_blast *events = material_fluid_blasts(materials->fluid, &count);
        for (int i = 0; i < count; i++) {
            float density = fminf(1, events[i].ignitions / 8.f);
            apush(
                blasts,
                ((material_debris_blast){
                    events[i].position,
                    materials->combustion.blast_radius * (.35f + .65f * density),
                    materials->combustion.blast_speed * density })
            );
        }
    }
    material_debris_tick(materials->debris, step, blasts, acount(blasts));
    afree(blasts);
    /* A spent chunk leaves a small puff of its material's dust. */
    int count;
    const material_chunk *expired = material_debris_expired(materials->debris, &count);
    for (int i = 0; i < count && materials->particles; i++) {
        const material_chunk *chunk = &expired[i];
        ecs_entity source = ecs_entity_from_u64(chunk->source);
        const MaterialBreakProfile
            *profile = ecs_entity_is_alive(materials->world, source)
                         ? GAME_MaterialBreakProfile_get_const(materials->world, source)
                         : NULL;
        unsigned color = profile && profile->smoke_color ? profile->smoke_color
                                                         : 0x8c8378b0u;
        game_particle puff = {
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
            .shape = GAME_PARTICLE_ROUND,
        };
        game_particles_emit_particle(materials->particles, &puff);
    }
}

const material_chunk *game_materials_chunks(const game_materials *materials, int *count)
{
    if (!materials || !materials->debris) {
        *count = 0;
        return NULL;
    }
    return material_debris_chunks(materials->debris, count);
}
