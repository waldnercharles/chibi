#include "runtime/materials.h"

#include "diagnostics/tracy.h"
#include "graph/runtime/flow.h"
#include "render/particles.h"
#include "runtime/behaviors/entities.h"
#include "runtime/generated.h"
#include "runtime/material_cave.h"
#include "runtime/task_pool.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    ecs_entity entity;
    material_mask *mask;
    bool fragment, volume, cave, release;
    float glow_age, glow_multiplier;
    int initial_cells;
    ecs_entity terrain_source;
} material_record;

typedef struct
{
    ecs_entity entity;
    fluid_vec point;
    float radius, energy;
    bool blast;
} material_cut;

struct game_materials
{
    ecs_world *world;
    game_entities *entities;
    game_particles *particles;
    material_fluid *fluid;
    game_task_pool *pool;
    GameMaterialWorld settings;
    GameCombustion combustion;
    material_record *records;
    material_cut *cuts;
    fluid_region *regions;
    int fragments;
};

game_materials *game_materials_new(ecs_world *world, game_entities *entities, game_particles *particles)
{
    game_materials *materials = xcalloc(1, sizeof *materials);
    materials->world = world;
    materials->entities = entities;
    materials->particles = particles;
    materials->pool = game_task_pool_acquire();
    return materials;
}

static void release_record(material_record *record)
{
    if (record->mask) {
        material_mask_free(record->mask);
        free(record->mask);
    }
    *record = (material_record){ 0 };
}

void game_materials_reset(game_materials *materials)
{
    for (int i = 0; i < acount(materials->records); i++) {
        material_record *record = &materials->records[i];
        if ((record->fragment || ecs_entity_to_u64(record->terrain_source)) &&
            ecs_entity_is_alive(materials->world, record->entity)) {
            int result = ecs_world_destroy_entity(materials->world, record->entity);
            assert(result == ECS_OK);
        }
        release_record(record);
    }
    aclear(materials->records);
    aclear(materials->cuts);
    aclear(materials->regions);
    materials->fragments = 0;
    if (materials->fluid) {
        material_fluid_free(materials->fluid);
        materials->fluid = NULL;
    }
    materials->settings = (GameMaterialWorld){ 0 };
}

void game_materials_free(game_materials *materials)
{
    game_materials_reset(materials);
    afree(materials->records);
    afree(materials->cuts);
    afree(materials->regions);
    game_task_pool_release(materials->pool);
    free(materials);
}

bool game_materials_enabled(const game_materials *materials)
{
    return materials && materials->fluid;
}

const material_fluid *game_materials_fluid(const game_materials *materials)
{
    return materials->fluid;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t count)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < count; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    return hash;
}

game_material_state game_materials_inspect(const game_materials *materials)
{
    game_material_state state = { 0 };
    if (!game_materials_enabled(materials)) {
        return state;
    }
    state.hash = UINT64_C(14695981039346656037);
    const fluid_particle
        *particles = material_fluid_particles(materials->fluid, &state.particles);
    state.active_particles = material_fluid_active_count(materials->fluid);
    uint64_t flow_hash = material_fluid_flow_hash(materials->fluid);
    if (flow_hash) {
        state.hash = hash_bytes(state.hash, &flow_hash, sizeof flow_hash);
    }
    state.fragments = materials->fragments;
    for (int i = 0; i < state.particles; i++) {
        fluid_particle p = particles[i];
        state.fuel_particles += p.fuel > 0;
        state.burning_particles += p.burning;
        float values[] = { p.position.x,
                           p.position.y,
                           p.velocity.x,
                           p.velocity.y,
                           p.mass,
                           p.radius,
                           p.absorption,
                           p.emission,
                           p.lifetime,
                           p.fuel,
                           p.heat,
                           p.glow,
                           p.visibility,
                           p.refraction,
                           p.style.duration,
                           p.style.size_end,
                           p.style.size_exponent,
                           p.style.emission_end,
                           p.style.emission_exponent,
                           p.style.drag,
                           p.style.drag_end,
                           p.style.drag_exponent,
                           p.style.blend };
        state.hash = hash_bytes(state.hash, &p.burning, sizeof p.burning);
        state.hash = hash_bytes(state.hash, &p.smoke, sizeof p.smoke);
        state.hash = hash_bytes(state.hash, values, sizeof values);
        state.hash = hash_bytes(state.hash, &p.kind, sizeof p.kind);
        state.hash = hash_bytes(state.hash, &p.color, sizeof p.color);
        state.hash = hash_bytes(state.hash, &p.style.end_color, sizeof p.style.end_color);
    }
    for (int i = 0; i < acount(materials->records); i++) {
        const material_record *record = &materials->records[i];
        if (!record->mask) {
            continue;
        }
        const material_mask *mask = record->mask;
        uint64_t entity = ecs_entity_to_u64(record->entity);
        int dimensions[] = { mask->width, mask->height };
        state.solids++;
        state.cells += mask->count;
        state.hash = hash_bytes(state.hash, &entity, sizeof entity);
        state.hash = hash_bytes(state.hash, &record->glow_age, sizeof record->glow_age);
        state.hash = hash_bytes(
            state.hash,
            &record->glow_multiplier,
            sizeof record->glow_multiplier
        );
        state.hash = hash_bytes(state.hash, &record->release, sizeof record->release);
        state
            .hash = hash_bytes(state.hash, &record->initial_cells, sizeof record->initial_cells);
        state.hash = hash_bytes(state.hash, dimensions, sizeof dimensions);
        state.hash = hash_bytes(state.hash, &mask->cell_size, sizeof mask->cell_size);
        state.hash = hash_bytes(state.hash, mask->cells, (size_t)mask->width * mask->height);
        state.hash = hash_bytes(
            state.hash,
            mask->distance,
            (size_t)mask->width * mask->height * sizeof *mask->distance
        );
        if (mask->damage) {
            state.hash = hash_bytes(
                state.hash,
                mask->damage,
                (size_t)mask->width * mask->height * sizeof *mask->damage
            );
        }
    }
    for (int i = 0; i < acount(materials->cuts); i++) {
        material_cut cut = materials->cuts[i];
        uint64_t entity = ecs_entity_to_u64(cut.entity);
        float values[] = { cut.point.x, cut.point.y, cut.radius, cut.energy };
        state.hash = hash_bytes(state.hash, &cut.blast, sizeof cut.blast);
        state.hash = hash_bytes(state.hash, &entity, sizeof entity);
        state.hash = hash_bytes(state.hash, values, sizeof values);
    }
    return state;
}

