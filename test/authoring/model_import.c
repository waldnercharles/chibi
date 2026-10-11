#include "assets/model_import.h"

#include "assets/standard_material.h"
#include "graph/document/serialize.h"
#include "graph/shader/shader_graph.h"
#include "runtime/component_catalog.h"
#include "runtime/geometry/mesh_prepared.h"
#include "runtime/shaders/prepared.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"
#include "test/support/gltf_fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* glTF and GLB files imported into a content folder: the files an import
 * writes, how it names them, what it refuses and warns of, and the tangent
 * frames it bakes, all inside one temporary directory. */
static char directory[64];
static model_import_report report;
static char error[256];
static render_mesh_asset mesh;

/* The fixture's square, its texture's u along +x and its image's up, toward
 * v = 0, along +z. */
static const float POSITIONS[] = { -1, 0, -1, -1, 0, 1, 1, 0, 1, 1, 0, -1 };
static const float NORMALS[] = { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0 };
static const float UVS[] = { 0, 1, 0, 0, 1, 0, 1, 1 };
#define SQUARE GLTF_FIXTURE_SQUARE

static void inside(char *path, size_t size, const char *name)
{
    int n = snprintf(path, size, "%s/%s", directory, name);
    assert(n > 0 && (size_t)n < size);
}

static void setup()
{
    strcpy(directory, "/tmp/chibi-model-import-XXXXXX");
    assert(mkdtemp(directory));
    const char *folders[] = { "content", "other" };
    for (int i = 0; i < 2; i++) {
        char path[128];
        inside(path, sizeof path, folders[i]);
        assert(!mkdir(path, 0700));
    }
}

static void teardown()
{
    model_import_report_free(&report);
    render_mesh_asset_free(&mesh);
    mesh = (render_mesh_asset){ 0 };
    char command[128];
    snprintf(command, sizeof command, "rm -rf '%s'", directory);
    if (!directory[0] || system(command) != 0) {
        abort();
    }
}

static void square(gltf_fixture *f)
{
    gltf_fixture_square(f, UVS);
}

/* Writes the model as `file` beside the content folders, then imports it
 * into `folder` as `name`. */
static bool import_into(
    const char *folder,
    const gltf_fixture *f,
    const char *file,
    const char *members,
    const char *name
)
{
    char source[128], content[128];
    inside(source, sizeof source, file);
    inside(content, sizeof content, folder);
    assert(gltf_fixture_write(f, source, members));
    model_import_report_free(&report);
    return model_import(content, source, name, &report, error, sizeof error);
}

static bool import(const gltf_fixture *f, const char *file, const char *members, const char *name)
{
    return import_into("content", f, file, members, name);
}

/* The whole file at `relative`, owned by the caller, or NULL. */
static char *read_file(const char *relative, size_t *length)
{
    char path[256];
    inside(path, sizeof path, relative);
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    char *bytes = NULL;
    for (int c; (c = fgetc(file)) != EOF;) {
        apush(bytes, (char)c);
    }
    fclose(file);
    *length = (size_t)acount(bytes);
    apush(bytes, '\0');
    char *copy = xmalloc(*length + 1);
    memcpy(copy, bytes, *length + 1);
    afree(bytes);
    return copy;
}

static bool exists(const char *relative)
{
    char path[256];
    inside(path, sizeof path, relative);
    return access(path, F_OK) == 0;
}

static bool reads_mesh(const char *key)
{
    render_mesh_asset_free(&mesh);
    mesh = (render_mesh_asset){ 0 };
    size_t length = 0, budget = SIZE_MAX;
    char *bytes = read_file(key, &length);
    bool read = bytes &&
                render_mesh_asset_prepared_read_budget(bytes, length, &budget, &mesh, error, sizeof error);
    free(bytes);
    return read;
}

static bool wrote(const char *const *keys, int count)
{
    if (acount(report.written) != count) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (strcmp(report.written[i], keys[i])) {
            return false;
        }
    }
    return true;
}

static bool warned(const char *const *warnings, int count)
{
    if (acount(report.warnings) != count) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (strcmp(report.warnings[i], warnings[i])) {
            return false;
        }
    }
    return true;
}

/* Whether the file at `key` is the Standard material `material` named
 * `name`, as chibi_ctl --format writes it. */
static bool holds_material(const char *key, const char *name, const standard_material *material)
{
    behavior_asset *asset = standard_material_build(name, material);
    char *saved = behavior_asset_save(asset);
    behavior_asset_free(asset);
    char reason[128];
    char *expected = asset_text_to_disk(saved, reason, sizeof reason);
    free(saved);
    size_t length = 0;
    char *text = read_file(key, &length);
    bool same = text && expected && !strcmp(text, expected);
    free(text);
    free(expected);
    return same;
}

static standard_material factors()
{
    return (standard_material){ .base_color = { 1, 1, 1, 1 },
                                .metallic = 1,
                                .roughness = 1,
                                .normal_scale = 1,
                                .occlusion_strength = 1,
                                .alpha_mode = SHADER_ALPHA_OPAQUE,
                                .alpha_cutoff = .5f };
}

static bool near_v3(CF_V3 a, CF_V3 b)
{
    return cf_len_v3(cf_sub_v3(a, b)) < 1e-4f;
}

static CF_V3 tangent_of(const render_mesh_surface_vertex *v)
{
    return (CF_V3){ v->tangent.x, v->tangent.y, v->tangent.z };
}

/* Where an OpenGL normal image's green tilts: against the bitangent,
 * cross(normal, tangent) * w, which points toward +v. */
static CF_V3 green_of(const render_mesh_surface_vertex *v)
{
    return cf_mul_v3_f(cf_cross_v3(v->normal, tangent_of(v)), -v->tangent.w);
}

/* Whether every vertex has `tangent` and tilts green toward `green`, and
 * every triangle winds counterclockwise around its vertex normals. */
static bool frames_are(CF_V3 tangent, CF_V3 green)
{
    for (int i = 0; i < acount(mesh.vertices); i++) {
        const render_mesh_surface_vertex *v = &mesh.vertices[i];
        if (!near_v3(tangent_of(v), tangent) || !near_v3(green_of(v), green)) {
            return false;
        }
    }
    for (int i = 0; i < acount(mesh.indices); i += 3) {
        const render_mesh_surface_vertex *a = &mesh.vertices[mesh.indices[i]],
                                         *b = &mesh.vertices[mesh.indices[i + 1]],
                                         *c = &mesh.vertices[mesh.indices[i + 2]];
        CF_V3 face = cf_cross_v3(
            cf_sub_v3(b->position, a->position),
            cf_sub_v3(c->position, a->position)
        );
        if (cf_dot_v3(face, a->normal) <= 0) {
            return false;
        }
    }
    return true;
}

static int entries(const char *relative)
{
    char path[128];
    inside(path, sizeof path, relative);
    DIR *folder = opendir(path);
    assert(folder);
    int count = 0;
    for (struct dirent *entry; (entry = readdir(folder));) {
        count += strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..");
    }
    closedir(folder);
    return count;
}

static const char CRATE[] =
    "\"extensionsUsed\":[\"KHR_materials_emissive_strength\"],"
    "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
    "\"nodes\":[{\"name\":\"Root\",\"translation\":[0,0,5],\"children\":[1]},"
    "{\"mesh\":0,\"scale\":[2,2,2]}],"
    "\"meshes\":[{\"primitives\":[{" SQUARE ",\"material\":0},{" SQUARE
    ",\"material\":1},{" SQUARE "}]}],"
    "\"materials\":[{\"name\":\"Painted Wood\",\"pbrMetallicRoughness\":{"
    "\"baseColorFactor\":[0.8,0.6,0.4,1],\"baseColorTexture\":{\"index\":0},"
    "\"metallicFactor\":0,\"roughnessFactor\":0.5}},"
    "{\"alphaMode\":\"MASK\",\"alphaCutoff\":0.25,\"pbrMetallicRoughness\":{"
    "\"baseColorFactor\":[1,1,1,0.5]},"
    "\"emissiveFactor\":[0.1,0.2,0.3],"
    "\"extensions\":{\"KHR_materials_emissive_strength\":{\"emissiveStrength\":"
    "2}}},"
    "{\"name\":\"Unused\"}],"
    "\"textures\":[{\"source\":0,\"sampler\":0}],"
    "\"samplers\":[{\"magFilter\":9728,\"minFilter\":9986,\"wrapS\":33071,"
    "\"wrapT\":33071}]";

static const char *const CRATE_FILES[] = {
    "content/textures/Crate/albedo.png",
    "content/materials/Crate_Painted_Wood.kdl",
    "content/materials/Crate_Material1.kdl",
    "content/materials/Crate_Default.kdl",
    "content/meshes/Crate.mesh",
    "content/prototypes/Crate.kdl"
};

static void crate(gltf_fixture *f)
{
    square(f);
    gltf_fixture_png(f, "albedo.png", 2, 2, (const uint8_t[]){ 200, 120, 60, 255 });
}

/* Each material a mesh node uses becomes one Standard material, a group of
 * the one baked mesh in glTF order with the default material last, and its
 * images are copied as they are. A material no mesh uses writes nothing. */
