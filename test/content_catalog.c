#include "assets/project.h"
#include "suite_internal.h"
#include "support/text_file.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void check_project_versions()
{
    char older[32], newer[32], error[256];
    snprintf(older, sizeof older, "version %d", CHIBI_PROJECT_OLDEST_VERSION - 1);
    snprintf(newer, sizeof newer, "version %d", CHIBI_PROJECT_VERSION + 1);
    bool valid = strstr(version_error(older, error, sizeof error), "needs an older build") &&
                 strstr(version_error(newer, error, sizeof error), "needs a newer build") &&
                 strstr(version_error("", error, sizeof error), "needs an integer version");
    test_check(valid, "project manifests open only from the oldest version this build upgrades");
}

static void check_project_creation()
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
    test_check(valid, "new projects serialize their name and open with an empty start scene");
    bool duplicate = chibi_project_create(root, "Other", error, sizeof error) != 0;
    test_check(duplicate, "project creation never overwrites an existing folder");
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
}

void run_content_catalog_checks()
{
    check_project_versions();
    check_project_creation();
    const char *roots[] = { "test/fixtures/language",
                            "test/fixtures/material-contracts" };
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
    test_check(valid, "Independent contract projects resolve their own manifests, content and start scenes");
    for (int i = 0; i < 2; i++) {
        chibi_project_free(&projects[i]);
    }
}