bool game_materials_emit(game_materials *materials, fluid_particle particle)
{
    particle.radius = fminf(particle.radius, materials->settings.kernel);
    return materials->fluid && material_fluid_add(materials->fluid, particle);
}

const material_mask *game_materials_mask(const game_materials *materials, ecs_entity entity)
{
    if (!materials || entity.slot >= (unsigned)acount(materials->records)) {
        return NULL;
    }
    const material_record *record = &materials->records[entity.slot];
    return ecs_entity_to_u64(record->entity) == ecs_entity_to_u64(entity)
             ? record->mask
             : NULL;
}

bool game_materials_is_fragment(const game_materials *materials, ecs_entity entity)
{
    return game_materials_mask(materials, entity) &&
           materials->records[entity.slot].fragment;
}

static material_record *record_for(game_materials *materials, ecs_entity entity)
{
    if (entity.slot >= (unsigned)acount(materials->records)) {
        int previous = acount(materials->records);
        asetlen(materials->records, (int)entity.slot + 1);
        memset(
            materials->records + previous,
            0,
            (size_t)(acount(materials->records) - previous) * sizeof *materials->records
        );
    }
    material_record *record = &materials->records[entity.slot];
    if (ecs_entity_to_u64(record->entity) != ecs_entity_to_u64(entity)) {
        materials->fragments -= record->fragment;
        release_record(record);
        record->entity = entity;
    }
    return record;
}

static const char *configure(game_materials *materials)
{
    ecs_component_id id = GAME_COMPONENT_GameMaterialWorld.id;
    if (!ecs_world_component(materials->world, id)) {
        return NULL;
    }
    ecs_query query;
    int result = ecs_query_begin(materials->world, &query, &id, 1);
    assert(result == ECS_OK);
    ecs_entity entity;
    int count = 0;
    GameMaterialWorld settings = { 0 };
    bool reactive = false;
    while (ecs_query_next(&query, &entity)) {
        settings = *GAME_GameMaterialWorld_get_const(materials->world, entity);
        const GameCombustion *reaction = GAME_GameCombustion_get_const(materials->world, entity);
        reactive = reaction != NULL;
        materials->combustion = reaction ? *reaction : (GameCombustion){ 0 };
        count++;
    }
    ecs_query_end(&query);
    if (!count) {
        if (materials->fluid) {
            game_materials_reset(materials);
        }
        return NULL;
    }
    if (count != 1 ||
        !isfinite(settings.chunk_size + settings.buffer + settings.kernel) ||
        settings.chunk_size < 32 || settings.chunk_size > 1024 ||
        settings.buffer < settings.kernel || settings.buffer > 2048 ||
        settings.kernel < 2 || settings.kernel > 64 ||
        settings.particle_limit < 1 || settings.particle_limit > 1000000 ||
        settings.fragment_limit < 0 || settings.fragment_limit > 1024) {
        return "Material world requires one configuration, chunks 32..1024, "
               "kernel 2..64, a buffer at least one kernel, 1..1000000 "
               "particles and 0..1024 fragments";
    }
    if (materials->fluid &&
        (settings.kernel != materials->settings.kernel ||
         settings.particle_limit != materials->settings.particle_limit)) {
        return "Restart the scene after changing the material kernel or "
               "particle capacity";
    }
    if (!isfinite(settings.gas_cell_size + settings.gas_vorticity + settings.gas_drag) ||
        (settings.gas_cell_size != 0 &&
         (settings.gas_cell_size < 2 || settings.gas_cell_size > 64)) ||
        settings.gas_vorticity < 0 || settings.gas_vorticity > 10 ||
        settings.gas_drag < 0 || settings.gas_drag > 10) {
        return "Gas flow requires cell size 0 or 2..64, vorticity 0..10 and "
               "drag 0..10";
    }
    materials->settings = settings;
    GameCombustion r = materials->combustion;
    if (reactive &&
        (!isfinite(
             r.ignition + r.cooling + r.burn_rate + r.heat_release +
             r.smoke_lifetime + r.smoke_absorption + r.flame_emission +
             r.blast_radius + r.blast_speed + r.blast_damage
         ) ||
         r.ignition <= 0 || r.cooling < 0 || r.burn_rate <= 0 ||
         r.heat_release <= r.ignition || r.smoke_lifetime <= 0 ||
         r.smoke_absorption < 0 || r.flame_emission < 0 || r.blast_radius < 0 ||
         r.blast_radius > 256 || r.blast_speed < 0 || r.blast_damage < 0)) {
        return "Combustion requires finite nonnegative tuning, positive "
               "ignition/burn/lifetime, heat above ignition and blast radius "
               "<=256";
    }
    if (!materials->fluid) {
        materials->fluid = material_fluid_new(settings.kernel, settings.particle_limit);
        material_fluid_set_tasks(
            materials->fluid,
            game_task_pool_tasks(materials->pool)
        );
    }
    material_fluid_set_flow(
        materials->fluid,
        (fluid_flow_settings){ settings.gas_cell_size,
                               settings.gas_vorticity,
                               settings.gas_drag }
    );
    material_fluid_set_reaction(
        materials->fluid,
        (fluid_reaction){ r.ignition,
                          r.cooling,
                          r.burn_rate,
                          r.heat_release,
                          r.smoke_lifetime,
                          r.smoke_color,
                          r.smoke_absorption,
                          r.flame_color,
                          r.flame_emission }
    );
    return NULL;
}