TEST_CASE(model_imports_write_each_part_once)
{
    gltf_fixture f = { 0 };
    crate(&f);
    REQUIRE(import(&f, "crate.gltf", CRATE, "Crate"));
    REQUIRE(wrote(CRATE_FILES, 6) && !acount(report.warnings));
    size_t length = 0;
    char *image = read_file("content/textures/Crate/albedo.png", &length);
    REQUIRE(image && length == f.images[0].length && !memcmp(image, f.images[0].bytes, length));
    free(image);

    standard_material wood = factors();
    wood.base_color[0] = .8f;
    wood.base_color[1] = .6f;
    wood.base_color[2] = .4f;
    wood.metallic = 0;
    wood.roughness = .5f;
    wood.textures[STANDARD_TEXTURE_BASE_COLOR] = (standard_texture){
        .image = "content/textures/Crate/albedo.png",
        .filter = SHADER_IMAGE_NEAREST,
        .wrap = SHADER_IMAGE_CLAMP,
        .scale = { 1, 1 }
    };
    REQUIRE(
        holds_material("content/materials/Crate_Painted_Wood.kdl", "Crate_Painted_Wood", &wood)
    );
    standard_material glow = factors();
    glow.base_color[3] = .5f;
    glow.emission[0] = .1f * 2;
    glow.emission[1] = .2f * 2;
    glow.emission[2] = .3f * 2;
    glow.alpha_mode = SHADER_ALPHA_MASK;
    glow.alpha_cutoff = .25f;
    REQUIRE(holds_material("content/materials/Crate_Material1.kdl", "Crate_Material1", &glow));
    standard_material plain = factors();
    REQUIRE(holds_material("content/materials/Crate_Default.kdl", "Crate_Default", &plain));

    REQUIRE(reads_mesh("content/meshes/Crate.mesh"));
    REQUIRE(acount(mesh.vertices) == 12 && acount(mesh.groups) == 3);
    static const char *const scopes[] = { "Painted_Wood", "Material1", "Default" };
    for (int i = 0; i < 3; i++) {
        const render_mesh_group *group = &mesh.groups[i];
        char material[64];
        snprintf(material, sizeof material, "Crate_%s", scopes[i]);
        REQUIRE(group->first == i * 6 && group->count == 6 && group->part == -1);
        REQUIRE(
            !strcmp(group->asset, "Crate") && !strcmp(group->name, scopes[i]) &&
            !*group->path
        );
        REQUIRE(
            !strcmp(group->material, material) &&
            !strcmp(group->material_scope, scopes[i])
        );
    }
    /* The root's offset and the mesh node's scale are baked in. */
    REQUIRE(near_v3(mesh.min, (CF_V3){ -2, 0, 3 }) && near_v3(mesh.max, (CF_V3){ 2, 0, 7 }));
    REQUIRE(near_v3(mesh.vertices[0].position, (CF_V3){ -2, 0, 3 }));

    char *prototype = read_file("content/prototypes/Crate.kdl", &length);
    REQUIRE(
        prototype && !strcmp(
                         prototype,
                         "name \"Crate\"\n"
                         "components {\n"
                         "    Geometry3D depth=1.0 height=1.0 width=1.0\n"
                         "    MeshAsset source=\"content/meshes/Crate.mesh\"\n"
                         "    Transform3D {\n"
                         "        rotation w=1.0\n"
                         "        scale x=1.0 y=1.0 z=1.0\n"
                         "    }\n"
                         "}\n"
                     )
    );
    free(prototype);

    /* Nor does one only a node scaled to nothing draws, whatever it holds. */
    static const char
        hidden[] = "\"nodes\":[{\"mesh\":0},{\"mesh\":1,\"scale\":[0,0,0]}],"
                   "\"meshes\":[{\"primitives\":[{" SQUARE "}]},"
                   "{\"primitives\":[{" SQUARE ",\"material\":0}]}],"
                   "\"textures\":[{\"source\":0}],\"materials\":[{"
                   "\"name\":\"Gone\","
                   "\"normalTexture\":{\"index\":0,\"texCoord\":1}}]";
    REQUIRE(import(&f, "hidden.gltf", hidden, "Hidden"));
    static const char *const shown[] = { "content/materials/Hidden_Default.kdl",
                                         "content/meshes/Hidden.mesh",
                                         "content/prototypes/Hidden.kdl" };
    REQUIRE(wrote(shown, 3) && !acount(report.warnings));
    gltf_fixture_free(&f);
    return true;
}

/* A .glb that holds the same model writes the same files. */
TEST_CASE(model_imports_read_binary_files_alike)
{
    gltf_fixture f = { 0 };
    crate(&f);
    REQUIRE(import(&f, "crate.gltf", CRATE, "Crate"));
    REQUIRE(import_into("other", &f, "crate.glb", CRATE, "Crate"));
    for (int i = 0; i < 6; i++) {
        char other[128];
        snprintf(other, sizeof other, "other/%s", CRATE_FILES[i] + strlen("content/"));
        size_t length = 0, other_length = 0;
        char *bytes = read_file(CRATE_FILES[i], &length),
             *other_bytes = read_file(other, &other_length);
        REQUIRE(
            bytes && other_bytes && length == other_length &&
            !memcmp(bytes, other_bytes, length)
        );
        free(bytes);
        free(other_bytes);
    }
    gltf_fixture_free(&f);
    return true;
}

/* Names keep only the characters asset names allow, stay within their
 * length, and differ from each other in more than case, as image files do. */
TEST_CASE(model_imports_name_parts_apart)
{
    gltf_fixture f = { 0 };
    square(&f);
    gltf_fixture_png(&f, "Tex.png", 1, 1, (const uint8_t[]){ 255, 255, 255, 255 });
    gltf_fixture_png(&f, "tex.png", 1, 1, (const uint8_t[]){ 0, 0, 0, 255 });
    char members[2048], primitives[512] = "", materials[512] = "";
    static const char *const names[] = { "Wood", "wood", "Wood!", NULL, NULL };
    for (int i = 0; i < 5; i++) {
        char name[80];
        snprintf(name, sizeof name, "%s", names[i] ? names[i] : "");
        if (!names[i]) {
            memset(name, 'a', 70);
            snprintf(name + 70, sizeof name - 70, "%s", i == 3 ? "" : "b");
        }
        snprintf(
            primitives + strlen(primitives),
            sizeof primitives - strlen(primitives),
            "%s{" SQUARE ",\"material\":%d}",
            i ? "," : "",
            i
        );
        snprintf(
            materials + strlen(materials),
            sizeof materials - strlen(materials),
            "%s{\"name\":\"%s\"%s}",
            i ? "," : "",
            name,
            i < 2 ? (i ? ",\"pbrMetallicRoughness\":{\"baseColorTexture\":{"
                         "\"index\":1}}"
                       : ",\"pbrMetallicRoughness\":{\"baseColorTexture\":{"
                         "\"index\":0}}")
                  : ""
        );
    }
    snprintf(
        members,
        sizeof members,
        "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[%s]}],"
        "\"materials\":[%s],"
        "\"textures\":[{\"source\":0},{\"source\":1}]",
        primitives,
        materials
    );
    REQUIRE(import(&f, "box.gltf", members, "Box"));
    char long_name[96], long_second[96];
    snprintf(
        long_name,
        sizeof long_name,
        "content/materials/Box_%.59s.kdl",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    );
    snprintf(
        long_second,
        sizeof long_second,
        "content/materials/Box_%.57s_2.kdl",
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    );
    const char *const keys[] = { "content/textures/Box/Tex.png",
                                 "content/textures/Box/tex_2.png",
                                 "content/materials/Box_Wood.kdl",
                                 "content/materials/Box_wood_2.kdl",
                                 "content/materials/Box_Wood_.kdl",
                                 long_name,
                                 long_second,
                                 "content/meshes/Box.mesh",
                                 "content/prototypes/Box.kdl" };
    REQUIRE(wrote(keys, 9));
    REQUIRE(strlen(long_name) - strlen("content/materials/.kdl") == 63);
    REQUIRE(strlen(long_second) - strlen("content/materials/.kdl") == 63);
    gltf_fixture_free(&f);
    return true;
}

/* A model named as long as a name may be keeps room in each material's name
 * for the material's own: as much of the model's name as fits, then the
 * material's. */
TEST_CASE(model_imports_name_materials_of_the_longest_models)
{
    gltf_fixture f = { 0 };
    square(&f);
    /* As long as NEW_ASSET_NAME_MAX allows. */
    char name[64] = { 0 };
    memset(name, 'L', sizeof name - 1);
    REQUIRE(import(
        &f,
        "long.gltf",
        "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{" SQUARE
        ",\"material\":0}]}],\"materials\":[{\"name\":\"Wood\"}]",
        name
    ));
    char material[128], mesh_key[128], prototype[128];
    snprintf(material, sizeof material, "content/materials/%.47s_Wood.kdl", name);
    snprintf(mesh_key, sizeof mesh_key, "content/meshes/%s.mesh", name);
    snprintf(prototype, sizeof prototype, "content/prototypes/%s.kdl", name);
    const char *const keys[] = { material, mesh_key, prototype };
    REQUIRE(wrote(keys, 3));
    REQUIRE(reads_mesh(mesh_key) && acount(mesh.groups) == 1);
    REQUIRE(!strcmp(mesh.groups[0].material_scope, "Wood"));
    gltf_fixture_free(&f);
    return true;
}

