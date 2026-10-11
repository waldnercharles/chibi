#include "assets/model_export.h"
#include "authoring/mesh_prepare.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/modeling/model_compile.h"
#include "runtime/component_catalog.h"
#include "runtime/geometry/mesh_pick.h"
#include "runtime/geometry/mesh_prepared.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Indexed `.mesh` files: tangents from texture directions, preparation from
 * mounted content, the meshes and files preparation refuses, and model export
 * bundles, all inside one temporary directory. */
static char directory[] = "/tmp/chibi-mesh-XXXXXX";
static render_mesh_asset mesh;
static unsigned char *bytes;
static size_t length;
static const registry_pack **previous_packs;
static bool packs_replaced;
/* What the cases may create below `directory`, children before parents. */
static const char *const created[] = { "content/meshes/quad.mesh",
                                       "content/meshes/parts.mesh",
                                       "content/meshes/text.mesh",
                                       "content/meshes/short.mesh",
                                       "content/meshes/old.mesh",
                                       "content/meshes",
                                       "content",
                                       "bundle/model.mesh",
                                       "bundle/model.obj",
                                       "bundle/model.mtl",
                                       "bundle/provenance.json",
                                       "bundle",
                                       "models/Ball.kdl",
                                       "models" };

static void inside(char *path, size_t size, const char *name)
{
    int n = snprintf(path, size, "%s/%s", directory, name);
    assert(n > 0 && (size_t)n < size);
}

static void setup()
{
    strcpy(directory, "/tmp/chibi-mesh-XXXXXX");
    assert(mkdtemp(directory));
    char path[256];
    inside(path, sizeof path, "content");
    assert(!mkdir(path, 0700));
    inside(path, sizeof path, "content/meshes");
    assert(!mkdir(path, 0700));
    char error[128];
    inside(path, sizeof path, "content");
    assert(authoring_mesh_mount_content(path, error, sizeof error));
}

static void teardown()
{
    render_mesh_asset_free(&mesh);
    free(bytes);
    bytes = NULL;
    length = 0;
    render_mesh_assets_clear();
    authoring_mesh_unmount_content();
    if (packs_replaced) {
        registry_set_packs(previous_packs, acount(previous_packs));
        packs_replaced = false;
    }
    afree(previous_packs);
    for (size_t i = 0; i < sizeof created / sizeof *created; i++) {
        char path[256];
        inside(path, sizeof path, created[i]);
        remove(path);
    }
    assert(!rmdir(directory));
}

static bool near(float a, float b)
{
    return fabsf(a - b) < 1e-5f;
}

static bool near3(CF_V3 a, CF_V3 b)
{
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

/* A two-unit quad facing +Y. Texture u runs along +X and v along +Z, and two
 * triangles share the diagonal's vertices. */
static void quad()
{
    render_mesh_asset_free(&mesh);
    mesh.min = (CF_V3){ -1, 0, -1 };
    mesh.max = (CF_V3){ 1, 0, 1 };
    for (int i = 0; i < 4; i++) {
        float x = i == 1 || i == 2 ? 1 : -1, z = i >= 2 ? 1 : -1;
        render_mesh_surface_vertex v = { .position = { x, 0, z },
                                         .normal = { 0, 1, 0 },
                                         .uv = { (x + 1) / 2, (z + 1) / 2 } };
        apush(mesh.vertices, v);
    }
    static const int corners[] = { 0, 2, 1, 0, 3, 2 };
    for (int i = 0; i < 6; i++) {
        apush(mesh.indices, corners[i]);
    }
    render_mesh_asset_tangents(&mesh);
}

/* Splits the quad into one material group per triangle. */
static void groups()
{
    static const char *const materials[] = { "Paint", "Trim" };
    for (int i = 0; i < 2; i++) {
        render_mesh_group group = { .first = i * 3,
                                    .count = 3,
                                    .part = -1,
                                    .asset = xstrdup("Quad"),
                                    .name = xstrdup(materials[i]),
                                    .path = xstrdup(materials[i]),
                                    .material = xstrdup(materials[i]),
                                    .material_scope = xstrdup("") };
        apush(mesh.groups, group);
    }
}

/* Publishes the quad under `source` and picks it from above, which builds its
 * pick tree. */
static bool picks_quad(const char *source)
{
    quad();
    render_mesh_asset_publish(source, &mesh);
    char error[128];
    const render_mesh_asset *resident = render_mesh_asset_load(source, error, sizeof error);
    REQUIRE(resident);
    CF_Ray3 down = cf_make_ray3((CF_V3){ .5f, 1, .5f }, (CF_V3){ .5f, -1, .5f });
    REQUIRE(near(render_mesh_pick(resident, down).t, 1));
    return true;
}

static bool tangent(const render_mesh_surface_vertex *v, CF_V3 direction, float hand)
{
    REQUIRE(near3((CF_V3){ v->tangent.x, v->tangent.y, v->tangent.z }, direction));
    REQUIRE(v->tangent.w == hand);
    return true;
}

static void write_file(const char *name, const void *data, size_t size)
{
    char path[256];
    inside(path, sizeof path, name);
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(data, 1, size, file) == size && !fclose(file));
}

