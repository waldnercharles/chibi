#include "assets/project.h"
#include "automation/controller.h"
#include "graph/compiler/graph_jit_sljit.h"
#include "graph/document/serialize.h"
#include "graph/runtime/instance.h"
#include "suite_internal.h"
#include "support/text_file.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void preview_test_write(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
}

static void execution_status_backend(const char *root, behavior_backend backend, const char *name)
{
    behavior_backend previous = behavior_debug_backend(backend);
    int code = -1;
    char *reply = chibi_automation_handle(
        "{\"contract\":1,\"operation\":\"execution_status\","
        "\"kind\":\"behavior\",\"name\":\"ExecutionBehavior\"}",
        root,
        &code
    );
    behavior_debug_backend(previous);
    assert(reply && code == CHIBI_AUTOMATION_OK);
    yyjson_doc *doc = yyjson_read(reply, strlen(reply), 0);
    assert(doc);
    yyjson_val *execution = yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(doc), "result"), "execution");
    assert(!strcmp(yyjson_get_str(yyjson_obj_get(execution, "backend")), name));
    assert(!yyjson_get_bool(yyjson_obj_get(execution, "fully_jit")));
    assert(yyjson_get_int(yyjson_obj_get(execution, "event_entries")) == 0);
    yyjson_doc_free(doc);
    free(reply);
}

void test_automation_preview_registration()
{
    char root[] = "/tmp/chibi-preview-registration-XXXXXX";
    assert(mkdtemp(root));
    char content[256], scenes[256], prototypes[256], shader_libraries[256];
    snprintf(content, sizeof content, "%s/content", root);
    snprintf(scenes, sizeof scenes, "%s/scenes", content);
    snprintf(prototypes, sizeof prototypes, "%s/prototypes", content);
    snprintf(shader_libraries, sizeof shader_libraries, "%s/shader-libraries", content);
    assert(mkdir(content, 0700) == 0);
    assert(mkdir(scenes, 0700) == 0);
    assert(mkdir(prototypes, 0700) == 0);
    assert(mkdir(shader_libraries, 0700) == 0);

    char path[512];
    snprintf(path, sizeof path, "%s/project.kdl", root);
    char manifest[128];
    snprintf(manifest, sizeof manifest, "version %d\nname \"Preview Test\"\ncontent \"content\"\n", CHIBI_PROJECT_VERSION);
    preview_test_write(path, manifest);
    snprintf(path, sizeof path, "%s/PreviewScene.kdl", scenes);
    preview_test_write(
        path,
        "version 1\n"
        "name \"PreviewScene\"\n"
        "presentation \"2d\"\n"
        "(array)entities {\n"
        "    - {\n"
        "        name \"Entity\"\n"
        "        prototype \"PreviewPrototype\"\n"
        "    }\n"
        "}\n"
    );
    snprintf(path, sizeof path, "%s/PreviewPrototype.kdl", prototypes);
    preview_test_write(
        path,
        "name \"PreviewPrototype\"\n"
        "(object)components\n"
    );
    char *library = text_file_read("test/fixtures/content/shader-libraries/ShaderMath.kdl");
    assert(library);
    snprintf(path, sizeof path, "%s/ShaderMath.kdl", shader_libraries);
    preview_test_write(path, library);
    free(library);

    chibi_preview_run *run = NULL;
    bool perspective = false;
    char error[256] = { 0 };
    int status = chibi_automation_preview(
        root,
        "{\"contract\":1,\"kind\":\"scene\",\"name\":\"PreviewScene\","
        "\"frames\":0}",
        &run,
        &perspective,
        error,
        sizeof error
    );
    if (status != CHIBI_AUTOMATION_OK) {
        printf("  scene preview: %s\n", error);
    }
    test_check(
        status == CHIBI_AUTOMATION_OK && run && !perspective,
        "headless scene preview registers a shared shader library once"
    );
    chibi_preview_free(run);

    char behaviors[256];
    snprintf(behaviors, sizeof behaviors, "%s/behaviors", content);
    assert(!mkdir(behaviors, 0700));
    behavior_asset *behavior = behavior_asset_new("ExecutionBehavior");
    char *json = behavior_asset_save(behavior);
    assert(json);
    snprintf(path, sizeof path, "%s/ExecutionBehavior.kdl", behaviors);
    preview_test_write(path, json);
    free(json);
    behavior_asset_free(behavior);
    execution_status_backend(root, BEHAVIOR_ORACLE, "interpreter");
    if (graph_sljit_available()) {
        execution_status_backend(root, BEHAVIOR_SLJIT, "sljit");
        execution_status_backend(root, BEHAVIOR_BYTECODE, "bytecode");
    }
    assert(!unlink(path) && !rmdir(behaviors));

    const char *files[] = {
        "project.kdl",
        "content/scenes/PreviewScene.kdl",
        "content/prototypes/PreviewPrototype.kdl",
        "content/shader-libraries/ShaderMath.kdl",
    };
    for (int i = 0; i < (int)(sizeof files / sizeof *files); i++) {
        snprintf(path, sizeof path, "%s/%s", root, files[i]);
        assert(unlink(path) == 0);
    }
    assert(rmdir(scenes) == 0);
    assert(rmdir(prototypes) == 0);
    assert(rmdir(shader_libraries) == 0);
    assert(rmdir(content) == 0);
    assert(rmdir(root) == 0);
}