/* An import never replaces a file. When one is in its way it fails and
 * removes every file and folder it made, leaving what was there. */
TEST_CASE(model_imports_replace_nothing_and_undo_failures)
{
    gltf_fixture f = { 0 };
    crate(&f);
    REQUIRE(import(&f, "crate.gltf", CRATE, "Crate"));
    size_t length = 0;
    char *before = read_file("content/meshes/Crate.mesh", &length);
    REQUIRE(!import(&f, "crate.gltf", CRATE, "Crate"));
    REQUIRE(!strcmp(error, "content/textures/Crate/albedo.png already exists"));
    REQUIRE(!acount(report.written));
    size_t after_length = 0;
    char *after = read_file("content/meshes/Crate.mesh", &after_length);
    REQUIRE(before && after && length == after_length && !memcmp(before, after, length));
    free(before);
    free(after);

    char path[128];
    inside(path, sizeof path, "content/prototypes/Late.kdl");
    FILE *file = fopen(path, "wb");
    REQUIRE(file && fputs("keep\n", file) >= 0 && !fclose(file));
    REQUIRE(!import(&f, "crate.gltf", CRATE, "Late"));
    REQUIRE(!strcmp(error, "content/prototypes/Late.kdl already exists"));
    REQUIRE(!exists("content/textures/Late") && !exists("content/materials/Late_Default.kdl"));
    REQUIRE(!exists("content/meshes/Late.mesh") && exists("content/meshes/Crate.mesh"));
    char *kept = read_file("content/prototypes/Late.kdl", &length);
    REQUIRE(kept && !strcmp(kept, "keep\n"));
    free(kept);
    gltf_fixture_free(&f);
    return true;
}

/* Writes `text` as the file at `relative`, making its folder. */
static bool put(const char *relative, const char *text)
{
    char path[128];
    inside(path, sizeof path, relative);
    char *slash = strrchr(path, '/');
    *slash = 0;
    bool folder = !mkdir(path, 0700) || errno == EEXIST;
    *slash = '/';
    FILE *file = folder ? fopen(path, "wb") : NULL;
    bool written = file && fputs(text, file) >= 0;
    return file && !fclose(file) && written;
}

/* Writes the content file `relative`, declaring `name`, in its folder. */
static bool declare(const char *relative, const char *name)
{
    char text[96];
    snprintf(text, sizeof text, "name \"%s\"\n", name);
    return put(relative, text);
}

/* An import declares no name that another file in the project declares,
 * since preparing the project refuses two declarations of one name: not a
 * material's among native graphs of any kind, nor its prototype's. It
 * then writes nothing, as it does when it cannot read a folder of
 * declarations. */
TEST_CASE(model_imports_refuse_names_the_project_declares)
{
    gltf_fixture f = { 0 };
    crate(&f);
    REQUIRE(declare("content/shapes/Plank.kdl", "Crate_Painted_Wood"));
    REQUIRE(!import(&f, "crate.gltf", CRATE, "Crate"));
    REQUIRE(!strcmp(error, "content/shapes/Plank.kdl already declares Crate_Painted_Wood"));
    REQUIRE(entries("content") == 1 && !acount(report.written));
    REQUIRE(declare("content/shapes/Plank.kdl", "Plank"));
    REQUIRE(declare("content/prototypes/Box.kdl", "Crate"));
    REQUIRE(!import(&f, "crate.gltf", CRATE, "Crate"));
    REQUIRE(!strcmp(error, "content/prototypes/Box.kdl already declares Crate"));
    REQUIRE(entries("content") == 2 && entries("content/prototypes") == 1);
    REQUIRE(declare("content/prototypes/Box.kdl", "Box"));
    /* A file or folder it cannot read could hide a clash, so it refuses
     * then too. A file that declares no name hides none. */
    REQUIRE(put("content/events/Broken.kdl", "name \"Broken\n"));
    REQUIRE(put("content/events/Nameless.kdl", "event\n"));
    REQUIRE(!import(&f, "crate.gltf", CRATE, "Crate"));
    REQUIRE(!strcmp(error, "Cannot read content/events/Broken.kdl: line 1, column 6: Parse error"));
    REQUIRE(entries("content") == 3 && entries("content/prototypes") == 1);
    REQUIRE(declare("content/events/Broken.kdl", "Broken"));
    char fields[128];
    inside(fields, sizeof fields, "content/fields");
    REQUIRE(declare("content/fields", "Fields"));
    REQUIRE(!import(&f, "crate.gltf", CRATE, "Crate"));
    REQUIRE(!strcmp(error, "Cannot read content/fields: Not a directory"));
    REQUIRE(entries("content") == 4 && entries("content/prototypes") == 1);
    REQUIRE(!unlink(fields));
    REQUIRE(import(&f, "crate.gltf", CRATE, "Crate"));
    gltf_fixture_free(&f);
    return true;
}

/* An import refuses materials that would bring the native graphs, every
 * kind but prototypes and scenes, past what a project can prepare. */
TEST_CASE(model_imports_refuse_more_native_graphs_than_a_project_prepares)
{
    gltf_fixture f = { 0 };
    crate(&f);
    static const char *const folders[] = { "shader-libraries", "events",
                                           "fields",           "materials",
                                           "shapes",           "models" };
    /* The crate writes three materials, one more than the catalog holds. */
    int existing = NATIVE_GRAPH_DOCUMENTS_MAX - 2;
    bool declared = declare("content/prototypes/Box.kdl", "Box");
    for (int i = 0; i < existing; i++) {
        char key[96], name[16];
        snprintf(name, sizeof name, "Graph%d", i);
        snprintf(key, sizeof key, "content/%s/%s.kdl", folders[i % 6], name);
        declared = declare(key, name) && declared;
    }
    REQUIRE(declared);
    REQUIRE(!import(&f, "crate.gltf", CRATE, "Crate"));
    REQUIRE(
        !strcmp(error, "The content holds 1022 native graphs, and 3 more materials would pass the 1024 a project can prepare")
    );
    REQUIRE(!acount(report.written) && !exists("content/prototypes/Crate.kdl"));
    char first[128];
    inside(first, sizeof first, "content/shader-libraries/Graph0.kdl");
    REQUIRE(!unlink(first));
    REQUIRE(import(&f, "crate.gltf", CRATE, "Crate"));
    gltf_fixture_free(&f);
    return true;
}

/* Why importing the model at `file` as Bad failed, or what went wrong
 * instead: it imported, or left files behind. */
static const char *refused(const char *file)
{
    char source[128], content[128];
    inside(source, sizeof source, file);
    inside(content, sizeof content, "content");
    model_import_report_free(&report);
    if (model_import(content, source, "Bad", &report, error, sizeof error)) {
        return "imported";
    }
    return entries("content") ? "left files" : error;
}

/* Copies the model `from` to `to` with its file `old` named `renamed`. */
static bool rename_uri(const char *from, const char *to, const char *old, const char *renamed)
{
    size_t length = 0;
    char *text = read_file(from, &length);
    char quoted[160], path[128];
    snprintf(quoted, sizeof quoted, "\"uri\":\"%s\"", old);
    char *uri = text ? strstr(text, quoted) : NULL;
    inside(path, sizeof path, to);
    FILE *file = uri ? fopen(path, "wb") : NULL;
    if (!file) {
        free(text);
        return false;
    }
    int before = (int)(uri - text);
    const char *after = uri + strlen(quoted);
    bool written = fprintf(file, "%.*s\"uri\":\"%s\"%s", before, text, renamed, after) > 0;
    written = !fclose(file) && written;
    free(text);
    return written;
}

static const char *refusal(const gltf_fixture *f, const char *members)
{
    char path[128];
    inside(path, sizeof path, "bad.gltf");
    assert(gltf_fixture_write(f, path, members));
    return refused("bad.gltf");
}

/* The square's members with material `material`, using the texture of the
 * fixture's first image. */
static const char *textured(const char *before, const char *material)
{
    static char members[1024];
    snprintf(
        members,
        sizeof members,
        "%s\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{" SQUARE
        ",\"material\":0}]}],"
        "\"textures\":[{\"source\":0}],\"materials\":[%s]",
        before,
        material
    );
    return members;
}

/* One texture slot, with the extension object supplied verbatim. */
static const char *transformed_material(int slot, const char *transform)
{
    static const char *const keys[] = { "baseColorTexture",
                                        "metallicRoughnessTexture",
                                        "normalTexture",
                                        "occlusionTexture",
                                        "emissiveTexture" };
    static char material[1024];
    bool pbr = slot <= STANDARD_TEXTURE_METALLIC_ROUGHNESS;
    snprintf(
        material,
        sizeof material,
        "{\"name\":\"M\",%s\"%s\":{\"index\":0%s%s%s}%s}",
        pbr ? "\"pbrMetallicRoughness\":{" : "",
        keys[slot],
        transform ? ",\"extensions\":{\"KHR_texture_transform\":" : "",
        transform ? transform : "",
        transform ? "}" : "",
        pbr ? "}" : ""
    );
    return material;
}