static const char *create_mask(game_materials *materials, ecs_entity entity)
{
    const GameFragmentGlow *glow = GAME_GameFragmentGlow_get_const(materials->world, entity);
    if (glow && (!isfinite(glow->duration + glow->flicker) ||
                 glow->duration < 0 || glow->flicker < 0 || glow->flicker > 1)) {
        return "Fragment glow needs a nonnegative duration and flicker 0..1";
    }
    const GameDestructible *style = GAME_GameDestructible_get_const(materials->world, entity);
    const GameCollider *collider = GAME_GameCollider_get_const(materials->world, entity);
    if (!collider || !GAME_GameTransform_get_const(materials->world, entity) ||
        !isfinite(style->cell_size) || style->cell_size <= 0 ||
        style->min_fragment_cells < 1 || !isfinite(style->release_fraction) ||
        style->release_fraction < 0 || style->release_fraction > 1) {
        return "Destructible needs Transform, Collider, a positive cell size "
               "and a minimum fragment size, with release fraction 0..1";
    }
    float width = collider->shape == GAME_COLLIDER_CIRCLE ? 2 * collider->radius
                                                          : collider->width;
    float height = collider->shape == GAME_COLLIDER_CIRCLE ? 2 * collider->radius
                                                           : collider->height;
    float nx = ceilf(width / style->cell_size), ny = ceilf(height / style->cell_size);
    if (!(nx >= 1 && nx <= 256 && ny >= 1 && ny <= 256)) {
        return "Destructible dimensions must fit 1..256 material cells per "
               "axis";
    }
    const GameSubstance *substance = GAME_GameSubstance_get_const(materials->world, entity);
    if (substance && (!isfinite(substance->hardness + substance->blast_multiplier) ||
                      substance->hardness <= 0 || substance->blast_multiplier < 0)) {
        return "Substance needs positive hardness and nonnegative blast "
               "multiplier";
    }
    material_record *record = record_for(materials, entity);
    if (!record->mask) {
        record->mask = xmalloc(sizeof *record->mask);
        *record->mask = material_mask_new((int)nx, (int)ny, style->cell_size, collider->shape == GAME_COLLIDER_CIRCLE);
        const GameMaterialCavity
            *cavity = GAME_GameMaterialCavity_get_const(materials->world, entity);
        if (cavity) {
            if (!isfinite(cavity->radius) || cavity->radius <= 0 ||
                cavity->radius >= fminf(width, height) * .5f) {
                return "Material cavity must fit inside its collider";
            }
            material_mask_carve(record->mask, 0, 0, cavity->radius);
        }
        record->initial_cells = record->mask->count;
    }
    return NULL;
}

static float volume_density(GameFluidVolume v, float x, float y)
{
    float nx = 2 * x / v.width, ny = 2 * y / v.height;
    float radius = hypotf(nx, ny);
    if (v.kind == FLUID_LIQUID) {
        return radius <= 1;
    }
    float angle = atan2f(ny, nx);
    float edge = .76f + .12f * sinf(3 * angle) + .07f * cosf(5 * angle + 1) +
                 .05f * sinf(7 * angle + 2);
    return fmaxf(0, fminf(1, (edge - radius) / .22f));
}

static bool in_solid(game_materials *materials, fluid_vec point)
{
    for (int i = 0; i < acount(materials->records); i++) {
        material_record record = materials->records[i];
        if (!record.mask) {
            continue;
        }
        const GameTransform
            *p = GAME_GameTransform_get_const(materials->world, record.entity);
        const GameOrientation
            *heading = GAME_GameOrientation_get_const(materials->world, record.entity);
        float angle = heading ? heading->radians : 0, c = cosf(angle), s = sinf(angle);
        float x = point.x - p->x, y = point.y - p->y;
        if (material_mask_contains(record.mask, c * x + s * y, -s * x + c * y)) {
            return true;
        }
    }
    return false;
}

static const char *fill_volume(game_materials *materials, ecs_entity entity)
{
    GameFluidVolume v = *GAME_GameFluidVolume_get_const(materials->world, entity);
    const GameFuel *fuel = GAME_GameFuel_get_const(materials->world, entity);
    const GameIgniter *igniter = GAME_GameIgniter_get_const(materials->world, entity);
    if ((fuel && (!isfinite(fuel->amount) || fuel->amount < 0)) ||
        (igniter && (!isfinite(igniter->heat) || igniter->heat < 0))) {
        return "Fuel and heat must be finite and nonnegative";
    }
    const GameTransform *origin = GAME_GameTransform_get_const(materials->world, entity);
    if (!origin || !isfinite(origin->x + origin->y) ||
        (v.kind != FLUID_GAS && v.kind != FLUID_LIQUID) ||
        !isfinite(
            v.width + v.height + v.spacing + v.mass + v.absorption + v.emission +
            v.visibility + v.refraction + v.velocity.x + v.velocity.y
        ) ||
        v.width <= 0 || v.height <= 0 || v.width > 8192 || v.height > 8192 ||
        v.spacing < materials->settings.kernel * .3f ||
        v.spacing > materials->settings.kernel || v.mass <= 0 ||
        v.absorption < 0 || v.emission < 0 || v.visibility < 0 ||
        v.visibility > 1 || v.refraction < 0 || v.refraction > 16) {
        return "Fluid volume needs Transform, gas/liquid kind, positive "
               "dimensions up to 8192, spacing 0.3..1 kernels, positive mass "
               "and nonnegative lighting";
    }
    material_record *record = record_for(materials, entity);
    if (record->volume) {
        return NULL;
    }
    const GameOrientation
        *orientation = GAME_GameOrientation_get_const(materials->world, entity);
    float angle = orientation ? orientation->radians : 0, c = cosf(angle),
          s = sinf(angle);
    int nx = (int)ceilf(v.width / v.spacing), ny = (int)ceilf(v.height / v.spacing);
    int required = 0, existing;
    material_fluid_particles(materials->fluid, &existing);
    for (int y = 0; y < ny; y++) {
        for (int x = 0; x < nx; x++) {
            float px = (x + .5f - nx * .5f) * v.spacing;
            float py = (y + .5f - ny * .5f) * v.spacing;
            fluid_vec position = { origin->x + c * px - s * py,
                                   origin->y + s * px + c * py };
            required += volume_density(v, px, py) > 0 &&
                        !in_solid(materials, position);
        }
    }
    if (required > materials->settings.particle_limit - existing) {
        return "Fluid volumes exceed the world's particle capacity; reduce "
               "their size or raise the limit";
    }
    record->volume = true;
    for (int y = 0; y < ny; y++) {
        for (int x = 0; x < nx; x++) {
            float px = (x + .5f - nx * .5f) * v.spacing;
            float py = (y + .5f - ny * .5f) * v.spacing;
            float fill = volume_density(v, px, py);
            fluid_vec position = { origin->x + c * px - s * py,
                                   origin->y + s * px + c * py };
            if (fill <= 0 || in_solid(materials, position)) {
                continue;
            }
            unsigned color = v.color;
            if (v.kind == FLUID_GAS) {
                /* Fixed local texture travels with the particles. Jitter
                 * removes lattice banding; broad opacity variation gives
                 * an art-directed cloud without a second fine fluid solve. */
                unsigned hash = ((unsigned)x * 73856093u ^ (unsigned)y * 19349663u) *
                                1597334677u;
                float density = .6f + .25f * sinf(px * .023f) * cosf(py * .037f) +
                                .15f * sinf(px * .087f + py * .049f);
                color = (color & 0xffffff00u) |
                        (unsigned)((color & 255) * density * fill);
                px += (((hash & 65535) / 65535.f) - .5f) * v.spacing * .65f;
                py += (((hash >> 16) / 65535.f) - .5f) * v.spacing * .65f;
            }
            fluid_particle p = {
                .position = { origin->x + c * px - s * py, origin->y + s * px + c * py },
                .velocity = { c * v.velocity.x - s * v.velocity.y,
                              s * v.velocity.x + c * v.velocity.y },
                .mass = v.mass,
                .radius = v.kind == FLUID_GAS
                            ? materials->settings.kernel
                            : fminf(v.spacing * 1.5f, materials->settings.kernel),
                .kind = v.kind,
                .color = color,
                .absorption = v.absorption,
                .fuel = fuel ? fuel->amount : 0,
                .heat = igniter ? igniter->heat : 0,
                .emission = v.emission,
                .visibility = v.visibility,
                .refraction = v.refraction
            };
            if (!material_fluid_add(materials->fluid, p)) {
                return "Fluid volumes exceed the world's particle capacity; "
                       "reduce their size or raise the limit";
            }
        }
    }
    return NULL;
}

