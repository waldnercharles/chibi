#include "assets/project.h"
#include "assets/project_migrate.h"
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
    char older[32], newer[32], error[256];
    snprintf(older, sizeof older, "version %d", CHIBI_PROJECT_OLDEST_VERSION - 1);
    snprintf(newer, sizeof newer, "version %d", CHIBI_PROJECT_VERSION + 1);
    bool valid = strstr(version_error(older, error, sizeof error), "needs an older build") &&
                 strstr(version_error(newer, error, sizeof error), "needs a newer build") &&
                 strstr(version_error("", error, sizeof error), "needs an integer version");
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

static char retirement_root[128];
static void retirement_path(char *path, size_t size, const char *name)
{
    snprintf(path, size, "%s/%s", retirement_root, name);
}
static void retirement_write(const char *name, const char *text)
{
    char path[256];
    retirement_path(path, sizeof path, name);
    FILE *file = fopen(path, "w");
    assert(file);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
}
static char *retirement_read(const char *name)
{
    char path[256];
    retirement_path(path, sizeof path, name);
    return text_file_read(path);
}
static void retirement_cleanup()
{
    static const char *const names[] = {
        "content/Actor.kdl",  "content/Leaf.kdl",    "content/Material.kdl",
        "content/Read.chibi", "content/Types.chibi", "content/Start.kdl",
        "project.kdl"
    };
    char path[256];
    for (size_t i = 0; retirement_root[0] && i < sizeof names / sizeof *names; i++) {
        retirement_path(path, sizeof path, names[i]);
        unlink(path);
    }
    if (retirement_root[0]) {
        retirement_path(path, sizeof path, "content");
        rmdir(path);
        rmdir(retirement_root);
        retirement_root[0] = 0;
    }
}
static void retirement_setup()
{
    strcpy(retirement_root, "/tmp/chibi-project-retirement-XXXXXX");
    assert(mkdtemp(retirement_root));
    char path[256];
    retirement_path(path, sizeof path, "content");
    assert(!mkdir(path, 0755));
    retirement_write("project.kdl", "version 38\nname \"Retirement\"\ncontent \"content\"\nstart_scene \"Start.kdl\"\n");
    retirement_write("content/Start.kdl", "version 1\nname \"Start\"\n(array)entities\n");
    retirement_write("content/Leaf.kdl", "name \"Leaf\"\ncomponents { Transform x=0 y=0 }\n");
}

