#include "../../tools/chibi_ctl/commands.h"
#include "../../tools/chibi_ctl/evidence.h"
#include "automation/controller.h"
#include "support/asset_json.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    REQUIRE(chibi_ctl_evidence_response(e, request, response, 0));
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

void test_cli_authoring()
{
    RUN_TEST_CASE(cli_verbs_preserve_paths_and_parameter_types);
    RUN_TEST_CASE(cli_rejects_ambiguous_or_nonfinite_arguments);
    RUN_TEST_CASE(cli_evidence_preserves_failed_response_without_claiming_validation);
    RUN_TEST_CASE(cli_preview_evidence_requires_success_image_and_backend_metadata);
}
