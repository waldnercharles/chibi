#include "runtime/material_ecs.h"

#include "diagnostics/tracy.h"
#include "render/particles.h"
#include "runtime/behaviors/entities.h"
#include "runtime/generated.h"
#include "runtime/material_destruction.h"
#include "runtime/material_terrain.h"
#include "runtime/task_pool.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

game_materials *game_materials_new(ecs_world *world, game_entities *entities, game_particles *particles)
{
    game_materials *materials = xcalloc(1, sizeof *materials);
    materials->world = world;
    materials->entities = entities;
    materials->particles = particles;
    materials->pool = game_task_pool_acquire();
    materials->destruction = material_destruction_new();
    materials->fields = material_fields_new();
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
    material_destruction_reset(materials->destruction);
    aclear(materials->damage_requests);
    aclear(materials->regions);
    materials->fragments = 0;
    if (materials->fluid) {
        material_fluid_free(materials->fluid);
        materials->fluid = NULL;
    }
    materials->settings = (MaterialFluidWorld){ 0 };
}

void game_materials_free(game_materials *materials)
{
    game_materials_reset(materials);
    afree(materials->records);
    material_destruction_free(materials->destruction);
    afree(materials->damage_requests);
    material_fields_free(materials->fields);
    afree(materials->regions);
    game_task_pool_release(materials->pool);
    free(materials);
}

bool game_materials_enabled(const game_materials *materials)
{
    return materials && (materials->fluid || acount(materials->records));
}

const material_fluid *game_materials_fluid(const game_materials *materials)
{
    return materials ? materials->fluid : NULL;
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
        *particles = materials->fluid
                       ? material_fluid_particles(materials->fluid, &state.particles)
                       : NULL;
    state.active_particles = materials->fluid
                               ? material_fluid_active_count(materials->fluid)
                               : 0;
    uint64_t flow_hash = materials->fluid ? material_fluid_flow_hash(materials->fluid)
                                          : 0;
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
    for (int i = 0; i < acount(materials->damage_requests); i++) {
        material_damage d = materials->damage_requests[i];
        float values[] = { d.point.x, d.point.y, d.radius, d.energy };
        state.hash = hash_bytes(state.hash, &d.target, sizeof d.target);
        state.hash = hash_bytes(state.hash, &d.cause, sizeof d.cause);
        state.hash = hash_bytes(state.hash, values, sizeof values);
    }
    state.hash = material_destruction_hash(materials->destruction, state.hash);
    return state;
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

bool game_materials_is_pierceable_fragment(const game_materials *materials, ecs_entity entity)
{
    if (!game_materials_is_fragment(materials, entity)) {
        return false;
    }
    return GAME_GameDestructible_get_const(materials->world, entity)->pierceable_fragments;
}

material_record *material_ecs_record(game_materials *materials, ecs_entity entity)
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
    materials->fragment_limit = 0;
    ecs_component_id destruction_id = GAME_COMPONENT_MaterialDestructionWorld.id;
    if (ecs_world_component(materials->world, destruction_id)) {
        ecs_query query;
        int result = ecs_query_begin(materials->world, &query, &destruction_id, 1);
        assert(result == ECS_OK);
        ecs_entity entity;
        int count = 0;
        while (ecs_query_next(&query, &entity)) {
            materials->fragment_limit = GAME_MaterialDestructionWorld_get_const(
                                            materials->world,
                                            entity
            )
                                            ->fragment_limit;
            count++;
        }
        ecs_query_end(&query);
        if (count > 1 || materials->fragment_limit < 0 ||
            materials->fragment_limit > 1024) {
            return "Destruction world requires one configuration and 0..1024 "
                   "fragments";
        }
    }
    return material_ecs_configure_fluid(materials);
}