TEST_CASE(project_retirement_migrates_owned_values_and_preserves_shader_wires)
{
    retirement_write(
        "content/Actor.kdl",
        "name \"Actor\"\ncomponents {\n Rhythm bpm=120\n VisualMotion speed=2\n"
        " Transform x=7 y=9\n GlobalBinding name=\"Rhythm\"\n}\n"
        "detached_components { VisualMotion speed=3 }\n"
        "(array)children { - {\n name \"child\"\n prototype \"Leaf\"\n"
        " components { Rhythm bpm=80; VisualTransform yaw=0.5 }\n"
        " retained_components { VisualMotion speed=4 }\n"
        " (array)children { - { name \"grandchild\"; prototype \"Leaf\"; "
        "components { VisualMotion speed=5 } } }\n } }\n"
    );
    retirement_write(
        "content/Material.kdl",
        "version 3\nname \"Material\"\ndomain \"material\"\nid_counter "
        "200\n(array)graphs {\n "
        "- {\n"
        " kind \"event\"\n name \"\"\n"
        " (array)nodes {\n"
        " - { id 41; type \"Shader Beat Pulse\"; outputs { Value id=52 "
        "type=\"number\" } }\n"
        " - id=61 type=\"Shader Beat Phase\"\n"
        " - { id 71; type \"Shader Add\"; inputs { B flags=0 group=0 "
        "default=0.375 } }\n"
        " - id=81 type=\"Shader Time\"\n"
        " - { id 91; type \"Shader Surface Output\"; inputs Roughness=0.1 "
        "Unlit=#true }\n }\n"
        " (array)edges {\n - from=\"41.Value\" to=\"71.A\"\n"
        " - from=\"71.Value\" to=\"91.Roughness\"\n"
        " - from=\"61.Value\" to=\"91.Metallic\"\n }\n }\n}\n"
    );
    char error[512];
    int migrated = chibi_project_migrate(retirement_root, error, sizeof error);
    if (migrated != 1) {
        fprintf(stderr, "Retirement fixture migration returned %d: %s\n", migrated, error);
    }
    REQUIRE(migrated == 1);
    char *actor = retirement_read("content/Actor.kdl"),
         *material = retirement_read("content/Material.kdl");
    REQUIRE(actor && material);
    av_doc *document = asset_text_read(actor, error, sizeof error);
    REQUIRE(document);
    av_value *root = av_doc_root(document),
             *components = av_obj_get(root, "components"),
             *child = av_arr_get(av_obj_get(root, "children"), 0);
    REQUIRE(!av_obj_get(components, "Rhythm") && !av_obj_get(components, "VisualMotion"));
    REQUIRE(av_get_int(av_obj_get(av_obj_get(components, "Transform"), "x")) == 7);
    REQUIRE(
        !strcmp(av_get_str(av_obj_get(av_obj_get(components, "GlobalBinding"), "name")), "Rhythm")
    );
    REQUIRE(!av_obj_get(root, "detached_components"));
    REQUIRE(!av_obj_get(av_obj_get(child, "components"), "Rhythm"));
    REQUIRE(av_obj_get(av_obj_get(child, "components"), "VisualTransform"));
    REQUIRE(!av_obj_get(child, "retained_components"));
    REQUIRE(
        !av_obj_get(av_obj_get(av_arr_get(av_obj_get(child, "children"), 0), "components"), "VisualMotion")
    );
    av_doc_free(document);
    document = asset_text_read(material, error, sizeof error);
    REQUIRE(document);
    av_value *graph = av_arr_get(av_obj_get(av_doc_root(document), "graphs"), 0),
             *nodes = av_obj_get(graph, "nodes");
    for (size_t i = 0; i < 2; i++) {
        av_value *node = av_arr_get(nodes, i),
                 *value = av_obj_get(av_obj_get(node, "outputs"), "Value");
        REQUIRE(!strcmp(av_get_str(av_obj_get(node, "type")), "Shader Constant"));
        REQUIRE(
            av_is_num(av_obj_get(value, "default")) &&
            av_get_num(av_obj_get(value, "default")) == 0
        );
    }
    REQUIRE(av_get_int(av_obj_get(av_arr_get(nodes, 0), "id")) == 41);
    REQUIRE(
        av_get_int(
            av_obj_get(av_obj_get(av_obj_get(av_arr_get(nodes, 0), "outputs"), "Value"), "id")
        ) == 52
    );
    REQUIRE(!strcmp(av_get_str(av_obj_get(av_arr_get(nodes, 3), "type")), "Shader Time"));
    REQUIRE(
        !strcmp(av_get_str(av_obj_get(av_arr_get(av_obj_get(graph, "edges"), 0), "from")), "41.Value")
    );
    av_doc_free(document);
    char content[256];
    retirement_path(content, sizeof content, "content");
    REQUIRE(!chibi_project_migrate_content(content, NULL, 38, error, sizeof error));
    char *again = retirement_read("content/Material.kdl");
    REQUIRE(again && !strcmp(material, again));
    free(again);
    again = retirement_read("content/Actor.kdl");
    REQUIRE(again && !strcmp(actor, again));
    free(again);
    free(actor);
    free(material);
    return true;
}