static void dust(game_materials *materials, fluid_vec point, int cells, unsigned color, float cell_size)
{
    if (!cells) {
        return;
    }
    /* Unretained cells are a short local dust puff. Bright impact sparks belong
     * to authored emitters; ordinary excavation must not become an explosion. */
    game_particle_spray spray = { .position = { point.x, point.y },
                                  .direction = { 1, 0 },
                                  .color = color,
                                  .end_color = color & 0xffffff00u,
                                  .count = cells < 8 ? cells : 8,
                                  .speed_min = cell_size * 4,
                                  .speed_max = cell_size * 12,
                                  .lifetime_min = .15f,
                                  .lifetime_max = .3f,
                                  .size_min = cell_size * .25f,
                                  .size_max = cell_size * .5f,
                                  .size_exponent = 1,
                                  .spread = 6.2831853f,
                                  .shape = GAME_PARTICLE_ROUND };
    game_particles_emit_directed(materials->particles, &spray);
}

static void add_component(
    ecs_world *world,
    ecs_entity entity,
    const ecs_component_desc *component,
    const void *value
)
{
    if (!ecs_world_component(world, component->id)) {
        int result = ecs_world_register_component(world, component, NULL);
        assert(result == ECS_OK);
    }
    int result = ecs_world_record_add(world, entity, component->id, value, 0);
    assert(result == ECS_OK);
}

enum
{
    TERRAIN_CHUNK_CELLS = 128
};

static const char *generate_cave(game_materials *materials, ecs_entity source)
{
    if (record_for(materials, source)->cave) {
        return NULL;
    }
    ecs_world *world = materials->world;
    GameCaveTerrain cave = *GAME_GameCaveTerrain_get_const(world, source);
    const GameTransform *position = GAME_GameTransform_get_const(world, source);
    if (!position ||
        !isfinite(
            position->x + position->y + cave.width + cave.height +
            cave.cell_size + cave.tunnel_radius + cave.roughness
        ) ||
        cave.cell_size < 1 || cave.cell_size > 8 ||
        cave.width < 128 * cave.cell_size || cave.height < 128 * cave.cell_size ||
        cave.width > 2048 * cave.cell_size || cave.height > 2048 * cave.cell_size ||
        cave.tunnel_radius < 16 * cave.cell_size ||
        cave.tunnel_radius > cave.height * .35f || cave.roughness < 0 ||
        cave.roughness > cave.tunnel_radius * .5f || cave.min_fragment_cells < 1) {
        return "Cave terrain needs a finite Transform, cell size 1..8, "
               "dimensions 128..2048 cells, tunnel radius 16 cells..35% "
               "height, roughness 0..half radius and a positive fragment "
               "minimum";
    }
    GameTransform origin = *position;
    const GameAppearance *look = GAME_GameAppearance_get_const(world, source);
    GameAppearance appearance = look ? *look
                                     : (GameAppearance){ .color = 0xffffffffu };
    const GameMaterial *surface = GAME_GameMaterial_get_const(world, source);
    GameMaterial material = surface ? *surface : (GameMaterial){ 0 };
    int width = (int)ceilf(cave.width / cave.cell_size),
        height = (int)ceilf(cave.height / cave.cell_size);
    const GameShader *binding = GAME_GameShader_get_const(world, source);
    GameShader shader = binding ? *binding : (GameShader){ 0 };
    bool has_shader = binding != NULL;
    const GameSubstance *substance_ptr = GAME_GameSubstance_get_const(world, source);
    GameSubstance substance = substance_ptr ? *substance_ptr : (GameSubstance){ 0 };
    const GameReflection *reflection_ptr = GAME_GameReflection_get_const(world, source);
    GameReflection reflection = reflection_ptr ? *reflection_ptr
                                               : (GameReflection){ 0 };
    float *cells = material_cave_generate(
        cave.seed,
        width,
        height,
        cave.cell_size,
        cave.tunnel_radius,
        cave.roughness
    );
    float chunk[TERRAIN_CHUNK_CELLS * TERRAIN_CHUNK_CELLS];
    for (int y = 0; y < height; y += TERRAIN_CHUNK_CELLS) {
        for (int x = 0; x < width; x += TERRAIN_CHUNK_CELLS) {
            int w = width - x < TERRAIN_CHUNK_CELLS ? width - x : TERRAIN_CHUNK_CELLS;
            int h = height - y < TERRAIN_CHUNK_CELLS ? height - y : TERRAIN_CHUNK_CELLS;
            int count = 0;
            for (int row = 0; row < h; row++) {
                memcpy(chunk + row * w, cells + (y + row) * width + x, (size_t)w * sizeof *cells);
                for (int col = 0; col < w; col++) {
                    count += chunk[row * w + col] <= 0;
                }
            }
            if (!count) {
                continue;
            }
            ecs_entity entity = ecs_world_create_entity(world);
            GameTransform transform = {
                origin.x + (x + w * .5f - width * .5f) * cave.cell_size,
                origin.y + (y + h * .5f - height * .5f) * cave.cell_size
            };
            GameCollider collider = { .shape = GAME_COLLIDER_BOX,
                                      .width = w * cave.cell_size,
                                      .height = h * cave.cell_size,
                                      .category = cave.category,
                                      .mask = cave.mask };
            GameDestructible destructible = { cave.cell_size, cave.min_fragment_cells };
            appearance.width = collider.width;
            appearance.height = collider.height;
            add_component(world, entity, &GAME_COMPONENT_GameTransform, &transform);
            add_component(world, entity, &GAME_COMPONENT_GameCollider, &collider);
            add_component(world, entity, &GAME_COMPONENT_GameDestructible, &destructible);
            add_component(world, entity, &GAME_COMPONENT_GameAppearance, &appearance);
            add_component(world, entity, &GAME_COMPONENT_GameMaterial, &material);
            if (substance.hardness > 0) {
                add_component(world, entity, &GAME_COMPONENT_GameSubstance, &substance);
            }
            if (reflection.strength > 0) {
                add_component(world, entity, &GAME_COMPONENT_GameReflection, &reflection);
            }
            if (has_shader) {
                add_component(world, entity, &GAME_COMPONENT_GameShader, &shader);
            }
            int result = ecs_world_apply_commands(world);
            assert(result == ECS_OK);
            material_record *record = record_for(materials, entity);
            record->terrain_source = source;
            record->mask = xmalloc(sizeof *record->mask);
            *record->mask = material_mask_from_field(w, h, cave.cell_size, chunk);
        }
    }
    free(cells);
    record_for(materials, source)->cave = true;
    return NULL;
}

