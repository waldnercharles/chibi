#include "assets/project.h"
#include "support/asset_text.h"
#include "support/text_file.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The error from opening a temporary project whose manifest starts with the
   given version field. The repository projects cover the current version. */
static const char *version_error(const char *version, char *error, size_t error_size)
{
    char root[] = "/tmp/chibi-project-version-XXXXXX";
    assert(mkdtemp(root));
    char manifest[64];
    snprintf(manifest, sizeof manifest, "%s/" CHIBI_PROJECT_MANIFEST, root);
    FILE *file = fopen(manifest, "w");
    assert(file);
    fprintf(file, "%s\ncontent \"content\"\n", version);
    fclose(file);
    chibi_project project = { 0 };
    error[0] = '\0';
    chibi_project_open(&project, root, error, error_size);
    chibi_project_free(&project);
    unlink(manifest);
    rmdir(root);
    return error;
}

TEST_CASE(project_manifests_open_only_supported_versions)
{
    char newer[32], error[256];
    snprintf(newer, sizeof newer, "version %d", CHIBI_PROJECT_VERSION + 1);
    bool valid = strstr(version_error(newer, error, sizeof error), "needs a newer build") &&
                 strstr(version_error("", error, sizeof error), "needs an integer version");
    REQUIRE(valid);
    return true;
}

/* A project at an older version names the version this build opens and how
 * to upgrade it, and its manifest stays as it was. */
TEST_CASE(project_manifests_older_than_current_name_how_to_upgrade)
{
    char root[] = "/tmp/chibi-project-older-XXXXXX";
    REQUIRE(mkdtemp(root));
    char manifest[64], text[64], error[512] = { 0 }, expected[512];
    snprintf(manifest, sizeof manifest, "%s/" CHIBI_PROJECT_MANIFEST, root);
    snprintf(text, sizeof text, "version %d\ncontent \"content\"\n", CHIBI_PROJECT_VERSION - 1);
    FILE *file = fopen(manifest, "w");
    REQUIRE(file && fputs(text, file) >= 0 && !fclose(file));
    char *resolved = realpath(manifest, NULL);
    REQUIRE(resolved);
    snprintf(
        expected,
        sizeof expected,
        "Project version %d is older than version %d, the only one this build "
        "opens: %s; edit it to the current format, or open it with the older "
        "Chibi SDK that wrote it and upgrade it there",
        CHIBI_PROJECT_VERSION - 1,
        CHIBI_PROJECT_VERSION,
        resolved
    );
    free(resolved);
    chibi_project project = { 0 };
    int status = chibi_project_open(&project, root, error, sizeof error);
    chibi_project_free(&project);
    char *after = text_file_read(manifest);
    bool unchanged = after && !strcmp(after, text);
    free(after);
    unlink(manifest);
    rmdir(root);
    if (status >= 0 || strcmp(error, expected)) {
        fprintf(stderr, "older project: %d %s\n", status, error);
    }
    REQUIRE(status < 0 && !strcmp(error, expected));
    REQUIRE(unchanged);
    return true;
}

TEST_CASE(project_creation_writes_an_empty_start_scene)
{
    char parent[] = "/tmp/chibi-project-create-XXXXXX", root[256], error[256];
    assert(mkdtemp(parent));
    snprintf(root, sizeof root, "%s/sample-project", parent);
    bool created = chibi_project_create(root, "Sample: Project", error, sizeof error) == 0;
    chibi_project project = { 0 };
    bool opened = created &&
                  chibi_project_open(&project, root, error, sizeof error) == 0;
    char *scene = opened ? chibi_project_scene_path(&project, NULL) : NULL;
    char *text = scene ? text_file_read(scene) : NULL;
    bool valid = opened && !strcmp(project.name, "Sample: Project") &&
                 !strcmp(project.start_scene, "scenes/Start.kdl") && text &&
                 strstr(text, "(array)entities");
    /* Project creation never overwrites an existing folder. */
    bool duplicate = chibi_project_create(root, "Other", error, sizeof error) != 0;
    free(text);
    free(scene);
    chibi_project_free(&project);
    char path[512];
    snprintf(path, sizeof path, "%s/content/scenes/Start.kdl", root);
    unlink(path);
    snprintf(path, sizeof path, "%s/project.kdl", root);
    unlink(path);
    snprintf(path, sizeof path, "%s/content/scenes", root);
    rmdir(path);
    snprintf(path, sizeof path, "%s/content", root);
    rmdir(path);
    rmdir(root);
    rmdir(parent);
    /* New projects serialize their name and open with an empty start scene. */
    REQUIRE(valid);
    REQUIRE(duplicate);
    return true;
}

TEST_CASE(contract_projects_resolve_their_own_content)
{
    const char *roots[] = { CHIBI_SOURCE_ROOT "/test/fixtures/language",
                            CHIBI_SOURCE_ROOT
                            "/test/fixtures/material-contracts" };
    const char *names[] = { "Language contracts", "Material contracts" };
    chibi_project projects[2] = { 0 };
    char error[256];
    bool valid = true;
    for (int i = 0; i < 2; i++) {
        bool opened = chibi_project_open(&projects[i], roots[i], error, sizeof error) == 0;
        valid &= opened;
        if (!opened) {
            continue;
        }
        char *scene = chibi_project_scene_path(&projects[i], NULL);
        char *text = scene ? text_file_read(scene) : NULL;
        valid &= !strcmp(projects[i].name, names[i]) &&
                 !strcmp(projects[i].start_scene, "scenes/Start.kdl") && text &&
                 strstr(text, "(array)entities");
        free(text);
        free(scene);
    }
    if (projects[0].root && projects[1].root) {
        valid &= strcmp(projects[0].root, projects[1].root) &&
                 strcmp(projects[0].content, projects[1].content);
    }
    for (int i = 0; i < 2; i++) {
        chibi_project_free(&projects[i]);
    }
    /* Independent contract projects resolve their own manifests, content and start scenes. */
    REQUIRE(valid);
    return true;
}

void test_project_document()
{
    RUN_TEST_CASE(project_manifests_open_only_supported_versions);
    RUN_TEST_CASE(project_manifests_older_than_current_name_how_to_upgrade);
    RUN_TEST_CASE(project_creation_writes_an_empty_start_scene);
    RUN_TEST_CASE(contract_projects_resolve_their_own_content);
}