/* Writes the mesh as a `.mesh` file below the content folder. */
static bool write_mesh(const char *name)
{
    char error[128] = { 0 };
    free(bytes);
    REQUIRE(render_mesh_asset_prepared_write(&mesh, &bytes, &length, error, sizeof error));
    write_file(name, bytes, length);
    return true;
}

TEST_CASE(mesh_tangents_follow_texture_directions)
{
    quad();
    for (int i = 0; i < 4; i++) {
        /* Bitangent +Z is cross(normal, tangent) negated. */
        REQUIRE(tangent(&mesh.vertices[i], (CF_V3){ 1, 0, 0 }, -1));
    }
    /* Mirrored texture u flips the tangent and the handedness. */
    for (int i = 0; i < 4; i++) {
        mesh.vertices[i].uv.x = 1 - mesh.vertices[i].uv.x;
    }
    render_mesh_asset_tangents(&mesh);
    REQUIRE(tangent(&mesh.vertices[0], (CF_V3){ -1, 0, 0 }, 1));
    /* Texture u along +Z and v along +X. */
    for (int i = 0; i < 4; i++) {
        CF_V3 p = mesh.vertices[i].position;
        mesh.vertices[i].uv = (CF_V2){ (p.z + 1) / 2, (p.x + 1) / 2 };
    }
    render_mesh_asset_tangents(&mesh);
    REQUIRE(tangent(&mesh.vertices[2], (CF_V3){ 0, 0, 1 }, 1));
    /* A tilted normal keeps a unit tangent perpendicular to it. */
    mesh.vertices[1].normal = cf_norm_v3((CF_V3){ 0, 1, 1 });
    render_mesh_asset_tangents(&mesh);
    CF_V4 t = mesh.vertices[1].tangent;
    CF_V3 side = { t.x, t.y, t.z };
    REQUIRE(near(cf_len_v3(side), 1) && near(cf_dot_v3(side, mesh.vertices[1].normal), 0));
    /* Unvarying texture coordinates still give a usable tangent. */
    for (int i = 0; i < 4; i++) {
        mesh.vertices[i].uv = (CF_V2){ 0 };
    }
    render_mesh_asset_tangents(&mesh);
    for (int i = 0; i < 4; i++) {
        t = mesh.vertices[i].tangent;
        side = (CF_V3){ t.x, t.y, t.z };
        REQUIRE(
            near(cf_len_v3(side), 1) &&
            near(cf_dot_v3(side, mesh.vertices[i].normal), 0)
        );
        REQUIRE(t.w == 1 || t.w == -1);
    }
    /* A sliver with almost no texture area does not outweigh a large
     * triangle that shares its corner. */
    render_mesh_asset_free(&mesh);
    static const CF_V3 fan[] = { { 0, 0, 0 },
                                 { 2, 0, 0 },
                                 { 0, 0, 2 },
                                 { 0, 0, -.1f },
                                 { -.1f, 0, 0 } };
    static const CF_V2 coords[] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1e-4f, 0 }, { 0, 1e-4f } };
    for (int i = 0; i < 5; i++) {
        apush(
            mesh.vertices,
            ((render_mesh_surface_vertex){ .position = fan[i],
                                           .normal = { 0, 1, 0 },
                                           .uv = coords[i] })
        );
    }
    static const int corners[] = { 0, 2, 1, 0, 3, 4 };
    for (int i = 0; i < 6; i++) {
        apush(mesh.indices, corners[i]);
    }
    render_mesh_asset_tangents(&mesh);
    REQUIRE(mesh.vertices[0].tangent.x > .99f);
    return true;
}