static const char *generate_caves(game_materials *materials)
{
    ecs_component_id id = GAME_COMPONENT_GameCaveTerrain.id;
    if (!ecs_world_component(materials->world, id)) {
        return NULL;
    }
    ecs_entity *sources = NULL, entity;
    ecs_query query;
    int result = ecs_query_begin(materials->world, &query, &id, 1);
    assert(result == ECS_OK);
    while (ecs_query_next(&query, &entity)) {
        if (!game_entities_is_retiring(materials->entities, entity)) {
            apush(sources, entity);
        }
    }
    ecs_query_end(&query);
    const char *error = NULL;
    for (int i = 0; i < acount(sources) && !error; i++) {
        error = generate_cave(materials, sources[i]);
    }
    afree(sources);
    return error;
}

static void detach(game_materials *materials, ecs_entity source, material_mask mask, fluid_vec hit, float kick)
{
    ecs_world *world = materials->world;
    /* Copy before entity growth can relocate component columns. */
    GameTransform transform = *GAME_GameTransform_get_const(world, source);
    GameCollider collider = *GAME_GameCollider_get_const(world, source);
    GameDestructible destructible = *GAME_GameDestructible_get_const(world, source);
    destructible.release_fraction = 0;
    const GameVelocity *movement = GAME_GameVelocity_get_const(world, source);
    GameVelocity velocity = movement ? *movement : (GameVelocity){ 0 };
    const GameOrientation *heading = GAME_GameOrientation_get_const(world, source);
    GameOrientation orientation = heading ? *heading : (GameOrientation){ 0 };
    const GameAppearance *look = GAME_GameAppearance_get_const(world, source);
    GameAppearance appearance = look ? *look
                                     : (GameAppearance){ .color = 0xffffffffu };
    const GameMaterial *surface = GAME_GameMaterial_get_const(world, source);
    GameMaterial material = surface ? *surface : (GameMaterial){ 0 };
    const GameShader *binding = GAME_GameShader_get_const(world, source);
    GameShader shader = binding ? *binding : (GameShader){ 0 };
    bool has_shader = binding != NULL;
    const GameSubstance *substance_ptr = GAME_GameSubstance_get_const(world, source);
    GameSubstance substance = substance_ptr ? *substance_ptr : (GameSubstance){ 0 };
    const GameReflection *reflection_ptr = GAME_GameReflection_get_const(world, source);
    GameReflection reflection = reflection_ptr ? *reflection_ptr
                                               : (GameReflection){ 0 };
    const GameFragmentGlow *glow_ptr = GAME_GameFragmentGlow_get_const(world, source);
    GameFragmentGlow glow = glow_ptr ? *glow_ptr : (GameFragmentGlow){ 0 };
    float cx = 0, cy = 0;
    for (int i = 0; i < acount(mask.rects); i++) {
        material_rect r = mask.rects[i];
        float area = r.width * r.height;
        cx += r.x * area;
        cy += r.y * area;
    }
    float area = mask.count * mask.cell_size * mask.cell_size;
    cx /= area;
    cy /= area;
    float c = cosf(orientation.radians), s = sinf(orientation.radians);
    /* Throw chips out of the source hull, rather than driving an edge chip
     * back into its parent from the projectile's surface contact. */
    float dx = c * cx - s * cy, dy = s * cx + c * cy;
    float distance = hypotf(dx, dy);
    if (distance < .001f) {
        dx = transform.x - hit.x;
        dy = transform.y - hit.y;
        distance = hypotf(dx, dy);
    }
    if (distance < .001f) {
        dx = 1;
        dy = 0;
        distance = 1;
    }
    velocity.x += dx / distance * kick;
    velocity.y += dy / distance * kick;
    ecs_entity entity = ecs_world_create_entity(world);
    add_component(world, entity, &GAME_COMPONENT_GameTransform, &transform);
    add_component(world, entity, &GAME_COMPONENT_GameVelocity, &velocity);
    add_component(world, entity, &GAME_COMPONENT_GameOrientation, &orientation);
    add_component(world, entity, &GAME_COMPONENT_GameCollider, &collider);
    add_component(world, entity, &GAME_COMPONENT_GameBody, &(GameBody){ .type = GAME_BODY_DYNAMIC });
    add_component(world, entity, &GAME_COMPONENT_GameDestructible, &destructible);
    add_component(world, entity, &GAME_COMPONENT_GameAppearance, &appearance);
    add_component(world, entity, &GAME_COMPONENT_GameMaterial, &material);
    if (substance.hardness > 0) {
        add_component(world, entity, &GAME_COMPONENT_GameSubstance, &substance);
    }
    if (reflection.strength > 0) {
        add_component(world, entity, &GAME_COMPONENT_GameReflection, &reflection);
    }
    if (has_shader) {
        add_component(world, entity, &GAME_COMPONENT_GameShader, &shader);
    }
    if (glow.duration > 0) {
        add_component(world, entity, &GAME_COMPONENT_GameFragmentGlow, &glow);
    }
    int result = ecs_world_apply_commands(world);
    assert(result == ECS_OK);
    material_record *record = record_for(materials, entity);
    record->fragment = true;
    record->initial_cells = mask.count;
    record->glow_multiplier = material.emission_multiplier;
    record->mask = xmalloc(sizeof *record->mask);
    *record->mask = mask;
    materials->fragments++;
}

