#include "assets/model_import.h"

#include "assets/standard_material.h"
#include "graph/document/serialize.h"
#include "graph/shader/shader_graph.h"
#include "runtime/component_catalog.h"
#include "runtime/geometry/mesh_prepared.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"
#include "test/support/gltf_fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <dirent.h>
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

/* cross(normal, tangent) * w, the direction normal maps' green tilts toward. */
static CF_V3 bitangent_of(const render_mesh_surface_vertex *v)
{
    return cf_mul_v3_f(cf_cross_v3(v->normal, tangent_of(v)), v->tangent.w);
}

/* Whether every vertex has `tangent` and `bitangent`, and every triangle
 * winds counterclockwise around its vertex normals. */
static bool frames_are(CF_V3 tangent, CF_V3 bitangent)
{
    for (int i = 0; i < acount(mesh.vertices); i++) {
        const render_mesh_surface_vertex *v = &mesh.vertices[i];
        if (!near_v3(tangent_of(v), tangent) || !near_v3(bitangent_of(v), bitangent)) {
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
    "\"samplers\":[{\"magFilter\":9728,\"wrapS\":33071,\"wrapT\":33071}]";

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
        "content/textures/Crate/albedo.png",
        SHADER_IMAGE_NEAREST,
        SHADER_IMAGE_CLAMP
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
        prototype &&
        !strcmp(
            prototype,
            "name \"Crate\"\n"
            "components {\n"
            "    GAME.GameGeometry3D depth=1.0 height=1.0 width=1.0\n"
            "    GAME.GameMeshAsset source=\"content/meshes/Crate.mesh\"\n"
            "    GAME.GameTransform3D {\n"
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

/* Models the import would draw unlike glTF write nothing. */
TEST_CASE(model_imports_refuse_what_they_cannot_draw_as_authored)
{
    gltf_fixture f = shape(NULL, 0, NULL);
    gltf_fixture_png(&f, "albedo.png", 1, 1, WHITE);
    REQUIRE(
        !strcmp(refusal(&f, textured("\"extensionsRequired\":[\"KHR_draco_mesh_compression\"],", "{}")), "The model requires KHR_draco_mesh_compression, which import does not support")
    );
    const char *moved = "{\"name\":\"M\",\"pbrMetallicRoughness\":{"
                        "\"baseColorTexture\":{\"index\":0,"
                        "\"extensions\":{\"KHR_texture_transform\":{\"offset\":"
                        "[0.5,0]}}}}}";
    REQUIRE(
        !strcmp(refusal(&f, textured("\"extensionsRequired\":[\"KHR_texture_transform\"],", moved)), "Material Bad_M's baseColorTexture transforms its UVs, which import does not support")
    );
    const char *second = "{\"name\":\"M\",\"normalTexture\":{\"index\":0,"
                         "\"texCoord\":1}}";
    REQUIRE(
        !strcmp(refusal(&f, textured("", second)), "Material Bad_M's normalTexture reads TEXCOORD_1; import reads only TEXCOORD_0")
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

/* Files the import cannot read, by name or as glTF, write nothing either. A
 * model names its files relative to itself, never by absolute path. */
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
    size_t length = 0;
    char *text = read_file("empty.gltf", &length);
    const char *relative = "\"uri\":\"empty.bin\"";
    char *uri = strstr(text, relative);
    REQUIRE(uri);
    inside(path, sizeof path, "empty.bin");
    file = fopen(source, "wb");
    REQUIRE(file);
    fprintf(file, "%.*s\"uri\":\"%s\"%s", (int)(uri - text), text, path, uri + strlen(relative));
    REQUIRE(!fclose(file));
    free(text);
    REQUIRE(access(path, R_OK) == 0);
    REQUIRE(!strncmp(refused("empty.gltf"), "Cannot load ", 12));
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
        "33648}]",
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
 * generated bitangents point up the image, toward -v, and authored tangents
 * keep their handedness. A mirroring node turns both and the winding with
 * it, and a primitive without normals takes each triangle's own. */
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
    /* Authored tangents along -z and twice unit length point bitangents
     * along -x. */
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
    /* Without normals glTF ignores tangents too. */
    snprintf(authored, sizeof authored, "\"POSITION\":0,\"TEXCOORD_0\":2,\"TANGENT\":%d", tangents);
    REQUIRE(frames(&f, "Flat", "{\"mesh\":0}", authored));
    REQUIRE(
        acount(mesh.vertices) == 6 && frames_are(x, z) &&
        near_v3(mesh.vertices[0].normal, (CF_V3){ 0, 1, 0 })
    );
    gltf_fixture_free(&f);

    /* The image's u running along -x keeps the bitangent up the image. */
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
        return GAME_TYPE_PACK;
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
    RUN_TEST_CASE(model_imports_name_parts_apart);
    RUN_TEST_CASE(model_imports_name_materials_of_the_longest_models);
    RUN_TEST_CASE(model_imports_replace_nothing_and_undo_failures);
    RUN_TEST_CASE(model_imports_refuse_what_they_cannot_draw_as_authored);
    RUN_TEST_CASE(model_imports_refuse_files_they_cannot_read);
    RUN_TEST_CASE(model_imports_warn_of_what_they_leave_out);
    RUN_TEST_CASE(model_imports_leave_out_vertices_only_dropped_triangles_use);
    RUN_TEST_CASE(model_imports_keep_gltf_tangent_frames);
    pu_clear_setup();
}
