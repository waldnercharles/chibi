#include "assets/model_validation.h"

#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "graph/shader/shader_graph.h"
#include "render/shader_catalog.h"
#include "support/array.h"
#include "support/file_io.h"
#include "test/graph/packs.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(!fclose(file));
}

static void save(const char *path, behavior_asset *asset)
{
    char *text = behavior_asset_save(asset);
    assert(text);
    write_text(path, text);
    free(text);
}

static behavior_asset *model(const char *name, const node_desc *desc)
{
    behavior_asset *asset = behavior_asset_new(name);
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    node *geometry = graph_add_node(asset->graph, desc);
    assert(!graph_connect_data(
        asset->graph,
        geometry,
        0,
        model_graph_add_output(asset->graph),
        0
    ));
    return asset;
}

static behavior_asset *parameter_material(const char *name, int count)
{
    behavior_asset *a = behavior_asset_new(name);
    a->domain = BEHAVIOR_DOMAIN_MATERIAL;
    graph *g = a->graph;
    node *sum = NULL;
    for (int i = 0; i < count; i++) {
        node *parameter = graph_add_node(g, registry_node_by_name("Shader Parameter"));
        char label[32];
        snprintf(label, sizeof label, "Color%d", i);
        assert(!graph_set_node_reference(g, parameter, i, label));
        assert(!graph_set_pin_type(g, parameter, true, 0, PIN_STRUCT, "ShaderVec3"));
        float white[3] = { 1, 1, 1 };
        pin_value value = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = white };
        assert(!graph_set_pin_default(g, parameter, true, 0, &value));
        if (!sum) {
            sum = parameter;
            continue;
        }
        node *add = graph_add_node(g, registry_node_by_name("Shader Add"));
        assert(!graph_connect_data(g, sum, 0, add, 0));
        assert(!graph_connect_data(g, parameter, 0, add, 1));
        sum = add;
    }
    assert(!graph_connect_data(g, sum, 0, shader_graph_add_output(g, false), 0));
    return a;
}

static behavior_asset *material_pair()
{
    behavior_asset *a = behavior_asset_new("SavedMaterialPair");
    a->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = a->graph;
    node *combine = graph_add_node(g, &NODE_ModelUnion);
    for (int i = 0; i < 2; i++) {
        const char *name = i ? "Right" : "Left";
        node *sphere = graph_add_node(g, &NODE_ModelSphere);
        node *part = graph_add_node(g, &NODE_ModelPart);
        assert(!graph_set_node_reference(g, part, 0, name));
        node *slot = graph_add_node(g, &NODE_ModelMaterialSlot);
        assert(!graph_set_node_reference(g, slot, 0, name));
        slot->reference_asset = graph_own_str(g, name, strlen(name));
        assert(!graph_connect_data(g, sphere, 0, part, 0));
        assert(!graph_connect_data(g, slot, 0, part, 1));
        assert(!graph_connect_data(g, part, 0, combine, i));
    }
    assert(!graph_connect_data(g, combine, 0, model_graph_add_output(g), 0));
    return a;
}

