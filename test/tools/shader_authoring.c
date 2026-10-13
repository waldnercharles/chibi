#include "authoring/material_create.h"
#include "authoring/shader_report.h"
#include "graph/document/serialize.h"
#include "graph/shader/shader_graph.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const registry_pack **previous;
static char root[128];

static void setup()
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &SHADER_PACK };
    registry_set_packs(packs, 1);
    type_catalog_pack types = { .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
    type_catalog_set_project(&types);
}

static void cleanup()
{
    type_catalog_set_project(NULL);
    registry_set_packs(previous, acount(previous));
    afree(previous);
    if (root[0]) {
        tool_remove_tree(root);
        root[0] = 0;
    }
}

TEST_CASE(material_presets_roundtrip_and_sample_with_owned_reports)
{
    for (int preset = 0; preset < MATERIAL_AUTHORING_PRESET_COUNT; preset++) {
        standard_material material = material_authoring_defaults(
            (material_authoring_preset)preset
        );
        material.base_color[0] = .125f;
        material.base_color[1] = .25f;
        material.base_color[2] = .75f;
        shader_error error;
        char *source = material_authoring_source("Surface", &material, &error);
        REQUIRE(source);
        behavior_asset *asset = behavior_asset_load(source);
        REQUIRE(asset);
        char *saved = behavior_asset_save(asset);
        REQUIRE(!strcmp(source, saved));
        shader_authoring_report report;
        shader_authoring_report_init(&report);
        REQUIRE(shader_authoring_inspect(asset, &report, &error));
        REQUIRE(report.costs.images == 0);
        REQUIRE(report.costs.texture_samples == 0);
        REQUIRE(report.widths[STANDARD_BASE_COLOR] == 3);
        REQUIRE(!strcmp(report.names[STANDARD_BASE_COLOR], "Base Color"));
        shader_sample_context context = shader_authoring_center_context(asset);
        behavior_asset_free(asset);
        float outputs[SHADER_OUTPUT_MAX][4] = { 0 };
        REQUIRE(shader_authoring_sample(&report, &context, NULL, outputs, &error));
        REQUIRE(fabsf(outputs[SHADER_SURFACE_BASE_COLOR][0] - .125f) < .00001f);
        REQUIRE(fabsf(outputs[SHADER_SURFACE_ROUGHNESS][0] - material.roughness) < .00001f);
        REQUIRE(fabsf(outputs[SHADER_SURFACE_METALLIC][0] - material.metallic) < .00001f);
        REQUIRE(fabsf(outputs[SHADER_SURFACE_EMISSION][0] - material.emission[0]) < .00001f);
        float parameters[SHADER_PARAMETER_MAX][4];
        memcpy(parameters, report.defaults, sizeof parameters);
        parameters[STANDARD_ROUGHNESS][0] = .4f;
        REQUIRE(shader_authoring_sample(&report, &context, parameters, outputs, &error));
        REQUIRE(fabsf(outputs[SHADER_SURFACE_ROUGHNESS][0] - .4f) < .00001f);
        parameters[STANDARD_ROUGHNESS][0] = NAN;
        REQUIRE(!shader_authoring_sample(&report, &context, parameters, outputs, &error));
        shader_authoring_report_free(&report);
        free(source);
        free(saved);
    }
    return true;
}

TEST_CASE(shader_report_preserves_valid_contract_on_duplicate_parameter_failure)
{
    standard_material material = material_authoring_defaults(MATERIAL_AUTHORING_MATTE);
    behavior_asset *asset = standard_material_build("Surface", &material);
    shader_authoring_report report;
    shader_authoring_report_init(&report);
    shader_error error;
    REQUIRE(shader_authoring_inspect(asset, &report, &error));
    int nodes = report.costs.nodes;
    uint64_t changed = 0;
    for (int i = 0; i < acount(asset->graph->nodes); i++) {
        node *n = asset->graph->nodes[i];
        if (n->reference_name && !strcmp(n->reference_name, "Metallic")) {
            n->reference_id = STANDARD_ROUGHNESS;
            changed = n->id;
        }
    }
    REQUIRE(changed);
    REQUIRE(!shader_authoring_inspect(asset, &report, &error));
    REQUIRE(error.node != 0);
    REQUIRE(strstr(error.message, "different slots"));
    REQUIRE(report.costs.nodes == nodes);
    REQUIRE(!strcmp(report.names[STANDARD_METALLIC], "Metallic"));
    shader_authoring_report_free(&report);
    behavior_asset_free(asset);
    return true;
}