/* Inspect the saved graph independently of the Standard material builder:
 * exactly one mapping feeds this image's UV, while Normal Map keeps the
 * mesh frame through its own unconnected UV input. */
static bool holds_mapping(const char *key, const standard_texture *expected)
{
    size_t length = 0;
    char *text = read_file(key, &length);
    const registry_pack **previous = NULL;
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &SHADER_PACK };
    registry_set_packs(packs, 1);
    behavior_asset *asset = text ? behavior_asset_load(text) : NULL;
    if (text && !asset) {
        fprintf(stderr, "Cannot reload imported material %s: %s\n", key, graph_last_error());
    }
    shader_graph lowered;
    shader_graph_init(&lowered);
    shader_error diagnostic = { 0 };
    bool compiled = asset &&
                    shader_graph_from_graph(asset->graph, &lowered, &diagnostic);
    if (asset && !compiled) {
        fprintf(
            stderr,
            "Cannot lower imported material %s: %s\n",
            key,
            diagnostic.message
        );
    }
    registry_set_packs(previous, acount(previous));
    afree(previous);
    free(text);
    if (!asset) {
        shader_graph_free(&lowered);
        return false;
    }
    graph *g = asset->graph;
    node *mapping = NULL, *image = NULL, *normal = NULL;
    int mappings = 0, images = 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        node *n = g->nodes[i];
        if (!strcmp(n->desc->name, "Shader Mapping")) {
            mapping = n;
            mappings++;
        }
        if (!strcmp(n->desc->name, "Shader Image Texture")) {
            image = n;
            images++;
        }
        if (!strcmp(n->desc->name, "Shader Normal Map")) {
            normal = n;
        }
    }
    bool valid = compiled && mappings == 1 && images == 1;
    if (valid) {
        const float *offset = mapping->input_pins[1].default_value.ptr;
        const float *scale = mapping->input_pins[3].default_value.ptr;
        valid = offset && scale && fabsf(offset[0] - expected->offset[0]) < 1e-6f &&
                fabsf(offset[1] - expected->offset[1]) < 1e-6f &&
                fabsf(scale[0] - expected->scale[0]) < 1e-6f &&
                fabsf(scale[1] - expected->scale[1]) < 1e-6f &&
                fabsf(
                    mapping->input_pins[2].default_value.f - expected->rotation * 57.29577951308232f
                ) < 1e-5f;
        int routes = 0;
        for (int i = 0; i < acount(g->edges); i++) {
            const edge *e = &g->edges[i];
            routes += e->from_id == mapping->id && e->to_id == image->id &&
                      e->to_pin_id == image->input_pins[SHADER_IMAGE_INPUT_UV].id;
            if (normal && e->to_id == normal->id &&
                e->to_pin_id == normal->input_pins[1].id) {
                valid = false;
            }
        }
        valid = valid && routes == 1 &&
                (!normal || normal->input_pins[4].default_value.i ==
                                SHADER_NORMAL_OPENGL_TANGENT);
    }
    if (normal) {
        int normal_maps = 0;
        for (int i = 0; i < acount(lowered.nodes); i++) {
            const shader_node *n = &lowered.nodes[i];
            if (n->op == SHADER_NORMAL_MAP) {
                normal_maps++;
                valid = valid && n->value[0] == SHADER_NORMAL_OPENGL_TANGENT;
            }
        }
        valid = valid && normal_maps == 1;
    }
    shader_graph_free(&lowered);
    behavior_asset_free(asset);
    return valid;
}

/* Each of the five PBR slots owns its transform. Binary and JSON containers
 * produce identical files, including the serialized mapping graph. */
TEST_CASE(model_imports_keep_each_texture_transform)
{
    gltf_fixture f = { 0 };
    square(&f);
    gltf_fixture_png(&f, "albedo.png", 1, 1, (const uint8_t[]){ 180, 90, 30, 255 });
    for (int slot = 0; slot < STANDARD_TEXTURE_COUNT; slot++) {
        char name[32], file[40], transform[128], material_key[96],
            material_name[40], image_key[96];
        snprintf(name, sizeof name, "Transform%d", slot);
        snprintf(file, sizeof file, "%s.gltf", name);
        float offset = .125f * (slot + 1), scale = .5f * (slot + 1);
        snprintf(
            transform,
            sizeof transform,
            "{\"offset\":[%g,-0.25],\"scale\":[%g,0.75],\"rotation\":0.5}",
            offset,
            scale
        );
        const char *members = textured(
            "\"extensionsRequired\":[\"KHR_texture_transform\"],",
            transformed_material(slot, transform)
        );
        REQUIRE(import(&f, file, members, name));
        REQUIRE(!acount(report.warnings));
        snprintf(material_key, sizeof material_key, "content/materials/%s_M.kdl", name);
        snprintf(material_name, sizeof material_name, "%s_M", name);
        snprintf(image_key, sizeof image_key, "content/textures/%s/albedo.png", name);
        standard_material expected = factors();
        expected.textures[slot] = (standard_texture){ .image = image_key,
                                                      .filter = SHADER_IMAGE_BILINEAR,
                                                      .wrap = SHADER_IMAGE_REPEAT,
                                                      .offset = { offset, -.25f },
                                                      .scale = { scale, .75f },
                                                      .rotation = .5f };
        REQUIRE(holds_material(material_key, material_name, &expected));
        REQUIRE(holds_mapping(material_key, &expected.textures[slot]));
        char *keys[4] = { 0 };
        REQUIRE(acount(report.written) == 4);
        for (int i = 0; i < 4; i++) {
            keys[i] = xstrdup(report.written[i]);
        }
        snprintf(file, sizeof file, "%s.glb", name);
        REQUIRE(import_into("other", &f, file, members, name));
        for (int i = 0; i < 4; i++) {
            char other[128];
            snprintf(other, sizeof other, "other/%s", keys[i] + strlen("content/"));
            size_t n = 0, m = 0;
            char *a = read_file(keys[i], &n), *b = read_file(other, &m);
            REQUIRE(a && b && n == m && !memcmp(a, b, n));
            free(a);
            free(b);
            free(keys[i]);
        }
    }
    gltf_fixture_free(&f);
    return true;
}

/* Missing fields use identity; explicit zero and negative scales are data,
 * rather than absent/default sentinels. */
TEST_CASE(model_imports_keep_default_and_signed_texture_scales)
{
    static const struct
    {
        const char *name, *transform;
        float x, y;
    } cases[] = {
        { "Missing", NULL, 1, 1 },
        { "Empty", "{}", 1, 1 },
        { "Identity", "{\"offset\":[0,0],\"scale\":[1,1],\"rotation\":0}", 1, 1 },
        { "Negative", "{\"scale\":[-2,0.5]}", -2, .5f },
        { "Zero", "{\"scale\":[0,0]}", 0, 0 },
    };
    gltf_fixture f = { 0 };
    square(&f);
    gltf_fixture_png(&f, "albedo.png", 1, 1, (const uint8_t[]){ 255, 255, 255, 255 });
    for (int i = 0; i < (int)(sizeof cases / sizeof *cases); i++) {
        char file[40], key[96], name[40], image[96];
        snprintf(file, sizeof file, "%s.gltf", cases[i].name);
        REQUIRE(import(
            &f,
            file,
            textured("", transformed_material(0, cases[i].transform)),
            cases[i].name
        ));
        snprintf(key, sizeof key, "content/materials/%s_M.kdl", cases[i].name);
        snprintf(name, sizeof name, "%s_M", cases[i].name);
        snprintf(image, sizeof image, "content/textures/%s/albedo.png", cases[i].name);
        standard_material expected = factors();
        expected.textures[0] = (standard_texture){ .image = image,
                                                   .filter = SHADER_IMAGE_BILINEAR,
                                                   .wrap = SHADER_IMAGE_REPEAT,
                                                   .scale = { cases[i].x,
                                                              cases[i].y } };
        REQUIRE(holds_material(key, name, &expected));
    }
    gltf_fixture_free(&f);
    return true;
}

/* The square with `indices` and `positions`, or the square's own. */
static gltf_fixture shape(const uint16_t *indices, int count, const float *positions)
{
    gltf_fixture f = { 0 };
    if (!indices && !positions) {
        square(&f);
        return f;
    }
    static const uint16_t corners[] = { 0, 1, 2, 0, 2, 3 };
    gltf_fixture_floats(&f, positions ? positions : POSITIONS, 4, 3);
    gltf_fixture_floats(&f, NORMALS, 4, 3);
    gltf_fixture_floats(&f, UVS, 4, 2);
    gltf_fixture_indices(&f, indices ? indices : corners, indices ? count : 6);
    return f;
}

static const char
    PLAIN[] = "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{" SQUARE "}]}]";

static const uint8_t WHITE[4] = { 255, 255, 255, 255 };

/* Every folder an import writes into, the content folder's own included,
 * must be a folder and not a link to one, so nothing lands outside the
 * content. A refused import leaves the link's target as it was. */
