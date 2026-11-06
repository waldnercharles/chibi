#include "authoring/shader_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/shader/shader_graph.h"
#include "render/material_height.h"
#include "render/mesh_surface.h"
#include "runtime/component_catalog.h"
#include "runtime/world.h"
#include "support/array.h"
#include "test/graph/packs.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum
{
    TILES = 2,
    REGION_WIDTH = 16,
    REGION_HEIGHT = 4
};
void test_native_catalog_enable();
static const char identity[] = "mesh-persistence/rock";
static const char *binary, *directory, *phase;
static char scratch[64], error[256];
static world *simulation;
static ecs_entity source;
static render_shader_parameter_cache *parameters;
static bool packs_installed;

typedef struct
{
    ecs_entity entity;
    const material_mask *mask;
    Transform pose;
    render_mesh_asset mesh;
    render_mesh_surface surface;
} terrain_piece;
static terrain_piece pieces[TILES];
static int piece_count;

typedef struct
{
    int cells, left, right, far, mesh_left, mesh_right, mesh_far;
    uint64_t near_cells, far_cells, near_surface, far_surface;
} terrain_evidence;

static void clear_meshes()
{
    for (int i = 0; i < piece_count; i++) {
        render_mesh_surface_free(&pieces[i].surface);
        render_mesh_asset_free(&pieces[i].mesh);
    }
    memset(pieces, 0, sizeof pieces);
    piece_count = 0;
}
static void cleanup()
{
    clear_meshes();
    render_shader_parameter_cache_free(parameters);
    parameters = NULL;
    if (simulation) {
        world_free(simulation);
    }
    simulation = NULL;
    authoring_shader_catalog_clear();
    if (packs_installed) {
        test_graph_packs_restore();
        type_catalog_set_project(NULL);
        packs_installed = false;
    }
    if (*scratch) {
        tool_remove_tree(scratch);
        *scratch = 0;
    }
}

/* The real shared Surface Height evaluator receives a static material graph.
 * Its small source-coordinate gradient makes frame/height loss observable;
 * no image, graphics context, GPU upload or authored game asset is required. */
static bool height_material()
{
    behavior_asset *asset = behavior_asset_new("Persistent rock height");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = asset->graph;
    node *out = shader_graph_add_output(g, false);
    node *position = graph_add_node(g, registry_node_by_name("Shader PartPosition"));
    node *x = graph_add_node(g, registry_node_by_name("Shader X"));
    node *gain = graph_add_node(g, registry_node_by_name("Shader Multiply"));
    node *add = graph_add_node(g, registry_node_by_name("Shader Add"));
    assert(!graph_connect_data(g, position, 0, x, 0));
    assert(!graph_connect_data(g, x, 0, gain, 0));
    pin_value scale = { .type = PIN_F32, .f = .005f };
    pin_value raised = { .type = PIN_F32, .f = 1 };
    assert(!graph_set_pin_default(g, gain, 0, 1, &scale));
    assert(!graph_connect_data(g, gain, 0, add, 0));
    assert(!graph_set_pin_default(g, add, 0, 1, &raised));
    assert(!graph_connect_data(g, add, 0, out, SHADER_SURFACE_HEIGHT));
    shader_error diagnostic = { 0 };
    bool ok = render_shader_upsert(asset, &diagnostic);
    behavior_asset_free(asset);
    if (!ok) {
        fprintf(stderr, "terrain mesh Height material: %s\n", diagnostic.message);
    }
    REQUIRE(ok);
    return true;
}