static yyjson_doc *profile_scenario_request(const char *request)
{
    int exit_code = 0;
    char *response = chibi_automation_handle(request, "projects/arena2d", &exit_code);
    assert(response);
    yyjson_doc *doc = yyjson_read(response, strlen(response), 0);
    assert(doc);
    free(response);
    return doc;
}

static bool response_ok(const yyjson_doc *doc)
{
    return yyjson_get_bool(yyjson_obj_get(yyjson_doc_get_root(doc), "ok"));
}

static const char *response_world_hash(const yyjson_doc *doc)
{
    yyjson_val *result = yyjson_obj_get(yyjson_doc_get_root(doc), "result");
    return yyjson_get_str(yyjson_obj_get(result, "world_hash"));
}

static void profile_fluid_work()
{
    yyjson_doc *doc = profile_scenario_request(
        "{\"contract\":1,\"operation\":\"profile_scenario\","
        "\"kind\":\"scene\",\"name\":\"HRCVolume\",\"warmup_frames\":2,"
        "\"frames\":3}"
    );
    yyjson_val *result = yyjson_obj_get(yyjson_doc_get_root(doc), "result");
    yyjson_val *timing = yyjson_obj_get(result, "timing");
    yyjson_val *frames = yyjson_obj_get(timing, "worst_frames");
    uint64_t builds = 0;
    double collision_index_ms = 0;
    bool valid = response_ok(doc) && yyjson_arr_size(frames) == 3;
    for (size_t i = 0; i <= yyjson_arr_size(frames); i++) {
        yyjson_val *sample = i < yyjson_arr_size(frames) ? yyjson_arr_get(frames, i)
                                                         : timing;
        yyjson_val *detail = yyjson_obj_get(sample, "physics_detail_ms");
        yyjson_val *fluid = yyjson_obj_get(detail, "material_fluid_ms");
        yyjson_val *work = yyjson_obj_get(sample, "material_fluid_work");
        yyjson_val *index = yyjson_obj_get(fluid, "collision_index");
        yyjson_val *count = yyjson_obj_get(work, "index_builds");
        valid &= yyjson_is_num(index) && yyjson_get_num(index) >= 0 &&
                 yyjson_get_num(index) <=
                     yyjson_get_num(yyjson_obj_get(fluid, "collisions")) + 1e-9 &&
                 yyjson_is_uint(count) &&
                 !yyjson_obj_get(detail, "material_fluid_work") &&
                 !yyjson_obj_get(fluid, "index_builds");
        if (sample == timing) {
            valid &= builds > 0 && yyjson_get_uint(count) == builds &&
                     fabs(yyjson_get_num(index) - collision_index_ms) < 1e-9;
            continue;
        }
        builds += yyjson_get_uint(count);
        collision_index_ms += yyjson_get_num(index);
    }
    test_check(valid, "fluid profile keeps work counters outside timing objects and sums index builds");
    yyjson_doc_free(doc);
}

