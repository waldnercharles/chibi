#include "graph/runtime/random.h"
#include "support/value_noise.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <math.h>

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

static void wire(graph *g, node *from, int output, node *to, int input)
{
    assert(graph_connect_data(g, from, output, to, input) == 0);
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

void test_run_capabilities()
{
    seeded_randomness();
    puts("Run capabilities PASS: seeded per-world randomness");
}
