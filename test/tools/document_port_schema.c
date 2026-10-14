#include "authoring/document_port_schema.h"

#include "graph/document/serialize.h"
#include "graph/shader/shader_graph.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const registry_pack **old_packs;
static type_catalog_pack old_types;
static bool had_types;
static workspace_documents *docs;
static document_semantics *semantic;
static behavior_asset *asset;
static arena storage;
static script_catalog catalog;
static int handle;
static char root[128];
static char error[256];
static void setup(void)
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(old_packs, registry_pack_at(i));
    }
    const type_catalog_pack *old = type_catalog_project();
    had_types = old != NULL;
    if (old) {
        old_types = *old;
        type_catalog_set_project(NULL);
    }
    type_catalog_pack types = { .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
    type_catalog_set_project(&types);
    const registry_pack *packs[] = { &SHADER_PACK };
    registry_set_packs(packs, 1);
}
static void cleanup(void)
{
    document_semantics_free(semantic);
    semantic = NULL;
    behavior_asset_free(asset);
    asset = NULL;
    workspace_documents_free(docs);
    docs = NULL;
    arena_free(&storage);
    if (*root) {
        rmdir(root);
        *root = 0;
    }
    type_catalog_set_project(NULL);
    if (had_types) {
        type_catalog_set_project(&old_types);
    }
    registry_set_packs(old_packs, acount(old_packs));
    afree(old_packs);
}
TEST_CASE(port_schema_preserves_color_components_enum_values_and_pure_field_edits)
{
    asset = behavior_asset_new("Material");
    asset->domain = BEHAVIOR_DOMAIN_MATERIAL;
    const node_desc *desc = registry_node_by_name("Shader Surface Output");
    REQUIRE(desc);
    node *surface = graph_add_node(asset->graph, desc);
    REQUIRE(surface);
    REQUIRE(shader_graph_init_node(asset->graph, surface));
    uint64_t graph = asset->graph->graph_id, node_id = surface->id,
             pin = surface->input_pins[0].id;
    char *source = behavior_asset_save(asset);
    REQUIRE(source);
    snprintf(root, sizeof root, "/tmp/chibi-port-schema-XXXXXX");
    REQUIRE(mkdtemp(root));
    docs = workspace_documents_new(root, NULL);
    REQUIRE(docs);
    REQUIRE(workspace_document_draft(docs, "Material.kdl", source, &handle) == WORKSPACE_DOCUMENT_OK);
    semantic = document_semantics_new(docs, handle, 1, &catalog, error, sizeof error);
    REQUIRE(semantic && document_semantics_valid(semantic));
    REQUIRE(!document_semantics_presentation(semantic));
    document_port_field *fields;
    document_port_choice *choices;
    int count, choice_count, total;
    REQUIRE(document_port_schema(
        semantic,
        graph,
        node_id,
        pin,
        false,
        0,
        256,
        &storage,
        &fields,
        &count,
        &choices,
        &choice_count,
        &total,
        error,
        sizeof error
    ));
    REQUIRE(count == 3 && total == 3 && choice_count == 0);
    REQUIRE(
        !strcmp(fields[0].path, "x") && !strcmp(fields[1].path, "y") &&
        !strcmp(fields[2].path, "z")
    );
    for (int i = 0; i < count; i++) {
        REQUIRE(
            fields[i].color && fields[i].editable && !fields[i].range &&
            fields[i].storage == PIN_F32
        );
    }
    REQUIRE(document_port_schema(
        semantic,
        graph,
        node_id,
        pin,
        false,
        1,
        1,
        &storage,
        &fields,
        &count,
        &choices,
        &choice_count,
        &total,
        error,
        sizeof error
    ));
    REQUIRE(count == 1 && total == 3 && !strcmp(fields[0].path, "y"));
    char *candidate = NULL;
    REQUIRE(
        document_port_prepare_field(docs, handle, semantic, graph, node_id, pin, false, "y", "0.25", &candidate, error, sizeof error)
    );
    behavior_asset *after = behavior_asset_load(candidate);
    REQUIRE(after);
    node *changed = graph_node_by_id(after->graph, node_id);
    REQUIRE(changed);
    const float *original = surface->input_pins[0].default_value.ptr,
                *value = changed->input_pins[0].default_value.ptr;
    REQUIRE(value[0] == original[0] && value[1] == .25f && value[2] == original[2]);
    free(candidate);
    behavior_asset_free(after);
    candidate = NULL;
    REQUIRE(!document_port_prepare_field(
        docs,
        handle,
        semantic,
        graph,
        node_id,
        pin,
        false,
        "missing",
        "0.25",
        &candidate,
        error,
        sizeof error
    ));
    REQUIRE(!candidate);
    bool root_color = document_port_prepare_field(
        docs,
        handle,
        semantic,
        graph,
        node_id,
        pin,
        false,
        "",
        "x 0.125\ny 0.25\nz 0.5\n",
        &candidate,
        error,
        sizeof error
    );
    if (!root_color) {
        fprintf(stderr, "root color candidate: %s\n", error);
    }
    REQUIRE(root_color);
    after = behavior_asset_load(candidate);
    REQUIRE(after);
    changed = graph_node_by_id(after->graph, node_id);
    REQUIRE(changed);
    value = changed->input_pins[0].default_value.ptr;
    REQUIRE(value[0] == .125f && value[1] == .25f && value[2] == .5f);
    free(candidate);
    behavior_asset_free(after);
    candidate = NULL;
    REQUIRE(!document_port_prepare_field(
        docs,
        handle,
        semantic,
        graph,
        node_id,
        pin,
        false,
        "",
        "\"not a vector\"",
        &candidate,
        error,
        sizeof error
    ));
    REQUIRE(!candidate);
    int enum_pin = -1;
    for (int i = 0; i < surface->input_count; i++) {
        if (surface->input_pins[i].type_name &&
            !strcmp(surface->input_pins[i].type_name, "ShaderAlphaMode")) {
            enum_pin = i;
        }
    }
    REQUIRE(enum_pin >= 0);
    REQUIRE(document_port_schema(
        semantic,
        graph,
        node_id,
        surface->input_pins[enum_pin].id,
        false,
        0,
        1,
        &storage,
        &fields,
        &count,
        &choices,
        &choice_count,
        &total,
        error,
        sizeof error
    ));
    REQUIRE(count == 1 && choice_count >= 2 && total == 1);
    const enum_desc *enumeration = type_catalog_enum_find("ShaderAlphaMode");
    REQUIRE(enumeration && choice_count == enumeration->value_count);
    for (int i = 0; i < choice_count; i++) {
        REQUIRE(
            atoi(choices[i].value) == enumeration->values[i].value &&
            !strcmp(choices[i].name, enumeration->values[i].name)
        );
    }
    workspace_document_view view;
    REQUIRE(workspace_document_read(docs, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == 1 && !view.can_undo && !strcmp(view.text, source));
    free(source);
    return true;
}
void test_document_port_schema(void)
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(port_schema_preserves_color_components_enum_values_and_pure_field_edits);
    pu_setup(NULL, NULL);
}
