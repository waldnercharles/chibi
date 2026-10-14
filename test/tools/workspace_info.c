#include "authoring/workspace_info.h"

#include "assets/project.h"
#include "authoring/workspace_info_definition.h"
#include "execution/function.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char root[128], error[256];
static workspace_info *service;
static workspace_manifest manifest;
static arena storage;
static void cleanup()
{
    workspace_info_free(service);
    service = NULL;
    workspace_manifest_free(&manifest);
    arena_free(&storage);
    if (*root) {
        tool_remove_tree(root);
        *root = 0;
    }
}
static bool write_manifest(const char *text)
{
    char path[256];
    snprintf(path, sizeof path, "%s/project.kdl", root);
    FILE *file = fopen(path, "wb");
    REQUIRE(file);
    REQUIRE(fwrite(text, 1, strlen(text), file) == strlen(text));
    REQUIRE(!fclose(file));
    return true;
}
TEST_CASE(workspace_info_caches_manifest_and_copies_native_query_without_disk_access)
{
    strcpy(root, "/tmp/chibi-info-XXXXXX");
    REQUIRE(mkdtemp(root));
    char text[256];
    snprintf(text, sizeof text, "version %d\nname \"Quiet World\"\ncontent \"data\"\nstart_scene \"scenes/Opening.kdl\"\n", CHIBI_PROJECT_VERSION);
    REQUIRE(write_manifest(text));
    service = workspace_info_new(root, error, sizeof error);
    REQUIRE(service);
    const workspace_manifest *info = workspace_info_get(service);
    REQUIRE(!strcmp(info->name, "Quiet World") && !strcmp(info->content, "data"));
    REQUIRE(!strcmp(info->start_scene, "data/scenes/Opening.kdl"));
    char path[256];
    snprintf(path, sizeof path, "%s/project.kdl", root);
    REQUIRE(!unlink(path));
    behavior_function_context context = { .value_storage = &storage };
    pin_value out[11] = { 0 };
    REQUIRE(!WORKSPACE_INFO_CALLS[0].context(&context, service, NULL, out));
    REQUIRE(
        out[0].b && !strcmp(out[2].str, "Quiet World") &&
        out[5].i == CHIBI_PROJECT_VERSION && !out[8].b
    );
    REQUIRE(
        !workspace_info_set_pending_project(service, "/explicit/old", CHIBI_PROJECT_VERSION - 1)
    );
    REQUIRE(!*out[9].str && out[10].i == 0);
    workspace_info_set_documents_restored(service, true);
    REQUIRE(!WORKSPACE_INFO_CALLS[0].context(&context, service, NULL, out));
    REQUIRE(out[8].b);
    REQUIRE(!workspace_info_refresh(service, error, sizeof error));
    workspace_info_free(service);
    service = NULL;
    REQUIRE(!strcmp(out[2].str, "Quiet World") && !strcmp(out[4].str, "data/scenes/Opening.kdl"));
    return true;
}
TEST_CASE(workspace_info_manifest_paths_are_contained_and_current)
{
    char text[256];
    snprintf(text, sizeof text, "version %d\nname \"Example\"\ncontent \"assets\"\nstart_scene \"content/scenes/Start.kdl\"\n", CHIBI_PROJECT_VERSION);
    REQUIRE(workspace_manifest_parse("/workspace", text, &manifest, error, sizeof error));
    REQUIRE(
        !strcmp(manifest.content, "assets") &&
        !strcmp(manifest.start_scene, "content/scenes/Start.kdl")
    );
    workspace_manifest_free(&manifest);
    const char *bad[] = { "../outside",
                          "/outside/start.kdl",
                          "scenes/../Start.kdl",
                          "scenes//Start.kdl",
                          "scenes/Start.kdl/" };
    for (int i = 0; i < 5; i++) {
        snprintf(
            text,
            sizeof text,
            "version %d\nname \"Example\"\ncontent \"content\"\nstart_scene "
            "\"%s\"\n",
            CHIBI_PROJECT_VERSION,
            bad[i]
        );
        REQUIRE(!workspace_manifest_parse("/workspace", text, &manifest, error, sizeof error));
        REQUIRE(!manifest.root && !manifest.name && *error);
    }
    snprintf(
        text,
        sizeof text,
        "version %d\nname \"Example\"\nstart_scene \"scenes/Start.kdl\"\n",
        CHIBI_PROJECT_VERSION - 1
    );
    REQUIRE(!workspace_manifest_parse("/workspace", text, &manifest, error, sizeof error));
    return true;
}
TEST_CASE(workspace_info_rejects_manifest_symlinks_and_preserves_last_good_refresh)
{
    strcpy(root, "/tmp/chibi-info-links-XXXXXX");
    REQUIRE(mkdtemp(root));
    char text[256];
    snprintf(text, sizeof text, "version %d\nname \"Example\"\nstart_scene \"scenes/Start.kdl\"\n", CHIBI_PROJECT_VERSION);
    REQUIRE(write_manifest(text));
    service = workspace_info_new(root, error, sizeof error);
    REQUIRE(service);
    REQUIRE(write_manifest("version 0\nname \"Wrong\"\n"));
    REQUIRE(!workspace_info_refresh(service, error, sizeof error));
    REQUIRE(!strcmp(workspace_info_get(service)->name, "Example"));
    workspace_info_free(service);
    service = NULL;
    char path[256], other[256];
    snprintf(path, sizeof path, "%s/project.kdl", root);
    snprintf(other, sizeof other, "%s/other.kdl", root);
    REQUIRE(!rename(path, other));
    REQUIRE(!symlink("other.kdl", path));
    REQUIRE(!workspace_info_new(root, error, sizeof error));
    return true;
}
TEST_CASE(workspace_info_idle_has_no_paths_and_current_scaffold_version)
{
    service = workspace_info_idle();
    REQUIRE(service && !workspace_info_active(service));
    workspace_info_set_documents_restored(service, true);
    behavior_function_context context = { .value_storage = &storage };
    pin_value out[11] = { 0 };
    REQUIRE(!WORKSPACE_INFO_CALLS[0].context(&context, service, NULL, out));
    REQUIRE(out[0].b && !out[7].b && !out[8].b && out[5].i == CHIBI_PROJECT_VERSION);
    REQUIRE(
        !*out[1].str && !*out[2].str && !*out[3].str && !*out[4].str && !*out[6].str
    );
    REQUIRE(!workspace_info_refresh(service, error, sizeof error) && *error);
    return true;
}
TEST_CASE(workspace_info_pending_intent_is_owned_idle_metadata_without_filesystem_access)
{
    service = workspace_info_idle();
    char requested[] = "/explicit-not-read/雪/World";
    REQUIRE(workspace_info_set_pending_project(service, requested, CHIBI_PROJECT_VERSION - 1));
    requested[1] = 'X';
    REQUIRE(!strcmp(workspace_info_pending_root(service), "/explicit-not-read/雪/World"));
    REQUIRE(workspace_info_pending_version(service) == CHIBI_PROJECT_VERSION - 1);
    REQUIRE(!workspace_info_set_pending_project(service, "/other", CHIBI_PROJECT_VERSION));
    REQUIRE(!workspace_info_set_pending_project(service, "/other\npath", CHIBI_PROJECT_VERSION - 1));
    REQUIRE(!workspace_info_set_pending_project(service, "\xff", CHIBI_PROJECT_VERSION - 1));
    REQUIRE(!strcmp(workspace_info_pending_root(service), "/explicit-not-read/雪/World"));
    behavior_function_context context = { .value_storage = &storage };
    pin_value out[11] = { 0 };
    REQUIRE(!WORKSPACE_INFO_CALLS[0].context(&context, service, NULL, out));
    REQUIRE(out[0].b && !out[7].b && !*out[1].str && !*out[3].str);
    REQUIRE(
        !strcmp(out[9].str, "/explicit-not-read/雪/World") &&
        out[10].i == CHIBI_PROJECT_VERSION - 1
    );
    REQUIRE(workspace_info_set_pending_project(service, NULL, 0));
    REQUIRE(!*workspace_info_pending_root(service) && !workspace_info_pending_version(service));
    workspace_info_free(service);
    service = NULL;
    REQUIRE(!strcmp(out[9].str, "/explicit-not-read/雪/World"));
    return true;
}
void test_workspace_info()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(workspace_info_idle_has_no_paths_and_current_scaffold_version);
    RUN_TEST_CASE(workspace_info_pending_intent_is_owned_idle_metadata_without_filesystem_access);
    RUN_TEST_CASE(workspace_info_caches_manifest_and_copies_native_query_without_disk_access);
    RUN_TEST_CASE(workspace_info_manifest_paths_are_contained_and_current);
    RUN_TEST_CASE(workspace_info_rejects_manifest_symlinks_and_preserves_last_good_refresh);
    pu_setup(NULL, NULL);
}
