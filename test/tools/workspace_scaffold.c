#include "authoring/workspace_scaffold.h"

#include "assets/project.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yyjson.h>

static char parent[128], error[256];
static char *created, *json;
static void cleanup()
{
    free(created);
    created = NULL;
    free(json);
    json = NULL;
    if (*parent) {
        tool_remove_tree(parent);
        *parent = 0;
    }
}
static bool setup_parent()
{
    strcpy(parent, "/tmp/chibi-scaffold-XXXXXX");
    REQUIRE(mkdtemp(parent));
    return true;
}
static void spec(const char *name, const char *extra_path, const char *suffix)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc), *files = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_str(doc, root, "Name", name);
    yyjson_mut_obj_add_val(doc, root, "Files", files);
    char manifest[512];
    snprintf(
        manifest,
        sizeof manifest,
        "version %d\nname \"%s\"\ncontent \"content\"\nstart_scene "
        "\"scenes/Start.kdl\"\n%s",
        CHIBI_PROJECT_VERSION,
        name,
        suffix ? suffix : ""
    );
    const char *paths[] = { "project.kdl", "content/scenes/Start.kdl", extra_path };
    const char *texts[] = { manifest,
                            "version 1\nname \"Start\"\n(array)entities\n",
                            "snow 雪 and \"quotes\"\n" };
    for (int i = 0; i < (extra_path ? 3 : 2); i++) {
        yyjson_mut_val *row = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, row, "Path", paths[i]);
        yyjson_mut_obj_add_str(doc, row, "Text", texts[i]);
        yyjson_mut_arr_append(files, row);
    }
    free(json);
    json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
}
TEST_CASE(workspace_scaffold_creates_current_project_exclusively_and_retains_utf8_text)
{
    REQUIRE(setup_parent());
    spec("New World 雪", "content/notes.txt", NULL);
    REQUIRE(workspace_scaffold_create(parent, json, &created, error, sizeof error));
    chibi_project project = { 0 };
    REQUIRE(!chibi_project_read(&project, created, error, sizeof error));
    REQUIRE(!strcmp(project.name, "New World 雪"));
    chibi_project_free(&project);
    char path[256], bytes[128];
    snprintf(path, sizeof path, "%s/content/notes.txt", created);
    FILE *file = fopen(path, "rb");
    REQUIRE(file);
    size_t n = fread(bytes, 1, sizeof bytes - 1, file);
    bytes[n] = 0;
    fclose(file);
    REQUIRE(!strcmp(bytes, "snow 雪 and \"quotes\"\n"));
    char *duplicate = NULL;
    REQUIRE(!workspace_scaffold_create(parent, json, &duplicate, error, sizeof error));
    REQUIRE(!duplicate && *error);
    REQUIRE(!chibi_project_read(&project, created, error, sizeof error));
    chibi_project_free(&project);
    return true;
}
TEST_CASE(workspace_scaffold_preflight_refuses_traversal_duplicate_and_portability_collisions)
{
    REQUIRE(setup_parent());
    const char *paths[] = { "../escape",
                            "content/../escape",
                            "content/scenes/Start.kdl",
                            "CONTENT/SCENES/start.KDL",
                            "content",
                            "content//bad",
                            "content/CON.txt" };
    for (int i = 0; i < 7; i++) {
        spec("Example", paths[i], NULL);
        REQUIRE(!workspace_scaffold_create(parent, json, &created, error, sizeof error));
        REQUIRE(!created);
        char path[256];
        snprintf(path, sizeof path, "%s/Example", parent);
        REQUIRE(access(path, F_OK));
    }
    spec("CON", NULL, NULL);
    REQUIRE(!workspace_scaffold_create(parent, json, &created, error, sizeof error));
    REQUIRE(!workspace_scaffold_create(
        parent,
        "{\"Name\":\"A\",\"Name\":\"B\",\"Files\":[]}",
        &created,
        error,
        sizeof error
    ));
    return true;
}
TEST_CASE(workspace_scaffold_canonical_loader_failure_rolls_back_owned_files_only)
{
    REQUIRE(setup_parent());
    char sentinel[256];
    snprintf(sentinel, sizeof sentinel, "%s/keep.txt", parent);
    FILE *file = fopen(sentinel, "wb");
    REQUIRE(file);
    REQUIRE(fwrite("keep", 1, 4, file) == 4);
    fclose(file);
    spec("Broken", NULL, "application width=0\n");
    REQUIRE(!workspace_scaffold_create(parent, json, &created, error, sizeof error));
    REQUIRE(!created && *error);
    char path[256];
    snprintf(path, sizeof path, "%s/Broken", parent);
    REQUIRE(access(path, F_OK));
    REQUIRE(!access(sentinel, F_OK));
    return true;
}
void test_workspace_scaffold()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(workspace_scaffold_creates_current_project_exclusively_and_retains_utf8_text);
    RUN_TEST_CASE(workspace_scaffold_preflight_refuses_traversal_duplicate_and_portability_collisions);
    RUN_TEST_CASE(workspace_scaffold_canonical_loader_failure_rolls_back_owned_files_only);
    pu_setup(NULL, NULL);
}