/* The quad's tangents at sizes whose edge products overflow or underflow a
 * float match its tangents at unit size. */
TEST_CASE(mesh_tangents_hold_at_any_scale)
{
    static const float scales[] = { 1e-25f, 1e20f, 3e38f };
    for (int s = 0; s < 3; s++) {
        quad();
        for (int i = 0; i < 4; i++) {
            mesh.vertices[i].position = cf_mul_v3_f(mesh.vertices[i].position, scales[s]);
        }
        render_mesh_asset_tangents(&mesh);
        for (int i = 0; i < 4; i++) {
            REQUIRE(tangent(&mesh.vertices[i], (CF_V3){ 1, 0, 0 }, -1));
        }
    }
    return true;
}

TEST_CASE(mesh_files_prepare_once_from_mounted_content)
{
    quad();
    groups();
    REQUIRE(write_mesh("content/meshes/quad.mesh"));
    /* Writing leaves the source untouched. */
    REQUIRE(near3(mesh.vertices[2].part_position, (CF_V3){ 0 }));
    REQUIRE(mesh.vertices[2].part_from_object[0][0] == 0);
    char error[128] = { 0 };
    const render_mesh_asset
        *asset = authoring_mesh_prepare("content/meshes/quad.mesh", error, sizeof error);
    REQUIRE(asset && asset->identity);
    REQUIRE(acount(asset->vertices) == 4 && acount(asset->indices) == 6);
    REQUIRE(!memcmp(asset->indices, mesh.indices, 6 * sizeof *mesh.indices));
    REQUIRE(
        acount(asset->groups) == 2 && asset->groups[1].first == 3 &&
        asset->groups[1].count == 3
    );
    REQUIRE(!strcmp(asset->groups[1].material, "Trim"));
    REQUIRE(!asset->has_part_coordinates);
    for (int i = 0; i < 4; i++) {
        const render_mesh_surface_vertex *v = &asset->vertices[i];
        REQUIRE(
            near3(v->position, mesh.vertices[i].position) &&
            near(v->uv.y, mesh.vertices[i].uv.y)
        );
        REQUIRE(tangent(v, (CF_V3){ 1, 0, 0 }, -1));
        /* Meshes without part coordinates read back in object space. */
        REQUIRE(near3(v->part_position, v->position));
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) {
                REQUIRE(v->part_from_object[j][k] == (j == k));
            }
        }
    }
    /* A resident mesh is not read again. */
    uint64_t identity = asset->identity;
    char path[256];
    inside(path, sizeof path, "content/meshes/quad.mesh");
    REQUIRE(!remove(path));
    asset = authoring_mesh_prepare("content/meshes/quad.mesh", error, sizeof error);
    REQUIRE(asset && asset->identity == identity);
    /* Part coordinates are stored when present. */
    size_t object_length = length;
    mesh.has_part_coordinates = true;
    for (int i = 0; i < 4; i++) {
        render_mesh_surface_vertex *v = &mesh.vertices[i];
        v->part_position = cf_add_v3(v->position, (CF_V3){ 5, 0, 0 });
        for (int j = 0; j < 3; j++) {
            v->part_from_object[j][j] = 2;
        }
    }
    REQUIRE(write_mesh("content/meshes/parts.mesh"));
    REQUIRE(length > object_length);
    asset = authoring_mesh_prepare("content/meshes/parts.mesh", error, sizeof error);
    REQUIRE(asset && asset->has_part_coordinates);
    REQUIRE(
        near(asset->vertices[2].part_position.x, 6) &&
        asset->vertices[2].part_from_object[1][1] == 2
    );
    return true;
}

