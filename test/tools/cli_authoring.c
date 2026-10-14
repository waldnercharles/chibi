#include "../../tools/chibi_ctl/commands.h"
#include "../../tools/chibi_ctl/evidence.h"
#include "authoring/image_prepare.h"
#include "authoring/material_create.h"
#include "authoring/shader_evidence.h"
#include "automation/controller.h"
#include "automation/render_preview.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/shader/shader_graph.h"
#include "platform/cute.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

TEST_CASE(cli_verbs_preserve_paths_and_parameter_types)
{
    char error[256];
    char *arguments[] = { "material", "create",      "Brass \"A\"", "--preset",
                          "metal",    "--roughness", "0.25" };
    char *json = chibi_ctl_command(7, arguments, error, sizeof error);
    REQUIRE(json);
    av_doc *doc = av_json_read(json);
    av_value *root = av_doc_root(doc);
    REQUIRE(av_get_int(av_obj_get(root, "contract")) == CHIBI_AUTOMATION_CONTRACT);
    REQUIRE(!strcmp(av_get_str(av_obj_get(root, "operation")), "create_material"));
    REQUIRE(!strcmp(av_get_str(av_obj_get(root, "name")), arguments[2]));
    REQUIRE(av_get_num(av_obj_get(root, "roughness")) == 0.25);
    av_doc_free(doc);
    free(json);
    char *model[] = { "model", "reimport", "Robot", "--check" };
    json = chibi_ctl_command(4, model, error, sizeof error);
    REQUIRE(json);
    doc = av_json_read(json);
    REQUIRE(av_get_bool(av_obj_get(av_doc_root(doc), "check")));
    av_doc_free(doc);
    free(json);
    char *inspect[] = { "inspect", "materials/Steel.kdl", "--sample" };
    json = chibi_ctl_command(3, inspect, error, sizeof error);
    REQUIRE(json);
    doc = av_json_read(json);
    REQUIRE(av_get_bool(av_obj_get(av_doc_root(doc), "cpu_sample")));
    av_doc_free(doc);
    free(json);
    return true;
}

TEST_CASE(cli_rejects_ambiguous_or_nonfinite_arguments)
{
    char error[256];
    char *arguments[] = { "material", "create",      "A",  "--preset",
                          "metal",    "--roughness", "nan" };
    REQUIRE(!chibi_ctl_command(7, arguments, error, sizeof error));
    arguments[6] = "1.1";
    REQUIRE(!chibi_ctl_command(7, arguments, error, sizeof error));
    char *extra[] = { "assets", "ignored" };
    REQUIRE(!chibi_ctl_command(2, extra, error, sizeof error));
    char *model[] = { "model", "plan", "model.glb", "--name" };
    REQUIRE(!chibi_ctl_command(4, model, error, sizeof error));
    return true;
}