void game_materials_carve(game_materials *materials, ecs_entity entity, fluid_vec point, float radius)
{
    assert(isfinite(point.x) && isfinite(point.y) && isfinite(radius) && radius >= 0);
    const material_mask *target = game_materials_mask(materials, entity);
    uint64_t terrain = target
                         ? ecs_entity_to_u64(materials->records[entity.slot].terrain_source)
                         : 0;
    if (!terrain) {
        apush(materials->cuts, ((material_cut){ entity, point, radius, 1 }));
        return;
    }
    /* A world-space blast crosses storage boundaries without clipping its disk. */
    for (int i = 0; i < acount(materials->records); i++) {
        material_record record = materials->records[i];
        if (ecs_entity_to_u64(record.terrain_source) != terrain || !record.mask ||
            !ecs_entity_is_alive(materials->world, record.entity)) {
            continue;
        }
        const GameTransform
            *p = GAME_GameTransform_get_const(materials->world, record.entity);
        float dx = fmaxf(
            0,
            fabsf(point.x - p->x) - record.mask->width * record.mask->cell_size * .5f
        );
        float dy = fmaxf(
            0,
            fabsf(point.y - p->y) - record.mask->height * record.mask->cell_size * .5f
        );
        if (dx * dx + dy * dy <= radius * radius) {
            apush(materials->cuts, ((material_cut){ record.entity, point, radius, 1 }));
        }
    }
}

static void apply_cuts(game_materials *materials)
{
    ecs_world *world = materials->world;
    for (int i = 0; i < acount(materials->cuts); i++) {
        material_cut cut = materials->cuts[i];
        if (!cut.energy || !ecs_entity_is_alive(world, cut.entity) ||
            game_entities_is_retiring(materials->entities, cut.entity)) {
            continue;
        }
        material_mask *mask = (material_mask *)game_materials_mask(materials, cut.entity);
        if (!mask) {
            continue;
        }
        GameTransform p = *GAME_GameTransform_get_const(world, cut.entity);
        const GameOrientation *heading = GAME_GameOrientation_get_const(world, cut.entity);
        float angle = heading ? heading->radians : 0, c = cosf(angle), s = sinf(angle);
        const GameSubstance *substance = GAME_GameSubstance_get_const(world, cut.entity);
        float hardness = substance ? substance->hardness : 1;
        unsigned char *before = NULL;
        if (substance && substance->retain_chips) {
            before = xmalloc((size_t)mask->width * mask->height);
            memcpy(before, mask->cells, (size_t)mask->width * mask->height);
        }
        int removed = 0;
        for (int j = i; j < acount(materials->cuts); j++) {
            material_cut *next = &materials->cuts[j];
            if (ecs_entity_to_u64(next->entity) != ecs_entity_to_u64(cut.entity)) {
                continue;
            }
            float x = next->point.x - p.x, y = next->point.y - p.y;
            float energy = next->energy * (next->blast && substance ? substance->blast_multiplier
                                                                    : 1);
            removed += material_mask_damage(mask, c * x + s * y, -s * x + c * y, next->radius, energy, hardness);
            next->energy = 0;
        }
        if (!removed) {
            free(before);
            continue;
        }
        material_mask_rebuild(mask);
        int minimum = GAME_GameDestructible_get_const(world, cut.entity)->min_fragment_cells;
        const GameAppearance *appearance = GAME_GameAppearance_get_const(world, cut.entity);
        unsigned color = appearance ? appearance->color : 0xffffffffu;
        if (before) {
            for (int j = 0; j < mask->width * mask->height; j++) {
                before[j] &= !mask->cells[j];
            }
            material_mask chip = material_mask_from_cells(mask->width, mask->height, mask->cell_size, before);
            free(before);
            int tiny;
            material_mask *chips = material_mask_split(&chip, minimum, &tiny);
            apush(chips, chip);
            for (int j = 0; j < acount(chips); j++) {
                if (chips[j].count >= minimum &&
                    materials->fragments < materials->settings.fragment_limit) {
                    removed -= chips[j].count;
                    detach(materials, cut.entity, chips[j], cut.point, cut.blast ? 70 : 35);
                } else {
                    material_mask_free(&chips[j]);
                }
            }
            afree(chips);
        }
        int discarded;
        bool terrain = ecs_entity_to_u64(
                           materials->records[cut.entity.slot].terrain_source
                       ) != 0;
        material_mask *pieces = terrain
                                  ? material_mask_split_anchored(mask, minimum, &discarded)
                                  : material_mask_split(mask, minimum, &discarded);
        for (int j = 0; j < acount(pieces); j++) {
            if (materials->fragments < materials->settings.fragment_limit) {
                detach(materials, cut.entity, pieces[j], cut.point, cut.blast ? 70 : 30);
            } else {
                discarded += pieces[j].count;
                material_mask_free(&pieces[j]);
            }
        }
        afree(pieces);
        if (mask->count < minimum) {
            discarded += mask->count;
            game_entities_request_destroy(materials->entities, cut.entity);
        }
        float fraction = GAME_GameDestructible_get_const(world, cut.entity)->release_fraction;
        material_record *record = &materials->records[cut.entity.slot];
        if (fraction > 0 && mask->count < record->initial_cells * fraction) {
            record->release = true;
        }
        dust(materials, cut.point, removed + discarded, color, mask->cell_size);
    }
    aclear(materials->cuts);
}

void game_materials_release(game_materials *materials, ecs_entity entity)
{
    // Events may arrive before the first physics boundary creates the mask.
    if (GAME_GameDestructible_get_const(materials->world, entity)) {
        record_for(materials, entity)->release = true;
    }
}

