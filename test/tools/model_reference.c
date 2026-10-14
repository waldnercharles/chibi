#include "authoring/visual_model_definition.h"
#include "execution/function.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/shader/shader_graph.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const registry_pack **previous;
static type_catalog_pack previous_types;
static bool had_types;
static workspace_documents *documents;
static int document;
static behavior_asset *asset;
static char *source, *candidate, *canonical;
static visual_model_reference owned;
static unsigned char *before, *after;
static native_type_catalog *types;
static native_capabilities *capabilities;
static void setup()
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous, registry_pack_at(i));
    }
    const type_catalog_pack *project = type_catalog_project();
    had_types = project != NULL;
    if (project) {
        previous_types = *project;
    }
    const registry_pack *packs[] = { &MODEL_PACK, &SHADER_PACK };
    registry_set_packs(packs, 2);
    type_catalog_pack shader_types = { .types = SHADER_PACK.types,
                                       .type_count = SHADER_PACK.type_count,
                                       .enums = SHADER_PACK.enums,
                                       .enum_count = SHADER_PACK.enum_count };
    type_catalog_set_project(&shader_types);
    documents = workspace_documents_new(NULL, NULL);
    asset = behavior_asset_new("ReferenceModel");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    node *sphere = graph_add_node(asset->graph, &NODE_ModelSphere);
    node *output = model_graph_add_output(asset->graph);
    if (graph_connect_data(asset->graph, sphere, 0, output, 0)) {
        abort();
    }
    source = behavior_asset_save(asset);
    behavior_asset_free(asset);
    asset = NULL;
    if (!source || workspace_document_memory(documents, source, &document) !=
                       WORKSPACE_DOCUMENT_OK) {
        abort();
    }
}
static void cleanup()
{
    native_capabilities_free(capabilities);
    capabilities = NULL;
    native_type_catalog_free(types);
    types = NULL;
    visual_model_reference_free(&owned);
    behavior_asset_free(asset);
    asset = NULL;
    free(source);
    source = NULL;
    free(candidate);
    candidate = NULL;
    free(canonical);
    canonical = NULL;
    free(before);
    before = NULL;
    free(after);
    after = NULL;
    workspace_documents_free(documents);
    documents = NULL;
    type_catalog_set_project(had_types ? &previous_types : NULL);
    registry_set_packs(previous, acount(previous));
    afree(previous);
}
TEST_CASE(model_reference_candidate_roundtrip_preserves_alignment_and_document_history)
{
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK);
    shader_error error = { 0 };
    REQUIRE(visual_model_reference_read(documents, document, view.revision, &owned, &error));
    REQUIRE(owned.image && !*owned.image && owned.height == 2 && !owned.locked);
    visual_model_reference_free(&owned);
    /* Retain a real redo branch, not merely an empty undo stack. */
    REQUIRE(
        workspace_document_edit(
            documents,
            document,
            view.revision,
            (int)view.bytes,
            (int)view.bytes,
            "\n"
        ) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_undo(documents, document, view.revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.can_redo && !view.transaction);
    int revision = view.revision;
    size_t before_size, after_size;
    REQUIRE(workspace_documents_snapshot(documents, &before, &before_size) == WORKSPACE_DOCUMENT_OK);
    visual_model_reference reference = {
        .image = "content/images/参考 camera.png",
        .center_x = -.42f,
        .center_y = 1.25f,
        .height = 3.5f,
        .angle = -37.25f,
        .locked = true
    };
    candidate = visual_model_reference_prepare(documents, document, revision, &reference, &error);
    REQUIRE(candidate);
    REQUIRE(workspace_documents_snapshot(documents, &after, &after_size) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(before_size == after_size && !memcmp(before, after, before_size));
    REQUIRE(workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == revision && view.can_redo && !strcmp(view.text, source));
    asset = behavior_asset_load(candidate);
    REQUIRE(asset && asset->domain == BEHAVIOR_DOMAIN_MODEL);
    REQUIRE(!strcmp(asset->reference.image, reference.image));
    REQUIRE(
        asset->reference.center[0] == reference.center_x &&
        asset->reference.center[1] == reference.center_y
    );
    REQUIRE(
        asset->reference.span == reference.height &&
        asset->reference.angle == reference.angle && asset->reference.locked
    );
    canonical = behavior_asset_save(asset);
    REQUIRE(canonical && !strcmp(candidate, canonical));
    int other;
    REQUIRE(workspace_document_memory(documents, candidate, &other) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(documents, other, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(visual_model_reference_read(documents, other, view.revision, &owned, &error));
    REQUIRE(!strcmp(owned.image, reference.image) && owned.locked);
    REQUIRE(
        owned.center_x == reference.center_x && owned.center_y == reference.center_y &&
        owned.height == reference.height && owned.angle == reference.angle
    );
    visual_model_reference_free(&owned);
    reference.image = "";
    free(candidate);
    candidate = visual_model_reference_prepare(documents, other, view.revision, &reference, &error);
    REQUIRE(candidate);
    behavior_asset_free(asset);
    asset = behavior_asset_load(candidate);
    REQUIRE(asset && !asset->reference.image && !asset->reference.locked);
    REQUIRE(
        workspace_document_edit(documents, other, view.revision, 0, (int)view.bytes, candidate) ==
        WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(workspace_document_read(documents, other, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(visual_model_reference_read(documents, other, view.revision, &owned, &error));
    REQUIRE(
        owned.image && !*owned.image && owned.height == 2 && !owned.locked &&
        owned.center_x == 0 && owned.center_y == 0 && owned.angle == 0
    );
    return true;
}
TEST_CASE(model_reference_refuses_stale_paths_and_nonfinite_alignment_without_mutation)
{
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK);
    int revision = view.revision;
    shader_error error = { 0 };
    visual_model_reference reference = { .image = "content/images/ref.png",
                                         .height = 2 };
    size_t before_size, after_size;
    REQUIRE(workspace_documents_snapshot(documents, &before, &before_size) == WORKSPACE_DOCUMENT_OK);
    const char *paths[] = { "../ref.png",         "/ref.png",
                            "content//ref.png",   "content/./ref.png",
                            "content/../ref.png", "C:ref.png",
                            "content\\ref.png",   "content/images/" };
    for (int i = 0; i < (int)(sizeof paths / sizeof *paths); i++) {
        reference.image = paths[i];
        candidate = visual_model_reference_prepare(documents, document, revision, &reference, &error);
        REQUIRE(!candidate && error.message[0]);
    }
    reference.image = "content/images/ref.png";
    const float heights[] = { NAN, INFINITY, -INFINITY, 0, -1, 1000001 };
    for (int i = 0; i < (int)(sizeof heights / sizeof *heights); i++) {
        reference.height = heights[i];
        candidate = visual_model_reference_prepare(documents, document, revision, &reference, &error);
        REQUIRE(!candidate && error.message[0]);
    }
    reference.height = 2;
    reference.center_x = NAN;
    REQUIRE(!visual_model_reference_prepare(documents, document, revision, &reference, &error));
    reference.center_x = 0;
    reference.angle = INFINITY;
    REQUIRE(!visual_model_reference_prepare(documents, document, revision, &reference, &error));
    reference.angle = 0;
    REQUIRE(workspace_documents_snapshot(documents, &after, &after_size) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(before_size == after_size && !memcmp(before, after, before_size));
    REQUIRE(
        workspace_document_edit(
            documents,
            document,
            revision,
            (int)view.bytes,
            (int)view.bytes,
            "\n"
        ) == WORKSPACE_DOCUMENT_OK
    );
    candidate = visual_model_reference_prepare(documents, document, revision, &reference, &error);
    REQUIRE(!candidate && strstr(error.message, "stale"));
    REQUIRE(!visual_model_reference_read(documents, document, revision, &owned, &error));
    REQUIRE(!owned.image && strstr(error.message, "stale"));
    return true;
}
TEST_CASE(model_reference_reflection_and_optional_host_contract_are_complete)
{
    const type_desc *layout = &VISUAL_MODEL_REFERENCE_LAYOUT;
    REQUIRE(!strcmp(layout->name, "ModelReference") && layout->size == sizeof(visual_model_reference));
    const char *names[] = { "image",  "center_x", "center_y",
                            "height", "angle",    "locked" };
    const size_t offsets[] = { offsetof(visual_model_reference, image),
                               offsetof(visual_model_reference, center_x),
                               offsetof(visual_model_reference, center_y),
                               offsetof(visual_model_reference, height),
                               offsetof(visual_model_reference, angle),
                               offsetof(visual_model_reference, locked) };
    REQUIRE(layout->field_count == 6);
    for (int i = 0; i < 6; i++) {
        REQUIRE(
            !strcmp(layout->fields[i].name, names[i]) &&
            layout->fields[i].offset == offsets[i]
        );
        REQUIRE(
            layout->fields[i].type == (i == 0   ? PIN_STR
                                       : i == 5 ? PIN_BOOL
                                                : PIN_F32)
        );
    }
    REQUIRE(VISUAL_MODEL_TYPES[4].layout == layout && VISUAL_MODEL_TYPES[4].storage == PIN_STRUCT);
    const native_definition_pack *pack = &VISUAL_MODEL_DEFINITION_PACK;
    REQUIRE(
        pack->layout_count == 5 && pack->definition_count >= 8 &&
        pack->binding_count == pack->definition_count
    );
    bool found_layout = false, found_read = false, found_prepare = false;
    for (int i = 0; i < pack->layout_count; i++) {
        found_layout |= pack->layouts[i] == layout;
    }
    REQUIRE(found_layout);
    pin_value outputs[32], untouched[32];
    memset(outputs, 0xa5, sizeof outputs);
    memcpy(untouched, outputs, sizeof outputs);
    for (int i = 0; i < pack->binding_count; i++) {
        const native_definition_binding *binding = &pack->bindings[i];
        found_read |= !strcmp(binding->canonical_name, "Inspect Model Reference");
        found_prepare |= !strcmp(binding->canonical_name, "Prepare Model Reference");
        const node_host_call *call = binding->implementation.call;
        REQUIRE(call && call->optional_service && call->context);
        const char *error = call->context(NULL, NULL, NULL, outputs);
        REQUIRE(error && strstr(error, "authoring host"));
        REQUIRE(!memcmp(outputs, untouched, sizeof outputs));
    }
    REQUIRE(found_read && found_prepare);
    char error[256];
    types = native_type_catalog_new(VISUAL_MODEL_TYPES, 5, error, sizeof error);
    REQUIRE(types);
    native_capability_pack type_pack = native_type_catalog_pack(types);
    capabilities = native_capabilities_from_definition_packs(&type_pack, 1, pack, 1, error, sizeof error);
    REQUIRE(capabilities);
    return true;
}
void test_model_reference(void)
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(model_reference_candidate_roundtrip_preserves_alignment_and_document_history);
    RUN_TEST_CASE(model_reference_refuses_stale_paths_and_nonfinite_alignment_without_mutation);
    RUN_TEST_CASE(model_reference_reflection_and_optional_host_contract_are_complete);
    pu_clear_setup();
}