static bool prepare()
{
    /* Material Result snapshots its CF_V2 payload even without a behavior
     * receiver. Select the real runtime types before dispatching that event. */
    test_native_catalog_enable();
    type_catalog_pack types = test_graph_types(0);
    type_catalog_set_project(&types);
    test_graph_packs_install();
    packs_installed = true;
    REQUIRE(type_catalog_find("CF_V2") == &T_CF_V2);
    REQUIRE(height_material());
    world_options options = {
        .modules = WORLD_MATERIALS | WORLD_PHYSICS,
        .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
    };
    simulation = world_new(&options, error, sizeof error);
    if (!simulation) {
        fprintf(stderr, "terrain mesh world: %s\n", error);
    }
    REQUIRE(simulation);
    REQUIRE(engine_set_playing(world_engine(simulation), false) == 0);
    materials *terrain = world_materials(simulation);
    field_ir constant = { .op = FIELD_CONSTANT, .value = -1 };
    field_program *program = field_program_new(&constant, 1, 0, NULL, 0, error, sizeof error);
    REQUIRE(program && material_field_register(materials_fields(terrain), "Rock", program));
    ecs_world *ecs = world_ecs(simulation);
    source = ecs_world_create_entity(ecs);
    MaterialTerrain authored = { .width = 256,
                                 .height = 16,
                                 .cell_size = 1,
                                 .field = "Rock",
                                 .seed = 17,
                                 .category = 8,
                                 .mask = 1,
                                 .min_fragment_cells = 1 };
    Transform pose = { 0 };
    Substance substance = { .hardness = 1 };
    MaterialSurface3D surface = { .enabled = true,
                                  .material_height = true,
                                  .elevation = 12,
                                  .depth = 8,
                                  .bevel_width = 1,
                                  .relief_scale = 8,
                                  .spacing = 4 };
    Shader binding = { .material = "Persistent rock height" };
    shader_error diagnostic = { 0 };
    REQUIRE(authoring_shader_binding_prepare(&binding, &diagnostic));
    REQUIRE(
        ecs_world_record_add(ecs, source, COMPONENT_MaterialTerrain.id, &authored, 0) == ECS_OK
    );
    REQUIRE(ecs_world_record_add(ecs, source, COMPONENT_Transform.id, &pose, 0) == ECS_OK);
    REQUIRE(ecs_world_record_add(ecs, source, COMPONENT_Substance.id, &substance, 0) == ECS_OK);
    REQUIRE(
        ecs_world_record_add(ecs, source, COMPONENT_MaterialSurface3D.id, &surface, 0) == ECS_OK
    );
    REQUIRE(ecs_world_record_add(ecs, source, COMPONENT_Shader.id, &binding, 0) == ECS_OK);
    REQUIRE(ecs_world_apply_commands(ecs) == ECS_OK);
    char path[128];
    int n = snprintf(path, sizeof path, "%s/save.json", directory);
    REQUIRE(n > 0 && n < (int)sizeof path);
    REQUIRE(save_open(world_modules(simulation)->save, path));
    REQUIRE(!materials_prepare(terrain));
    REQUIRE(physics_tick(world_modules(simulation)->physics, 0) == 0);
    REQUIRE(engine_frame(world_engine(simulation), 0) == 0);
    return true;
}

static bool step()
{
    engine *engine = world_engine(simulation);
    REQUIRE(engine_step(engine) == 0);
    int result = engine_frame(engine, 0);
    if (result) {
        fprintf(stderr, "terrain mesh step: %s\n", engine_error(engine));
    }
    REQUIRE(result == 0 && engine_last_timing(engine)->steps == 1);
    return true;
}
static bool store(bool load)
{
    terrain_storage *storage = world_modules(simulation)->terrain_storage;
    int request = terrain_storage_request(storage, source, identity, load, error, sizeof error);
    REQUIRE(request > 0 && terrain_storage_result(storage, request).pending);
    /* Only the ordinary engine boundary performs the persistence operation. */
    REQUIRE(step());
    terrain_storage_state state = terrain_storage_result(storage, request);
    if (!state.succeeded) {
        fprintf(stderr, "terrain mesh storage: %s\n", state.error);
    }
    REQUIRE(!state.pending && state.succeeded && !state.error[0]);
    /* Load publishes new masks after this step's physics. Refresh their shapes
     * and transforms at the next normal boundary before querying collision. */
    REQUIRE(step());
    return true;
}