TEST_CASE(cli_evidence_preserves_failed_response_without_claiming_validation)
{
    char directory[] = "/tmp/chibi-cli-evidence-XXXXXX";
    REQUIRE(mkdtemp(directory));
    char path[512];
    snprintf(path, sizeof path, "%s/evidence", directory);
    char project[512];
    snprintf(project, sizeof project, "%s/project.kdl", directory);
    FILE *project_file = fopen(project, "wb");
    REQUIRE(project_file);
    REQUIRE(fputs("content \"content\"\r\nname \"Before\"\r\n", project_file) >= 0);
    REQUIRE(!fclose(project_file));
    char *arguments[] = { "/unavailable/chibi_ctl", "--evidence", "--render", "validate" };
    chibi_ctl_evidence *e = chibi_ctl_evidence_begin(path, directory, false, 4, arguments);
    REQUIRE(e);
    REQUIRE(!chibi_ctl_evidence_begin(path, directory, false, 4, arguments));
    const char *request = "{\"operation\":\"validate_project\"}";
    REQUIRE(chibi_ctl_evidence_request(e, request, strlen(request)));
    REQUIRE(chibi_ctl_evidence_request(e, "\n", 1));
    REQUIRE(chibi_ctl_evidence_response(e, request, "{\"ok\":false}", 4));
    const char *sample_request = "{\"operation\":\"inspect_graph\",\"cpu_"
                                 "sample\":true}";
    REQUIRE(
        chibi_ctl_evidence_response(e, sample_request, "{\"ok\":true,\"result\":{\"shader_report\":{\"cpu_sample\":{\"valid\":false}}}}", 0)
    );
    REQUIRE(
        chibi_ctl_evidence_response(e, sample_request, "{\"ok\":true,\"result\":{\"shader_report\":{\"cpu_sample\":{\"valid\":true}}}}", 0)
    );
    project_file = fopen(project, "wb");
    REQUIRE(project_file);
    REQUIRE(fputs("content \"content\"\nname \"After\"\n", project_file) >= 0);
    REQUIRE(!fclose(project_file));
    REQUIRE(chibi_ctl_evidence_finish(e, 4));
    char manifest[1024];
    snprintf(manifest, sizeof manifest, "%s/manifest.json", path);
    FILE *file = fopen(manifest, "rb");
    REQUIRE(file);
    char *text = NULL;
    size_t capacity = 0;
    REQUIRE(getdelim(&text, &capacity, '\0', file) > 0);
    fclose(file);
    av_doc *doc = av_json_read(text);
    av_value *root = av_doc_root(doc);
    REQUIRE(av_get_int(av_obj_get(root, "exit_status")) == 4);
    REQUIRE(av_get_bool(av_obj_get(root, "evidence_complete")));
    REQUIRE(!strcmp(av_get_str(av_obj_get(root, "request_transport")), "ndjson_automation"));
    av_value *replay = av_obj_get(root, "replay");
    REQUIRE(av_arr_size(av_obj_get(replay, "argv")) == 3);
    for (size_t i = 0; i < av_arr_size(av_obj_get(replay, "argv")); i++) {
        REQUIRE(strcmp(av_get_str(av_arr_get(av_obj_get(replay, "argv"), i)), "--render"));
    }
    av_value *environment = av_obj_get(replay, "environment");
    REQUIRE(av_is_obj(environment) && av_obj_size(environment) == 10);
    REQUIRE(!av_obj_get(environment, "PATH"));
    REQUIRE(!av_obj_get(environment, "HOME"));
    REQUIRE(!av_obj_get(environment, "CHIBI_TEST_DEADLINE"));
    const char *driver = getenv("SDL_VIDEO_DRIVER");
    av_value *recorded_driver = av_obj_get(environment, "SDL_VIDEO_DRIVER");
    REQUIRE(
        driver ? av_is_str(recorded_driver) && !strcmp(driver, av_get_str(recorded_driver))
               : av_is_null(recorded_driver)
    );
    const char *shader_dir = getenv("CHIBI_RENDER_SHADER_DIR");
    av_value *recorded_shader_dir = av_obj_get(environment, "CHIBI_RENDER_SHADER_DIR");
    REQUIRE(recorded_shader_dir);
    REQUIRE(
        shader_dir ? av_is_str(recorded_shader_dir) &&
                         !strcmp(shader_dir, av_get_str(recorded_shader_dir))
                   : av_is_null(recorded_shader_dir)
    );
    av_value *before = av_obj_get(av_obj_get(root, "project_before"), "manifest");
    av_value *after = av_obj_get(av_obj_get(root, "project_after"), "manifest");
    REQUIRE(av_get_bool(av_obj_get(before, "available")));
    REQUIRE(av_get_bool(av_obj_get(after, "available")));
    REQUIRE(
        strcmp(av_get_str(av_obj_get(before, "fnv1a64")), av_get_str(av_obj_get(after, "fnv1a64")))
    );
    REQUIRE(!av_get_bool(av_obj_get(av_obj_get(root, "coverage"), "static_validation")));
    REQUIRE(!av_get_bool(av_obj_get(av_obj_get(root, "coverage"), "gpu_rendered")));
    REQUIRE(!av_get_bool(av_obj_get(av_obj_get(root, "coverage"), "cpu_runtime")));
    REQUIRE(av_get_bool(av_obj_get(av_obj_get(root, "coverage"), "cpu_shader_sampling")));
    av_value *operations = av_obj_get(root, "operations");
    REQUIRE(!strcmp(av_get_str(av_obj_get(av_arr_get(operations, 1), "coverage")), "operation_only"));
    REQUIRE(
        !strcmp(av_get_str(av_obj_get(av_arr_get(operations, 2), "coverage")), "cpu_shader_sampling")
    );
    av_doc_free(doc);
    free(text);
    unlink(manifest);
    snprintf(manifest, sizeof manifest, "%s/requests.ndjson", path);
    unlink(manifest);
    snprintf(manifest, sizeof manifest, "%s/responses.ndjson", path);
    unlink(manifest);
    snprintf(manifest, sizeof manifest, "%s/project_before.kdl", path);
    file = fopen(manifest, "rb");
    REQUIRE(file);
    text = NULL;
    capacity = 0;
    REQUIRE(getdelim(&text, &capacity, '\0', file) > 0);
    REQUIRE(!strcmp(text, "content \"content\"\r\nname \"Before\"\r\n"));
    free(text);
    fclose(file);
    unlink(manifest);
    snprintf(manifest, sizeof manifest, "%s/project_after.kdl", path);
    unlink(manifest);
    unlink(project);
    rmdir(path);
    rmdir(directory);
    return true;
}

