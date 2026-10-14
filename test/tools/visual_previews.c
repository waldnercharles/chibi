#include "authoring/image_prepare.h"
#include "authoring/shader_prepare.h"
#include "authoring/visual_authoring_preview.h"
#include "authoring/visual_authoring_preview_definition.h"
#include "authoring/visual_model_preview.h"
#include "authoring/workspace_resources.h"
#include "execution/function.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "graph/nodes/core_nodes.h"
#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "render/shader_catalog.h"
#include "render/shader_catalog_records.h"
#include "render/shader_publish.h"
#include "support/array.h"
#include "support/file_io.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const registry_pack **previous;
static workspace_documents *documents;
static preview_world *live;
static visual_previews *previews;
static void setup()
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &SHADER_PACK, &CORE_FLOW_PACK, &MODEL_PACK };
    registry_set_packs(packs, 3);
    type_catalog_pack types = { .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
    type_catalog_set_project(&types);
    documents = workspace_documents_new(NULL, NULL);
    live = preview_world_new(NULL);
    previews = visual_previews_new(documents, live, ".", ".");
}
static void cleanup()
{
    visual_previews_free(previews);
    preview_world_free(live);
    workspace_documents_free(documents);
    authoring_shader_catalog_clear();
    type_catalog_set_project(NULL);
    registry_set_packs(previous, acount(previous));
    afree(previous);
}
static int material()
{
    shader_error error = { 0 };
    char *source = visual_authoring_preset_source("VisualPublication", MATERIAL_AUTHORING_MATTE, &error);
    int document = 0;
    if (source) {
        workspace_document_memory(documents, source, &document);
    }
    free(source);
    return document;
}
TEST_CASE(visual_publication_owns_applied_generations_and_refuses_stale_edits)
{
    int document = material();
    REQUIRE(document > 0);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK);
    int initial = view.revision;
    char error[256] = "";
    REQUIRE(visual_preview_publish(previews, document, initial, error, sizeof error));
    REQUIRE(!error[0]);
    preview_definition *definition = preview_find_definition(live, "VisualPublication");
    REQUIRE(definition && definition->applied);
    shader_authoring_report report;
    shader_authoring_report_init(&report);
    shader_error shader = { 0 };
    REQUIRE(shader_authoring_inspect(definition->applied, &report, &shader));
    float original = report.defaults[STANDARD_ROUGHNESS][0];
    float value[4] = { .19f };
    int revision;
    REQUIRE(
        visual_authoring_set_default(documents, document, initial, STANDARD_ROUGHNESS, 1, value, &revision, &shader)
    );
    REQUIRE(!visual_preview_publish(previews, document, initial, error, sizeof error));
    REQUIRE(strstr(error, "stale"));
    REQUIRE(shader_authoring_inspect(definition->applied, &report, &shader));
    REQUIRE(fabsf(report.defaults[STANDARD_ROUGHNESS][0] - original) < .00001f);
    REQUIRE(visual_preview_publish(previews, document, revision, error, sizeof error));
    REQUIRE(shader_authoring_inspect(definition->applied, &report, &shader));
    REQUIRE(fabsf(report.defaults[STANDARD_ROUGHNESS][0] - .19f) < .00001f);
    REQUIRE(workspace_document_close(documents, document) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(shader_authoring_inspect(definition->applied, &report, &shader));
    REQUIRE(fabsf(report.defaults[STANDARD_ROUGHNESS][0] - .19f) < .00001f);
    shader_authoring_report_free(&report);
    return true;
}
TEST_CASE(visual_handles_are_bounded_and_never_reused_without_drawing)
{
    int document = material(), first, next;
    char error[256] = "";
    REQUIRE(!visual_preview_open(previews, document, 1, 32, &first, error, sizeof error));
    REQUIRE(visual_preview_open(previews, document, 32, 32, &first, error, sizeof error));
    visual_preview_view view;
    REQUIRE(visual_preview_read(previews, first, &view));
    REQUIRE(!view.drawn && !view.texture);
    int job = 0;
    REQUIRE(
        !visual_preview_export(previews, first, "evidence", "version", "build", "manifest", &job, error, sizeof error)
    );
    REQUIRE(!job);
    REQUIRE(visual_preview_close(previews, first));
    REQUIRE(!visual_preview_close(previews, first));
    REQUIRE(visual_preview_open(previews, document, 32, 32, &next, error, sizeof error));
    REQUIRE(next > first);
    int handles[16];
    REQUIRE(visual_preview_handles(previews, handles, 16) == 1 && handles[0] == next);
    for (int i = 1; i < 16; i++) {
        REQUIRE(visual_preview_open(previews, document, 32, 32, &first, error, sizeof error));
    }
    REQUIRE(!visual_preview_open(previews, document, 32, 32, &first, error, sizeof error));
    return true;
}
TEST_CASE(authoring_source_cache_follows_its_owned_visual_generation)
{
    shader_error error = { 0 };
    char *first_text = visual_authoring_preset_source("FirstGeneration", MATERIAL_AUTHORING_MATTE, &error);
    char *second_text = visual_authoring_preset_source("SecondGeneration", MATERIAL_AUTHORING_METAL, &error);
    behavior_asset *first = behavior_asset_load(first_text),
                   *second = behavior_asset_load(second_text);
    REQUIRE(first && second);
    REQUIRE(render_shader_upsert(first, &error));
    shader_asset *first_generation = shader_catalog_assets;
    REQUIRE(first_generation[0].authoring && first_generation[0].authoring_free);
    shader_catalog_assets = NULL;
    REQUIRE(render_shader_upsert(second, &error));
    shader_asset *second_generation = shader_catalog_assets;
    REQUIRE(second_generation[0].authoring != first_generation[0].authoring);
    shader_catalog_assets = first_generation;
    unsigned revision = render_shader_revision(),
             ramps = render_shader_ramp_revision();
    REQUIRE(render_shader_upsert(first, &error));
    REQUIRE(render_shader_revision() == revision && render_shader_ramp_revision() == ramps);
    shader_catalog_assets = second_generation;
    authoring_shader_catalog_clear();
    shader_catalog_assets = first_generation;
    REQUIRE(render_shader_material_rename("FirstGeneration", "RetainedGeneration", &error));
    REQUIRE(behavior_asset_set_name(first, "RetainedGeneration") == 0);
    ramps = render_shader_ramp_revision();
    REQUIRE(render_shader_upsert(first, &error));
    REQUIRE(render_shader_ramp_revision() == ramps);
    behavior_asset_free(first);
    behavior_asset_free(second);
    free(first_text);
    free(second_text);
    return true;
}
TEST_CASE(visual_catalog_seeds_private_dependencies_and_preserves_last_good_on_invalid_overlay)
{
    char root[1024], path[1200], error[512];
    snprintf(root, sizeof root, "%s/visual-catalog-XXXXXX", tool_root());
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/shader-libraries", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/materials", root);
    REQUIRE(!mkdir(path, 0700));
    char *library = tool_read("test/fixtures/content/shader-libraries/ShaderMath.kdl");
    char *material = tool_read("test/fixtures/content/materials/PulseRing.kdl");
    REQUIRE(library && material);
    snprintf(path, sizeof path, "%s/content/shader-libraries/ShaderMath.kdl", root);
    tool_write(path, library);
    snprintf(path, sizeof path, "%s/content/materials/PulseRing.kdl", root);
    tool_write(path, material);
    free(library);
    free(material);
    workspace_document_status status;
    workspace_documents *master = workspace_documents_new(root, &status);
    REQUIRE(master);
    workspace_resources *inventory = workspace_resources_new(root, error, sizeof error);
    REQUIRE(inventory);
    REQUIRE(workspace_resources_refresh(inventory).ok);
    visual_previews *service = visual_previews_new(master, NULL, root, root);
    bool seeded = visual_previews_refresh_catalog(service, inventory, error, sizeof error);
    if (!seeded) {
        fprintf(stderr, "visual catalog: %s\n", error);
    }
    REQUIRE(seeded);
    REQUIRE(workspace_document_handles(master, NULL, 0) == 0);
    preview_world *published = visual_previews_world(service);
    REQUIRE(
        preview_find_definition(published, "ShaderMath") &&
        preview_find_definition(published, "PulseRing")
    );
    render_resource_scope scope = { 0 };
    REQUIRE(
        render_resource_scope_enter(visual_previews_resources(service), &scope, error, sizeof error)
    );
    REQUIRE(render_shader_find("PulseRing") > 0);
    REQUIRE(render_resource_scope_leave(&scope, error, sizeof error));
    int document = 0;
    REQUIRE(
        workspace_document_open(master, "content/shader-libraries/ShaderMath.kdl", &document) ==
        WORKSPACE_DOCUMENT_OK
    );
    workspace_document_view view;
    REQUIRE(workspace_document_read(master, document, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_edit(master, document, view.revision, 0, (int)view.bytes, "invalid dependency") ==
        WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(!visual_previews_refresh_catalog(service, inventory, error, sizeof error));
    REQUIRE(visual_previews_world(service) == published && error[0]);
    REQUIRE(workspace_document_handles(master, NULL, 0) == 1);
    visual_previews_free(service);
    workspace_resources_free(inventory);
    workspace_documents_free(master);
    tool_remove_tree(root);
    return true;
}
TEST_CASE(image_loaders_retain_isolated_roots_and_refuse_links_special_files)
{
    char root[1024], first[1200], second[1200], path[1400], error[256];
    snprintf(root, sizeof root, "%s/image-loaders-XXXXXX", tool_root());
    REQUIRE(mkdtemp(root));
    snprintf(first, sizeof first, "%s/first", root);
    REQUIRE(!mkdir(first, 0700));
    snprintf(second, sizeof second, "%s/second", root);
    REQUIRE(!mkdir(second, 0700));
    CF_Pixel pixel = { .r = 255, .g = 0, .b = 0, .a = 255 };
    CF_Image image = { 1, 1, &pixel };
    void *png = NULL;
    int bytes = 0;
    REQUIRE(!cf_is_error(cf_image_save_png_to_memory(&image, &png, &bytes)));
    snprintf(path, sizeof path, "%s/pixel.png", first);
    FILE *file = fopen(path, "wb");
    REQUIRE(file);
    REQUIRE(fwrite(png, 1, (size_t)bytes, file) == (size_t)bytes);
    REQUIRE(!fclose(file));
    cf_free(png);
    render_image_loader a, b, retained;
    REQUIRE(image_prepare_loader_new(first, &a, error, sizeof error));
    REQUIRE(image_prepare_loader_new(second, &b, error, sizeof error));
    REQUIRE(render_image_loader_copy(a, &retained));
    render_image_loader_release(&a);
    shader_image_view decoded = { 0 };
    REQUIRE(retained.read(retained.context, "content/pixel.png", &decoded, error, sizeof error));
    REQUIRE(decoded.width == 1 && decoded.pixels[0] == 255);
    free((void *)decoded.pixels);
    REQUIRE(!b.read(b.context, "content/pixel.png", &decoded, error, sizeof error));
    REQUIRE(
        !retained.read(retained.context, "content/../pixel.png", &decoded, error, sizeof error)
    );
    snprintf(path, sizeof path, "%s/link", first);
    REQUIRE(!symlink(second, path));
    REQUIRE(
        !retained.read(retained.context, "content/link/pixel.png", &decoded, error, sizeof error)
    );
    snprintf(path, sizeof path, "%s/special.png", first);
    REQUIRE(!mkfifo(path, 0600));
    REQUIRE(!retained.read(retained.context, "content/special.png", &decoded, error, sizeof error));
    render_image_loader_release(&retained);
    render_image_loader_release(&retained);
    render_image_loader_release(&b);
    tool_remove_tree(root);
    return true;
}
TEST_CASE(model_preview_inspection_validates_parts_axes_and_canonical_generation_without_drawing)
{
    behavior_asset *asset = behavior_asset_new("InspectedSphere");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    node *sphere = graph_add_node(asset->graph, &NODE_ModelSphere),
         *output = model_graph_add_output(asset->graph);
    REQUIRE(graph_connect_data(asset->graph, sphere, 0, output, 0) == 0);
    char *source = behavior_asset_save(asset);
    behavior_asset_free(asset);
    REQUIRE(source);
    int document = 0;
    REQUIRE(workspace_document_memory(documents, source, &document) == WORKSPACE_DOCUMENT_OK);
    free(source);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK);
    char error[256];
    REQUIRE(visual_preview_publish(previews, document, view.revision, error, sizeof error));
    int handle = 0;
    REQUIRE(visual_preview_open(previews, document, 160, 120, &handle, error, sizeof error));
    REQUIRE(
        visual_preview_model_inspection(previews, handle, view.revision, -1, 1, .25f, error, sizeof error)
    );
    REQUIRE(
        !visual_preview_model_inspection(previews, handle, view.revision, 999, 1, .25f, error, sizeof error)
    );
    REQUIRE(
        !visual_preview_model_inspection(previews, handle, view.revision, -1, 4, .25f, error, sizeof error)
    );
    REQUIRE(
        !visual_preview_model_inspection(previews, handle, view.revision, -1, 1, NAN, error, sizeof error)
    );
    REQUIRE(
        !visual_preview_model_inspection(previews, handle, view.revision + 1, -1, 1, 0, error, sizeof error)
    );
    visual_preview_view observed;
    REQUIRE(visual_preview_read(previews, handle, &observed));
    REQUIRE(
        observed.model_part == -1 && observed.section_axis == 1 &&
        observed.section_offset == .25f && !observed.drawn
    );
    REQUIRE(visual_preview_close(previews, handle));
    return true;
}
static bool corners_equal(const CF_V2 *actual, const CF_V2 *expected)
{
    for (int i = 0; i < 4; i++) {
        if (fabsf(actual[i].x - expected[i].x) > .000001f ||
            fabsf(actual[i].y - expected[i].y) > .000001f) {
            return false;
        }
    }
    return true;
}
TEST_CASE(model_reference_corners_preserve_aspect_source_rotation_and_camera_pan)
{
    visual_model_reference reference = { .image = "images/reference.png", .height = 2 };
    engine_viewport_camera camera = { .span_width = 8, .span_height = 4 };
    CF_V2 corners[4];
    /* Image pixel dimensions preserve aspect in model units, independent of
     * camera aspect. Corner order follows the image's top-left clockwise. */
    const CF_V2 wide[] = { { -.5f, .5f }, { .5f, .5f }, { .5f, -.5f }, { -.5f, -.5f } };
    REQUIRE(visual_model_reference_corners(&reference, 400, 200, &camera, corners));
    REQUIRE(corners_equal(corners, wide));
    const CF_V2 tall[] = { { -.125f, .5f },
                           { .125f, .5f },
                           { .125f, -.5f },
                           { -.125f, -.5f } };
    REQUIRE(visual_model_reference_corners(&reference, 100, 200, &camera, corners));
    REQUIRE(corners_equal(corners, tall));
    reference.center_x = 1;
    reference.center_y = 2;
    camera.center_x = 1;
    camera.center_y = -2; /* Viewport camera Y is down; source Y is up. */
    REQUIRE(visual_model_reference_corners(&reference, 400, 200, &camera, corners));
    REQUIRE(corners_equal(corners, wide));
    reference.angle = 1.5707963267948966f;
    const CF_V2 rotated[] = { { -.25f, -1 }, { -.25f, 1 }, { .25f, 1 }, { .25f, -1 } };
    REQUIRE(visual_model_reference_corners(&reference, 400, 200, &camera, corners));
    REQUIRE(corners_equal(corners, rotated));
    camera.center_x += .5f;
    camera.center_y += .25f;
    const CF_V2 panned[] = { { -.375f, -.875f },
                             { -.375f, 1.125f },
                             { .125f, 1.125f },
                             { .125f, -.875f } };
    REQUIRE(visual_model_reference_corners(&reference, 400, 200, &camera, corners));
    REQUIRE(corners_equal(corners, panned));
    /* Uniformly increasing camera spans halves displacement from screen center. */
    camera.span_width *= 2;
    camera.span_height *= 2;
    const CF_V2 zoomed[] = { { -.1875f, -.4375f },
                             { -.1875f, .5625f },
                             { .0625f, .5625f },
                             { .0625f, -.4375f } };
    REQUIRE(visual_model_reference_corners(&reference, 400, 200, &camera, corners));
    REQUIRE(corners_equal(corners, zoomed));
    return true;
}
TEST_CASE(model_reference_corner_refusals_preserve_all_output_bytes)
{
    visual_model_reference reference = { .height = 2 };
    engine_viewport_camera camera = { .span_width = 8, .span_height = 4 };
    const CF_V2 sentinel[] = { { 17, -19 }, { 23, -29 }, { 31, -37 }, { 41, -43 } };
    CF_V2 corners[4];
    memcpy(corners, sentinel, sizeof corners);
    REQUIRE(!visual_model_reference_corners(&reference, 0, 200, &camera, corners));
    REQUIRE(!visual_model_reference_corners(&reference, 200, 0, &camera, corners));
    REQUIRE(!visual_model_reference_corners(&reference, -1, 200, &camera, corners));
    REQUIRE(!visual_model_reference_corners(NULL, 200, 200, &camera, corners));
    REQUIRE(!visual_model_reference_corners(&reference, 200, 200, NULL, corners));
    const float invalid[] = { NAN, INFINITY, -INFINITY, 0, -1 };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        reference.height = invalid[i];
        REQUIRE(!visual_model_reference_corners(&reference, 200, 200, &camera, corners));
        REQUIRE(!memcmp(corners, sentinel, sizeof corners));
        reference.height = 2;
        camera.span_width = invalid[i];
        REQUIRE(!visual_model_reference_corners(&reference, 200, 200, &camera, corners));
        REQUIRE(!memcmp(corners, sentinel, sizeof corners));
        camera.span_width = 8;
        camera.span_height = invalid[i];
        REQUIRE(!visual_model_reference_corners(&reference, 200, 200, &camera, corners));
        REQUIRE(!memcmp(corners, sentinel, sizeof corners));
        camera.span_height = 4;
    }
    reference.center_x = NAN;
    REQUIRE(!visual_model_reference_corners(&reference, 200, 200, &camera, corners));
    reference.center_x = 0;
    reference.center_y = INFINITY;
    REQUIRE(!visual_model_reference_corners(&reference, 200, 200, &camera, corners));
    reference.center_y = 0;
    reference.angle = NAN;
    REQUIRE(!visual_model_reference_corners(&reference, 200, 200, &camera, corners));
    reference.angle = 0;
    camera.center_x = INFINITY;
    REQUIRE(!visual_model_reference_corners(&reference, 200, 200, &camera, corners));
    camera.center_x = 0;
    camera.center_y = NAN;
    REQUIRE(!visual_model_reference_corners(&reference, 200, 200, &camera, corners));
    camera.center_y = 0;
    reference.height = FLT_MAX;
    REQUIRE(!visual_model_reference_corners(&reference, 400, 200, &camera, corners));
    reference.height = 2;
    camera.span_width = FLT_MIN;
    REQUIRE(!visual_model_reference_corners(&reference, 400, 200, &camera, corners));
    REQUIRE(!memcmp(corners, sentinel, sizeof corners));
    return true;
}
TEST_CASE(test_tool_visual_previews_missing_host_is_optional_and_refuses_without_effects)
{
    pin_value output[32], unchanged[32];
    memset(output, 0xa5, sizeof output);
    memcpy(unchanged, output, sizeof output);
    const native_definition_pack *packs[] = { &VISUAL_PREVIEW_DECLARATION_PACK,
                                              &VISUAL_PREVIEW_DEFINITION_PACK };
    for (int p = 0; p < 2; p++) {
        const native_definition_pack *pack = packs[p];
        REQUIRE(
            pack->definition_count == VISUAL_PREVIEW_OPERATION_COUNT &&
            pack->binding_count == pack->definition_count
        );
        bool reference = false, pan = false;
        for (int i = 0; i < pack->binding_count; i++) {
            const native_definition_binding *binding = &pack->bindings[i];
            const native_capability_definition *definition = &pack->definitions[i];
            REQUIRE(!strcmp(binding->canonical_name, definition->canonical_name));
            if (!strcmp(binding->canonical_name, "Set Model Preview Reference")) {
                reference = true;
                REQUIRE(!strcmp(definition->source_name, "set_model_preview_reference"));
                REQUIRE(
                    definition->input_count == 6 &&
                    definition->inputs[3].storage == PIN_STRUCT &&
                    !strcmp(definition->inputs[3].type_name, "ModelReference")
                );
            }
            if (!strcmp(binding->canonical_name, "Set Model Preview Pan")) {
                pan = true;
                REQUIRE(!strcmp(definition->source_name, "set_model_preview_pan"));
                REQUIRE(definition->input_count == 5);
            }
            const node_host_call *call = binding->implementation.call;
            REQUIRE(call && call->optional_service && call->context);
            const char *error = call->context(NULL, NULL, NULL, output);
            REQUIRE(error && strstr(error, "authoring host"));
            REQUIRE(!memcmp(output, unchanged, sizeof output));
        }
        REQUIRE(reference && pan);
    }
    return true;
}
void test_tool_visual_previews()
{
    pu_setup(NULL, NULL);
    RUN_TEST_CASE(model_reference_corners_preserve_aspect_source_rotation_and_camera_pan);
    RUN_TEST_CASE(model_reference_corner_refusals_preserve_all_output_bytes);
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(
        model_preview_inspection_validates_parts_axes_and_canonical_generation_without_drawing
    );
    RUN_TEST_CASE(image_loaders_retain_isolated_roots_and_refuse_links_special_files);
    RUN_TEST_CASE(visual_catalog_seeds_private_dependencies_and_preserves_last_good_on_invalid_overlay);
    RUN_TEST_CASE(visual_publication_owns_applied_generations_and_refuses_stale_edits);
    RUN_TEST_CASE(visual_handles_are_bounded_and_never_reused_without_drawing);
    RUN_TEST_CASE(authoring_source_cache_follows_its_owned_visual_generation);
    RUN_TEST_CASE(test_tool_visual_previews_missing_host_is_optional_and_refuses_without_effects);
    pu_clear_setup();
}