static bool build_meshes()
{
    clear_meshes();
    ecs_world *ecs = world_ecs(simulation);
    materials *terrain = world_materials(simulation);
    ecs_component_id ids[] = { COMPONENT_Destructible.id, COMPONENT_Transform.id };
    ecs_query query;
    REQUIRE(ecs_query_begin(ecs, &query, ids, 2) == ECS_OK);
    ecs_entity entity;
    int found = 0;
    while (ecs_query_next(&query, &entity)) {
        const material_mask *mask = materials_mask(terrain, entity);
        if (!mask || materials_is_fragment(terrain, entity)) {
            continue;
        }
        if (found < TILES) {
            pieces[found].entity = entity;
            pieces[found].mask = mask;
            pieces[found].pose = *Transform_get_const(ecs, entity);
        }
        found++;
    }
    ecs_query_end(&query);
    REQUIRE(found == TILES);
    piece_count = found;
    for (int i = 0; i < piece_count; i++) {
        terrain_piece *piece = &pieces[i];
        const MaterialSurface3D
            *options = MaterialSurface3D_get_const(ecs, piece->entity);
        const MaterialFrame *frame = MaterialFrame_get_const(ecs, piece->entity);
        const Shader *binding = Shader_get_const(ecs, piece->entity);
        REQUIRE(options && options->enabled && options->material_height && frame && binding);
        render_material_height *height = NULL;
        bool prepared = render_material_height_prepare(
            ecs,
            piece->entity,
            binding,
            &parameters,
            NULL,
            &height,
            error,
            sizeof error
        );
        if (!prepared) {
            fprintf(stderr, "terrain mesh Height: %s\n", error);
        }
        REQUIRE(prepared);
        material_surface_height sampler = render_material_height_sampler(height);
        bool built = material_surface_mesh_build_height(
            material_mask_occupancy(piece->mask),
            *options,
            *frame,
            &sampler,
            &piece->mesh,
            error,
            sizeof error
        );
        render_material_height_free(height);
        if (!built) {
            fprintf(stderr, "terrain mesh geometry: %s\n", error);
        }
        REQUIRE(built && acount(piece->mesh.indices) > 0);
        render_mesh_surface_part part = {
            .visual = { .part = { .primitive = PRIMITIVE_BOX, .dimensions = { 1, 1, 1 } },
                        .frame = cf_m4_identity() },
            .vertices = piece->mesh.vertices,
            .indices = piece->mesh.indices,
            .corner_count = acount(piece->mesh.indices),
            .borrowed = true,
        };
        part.visual.frame.elements[12] = piece->pose.x;
        part.visual.frame.elements[14] = piece->pose.y;
        apush(piece->surface.parts, part);
    }
    return true;
}