TEST_CASE(cli_preview_evidence_requires_success_image_and_backend_metadata)
{
    char directory[] = "/tmp/chibi-cli-preview-evidence-XXXXXX";
    REQUIRE(mkdtemp(directory));
    char path[512];
    snprintf(path, sizeof path, "%s/evidence", directory);
    char *arguments[] = { "/unavailable/chibi_ctl", "--render-request", "original.json" };
    chibi_ctl_evidence *e = chibi_ctl_evidence_begin(path, directory, true, 3, arguments);
    REQUIRE(e);
    const char *request = "{\"operation\":\"preview_shader\"}";
    REQUIRE(chibi_ctl_evidence_request(e, request, strlen(request)));
    /* Synthetic native-response fixtures test bookkeeping, never render. */
    REQUIRE(
        chibi_ctl_evidence_response(e, request, "{\"ok\":false,\"result\":{\"backend\":\"Metal\",\"preview\":{\"mime_type\":\"image/png\",\"width\":32,\"height\":32,\"data\":\"fixture\"}}}", 5)
    );
    REQUIRE(
        chibi_ctl_evidence_response(e, request, "{\"ok\":true,\"result\":{\"preview\":{\"mime_type\":\"image/png\",\"width\":32,\"height\":32,\"data\":\"fixture\"}}}", 0)
    );
    REQUIRE(
        chibi_ctl_evidence_response(e, request, "{\"ok\":true,\"result\":{\"backend\":\"Metal\",\"preview\":{\"mime_type\":\"image/png\",\"width\":1,\"height\":1,\"data\":\"iVBORw0KGgo=\"}}}", 0)
    );
    const char *png = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4"
                      "2mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==";
    char response[512];
    snprintf(
        response,
        sizeof response,
        "{\"ok\":true,\"result\":{\"backend\":\"Metal\",\"preview\":{\"mime_"
        "type\":\"image/png\",\"width\":32,\"height\":32,\"data\":\"%s\"}}}",
        png
    );
    REQUIRE(chibi_ctl_evidence_response(e, request, response, 0));
    snprintf(response, sizeof response, "{\"ok\":true,\"result\":{\"backend\":\"Metal\",\"video_driver\":\"cocoa\",\"preview\":{\"mime_type\":\"image/png\",\"width\":1,\"height\":1,\"data\":\"%s\"}}}", png);
    REQUIRE(chibi_ctl_evidence_response(e, "{\"operation\":\"preview_model\"}", response, 0));
    REQUIRE(chibi_ctl_evidence_finish(e, 5));
    char manifest[1024];
    snprintf(manifest, sizeof manifest, "%s/manifest.json", path);
    FILE *file = fopen(manifest, "rb");
    REQUIRE(file);
    char *text = NULL;
    size_t capacity = 0;
    REQUIRE(getdelim(&text, &capacity, '\0', file) > 0);
    fclose(file);
    av_doc *doc = av_json_read(text);
    av_value *root = av_doc_root(doc), *operations = av_obj_get(root, "operations");
    REQUIRE(!strcmp(av_get_str(av_obj_get(av_arr_get(operations, 0), "coverage")), "operation_only"));
    REQUIRE(!strcmp(av_get_str(av_obj_get(av_arr_get(operations, 1), "coverage")), "operation_only"));
    REQUIRE(!strcmp(av_get_str(av_obj_get(av_arr_get(operations, 2), "coverage")), "operation_only"));
    REQUIRE(!strcmp(av_get_str(av_obj_get(av_arr_get(operations, 3), "coverage")), "operation_only"));
    REQUIRE(!strcmp(av_get_str(av_obj_get(av_arr_get(operations, 4), "coverage")), "gpu_rendered"));
    REQUIRE(!strcmp(av_get_str(av_obj_get(av_arr_get(operations, 4), "operation")), "preview_model"));
    av_value *artifact = av_obj_get(av_arr_get(operations, 4), "preview_artifact");
    REQUIRE(av_get_bool(av_obj_get(artifact, "available")));
    REQUIRE(!strcmp(av_get_str(av_obj_get(artifact, "file")), "preview-000005.png"));
    REQUIRE(av_get_int(av_obj_get(artifact, "bytes")) == 70);
    REQUIRE(!strcmp(av_get_str(av_obj_get(artifact, "fnv1a64")), "517bc1418be279d9"));
    REQUIRE(!av_get_bool(
        av_obj_get(av_obj_get(av_arr_get(operations, 2), "preview_artifact"), "available")
    ));
    REQUIRE(av_get_bool(av_obj_get(av_obj_get(root, "coverage"), "gpu_rendered")));
    REQUIRE(!av_get_bool(av_obj_get(av_obj_get(root, "coverage"), "full_verification")));
    av_value *replay = av_obj_get(root, "replay");
    REQUIRE(!strcmp(av_get_str(av_arr_get(av_obj_get(replay, "argv"), 3)), "--render"));
    av_value *environment = av_obj_get(replay, "environment");
    REQUIRE(av_obj_size(environment) == 10);
    REQUIRE(!av_obj_get(environment, "HOME"));
    REQUIRE(!av_obj_get(environment, "PATH"));
    const char *resource_root = getenv("CHIBI_RESOURCE_DIR");
    av_value *recorded_root = av_obj_get(environment, "CHIBI_RESOURCE_DIR");
    REQUIRE(
        resource_root ? !strcmp(resource_root, av_get_str(recorded_root))
                      : av_is_null(recorded_root)
    );
    av_doc_free(doc);
    free(text);
    unlink(manifest);
    snprintf(manifest, sizeof manifest, "%s/requests.ndjson", path);
    unlink(manifest);
    snprintf(manifest, sizeof manifest, "%s/responses.ndjson", path);
    file = fopen(manifest, "rb");
    REQUIRE(file);
    text = NULL;
    capacity = 0;
    REQUIRE(getdelim(&text, &capacity, '\0', file) > 0);
    REQUIRE(strstr(text, response));
    free(text);
    fclose(file);
    unlink(manifest);
    snprintf(manifest, sizeof manifest, "%s/preview-000005.png", path);
    file = fopen(manifest, "rb");
    REQUIRE(file);
    unsigned char image[70];
    REQUIRE(fread(image, 1, sizeof image, file) == sizeof image && fgetc(file) == EOF);
    REQUIRE(!memcmp(image, "\x89PNG\r\n\x1a\n", 8));
    REQUIRE(image[19] == 1 && image[23] == 1);
    fclose(file);
    unlink(manifest);
    rmdir(path);
    rmdir(directory);
    return true;
}