TEST_CASE(saved_material_failures)
{
    char directory[] = "/tmp/chibi-model-material-export-XXXXXX";
    REQUIRE(mkdtemp(directory));
    char models[256], materials[256], model_path[256], left_path[256],
        right_path[256], error[1024];
    snprintf(models, sizeof models, "%s/models", directory);
    snprintf(materials, sizeof materials, "%s/materials", directory);
    REQUIRE(!mkdir(models, 0700));
    REQUIRE(!mkdir(materials, 0700));
    snprintf(model_path, sizeof model_path, "%s/models/Pair.kdl", directory);
    snprintf(left_path, sizeof left_path, "%s/materials/Left.kdl", directory);
    snprintf(right_path, sizeof right_path, "%s/materials/Right.kdl", directory);
    behavior_asset *pair = material_pair();
    behavior_asset *left = parameter_material("Left", 17);
    behavior_asset *right = parameter_material("Right", 15);
    save(model_path, pair);
    save(left_path, left);
    save(right_path, right);
    unsigned revision = render_shader_revision();
    REQUIRE(model_content_validate(directory, error, sizeof error));
    REQUIRE(render_shader_revision() == revision);
    behavior_asset *too_many = parameter_material("Right", 16);
    save(right_path, too_many);
    char *before = file_read_text(right_path);
    REQUIRE(!model_content_validate(directory, error, sizeof error));
    REQUIRE(strstr(error, "32") && strstr(error, "Pair.kdl"));
    char *after = file_read_text(right_path);
    REQUIRE(!strcmp(before, after));
    REQUIRE(render_shader_revision() == revision);
    free(before);
    free(after);
    behavior_asset *invalid = behavior_asset_new("Right");
    invalid->domain = BEHAVIOR_DOMAIN_MATERIAL;
    save(right_path, invalid);
    before = file_read_text(right_path);
    REQUIRE(!model_content_validate(directory, error, sizeof error));
    REQUIRE(strstr(error, "Pair.kdl"));
    after = file_read_text(right_path);
    REQUIRE(!strcmp(before, after));
    REQUIRE(render_shader_revision() == revision);
    free(before);
    free(after);
    save(right_path, right);
    REQUIRE(model_content_validate(directory, error, sizeof error));
    char libraries[256], library_path[256];
    snprintf(libraries, sizeof libraries, "%s/shader-libraries", directory);
    REQUIRE(!mkdir(libraries, 0700));
    snprintf(library_path, sizeof library_path, "%s/ShaderMath.kdl", libraries);
    char *library_text = file_read_text(
        CHIBI_SOURCE_ROOT
        "/test/fixtures/content/shader-libraries/ShaderMath.kdl"
    );
    char *material_text = file_read_text(
        CHIBI_SOURCE_ROOT "/test/fixtures/content/materials/PulseRing.kdl"
    );
    REQUIRE(library_text && material_text);
    behavior_asset *library_material = behavior_asset_load(material_text);
    REQUIRE(library_material && !behavior_asset_set_name(library_material, "Right"));
    write_text(library_path, library_text);
    save(right_path, library_material);
    REQUIRE(model_content_validate(directory, error, sizeof error));
    REQUIRE(render_shader_revision() == revision);
    REQUIRE(!unlink(library_path));
    REQUIRE(!model_content_validate(directory, error, sizeof error));
    save(right_path, right);
    REQUIRE(model_content_validate(directory, error, sizeof error));
    behavior_asset_free(library_material);
    free(library_text);
    free(material_text);
    behavior_asset_free(pair);
    behavior_asset_free(left);
    behavior_asset_free(right);
    behavior_asset_free(too_many);
    behavior_asset_free(invalid);
    REQUIRE(!unlink(model_path));
    REQUIRE(!unlink(left_path));
    REQUIRE(!unlink(right_path));
    REQUIRE(!rmdir(models));
    REQUIRE(!rmdir(materials));
    REQUIRE(!rmdir(libraries));
    REQUIRE(!rmdir(directory));
    return true;
}

TEST_CASE(saved_model_sources)
{
    char directory[] = "/tmp/chibi-model-export-XXXXXX";
    REQUIRE(mkdtemp(directory));
    char folder[256], leaf_path[256], parent_path[256], error[1024];
    snprintf(folder, sizeof folder, "%s/models", directory);
    REQUIRE(!mkdir(folder, 0700));
    snprintf(leaf_path, sizeof leaf_path, "%s/models/ZLeaf.kdl", directory);
    snprintf(parent_path, sizeof parent_path, "%s/models/AParent.kdl", directory);
    REQUIRE(model_content_validate(directory, error, sizeof error));
    behavior_asset *leaf = model("ZLeaf", &NODE_ModelSphere);
    behavior_asset *parent = model("AParent", &NODE_ModelInstance);
    node *reference = parent->graph->nodes[0];
    reference->reference_asset = graph_own_str(parent->graph, "ZLeaf", 5);
    save(leaf_path, leaf);
    save(parent_path, parent);
    REQUIRE(model_content_validate(directory, error, sizeof error));

    /* The validator reads saved source, even if a runtime could retain an old
       successful generation. It never repairs or drops the invalid edit. */
    node *sphere = leaf->graph->nodes[0];
    pin_value radius = { .type = PIN_F32, .f = -1 };
    REQUIRE(!graph_set_pin_default(leaf->graph, sphere, false, 0, &radius));
    save(leaf_path, leaf);
    char *before = file_read_text(leaf_path);
    REQUIRE(!model_content_validate(directory, error, sizeof error));
    REQUIRE(strstr(error, ".kdl") && strstr(error, "node"));
    char *after = file_read_text(leaf_path);
    REQUIRE(!strcmp(before, after));
    free(before);
    free(after);

    radius.f = 1;
    REQUIRE(!graph_set_pin_default(leaf->graph, sphere, false, 0, &radius));
    save(leaf_path, leaf);
    REQUIRE(!unlink(leaf_path));
    REQUIRE(!model_content_validate(directory, error, sizeof error));
    REQUIRE(strstr(error, "AParent.kdl") && strstr(error, "ZLeaf"));
    write_text(leaf_path, "domain \"model\" {\n");
    REQUIRE(!model_content_validate(directory, error, sizeof error));
    REQUIRE(strstr(error, "ZLeaf.kdl"));
    save(leaf_path, leaf);
    REQUIRE(model_content_validate(directory, error, sizeof error));

    behavior_asset_free(leaf);
    behavior_asset_free(parent);
    REQUIRE(!unlink(leaf_path));
    REQUIRE(!unlink(parent_path));
    REQUIRE(!rmdir(folder));
    REQUIRE(!rmdir(directory));
    return true;
}

void test_model_validation()
{
    test_graph_packs_install();
    RUN_TEST_CASE(saved_material_failures);
    RUN_TEST_CASE(saved_model_sources);
    test_graph_packs_restore();
}