static void release_hulls(game_materials *materials)
{
    for (int i = 0; i < acount(materials->records); i++) {
        material_record *record = &materials->records[i];
        if (!record->release || !record->mask) {
            continue;
        }
        record->release = false;
        ecs_entity source = record->entity;
        if (game_entities_is_retiring(materials->entities, source)) {
            continue;
        }
        if (materials->fragments < materials->settings.fragment_limit) {
            material_mask *mask = record->mask;
            record->mask = NULL;
            GameTransform p = *GAME_GameTransform_get_const(materials->world, source);
            detach(materials, source, *mask, (fluid_vec){ p.x, p.y }, 12);
            free(mask);
        }
        game_entities_request_destroy(materials->entities, source);
    }
}

static void fade_fragments(game_materials *materials, float delta)
{
    for (int i = 0; i < acount(materials->records); i++) {
        material_record *record = &materials->records[i];
        if (!record->fragment) {
            continue;
        }
        const GameFragmentGlow
            *glow = GAME_GameFragmentGlow_get_const(materials->world, record->entity);
        GameMaterial *surface = GAME_GameMaterial_get(materials->world, record->entity);
        if (!glow || !surface || glow->duration <= 0) {
            continue;
        }
        record->glow_age = fminf(glow->duration, record->glow_age + delta);
        float remaining = 1 - record->glow_age / glow->duration;
        float phase = record->glow_age * 29 + record->entity.slot * 2.399963f;
        float pulse = fmaxf(0, sinf(phase) * sinf(phase * .37f));
        float power = remaining * remaining * (1 - glow->flicker * pulse);
        surface->emission_multiplier = record->glow_multiplier * power;
    }
}

const char *game_materials_prepare(game_materials *materials)
{
    const char *error = configure(materials);
    if (error || !materials->fluid) {
        return error;
    }
    for (int i = 0; i < acount(materials->records); i++) {
        material_record *record = &materials->records[i];
        if (ecs_entity_to_u64(record->terrain_source) &&
            !GAME_GameCaveTerrain_get_const(materials->world, record->terrain_source) &&
            ecs_entity_is_alive(materials->world, record->entity)) {
            int result = ecs_world_destroy_entity(materials->world, record->entity);
            assert(result == ECS_OK);
        }
        if (!ecs_entity_is_alive(materials->world, record->entity)) {
            materials->fragments -= record->fragment;
            release_record(record);
            continue;
        }
        if (record->mask &&
            !GAME_GameDestructible_get_const(materials->world, record->entity)) {
            materials->fragments -= record->fragment;
            release_record(record);
        }
        if (record->cave &&
            !GAME_GameCaveTerrain_get_const(materials->world, record->entity)) {
            record->cave = false;
        }
    }
    error = generate_caves(materials);
    if (error) {
        return error;
    }
    ecs_component_id ids[] = { GAME_COMPONENT_GameDestructible.id,
                               GAME_COMPONENT_GameFluidVolume.id };
    for (int i = 0; i < 2; i++) {
        if (!ecs_world_component(materials->world, ids[i])) {
            continue;
        }
        ecs_query query;
        int result = ecs_query_begin(materials->world, &query, &ids[i], 1);
        assert(result == ECS_OK);
        ecs_entity entity;
        while (ecs_query_next(&query, &entity)) {
            if (game_entities_is_retiring(materials->entities, entity)) {
                continue;
            }
            error = i == 0 ? create_mask(materials, entity)
                           : fill_volume(materials, entity);
            if (error) {
                break;
            }
        }
        ecs_query_end(&query);
        if (error) {
            return error;
        }
    }
    apply_cuts(materials);
    release_hulls(materials);
    return NULL;
}

static void camera_regions(game_materials *materials)
{
    aclear(materials->regions);
    ecs_component_id ids[] = { GAME_COMPONENT_GameCamera.id,
                               GAME_COMPONENT_GameTransform.id };
    if (!ecs_world_component(materials->world, ids[0]) ||
        !ecs_world_component(materials->world, ids[1])) {
        return;
    }
    ecs_query query;
    int result = ecs_query_begin(materials->world, &query, ids, 2);
    assert(result == ECS_OK);
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const GameCamera *camera = GAME_GameCamera_get_const(materials->world, entity);
        if (camera->disabled) {
            continue;
        }
        const GameTransform *position = GAME_GameTransform_get_const(materials->world, entity);
        float buffer = materials->settings.buffer;
        apush(
            materials->regions,
            ((fluid_region){ position->x + camera->offset.x - camera->width * .5f - buffer,
                             position->y + camera->offset.y - camera->height * .5f - buffer,
                             camera->width + 2 * buffer,
                             camera->height + 2 * buffer })
        );
    }
    ecs_query_end(&query);
}

static void blasts(
    game_materials *materials,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
)
{
    int count;
    const fluid_blast *events = material_fluid_blasts(materials->fluid, &count);
    GameCombustion settings = materials->combustion;
    for (int e = 0; e < count; e++) {
        fluid_blast event = events[e];
        float density = fminf(1, event.ignitions / 8.f);
        float radius = settings.blast_radius * (.35f + .65f * density);
        if (radius <= 0) {
            continue;
        }
        material_fluid_push(
            materials->fluid,
            event.position,
            radius,
            settings.blast_speed * density,
            bodies,
            obstacles,
            obstacle_count
        );
        for (int i = 0; i < body_count; i++) {
            fluid_body *b = &bodies[i];
            float dx = b->position.x - event.position.x,
                  dy = b->position.y - event.position.y;
            float distance = hypotf(dx, dy);
            if (b->inverse_mass <= 0 || distance < .001f || distance >= radius ||
                material_fluid_blocked(event.position, b->position, bodies, obstacles, obstacle_count, i)) {
                continue;
            }
            float impulse = settings.blast_speed * density *
                            (1 - distance / radius) / b->inverse_mass;
            b->impulse.x += dx / distance * impulse;
            b->impulse.y += dy / distance * impulse;
            ecs_entity entity = ecs_entity_from_u64(b->owner);
            GameHealth *health = GAME_GameHealth_get(materials->world, entity);
            if (health && !game_materials_mask(materials, entity)) {
                int damage = (int)ceilf(
                    settings.blast_damage * density * (1 - distance / radius)
                );
                health->current = health->current > damage ? health->current - damage
                                                           : 0;
                if (!health->current) {
                    game_entities_request_destroy(materials->entities, entity);
                }
            }
        }
        /* Trace spokes to the first surviving cell. A blast chips the facing
         * wall but cannot damage a second wall/body through it in this tick. */
        for (int spoke = 0; spoke < 24; spoke++) {
            float angle = spoke * (6.2831853f / 24), dx = cosf(angle),
                  dy = sinf(angle);
            bool hit = false;
            for (float distance = 2; distance <= radius && !hit; distance += 2) {
                fluid_vec point = { event.position.x + dx * distance,
                                    event.position.y + dy * distance };
                for (int i = 0; i < acount(materials->records); i++) {
                    material_record record = materials->records[i];
                    if (!record.mask ||
                        !ecs_entity_is_alive(materials->world, record.entity)) {
                        continue;
                    }
                    const GameTransform *p = GAME_GameTransform_get_const(
                        materials->world,
                        record.entity
                    );
                    const GameOrientation *o = GAME_GameOrientation_get_const(
                        materials->world,
                        record.entity
                    );
                    float c = o ? cosf(o->radians) : 1, s = o ? sinf(o->radians) : 0;
                    float x = point.x - p->x, y = point.y - p->y;
                    if (!material_mask_contains(record.mask, c * x + s * y, -s * x + c * y)) {
                        continue;
                    }
                    apush(
                        materials->cuts,
                        ((material_cut){ record.entity,
                                         point,
                                         5,
                                         settings.blast_damage * density *
                                             (1 - distance / radius),
                                         true })
                    );
                    hit = true;
                    break;
                }
            }
        }
    }
}