TEST_CASE(cli_model_preview_rejects_malformed_controls_before_file_or_gpu_work)
{
    const char *requests[] = {
        "{\"operation\":\"preview_model\",\"inspection\":false}",
        "{\"operation\":\"preview_model\",\"inspection\":{\"part\":1.5}}",
        "{\"operation\":\"preview_model\",\"inspection\":{\"part\":-2}}",
        "{\"operation\":\"preview_model\",\"inspection\":{\"section_axis\":4}}",
        "{\"operation\":\"preview_model\",\"inspection\":{\"section_offset\":"
        "\"1\"}}",
        "{\"operation\":\"preview_model\",\"inspection\":{\"unknown\":0}}",
        "{\"operation\":\"preview_model\",\"time\":-1}",
        "{\"operation\":\"preview_model\",\"sample_grid\":{}}"
    };
    for (size_t i = 0; i < sizeof requests / sizeof *requests; i++) {
        int status = 0;
        char *text = chibi_render_preview_handle(requests[i], "/unavailable/workspace", &status);
        REQUIRE(text && status == CHIBI_AUTOMATION_INVALID_REQUEST);
        av_doc *doc = av_json_read(text);
        av_value *value = av_doc_root(doc);
        REQUIRE(av_get_int(av_obj_get(value, "contract")) == CHIBI_AUTOMATION_CONTRACT);
        REQUIRE(!av_get_bool(av_obj_get(value, "ok")));
        REQUIRE(!strcmp(av_get_str(av_obj_get(value, "operation")), "preview_model"));
        REQUIRE(!av_obj_get(value, "status"));
        const char *message = av_get_str(av_obj_get(av_obj_get(value, "error"), "message"));
        REQUIRE(
            message && strstr(
                           message,
                           i < 6    ? "inspection"
                           : i == 6 ? "nonnegative"
                                    : "shader-only"
                       )
        );
        REQUIRE(!av_obj_get(av_obj_get(value, "result"), "backend"));
        av_doc_free(doc);
        free(text);
    }
    return true;
}