TEST_CASE(model_imports_refuse_linked_folders)
{
    gltf_fixture f = { 0 };
    crate(&f);
    char link[128], target[128];
    inside(target, sizeof target, "other");
    inside(link, sizeof link, "content/materials");
    REQUIRE(!symlink(target, link));
    REQUIRE(!import(&f, "crate.gltf", CRATE, "Crate"));
    REQUIRE(strstr(error, "content/materials is not a folder"));
    REQUIRE(!entries("other") && entries("content") == 1);
    REQUIRE(!unlink(link));
    inside(link, sizeof link, "linked");
    REQUIRE(!symlink(target, link));
    REQUIRE(!import_into("linked", &f, "crate.gltf", CRATE, "Crate"));
    REQUIRE(strstr(error, "linked is not a folder") && !entries("other"));
    gltf_fixture_free(&f);
    return true;
}

/* Models the import would draw unlike glTF write nothing. */
TEST_CASE(model_imports_refuse_what_they_cannot_draw_as_authored)
{
    gltf_fixture f = shape(NULL, 0, NULL);
    gltf_fixture_png(&f, "albedo.png", 1, 1, WHITE);
    REQUIRE(
        !strcmp(refusal(&f, textured("\"extensionsRequired\":[\"KHR_draco_mesh_compression\"],", "{}")), "The model requires KHR_draco_mesh_compression, which import does not support")
    );
    const char *second = "{\"name\":\"M\",\"normalTexture\":{\"index\":0,"
                         "\"texCoord\":1}}";
    REQUIRE(
        !strcmp(refusal(&f, textured("", second)), "Material Bad_M's normalTexture reads TEXCOORD_1; import reads only TEXCOORD_0")
    );

    const char
        *strong = "{\"name\":\"M\",\"emissiveFactor\":[1,1,1],\"extensions\":{"
                  "\"KHR_materials_emissive_strength\":{"
                  "\"emissiveStrength\":1e39}}}";
    REQUIRE(
        !strcmp(refusal(&f, textured("", strong)), "Material Bad_M holds a number beyond float range")
    );
    gltf_fixture_free(&f);

    gltf_fixture gif = shape(NULL, 0, NULL);
    gltf_fixture_image(&gif, "albedo.png", "GIF89a\1\0\1\0\0\0\0;", 14);
    const char *glowing = "{\"emissiveTexture\":{\"index\":0}}";
    REQUIRE(
        !strcmp(refusal(&gif, textured("", glowing)), "Image 0 of the model: Expected a PNG or JPEG image")
    );
    gltf_fixture_free(&gif);
    gltf_fixture wide = shape(NULL, 0, NULL);
    gltf_fixture_png(&wide, "albedo.png", 2049, 1, WHITE);
    REQUIRE(
        !strcmp(refusal(&wide, textured("", glowing)), "Image 0 of the model: Image dimensions must be between 1 and 2048 pixels per side")
    );
    gltf_fixture_free(&wide);

    gltf_fixture odd = shape((const uint16_t[]){ 0, 1, 2, 3 }, 4, NULL);
    REQUIRE(!strcmp(refusal(&odd, PLAIN), "A primitive's 4 indices are not whole triangles"));
    gltf_fixture_free(&odd);
    gltf_fixture beyond = shape((const uint16_t[]){ 0, 1, 9 }, 3, NULL);
    REQUIRE(!strcmp(refusal(&beyond, PLAIN), "A primitive's index 2 names a missing vertex"));
    gltf_fixture_free(&beyond);
    gltf_fixture point = shape(NULL, 0, (const float[12]){ 0 });
    REQUIRE(!strcmp(refusal(&point, PLAIN), "The model has no triangles to import"));
    REQUIRE(strstr(
        refusal(&point, "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{" SQUARE ",\"mode\":1}]}]"),
        "only triangle primitives"
    ));
    gltf_fixture_free(&point);
    return true;
}

/* Invalid transform numbers and shapes fail before publishing any part,
 * even when a preceding material and image are valid. */
TEST_CASE(model_imports_refuse_malformed_texture_transforms_transactionally)
{
    static const char *const invalid[] = {
        "null",
        "[]",
        "1",
        "{\"offset\":null}",
        "{\"offset\":[0]}",
        "{\"offset\":[0,1,2]}",
        "{\"offset\":[\"0\",1]}",
        "{\"offset\":[false,1]}",
        "{\"offset\":[1e39,0]}",
        "{\"offset\":[0,-1e999]}",
        "{\"scale\":{}}",
        "{\"scale\":[1]}",
        "{\"scale\":[1,1,1]}",
        "{\"scale\":[1,null]}",
        "{\"scale\":[1,\"2\"]}",
        "{\"scale\":[1,1e39]}",
        "{\"rotation\":[]}",
        "{\"rotation\":null}",
        "{\"rotation\":true}",
        "{\"rotation\":\"0\"}",
        "{\"rotation\":1e39}",
        "{\"rotation\":-1e999}",
    };
    gltf_fixture f = shape(NULL, 0, NULL);
    gltf_fixture_png(&f, "albedo.png", 1, 1, WHITE);
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        char members[2048];
        snprintf(
            members,
            sizeof members,
            "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{" SQUARE
            ",\"material\":0},{" SQUARE ",\"material\":1}]}],"
            "\"textures\":[{\"source\":0}],\"materials\":[{\"name\":\"Good\","
            "\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}},%"
            "s]",
            transformed_material(STANDARD_TEXTURE_BASE_COLOR, invalid[i])
        );
        const char *reason = refusal(&f, members);
        REQUIRE(strcmp(reason, "imported") && strcmp(reason, "left files") && *reason);
        REQUIRE(!acount(report.written) && !entries("content"));
    }
    REQUIRE(
        !strcmp(refusal(&f, textured("", transformed_material(STANDARD_TEXTURE_NORMAL, "{\"texCoord\":1}"))), "Material Bad_M's normalTexture reads TEXCOORD_1; import reads only TEXCOORD_0")
    );
    REQUIRE(!strcmp(
        refusal(&f, textured("", transformed_material(STANDARD_TEXTURE_NORMAL, "{\"offset\":[3e38,0],\"scale\":[3e38,1]}"))),
        "Material Bad_M's normalTexture transforms UVs beyond float range"
    ));
    /* The extension's texCoord overrides the textureInfo texCoord. */
    REQUIRE(import(
        &f,
        "override.gltf",
        textured(
            "",
            "{\"name\":\"M\",\"normalTexture\":{\"index\":0,\"texCoord\":1,"
            "\"extensions\":{\"KHR_texture_transform\":{\"texCoord\":0}}}}"
        ),
        "Override"
    ));
    gltf_fixture_free(&f);
    return true;
}

/* Missing tangents use the normal image's transformed coordinates. Authored
 * glTF tangents keep their frame, and both paths preserve source mesh UVs
 * for the other material slots. */
TEST_CASE(model_imports_generate_transformed_normal_tangents)
{
    static const struct
    {
        const char *name, *transform;
        CF_V3 tangent, green;
    } cases[] = {
        { "Rotated",
          "{\"offset\":[0.25,-0.5],\"scale\":[2,0.5],\"rotation\":1."
          "5707963267948966}",
          { 0, 0, 1 },
          { -1, 0, 0 } },
        { "Reflected", "{\"scale\":[-2,0.5]}", { -1, 0, 0 }, { 0, 0, 1 } },
    };
    gltf_fixture f = shape(NULL, 0, NULL);
    gltf_fixture_png(&f, "albedo.png", 1, 1, WHITE);
    for (int i = 0; i < 2; i++) {
        char file[40], key[96];
        snprintf(file, sizeof file, "%s.gltf", cases[i].name);
        snprintf(key, sizeof key, "content/meshes/%s.mesh", cases[i].name);
        REQUIRE(import(
            &f,
            file,
            textured("", transformed_material(STANDARD_TEXTURE_NORMAL, cases[i].transform)),
            cases[i].name
        ));
        REQUIRE(reads_mesh(key) && frames_are(cases[i].tangent, cases[i].green));
        for (int v = 0; v < acount(mesh.vertices); v++) {
            REQUIRE(
                fabsf(mesh.vertices[v].uv.x - (mesh.vertices[v].position.x + 1) * .5f) < 1e-6f
            );
            REQUIRE(
                fabsf(mesh.vertices[v].uv.y - (1 - mesh.vertices[v].position.z) * .5f) < 1e-6f
            );
        }
    }
    int tangents = gltf_fixture_floats(
        &f,
        (const float[16]){ 0, 0, -2, 1, 0, 0, -2, 1, 0, 0, -2, 1, 0, 0, -2, 1 },
        4,
        4
    );
    char members[2048];
    snprintf(
        members,
        sizeof members,
        "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{"
        "\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,"
        "\"TANGENT\":%d},\"indices\":3,\"material\":0}]}],"
        "\"textures\":[{\"source\":0}],\"materials\":[%s]",
        tangents,
        transformed_material(STANDARD_TEXTURE_NORMAL, cases[0].transform)
    );
    REQUIRE(import(&f, "authored.gltf", members, "Authored"));
    REQUIRE(reads_mesh("content/meshes/Authored.mesh"));
    REQUIRE(frames_are((CF_V3){ 0, 0, -1 }, (CF_V3){ -1, 0, 0 }));
    for (int v = 0; v < acount(mesh.vertices); v++) {
        REQUIRE(fabsf(mesh.vertices[v].uv.x - (mesh.vertices[v].position.x + 1) * .5f) < 1e-6f);
        REQUIRE(fabsf(mesh.vertices[v].uv.y - (1 - mesh.vertices[v].position.z) * .5f) < 1e-6f);
    }
    gltf_fixture_free(&f);
    return true;
}