/* Preparation reads the mesh a component draws, or a hull or mesh collider
 * wraps, and not one a collider of another shape names. */
TEST_CASE(mesh_files_come_from_drawn_meshes_and_mesh_colliders)
{
    MeshAsset drawn = { .source = "content/meshes/quad.mesh" };
    REQUIRE(authoring_component_mesh(&COMPONENT_MeshAsset, &drawn) == drawn.source);
    Collider3D collider = { .shape = COLLIDER3D_HULL, .mesh = drawn.source };
    REQUIRE(authoring_component_mesh(&COMPONENT_Collider3D, &collider) == drawn.source);
    collider.shape = COLLIDER3D_MESH;
    REQUIRE(authoring_component_mesh(&COMPONENT_Collider3D, &collider) == drawn.source);
    collider.shape = COLLIDER3D_BOX;
    REQUIRE(!authoring_component_mesh(&COMPONENT_Collider3D, &collider));
    collider = (Collider3D){ .shape = COLLIDER3D_HULL, .mesh = "" };
    REQUIRE(!authoring_component_mesh(&COMPONENT_Collider3D, &collider));
    drawn.source = NULL;
    REQUIRE(!authoring_component_mesh(&COMPONENT_MeshAsset, &drawn));
    return true;
}

/* Whether writing the mesh is refused for `reason`, writing nothing. */
static bool refused(const char *reason)
{
    char error[256] = { 0 };
    free(bytes);
    bytes = NULL;
    REQUIRE(!render_mesh_asset_prepared_write(&mesh, &bytes, &length, error, sizeof error));
    REQUIRE(strstr(error, "Invalid prepared mesh: ") && strstr(error, reason));
    REQUIRE(!bytes && !length);
    return true;
}

TEST_CASE(mesh_files_refuse_invalid_triangles)
{
    quad();
    asetlen(mesh.indices, 0);
    REQUIRE(refused("0 indices are not whole triangles"));
    quad();
    apush(mesh.indices, 1);
    REQUIRE(refused("7 indices are not whole triangles"));
    quad();
    mesh.indices[4] = 4;
    REQUIRE(refused("triangle 1 names a missing vertex"));
    quad();
    mesh.indices[2] = -1;
    REQUIRE(refused("triangle 0 names a missing vertex"));
    quad();
    mesh.indices[1] = mesh.indices[0];
    REQUIRE(refused("triangle 0 names a missing vertex or has no area"));
    quad();
    mesh.vertices[2].tangent = (CF_V4){ 0, 0, 0, 1 };
    REQUIRE(refused("vertex 2 "));
    quad();
    mesh.vertices[2].tangent.w = .5f;
    REQUIRE(refused("vertex 2 "));
    /* Material groups hold whole triangles and cover every index. */
    quad();
    groups();
    mesh.groups[0].count = 2;
    mesh.groups[1].first = 2;
    mesh.groups[1].count = 4;
    REQUIRE(refused("group 0 must continue"));
    quad();
    groups();
    mesh.groups[1].first = 0;
    REQUIRE(refused("group 1 must continue"));
    quad();
    groups();
    render_mesh_group *last = &mesh.groups[1];
    free(last->asset);
    free(last->name);
    free(last->path);
    free(last->material);
    free(last->material_scope);
    asetlen(mesh.groups, 1);
    REQUIRE(refused("groups cover 3 of 6 indices"));
    /* Part coordinates a mesh does not store are not checked. */
    quad();
    groups();
    mesh.vertices[1].part_position.x = NAN;
    REQUIRE(write_mesh("content/meshes/quad.mesh"));
    return true;
}

/* Whether preparing `source` fails, saying `message`, and leaves it unloaded. */
static bool unreadable(const char *source, const char *message)
{
    char error[160] = { 0 };
    REQUIRE(!authoring_mesh_prepare(source, error, sizeof error));
    REQUIRE(strstr(error, message));
    REQUIRE(!render_mesh_asset_load(source, error, sizeof error));
    return true;
}