static bool occupied(CF_V2 point)
{
    for (int i = 0; i < piece_count; i++) {
        if (material_mask_contains(
                pieces[i].mask,
                point.x - pieces[i].pose.x,
                point.y - pieces[i].pose.y
            )) {
            return true;
        }
    }
    return false;
}
static bool mesh_height(CF_V2 point, float *height)
{
    bool found = false;
    for (int i = 0; i < piece_count; i++) {
        const terrain_piece *piece = &pieces[i];
        float x = point.x - piece->pose.x, y = point.y - piece->pose.y;
        if (fabsf(x) > piece->mask->width * piece->mask->cell_size * .5f ||
            fabsf(y) > piece->mask->height * piece->mask->cell_size * .5f) {
            continue;
        }
        float candidate;
        if (render_mesh_surface_pick(&piece->surface, point, &candidate)) {
            if (!found || candidate > *height) {
                *height = candidate;
            }
            found = true;
        }
    }
    return found;
}
static uint64_t hash_number(uint64_t hash, int64_t value)
{
    uint64_t bits = (uint64_t)value;
    for (int i = 0; i < 8; i++) {
        hash = (hash ^ (bits & 255u)) * UINT64_C(1099511628211);
        bits >>= 8;
    }
    return hash;
}
static bool snapshot(terrain_evidence *out)
{
    REQUIRE(build_meshes());
    *out = (terrain_evidence){
        .cells = materials_inspect(world_materials(simulation)).cells,
        .near_cells = UINT64_C(14695981039346656037),
        .far_cells = UINT64_C(14695981039346656037),
        .near_surface = UINT64_C(14695981039346656037),
        .far_surface = UINT64_C(14695981039346656037),
    };
    /* Canonical spatial coverage plus height quantized to 1e-5 world units.
     * No ECS handles, mask incarnation, memory layout or allocation order enter
     * the restart oracle. The far region stays away from the edit and bevel. */
    for (int region = 0; region < 2; region++) {
        for (int y = 0; y < REGION_HEIGHT; y++) {
            for (int x = 0; x < REGION_WIDTH; x++) {
                CF_V2 point = { x * 2.f - 15 + region * 80, y * 2.f - 3 };
                bool solid = occupied(point);
                float height = 0;
                bool covered = mesh_height(point, &height);
                REQUIRE(!covered || isfinite(height));
                uint64_t *cells = region ? &out->far_cells : &out->near_cells;
                uint64_t *surface = region ? &out->far_surface : &out->near_surface;
                *cells = hash_number(*cells, solid);
                *surface = hash_number(*surface, covered);
                if (covered) {
                    *surface = hash_number(*surface, llroundf(height * 100000));
                }
                if (region) {
                    out->far += solid;
                    out->mesh_far += covered;
                    REQUIRE(covered && fabsf(height - (13 + .005f * point.x)) < .001f);
                } else if (point.x < 0) {
                    out->left += solid;
                    out->mesh_left += covered;
                } else {
                    out->right += solid;
                    out->mesh_right += covered;
                }
            }
        }
    }
    return true;
}
static bool same(terrain_evidence a, terrain_evidence b)
{
    return a.cells == b.cells && a.left == b.left && a.right == b.right &&
           a.far == b.far && a.mesh_left == b.mesh_left &&
           a.mesh_right == b.mesh_right && a.mesh_far == b.mesh_far &&
           a.near_cells == b.near_cells && a.far_cells == b.far_cells &&
           a.near_surface == b.near_surface && a.far_surface == b.far_surface;
}
static bool collision(float x, bool expected)
{
    ray_hit hit;
    REQUIRE(
        physics_raycast(
            world_ecs(simulation),
            world_entities(simulation),
            (CF_V2){ x, -4 },
            (CF_V2){ x, 4 },
            8,
            (ecs_entity){ 0 },
            false,
            &hit
        ) == 0
    );
    REQUIRE(hit.found == expected);
    return true;
}
static bool passage(bool expected)
{
    REQUIRE(collision(-.5f, expected));
    REQUIRE(collision(.5f, expected));
    REQUIRE(collision(80, true));
    float height;
    REQUIRE(mesh_height((CF_V2){ -.5f, 0 }, &height) == expected);
    REQUIRE(mesh_height((CF_V2){ .5f, 0 }, &height) == expected);
    return true;
}

static void print_evidence(FILE *file, terrain_evidence state)
{
    fprintf(
        file,
        "%d %d %d %d %d %d %d %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
        state.cells,
        state.left,
        state.right,
        state.far,
        state.mesh_left,
        state.mesh_right,
        state.mesh_far,
        state.near_cells,
        state.far_cells,
        state.near_surface,
        state.far_surface
    );
}
static bool read_evidence(FILE *file, terrain_evidence *state)
{
    REQUIRE(
        fscanf(
            file,
            "%d %d %d %d %d %d %d %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
            &state->cells,
            &state->left,
            &state->right,
            &state->far,
            &state->mesh_left,
            &state->mesh_right,
            &state->mesh_far,
            &state->near_cells,
            &state->far_cells,
            &state->near_surface,
            &state->far_surface
        ) == 11
    );
    return true;
}