void game_materials_tick(
    game_materials *materials,
    float delta,
    fluid_body *bodies,
    int body_count,
    const fluid_obstacle *obstacles,
    int obstacle_count
)
{
    if (!materials->fluid || delta <= 0) {
        return;
    }
    camera_regions(materials);
    fade_fragments(materials, delta);
    float buffer = materials->settings.buffer;
    for (int i = 0; i < body_count; i++) {
        fluid_body b = bodies[i];
        if (b.velocity.x * b.velocity.x + b.velocity.y * b.velocity.y > 1) {
            apush(
                materials->regions,
                ((fluid_region){ b.position.x - buffer, b.position.y - buffer, 2 * buffer, 2 * buffer })
            );
        }
    }
    game_task_pool_begin(materials->pool, true);
    material_fluid_tick(
        materials->fluid,
        delta,
        materials->settings.chunk_size,
        materials->regions,
        acount(materials->regions),
        bodies,
        body_count,
        obstacles,
        obstacle_count
    );
    game_task_pool_end(materials->pool);
    CHIBI_TRACE_ZONE_BEGIN(blast_zone, "Material blast effects");
    blasts(materials, bodies, body_count, obstacles, obstacle_count);
    CHIBI_TRACE_ZONE_END(blast_zone);
}

static int carve_node(node *, flow_context *ctx)
{
    game_entities *entities = flow_context_service(ctx, "game.entities");
    game_materials *materials = entities ? game_entities_materials(entities) : NULL;
    if (!materials) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Carve Material requires a live world");
    }
    pin_value target, point, radius;
    int result = flow_context_eval_data(ctx, 1, &target);
    if (result >= 0) {
        result = flow_context_eval_data(ctx, 2, &point);
    }
    if (result >= 0) {
        result = flow_context_eval_data(ctx, 3, &radius);
    }
    if (result < 0) {
        return result;
    }
    CF_V2 position;
    if (!point.ptr) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Carve Material needs a position");
    }
    memcpy(&position, point.ptr, sizeof position);
    if (!isfinite(position.x + position.y + radius.f) || radius.f < 0) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Carve Material needs a finite position and nonnegative radius");
    }
    game_materials_carve(
        materials,
        ecs_entity_from_u64(target.u64),
        (fluid_vec){ position.x, position.y },
        radius.f
    );
    return flow_context_continue(ctx, 0);
}

static pin_spec CARVE_IN[] = {
    { .name = "In", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Entity", .type = PIN_HANDLE, .type_name = "ecs.entity" },
    { .name = "Point", .type = PIN_STRUCT, .type_name = "CF_V2" },
    { .name = "Radius", .type = PIN_F32 },
};
static pin_spec CARVE_OUT[] = { { .name = "Out", .type = PIN_FLOW, .kind = PIN_EXEC } };
static const node_access CARVE_ACCESS = { .flags = NODE_EFFECT_STRUCTURAL };
node_desc GAME_NODE_CarveMaterial = {
    .name = "Carve Material",
    .category = "Physics",
    .input_count = 4,
    .inputs = CARVE_IN,
    .output_count = 1,
    .outputs = CARVE_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = carve_node,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &CARVE_ACCESS,
};

static int release_node(node *, flow_context *ctx)
{
    game_entities *entities = flow_context_service(ctx, "game.entities");
    game_materials *materials = entities ? game_entities_materials(entities) : NULL;
    if (!materials) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Release Material requires a live world");
    }
    pin_value target;
    int result = flow_context_eval_data(ctx, 1, &target);
    if (result < 0) {
        return result;
    }
    game_materials_release(materials, ecs_entity_from_u64(target.u64));
    return flow_context_continue(ctx, 0);
}

static const char *fragment_node(const eval_context *ctx, const pin_value *inputs, arena *, pin_value *out)
{
    game_entities *entities = eval_context_service(ctx, "game.entities");
    game_materials *materials = entities ? game_entities_materials(entities) : NULL;
    if (!materials) {
        return "Is Material Fragment requires a live world";
    }
    out[0] = (pin_value){
        .type = PIN_BOOL,
        .b = game_materials_is_fragment(materials, ecs_entity_from_u64(inputs[0].u64))
    };
    return NULL;
}

static pin_spec FRAGMENT_OUT[] = { { .name = "Fragment", .type = PIN_BOOL } };
static const node_access FRAGMENT_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
node_desc GAME_NODE_IsMaterialFragment = {
    .name = "Is Material Fragment",
    .category = "Physics",
    .input_count = 1,
    .inputs = CARVE_IN + 1,
    .output_count = 1,
    .outputs = FRAGMENT_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = fragment_node,
    .access = &FRAGMENT_ACCESS,
};

node_desc GAME_NODE_ReleaseMaterial = {
    .name = "Release Material",
    .category = "Physics",
    .input_count = 2,
    .inputs = CARVE_IN,
    .output_count = 1,
    .outputs = CARVE_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = release_node,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &CARVE_ACCESS,
};