TEST_CASE(mesh_files_refuse_unreadable_files)
{
    REQUIRE(unreadable("content/meshes/old.mesh.json", "must be a content/... .mesh path"));
    REQUIRE(unreadable("content/meshes/old.mesh.json", "export the model again"));
    REQUIRE(unreadable("content/../quad.mesh", "must be a content/... .mesh path"));
    REQUIRE(unreadable("meshes/quad.mesh", "must be a content/... .mesh path"));
    REQUIRE(unreadable("content/meshes/missing.mesh", "Cannot read mesh"));
    const char *json = "{\"version\":1,\"vertices\":[]}";
    write_file("content/meshes/text.mesh", json, strlen(json));
    REQUIRE(unreadable("content/meshes/text.mesh", "Not a prepared mesh"));
    quad();
    REQUIRE(write_mesh("content/meshes/short.mesh"));
    write_file("content/meshes/short.mesh", bytes, length - 1);
    REQUIRE(unreadable("content/meshes/short.mesh", "not a valid mesh file"));
    /* The version follows the nine-byte magic number. */
    REQUIRE(length > 10 && bytes[9] == RENDER_MESH_PREPARED_VERSION);
    bytes[9] = 2;
    write_file("content/meshes/old.mesh", bytes, length);
    REQUIRE(unreadable("content/meshes/old.mesh", "version differs from this engine's"));
    return true;
}

/* A saved model of a sphere whose radius is a public parameter. Loading it
 * needs the model nodes, so they replace the registry's packs until teardown. */
static void ball()
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous_packs, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &MODEL_PACK };
    registry_set_packs(packs, 1);
    packs_replaced = true;
    char path[256];
    inside(path, sizeof path, "models");
    assert(!mkdir(path, 0700));
    behavior_asset *asset = behavior_asset_new("Ball");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = asset->graph;
    node *radius = graph_add_node(g, &NODE_ModelParameter);
    assert(!graph_set_node_reference(g, radius, 0, "Radius"));
    const float range[] = { 1, .5f, 2 };
    for (int i = 0; i < 3; i++) {
        pin_value value = { .type = PIN_F32, .f = range[i] };
        assert(!graph_set_pin_default(g, radius, false, i, &value));
    }
    node *sphere = graph_add_node(g, &NODE_ModelSphere);
    assert(!graph_connect_data(g, radius, 0, sphere, 0));
    assert(!graph_connect_data(g, sphere, 0, model_graph_add_output(g), 0));
    char *saved = behavior_asset_save(asset);
    assert(saved);
    write_file("models/Ball.kdl", saved, strlen(saved));
    free(saved);
    behavior_asset_free(asset);
}

/* The bundle file's text, owned by the caller. */
static char *bundle_text(const char *name)
{
    char path[256];
    inside(path, sizeof path, name);
    FILE *file = fopen(path, "rb");
    assert(file && !fseek(file, 0, SEEK_END));
    long size = ftell(file);
    assert(size >= 0 && !fseek(file, 0, SEEK_SET));
    char *text = xmalloc((size_t)size + 1);
    assert(fread(text, 1, (size_t)size, file) == (size_t)size && !fclose(file));
    text[size] = '\0';
    return text;
}