static const registry_pack **evidence_packs;
static char shader_evidence_root[128];
static void evidence_setup()
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(evidence_packs, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &SHADER_PACK };
    registry_set_packs(packs, 1);
    type_catalog_pack types = { .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
    type_catalog_set_project(&types);
    strcpy(shader_evidence_root, "/tmp/chibi-shader-evidence-XXXXXX");
    assert(mkdtemp(shader_evidence_root));
}
static void evidence_cleanup()
{
    type_catalog_set_project(NULL);
    registry_set_packs(evidence_packs, acount(evidence_packs));
    afree(evidence_packs);
    tool_remove_tree(shader_evidence_root);
}
static bool evidence_png(void **png, int *size)
{
    CF_Pixel pixels[16 * 16] = { 0 };
    CF_Image image = { 16, 16, pixels };
    return !cf_is_error(cf_image_save_png_to_memory(&image, png, size));
}
static shader_authoring_evidence evidence_input(behavior_asset *asset, void *png, int size, const char *destination)
{
    return (shader_authoring_evidence){
        .snapshot = asset,
        .content_root = shader_evidence_root,
        .destination = destination,
        .png = png,
        .png_size = (size_t)size,
        .width = 16,
        .height = 16,
        .zoom = 1,
        .backend = "synthetic CPU fixture; no GPU rendered",
        .engine_version = "fixture",
        .engine_build_id = "fixture",
        .engine_manifest_hash = "0123456789abcdef0123456789abcdef0123456789abcd"
                                "ef0123456789abcdef"
    };
}