TEST_CASE(terrain_mesh_child_regenerates_then_restores_published_geometry)
{
    REQUIRE(prepare());
    terrain_evidence base;
    REQUIRE(snapshot(&base));
    REQUIRE(base.cells == 4096 && base.left > 0 && base.right > 0);
    REQUIRE(base.far == REGION_WIDTH * REGION_HEIGHT && base.mesh_far == base.far);
    REQUIRE(passage(true));
    REQUIRE(pieces[0].pose.x != pieces[1].pose.x);
    char path[128];
    int n = snprintf(path, sizeof path, "%s/evidence.txt", directory);
    REQUIRE(n > 0 && n < (int)sizeof path);
    if (!strcmp(phase, "write")) {
        materials_damage(
            world_materials(simulation),
            source,
            (fluid_vec){ 0, 0 },
            6,
            2,
            0,
            (material_impact){ 0 }
        );
        REQUIRE(step());
        terrain_evidence cut;
        REQUIRE(snapshot(&cut));
        REQUIRE(cut.cells < base.cells && cut.left < base.left && cut.right < base.right);
        REQUIRE(cut.mesh_left < base.mesh_left && cut.mesh_right < base.mesh_right);
        REQUIRE(cut.near_cells != base.near_cells && cut.near_surface != base.near_surface);
        REQUIRE(cut.far_cells == base.far_cells && cut.far_surface == base.far_surface);
        REQUIRE(passage(false));
        REQUIRE(store(false));
        terrain_evidence saved;
        REQUIRE(snapshot(&saved) && same(saved, cut));
        FILE *file = fopen(path, "w");
        REQUIRE(file);
        print_evidence(file, base);
        print_evidence(file, cut);
        REQUIRE(!fclose(file));
        fprintf(
            stderr,
            "terrain mesh writer: cells %d -> %d; near mesh %016" PRIx64
            " -> %016" PRIx64 "; far %016" PRIx64 " unchanged\n",
            base.cells,
            cut.cells,
            base.near_surface,
            cut.near_surface,
            cut.far_surface
        );
        return true;
    }
    REQUIRE(!strcmp(phase, "read"));
    FILE *file = fopen(path, "r");
    REQUIRE(file);
    terrain_evidence before, saved;
    bool read = read_evidence(file, &before) && read_evidence(file, &saved);
    REQUIRE(!fclose(file));
    REQUIRE(read && same(before, base) && !same(saved, base));
    REQUIRE(store(true));
    terrain_evidence loaded;
    REQUIRE(snapshot(&loaded) && same(saved, loaded));
    REQUIRE(passage(false));
    fprintf(
        stderr,
        "terrain mesh reader: pristine regeneration verified; restored "
        "coverage/Height/collision across seam; far %016" PRIx64 " unchanged\n",
        loaded.far_surface
    );
    return true;
}

int test_terrain_mesh_persistence_child(const char *save_directory, const char *child_phase)
{
    directory = save_directory;
    phase = child_phase;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(terrain_mesh_child_regenerates_then_restores_published_geometry);
    pu_clear_setup();
    return pu_test_failed() ? 1 : 0;
}
TEST_CASE(terrain_mesh_restores_in_a_fresh_process)
{
    snprintf(scratch, sizeof scratch, "/tmp/chibi-terrain-mesh-XXXXXX");
    REQUIRE(mkdtemp(scratch));
    const char *phases[] = { "write", "read" };
    for (int i = 0; i < 2; i++) {
        char log[128];
        snprintf(log, sizeof log, "%s/%s.log", scratch, phases[i]);
        const char *args[] = { binary,
                               "--terrain-mesh-persistence-child",
                               scratch,
                               phases[i],
                               NULL };
        REQUIRE(tool_run(args, tool_root(), log) == 0);
        char *output = tool_read(log);
        REQUIRE(output);
        fputs(output, stdout);
        free(output);
    }
    return true;
}
void test_terrain_mesh_persistence(const char *program)
{
    binary = program;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(terrain_mesh_restores_in_a_fresh_process);
    pu_clear_setup();
}