TEST_CASE(model_exports_write_indexed_meshes)
{
    ball();
    char destination[256], error[256] = { 0 };
    inside(destination, sizeof destination, "bundle");
    model_export_options options = { .bake = model_bake_defaults() };
    options.bake.cell_size = .25f;
    model_export_summary summary = { 0 };
    REQUIRE(
        model_export_bundle(directory, "Ball", destination, &options, &summary, error, sizeof error)
    );
    /* The bundle folder serves as the content folder of its `.mesh` file. */
    REQUIRE(authoring_mesh_mount_content(destination, error, sizeof error));
    const render_mesh_asset *asset = authoring_mesh_prepare("content/model.mesh", error, sizeof error);
    REQUIRE(asset && asset->has_part_coordinates && summary.triangles > 0);
    REQUIRE(acount(asset->indices) == summary.triangles * 3);
    int covered = 0;
    for (int i = 0; i < acount(asset->groups); i++) {
        REQUIRE(asset->groups[i].first == covered);
        covered += asset->groups[i].count;
    }
    REQUIRE(covered == acount(asset->indices));
    for (int i = 0; i < acount(asset->vertices); i++) {
        const render_mesh_surface_vertex *v = &asset->vertices[i];
        REQUIRE(fabsf(cf_len_v3(v->position) - 1) < .1f);
        CF_V3 side = { v->tangent.x, v->tangent.y, v->tangent.z };
        REQUIRE(near(cf_len_v3(side), 1) && fabsf(cf_dot_v3(side, v->normal)) < 1e-4f);
    }
    /* OBJ faces name the same vertices, one-based. */
    char *obj = bundle_text("bundle/model.obj");
    int faces = 0;
    for (const char *line = strstr(obj, "\nf "); line; line = strstr(line + 1, "\nf ")) {
        faces++;
    }
    const int *corner = asset->indices;
    char first[96];
    snprintf(
        first,
        sizeof first,
        "\nf %d/%d/%d %d/%d/%d %d/%d/%d\n",
        corner[0] + 1,
        corner[0] + 1,
        corner[0] + 1,
        corner[1] + 1,
        corner[1] + 1,
        corner[1] + 1,
        corner[2] + 1,
        corner[2] + 1,
        corner[2] + 1
    );
    REQUIRE(faces == summary.triangles && strstr(obj, first) && strstr(obj, "usemtl material_0"));
    free(obj);
    char *provenance = bundle_text("bundle/provenance.json");
    REQUIRE(strstr(provenance, "\"obj_group\": \"part_0\"") && strstr(provenance, "\"count\""));
    free(provenance);
    /* An existing bundle is kept. */
    REQUIRE(
        !model_export_bundle(directory, "Ball", destination, &options, NULL, error, sizeof error)
    );
    REQUIRE(strstr(error, "new directory"));
    /* A bake never makes more triangles than a mesh file holds. */
    inside(destination, sizeof destination, "larger");
    options.bake.max_triangles = MODEL_BAKE_TRIANGLE_MAX + 1;
    REQUIRE(
        !model_export_bundle(directory, "Ball", destination, &options, NULL, error, sizeof error)
    );
    char expected[64];
    snprintf(expected, sizeof expected, "triangles 1..%d", MODEL_BAKE_TRIANGLE_MAX);
    REQUIRE(strstr(error, expected));
    return true;
}

/* The largest mesh a bake can write: separate triangles with part
 * coordinates, in the most groups a mesh holds. One more triangle is refused. */
TEST_CASE(mesh_files_hold_the_largest_bake)
{
    mesh.has_part_coordinates = true;
    mesh.max = (CF_V3){ 1000, 1000, 0 };
    for (int t = 0; t < MODEL_BAKE_TRIANGLE_MAX + 1; t++) {
        float x = (float)(t % 999), y = (float)(t / 999);
        CF_V3 corners[] = { { x, y, 0 }, { x + 1, y, 0 }, { x, y + 1, 0 } };
        for (int j = 0; j < 3; j++) {
            render_mesh_surface_vertex v = {
                .position = corners[j],
                .normal = { 0, 0, 1 },
                .tangent = { 1, 0, 0, 1 },
                .part_position = corners[j],
                .part_from_object = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }
            };
            apush(mesh.indices, acount(mesh.vertices));
            apush(mesh.vertices, v);
        }
    }
    int share = MODEL_BAKE_TRIANGLE_MAX / RENDER_MESH_PREPARED_GROUP_MAX * 3;
    for (int i = 0; i < RENDER_MESH_PREPARED_GROUP_MAX; i++) {
        render_mesh_group group = { .first = i * share,
                                    .count = share,
                                    .part = -1,
                                    .asset = xstrdup(""),
                                    .name = xstrdup(""),
                                    .path = xstrdup(""),
                                    .material = xstrdup(""),
                                    .material_scope = xstrdup("") };
        apush(mesh.groups, group);
    }
    render_mesh_group *last = &mesh.groups[RENDER_MESH_PREPARED_GROUP_MAX - 1];
    last->count = acount(mesh.indices) - last->first;
    char error[256] = { 0 };
    REQUIRE(!render_mesh_asset_prepared_write(&mesh, &bytes, &length, error, sizeof error));
    REQUIRE(strstr(error, "record count limit"));
    last->count -= 3;
    asetlen(mesh.indices, acount(mesh.indices) - 3);
    asetlen(mesh.vertices, acount(mesh.vertices) - 3);
    REQUIRE(render_mesh_asset_prepared_write(&mesh, &bytes, &length, error, sizeof error));
    render_mesh_asset read = { 0 };
    size_t budget = SIZE_MAX;
    bool ok = render_mesh_asset_prepared_read_budget(bytes, length, &budget, &read, error, sizeof error);
    int corners = acount(read.indices);
    render_mesh_asset_free(&read);
    REQUIRE(ok && corners == MODEL_BAKE_TRIANGLE_MAX * 3);
    return true;
}

