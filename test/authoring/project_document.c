#include "assets/project.h"
#include "support/asset_document.h"
#include "support/text_file.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The error from opening a temporary project at the given version; zero omits
 * the field. The repository projects cover the current version. */
static const char *version_error(int version, char *error, size_t error_size)
{
    char root[] = "/tmp/chibi-project-version-XXXXXX";
    assert(mkdtemp(root));
    char manifest[64];
    snprintf(manifest, sizeof manifest, "%s/" CHIBI_PROJECT_MANIFEST, root);
    FILE *file = fopen(manifest, "w");
    assert(file);
    av_doc *doc = av_doc_new();
    av_value *value = av_obj(doc);
    av_doc_set_root(doc, value);
    if (version) {
        av_obj_add_int(doc, value, "version", version);
    }
    av_obj_add_str(doc, value, "content", "content");
    char *text = asset_document_write(value);
    assert(text);
    fputs(text, file);
    free(text);
    av_doc_free(doc);
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
    char error[256];
    bool valid = strstr(version_error(CHIBI_PROJECT_OLDEST_VERSION - 1, error, sizeof error), "needs an older build") &&
                 strstr(version_error(CHIBI_PROJECT_VERSION + 1, error, sizeof error), "needs a newer build") &&
                 strstr(version_error(0, error, sizeof error), "needs an integer version");
    /* Project manifests open only from the oldest version this build upgrades. */
    REQUIRE(valid);
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
    av_doc *doc = text ? asset_document_read(text, error, sizeof error) : NULL;
    av_value *entities = av_obj_get(av_doc_root(doc), "entities");
    bool valid = opened && !strcmp(project.name, "Sample: Project") &&
                 !strcmp(project.start_scene, "scenes/Start.chibi") && doc &&
                 av_is_arr(entities) && av_arr_size(entities) == 0;
    av_doc_free(doc);
    /* Project creation never overwrites an existing folder. */
    bool duplicate = chibi_project_create(root, "Other", error, sizeof error) != 0;
    free(text);
    free(scene);
    chibi_project_free(&project);
    char path[512];
    snprintf(path, sizeof path, "%s/content/scenes/Start.chibi", root);
    unlink(path);
    snprintf(path, sizeof path, "%s/project.chibi", root);
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
        av_doc *doc = text ? asset_document_read(text, error, sizeof error) : NULL;
        av_value *entities = av_obj_get(av_doc_root(doc), "entities");
        valid &= !strcmp(projects[i].name, names[i]) &&
                 !strcmp(projects[i].start_scene, "scenes/Start.chibi") &&
                 doc && av_is_arr(entities) && av_arr_size(entities) == 0;
        av_doc_free(doc);
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
    RUN_TEST_CASE(project_creation_writes_an_empty_start_scene);
    RUN_TEST_CASE(contract_projects_resolve_their_own_content);
}