TEST_CASE(shader_evidence_snapshot_reloads_and_existing_destination_survives)
{
    standard_material material = material_authoring_defaults(MATERIAL_AUTHORING_METAL);
    behavior_asset *asset = standard_material_build("Metal", &material);
    REQUIRE(asset);
    void *png = NULL;
    int png_size = 0;
    REQUIRE(evidence_png(&png, &png_size));
    char destination[256];
    snprintf(destination, sizeof destination, "%s/bundle", shader_evidence_root);
    shader_authoring_evidence input = evidence_input(asset, png, png_size, destination);
    input.cpu_attempted = true;
    input.cpu_context = shader_authoring_center_context(asset);
    shader_authoring_report report;
    shader_authoring_report_init(&report);
    shader_error problem;
    REQUIRE(shader_authoring_inspect(asset, &report, &problem));
    input.cpu_valid = shader_authoring_sample(
        &report,
        &input.cpu_context,
        NULL,
        input.cpu_outputs,
        &input.cpu_error
    );
    REQUIRE(input.cpu_valid);
    float parameters[SHADER_PARAMETER_MAX][4];
    memcpy(parameters, report.defaults, sizeof parameters);
    input.parameters = parameters;
    char error[512];
    const char *manifest_identity = input.engine_manifest_hash;
    input.engine_manifest_hash = NULL;
    REQUIRE(!shader_authoring_evidence_prepare(&input, error, sizeof error));
    input.engine_manifest_hash = "invalid";
    REQUIRE(!shader_authoring_evidence_prepare(&input, error, sizeof error));
    input.engine_manifest_hash = manifest_identity;
    shader_authoring_evidence_plan
        *prepared = shader_authoring_evidence_prepare(&input, error, sizeof error);
    REQUIRE(prepared);
    parameters[STANDARD_ROUGHNESS][0] = .9f;
    float changed = .9f;
    REQUIRE(shader_authoring_set_default(asset, STANDARD_ROUGHNESS, &changed, 1, &problem));
    REQUIRE(
        shader_authoring_evidence_finalize(prepared, png, (size_t)png_size, error, sizeof error)
    );
    shader_authoring_evidence_free(prepared);
    char path[512];
    snprintf(path, sizeof path, "%s/manifest.json", destination);
    char *before = tool_read(path);
    REQUIRE(before);
    REQUIRE(!shader_authoring_export_evidence(&input, error, sizeof error));
    char *after = tool_read(path);
    REQUIRE(after && !strcmp(before, after));
    av_doc *doc = av_json_read(after);
    av_value *root = av_doc_root(doc), *coverage = av_obj_get(root, "coverage");
    REQUIRE(av_get_bool(av_obj_get(coverage, "graph_validated")));
    REQUIRE(av_get_bool(av_obj_get(coverage, "cpu_shader_sampling")));
    REQUIRE(!av_get_bool(av_obj_get(coverage, "cpu_runtime")));
    REQUIRE(!av_get_bool(av_obj_get(coverage, "full_verification")));
    av_value *outputs = av_obj_get(av_obj_get(root, "cpu_sample"), "outputs");
    REQUIRE(!strcmp(av_get_str(av_obj_get(av_arr_get(outputs, 0), "type")), "vec3"));
    av_doc_free(doc);
    free(before);
    free(after);
    snprintf(path, sizeof path, "%s/requests.ndjson", destination);
    char *request = tool_read(path);
    doc = av_json_read(request);
    REQUIRE(av_get_int(av_obj_get(av_doc_root(doc), "contract")) == CHIBI_AUTOMATION_CONTRACT);
    REQUIRE(av_get_int(av_obj_get(av_doc_root(doc), "width")) == 16);
    REQUIRE(
        av_get_num(av_obj_get(av_obj_get(av_doc_root(doc), "parameters"), "Roughness")) == .25
    );
    av_doc_free(doc);
    free(request);
    snprintf(path, sizeof path, "%s/content/materials/Snapshot.kdl", destination);
    char *source = tool_read(path);
    behavior_asset *loaded = behavior_asset_load(source);
    REQUIRE(loaded);
    behavior_asset *linked = behavior_asset_link_libraries(loaded, NULL, 0, error, sizeof error);
    REQUIRE(linked && shader_authoring_inspect(linked, &report, &problem));
    REQUIRE(report.defaults[STANDARD_METALLIC][0] == 1);
    REQUIRE(report.defaults[STANDARD_ROUGHNESS][0] == .25f);
    behavior_asset_free(linked);
    behavior_asset_free(loaded);
    free(source);
    snprintf(path, sizeof path, "%s/preview.png", destination);
    FILE *file = fopen(path, "rb");
    REQUIRE(file);
    unsigned char *copied = malloc((size_t)png_size);
    REQUIRE(copied && fread(copied, 1, (size_t)png_size, file) == (size_t)png_size);
    REQUIRE(!memcmp(copied, png, (size_t)png_size) && fgetc(file) == EOF);
    fclose(file);
    free(copied);
    node *roughness = NULL, *metallic = NULL;
    for (int i = 0; i < acount(asset->graph->nodes); i++) {
        node *parameter = asset->graph->nodes[i];
        if (strcmp(parameter->desc->name, "Shader Parameter")) {
            continue;
        }
        if (parameter->reference_id == STANDARD_ROUGHNESS ||
            parameter->reference_id == STANDARD_METALLIC) {
            parameter->reference_name = graph_own_str(asset->graph, "Factor", 6);
        }
        if (parameter->reference_id == STANDARD_ROUGHNESS) {
            roughness = parameter;
        }
        if (parameter->reference_id == STANDARD_METALLIC) {
            metallic = parameter;
        }
    }
    REQUIRE(roughness && metallic);
    snprintf(destination, sizeof destination, "%s/duplicates", shader_evidence_root);
    input.destination = destination;
    REQUIRE(!shader_authoring_evidence_prepare(&input, error, sizeof error));
    REQUIRE(strstr(error, "unique nonempty"));
    roughness->reference_name = graph_own_str(asset->graph, "", 0);
    REQUIRE(!shader_authoring_evidence_prepare(&input, error, sizeof error));
    roughness->reference_name = graph_own_str(asset->graph, "material.Factor", sizeof "material.Factor" - 1);
    REQUIRE(!shader_authoring_evidence_prepare(&input, error, sizeof error));
    REQUIRE(strstr(error, "scope aliases"));
    roughness->reference_name = graph_own_str(asset->graph, "Factor", 6);
    metallic->reference_name = graph_own_str(asset->graph, "material.Factor", sizeof "material.Factor" - 1);
    REQUIRE(!shader_authoring_evidence_prepare(&input, error, sizeof error));
    metallic->reference_name = graph_own_str(asset->graph, "Factor", 6);
    input.parameters = NULL;
    input.cpu_attempted = input.cpu_valid = false;
    REQUIRE(shader_authoring_export_evidence(&input, error, sizeof error));
    snprintf(path, sizeof path, "%s/requests.ndjson", destination);
    request = tool_read(path);
    doc = av_json_read(request);
    REQUIRE(doc && !av_obj_get(av_doc_root(doc), "parameters"));
    av_doc_free(doc);
    free(request);
    snprintf(path, sizeof path, "%s/content/materials/Snapshot.kdl", destination);
    source = tool_read(path);
    loaded = behavior_asset_load(source);
    REQUIRE(loaded && shader_authoring_inspect(loaded, &report, &problem));
    float outputs_again[SHADER_OUTPUT_MAX][4] = { 0 };
    REQUIRE(shader_authoring_sample(&report, &input.cpu_context, NULL, outputs_again, &problem));
    REQUIRE(outputs_again[SHADER_SURFACE_METALLIC][0] == 1);
    REQUIRE(outputs_again[SHADER_SURFACE_ROUGHNESS][0] == .9f);
    behavior_asset_free(loaded);
    free(source);
    shader_authoring_report_free(&report);
    behavior_asset_free(asset);
    cf_free(png);
    return true;
}

