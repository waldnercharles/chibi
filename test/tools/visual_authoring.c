#include "authoring/visual_authoring.h"

#include "authoring/visual_authoring_definition.h"
#include "execution/function.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/shader/shader_graph.h"
#include "host/native_catalog.h"
#include "runtime/color_value.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const registry_pack **previous;
static workspace_documents *documents, *factory_documents;
static char factory_root[1200];
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
    documents = workspace_documents_new(NULL, NULL);
}
static void cleanup()
{
    workspace_documents_free(factory_documents);
    factory_documents = NULL;
    if (factory_root[0]) {
        tool_remove_tree(factory_root);
        factory_root[0] = 0;
    }
    workspace_documents_free(documents);
    type_catalog_set_project(NULL);
    registry_set_packs(previous, acount(previous));
    afree(previous);
}
static int material()
{
    shader_error error = { 0 };
    char *source = visual_authoring_preset_source("Authored", MATERIAL_AUTHORING_METAL, &error);
    int handle = 0;
    if (source) {
        workspace_document_memory(documents, source, &handle);
    }
    free(source);
    return handle;
}
TEST_CASE(visual_defaults_share_document_transactions_and_reject_stale_generations)
{
    int handle = material();
    REQUIRE(handle > 0);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    int initial = view.revision;
    visual_authoring_report report;
    visual_authoring_report_init(&report);
    REQUIRE(visual_authoring_inspect(documents, handle, initial, &report));
    float original = report.shader.defaults[STANDARD_ROUGHNESS][0];
    REQUIRE(workspace_document_begin(documents, handle, initial) == WORKSPACE_DOCUMENT_OK);
    shader_error error = { 0 };
    float values[4] = { .23f };
    char *before = strdup(view.text);
    char *candidate = visual_authoring_prepare_default(
        documents,
        handle,
        initial,
        STANDARD_ROUGHNESS,
        1,
        values,
        &error
    );
    REQUIRE(candidate && strcmp(candidate, before));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == initial && !view.can_undo && !strcmp(view.text, before));
    free(candidate);
    free(before);
    int edited;
    REQUIRE(
        visual_authoring_set_default(documents, handle, initial, STANDARD_ROUGHNESS, 1, values, &edited, &error)
    );
    REQUIRE(edited > initial);
    REQUIRE(
        !visual_authoring_set_default(documents, handle, initial, STANDARD_ROUGHNESS, 1, values, &edited, &error)
    );
    REQUIRE(strstr(error.message, "stale"));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == edited && view.transaction);
    REQUIRE(workspace_document_cancel(documents, handle, edited) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(visual_authoring_inspect(documents, handle, view.revision, &report));
    REQUIRE(fabsf(report.shader.defaults[STANDARD_ROUGHNESS][0] - original) < .00001f);
    REQUIRE(!view.can_undo);
    REQUIRE(
        visual_authoring_set_default(documents, handle, view.revision, STANDARD_ROUGHNESS, 1, values, &edited, &error)
    );
    REQUIRE(workspace_document_undo(documents, handle, edited) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(visual_authoring_inspect(documents, handle, view.revision, &report));
    REQUIRE(fabsf(report.shader.defaults[STANDARD_ROUGHNESS][0] - original) < .00001f);
    REQUIRE(workspace_document_redo(documents, handle, view.revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(visual_authoring_inspect(documents, handle, view.revision, &report));
    REQUIRE(fabsf(report.shader.defaults[STANDARD_ROUGHNESS][0] - .23f) < .00001f);
    visual_authoring_report_free(&report);
    return true;
}
TEST_CASE(visual_report_samples_are_owned_and_invalid_edits_are_atomic)
{
    int handle = material();
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    char *before = strdup(view.text);
    int revision = view.revision;
    visual_authoring_report report;
    visual_authoring_report_init(&report);
    REQUIRE(visual_authoring_inspect(documents, handle, revision, &report));
    float invalid[4] = { NAN };
    int result = -1;
    shader_error error = { 0 };
    REQUIRE(
        !visual_authoring_set_default(documents, handle, revision, STANDARD_ROUGHNESS, 1, invalid, &result, &error)
    );
    REQUIRE(
        !visual_authoring_set_default(documents, handle, revision, STANDARD_BASE_COLOR, 1, invalid, &result, &error)
    );
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == revision && !strcmp(view.text, before));
    REQUIRE(!view.can_undo && result == -1);
    REQUIRE(workspace_document_close(documents, handle) == WORKSPACE_DOCUMENT_OK);
    float first[SHADER_OUTPUT_MAX][4] = { 0 }, second[SHADER_OUTPUT_MAX][4] = { 0 };
    REQUIRE(visual_authoring_sample(&report, .25f, .75f, 2, NULL, NULL, first, &error));
    REQUIRE(visual_authoring_sample(&report, .25f, .75f, 2, NULL, NULL, second, &error));
    REQUIRE(!memcmp(first, second, sizeof first));
    REQUIRE(!visual_authoring_sample(&report, NAN, .5f, 0, NULL, NULL, second, &error));
    REQUIRE(first[SHADER_SURFACE_METALLIC][0] == 1);
    visual_authoring_report_free(&report);
    free(before);
    return true;
}
TEST_CASE(visual_inspection_returns_current_errors_and_refuses_unknown_presets)
{
    int handle = 0;
    REQUIRE(workspace_document_memory(documents, "invalid source", &handle) == WORKSPACE_DOCUMENT_OK);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    visual_authoring_report report;
    visual_authoring_report_init(&report);
    REQUIRE(!visual_authoring_inspect(documents, handle, view.revision, &report));
    REQUIRE(!report.valid && report.error.message[0]);
    shader_error error = { 0 };
    REQUIRE(!visual_authoring_preset_source("Invalid", MATERIAL_AUTHORING_PRESET_COUNT, &error));
    REQUIRE(error.message[0]);
    visual_authoring_report_free(&report);
    return true;
}
static const char *owned_string(void *storage, const char *text)
{
    size_t size = strlen(text) + 1;
    char *result = arena_alloc(storage, size);
    memcpy(result, text, size);
    return result;
}
TEST_CASE(visual_typed_bindings_snapshot_arrays_before_document_close)
{
    char error[256];
    native_type_catalog *types = native_type_catalog_new(VISUAL_AUTHORING_TYPES, 4, error, sizeof error);
    REQUIRE(types);
    native_capability_pack type_pack = native_type_catalog_pack(types);
    native_capabilities *catalog = host_native_catalog_new(
        &type_pack,
        1,
        &VISUAL_AUTHORING_DEFINITION_PACK,
        1,
        error,
        sizeof error
    );
    REQUIRE(catalog);
    native_capabilities_free(catalog);
    native_type_catalog_free(types);
    int handle = material();
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    behavior_asset *typed = behavior_asset_load(view.text);
    REQUIRE(typed);
    node *surface = NULL;
    for (int i = 0; i < acount(typed->graph->nodes); i++) {
        node *n = typed->graph->nodes[i];
        if (!strcmp(n->desc->name, "Shader Surface Output")) {
            surface = n;
        }
    }
    REQUIRE(surface);
    /* Both ends use the authored scalar storage. Retyping a producer alone
     * correctly removes its incompatible old float connection. */
    REQUIRE(
        !graph_set_pin_type(typed->graph, surface, 0, SHADER_SURFACE_ROUGHNESS, PIN_I32, NULL)
    );
    REQUIRE(!graph_set_pin_type(typed->graph, surface, 0, SHADER_SURFACE_METALLIC, PIN_BOOL, NULL));
    for (int i = 0; i < acount(typed->graph->nodes); i++) {
        node *n = typed->graph->nodes[i];
        if (strcmp(n->desc->name, "Shader Parameter")) {
            continue;
        }
        if (n->reference_id == STANDARD_ROUGHNESS) {
            REQUIRE(!graph_set_pin_type(typed->graph, n, 1, 0, PIN_I32, NULL));
            REQUIRE(
                !graph_set_pin_default(typed->graph, n, 1, 0, &(pin_value){ .type = PIN_I32, .i = 3 })
            );
            REQUIRE(!graph_connect_data(typed->graph, n, 0, surface, SHADER_SURFACE_ROUGHNESS));
        }
        if (n->reference_id == STANDARD_METALLIC) {
            REQUIRE(!graph_set_pin_type(typed->graph, n, 1, 0, PIN_BOOL, NULL));
            REQUIRE(
                !graph_set_pin_default(typed->graph, n, 1, 0, &(pin_value){ .type = PIN_BOOL, .b = true })
            );
            REQUIRE(!graph_connect_data(typed->graph, n, 0, surface, SHADER_SURFACE_METALLIC));
        }
    }
    char *typed_text = behavior_asset_save(typed);
    behavior_asset_free(typed);
    REQUIRE(typed_text);
    REQUIRE(
        workspace_document_edit(documents, handle, view.revision, 0, (int)view.bytes, typed_text) ==
        WORKSPACE_DOCUMENT_OK
    );
    free(typed_text);
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    arena storage = { 0 };
    behavior_function_context context = { .value_storage = &storage,
                                          .copy_string = owned_string };
    visual_authoring_service service = { .documents = documents };
    pin_value input[] = { { .i = handle }, { .i = view.revision } };
    visual_costs costs = { 0 };
    pin_value output[6] = { { 0 }, { .ptr = &costs }, { 0 }, { 0 }, { 0 }, { 0 } };
    const node_host_call *call = NULL;
    for (int i = 0; i < VISUAL_AUTHORING_DEFINITION_PACK.definition_count; i++) {
        if (!strcmp(VISUAL_AUTHORING_DEFINITION_PACK.definitions[i].source_name, "inspect_visual")) {
            call = VISUAL_AUTHORING_DEFINITION_PACK.bindings[i].implementation.call;
            break;
        }
    }
    REQUIRE(call);
    REQUIRE(!call->context(&context, &service, input, output));
    REQUIRE(output[2].b && !output[3].str[0]);
    pin_array *parameters = output[0].ptr;
    REQUIRE(parameters->count > 0 && costs.nodes > 0);
    bool integer = false, boolean = false, color = false;
    for (int i = 0; i < parameters->count; i++) {
        const visual_parameter *parameter = parameters->items[i].ptr;
        REQUIRE(parameter->editable);
        if (parameter->slot == STANDARD_ROUGHNESS) {
            REQUIRE(
                !strcmp(parameter->kind, "integer") && parameter->width == 1 &&
                parameter->x == 3
            );
            REQUIRE(
                parameter->bounded && parameter->minimum == INT32_MIN &&
                parameter->maximum == INT32_MAX
            );
            integer = true;
        }
        if (parameter->slot == STANDARD_METALLIC) {
            REQUIRE(
                !strcmp(parameter->kind, "boolean") && parameter->width == 1 &&
                parameter->x == 1
            );
            REQUIRE(parameter->bounded && parameter->minimum == 0 && parameter->maximum == 1);
            boolean = true;
        }
        if (parameter->slot == STANDARD_BASE_COLOR) {
            REQUIRE(
                !strcmp(parameter->kind, "vector") &&
                parameter->presentation == SHADER_AUTHORING_COLOR &&
                parameter->width == 3
            );
            color = true;
        }
    }
    REQUIRE(integer && boolean && color);
    shader_error diagnostic = { 0 };
    int revision = view.revision;
    const double limits[] = { INT32_MIN, INT32_MAX };
    for (int i = 0; i < 2; i++) {
        const double exact[4] = { limits[i] };
        REQUIRE(visual_authoring_set_default_exact(
            documents,
            handle,
            revision,
            STANDARD_ROUGHNESS,
            1,
            exact,
            &revision,
            &diagnostic
        ));
        visual_authoring_report report;
        visual_authoring_report_init(&report);
        REQUIRE(visual_authoring_inspect(documents, handle, revision, &report));
        double authored[4];
        REQUIRE(visual_authoring_parameter_values(&report, STANDARD_ROUGHNESS, authored));
        REQUIRE(authored[0] == limits[i]);
        visual_authoring_report_free(&report);
    }
    const double refused[] = { 1.5, (double)INT32_MAX + 1, (double)INT32_MIN - 1, NAN, INFINITY };
    for (int i = 0; i < 5; i++) {
        REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
        char *before = strdup(view.text);
        const double invalid[4] = { refused[i] };
        REQUIRE(!visual_authoring_set_default_exact(
            documents,
            handle,
            revision,
            STANDARD_ROUGHNESS,
            1,
            invalid,
            &revision,
            &diagnostic
        ));
        REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
        REQUIRE(view.revision == revision && !strcmp(view.text, before));
        free(before);
    }
    const double invalid_bool[4] = { 2 };
    REQUIRE(!visual_authoring_set_default_exact(
        documents,
        handle,
        revision,
        STANDARD_METALLIC,
        1,
        invalid_bool,
        &revision,
        &diagnostic
    ));
    for (int i = 0; i < 2; i++) {
        const double exact[4] = { i };
        REQUIRE(visual_authoring_set_default_exact(
            documents,
            handle,
            revision,
            STANDARD_METALLIC,
            1,
            exact,
            &revision,
            &diagnostic
        ));
    }
    const node_host_call *set = NULL;
    for (int i = 0; i < VISUAL_AUTHORING_DEFINITION_PACK.definition_count; i++) {
        if (!strcmp(VISUAL_AUTHORING_DEFINITION_PACK.definitions[i].source_name, "set_visual_default")) {
            set = VISUAL_AUTHORING_DEFINITION_PACK.bindings[i].implementation.call;
        }
    }
    REQUIRE(set);
    pin_value exact_input[] = {
        { .i = handle }, { .i = revision },  { .i = STANDARD_ROUGHNESS },
        { .i = 1 },      { .d = INT32_MAX }, { .d = 0 },
        { .d = 0 },      { .d = 0 }
    };
    pin_value exact_output[3] = { 0 };
    REQUIRE(!set->context(&context, &service, exact_input, exact_output));
    REQUIRE(exact_output[0].b && !exact_output[2].str[0]);
    revision = exact_output[1].i;
    input[1].i = revision;
    REQUIRE(!call->context(&context, &service, input, output) && output[2].b);
    const pin_array *current = output[0].ptr;
    bool exact_max = false;
    for (int i = 0; i < current->count; i++) {
        const visual_parameter *parameter = current->items[i].ptr;
        if (parameter->slot == STANDARD_ROUGHNESS) {
            exact_max = parameter->x == INT32_MAX;
        }
    }
    REQUIRE(exact_max);
    visual_parameter *row = parameters->items[0].ptr;
    char *name = strdup(row->name);
    REQUIRE(workspace_document_close(documents, handle) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!strcmp(row->name, name));
    REQUIRE(row->editable && row->kind && row->kind[0]);
    free(name);
    arena_free(&storage);
    return true;
}
static behavior_asset *linked_source(void *userdata, const behavior_asset *source)
{
    (*(int *)userdata)++;
    behavior_asset *linked = behavior_asset_clone(source);
    shader_error error;
    float values[4] = { .33f };
    if (!shader_authoring_set_default(linked, STANDARD_ROUGHNESS, values, 1, &error)) {
        behavior_asset_free(linked);
        return NULL;
    }
    return linked;
}
static behavior_asset *missing_link(void *, const behavior_asset *)
{
    return NULL;
}
TEST_CASE(visual_linking_owns_its_snapshot_without_mutating_canonical_source)
{
    int handle = material();
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    int revision = view.revision, calls = 0;
    char *text = strdup(view.text);
    visual_authoring_report report;
    visual_authoring_report_init(&report);
    REQUIRE(
        visual_authoring_inspect_linked(documents, handle, revision, linked_source, &calls, &report)
    );
    REQUIRE(calls == 1 && fabsf(report.shader.defaults[STANDARD_ROUGHNESS][0] - .33f) < .00001f);
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == revision && !strcmp(view.text, text));
    REQUIRE(
        !visual_authoring_inspect_linked(documents, handle, revision, missing_link, NULL, &report)
    );
    REQUIRE(!report.source && !report.valid && report.error.message[0]);
    visual_authoring_report_free(&report);
    free(text);
    return true;
}
TEST_CASE(linear_colors_roundtrip_display_hex_without_losing_alpha)
{
    float color[4] = { .21404114f, 0, 1, .25f }, decoded[4] = { 9, 9, 9, 9 };
    char text[10], error[128];
    REQUIRE(color_value_to_hex(color, text, error, sizeof error));
    REQUIRE(!strcmp(text, "#8000ff40"));
    REQUIRE(color_value_from_hex(text, decoded, error, sizeof error));
    REQUIRE(fabsf(decoded[0] - .2158605f) < .000001f && fabsf(decoded[3] - 64 / 255.f) < .000001f);
    REQUIRE(color_value_from_hex("AbCDef", decoded, error, sizeof error) && decoded[3] == 1);
    float before[4];
    memcpy(before, decoded, sizeof before);
    REQUIRE(!color_value_from_hex("#zzzzzz", decoded, error, sizeof error));
    REQUIRE(!memcmp(before, decoded, sizeof before));
    color[0] = 4;
    color[1] = -1;
    color[3] = 1;
    REQUIRE(color_value_to_hex(color, text, error, sizeof error) && !strcmp(text, "#ff00ffff"));
    color[0] = NAN;
    REQUIRE(!color_value_to_hex(color, text, error, sizeof error));
    return true;
}
TEST_CASE(material_factory_preserves_preset_factors_and_refuses_owned_namespace_drafts)
{
    shader_error diagnostic = { 0 };
    visual_material_settings settings;
    REQUIRE(visual_material_preset_settings(MATERIAL_AUTHORING_METAL, &settings, &diagnostic));
    REQUIRE(settings.metallic == 1 && settings.emission_gain == 1);
    settings.alpha_mode = SHADER_ALPHA_BLEND;
    settings.opacity = .4f;
    settings.emission_rgb[0] = .2f;
    settings.emission_rgb[1] = .4f;
    settings.emission_rgb[2] = .6f;
    settings.emission_gain = 3;
    char *source = visual_material_source("Reserved", &settings, &diagnostic);
    REQUIRE(source);
    behavior_asset *asset = behavior_asset_load(source);
    REQUIRE(asset);
    shader_authoring_report report;
    shader_authoring_report_init(&report);
    REQUIRE(shader_authoring_inspect(asset, &report, &diagnostic));
    REQUIRE(fabsf(report.defaults[STANDARD_EMISSION][1] - 1.2f) < .00001f);
    REQUIRE(fabsf(report.defaults[STANDARD_OPACITY][0] - .4f) < .00001f);
    shader_authoring_report_free(&report);
    behavior_asset_free(asset);
    snprintf(factory_root, sizeof factory_root, "%s/material-factory-XXXXXX", tool_root());
    REQUIRE(mkdtemp(factory_root));
    char content[1400], path[1500], error[512];
    snprintf(content, sizeof content, "%s/content", factory_root);
    REQUIRE(!mkdir(content, 0700));
    factory_documents = workspace_documents_new(factory_root, NULL);
    int pending = 0;
    REQUIRE(workspace_document_memory(factory_documents, source, &pending) == WORKSPACE_DOCUMENT_OK);
    free(source);
    REQUIRE(
        !visual_material_create(factory_documents, content, factory_root, "Reserved", &settings, error, sizeof error)
    );
    REQUIRE(workspace_document_close(factory_documents, pending) == WORKSPACE_DOCUMENT_OK);
    char *created = visual_material_create(
        factory_documents,
        content,
        factory_root,
        "Reserved",
        &settings,
        error,
        sizeof error
    );
    REQUIRE(created && !strcmp(created, "content/materials/Reserved.kdl"));
    free(created);
    REQUIRE(
        !visual_material_create(factory_documents, content, factory_root, "Reserved", &settings, error, sizeof error)
    );
    REQUIRE(
        workspace_document_draft(factory_documents, "content/materials/Pending.kdl", "", &pending) ==
        WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(
        !visual_material_create(factory_documents, content, factory_root, "Pending", &settings, error, sizeof error)
    );
    snprintf(path, sizeof path, "%s/materials/Pending.kdl", content);
    REQUIRE(access(path, F_OK) != 0);
    REQUIRE(
        !visual_material_create(factory_documents, content, "/other-workspace", "Outside", &settings, error, sizeof error)
    );
    settings.emission_gain = NAN;
    REQUIRE(!visual_material_source("Invalid", &settings, &diagnostic));
    return true;
}
TEST_CASE(packed_color_preserves_rgba_bytes_and_refuses_nonintegers_without_mutation)
{
    char text[10] = "unchanged", error[128];
    uint32_t color = 0x12345678u;
    REQUIRE(color_value_packed_to_hex(color, text, error, sizeof error));
    REQUIRE(!strcmp(text, "#12345678"));
    REQUIRE(color_value_packed_from_hex(text, &color, error, sizeof error) && color == 0x12345678u);
    REQUIRE(
        color_value_packed_from_hex("AbCDef", &color, error, sizeof error) && color == 0xabcdefffu
    );
    REQUIRE(color_value_packed_from_hex("#00000000", &color, error, sizeof error) && color == 0);
    REQUIRE(
        color_value_packed_to_hex(UINT32_MAX, text, error, sizeof error) &&
        !strcmp(text, "#ffffffff")
    );
    const double refused[] = { -1, 4294967296.0, 1.5, NAN, INFINITY, -INFINITY };
    for (int i = 0; i < (int)(sizeof refused / sizeof *refused); i++) {
        strcpy(text, "unchanged");
        REQUIRE(!color_value_packed_to_hex(refused[i], text, error, sizeof error));
        REQUIRE(!strcmp(text, "unchanged") && *error);
    }
    const char *invalid[] = { "#12345", "#123456789", "#12xx5678", " 123456", "#123456 " };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        color = 0x12345678u;
        REQUIRE(!color_value_packed_from_hex(invalid[i], &color, error, sizeof error));
        REQUIRE(color == 0x12345678u && *error);
    }
    return true;
}
TEST_CASE(test_tool_visual_authoring_missing_host_is_optional_and_refuses_without_effects)
{
    pin_value output[32], unchanged[32];
    memset(output, 0xa5, sizeof output);
    memcpy(unchanged, output, sizeof output);
    for (int i = 0; i < VISUAL_AUTHORING_DEFINITION_PACK.binding_count; i++) {
        const node_host_call *call = VISUAL_AUTHORING_DEFINITION_PACK
                                         .bindings[i]
                                         .implementation.call;
        REQUIRE(call && call->optional_service && call->context);
        const char *error = call->context(NULL, NULL, NULL, output);
        REQUIRE(error && strstr(error, "authoring host"));
        REQUIRE(!memcmp(output, unchanged, sizeof output));
    }
    return true;
}
static behavior_asset *invalid_link(void *, const behavior_asset *source)
{
    char *text = behavior_asset_save(source);
    behavior_asset *linked = behavior_asset_load(text);
    free(text);
    for (int i = 0; i < acount(linked->graph->nodes); i++) {
        node *n = linked->graph->nodes[i];
        if (!strcmp(n->desc->name, "Shader Surface Output")) {
            n->input_pins[SHADER_SURFACE_ALPHA_MODE].default_value.i = 99;
        }
    }
    return linked;
}
TEST_CASE(visual_diagnostic_identity_requires_original_source_error_and_owned_revision)
{
    int handle = material();
    REQUIRE(handle > 0);
    workspace_document_view before;
    REQUIRE(workspace_document_read(documents, handle, &before) == WORKSPACE_DOCUMENT_OK);
    visual_authoring_report report;
    visual_authoring_report_init(&report);
    REQUIRE(
        !visual_authoring_inspect_linked(documents, handle, before.revision, invalid_link, NULL, &report)
    );
    REQUIRE(report.error.node && !strcmp(report.error.message, "Invalid Alpha Mode"));
    REQUIRE(!report.diagnostic_graph && !report.diagnostic_node);
    char *invalid = behavior_asset_save(report.source);
    visual_authoring_report_free(&report);
    int bad = 0;
    REQUIRE(workspace_document_memory(documents, invalid, &bad) == WORKSPACE_DOCUMENT_OK);
    free(invalid);
    REQUIRE(!visual_authoring_inspect(documents, bad, 1, &report));
    REQUIRE(
        report.error.node && report.diagnostic_graph == report.source->graph->graph_id &&
        report.diagnostic_node == report.error.node
    );
    REQUIRE(report.document == bad && report.revision == 1);
    REQUIRE(workspace_document_close(documents, bad) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(graph_node_by_id(report.source->graph, report.diagnostic_node));
    REQUIRE(!visual_authoring_inspect(documents, handle, before.revision + 1, &report));
    REQUIRE(
        !report.diagnostic_graph && !report.diagnostic_node &&
        strstr(report.error.message, "stale")
    );
    visual_authoring_report_free(&report);
    workspace_document_view after;
    REQUIRE(workspace_document_read(documents, handle, &after) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(after.revision == before.revision && !after.can_undo);
    return true;
}
void test_tool_visual_authoring()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(linear_colors_roundtrip_display_hex_without_losing_alpha);
    RUN_TEST_CASE(visual_defaults_share_document_transactions_and_reject_stale_generations);
    RUN_TEST_CASE(visual_report_samples_are_owned_and_invalid_edits_are_atomic);
    RUN_TEST_CASE(visual_inspection_returns_current_errors_and_refuses_unknown_presets);
    RUN_TEST_CASE(visual_typed_bindings_snapshot_arrays_before_document_close);
    RUN_TEST_CASE(visual_linking_owns_its_snapshot_without_mutating_canonical_source);
    RUN_TEST_CASE(material_factory_preserves_preset_factors_and_refuses_owned_namespace_drafts);
    RUN_TEST_CASE(packed_color_preserves_rgba_bytes_and_refuses_nonintegers_without_mutation);
    RUN_TEST_CASE(test_tool_visual_authoring_missing_host_is_optional_and_refuses_without_effects);
    RUN_TEST_CASE(visual_diagnostic_identity_requires_original_source_error_and_owned_revision);
    pu_clear_setup();
}