TEST_CASE(project_retirement_preserves_authored_names_and_rejects_native_uses_before_writes)
{
    static const char *const uses[] = {
        "P :: behavior { on Begin :: () { r:=scene_rhythm(); } }\n",
        "P :: behavior { on Begin :: () { trigger_visual_reaction(vec3(0,0,0), "
        "1); } }\n",
        "P :: behavior { on Begin :: () { r:=self.Rhythm; } }\n",
        "P :: behavior { on Begin :: () { r:=self.VisualMotion; } }\n",
        "P :: prototype { Rhythm { bpm:120, pulse_width:0.2 } }\n",
        "P :: prototype { VisualMotion { speed:2 } }\n",
        "P :: prototype { "
        "children:[{/* child */ name:\"child\",prototype:Leaf,"
        "components:{/* component */ VisualMotion:{"
        "speed:2}}}] }\n",
        "Move :: system(motion: ^VisualMotion) { }\n",
        "Move :: system(position: ^Transform, if !VisualMotion && !Rhythm) { "
        "}\n",
        "P :: behavior { on Begin :: () { r:=has_component(Component: Rhythm, "
        "Entity: self); } }\n",
        "P :: behavior { on Begin :: () { set_component(VisualMotion, self, "
        "{}); } }\n"
    };
    const char *actor = "name \"Actor\"\ncomponents { Rhythm bpm=120; "
                        "VisualMotion speed=2 }\n";
    const char
        *material = "version 3\ndomain \"material\"\n(array)graphs { - { "
                    "(array)nodes { - id=1 type=\"Shader Beat Pulse\" } } }\n";
    retirement_write("content/Actor.kdl", actor);
    retirement_write("content/Material.kdl", material);
    char error[512];
    for (size_t i = 0; i < sizeof uses / sizeof *uses; i++) {
        retirement_write("content/Read.chibi", uses[i]);
        REQUIRE(chibi_project_migrate(retirement_root, error, sizeof error) < 0);
        REQUIRE(strstr(error, "Read.chibi:") && strstr(error, "before migrating"));
        char *after = retirement_read("content/Actor.kdl");
        REQUIRE(after && !strcmp(actor, after));
        free(after);
        after = retirement_read("content/Material.kdl");
        REQUIRE(after && !strcmp(material, after));
        free(after);
        after = retirement_read("project.kdl");
        REQUIRE(after && strstr(after, "version 38"));
        free(after);
    }
    const char *authored = "P :: behavior {\n"
                           " scene_rhythm :: () -> int { return 7; }\n"
                           " trigger_visual_reaction :: () { }\n"
                           " on Begin :: () { r:=scene_rhythm(); "
                           "trigger_visual_reaction();\n"
                           " text:=\"scene_rhythm() self.VisualMotion\";\n"
                           " // trigger_visual_reaction()\n"
                           " Helper.scene_rhythm(); value:={VisualMotion:9}; "
                           "r=value.VisualMotion; }\n}\n";
    retirement_write("content/Read.chibi", authored);
    int migrated = chibi_project_migrate(retirement_root, error, sizeof error);
    if (migrated != 1) {
        fprintf(stderr, "Authored-name migration returned %d: %s\n", migrated, error);
    }
    REQUIRE(migrated == 1);
    char *after = retirement_read("content/Read.chibi");
    REQUIRE(after && !strcmp(authored, after));
    free(after);
    return true;
}

TEST_CASE(project_retirement_preserves_authored_component_types)
{
    const char *sources[] = {
        "VisualMotion :: component { speed:number }\n",
        "package custom\nVisualMotion :: struct { speed:number }\n"
        "Use :: (value:VisualMotion) -> number { return value.speed; }\n",
        "P :: behavior { Rhythm:int=7; set_component :: (Component:int) { }\n"
        "on Begin :: () { set_component(Component:Rhythm); } }\n",
        "Move :: system(Rhythm:=true, if Rhythm) { }\n",
        "Move :: system(if Check(Rhythm:true)) { Check :: (Rhythm:bool) -> "
        "bool { return Rhythm; } }\n"
    };
    char content[256], error[512];
    retirement_path(content, sizeof content, "content");
    for (size_t i = 0; i < sizeof sources / sizeof *sources; i++) {
        retirement_write("content/Read.chibi", sources[i]);
        REQUIRE(!chibi_project_migrate_content(content, NULL, 38, error, sizeof error));
        char *after = retirement_read("content/Read.chibi");
        REQUIRE(after && !strcmp(after, sources[i]));
        free(after);
    }
    const char
        *types = "package custom\nVisualMotion :: struct { speed:number }\n"
                 "Rhythm :: union { Idle }\n";
    const char
        *consumer = "package custom\nUse :: (motion:VisualMotion, "
                    "rhythm:Rhythm) -> number { return motion.speed; }\n";
    retirement_write("content/Types.chibi", types);
    retirement_write("content/Read.chibi", consumer);
    REQUIRE(!chibi_project_migrate_content(content, NULL, 38, error, sizeof error));
    char *after = retirement_read("content/Read.chibi");
    REQUIRE(after && !strcmp(after, consumer));
    free(after);
    after = retirement_read("content/Types.chibi");
    REQUIRE(after && !strcmp(after, types));
    free(after);
    retirement_write("content/Types.chibi", "package other\nVisualMotion :: struct { speed:number }\n");
    REQUIRE(chibi_project_migrate_content(content, NULL, 38, error, sizeof error) < 0);
    REQUIRE(strstr(error, "VisualMotion was removed"));
    return true;
}

void test_project_document()
{
    RUN_TEST_CASE(project_manifests_open_only_supported_versions);
    RUN_TEST_CASE(project_creation_writes_an_empty_start_scene);
    RUN_TEST_CASE(contract_projects_resolve_their_own_content);
    pu_setup(retirement_setup, retirement_cleanup);
    RUN_TEST_CASE(project_retirement_migrates_owned_values_and_preserves_shader_wires);
    RUN_TEST_CASE(project_retirement_preserves_authored_names_and_rejects_native_uses_before_writes);
    RUN_TEST_CASE(project_retirement_preserves_authored_component_types);
    pu_clear_setup();
}