void test_automation_profile_scenario_inputs()
{
    profile_fluid_work();
    static const char *const
        timeline = "\"input_timeline\":["
                   "{\"frame\":0,\"input\":{\"movement\":{\"x\":1,\"y\":0},"
                   "\"aim\":{\"x\":1,\"y\":0,\"firing\":true}}},"
                   "{\"frame\":6,\"input\":{\"movement\":{\"x\":0,\"y\":1},"
                   "\"aim\":{\"x\":0,\"y\":1,\"firing\":false}}},"
                   "{\"frame\":12,\"input\":{\"movement\":{\"x\":-1,\"y\":0},"
                   "\"aim\":{\"x\":-1,\"y\":0,\"firing\":true}}}]";
    char run_request[2048], profile_request[2048], plain_request[512];
    snprintf(
        run_request,
        sizeof run_request,
        "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"prototype\","
        "\"name\":\"Player\",\"frames\":20,%s}",
        timeline
    );
    snprintf(
        profile_request,
        sizeof profile_request,
        "{\"contract\":1,\"operation\":\"profile_scenario\","
        "\"kind\":\"prototype\",\"name\":\"Player\",\"warmup_frames\":7,"
        "\"frames\":13,%s}",
        timeline
    );
    snprintf(
        plain_request,
        sizeof plain_request,
        "{\"contract\":1,\"operation\":\"run_scenario\","
        "\"kind\":\"prototype\",\"name\":\"Player\",\"frames\":20}"
    );
    yyjson_doc *run = profile_scenario_request(run_request);
    yyjson_doc *profile = profile_scenario_request(profile_request);
    yyjson_doc *plain = profile_scenario_request(plain_request);
    const char *run_hash = response_world_hash(run);
    const char *profile_hash = response_world_hash(profile);
    const char *plain_hash = response_world_hash(plain);
    test_check(
        response_ok(run) && response_ok(profile) && run_hash && profile_hash &&
            !strcmp(run_hash, profile_hash),
        "profile_scenario applies sparse inputs across warmup and measured "
        "frames"
    );
    test_check(run_hash && plain_hash && strcmp(run_hash, plain_hash), "profile input replay changes simulated prototype state");
    yyjson_doc_free(plain);
    yyjson_doc_free(profile);
    yyjson_doc_free(run);

    static const char *const
        named_run = "{\"contract\":1,\"operation\":\"run_scenario\","
                    "\"scenario\":\"Firefight\",\"frames\":4}";
    static const char *const
        named_profile = "{\"contract\":1,\"operation\":\"profile_scenario\","
                        "\"scenario\":\"Firefight\",\"warmup_frames\":2,"
                        "\"frames\":2}";
    run = profile_scenario_request(named_run);
    profile = profile_scenario_request(named_profile);
    run_hash = response_world_hash(run);
    profile_hash = response_world_hash(profile);
    test_check(
        response_ok(run) && response_ok(profile) && run_hash && profile_hash &&
            !strcmp(run_hash, profile_hash),
        "profile_scenario preserves named scenario input replay without "
        "request inputs"
    );
    yyjson_doc_free(profile);
    yyjson_doc_free(run);

    static const char *const invalid_timeline =
        "{\"contract\":1,\"operation\":\"profile_scenario\","
        "\"kind\":\"prototype\",\"name\":\"Player\",\"warmup_frames\":3,"
        "\"frames\":5,\"input_timeline\":["
        "{\"frame\":4,\"input\":{}},{\"frame\":2,\"input\":{}}]}";
    yyjson_doc *invalid = profile_scenario_request(invalid_timeline);
    yyjson_val *root = yyjson_doc_get_root(invalid);
    yyjson_val *error = yyjson_obj_get(root, "error");
    const char *code = yyjson_get_str(yyjson_obj_get(error, "code"));
    test_check(
        !response_ok(invalid) && code && !strcmp(code, "invalid_request"),
        "profile_scenario rejects out-of-order input timelines"
    );
    yyjson_doc_free(invalid);
}