TEST_CASE(material_creation_refuses_existing_declarations_and_invalid_sources)
{
    snprintf(root, sizeof root, "/tmp/chibi-material-authoring-XXXXXX");
    REQUIRE(mkdtemp(root));
    standard_material material = material_authoring_defaults(MATERIAL_AUTHORING_METAL);
    char error[256];
    char *path = material_authoring_create(root, "Steel", &material, error, sizeof error);
    REQUIRE(path);
    char *original = tool_read(path);
    REQUIRE(original);
    REQUIRE(!material_authoring_create(root, "Steel", &material, error, sizeof error));
    REQUIRE(errno == EEXIST);
    char *after = tool_read(path);
    REQUIRE(after && !strcmp(original, after));
    free(after);
    char renamed[256];
    snprintf(renamed, sizeof renamed, "%s/materials/DifferentFile.kdl", root);
    REQUIRE(rename(path, renamed) == 0);
    REQUIRE(!material_authoring_create(root, "Steel", &material, error, sizeof error));
    REQUIRE(errno == EEXIST);
    char shapes[256];
    snprintf(shapes, sizeof shapes, "%s/shapes", root);
    REQUIRE(mkdir(shapes, 0755) == 0);
    snprintf(shapes, sizeof shapes, "%s/shapes/Elsewhere.kdl", root);
    tool_write(shapes, "name \"Block\"\ndomain \"shape\"\n");
    REQUIRE(!material_authoring_create(root, "Block", &material, error, sizeof error));
    REQUIRE(errno == EEXIST);
    char target[256], linked[256], absent[256];
    snprintf(target, sizeof target, "%s/declaration.txt", root);
    snprintf(linked, sizeof linked, "%s/shapes/Linked.kdl", root);
    tool_write(target, "name \"LinkedMaterial\"\ndomain \"material\"\n");
    REQUIRE(symlink("../declaration.txt", linked) == 0);
    REQUIRE(!material_authoring_create(root, "LinkedRefusal", &material, error, sizeof error));
    REQUIRE(strstr(error, "Cannot open shader declaration Linked.kdl"));
    snprintf(absent, sizeof absent, "%s/materials/LinkedRefusal.kdl", root);
    REQUIRE(access(absent, F_OK) != 0);
    REQUIRE(unlink(linked) == 0);
    snprintf(linked, sizeof linked, "%s/shapes/Oversized.kdl", root);
    int fd = open(linked, O_WRONLY | O_CREAT | O_EXCL, 0600);
    REQUIRE(fd >= 0);
    REQUIRE(ftruncate(fd, 16 * 1024 * 1024 + 1) == 0);
    REQUIRE(close(fd) == 0);
    REQUIRE(!material_authoring_create(root, "OversizeRefusal", &material, error, sizeof error));
    REQUIRE(strstr(error, "at most 16 MiB"));
    snprintf(absent, sizeof absent, "%s/materials/OversizeRefusal.kdl", root);
    REQUIRE(access(absent, F_OK) != 0);
    REQUIRE(unlink(linked) == 0);
    material.roughness = NAN;
    REQUIRE(!material_authoring_create(root, "Invalid", &material, error, sizeof error));
    snprintf(renamed, sizeof renamed, "%s/materials/Invalid.kdl", root);
    REQUIRE(access(renamed, F_OK) != 0);
    free(original);
    free(path);
    return true;
}

TEST_CASE(material_texture_settings_survive_source_generation)
{
    standard_material material = material_authoring_defaults(MATERIAL_AUTHORING_MATTE);
    material.textures[STANDARD_TEXTURE_BASE_COLOR]
        .image = "content/images/base.png";
    material.textures[STANDARD_TEXTURE_BASE_COLOR].filter = SHADER_IMAGE_NEAREST;
    material.textures[STANDARD_TEXTURE_BASE_COLOR].wrap = SHADER_IMAGE_CLAMP;
    material.textures[STANDARD_TEXTURE_NORMAL]
        .image = "content/images/normal.png";
    shader_error error;
    char *source = material_authoring_source("Textured", &material, &error);
    REQUIRE(source);
    behavior_asset *asset = behavior_asset_load(source);
    REQUIRE(asset);
    shader_authoring_report report;
    shader_authoring_report_init(&report);
    REQUIRE(shader_authoring_inspect(asset, &report, &error));
    REQUIRE(report.costs.images == 2);
    bool color = false, normal = false;
    for (int i = 0; i < acount(report.program.images); i++) {
        shader_image_source *image = &report.program.images[i];
        if (!strcmp(image->key, "content/images/base.png")) {
            color = image->color_space == SHADER_IMAGE_SRGB &&
                    image->filter == SHADER_IMAGE_NEAREST &&
                    image->wrap == SHADER_IMAGE_CLAMP;
        }
        if (!strcmp(image->key, "content/images/normal.png")) {
            normal = image->color_space == SHADER_IMAGE_LINEAR;
        }
    }
    REQUIRE(color && normal);
    shader_authoring_report_free(&report);
    behavior_asset_free(asset);
    free(source);
    return true;
}

TEST_CASE(material_creation_refuses_symlink_content_folder)
{
    snprintf(root, sizeof root, "/tmp/chibi-material-authoring-XXXXXX");
    REQUIRE(mkdtemp(root));
    char outside[256], linked[256];
    snprintf(outside, sizeof outside, "%s/actual", root);
    snprintf(linked, sizeof linked, "%s/linked", root);
    REQUIRE(mkdir(outside, 0755) == 0);
    REQUIRE(symlink(outside, linked) == 0);
    standard_material material = material_authoring_defaults(MATERIAL_AUTHORING_MATTE);
    char error[256];
    REQUIRE(!material_authoring_create(linked, "Surface", &material, error, sizeof error));
    snprintf(outside, sizeof outside, "%s/actual/materials", root);
    REQUIRE(access(outside, F_OK) != 0);
    return true;
}

void test_shader_authoring()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(material_presets_roundtrip_and_sample_with_owned_reports);
    RUN_TEST_CASE(shader_report_preserves_valid_contract_on_duplicate_parameter_failure);
    RUN_TEST_CASE(material_creation_refuses_existing_declarations_and_invalid_sources);
    RUN_TEST_CASE(material_texture_settings_survive_source_generation);
    RUN_TEST_CASE(material_creation_refuses_symlink_content_folder);
    pu_clear_setup();
}