/* glTF normals decode in a unit orthogonal frame, independently of UV
 * density and shear. OpenGL green points against the positive-v direction. */
TEST_CASE(model_import_normal_convention_uses_unit_tangent_frames)
{
    shader_graph g;
    shader_graph_init(&g);
    int color = shader_graph_add(&g, SHADER_CONSTANT, 3);
    memcpy(g.nodes[color].value, (const float[]){ .75f, .75f, 1 }, 3 * sizeof(float));
    int uv = shader_graph_add(&g, SHADER_UV, 2);
    int strength = shader_graph_add(&g, SHADER_CONSTANT, 1);
    g.nodes[strength].value[0] = 1;
    int spacing = shader_graph_add(&g, SHADER_CONSTANT, 1);
    g.nodes[spacing].value[0] = .01f;
    int map = shader_graph_add(&g, SHADER_NORMAL_MAP, 3);
    memcpy(g.nodes[map].input, (const int[]){ color, uv, strength, spacing }, 4 * sizeof(int));
    g.nodes[map].value[0] = SHADER_NORMAL_OPENGL_TANGENT;
    g.output_count = 1;
    g.output[0] = map;
    unsigned char *bytes = NULL;
    size_t length = 0;
    shader_error diagnostic = { 0 };
    shader_graph restored;
    shader_graph_init(&restored);
    REQUIRE(shader_graph_prepared_write(&g, &bytes, &length, &diagnostic));
    REQUIRE(shader_graph_prepared_read(bytes, length, &restored, &diagnostic));
    free(bytes);
    REQUIRE(acount(restored.nodes) == acount(g.nodes));
    REQUIRE(restored.nodes[map].op == SHADER_NORMAL_MAP);
    REQUIRE(restored.nodes[map].value[0] == SHADER_NORMAL_OPENGL_TANGENT);
    static const float gradients[][2][3] = {
        { { 3, 0, 0 }, { 2, 0, -.25f } },
        { { 1e20f, 0, 0 }, { 1e19f, 0, -1e18f } },
        { { 3, 0, 0 }, { 2, 0, .25f } },
    };
    for (int i = 0; i < 3; i++) {
        shader_sample_context context = { .normal = { 0, 1, 0 } };
        memcpy(context.uv_from_world, gradients[i], sizeof context.uv_from_world);
        const shader_graph *graphs[] = { &g, &restored };
        for (int j = 0; j < 2; j++) {
            float output[SHADER_OUTPUT_MAX][4];
            REQUIRE(shader_graph_eval_outputs(graphs[j], &context, NULL, output, &diagnostic));
            float magnitude = sqrtf(1.5f);
            REQUIRE(fabsf(output[0][0] - .5f / magnitude) < 1e-4f);
            REQUIRE(fabsf(output[0][1] - 1 / magnitude) < 1e-4f);
            REQUIRE(fabsf(output[0][2] - (i == 2 ? -.5f : .5f) / magnitude) < 1e-4f);
        }
    }
    shader_graph_free(&restored);
    shader_graph_free(&g);
    return true;
}

/* Files the import cannot read, by name or as glTF, write nothing either. A
 * model names its files relative to itself, never by absolute path, and
 * neither .. nor a link leads it out of its folder. */
TEST_CASE(model_imports_refuse_files_they_cannot_read)
{
    REQUIRE(!strncmp(refused("nothing.gltf"), "Cannot read ", 12));
    char source[128], content[128];
    inside(source, sizeof source, "empty.gltf");
    inside(content, sizeof content, "content");
    FILE *file = fopen(source, "wb");
    REQUIRE(file && fputs("not glTF", file) >= 0 && !fclose(file));
    REQUIRE(!strncmp(refused("empty.gltf"), "Cannot load ", 12));
    REQUIRE(!model_import(content, source, "1Bad", &report, error, sizeof error));
    REQUIRE(!strcmp(error, "Start with a letter; use letters, digits, - and _"));

    gltf_fixture f = shape(NULL, 0, NULL);
    gltf_fixture_png(&f, "albedo.png", 1, 1, WHITE);
    REQUIRE(gltf_fixture_write(&f, source, textured("", "{\"emissiveTexture\":{\"index\":0}}")));
    char path[128];
    inside(path, sizeof path, "albedo.png");
    REQUIRE(!unlink(path));
    REQUIRE(!strcmp(refused("empty.gltf"), "Cannot read image 0 of the model"));
    gltf_fixture_free(&f);

    /* empty.bin, named by its absolute path, is still there. */
    f = shape(NULL, 0, NULL);
    REQUIRE(gltf_fixture_write(&f, source, PLAIN));
    inside(path, sizeof path, "empty.bin");
    REQUIRE(rename_uri("empty.gltf", "empty.gltf", "empty.bin", path));
    REQUIRE(access(path, R_OK) == 0);
    REQUIRE(!strncmp(refused("empty.gltf"), "Cannot load ", 12));
    REQUIRE(rename_uri("empty.gltf", "other/empty.gltf", path, "../empty.bin"));
    REQUIRE(!strncmp(refused("other/empty.gltf"), "Cannot load ", 12));
    char link[128];
    inside(link, sizeof link, "other/empty.bin");
    REQUIRE(!symlink(path, link));
    REQUIRE(rename_uri("other/empty.gltf", "other/empty.gltf", "../empty.bin", "empty.bin"));
    REQUIRE(!strncmp(refused("other/empty.gltf"), "Cannot load ", 12));
    /* Files each within the size limit, which together pass it, are
     * refused before the last is read. */
    const char *vast = "empty.bin\"},{\"byteLength\":1,\"uri\":\"vast.bin";
    REQUIRE(rename_uri("other/empty.gltf", "vast.gltf", "empty.bin", vast));
    inside(path, sizeof path, "vast.bin");
    file = fopen(path, "wb");
    REQUIRE(file && !fclose(file) && !truncate(path, MODEL_IMPORT_BYTES_MAX));
    REQUIRE(strstr(refused("vast.gltf"), "vast.gltf and the files it names are over 256 MiB together"));
    /* The same link, read from a model it keeps inside the folder, imports,
     * with the URI's escapes decoded. */
    REQUIRE(rename_uri("other/empty.gltf", "linked.gltf", "empty.bin", "other/empty%2Ebin"));
    REQUIRE(!strcmp(refused("linked.gltf"), "imported"));
    gltf_fixture_free(&f);
    return true;
}

/* An unlit material keeps its base color and its image alone. Its emission
 * and other images are glTF's fallbacks for viewers that light it, so the
 * import neither applies nor checks them. */
TEST_CASE(model_imports_keep_only_the_base_color_of_unlit_materials)
{
    gltf_fixture f = shape(NULL, 0, NULL);
    gltf_fixture_png(&f, "albedo.png", 1, 1, WHITE);
    const char
        *glow = "{\"name\":\"Glow\",\"pbrMetallicRoughness\":{"
                "\"baseColorTexture\":{\"index\":0}},"
                "\"emissiveFactor\":[1,1,1],\"emissiveTexture\":{\"index\":0},"
                "\"normalTexture\":{\"index\":0,\"texCoord\":1},"
                "\"extensions\":{\"KHR_materials_unlit\":{}}}";
    REQUIRE(
        import(&f, "lamp.gltf", textured("\"extensionsUsed\":[\"KHR_materials_unlit\"],", glow), "Lamp")
    );
    REQUIRE(!acount(report.warnings));
    standard_material unlit = factors();
    unlit.unlit = true;
    unlit.textures[STANDARD_TEXTURE_BASE_COLOR] = (standard_texture){
        .image = "content/textures/Lamp/albedo.png",
        .filter = SHADER_IMAGE_BILINEAR,
        .wrap = SHADER_IMAGE_REPEAT,
        .scale = { 1, 1 }
    };
    REQUIRE(holds_material("content/materials/Lamp_Glow.kdl", "Lamp_Glow", &unlit));
    gltf_fixture_free(&f);
    return true;
}