static bool evidence_resident_image(void *context, const shader_image_source *source, shader_image_view *out)
{
    (void)source;
    CF_Image *image = context;
    *out = (shader_image_view){ (const unsigned char *)image->pix,
                                image->w,
                                image->h };
    return true;
}

TEST_CASE(shader_evidence_copies_image_bytes_and_rejects_symlinks_before_writing)
{
    void *png = NULL;
    int png_size = 0;
    REQUIRE(evidence_png(&png, &png_size));
    char folder[256], image[256], destination[256];
    snprintf(folder, sizeof folder, "%s/images", shader_evidence_root);
    REQUIRE(!mkdir(folder, 0700));
    snprintf(image, sizeof image, "%s/images/color.png", shader_evidence_root);
    FILE *file = fopen(image, "wb");
    REQUIRE(file && fwrite(png, 1, (size_t)png_size, file) == (size_t)png_size && !fclose(file));
    standard_material material = material_authoring_defaults(MATERIAL_AUTHORING_MATTE);
    material.textures[STANDARD_TEXTURE_BASE_COLOR]
        .image = "content/images/color.png";
    behavior_asset *asset = standard_material_build("Textured", &material);
    REQUIRE(asset);
    snprintf(destination, sizeof destination, "%s/textured", shader_evidence_root);
    shader_authoring_evidence input = evidence_input(asset, png, png_size, destination);
    input.cpu_attempted = true;
    snprintf(input.cpu_error.message, sizeof input.cpu_error.message, "Frozen fixture has no image resolver");
    CF_Pixel pixels[16 * 16] = { 0 };
    CF_Image resident = { 16, 16, pixels };
    input.cpu_context.resolve_image = evidence_resident_image;
    input.cpu_context.image_userdata = &resident;
    char error[512];
    shader_image_view decoded;
    const char *format = image_prepare_decode(png, (size_t)png_size, &decoded, error, sizeof error);
    REQUIRE(format && !strcmp(format, "png"));
    REQUIRE(decoded.width == 16 && decoded.height == 16);
    REQUIRE(decoded.pixels != (const unsigned char *)pixels);
    REQUIRE(!memcmp(decoded.pixels, pixels, sizeof pixels));
    free((void *)decoded.pixels);
    decoded = (shader_image_view){ (const unsigned char *)pixels, 16, 16 };
    REQUIRE(!image_prepare_decode(png, 8, &decoded, error, sizeof error));
    REQUIRE(!decoded.pixels && decoded.width == 0 && decoded.height == 0);
    ((unsigned char *)pixels)[0] = 255;
    REQUIRE(!shader_authoring_evidence_prepare(&input, error, sizeof error));
    REQUIRE(strstr(error, "differ from the current resident preview"));
    REQUIRE(access(destination, F_OK) != 0);
    ((unsigned char *)pixels)[0] = 0;
    shader_authoring_evidence_plan
        *prepared = shader_authoring_evidence_prepare(&input, error, sizeof error);
    REQUIRE(prepared);
    /* Async readback may finish after a watcher or external writer changes the
     * file. Publication must use the bytes frozen before capture. */
    file = fopen(image, "wb");
    REQUIRE(file && fputs("changed after capture", file) >= 0 && !fclose(file));
    REQUIRE(
        shader_authoring_evidence_finalize(prepared, png, (size_t)png_size, error, sizeof error)
    );
    shader_authoring_evidence_free(prepared);
    char copied[512];
    snprintf(copied, sizeof copied, "%s/content/images/color.png", destination);
    file = fopen(copied, "rb");
    REQUIRE(file);
    unsigned char *bytes = malloc((size_t)png_size);
    REQUIRE(
        bytes && fread(bytes, 1, (size_t)png_size, file) == (size_t)png_size &&
        fgetc(file) == EOF
    );
    REQUIRE(!memcmp(bytes, png, (size_t)png_size));
    fclose(file);
    free(bytes);
    REQUIRE(!unlink(image) && !symlink(copied, image));
    snprintf(destination, sizeof destination, "%s/rejected", shader_evidence_root);
    input.destination = destination;
    REQUIRE(!shader_authoring_export_evidence(&input, error, sizeof error));
    REQUIRE(access(destination, F_OK) != 0);
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    /* No writer opens this FIFO: the regular-file prerequisite must reject it
     * without blocking the production-supervised authoring process. */
    REQUIRE(unlink(image) == 0);
    REQUIRE(mkfifo(image, 0600) == 0);
    REQUIRE(!shader_authoring_export_evidence(&input, error, sizeof error));
    REQUIRE(strstr(error, "regular image dependency"));
    REQUIRE(access(destination, F_OK) != 0);
    REQUIRE(unlink(image) == 0);
#endif
    behavior_asset_free(asset);
    cf_free(png);
    return true;
}

void test_cli_authoring()
{
    RUN_TEST_CASE(cli_verbs_preserve_paths_and_parameter_types);
    RUN_TEST_CASE(cli_rejects_ambiguous_or_nonfinite_arguments);
    RUN_TEST_CASE(cli_evidence_preserves_failed_response_without_claiming_validation);
    RUN_TEST_CASE(cli_preview_evidence_requires_success_image_and_backend_metadata);
    RUN_TEST_CASE(cli_model_preview_rejects_malformed_controls_before_file_or_gpu_work);
    pu_setup(evidence_setup, evidence_cleanup);
    RUN_TEST_CASE(shader_evidence_snapshot_reloads_and_existing_destination_survives);
    RUN_TEST_CASE(shader_evidence_copies_image_bytes_and_rejects_symlinks_before_writing);
    pu_clear_setup();
}
