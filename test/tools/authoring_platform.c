#include "assets/project.h"
#include "execution/function.h"
#include "host/authoring_platform_definition.h"
#include "support/arena.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char root[128];
static authoring_platform *platform;
static int cancelled, adapter_calls;
static bool adapter(void *, authoring_platform *, int, const char *operation, const char *, double, double, int)
{
    adapter_calls++;
    return !strcmp(operation, "choose-file") || !strcmp(operation, "choose-folder") ||
           !strcmp(operation, "clipboard-read");
}
static void cancel(void *, int)
{
    cancelled++;
}
static void setup()
{
    strcpy(root, "/tmp/chibi-authoring-platform-XXXXXX");
    assert(mkdtemp(root));
    authoring_platform_config config = { .root = root, .request = adapter, .cancel = cancel };
    char error[256];
    platform = authoring_platform_new(&config, error, sizeof error);
    assert(platform);
    cancelled = 0;
    adapter_calls = 0;
}
static void teardown()
{
    authoring_platform_free(platform);
    platform = NULL;
    tool_remove_tree(root);
}
TEST_CASE(platform_async_pending_denied_and_snapshot_result)
{
    authoring_platform_result
        r = authoring_platform_request(platform, "clipboard-read", "", 0, 0, 0);
    REQUIRE(r.ok && r.request > 0 && !strcmp(r.state, "running"));
    REQUIRE(!authoring_platform_release(platform, r.request));
    REQUIRE(
        authoring_platform_resolve(platform, r.request, AUTHORING_PLATFORM_FAILED, "denied", "Host denied clipboard permission", "")
    );
    r = authoring_platform_poll(platform, r.request);
    REQUIRE(!r.ok && !strcmp(r.state, "failed") && !strcmp(r.status, "denied"));
    REQUIRE(
        !authoring_platform_resolve(platform, r.request, AUTHORING_PLATFORM_SUCCEEDED, "ok", "", "late")
    );
    REQUIRE(authoring_platform_release(platform, r.request));
    return true;
}
TEST_CASE(platform_cancel_rejects_late_completion_and_reused_handles)
{
    authoring_platform_result
        r = authoring_platform_request(platform, "choose-file", "", 0, 0, 0);
    int old = r.request;
    r = authoring_platform_cancel(platform, old);
    REQUIRE(!r.ok && !strcmp(r.state, "cancelled") && cancelled == 1);
    REQUIRE(
        !authoring_platform_resolve(platform, old, AUTHORING_PLATFORM_SUCCEEDED, "ok", "", root)
    );
    REQUIRE(authoring_platform_release(platform, old));
    r = authoring_platform_request(platform, "choose-file", "", 0, 0, 0);
    REQUIRE(r.request != old);
    REQUIRE(
        !authoring_platform_resolve(platform, old, AUTHORING_PLATFORM_SUCCEEDED, "ok", "", root)
    );
    authoring_platform_free(platform);
    platform = NULL;
    REQUIRE(cancelled == 2);
    return true;
}
TEST_CASE(platform_fixed_operations_and_request_capacity)
{
    authoring_platform_result
        r = authoring_platform_request(platform, "shell", "rm -rf *", 0, 0, 0);
    REQUIRE(!r.ok && !strcmp(r.status, "invalid-operation"));
    REQUIRE(authoring_platform_release(platform, r.request));
    for (int i = 0; i < AUTHORING_PLATFORM_REQUESTS_MAX; i++) {
        r = authoring_platform_request(platform, "choose-file", "", 0, 0, 0);
        REQUIRE(r.ok);
    }
    r = authoring_platform_request(platform, "choose-file", "", 0, 0, 0);
    REQUIRE(!r.ok && !strcmp(r.status, "limit") && r.request == 0);
    return true;
}
TEST_CASE(platform_window_scale_and_missing_jobs_are_truthful)
{
    authoring_platform_result r = authoring_platform_request(platform, "ui-scale", "", 1.5, 0, 0);
    REQUIRE(
        r.ok && !strcmp(r.status, "requested") &&
        authoring_platform_ui_scale(platform) == 1.5
    );
    REQUIRE(authoring_platform_release(platform, r.request));
    r = authoring_platform_request(platform, "ui-scale", "", 0, 0, 0);
    REQUIRE(!r.ok && authoring_platform_ui_scale(platform) == 1.5);
    REQUIRE(authoring_platform_release(platform, r.request));
    r = authoring_platform_request(platform, "compile", "", 0, 0, 0);
    REQUIRE(!r.ok && !strcmp(r.state, "unavailable"));
    REQUIRE(authoring_platform_release(platform, r.request));
    r = authoring_platform_request(platform, "quit", "", 0, 0, 0);
    REQUIRE(r.ok && authoring_platform_quit_requested(platform));
    return true;
}
static const char *copy(void *storage, const char *text)
{
    size_t n = strlen(text) + 1;
    char *out = arena_alloc(storage, n);
    memcpy(out, text, n);
    return out;
}
TEST_CASE(platform_binding_copies_terminal_status_after_release)
{
    authoring_platform_result
        r = authoring_platform_request(platform, "choose-file", "", 0, 0, 0);
    REQUIRE(
        authoring_platform_resolve(platform, r.request, AUTHORING_PLATFORM_SUCCEEDED, "ok", "Selected file", "/tmp/example.png")
    );
    arena storage = { 0 };
    behavior_function_context context = { .value_storage = &storage,
                                          .copy_string = copy };
    pin_value in = { .type = PIN_I32, .i = r.request };
    authoring_platform_result snapshot = { 0 };
    pin_value out = { .type = PIN_STRUCT,
                      .type_name = "PlatformRequestResult",
                      .ptr = &snapshot };
    REQUIRE(!AUTHORING_PLATFORM_POLL_CALL.context(&context, platform, &in, &out));
    REQUIRE(authoring_platform_release(platform, r.request));
    REQUIRE(
        snapshot.ok && !strcmp(snapshot.value, "/tmp/example.png") &&
        !strcmp(snapshot.message, "Selected file")
    );
    arena_free(&storage);
    return true;
}
TEST_CASE(platform_import_is_exclusive_and_rejects_symlink_destination)
{
    char source[256], target[256], link[256];
    snprintf(source, sizeof source, "%s/source.bin", root);
    snprintf(target, sizeof target, "%s/copied.bin", root);
    snprintf(link, sizeof link, "%s/linked", root);
    FILE *file = fopen(source, "wb");
    REQUIRE(file);
    REQUIRE(fwrite("original", 1, 8, file) == 8);
    REQUIRE(!fclose(file));
    authoring_platform_result
        selected = authoring_platform_request(platform, "choose-file", "", 0, 0, 0);
    REQUIRE(
        authoring_platform_resolve(platform, selected.request, AUTHORING_PLATFORM_SUCCEEDED, "ok", "", source)
    );
    authoring_platform_result imported = authoring_platform_request(
        platform,
        "import-file",
        "copied.bin",
        0,
        0,
        selected.request
    );
    for (int i = 0; i < 4 && !strcmp(imported.state, "running"); i++) {
        imported = authoring_platform_poll(platform, imported.request);
    }
    REQUIRE(imported.ok && !strcmp(imported.state, "succeeded"));
    REQUIRE(authoring_platform_release(platform, imported.request));
    file = fopen(target, "rb");
    REQUIRE(file);
    char bytes[9] = { 0 };
    REQUIRE(fread(bytes, 1, 8, file) == 8 && !strcmp(bytes, "original"));
    REQUIRE(!fclose(file));
    imported = authoring_platform_request(
        platform,
        "import-file",
        "copied.bin",
        0,
        0,
        selected.request
    );
    for (int i = 0; i < 4 && !strcmp(imported.state, "running"); i++) {
        imported = authoring_platform_poll(platform, imported.request);
    }
    REQUIRE(!imported.ok && !strcmp(imported.status, "exists"));
    REQUIRE(authoring_platform_release(platform, imported.request));
    REQUIRE(!symlink(root, link));
    imported = authoring_platform_request(
        platform,
        "import-file",
        "linked/out.bin",
        0,
        0,
        selected.request
    );
    REQUIRE(!imported.ok && !strcmp(imported.status, "unsafe-file"));
    REQUIRE(authoring_platform_release(platform, imported.request));
    imported = authoring_platform_request(
        platform,
        "import-file",
        "../out.bin",
        0,
        0,
        selected.request
    );
    REQUIRE(!imported.ok && !strcmp(imported.status, "invalid"));
    return true;
}
TEST_CASE(platform_activation_is_queued_until_host_completion)
{
    authoring_platform_result
        r = authoring_platform_request(platform, "activate-project", root, 0, 0, 0);
    REQUIRE(!r.ok && !authoring_platform_pending_project(platform));
    REQUIRE(authoring_platform_release(platform, r.request));
    char path[256];
    snprintf(path, sizeof path, "%s/project.kdl", root);
    FILE *manifest = fopen(path, "wb");
    REQUIRE(manifest);
    REQUIRE(
        fputs("version 25\nname \"Platform fixture\"\ncontent \"content\"\nstart_scene \"scenes/Start.kdl\"\n", manifest) >=
        0
    );
    REQUIRE(!fclose(manifest));
    r = authoring_platform_request(platform, "activate-project", root, 0, 0, 0);
    REQUIRE(r.ok && !strcmp(r.state, "running") && !strcmp(r.status, "pending-host"));
    char *canonical_root = realpath(root, NULL);
    REQUIRE(canonical_root);
    bool same_project = !strcmp(authoring_platform_pending_project(platform), canonical_root);
    free(canonical_root);
    REQUIRE(same_project);
    authoring_platform_project_complete(platform, false, "Replacement preparation failed");
    r = authoring_platform_poll(platform, r.request);
    REQUIRE(
        !r.ok && !strcmp(r.status, "activation-error") &&
        !authoring_platform_pending_project(platform)
    );
    REQUIRE(authoring_platform_release(platform, r.request));
    r = authoring_platform_request(platform, "activate-project", root, 0, 0, 0);
    REQUIRE(r.ok && authoring_platform_pending_project(platform));
    r = authoring_platform_cancel(platform, r.request);
    REQUIRE(!r.ok && !authoring_platform_pending_project(platform));
    return true;
}
TEST_CASE(platform_workspace_creation_requires_completed_folder_source_and_never_activates)
{
    char scaffold[1024];
    snprintf(scaffold, sizeof scaffold, "{\"Name\":\"Created\",\"Files\":[{\"Path\":\"project.kdl\",\"Text\":\"version %d\\nname \\\"Created\\\"\\ncontent \\\"content\\\"\\nstart_scene \\\"scenes/Start.kdl\\\"\\n\"},{\"Path\":\"content/scenes/Start.kdl\",\"Text\":\"version 1\\nname \\\"Start\\\"\\n(array)entities\\n\"}]}", CHIBI_PROJECT_VERSION);
    authoring_platform_result
        result = authoring_platform_request(platform, "create-workspace", scaffold, 0, 0, 0);
    REQUIRE(!result.ok && !strcmp(result.state, "failed"));
    REQUIRE(authoring_platform_release(platform, result.request));
    authoring_platform_result
        chosen = authoring_platform_request(platform, "choose-folder", "", 0, 0, 0);
    result = authoring_platform_request(
        platform,
        "create-workspace",
        scaffold,
        0,
        0,
        chosen.request
    );
    REQUIRE(!result.ok);
    REQUIRE(authoring_platform_release(platform, result.request));
    REQUIRE(
        authoring_platform_resolve(platform, chosen.request, AUTHORING_PLATFORM_SUCCEEDED, "ok", "", root)
    );
    chosen = authoring_platform_poll(platform, chosen.request);
    REQUIRE(chosen.ok && !strcmp(chosen.state, "succeeded"));
    result = authoring_platform_request(
        platform,
        "create-workspace",
        scaffold,
        0,
        0,
        chosen.request
    );
    REQUIRE(result.ok && !strcmp(result.state, "succeeded"));
    REQUIRE(!authoring_platform_pending_project(platform));
    chibi_project project = { 0 };
    char error[256];
    REQUIRE(!chibi_project_read(&project, result.value, error, sizeof error));
    REQUIRE(!strcmp(project.name, "Created"));
    chibi_project_free(&project);
    REQUIRE(authoring_platform_release(platform, result.request));
    REQUIRE(authoring_platform_release(platform, chosen.request));
    return true;
}
TEST_CASE(platform_without_workspace_keeps_global_services_and_refuses_file_jobs)
{
    authoring_platform_free(platform);
    authoring_platform_config config = { .request = adapter, .cancel = cancel };
    char error[256];
    platform = authoring_platform_new(&config, error, sizeof error);
    REQUIRE(platform);
    static const char *const operations[] = { "reveal-path",
                                              "compile",
                                              "export",
                                              "import-file",
                                              "export-project" };
    for (size_t i = 0; i < sizeof operations / sizeof *operations; i++) {
        authoring_platform_result result = authoring_platform_request(
            platform,
            operations[i],
            "content/asset.kdl",
            0,
            0,
            0
        );
        REQUIRE(!result.ok && !strcmp(result.state, "unavailable"));
        REQUIRE(!strcmp(result.status, "no-workspace"));
        REQUIRE(authoring_platform_release(platform, result.request));
    }
    REQUIRE(adapter_calls == 0);
    authoring_platform_result
        result = authoring_platform_request(platform, "clipboard-read", "", 0, 0, 0);
    REQUIRE(result.ok && !strcmp(result.state, "running") && adapter_calls == 1);
    result = authoring_platform_cancel(platform, result.request);
    REQUIRE(!strcmp(result.state, "cancelled") && cancelled == 1);
    REQUIRE(authoring_platform_release(platform, result.request));
    return true;
}
TEST_CASE(platform_compile_passes_absolute_custom_content_and_reserved_scene_paths)
{
    char tool[256], path[256];
    snprintf(tool, sizeof tool, "%s/compiler", root);
    FILE *file = fopen(tool, "wb");
    REQUIRE(file);
    REQUIRE(
        fputs("#!/bin/sh\n[ \"$1\" = --project-root ] && [ \"$3\" = --output ] && [ \"$5\" = --scene ] || exit 18\ncase \"$6\" in \"$2\"/assets/*|\"$2\"/.chibi/*) ;; *) exit 19 ;; esac\n[ -f \"$6\" ] || exit 20\nprintf '%s' \"$6\"\n", file) >=
        0
    );
    REQUIRE(!fclose(file));
    REQUIRE(!chmod(tool, 0700));
    authoring_platform_free(platform);
    authoring_platform_config config = { .root = root, .compiler = tool };
    char error[256];
    platform = authoring_platform_new(&config, error, sizeof error);
    REQUIRE(platform);
    const char *folders[] = { "assets", ".chibi" };
    for (int i = 0; i < 2; i++) {
        snprintf(path, sizeof path, "%s/%s", root, folders[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    for (int i = 0; i < 2; i++) {
        char relative[80];
        snprintf(relative, sizeof relative, "%s/custom scene.kdl", folders[i]);
        snprintf(path, sizeof path, "%s/%s", root, relative);
        file = fopen(path, "wb");
        REQUIRE(file);
        REQUIRE(fputs("scene", file) >= 0);
        REQUIRE(!fclose(file));
        authoring_platform_result
            result = authoring_platform_request(platform, "compile", relative, 0, 0, 0);
        int request = result.request;
        for (int tick = 0; tick < 2000 && !strcmp(result.state, "running"); tick++) {
            authoring_platform_tick(platform);
            result = authoring_platform_poll(platform, request);
            if (!strcmp(result.state, "running")) {
                nanosleep(&(struct timespec){ .tv_nsec = 1000000 }, NULL);
            }
        }
        REQUIRE(result.ok && !strcmp(result.state, "succeeded") && result.exit_code == 0);
        REQUIRE(result.log && result.log[0] == '/' && strstr(result.log, relative));
        REQUIRE(authoring_platform_release(platform, request));
    }
    return true;
}
TEST_CASE(platform_nested_text_focus_is_explicit_bounded_and_host_applied)
{
    /* A hidden/no-window host still supplies the focus of its input frame.
     * Replay must not lose target capture by re-reading absent OS focus. */
    REQUIRE(!authoring_platform_environment_get(platform).focused);
    authoring_platform_set_frame_focus(platform, true);
    REQUIRE(authoring_platform_environment_get(platform).focused);
    authoring_platform_set_frame_focus(platform, false);
    REQUIRE(!authoring_platform_environment_get(platform).focused);
    authoring_platform_set_frame_focus(NULL, true);
    REQUIRE(!authoring_platform_text_input_requested(platform));
    authoring_platform_result
        requested = authoring_platform_request(platform, "text-input", "", 1, 0, 0);
    REQUIRE(
        requested.ok && !strcmp(requested.state, "succeeded") &&
        authoring_platform_text_input_requested(platform)
    );
    REQUIRE(authoring_platform_release(platform, requested.request));
    requested = authoring_platform_request(platform, "text-input", "", 2, 0, 0);
    REQUIRE(!requested.ok && authoring_platform_text_input_requested(platform));
    REQUIRE(authoring_platform_release(platform, requested.request));
    requested = authoring_platform_request(platform, "text-input", "", 0, 0, 0);
    REQUIRE(requested.ok && !authoring_platform_text_input_requested(platform));
    REQUIRE(authoring_platform_release(platform, requested.request));
    REQUIRE(adapter_calls == 0);
    return true;
}
void test_tool_authoring_platform()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(platform_compile_passes_absolute_custom_content_and_reserved_scene_paths);
    RUN_TEST_CASE(platform_nested_text_focus_is_explicit_bounded_and_host_applied);
    RUN_TEST_CASE(platform_without_workspace_keeps_global_services_and_refuses_file_jobs);
    RUN_TEST_CASE(platform_workspace_creation_requires_completed_folder_source_and_never_activates);
    RUN_TEST_CASE(platform_async_pending_denied_and_snapshot_result);
    RUN_TEST_CASE(platform_cancel_rejects_late_completion_and_reused_handles);
    RUN_TEST_CASE(platform_fixed_operations_and_request_capacity);
    RUN_TEST_CASE(platform_window_scale_and_missing_jobs_are_truthful);
    RUN_TEST_CASE(platform_binding_copies_terminal_status_after_release);
    RUN_TEST_CASE(platform_import_is_exclusive_and_rejects_symlink_destination);
    RUN_TEST_CASE(platform_activation_is_queued_until_host_completion);
    pu_clear_setup();
}
