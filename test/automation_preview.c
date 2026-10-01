#include "assets/project.h"
#include "automation/controller.h"
#include "graph/compiler/graph_jit_sljit.h"
#include "graph/document/serialize.h"
#include "graph/runtime/instance.h"
#include "suite_internal.h"
#include "support/text_file.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
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
    snprintf(path, sizeof path, "%s/project.yaml", root);
    char manifest[128];
    snprintf(manifest, sizeof manifest, "version: %d\nname: Preview Test\ncontent: content\n", CHIBI_PROJECT_VERSION);
    preview_test_write(path, manifest);
    snprintf(path, sizeof path, "%s/PreviewScene.yaml", scenes);
    preview_test_write(
        path,
        "{\"version\":1,\"name\":\"PreviewScene\",\"presentation\":\"2d\","
        "\"entities\":[{\"name\":\"Entity\",\"prototype\":\"PreviewPrototype\"}"
        "]}"
    );
    snprintf(path, sizeof path, "%s/PreviewPrototype.yaml", prototypes);
    preview_test_write(path, "{\"name\":\"PreviewPrototype\",\"components\":{}}");
    char *library = text_file_read("test/fixtures/content/shader-libraries/ShaderMath.yaml");
    assert(library);
    snprintf(path, sizeof path, "%s/ShaderMath.yaml", shader_libraries);
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
    test_check(
        status == CHIBI_AUTOMATION_OK && run && !perspective,
        "headless scene preview registers a shared shader library once"
    );
    chibi_preview_free(run);

    char behaviors[256];
    snprintf(behaviors, sizeof behaviors, "%s/behaviors", content);
    assert(!mkdir(behaviors, 0700));
    behavior_asset *behavior = behavior_asset_new("ExecutionBehavior");
    char *json = behavior_asset_save_json(behavior);
    assert(json);
    snprintf(path, sizeof path, "%s/ExecutionBehavior.yaml", behaviors);
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
        "project.yaml",
        "content/scenes/PreviewScene.yaml",
        "content/prototypes/PreviewPrototype.yaml",
        "content/shader-libraries/ShaderMath.yaml",
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