/* A mesh's pick tree lasts until the catalog changes. A publish, a clear and
 * an exchange each free every tree, so a mesh the catalog drops keeps none. */
TEST_CASE(mesh_pick_trees_last_until_the_catalog_changes)
{
    REQUIRE(picks_quad("content/meshes/a.mesh"));
    REQUIRE(render_mesh_pick_trees() == 1);
    quad();
    render_mesh_asset_publish("content/meshes/b.mesh", &mesh);
    REQUIRE(!render_mesh_pick_trees());
    REQUIRE(picks_quad("content/meshes/a.mesh"));
    render_mesh_assets_clear();
    REQUIRE(!render_mesh_pick_trees());
    REQUIRE(picks_quad("content/meshes/a.mesh"));
    render_mesh_table *empty = render_mesh_table_new();
    render_mesh_table_exchange(empty);
    render_mesh_table_free(empty);
    REQUIRE(!render_mesh_pick_trees());
    return true;
}

/* A mesh near float's range picks like one of ordinary size: a quad 2e20
 * across is hit by a ray from 1e20 above it, 1e20 away, though the products
 * of its edges pass float's range. */
TEST_CASE(meshes_near_the_float_limit_pick)
{
    quad();
    for (int i = 0; i < acount(mesh.vertices); i++) {
        mesh.vertices[i].position = cf_mul_v3_f(mesh.vertices[i].position, 1e20f);
    }
    mesh.min = cf_mul_v3_f(mesh.min, 1e20f);
    mesh.max = cf_mul_v3_f(mesh.max, 1e20f);
    render_mesh_asset_publish("content/meshes/vast.mesh", &mesh);
    char error[128];
    const render_mesh_asset
        *resident = render_mesh_asset_load("content/meshes/vast.mesh", error, sizeof error);
    REQUIRE(resident);
    CF_Ray3 down = { .p = { .5e20f, 1e20f, .5e20f }, .d = { 0, -1, 0 }, .t = 2e20f };
    REQUIRE(near(render_mesh_pick(resident, down).t / 1e20f, 1));
    return true;
}

void test_mesh_files()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(mesh_tangents_follow_texture_directions);
    RUN_TEST_CASE(mesh_tangents_hold_at_any_scale);
    RUN_TEST_CASE(mesh_files_prepare_once_from_mounted_content);
    RUN_TEST_CASE(mesh_files_come_from_drawn_meshes_and_mesh_colliders);
    RUN_TEST_CASE(mesh_files_refuse_invalid_triangles);
    RUN_TEST_CASE(mesh_files_refuse_unreadable_files);
    RUN_TEST_CASE(mesh_files_hold_the_largest_bake);
    RUN_TEST_CASE(model_exports_write_indexed_meshes);
    RUN_TEST_CASE(mesh_pick_trees_last_until_the_catalog_changes);
    RUN_TEST_CASE(meshes_near_the_float_limit_pick);
    pu_clear_setup();
}
