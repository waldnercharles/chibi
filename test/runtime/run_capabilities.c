#include "graph/runtime/random.h"
#include "runtime/camera.h"
#include "runtime/camera_shake.h"
#include "runtime/material_terrain.h"
#include "support/text_file.h"
#include "support/value_noise.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <math.h>
#include <time.h>

/* A headless world running authored behaviors through the normal engine. */
typedef struct
{
    ecs_world *world;
    game_live_world *live;
    engine *runtime;
    double time;
} run_fixture;

static run_fixture fixture_new()
{
    run_fixture f = { .world = ecs_world_new() };
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(f.world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    f.live = game_live_world_new(f.world);
    f.runtime = engine_new(f.world, f.live);
    return f;
}

static void fixture_free(run_fixture *f)
{
    engine_free(f->runtime);
    game_live_world_free(f->live);
    ecs_world_free(f->world);
}

static int fixture_attach(ecs_world *, ecs_entity entity, const char *name, void *user)
{
    return game_behavior_attach(user, entity, name);
}

/* Spawns a prototype given as JSON, attaching its behaviors. */
static ecs_entity fixture_spawn(run_fixture *f, const char *json)
{
    game_prototype_registry registry = { GAME_REGISTRY_COMPONENTS,
                                         GAME_REGISTRY_COMPONENT_COUNT };
    char error[256];
    game_prototype *prototype = game_prototype_load(json, &registry, error, sizeof error);
    if (!prototype) {
        fprintf(stderr, "%s\n", error);
    }
    assert(prototype);
    ecs_entity entity;
    int result = game_prototype_spawn(
        prototype,
        f->world,
        &(game_prototype_spawn_hooks){ fixture_attach, f->live->behaviors },
        &entity,
        error,
        sizeof error
    );
    if (result != 0) {
        fprintf(stderr, "%s\n", error);
    }
    assert(result == 0);
    game_prototype_free(prototype);
    return entity;
}

static void fixture_behavior(run_fixture *f, behavior_asset *asset)
{
    compile_report report;
    compile_report_init(&report);
    int compiled = behavior_asset_compile(asset, &report);
    for (int i = 0; i < acount(report.items); i++) {
        fprintf(stderr, "%s: %s\n", asset->name, report.items[i].message);
    }
    assert(compiled == 0 && !report.error_count);
    compile_report_free(&report);
    assert(game_live_add_definition(f->live, game_live_definition_new(asset->name, asset)) == 0);
}

static void fixture_frame(run_fixture *f)
{
    int result = engine_frame(f->runtime, f->time);
    if (result < 0) {
        fprintf(stderr, "%s\n", engine_error(f->runtime));
    }
    assert(result == FLOW_OK);
}

/* Runs `steps` fixed steps, one per frame; the first frame only starts the clock. */
static void fixture_steps(run_fixture *f, int steps)
{
    if (f->time == 0) {
        fixture_frame(f);
    }
    for (int i = 0; i < steps; i++) {
        f->time += ENGINE_FIXED_SECONDS;
        fixture_frame(f);
    }
}

static node *add(graph *g, const char *name)
{
    node *n = graph_add_node(g, registry_node_by_name(name));
    if (!n) {
        fprintf(stderr, "no node %s\n", name);
    }
    assert(n);
    return n;
}

static node *event(graph *g, const char *name)
{
    node *n = add(g, "Event");
    assert(graph_set_event_name(g, n, name) == 0);
    return n;
}

static void flow(graph *g, node *from, node *to)
{
    assert(graph_connect_exec(g, from, 0, to, 0, 0) == 0);
}

/* Converts between numeric pins as the editor does. */
static void wire(graph *g, node *from, int output, node *to, int input)
{
    const node_desc *conversion = conversion_for_pins(&from->output_pins[output], &to->input_pins[input]);
    if (graph_connect_data_conversion(g, from, output, to, input, conversion) != 0) {
        fprintf(
            stderr,
            "cannot wire %s.%d to %s.%d\n",
            from->desc->name,
            output,
            to->desc->name,
            input
        );
        assert(false);
    }
}

static void set_input(graph *g, node *n, int input, pin_value value)
{
    assert(graph_set_pin_default(g, n, 0, input, &value) == 0);
}

static behavior_asset *behavior(const char *name)
{
    behavior_asset *asset = behavior_asset_new(name);
    assert(behavior_asset_set_owner_type(asset, "ecs.entity") == 0);
    return asset;
}

/* Tick writes (Random Real, Random Range 0..10) to Transform; Begin reseeds
 * with Seed when it is nonnegative. */
static behavior_asset *roll_behavior(int seed)
{
    behavior_asset *asset = behavior("Roll");
    graph *g = asset->graph;
    node *set = add(g, "Set_GameTransform");
    node *make = add(g, "Make_CF_V2");
    node *range = add(g, "Random Range");
    set_input(g, range, 1, (pin_value){ .type = PIN_F64, .d = 10 });
    flow(g, event(g, "Tick"), set);
    wire(g, add(g, "Random Real"), 0, make, 0);
    wire(g, range, 0, make, 1);
    wire(g, make, 0, set, 1);
    if (seed >= 0) {
        node *reseed = add(g, "Set Random Seed");
        set_input(g, reseed, 1, (pin_value){ .type = PIN_I32, .i = seed });
        flow(g, event(g, "Begin"), reseed);
    }
    return asset;
}

static const char
    ROLLER[] = "{\"name\":\"Roller\",\"behaviors\":[\"Roll\"],"
               "\"components\":{\"GameTransform\":{\"x\":0,\"y\":0}}}";

static CF_V2 roll(uint64_t world_seed, int seed, int steps)
{
    run_fixture f = fixture_new();
    game_behavior_seed_random(f.live->behaviors, world_seed);
    fixture_behavior(&f, roll_behavior(seed));
    ecs_entity roller = fixture_spawn(&f, ROLLER);
    fixture_steps(&f, steps);
    CF_V2 result = *GAME_GameTransform_get_const(f.world, roller);
    fixture_free(&f);
    return result;
}

static void seeded_randomness()
{
    CF_V2 a = roll(7, -1, 5), b = roll(7, -1, 5), c = roll(8, -1, 5);
    assert(a.x == b.x && a.y == b.y && (a.x != c.x || a.y != c.y));
    assert(a.x >= 0 && a.x < 1 && a.y >= 0 && a.y <= 10);
    /* Set Random Seed restarts the world's stream; Begin runs before Tick. */
    behavior_random expected;
    behavior_random_use(&expected);
    behavior_srand(12);
    double x = behavior_random_unit(), y = behavior_random_range(0, 10);
    behavior_random_use(NULL);
    CF_V2 reseeded = roll(99, 12, 1);
    assert(reseeded.x == (float)x && reseeded.y == (float)y);
    assert(
        value_hash(3, 1, 2) == value_hash(3, 1, 2) &&
        value_hash(3, 1, 2) != value_hash(4, 1, 2)
    );
}

/* Tick writes the run seed to Transform.x; Begin makes Request's restart. */
static behavior_asset *seed_behavior(const char *request)
{
    behavior_asset *asset = behavior("Seeded");
    graph *g = asset->graph;
    node *set = add(g, "Set_GameTransform");
    node *make = add(g, "Make_CF_V2");
    flow(g, event(g, "Tick"), set);
    wire(g, add(g, "Get Run Seed"), 0, make, 0);
    wire(g, make, 0, set, 1);
    if (request) {
        flow(g, event(g, "Begin"), add(g, request));
    }
    return asset;
}

static const char
    SEEDED[] = "{\"name\":\"Seeded\",\"behaviors\":[\"Seeded\"],"
               "\"components\":{\"GameTransform\":{\"x\":0,\"y\":0}}}";

static int restart(const char *request, int pin)
{
    run_fixture f = fixture_new();
    if (pin >= 0) {
        engine_pin_run_seed(f.runtime, pin);
    }
    fixture_behavior(&f, seed_behavior(request));
    fixture_spawn(&f, SEEDED);
    assert(engine_restart_requested(f.runtime));
    int before = engine_run_seed(f.runtime);
    engine_reset(f.runtime);
    assert(!engine_restart_requested(f.runtime));
    int after = engine_run_seed(f.runtime);
    fixture_free(&f);
    return after == before;
}

static const char CAVE_PARAMETERS[] = "{\"Radius\":72,\"Roughness\":30}";

/* A CaveField terrain at the origin with the given bounds, streaming and
 * seed settings. */
static ecs_entity cave_terrain(run_fixture *f, MaterialTerrain terrain)
{
    char *json = text_file_read("projects/arena2d/content/fields/CaveField.yaml");
    behavior_asset *field = behavior_asset_load_json(json);
    free(json);
    char error[256];
    assert(
        field &&
        material_field_compile(game_materials_fields(f->live->materials), field, error, sizeof error)
    );
    behavior_asset_free(field);
    ecs_entity source = ecs_world_create_entity(f->world);
    assert(GAME_GameTransform_add(f->world, source, 0) == ECS_OK);
    terrain.seed = 1729;
    terrain.cell_size = 4;
    terrain.field = "CaveField";
    terrain.parameters = CAVE_PARAMETERS;
    terrain.category = 8;
    terrain.mask = 1;
    terrain.min_fragment_cells = 4;
    assert(
        ecs_world_record_add(f->world, source, GAME_COMPONENT_MaterialTerrain.id, &terrain, 0) == ECS_OK
    );
    assert(ecs_world_apply_commands(f->world) == ECS_OK);
    return source;
}

/* Hashes every generated terrain tile's cells. */
static uint64_t terrain_hash(run_fixture *f)
{
    assert(!game_materials_prepare(f->live->materials));
    uint64_t hash = UINT64_C(14695981039346656037);
    ecs_component_id id = GAME_COMPONENT_GameDestructible.id;
    ecs_query query;
    assert(ecs_query_begin(f->world, &query, &id, 1) == ECS_OK);
    ecs_entity entity;
    while (ecs_query_next(&query, &entity)) {
        const material_mask *mask = game_materials_mask(f->live->materials, entity);
        for (int i = 0; i < mask->width * mask->height; i++) {
            hash = (hash ^ mask->cells[i]) * UINT64_C(1099511628211);
        }
    }
    ecs_query_end(&query);
    return hash;
}

static void run_seed()
{
    run_fixture f = fixture_new();
    fixture_behavior(&f, seed_behavior(NULL));
    engine_pin_run_seed(f.runtime, 12345);
    ecs_entity seeded = fixture_spawn(&f, SEEDED);
    fixture_steps(&f, 1);
    assert(GAME_GameTransform_get_const(f.world, seeded)->x == 12345);
    engine_reset(f.runtime);
    assert(engine_run_seed(f.runtime) == 12345);
    engine_pin_run_seed(f.runtime, ENGINE_RUN_SEED_RANDOM);
    int first = engine_run_seed(f.runtime);
    engine_reset(f.runtime);
    assert(engine_run_seed(f.runtime) != first && engine_run_seed(f.runtime) >= 0);

    /* seed_from_run terrain regenerates per run seed and repeats for a pin. */
    ecs_entity source = cave_terrain(
        &f,
        (MaterialTerrain){ .width = 256, .height = 256, .seed_from_run = true }
    );
    engine_pin_run_seed(f.runtime, 1);
    uint64_t one = terrain_hash(&f);
    engine_pin_run_seed(f.runtime, 2);
    uint64_t two = terrain_hash(&f);
    engine_pin_run_seed(f.runtime, 1);
    assert(one != two && terrain_hash(&f) == one);
    GAME_MaterialTerrain_get(f.world, source)->seed_from_run = false;
    uint64_t fixed = terrain_hash(&f);
    engine_pin_run_seed(f.runtime, 2);
    assert(terrain_hash(&f) == fixed && fixed != one);
    fixture_free(&f);

    /* Restart Scene keeps the seed, Restart Run draws a new one, and a pin
     * holds either way. */
    assert(restart("Restart Scene", -1) && !restart("Restart Run", -1));
    assert(restart("Restart Run", 77));

    /* A scene's seed: pins every run when present. */
    char scene_error[256];
    game_scene_document *scene = game_scene_document_load(
        "{\"version\":1,\"name\":\"Seeded\",\"seed\":5,\"entities\":[]}",
        scene_error,
        sizeof scene_error
    );
    assert(scene && scene->seed == 5);
    char *saved = game_scene_document_save(scene);
    game_scene_document_free(scene);
    scene = game_scene_document_load(saved, scene_error, sizeof scene_error);
    assert(scene && scene->seed == 5);
    free(saved);
    game_scene_document_free(scene);
    scene = game_scene_document_load(
        "{\"version\":1,\"name\":\"Free\",\"entities\":[]}",
        scene_error,
        sizeof scene_error
    );
    assert(scene && scene->seed == GAME_SCENE_RANDOM_SEED);
    game_scene_document_free(scene);
    assert(!game_scene_document_load(
        "{\"version\":1,\"name\":\"Bad\",\"seed\":-3,\"entities\":[]}",
        scene_error,
        sizeof scene_error
    ));
}

static game_terrain_sample terrain_at(run_fixture *f, ecs_entity terrain, float x, float y)
{
    game_terrain_sample sample;
    const char *error = game_materials_terrain_at(
        f->live->materials,
        terrain,
        (fluid_vec){ x, y },
        &sample
    );
    if (error) {
        fprintf(stderr, "%s\n", error);
    }
    assert(!error);
    return sample;
}

static float field_at(run_fixture *f, float x, float y)
{
    float value;
    char error[256];
    assert(material_field_sample(
        game_materials_fields(f->live->materials),
        "CaveField",
        CAVE_PARAMETERS,
        1729,
        x,
        y,
        &value,
        error,
        sizeof error
    ));
    return value;
}

/* Tick writes (Sample Field, Terrain Distance) at (30, -50) to Velocity. */
static behavior_asset *probe_behavior()
{
    behavior_asset *asset = behavior("Probe");
    graph *g = asset->graph;
    node *set = add(g, "Set_GameVelocity");
    node *make = add(g, "Make_CF_V2");
    node *sample = add(g, "Sample Field");
    node *distance = add(g, "Terrain Distance");
    node *point = add(g, "Make_CF_V2");
    set_input(g, sample, 0, (pin_value){ .type = PIN_NAME, .type_name = "name", .str = "CaveField" });
    set_input(g, sample, 1, (pin_value){ .type = PIN_STR, .type_name = "const char *", .str = CAVE_PARAMETERS });
    set_input(g, sample, 2, (pin_value){ .type = PIN_F32, .type_name = "float", .f = 30 });
    set_input(g, sample, 3, (pin_value){ .type = PIN_F32, .type_name = "float", .f = -50 });
    set_input(g, sample, 4, (pin_value){ .type = PIN_I32, .type_name = "int", .i = 1729 });
    set_input(g, point, 0, (pin_value){ .type = PIN_F32, .type_name = "float", .f = 30 });
    set_input(g, point, 1, (pin_value){ .type = PIN_F32, .type_name = "float", .f = -50 });
    flow(g, event(g, "Tick"), set);
    wire(g, behavior_asset_add_self_node(asset), 0, distance, 0);
    wire(g, point, 0, distance, 1);
    wire(g, sample, 0, make, 0);
    wire(g, distance, 0, make, 1);
    wire(g, make, 0, set, 1);
    return asset;
}

static void terrain_queries()
{
    run_fixture f = fixture_new();
    ecs_entity cave = cave_terrain(&f, (MaterialTerrain){ .width = 256, .height = 256 });
    /* Before generation the field answers. */
    game_terrain_sample early = terrain_at(&f, cave, 10, 20);
    assert(early.distance == field_at(&f, 10, 20) && early.solid == (early.distance <= 0));
    assert(!game_materials_prepare(f.live->materials));
    /* Resident cells answer: occupancy decides, distance follows the field. */
    int solid = 0, air = 0;
    for (int i = 0; i < 64; i++) {
        float x = -110 + i * 3.4f, y = 70 - i * 2.2f;
        game_terrain_sample sample = terrain_at(&f, cave, x, y);
        float field = field_at(&f, x, y);
        solid += sample.solid;
        air += !sample.solid;
        assert(sample.solid == (sample.distance <= 0));
        if (fabsf(field) > 8) {
            assert(sample.solid == (field < 0) && fabsf(sample.distance - field) < 8);
        }
    }
    assert(solid && air);
    /* Outside the bounds is air, measured to the bounds. */
    game_terrain_sample outside = terrain_at(&f, cave, 128 + 30, 0);
    assert(!outside.solid && fabsf(outside.distance - 30) < 1e-3f);
    /* Damage opens rock at once. */
    float rock_x = 0, rock_y = 0;
    for (int i = 0; i < 4096 && !terrain_at(&f, cave, rock_x, rock_y).solid; i++) {
        rock_x = (float)(i % 64) * 4 - 126;
        rock_y = (float)(i / 64) * 4 - 126;
    }
    assert(terrain_at(&f, cave, rock_x, rock_y).solid);
    ecs_component_id id = GAME_COMPONENT_GameDestructible.id;
    ecs_query query;
    assert(ecs_query_begin(f.world, &query, &id, 1) == ECS_OK);
    ecs_entity tile;
    while (ecs_query_next(&query, &tile)) {
        game_materials_damage(
            f.live->materials,
            tile,
            (fluid_vec){ rock_x, rock_y },
            10,
            100,
            0,
            (material_impact){ 0 }
        );
    }
    ecs_query_end(&query);
    assert(!game_materials_prepare(f.live->materials));
    assert(!terrain_at(&f, cave, rock_x, rock_y).solid);
    /* Hundreds of probes per frame stay cheap. */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    float total = 0;
    for (int i = 0; i < 1000; i++) {
        total += terrain_at(&f, cave, (float)(i % 50) * 5 - 120, (float)(i / 50) * 10 - 100)
                     .distance;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double ms = (end.tv_sec - start.tv_sec) * 1e3 + (end.tv_nsec - start.tv_nsec) / 1e6;
    printf("Terrain Distance: 1000 resident probes in %.3f ms (checksum %g)\n", ms, total);

    /* The nodes, in a behavior on the terrain itself. */
    fixture_behavior(&f, probe_behavior());
    assert(GAME_GameVelocity_add(f.world, cave, 0) == ECS_OK);
    assert(ecs_world_apply_commands(f.world) == ECS_OK);
    assert(game_behavior_attach(f.live->behaviors, cave, "Probe") == 0);
    fixture_steps(&f, 1);
    CF_V2 probed = *GAME_GameVelocity_get_const(f.world, cave);
    assert(probed.x == field_at(&f, 30, -50));
    assert(probed.y == terrain_at(&f, cave, 30, -50).distance);
    fixture_free(&f);

    /* Streamed terrain answers from its field until a tile is resident. */
    f = fixture_new();
    ecs_entity streamed = cave_terrain(&f, (MaterialTerrain){ .stream_radius = 64, .stream_budget = 4 });
    game_terrain_sample far = terrain_at(&f, streamed, 5000, -3000);
    assert(far.distance == field_at(&f, 5000, -3000) && far.solid == (far.distance <= 0));
    fixture_free(&f);
}

/* Tick writes Delta Time to Velocity.x; Begin waits Wait seconds, then moves
 * Transform to (7, 7). */
static behavior_asset *clocked_behavior()
{
    behavior_asset *asset = behavior("Clocked");
    graph *g = asset->graph;
    node *velocity = add(g, "Set_GameVelocity");
    node *make = add(g, "Make_CF_V2");
    flow(g, event(g, "Tick"), velocity);
    wire(g, add(g, "Delta Time"), 0, make, 0);
    wire(g, make, 0, velocity, 1);
    node *delay = add(g, "Delay");
    set_input(g, delay, 1, (pin_value){ .type = PIN_F64, .d = 0.05 });
    node *moved = add(g, "Set_GameTransform");
    node *target = add(g, "Make_CF_V2");
    set_input(g, target, 0, (pin_value){ .type = PIN_F32, .type_name = "float", .f = 7 });
    set_input(g, target, 1, (pin_value){ .type = PIN_F32, .type_name = "float", .f = 7 });
    flow(g, event(g, "Begin"), delay);
    flow(g, delay, moved);
    wire(g, target, 0, moved, 1);
    return asset;
}

/* Begin sets the time scale, then starts a hit-stop when Hit is positive. */
static behavior_asset *timing_behavior(float scale, float hit, float hit_scale)
{
    behavior_asset *asset = behavior("Timing");
    graph *g = asset->graph;
    node *set = add(g, "Set Time Scale");
    set_input(g, set, 1, (pin_value){ .type = PIN_F32, .type_name = "float", .f = scale });
    flow(g, event(g, "Begin"), set);
    if (hit > 0) {
        node *stop = add(g, "Hit Stop");
        set_input(g, stop, 1, (pin_value){ .type = PIN_F32, .type_name = "float", .f = hit });
        set_input(g, stop, 2, (pin_value){ .type = PIN_F32, .type_name = "float", .f = hit_scale });
        flow(g, set, stop);
    }
    return asset;
}

static const char TIMER[] = "{\"name\":\"Timer\",\"behaviors\":[\"Timing\"],"
                            "\"components\":{}}";
static const char
    CLOCKED[] = "{\"name\":\"Clocked\",\"behaviors\":[\"Clocked\"],"
                "\"components\":{\"GameTransform\":{\"x\":0,\"y\":0},"
                "\"GameVelocity\":{\"x\":0,\"y\":0}}}";
static const char
    UNSCALED[] = "{\"name\":\"Unscaled\",\"behaviors\":[\"Clocked\"],"
                 "\"components\":{\"GameTransform\":{\"x\":0,\"y\":0},"
                 "\"GameVelocity\":{\"x\":0,\"y\":0},\"GameUnscaledTime\":{}}}";
static const char MOVER[] = "{\"name\":\"Mover\",\"components\":{"
                            "\"GameTransform\":{\"x\":0,\"y\":0},"
                            "\"GameVelocity\":{\"x\":120,\"y\":0},"
                            "\"GameBody\":{\"type\":1},\"GameCollider\":{"
                            "\"radius\":2,\"category\":1}}}";

/* How far a body moving at 120 units/s travels in 30 real steps at scale. */
static float traveled(float scale)
{
    run_fixture f = fixture_new();
    fixture_behavior(&f, timing_behavior(scale, 0, 0));
    fixture_spawn(&f, TIMER);
    ecs_entity mover = fixture_spawn(&f, MOVER);
    fixture_steps(&f, 30);
    float x = GAME_GameTransform_get_const(f.world, mover)->x;
    fixture_free(&f);
    return x;
}

static void time_scale()
{
    /* At scale 0 only GameUnscaledTime behaviors see time pass. */
    run_fixture f = fixture_new();
    fixture_behavior(&f, timing_behavior(0, 0, 0));
    fixture_behavior(&f, clocked_behavior());
    fixture_spawn(&f, TIMER);
    ecs_entity scaled = fixture_spawn(&f, CLOCKED);
    ecs_entity unscaled = fixture_spawn(&f, UNSCALED);
    fixture_steps(&f, 12);
    assert(GAME_GameVelocity_get_const(f.world, scaled)->x == 0);
    assert(GAME_GameVelocity_get_const(f.world, unscaled)->x == (float)ENGINE_FIXED_SECONDS);
    assert(GAME_GameTransform_get_const(f.world, scaled)->x == 0);
    assert(GAME_GameTransform_get_const(f.world, unscaled)->x == 7);
    assert(engine_last_timing(f.runtime)->simulation_time == 0);
    /* Restart returns to real-time speed. */
    engine_reset(f.runtime);
    assert(game_run_time_scale(game_behavior_run(f.live->behaviors)) == 1);
    fixture_free(&f);

    /* Hit-stop runs its scale for its real duration, then the time scale. */
    f = fixture_new();
    fixture_behavior(&f, timing_behavior(1, 0.1f, 0.25f));
    fixture_spawn(&f, TIMER);
    fixture_steps(&f, 10);
    double expected = 6 * ENGINE_FIXED_SECONDS * 0.25 + 4 * ENGINE_FIXED_SECONDS;
    assert(fabs(engine_last_timing(f.runtime)->simulation_time - expected) < 1e-9);
    fixture_free(&f);

    /* Physics integrates scaled time. */
    float full = traveled(1), half = traveled(0.5f), frozen = traveled(0);
    assert(full > 50 && fabsf(half - full * .5f) < 1 && frozen == 0);
}

/* Begin adds Amount of camera trauma. */
static behavior_asset *trauma_behavior(float amount)
{
    behavior_asset *asset = behavior("Trauma");
    graph *g = asset->graph;
    node *shake = add(g, "Add Camera Trauma");
    set_input(g, shake, 1, (pin_value){ .type = PIN_F32, .type_name = "float", .f = amount });
    flow(g, event(g, "Begin"), shake);
    return asset;
}

static const char SHAKEN[] = "{\"name\":\"Shaken\",\"behaviors\":[\"Trauma\"],"
                             "\"components\":{"
                             "\"GameTransform\":{\"x\":40,\"y\":-20},"
                             "\"GameCamera\":{\"width\":320,\"height\":180},"
                             "\"GameCameraShake\":{\"decay\":1,\"max_offset\":"
                             "12,\"frequency\":15}}}";

static void camera_trauma()
{
    run_fixture f = fixture_new();
    fixture_behavior(&f, trauma_behavior(.8f));
    ecs_entity camera = fixture_spawn(&f, SHAKEN);
    const GameCameraShake *shake = GAME_GameCameraShake_get_const(f.world, camera);
    assert(fabsf(shake->trauma - .8f) < 1e-6f);
    /* Trauma decays per real second, even when time is frozen. */
    game_behavior_run(f.live->behaviors)->time_scale = 0;
    fixture_steps(&f, 30);
    assert(fabsf(shake->trauma - .3f) < 1e-4f);
    /* The view, not the camera, moves: within max_offset times trauma squared. */
    engine_viewport_view view;
    float moved = 0;
    for (int i = 0; i < 6; i++) {
        fixture_steps(&f, 1);
        assert(game_camera_view(f.world, f.runtime, &view) == 1);
        float dx = view.center_x - 40, dy = view.center_y - 20;
        float limit = 12 * shake->trauma * shake->trauma + 1e-4f;
        assert(fabsf(dx) <= limit && fabsf(dy) <= limit);
        moved = fmaxf(moved, fabsf(dx) + fabsf(dy));
    }
    assert(moved > 0);
    assert(GAME_GameTransform_get_const(f.world, camera)->x == 40);
    fixture_steps(&f, 30);
    assert(shake->trauma == 0);
    assert(game_camera_view(f.world, f.runtime, &view) == 1);
    assert(view.center_x == 40 && view.center_y == 20);
    fixture_free(&f);
    /* Trauma stays within 0 to 1. */
    f = fixture_new();
    fixture_behavior(&f, trauma_behavior(5));
    camera = fixture_spawn(&f, SHAKEN);
    assert(GAME_GameCameraShake_get_const(f.world, camera)->trauma == 1);
    fixture_free(&f);
}

void test_run_capabilities()
{
    seeded_randomness();
    run_seed();
    terrain_queries();
    time_scale();
    camera_trauma();
    puts("Run capabilities PASS: seeded per-world randomness, run seeds, seed_from_run terrain, restart requests, Sample Field, Terrain Distance, time scale, hit-stop and camera trauma");
}