/* What the engine cannot draw yet imports without it, each with a warning. */
TEST_CASE(model_imports_warn_of_what_they_leave_out)
{
    gltf_fixture f = shape((const uint16_t[]){ 0, 1, 2, 0, 2, 3, 0, 0, 1 }, 9, NULL);
    gltf_fixture_png(&f, "cloth.png", 1, 1, WHITE);
    int colors = gltf_fixture_floats(
        &f,
        (const float[16]){ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
        4,
        4
    );
    int times = gltf_fixture_floats(&f, (const float[]){ 0, 1 }, 2, 1);
    int moves = gltf_fixture_floats(&f, (const float[]){ 0, 0, 0, 0, 1, 0 }, 2, 3);
    char members[2048];
    snprintf(
        members,
        sizeof members,
        "\"extensionsUsed\":[\"KHR_materials_clearcoat\",\"KHR_lights_"
        "punctual\"],"
        "\"extensions\":{\"KHR_lights_punctual\":{\"lights\":[{\"type\":"
        "\"point\"}]}},"
        "\"nodes\":[{\"mesh\":0,\"skin\":0},{\"camera\":0,\"extensions\":{"
        "\"KHR_lights_punctual\":{\"light\":0}}}],"
        "\"cameras\":[{\"type\":\"perspective\",\"perspective\":{\"yfov\":1,"
        "\"znear\":0.1}}],"
        "\"skins\":[{\"joints\":[1]}],"
        "\"animations\":[{\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,"
        "\"path\":\"translation\"}}],"
        "\"samplers\":[{\"input\":%d,\"output\":%d}]}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,"
        "\"NORMAL\":1,\"TEXCOORD_0\":2,\"COLOR_0\":%d},"
        "\"indices\":3,\"material\":0,\"targets\":[{\"POSITION\":0}]}]}],"
        "\"materials\":[{\"name\":\"Cloth\",\"doubleSided\":true,"
        "\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],"
        "\"textures\":[{\"source\":0,\"sampler\":0}],\"samplers\":[{\"wrapS\":"
        "33648,\"minFilter\":9984}]",
        times,
        moves,
        colors
    );
    REQUIRE(import(&f, "rug.gltf", members, "Rug"));
    static const char *const warnings[] = {
        "Ignored the KHR_materials_clearcoat extension.",
        "Ignored vertex colors.",
        "Ignored morph targets.",
        "Left out 1 triangle with no area.",
        "Material Rug_Cloth draws single-sided.",
        "Material Rug_Cloth's baseColorTexture shrinks with its magFilter, not "
        "the model's minFilter.",
        "Material Rug_Cloth's baseColorTexture repeats where the model mirrors "
        "or clamps it.",
        "Ignored 1 skin.",
        "Ignored 1 animation.",
        "Ignored 1 camera.",
        "Ignored 1 light.",
    };
    REQUIRE(warned(warnings, sizeof warnings / sizeof *warnings));
    REQUIRE(reads_mesh("content/meshes/Rug.mesh") && acount(mesh.indices) == 6);
    gltf_fixture_free(&f);
    return true;
}

/* A vertex that only a left-out triangle used is left out with it, so it
 * neither widens the mesh's bounds nor stays in its file. */
TEST_CASE(model_imports_leave_out_vertices_only_dropped_triangles_use)
{
    static const float positions[] = { -1, 0, -1, -1, 0, 1, 1, 0, 1, 100, 0, 0 };
    gltf_fixture f = shape((const uint16_t[]){ 0, 1, 2, 3, 3, 0 }, 6, positions);
    REQUIRE(import(&f, "sliver.gltf", PLAIN, "Sliver"));
    REQUIRE(reads_mesh("content/meshes/Sliver.mesh"));
    REQUIRE(acount(mesh.vertices) == 3 && acount(mesh.indices) == 3);
    REQUIRE(mesh.max.x == 1 && mesh.min.x == -1);
    for (int i = 0; i < 3; i++) {
        REQUIRE(mesh.indices[i] >= 0 && mesh.indices[i] < 3);
    }
    gltf_fixture_free(&f);
    return true;
}

/* A model whose nodes copy one primitive past what a .mesh file holds is
 * refused as soon as its baked mesh outgrows that, before its copies
 * outgrow memory. */
TEST_CASE(model_imports_refuse_meshes_a_mesh_file_cannot_hold)
{
    enum
    {
        TRIANGLES = 21000,
        CORNERS = TRIANGLES * 3,
        NODES = 12
    };
    float *positions = xmalloc(CORNERS * 3 * sizeof *positions);
    float *normals = xmalloc(CORNERS * 3 * sizeof *normals);
    float *uvs = xmalloc(CORNERS * 2 * sizeof *uvs);
    uint16_t *indices = xmalloc(CORNERS * sizeof *indices);
    for (int i = 0; i < CORNERS; i++) {
        int k = i / 3, j = i % 3;
        memcpy(
            positions + i * 3,
            (const float[]){ (float)k + (j == 2), 0, (float)(j == 1) },
            3 * sizeof *positions
        );
        memcpy(normals + i * 3, (const float[]){ 0, 1, 0 }, 3 * sizeof *normals);
        memcpy(uvs + i * 2, (const float[]){ (float)(j == 2), (float)(j == 1) }, 2 * sizeof *uvs);
        indices[i] = (uint16_t)i;
    }
    gltf_fixture f = { 0 };
    gltf_fixture_floats(&f, positions, CORNERS, 3);
    gltf_fixture_floats(&f, normals, CORNERS, 3);
    gltf_fixture_floats(&f, uvs, CORNERS, 2);
    gltf_fixture_indices(&f, indices, CORNERS);
    free(positions);
    free(normals);
    free(uvs);
    free(indices);
    char members[512] = "\"nodes\":[";
    for (int i = 0; i < NODES; i++) {
        strcat(members, i ? ",{\"mesh\":0}" : "{\"mesh\":0}");
    }
    strcat(members, "],\"meshes\":[{\"primitives\":[{" SQUARE "}]}]");
    REQUIRE(!import(&f, "copies.gltf", members, "Copies"));
    REQUIRE(
        !strcmp(error, "The model's meshes, copied for each node that draws them, are larger than a .mesh file holds")
    );
    REQUIRE(!entries("content"));
    gltf_fixture_free(&f);
    return true;
}

/* Vertices that no triangle names don't count toward what a .mesh file
 * holds: a primitive whose accessors carry more than that, of which one
 * triangle uses three, imports those three. */
TEST_CASE(model_imports_count_only_the_vertices_their_triangles_use)
{
    enum
    {
        VERTICES = 700000
    };
    float *positions = xcalloc(VERTICES * 3, sizeof *positions);
    float *normals = xcalloc(VERTICES * 3, sizeof *normals);
    float *uvs = xcalloc(VERTICES * 2, sizeof *uvs);
    memcpy(positions, (const float[]){ -1, 0, -1, -1, 0, 1, 1, 0, 1 }, 9 * sizeof *positions);
    for (int i = 0; i < 3; i++) {
        normals[i * 3 + 1] = 1;
    }
    gltf_fixture f = { 0 };
    gltf_fixture_floats(&f, positions, VERTICES, 3);
    gltf_fixture_floats(&f, normals, VERTICES, 3);
    gltf_fixture_floats(&f, uvs, VERTICES, 2);
    gltf_fixture_indices(&f, (const uint16_t[]){ 0, 1, 2 }, 3);
    free(positions);
    free(normals);
    free(uvs);
    REQUIRE(import(&f, "sparse.gltf", PLAIN, "Sparse"));
    REQUIRE(reads_mesh("content/meshes/Sparse.mesh"));
    REQUIRE(acount(mesh.vertices) == 3 && acount(mesh.indices) == 3);
    REQUIRE(mesh.min.x == -1 && mesh.max.x == 1);
    gltf_fixture_free(&f);
    return true;
}

/* A primitive past what a .mesh file holds is refused before the import
 * builds its vertices: here one without normals, whose 720,000 corners
 * would each become a vertex though they name 60,000. */
TEST_CASE(model_imports_refuse_vast_primitives_before_building_them)
{
    enum
    {
        TRIANGLES = 20000,
        CORNERS = TRIANGLES * 3,
        INDICES = CORNERS * 12
    };
    float *positions = xmalloc(CORNERS * 3 * sizeof *positions);
    uint16_t *indices = xmalloc(INDICES * sizeof *indices);
    for (int i = 0; i < CORNERS; i++) {
        int k = i / 3, j = i % 3;
        memcpy(
            positions + i * 3,
            (const float[]){ (float)k + (j == 2), 0, (float)(j == 1) },
            3 * sizeof *positions
        );
    }
    for (int i = 0; i < INDICES; i++) {
        indices[i] = (uint16_t)(i % CORNERS);
    }
    gltf_fixture f = { 0 };
    gltf_fixture_floats(&f, positions, CORNERS, 3);
    gltf_fixture_indices(&f, indices, INDICES);
    free(positions);
    free(indices);
    xalloc_stats before, after;
    xalloc_stats_get(&before);
    REQUIRE(!import(
        &f,
        "vast.gltf",
        "\"nodes\":[{\"mesh\":0}],\"meshes\":[{\"primitives\":[{"
        "\"attributes\":{\"POSITION\":0},\"indices\":1}]}]",
        "Vast"
    ));
    xalloc_stats_get(&after);
    REQUIRE(
        !strcmp(error, "The model's meshes, copied for each node that draws them, are larger than a .mesh file holds")
    );
    REQUIRE(
        after.requested_bytes - before.requested_bytes <
        INDICES * sizeof(render_mesh_surface_vertex)
    );
    REQUIRE(!entries("content"));
    gltf_fixture_free(&f);
    return true;
}

/* Imports the square, its node's members `node` and its primitive's
 * attributes `attributes`, as `name`, and reads its mesh. */
static bool frames(gltf_fixture *f, const char *name, const char *nodes, const char *attributes)
{
    char members[1024], file[64], key[96];
    snprintf(members, sizeof members, "\"nodes\":[%s],\"meshes\":[{\"primitives\":[{\"attributes\":{%s},\"indices\":3}]}]", nodes, attributes);
    snprintf(file, sizeof file, "%s.gltf", name);
    snprintf(key, sizeof key, "content/meshes/%s.mesh", name);
    return import(f, file, members, name) && reads_mesh(key);
}

/* A normal map's green tilts toward the top of its image, as glTF's does:
 * generated bitangents point down the image, toward +v, as Chibi's do, and
 * authored tangents keep their handedness against glTF's bitangent, which
 * points up it. A mirroring node turns both and the winding with it, and a
 * primitive without normals takes each triangle's own. A parent that
 * flattens a turned square onto the ground keeps it, facing up or down as
 * its normals did, mirrored or not. */
TEST_CASE(model_imports_keep_gltf_tangent_frames)
{
    static const CF_V3 x = { 1, 0, 0 }, z = { 0, 0, 1 };
    const char *attributes = "\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2";
    gltf_fixture f = shape(NULL, 0, NULL);
    REQUIRE(frames(&f, "Made", "{\"mesh\":0}", attributes) && frames_are(x, z));
    REQUIRE(frames(&f, "Mirrored", "{\"mesh\":0,\"scale\":[-1,1,1]}", attributes));
    REQUIRE(acount(mesh.vertices) == 4 && frames_are(cf_neg_v3(x), z));
    REQUIRE(near_v3(mesh.min, (CF_V3){ -1, 0, -1 }) && near_v3(mesh.max, (CF_V3){ 1, 0, 1 }));
    REQUIRE(frames(&f, "Hidden", "{\"mesh\":0,\"scale\":[0,1,1]},{\"mesh\":0}", attributes));
    REQUIRE(acount(mesh.vertices) == 4);
    /* A scale whose volume overflows float keeps finite vertices. */
    REQUIRE(frames(&f, "Vast", "{\"mesh\":0,\"scale\":[1e13,1e13,1e13]}", attributes));
    REQUIRE(acount(mesh.indices) == 6 && frames_are(x, z) && mesh.max.x == 1e13f);
    static const struct
    {
        const char *name, *nodes;
        CF_V3 tangent, normal;
    } pressed[] = {
        { "Pressed",
          "{\"children\":[1],\"scale\":[1,0,1]},"
          "{\"mesh\":0,\"rotation\":[0,0,0.38268343,0.92387953]}",
          { 1, 0, 0 },
          { 0, 1, 0 } },
        { "Underside",
          "{\"children\":[1],\"scale\":[1,0,1]},"
          "{\"mesh\":0,\"rotation\":[0,0,0.92387953,0.38268343]}",
          { -1, 0, 0 },
          { 0, -1, 0 } },
        { "PressedMirror",
          "{\"children\":[1],\"scale\":[-1,0,1]},"
          "{\"mesh\":0,\"rotation\":[0,0,0.38268343,0.92387953]}",
          { -1, 0, 0 },
          { 0, 1, 0 } },
    };
    for (int i = 0; i < 3; i++) {
        REQUIRE(frames(&f, pressed[i].name, pressed[i].nodes, attributes));
        REQUIRE(acount(mesh.indices) == 6 && frames_are(pressed[i].tangent, z));
        REQUIRE(near_v3(mesh.vertices[0].normal, pressed[i].normal));
    }
    /* Authored tangents along -z and twice unit length tilt green along
     * -x. */
    int tangents = gltf_fixture_floats(
        &f,
        (const float[16]){ 0, 0, -2, 1, 0, 0, -2, 1, 0, 0, -2, 1, 0, 0, -2, 1 },
        4,
        4
    );
    char authored[128];
    snprintf(authored, sizeof authored, "%s,\"TANGENT\":%d", attributes, tangents);
    REQUIRE(
        frames(&f, "Authored", "{\"mesh\":0}", authored) &&
        frames_are(cf_neg_v3(z), cf_neg_v3(x))
    );
    REQUIRE(frames(&f, "Turned", "{\"mesh\":0,\"scale\":[-1,1,1]}", authored));
    REQUIRE(frames_are(cf_neg_v3(z), x));
    /* A scale that stretches them past float's square keeps them. */
    REQUIRE(frames(&f, "Stretched", "{\"mesh\":0,\"scale\":[1e-20,1,1e20]}", authored));
    REQUIRE(acount(mesh.indices) == 6 && frames_are(cf_neg_v3(z), cf_neg_v3(x)));
    /* So does one whose two parts the frame carries past float range
     * together, on a square small enough that its corners stay in range. */
    static const float small[] = { -1e-10f, 0, -1e-10f, -1e-10f, 0, 1e-10f,
                                   1e-10f,  0, 1e-10f,  1e-10f,  0, -1e-10f };
    gltf_fixture steep = shape(NULL, 0, small);
    const float h = .70710678f;
    char steep_attributes[128];
    snprintf(
        steep_attributes,
        sizeof steep_attributes,
        "%s,\"TANGENT\":%d",
        attributes,
        gltf_fixture_floats(
            &steep,
            (const float[16]){ -h, 0, h, 1, -h, 0, h, 1, -h, 0, h, 1, -h, 0, h, 1 },
            4,
            4
        )
    );
    REQUIRE(frames(
        &steep,
        "Steep",
        "{\"children\":[1],\"scale\":[1,1,3e38]},"
        "{\"mesh\":0,\"rotation\":[0,0.38268343,0,0.92387953],\"scale\":[1."
        "4142135,1,1.4142135]}",
        steep_attributes
    ));
    REQUIRE(acount(mesh.indices) == 6 && frames_are(z, x));
    gltf_fixture_free(&steep);
    /* Without normals glTF ignores tangents too. */
    snprintf(authored, sizeof authored, "\"POSITION\":0,\"TEXCOORD_0\":2,\"TANGENT\":%d", tangents);
    REQUIRE(frames(&f, "Flat", "{\"mesh\":0}", authored));
    REQUIRE(
        acount(mesh.vertices) == 6 && frames_are(x, z) &&
        near_v3(mesh.vertices[0].normal, (CF_V3){ 0, 1, 0 })
    );
    gltf_fixture_free(&f);

    /* A flat normal keeps its direction where its float cross product would
     * overflow. */
    static const float wall[] = { -1e20f, -1e20f, 0, 1e20f,  -1e20f, 0,
                                  1e20f,  1e20f,  0, -1e20f, 1e20f,  0 };
    gltf_fixture vast = shape(NULL, 0, wall);
    REQUIRE(frames(&vast, "Wall", "{\"mesh\":0}", "\"POSITION\":0,\"TEXCOORD_0\":2"));
    REQUIRE(acount(mesh.indices) == 6);
    for (int i = 0; i < acount(mesh.vertices); i++) {
        REQUIRE(near_v3(mesh.vertices[i].normal, z));
    }
    gltf_fixture_free(&vast);

    /* The image's u running along -x keeps green tilting up the image. */
    gltf_fixture flipped = { 0 };
    gltf_fixture_square(&flipped, (const float[]){ 1, 1, 1, 0, 0, 0, 0, 1 });
    REQUIRE(
        frames(&flipped, "Flipped", "{\"mesh\":0}", attributes) &&
        frames_are(cf_neg_v3(x), z)
    );
    gltf_fixture_free(&flipped);
    return true;
}

/* Imports write shader-typed material defaults and game-typed prototypes. */
type_catalog_pack test_model_import_types(int index)
{
    if (index) {
        return TYPE_PACK;
    }
    return (type_catalog_pack){ .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
}

void test_model_import()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(model_imports_write_each_part_once);
    RUN_TEST_CASE(model_imports_read_binary_files_alike);
    RUN_TEST_CASE(model_imports_keep_each_texture_transform);
    RUN_TEST_CASE(model_imports_keep_default_and_signed_texture_scales);
    RUN_TEST_CASE(model_imports_refuse_malformed_texture_transforms_transactionally);
    RUN_TEST_CASE(model_imports_generate_transformed_normal_tangents);
    RUN_TEST_CASE(model_import_normal_convention_uses_unit_tangent_frames);
    RUN_TEST_CASE(model_imports_name_parts_apart);
    RUN_TEST_CASE(model_imports_name_materials_of_the_longest_models);
    RUN_TEST_CASE(model_imports_replace_nothing_and_undo_failures);
    RUN_TEST_CASE(model_imports_refuse_names_the_project_declares);
    RUN_TEST_CASE(model_imports_refuse_more_native_graphs_than_a_project_prepares);
    RUN_TEST_CASE(model_imports_refuse_linked_folders);
    RUN_TEST_CASE(model_imports_refuse_what_they_cannot_draw_as_authored);
    RUN_TEST_CASE(model_imports_refuse_files_they_cannot_read);
    RUN_TEST_CASE(model_imports_keep_only_the_base_color_of_unlit_materials);
    RUN_TEST_CASE(model_imports_warn_of_what_they_leave_out);
    RUN_TEST_CASE(model_imports_leave_out_vertices_only_dropped_triangles_use);
    RUN_TEST_CASE(model_imports_refuse_meshes_a_mesh_file_cannot_hold);
    RUN_TEST_CASE(model_imports_count_only_the_vertices_their_triangles_use);
    RUN_TEST_CASE(model_imports_refuse_vast_primitives_before_building_them);
    RUN_TEST_CASE(model_imports_keep_gltf_tangent_frames);
    pu_clear_setup();
}