static const char *create_mask(game_materials *materials, ecs_entity entity)
{
    const MaterialBreakProfile
        *profile = GAME_MaterialBreakProfile_get_const(materials->world, entity);
    if (profile) {
        float values[] = {
            profile->chip_speed,        profile->fragment_speed,
            profile->blast_speed,       profile->release_speed,
            profile->dust_speed_min,    profile->dust_speed_max,
            profile->dust_lifetime_min, profile->dust_lifetime_max,
            profile->dust_size_min,     profile->dust_size_max
        };
        for (int i = 0; i < (int)(sizeof values / sizeof *values); i++) {
            if (!isfinite(values[i]) || values[i] < 0) {
                return "Break profile values must be finite and nonnegative";
            }
        }
        if (profile->dust_limit < 0 || profile->dust_limit > 1024 ||
            profile->dust_speed_min > profile->dust_speed_max ||
            profile->dust_lifetime_min > profile->dust_lifetime_max ||
            profile->dust_size_min > profile->dust_size_max) {
            return "Break profile requires ordered ranges and 0..1024 dust "
                   "particles";
        }
    }
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
    material_record *record = material_ecs_record(materials, entity);
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

static MaterialBreakProfile break_profile(game_materials *materials, ecs_entity entity)
{
    const MaterialBreakProfile
        *profile = GAME_MaterialBreakProfile_get_const(materials->world, entity);
    return profile ? *profile : (MaterialBreakProfile){ 0 };
}

static void dust(
    game_materials *materials,
    fluid_vec point,
    int cells,
    unsigned color,
    float cell_size,
    MaterialBreakProfile profile
)
{
    if (!cells || !materials->particles || profile.dust_limit <= 0) {
        return;
    }
    /* Unretained cells are a short local dust puff. Bright impact sparks belong
     * to authored emitters; ordinary excavation must not become an explosion. */
    game_particle_spray spray = { .position = { point.x, point.y },
                                  .direction = { 1, 0 },
                                  .color = color,
                                  .end_color = color & 0xffffff00u,
                                  .count = cells < profile.dust_limit ? cells
                                                                      : profile.dust_limit,
                                  .speed_min = cell_size * profile.dust_speed_min,
                                  .speed_max = cell_size * profile.dust_speed_max,
                                  .lifetime_min = profile.dust_lifetime_min,
                                  .lifetime_max = profile.dust_lifetime_max,
                                  .size_min = cell_size * profile.dust_size_min,
                                  .size_max = cell_size * profile.dust_size_max,
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

static const char *generate_terrain(game_materials *materials, ecs_entity source)
{
    ecs_world *world = materials->world;
    MaterialTerrain terrain = *GAME_MaterialTerrain_get_const(world, source);
    const GameTransform *position = GAME_GameTransform_get_const(world, source);
    if (!position || !isfinite(position->x) || !isfinite(position->y) ||
        !isfinite(terrain.width) || !isfinite(terrain.height) ||
        !isfinite(terrain.cell_size) || terrain.cell_size <= 0 ||
        terrain.width < terrain.cell_size || terrain.height < terrain.cell_size ||
        terrain.width / terrain.cell_size > 4096 ||
        terrain.height / terrain.cell_size > 4096 || terrain.min_fragment_cells < 1 ||
        terrain.border_cells < 0 || terrain.border_cells > 2048 ||
        terrain.connectivity < MATERIAL_CONNECTIVITY_ALL ||
        terrain.connectivity > MATERIAL_CONNECTIVITY_FIRST_AIR) {
        return "Terrain requires finite bounds, 1..4096 cells per axis, "
               "positive cell size and fragment minimum, and valid "
               "connectivity";
    }
    unsigned revision = material_field_revision(materials->fields, terrain.field);
    if (!revision) {
        return "Terrain references an unavailable procedural field";
    }
    float dimensions[] = { position->x,
                           position->y,
                           terrain.width,
                           terrain.height,
                           terrain.cell_size };
    unsigned settings[] = { terrain.seed,
                            terrain.category,
                            terrain.mask,
                            (unsigned)terrain.min_fragment_cells,
                            (unsigned)terrain.connectivity,
                            (unsigned)terrain.border_cells,
                            revision };
    uint64_t hash = hash_bytes(UINT64_C(14695981039346656037), dimensions, sizeof dimensions);
    hash = hash_bytes(hash, settings, sizeof settings);
    hash = hash_bytes(hash, terrain.field, strlen(terrain.field));
    if (terrain.parameters) {
        hash = hash_bytes(hash, terrain.parameters, strlen(terrain.parameters));
    }
    if (material_ecs_record(materials, source)->terrain_hash == hash) {
        return NULL;
    }
    GameTransform origin = *position;
    field_grid grid = { (int)ceilf(terrain.width / terrain.cell_size),
                        (int)ceilf(terrain.height / terrain.cell_size),
                        terrain.cell_size };
    float *field = xmalloc((size_t)grid.width * grid.height * sizeof(float));
    if (!material_field_bake(
            materials->fields,
            terrain.field,
            terrain.parameters,
            terrain.seed,
            grid,
            field,
            materials->error,
            sizeof materials->error
        )) {
        free(field);
        return materials->error;
    }
    material_terrain_connect(field, grid, terrain.connectivity, terrain.border_cells);
    material_terrain_chunk *chunks = material_terrain_chunks(field, grid);
    free(field);
    for (int i = 0; i < acount(materials->records); i++) {
        material_record *record = &materials->records[i];
        if (ecs_entity_to_u64(record->terrain_source) != ecs_entity_to_u64(source)) {
            continue;
        }
        if (ecs_entity_is_alive(world, record->entity)) {
            int result = ecs_world_destroy_entity(world, record->entity);
            assert(result == ECS_OK);
        }
        materials->fragments -= record->fragment;
        release_record(record);
    }
    /* Snapshot the controlled component bundle before entity growth. */
    const GameAppearance *look = GAME_GameAppearance_get_const(world, source);
    GameAppearance appearance = look ? *look
                                     : (GameAppearance){ .color = 0xffffffffu };
    const GameMaterial *surface = GAME_GameMaterial_get_const(world, source);
    GameMaterial material = surface ? *surface : (GameMaterial){ 0 };
    const GameShader *binding = GAME_GameShader_get_const(world, source);
    bool has_shader = binding != NULL;
    GameShader shader = binding ? *binding : (GameShader){ 0 };
    const GameSubstance *sub = GAME_GameSubstance_get_const(world, source);
    GameSubstance substance = sub ? *sub : (GameSubstance){ 0 };
    const GameReflection *ref = GAME_GameReflection_get_const(world, source);
    GameReflection reflection = ref ? *ref : (GameReflection){ 0 };
    MaterialBreakProfile profile = break_profile(materials, source);
    for (int i = 0; i < acount(chunks); i++) {
        material_terrain_chunk *chunk = &chunks[i];
        ecs_entity entity = ecs_world_create_entity(world);
        GameTransform transform = { origin.x + chunk->x, origin.y + chunk->y };
        GameCollider collider = { .shape = GAME_COLLIDER_BOX,
                                  .width = chunk->mask.width * terrain.cell_size,
                                  .height = chunk->mask.height * terrain.cell_size,
                                  .category = terrain.category,
                                  .mask = terrain.mask };
        GameDestructible destructible = { terrain.cell_size, terrain.min_fragment_cells };
        appearance.width = collider.width;
        appearance.height = collider.height;
        add_component(world, entity, &GAME_COMPONENT_GameTransform, &transform);
        add_component(world, entity, &GAME_COMPONENT_GameCollider, &collider);
        add_component(world, entity, &GAME_COMPONENT_GameDestructible, &destructible);
        add_component(world, entity, &GAME_COMPONENT_GameAppearance, &appearance);
        add_component(world, entity, &GAME_COMPONENT_GameMaterial, &material);
        add_component(world, entity, &GAME_COMPONENT_MaterialBreakProfile, &profile);
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
        material_record *record = material_ecs_record(materials, entity);
        record->terrain_source = source;
        record->mask = xmalloc(sizeof *record->mask);
        *record->mask = chunk->mask;
        chunk->mask = (material_mask){ 0 };
        record->initial_cells = record->mask->count;
    }
    material_terrain_chunks_free(chunks);
    material_record *record = material_ecs_record(materials, source);
    record->terrain = true;
    record->terrain_hash = hash;
    return NULL;
}

static const char *generate_terrains(game_materials *materials)
{
    ecs_component_id id = GAME_COMPONENT_MaterialTerrain.id;
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
        error = generate_terrain(materials, sources[i]);
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
    MaterialBreakProfile profile = break_profile(materials, source);
    add_component(world, entity, &GAME_COMPONENT_MaterialBreakProfile, &profile);
    int result = ecs_world_apply_commands(world);
    assert(result == ECS_OK);
    material_record *record = material_ecs_record(materials, entity);
    record->fragment = true;
    record->terrain_source = materials->records[source.slot].terrain_source;
    record->initial_cells = mask.count;
    record->glow_multiplier = material.emission_multiplier;
    record->mask = xmalloc(sizeof *record->mask);
    *record->mask = mask;
    materials->fragments++;
}

void game_materials_damage(
    game_materials *materials,
    ecs_entity entity,
    fluid_vec point,
    float radius,
    float energy,
    int cause
)
{
    assert(isfinite(point.x) && isfinite(point.y) && isfinite(radius) && radius >= 0);
    assert(isfinite(energy) && energy >= 0);
    apush(
        materials->damage_requests,
        ((material_damage){ ecs_entity_to_u64(entity), { point.x, point.y }, radius, energy, cause })
    );
}

static void queue_damage(
    game_materials *materials,
    ecs_entity entity,
    fluid_vec point,
    float radius,
    float energy,
    int cause
)
{
    assert(isfinite(point.x) && isfinite(point.y) && isfinite(radius) && radius >= 0);
    const material_mask *target = game_materials_mask(materials, entity);
    uint64_t terrain = target && !materials->records[entity.slot].fragment
                         ? ecs_entity_to_u64(materials->records[entity.slot].terrain_source)
                         : 0;
    if (GAME_MaterialTerrain_get_const(materials->world, entity)) {
        terrain = ecs_entity_to_u64(entity);
    }
    if (!terrain) {
        material_destruction_queue_damage(
            materials->destruction,
            (material_damage){ ecs_entity_to_u64(entity), { point.x, point.y }, radius, energy, cause }
        );
        return;
    }
    /* A world-space blast crosses storage boundaries without clipping its disk. */
    for (int i = 0; i < acount(materials->records); i++) {
        material_record record = materials->records[i];
        if (record.fragment || ecs_entity_to_u64(record.terrain_source) != terrain ||
            !record.mask || !ecs_entity_is_alive(materials->world, record.entity)) {
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
            material_destruction_queue_damage(
                materials->destruction,
                (material_damage){ ecs_entity_to_u64(record.entity),
                                   { point.x, point.y },
                                   radius,
                                   energy,
                                   cause }
            );
        }
    }
}

static bool read_source(void *user, uint64_t target, material_source *out)
{
    game_materials *materials = user;
    ecs_entity entity = ecs_entity_from_u64(target);
    const material_mask *mask = game_materials_mask(materials, entity);
    if (!mask || !mask->count || !ecs_entity_is_alive(materials->world, entity) ||
        game_entities_is_retiring(materials->entities, entity)) {
        return false;
    }
    const GameDestructible *style = GAME_GameDestructible_get_const(materials->world, entity);
    const GameSubstance *substance = GAME_GameSubstance_get_const(materials->world, entity);
    const GameOrientation *heading = GAME_GameOrientation_get_const(materials->world, entity);
    GameTransform p = *GAME_GameTransform_get_const(materials->world, entity);
    material_record *record = &materials->records[entity.slot];
    MaterialBreakProfile profile = break_profile(materials, entity);
    *out = (material_source){
        .mask = record->mask,
        .position = { p.x, p.y },
        .angle = heading ? heading->radians : 0,
        .hardness = substance ? substance->hardness : 1,
        .blast_multiplier = substance ? substance->blast_multiplier : 1,
        .release_fraction = style->release_fraction,
        .initial_cells = record->initial_cells,
        .minimum = style->min_fragment_cells,
        .retain_chips = substance && substance->retain_chips,
        .anchored = !record->fragment && ecs_entity_to_u64(record->terrain_source) != 0,
        .chip_speed = profile.chip_speed,
        .fragment_speed = profile.fragment_speed,
        .blast_speed = profile.blast_speed,
        .release_speed = profile.release_speed
    };
    return true;
}

static void create_fragment(void *user, uint64_t source, material_fragment fragment)
{
    game_materials *materials = user;
    material_record *record = &materials->records[ecs_entity_from_u64(source).slot];
    if (record->mask && !record->mask->width) {
        free(record->mask);
        record->mask = NULL;
    }
    detach(
        user,
        ecs_entity_from_u64(source),
        fragment.mask,
        (fluid_vec){ fragment.hit.x, fragment.hit.y },
        fragment.speed
    );
}

static void destroy_source(void *user, uint64_t target)
{
    game_materials *materials = user;
    game_entities_request_destroy(materials->entities, ecs_entity_from_u64(target));
}

/* Physics and render adapters consume mask revision at this boundary. */
static void invalidate_geometry(void *, uint64_t) {}

static void material_result(void *user, const material_event *event)
{
    if (event->kind != MATERIAL_EVENT_DAMAGE) {
        return;
    }
    game_materials *materials = user;
    ecs_entity entity = ecs_entity_from_u64(event->target);
    const GameAppearance *appearance = GAME_GameAppearance_get_const(materials->world, entity);
    const material_mask *mask = game_materials_mask(materials, entity);
    dust(
        materials,
        (fluid_vec){ event->point.x, event->point.y },
        event->dust,
        appearance ? appearance->color : 0xffffffffu,
        mask->cell_size,
        break_profile(materials, entity)
    );
}

void game_materials_release(game_materials *materials, ecs_entity entity)
{
    material_destruction_queue_release(materials->destruction, ecs_entity_to_u64(entity));
}

void material_ecs_fade_fragments(game_materials *materials, float delta)
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
    if (error) {
        return error;
    }
    for (int i = 0; i < acount(materials->records); i++) {
        material_record *record = &materials->records[i];
        if (ecs_entity_to_u64(record->terrain_source) &&
            !GAME_MaterialTerrain_get_const(materials->world, record->terrain_source) &&
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
        if (record->terrain &&
            !GAME_MaterialTerrain_get_const(materials->world, record->entity)) {
            record->terrain = false;
        }
    }
    error = generate_terrains(materials);
    if (error) {
        return error;
    }
    ecs_component_id ids[] = { GAME_COMPONENT_GameDestructible.id,
                               GAME_COMPONENT_GameFluidVolume.id };
    for (int i = 0; i < (materials->fluid ? 2 : 1); i++) {
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
                           : material_ecs_fill_volume(materials, entity);
            if (error) {
                break;
            }
        }
        ecs_query_end(&query);
        if (error) {
            return error;
        }
    }
    /* Resolve authored sources after baking, including damage queued by Begin. */
    for (int i = 0; i < acount(materials->damage_requests); i++) {
        material_damage d = materials->damage_requests[i];
        queue_damage(
            materials,
            ecs_entity_from_u64(d.target),
            (fluid_vec){ d.point.x, d.point.y },
            d.radius,
            d.energy,
            d.cause
        );
    }
    aclear(materials->damage_requests);
    material_host host = { materials,      read_source,         create_fragment,
                           destroy_source, invalidate_geometry, material_result };
    material_destruction_step(
        materials->destruction,
        &host,
        materials->fragment_limit > materials->fragments
            ? materials->fragment_limit - materials->fragments
            : 0
    );
    return NULL;
}

int game_materials_dispatch_events(game_materials *materials)
{
    material_event event;
    while (material_destruction_poll_event(materials->destruction, &event)) {
        CF_V2 point = { event.point.x, event.point.y };
        ecs_entity target = ecs_entity_from_u64(event.target);
        pin_value values[] = {
            { .type = PIN_F32, .f = event.energy },
            { .type = PIN_I32, .i = event.cause },
            { .type = PIN_STRUCT, .type_name = "CF_V2", .ptr = &point },
            { .type = PIN_I32, .i = event.kind },
            { .type = PIN_I32, .i = event.removed },
            { .type = PIN_I32, .i = event.dust },
            { .type = PIN_HANDLE, .type_name = "ecs.entity", .u64 = event.target }
        };
        bool impact = event.kind == MATERIAL_EVENT_BODY_IMPACT;
        if (!impact && target.slot < (unsigned)acount(materials->records)) {
            material_record *record = &materials->records[target.slot];
            if (ecs_entity_to_u64(record->entity) == event.target &&
                record->terrain_source.slot) {
                target = record->terrain_source;
            }
        }
        int result = game_entities_request_event(
            materials->entities,
            target,
            impact ? "Material Impact" : "Material Result",
            (flow_inputs){ values, impact ? 3 : 7 }
        );
        if (result < 0) {
            return result;
        }
    }
    return 0;
}

material_fields *game_materials_fields(game_materials *materials)
{
    return materials->fields;
}
