#include "assets/model_export_artifacts.h"
#include "authoring/image_prepare.h"
#include "authoring/model_evidence.h"
#include "authoring/visual_model_definition.h"
#include "authoring/workspace_file.h"
#include "execution/function.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "runtime/geometry/mesh_prepared.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/file_io.h"
#include "support/rooted_files.h"
#include "support/sha256.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const registry_pack **previous;
static workspace_documents *documents;
static visual_model_service *models;
static workspace_files *files;
static char root[1024];
static unsigned char *sampled_pixels;
static model_export_artifacts expected_bundle;
static visual_model_report expected_report;
static model_bake_result expected_bake;
static int sampled_resolves;
static render_image_loader reference_loader;
static void *reference_png;
static bool reference_resolve(void *user, const shader_image_source *source, shader_image_view *out)
{
    if (strcmp(source->key, "content/reference.png")) {
        return false;
    }
    *out = *(shader_image_view *)user;
    return true;
}
static void setup()
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &MODEL_PACK, &SHADER_PACK };
    registry_set_packs(packs, 2);
    type_catalog_pack types = { .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
    type_catalog_set_project(&types);
    documents = workspace_documents_new(NULL, NULL);
    snprintf(root, sizeof root, "%s/visual-model-XXXXXX", tool_root());
    if (!mkdtemp(root)) {
        abort();
    }
    char error[256];
    files = workspace_files_new(root, error, sizeof error);
    models = visual_models_new(documents, root);
}
static void cleanup()
{
    render_image_loader_release(&reference_loader);
    cf_free(reference_png);
    reference_png = NULL;
    model_export_artifacts_free(&expected_bundle);
    visual_model_report_free(&expected_report);
    model_bake_result_free(&expected_bake);
    free(sampled_pixels);
    sampled_pixels = NULL;
    sampled_resolves = 0;
    visual_models_free(models);
    workspace_files_free(files);
    workspace_documents_free(documents);
    type_catalog_set_project(NULL);
    registry_set_packs(previous, acount(previous));
    afree(previous);
    tool_remove_tree(root);
}
static int model(uint64_t *id)
{
    behavior_asset *asset = behavior_asset_new("ParameterizedSphere");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = asset->graph;
    node *parameter = graph_add_node(g, &NODE_ModelParameter),
         *sphere = graph_add_node(g, &NODE_ModelSphere),
         *output = model_graph_add_output(g);
    *id = parameter->id;
    graph_set_node_reference(g, parameter, 0, "Radius");
    const float values[] = { 1, .25f, 4 };
    for (int i = 0; i < 3; i++) {
        pin_value v = { .type = PIN_F32, .f = values[i] };
        graph_set_pin_default(g, parameter, 0, i, &v);
    }
    graph_connect_data(g, parameter, 0, sphere, 0);
    graph_connect_data(g, sphere, 0, output, 0);
    char *source = behavior_asset_save(asset);
    int document = 0;
    workspace_document_memory(documents, source, &document);
    free(source);
    behavior_asset_free(asset);
    return document;
}
TEST_CASE(model_declaration_edits_are_guarded_ranged_and_share_history)
{
    uint64_t id;
    int document = model(&id);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK);
    int initial = view.revision;
    visual_model_report report;
    visual_model_report_init(&report);
    REQUIRE(visual_model_inspect(documents, document, initial, NULL, 0, &report));
    REQUIRE(fabsf(report.bounds.max[0] - 1) < .00001f);
    shader_error error = { 0 };
    float value[4] = { 2 };
    char *before = strdup(view.text);
    char *candidate = visual_model_prepare_default(documents, document, initial, id, 1, value, &error);
    REQUIRE(candidate && strcmp(candidate, before));
    REQUIRE(workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == initial && !view.can_undo && !strcmp(view.text, before));
    free(candidate);
    free(before);
    int revised = 0;
    REQUIRE(
        visual_model_set_default(documents, document, initial, id, 1, value, &revised, &error)
    );
    REQUIRE(revised > initial);
    REQUIRE(
        !visual_model_set_default(documents, document, initial, id, 1, value, &revised, &error)
    );
    REQUIRE(visual_model_inspect(documents, document, revised, NULL, 0, &report));
    REQUIRE(fabsf(report.bounds.max[0] - 2) < .00001f);
    value[0] = 5;
    REQUIRE(
        !visual_model_set_default(documents, document, revised, id, 1, value, &initial, &error)
    );
    REQUIRE(
        workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK &&
        view.revision == revised
    );
    REQUIRE(workspace_document_undo(documents, document, revised) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(visual_model_inspect(documents, document, view.revision, NULL, 0, &report));
    REQUIRE(fabsf(report.bounds.max[0] - 1) < .00001f);
    REQUIRE(workspace_document_close(documents, document) == WORKSPACE_DOCUMENT_OK);
    float parameters[32][4];
    model_bounds bounds;
    REQUIRE(model_program_prepare(&report.program, NULL, parameters, &bounds, &error));
    REQUIRE(fabsf(bounds.max[0] - 1) < .00001f);
    visual_model_report_free(&report);
    return true;
}
static bool bundle_matches(const char *directory, const model_export_artifacts *expected)
{
    rooted_files *workspace = rooted_files_open(root);
    if (!workspace) {
        return false;
    }
    bool ok = true;
    for (int i = 0; ok && i < 4; i++) {
        char path[1200];
        snprintf(path, sizeof path, "%s/%s", directory, expected->files[i].name);
        unsigned char *bytes = NULL;
        size_t size = 0;
        ok = rooted_files_read(workspace, path, WORKSPACE_FILE_BYTES_MAX, &bytes, &size) &&
             size == expected->files[i].size &&
             !memcmp(bytes, expected->files[i].bytes, size);
        free(bytes);
    }
    rooted_file_entry *entries = NULL;
    int count = 0;
    if (ok) {
        ok = rooted_files_list(workspace, directory, 8, &entries, &count) &&
             count == 4;
    }
    rooted_file_entries_free(entries, count);
    rooted_files_close(workspace);
    return ok;
}
static bool no_private_bundle_stages()
{
    rooted_files *workspace = rooted_files_open(root);
    if (!workspace) {
        return false;
    }
    rooted_file_entry *entries = NULL;
    int count = 0;
    bool ok = rooted_files_list(workspace, "", 128, &entries, &count);
    for (int i = 0; ok && i < count; i++) {
        ok = !strstr(entries[i].name, ".stage-");
    }
    rooted_file_entries_free(entries, count);
    rooted_files_close(workspace);
    return ok;
}
TEST_CASE(model_bake_artifacts_are_owned_repeatable_and_exclusive)
{
    uint64_t id;
    int document = model(&id);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK);
    model_bake_options options = model_bake_defaults();
    options.cell_size = .4f;
    options.max_samples = 50000;
    options.max_work = 10000000;
    options.max_triangles = 10000;
    visual_model_bake_view first, second;
    char error[256];
    REQUIRE(
        visual_models_bake(models, document, view.revision, &options, &first, error, sizeof error)
    );
    REQUIRE(
        visual_models_bake(models, document, view.revision, &options, &second, error, sizeof error)
    );
    REQUIRE(
        first.bake != second.bake && first.triangles > 0 &&
        first.bytes == second.bytes && first.samples == second.samples &&
        first.work == second.work
    );
    /* Compare the retained service bundle with the shared CPU serializer on
     * the exact original document, parameter snapshot and bake options. */
    visual_model_report_init(&expected_report);
    REQUIRE(visual_model_inspect(documents, document, view.revision, NULL, 0, &expected_report));
    shader_error diagnostic = { 0 };
    REQUIRE(visual_model_bake(&expected_report, &options, &expected_bake, &diagnostic));
    model_export_options export_options = { .bake = options };
    model_export_source snapshot = { expected_report.source->name,
                                     view.path ? view.path : "",
                                     view.text };
    REQUIRE(model_export_artifacts_build(
        expected_report.source->name,
        &expected_report.program,
        &expected_bake,
        &export_options,
        &snapshot,
        1,
        WORKSPACE_BUNDLE_BYTES_MAX,
        &expected_bundle,
        error,
        sizeof error
    ));
    REQUIRE(
        visual_model_bake_export_bundle(models, first.bake, "bundle-first", error, sizeof error)
    );
    REQUIRE(bundle_matches("bundle-first", &expected_bundle));
    /* Mutating and closing the original authoring document cannot change the
     * already retained source/provenance or baked geometry. */
    const float changed[4] = { 2, 0, 0, 0 };
    int changed_revision;
    REQUIRE(
        visual_model_set_default(documents, document, view.revision, id, 1, changed, &changed_revision, &diagnostic)
    );
    REQUIRE(changed_revision != view.revision);
    REQUIRE(workspace_document_close(documents, document) == WORKSPACE_DOCUMENT_OK);
    model_bake_result_free(&expected_bake);
    visual_model_report_free(&expected_report);
    REQUIRE(
        visual_model_bake_export_bundle(models, first.bake, "bundle-after-edit", error, sizeof error)
    );
    REQUIRE(
        visual_model_bake_export_bundle(models, second.bake, "bundle-second", error, sizeof error)
    );
    REQUIRE(
        bundle_matches("bundle-after-edit", &expected_bundle) &&
        bundle_matches("bundle-second", &expected_bundle)
    );
    REQUIRE(
        !visual_model_bake_export_bundle(models, first.bake, "bundle-first", error, sizeof error)
    );
    REQUIRE(bundle_matches("bundle-first", &expected_bundle));
    REQUIRE(
        !visual_model_bake_export_bundle(models, first.bake, "bundle-first/../invalid", error, sizeof error)
    );
    REQUIRE(
        !visual_model_bake_export_bundle(models, first.bake, "missing-parent/bundle", error, sizeof error)
    );
    char invalid_path[1200];
    snprintf(invalid_path, sizeof invalid_path, "%s/invalid", root);
    REQUIRE(access(invalid_path, F_OK) != 0);
    REQUIRE(no_private_bundle_stages());
    REQUIRE(visual_model_bake_export(models, first.bake, "first.mesh", error, sizeof error));
    REQUIRE(visual_model_bake_export(models, second.bake, "second.mesh", error, sizeof error));
    char path[1200];
    snprintf(path, sizeof path, "%s/first.mesh", root);
    char *a = file_read_text(path);
    snprintf(path, sizeof path, "%s/second.mesh", root);
    char *b = file_read_text(path);
    REQUIRE(a && b && !memcmp(a, b, first.bytes));
    render_mesh_asset mesh = { 0 };
    size_t remaining = 16 * 1024 * 1024;
    REQUIRE(
        render_mesh_asset_prepared_read_budget(a, first.bytes, &remaining, &mesh, error, sizeof error)
    );
    REQUIRE(acount(mesh.indices) / 3 == first.triangles && acount(mesh.vertices) > 0);
    render_mesh_asset_free(&mesh);
    REQUIRE(!visual_model_bake_export(models, first.bake, "first.mesh", error, sizeof error));
    REQUIRE(!visual_model_bake_export(models, first.bake, "../outside.mesh", error, sizeof error));
    REQUIRE(visual_model_bake_close(models, first.bake));
    REQUIRE(!visual_model_bake_close(models, first.bake));
    REQUIRE(!visual_model_bake_read(models, first.bake, &first));
    free(a);
    free(b);
    return true;
}
TEST_CASE(workspace_binary_files_refuse_overwrite_links_and_unbounded_payloads)
{
    unsigned char payload[] = { 0, 1, 2, 255, 0, 17 };
    char error[256];
    REQUIRE(files);
    REQUIRE(
        workspace_file_write_new(files, "binary.data", payload, sizeof payload, error, sizeof error)
    );
    REQUIRE(
        !workspace_file_write_new(files, "binary.data", payload, sizeof payload, error, sizeof error)
    );
    REQUIRE(
        !workspace_file_write_new(files, "../outside.data", payload, sizeof payload, error, sizeof error)
    );
    REQUIRE(
        !workspace_file_write_new(files, "dot/../outside.data", payload, sizeof payload, error, sizeof error)
    );
    REQUIRE(
        !workspace_file_write_new(files, "//absolute.data", payload, sizeof payload, error, sizeof error)
    );
    REQUIRE(
        !workspace_file_write_new(files, "oversize.data", payload, WORKSPACE_FILE_BYTES_MAX + 1, error, sizeof error)
    );
    char directory[1200], link[1200];
    snprintf(directory, sizeof directory, "%s/nested", root);
    snprintf(link, sizeof link, "%s/linked", root);
    REQUIRE(!mkdir(directory, 0700));
    REQUIRE(!symlink("nested", link));
    REQUIRE(
        !workspace_file_write_new(files, "linked/file.data", payload, sizeof payload, error, sizeof error)
    );
    REQUIRE(
        workspace_file_write_new(files, "nested/file.data", payload, sizeof payload, error, sizeof error)
    );
    snprintf(link, sizeof link, "%s/fifo", root);
    REQUIRE(!mkfifo(link, 0600));
    REQUIRE(!workspace_file_write_new(files, "fifo", payload, sizeof payload, error, sizeof error));
    return true;
}
TEST_CASE(workspace_flat_bundle_preflight_refuses_invalid_entries_and_preserves_destinations)
{
    unsigned char payload[] = { 0, 7, 255, 0 };
    char error[256], path[1200];
    const workspace_file_entry invalid[][2] = {
        { { "same", payload, sizeof payload }, { "same", payload, sizeof payload } },
        { { "nested/file", payload, sizeof payload },
          { "other", payload, sizeof payload } },
        { { "file", payload, sizeof payload }, { "file/child", payload, sizeof payload } },
        { { "../escape", payload, sizeof payload }, { "other", payload, sizeof payload } },
        { { "bad", NULL, sizeof payload }, { "other", payload, sizeof payload } },
        { { "large", payload, WORKSPACE_FILE_BYTES_MAX + 1 },
          { "other", payload, sizeof payload } }
    };
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        REQUIRE(
            !workspace_file_bundle_write_atomic(files, "rejected", invalid[i], 2, error, sizeof error)
        );
        REQUIRE(error[0]);
        snprintf(path, sizeof path, "%s/rejected", root);
        REQUIRE(access(path, F_OK) != 0 && no_private_bundle_stages());
    }
    workspace_file_entry entries[] = { { "a.bin", payload, sizeof payload },
                                       { "manifest", "complete", 8 } };
    REQUIRE(workspace_file_bundle_write_atomic(files, "complete", entries, 2, error, sizeof error));
    REQUIRE(no_private_bundle_stages());
    entries[0].data = "changed";
    entries[0].bytes = 7;
    REQUIRE(
        !workspace_file_bundle_write_atomic(files, "complete", entries, 2, error, sizeof error)
    );
    rooted_files *workspace = rooted_files_open(root);
    REQUIRE(workspace);
    unsigned char *bytes = NULL;
    size_t size = 0;
    bool ok = rooted_files_read(workspace, "complete/a.bin", 16, &bytes, &size) &&
              size == sizeof payload && !memcmp(bytes, payload, size);
    free(bytes);
    rooted_files_close(workspace);
    REQUIRE(ok && no_private_bundle_stages());
    /* A case-insensitive volume discovers this collision during staged
     * writes. Its rollback must remove every file and the private directory. */
    workspace = rooted_files_open(root);
    REQUIRE(workspace);
    bool aliases = rooted_files_path_equal(workspace, "Alias.bin", "alias.bin");
    rooted_files_close(workspace);
    workspace_file_entry case_entries[] = { { "Alias.bin", payload, sizeof payload },
                                            { "alias.bin", payload, sizeof payload } };
    bool published = workspace_file_bundle_write_atomic(files, "case-alias", case_entries, 2, error, sizeof error);
    REQUIRE(published == !aliases);
    snprintf(path, sizeof path, "%s/case-alias", root);
    REQUIRE((access(path, F_OK) == 0) == !aliases);
    REQUIRE(no_private_bundle_stages());
    REQUIRE(
        workspace_file_write_new(files, "file-collision", payload, sizeof payload, error, sizeof error)
    );
    REQUIRE(
        !workspace_file_bundle_write_atomic(files, "file-collision", entries, 2, error, sizeof error)
    );
    REQUIRE(no_private_bundle_stages());
    return true;
}
TEST_CASE(model_evidence_freezes_source_parameters_and_publishes_truthful_cpu_fixture)
{
    uint64_t node;
    int document = model(&node);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK);
    behavior_asset *source = behavior_asset_load(view.text);
    REQUIRE(source);
    model_reference reference = { .image = "reference.png",
                                  .center = { .25f, -.5f },
                                  .span = 5,
                                  .angle = .5f,
                                  .locked = true };
    REQUIRE(behavior_asset_set_model_reference(source, &reference));
    CF_Pixel reference_pixels[2] = { { .r = 255, .g = 17, .b = 31, .a = 255 },
                                     { .r = 7, .g = 191, .b = 223, .a = 255 } };
    CF_Image reference_image = { 2, 1, reference_pixels };
    int reference_bytes = 0;
    REQUIRE(!cf_is_error(
        cf_image_save_png_to_memory(&reference_image, &reference_png, &reference_bytes)
    ));
    char error[512];
    REQUIRE(
        workspace_file_write_new(files, "reference.png", reference_png, (size_t)reference_bytes, error, sizeof error)
    );
    REQUIRE(image_prepare_loader_new(root, &reference_loader, error, sizeof error));
    shader_image_view resident = { (const unsigned char *)reference_pixels, 2, 1 };
    model_evidence_input in = {
        .source = source,
        .width = 16,
        .height = 16,
        .revision = view.revision,
        .part = -1,
        .zoom = 1,
        .pan_x = .75f,
        .pan_y = -1.25f,
        .reference_visible = true,
        .reference_opacity = .375f,
        .images = reference_loader,
        .resolve_image = reference_resolve,
        .image_user = &resident,
        .engine_version = "fixture",
        .build_id = "fixture",
        .manifest_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef012"
                           "3456789abcdef",
        .backend = "synthetic CPU fixture; no GPU draw",
        .gpu_rendered = false
    };
    in.build_id = NULL;
    REQUIRE(!model_evidence_prepare(&in, error, sizeof error));
    in.build_id = "fixture";
    reference_pixels[0].r = 0;
    REQUIRE(!model_evidence_prepare(&in, error, sizeof error));
    REQUIRE(strstr(error, "differ"));
    reference_pixels[0].r = 255;
    model_evidence_plan *plan = model_evidence_prepare(&in, error, sizeof error);
    REQUIRE(plan);
    /* Destroy or alter every mutable input before final publication. */
    reference.image = "changed.png";
    reference.span = 99;
    reference.center[0] = 100;
    reference.locked = false;
    REQUIRE(behavior_asset_set_model_reference(source, &reference));
    memset(reference_pixels, 0, sizeof reference_pixels);
    in.pan_x = 99;
    in.pan_y = 99;
    in.reference_visible = false;
    in.reference_opacity = 0;
    render_image_loader_release(&reference_loader);
    char original_image_path[1200];
    snprintf(original_image_path, sizeof original_image_path, "%s/reference.png", root);
    REQUIRE(!unlink(original_image_path));
    behavior_asset_free(source);
    REQUIRE(workspace_document_close(documents, document) == WORKSPACE_DOCUMENT_OK);
    CF_Pixel pixels[16 * 16] = { 0 };
    CF_Image image = { 16, 16, pixels };
    void *png = NULL;
    int bytes = 0;
    REQUIRE(!cf_is_error(cf_image_save_png_to_memory(&image, &png, &bytes)));
    REQUIRE(!model_evidence_finalize(plan, files, "malformed", "png", 3, error, sizeof error));
    CF_Image smaller = { 8, 8, pixels };
    void *wrong_png = NULL;
    int wrong_bytes = 0;
    REQUIRE(!cf_is_error(cf_image_save_png_to_memory(&smaller, &wrong_png, &wrong_bytes)));
    REQUIRE(
        !model_evidence_finalize(plan, files, "wrong-size", wrong_png, (size_t)wrong_bytes, error, sizeof error)
    );
    cf_free(wrong_png);
    REQUIRE(
        model_evidence_finalize(plan, files, "model-evidence", png, (size_t)bytes, error, sizeof error)
    );
    REQUIRE(
        !model_evidence_finalize(plan, files, "model-evidence", png, (size_t)bytes, error, sizeof error)
    );
    REQUIRE(
        !model_evidence_finalize(plan, files, "../outside", png, (size_t)bytes, error, sizeof error)
    );
    char path[1200];
    snprintf(path, sizeof path, "%s/model-evidence/manifest.json", root);
    char *manifest = file_read_text(path);
    REQUIRE(manifest);
    av_doc *metadata = av_json_read(manifest);
    REQUIRE(metadata);
    const av_value *coverage = av_obj_get(av_doc_root(metadata), "coverage");
    REQUIRE(
        av_get_bool(av_obj_get(coverage, "model_compiled")) &&
        !av_get_bool(av_obj_get(coverage, "gpu_preview_rendered")) &&
        !av_get_bool(av_obj_get(coverage, "cpu_mesh_baked")) &&
        av_get_bool(av_obj_get(coverage, "reference_overlay")) &&
        av_get_bool(av_obj_get(coverage, "resident_images_matched"))
    );
    REQUIRE(
        !strcmp(av_get_str(av_obj_get(av_obj_get(av_doc_root(metadata), "request"), "operation")), "preview_model")
    );
    const av_value *request = av_obj_get(av_doc_root(metadata), "request");
    const av_value *frozen_view = av_obj_get(request, "view");
    const av_value *frozen_inspection = av_obj_get(request, "inspection");
    REQUIRE(
        av_get_num(av_obj_get(frozen_view, "pan_x")) == .75 &&
        av_get_num(av_obj_get(frozen_view, "pan_y")) == -1.25
    );
    REQUIRE(
        av_get_bool(av_obj_get(frozen_inspection, "reference_visible")) &&
        av_get_num(av_obj_get(frozen_inspection, "reference_opacity")) == .375
    );
    snprintf(path, sizeof path, "%s/model-evidence/requests.ndjson", root);
    char *request_text = file_read_text(path);
    REQUIRE(request_text);
    av_doc *request_doc = av_json_read(request_text);
    free(request_text);
    REQUIRE(request_doc);
    frozen_view = av_obj_get(av_doc_root(request_doc), "view");
    frozen_inspection = av_obj_get(av_doc_root(request_doc), "inspection");
    REQUIRE(
        av_get_num(av_obj_get(frozen_view, "pan_x")) == .75 &&
        av_get_num(av_obj_get(frozen_view, "pan_y")) == -1.25 &&
        av_get_bool(av_obj_get(frozen_inspection, "reference_visible")) &&
        av_get_num(av_obj_get(frozen_inspection, "reference_opacity")) == .375
    );
    av_doc_free(request_doc);
    rooted_files *workspace = rooted_files_open(root);
    REQUIRE(workspace);
    unsigned char *frozen_png = NULL;
    size_t frozen_png_bytes = 0;
    bool image_matches = rooted_files_read(
                             workspace,
                             "model-evidence/content/reference.png",
                             WORKSPACE_FILE_BYTES_MAX,
                             &frozen_png,
                             &frozen_png_bytes
                         ) &&
                         frozen_png_bytes == (size_t)reference_bytes &&
                         !memcmp(frozen_png, reference_png, frozen_png_bytes);
    free(frozen_png);
    rooted_files_close(workspace);
    REQUIRE(image_matches);
    char digest[65];
    sha256_hex(reference_png, (size_t)reference_bytes, digest);
    const av_value *inventory = av_obj_get(av_doc_root(metadata), "files");
    bool inventoried = false;
    for (size_t i = 0; i < av_arr_size(inventory); i++) {
        const av_value *entry = av_arr_get(inventory, i);
        const char *name = av_get_str(av_obj_get(entry, "path"));
        if (!name || strcmp(name, "content/reference.png")) {
            continue;
        }
        inventoried = av_get_int(av_obj_get(entry, "bytes")) == reference_bytes &&
                      !strcmp(av_get_str(av_obj_get(entry, "sha256")), digest);
    }
    REQUIRE(inventoried);
    snprintf(path, sizeof path, "%s/model-evidence/content/models/000.kdl", root);
    char *text = file_read_text(path);
    REQUIRE(text);
    source = behavior_asset_load(text);
    REQUIRE(source);
    REQUIRE(
        source->reference.image && !strcmp(source->reference.image, "reference.png") &&
        source->reference.center[0] == .25f &&
        source->reference.center[1] == -.5f && source->reference.span == 5 &&
        source->reference.angle == .5f && source->reference.locked
    );
    model_program program;
    model_program_init(&program);
    shader_error problem = { 0 };
    REQUIRE(model_compile_with_catalog(source, NULL, 0, &program, &problem));
    float parameters[32][4];
    model_bounds bounds;
    REQUIRE(model_program_prepare(&program, NULL, parameters, &bounds, &problem));
    REQUIRE(fabsf(bounds.max[0] - 1) < .00001f);
    model_program_free(&program);
    behavior_asset_free(source);
    free(text);
    free(manifest);
    av_doc_free(metadata);
    cf_free(png);
    model_evidence_free(plan);
    return true;
}
static bool sampled_resolve(void *user, const shader_image_source *source, shader_image_view *out)
{
    if (strcmp(source->key, "content/sdf/0123456789abcdef.sdf.png")) {
        return false;
    }
    sampled_resolves++;
    *out = *(shader_image_view *)user;
    return true;
}
TEST_CASE(model_cpu_bake_uses_owned_service_image_resolver)
{
    const int n = 9, side = 27;
    sampled_pixels = calloc((size_t)side * side, 4);
    REQUIRE(sampled_pixels);
    for (int z = 0; z < n; z++) {
        for (int y = 0; y < n; y++) {
            for (int x = 0; x < n; x++) {
                float dx = x / 4.f - 1, dy = y / 4.f - 1, dz = z / 4.f - 1;
                float distance = sqrtf(dx * dx + dy * dy + dz * dz) - .55f;
                unsigned encoded = (unsigned)lroundf((distance / 2 + 1) * .5f * 65535);
                unsigned char *pixel = sampled_pixels + 4 * (((z / 3) * n + y) * side +
                                                             (z % 3) * n + x);
                pixel[0] = encoded >> 8;
                pixel[1] = encoded & 255;
                pixel[3] = 255;
            }
        }
    }
    shader_image_view image = { sampled_pixels, side, side };
    behavior_asset *asset = behavior_asset_new("SampledCPU");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = asset->graph;
    node *volume = graph_add_node(g, &NODE_ModelSampledSdf),
         *part = graph_add_node(g, &NODE_ModelPart),
         *output = model_graph_add_output(g);
    float extent[3] = { 1, 1, 1 };
    pin_value values[] = {
        { .type = PIN_STR, .str = "content/sdf/0123456789abcdef.sdf.png" },
        { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = extent },
        { .type = PIN_I32, .i = n },
        { .type = PIN_F32, .f = 2 },
        { .type = PIN_F32, .f = 2 }
    };
    for (int i = 0; i < 5; i++) {
        REQUIRE(!graph_set_pin_default(g, volume, 0, i, &values[i]));
    }
    REQUIRE(!graph_set_node_reference(g, part, 0, "Sampled part"));
    REQUIRE(
        !graph_connect_data(g, volume, 0, part, 0) &&
        !graph_connect_data(g, part, 0, output, 0)
    );
    char *source = behavior_asset_save(asset);
    REQUIRE(source);
    int document = 0;
    REQUIRE(workspace_document_memory(documents, source, &document) == WORKSPACE_DOCUMENT_OK);
    free(source);
    behavior_asset_free(asset);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, document, &view) == WORKSPACE_DOCUMENT_OK);
    visual_models_set_images(models, sampled_resolve, &image);
    model_bake_options options = model_bake_defaults();
    options.cell_size = .25f;
    options.max_samples = 50000;
    options.max_work = 10000000;
    visual_model_bake_view artifact;
    char error[512];
    bool baked = visual_models_bake(models, document, view.revision, &options, &artifact, error, sizeof error);
    if (!baked) {
        fprintf(stderr, "Sampled model bake: %s\n", error);
    }
    REQUIRE(baked);
    REQUIRE(artifact.triangles > 0 && sampled_resolves > 0);
    return true;
}
TEST_CASE(test_tool_visual_model_missing_host_is_optional_and_refuses_without_effects)
{
    pin_value output[32], unchanged[32];
    memset(output, 0xa5, sizeof output);
    memcpy(unchanged, output, sizeof output);
    for (int i = 0; i < VISUAL_MODEL_DEFINITION_PACK.binding_count; i++) {
        const node_host_call *call = VISUAL_MODEL_DEFINITION_PACK.bindings[i]
                                         .implementation.call;
        REQUIRE(call && call->optional_service && call->context);
        const char *error = call->context(NULL, NULL, NULL, output);
        REQUIRE(error && strstr(error, "authoring host"));
        REQUIRE(!memcmp(output, unchanged, sizeof output));
    }
    return true;
}
void test_tool_visual_model()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(model_evidence_freezes_source_parameters_and_publishes_truthful_cpu_fixture);
    RUN_TEST_CASE(model_declaration_edits_are_guarded_ranged_and_share_history);
    RUN_TEST_CASE(model_bake_artifacts_are_owned_repeatable_and_exclusive);
    RUN_TEST_CASE(workspace_binary_files_refuse_overwrite_links_and_unbounded_payloads);
    RUN_TEST_CASE(workspace_flat_bundle_preflight_refuses_invalid_entries_and_preserves_destinations);
    RUN_TEST_CASE(model_cpu_bake_uses_owned_service_image_resolver);
    RUN_TEST_CASE(test_tool_visual_model_missing_host_is_optional_and_refuses_without_effects);
    pu_clear_setup();
}
