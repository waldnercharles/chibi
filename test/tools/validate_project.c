#include "assets/project.h"
#include "assets/prototype.h"
#include "support/asset_document.h"
#include "test/tools/fixture.h"
#if CHIBI_TEST_MODEL_IMPORT
#include "test/support/gltf_fixture.h"
#endif
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The longest prototype name the player takes, PROTOTYPE_NAME_MAX. */
#define PROTOTYPE_NAME_MAX 1024
/* How many resources a player takes in all, PLAYER_MAX_ASSETS. */
#define PLAYER_MAX_ASSETS 4096

/* An Ogg Vorbis file's first page holds only its 30-byte identification
 * header; a page with no segments is its header alone. */
enum
{
    OGG_PAGE_HEADER = 27,
    OGG_FIRST_PAGE = OGG_PAGE_HEADER + 1 + 30,
    OGG_END_OF_STREAM = 4
};

static char program[4096];
static char root[64], log_path[96];
static yyjson_doc *reply;

static void path(char *out, size_t size, const char *relative)
{
    snprintf(out, size, "%s/%s", root, relative);
}

static void write_file(const char *relative, const char *text)
{
    char file[256];
    path(file, sizeof file, relative);
    tool_write(file, text);
}
/* A fixture replaces the authored prototype the scaffold made. */
static bool remove_authored_prototype(const char *name)
{
    char relative[128], file[256];
    snprintf(relative, sizeof relative, "game/content/prototypes/%s.chibi", name);
    path(file, sizeof file, relative);
    return unlink(file) == 0;
}

/* Runs a shell command from `root` that sees chibi_ctl as $0 and its own
 * output as $1, using the optional `allowed` duration; zero has no time limit.
 * Returns its exit status and keeps the last reply. */
static int run_within(const char *command, double allowed)
{
    const char *args[] = { "/bin/sh", "-c", command, program, log_path, NULL };
    int status = tool_run_within(args, root, log_path, allowed);
    char *output = tool_read(log_path);
    const char *json = output ? strstr(output, "{\"contract\"") : NULL;
    for (const char *next; json && (next = strstr(json + 1, "{\"contract\""));) {
        json = next;
    }
    yyjson_doc_free(reply);
    reply = json ? yyjson_read(json, strlen(json), YYJSON_READ_STOP_WHEN_DONE) : NULL;
    free(output);
    assert(reply);
    return status;
}

static int run(const char *command)
{
    return run_within(command, 0);
}

/* Sends requests, one per line, to one chibi_ctl, inside the project "game"
 * when `scoped`. */
static int ctl(bool scoped, const char *request)
{
    write_file("request.json", request);
    return run(scoped ? "exec \"$0\" --project-root game < request.json" : "exec \"$0\" < request.json");
}

static int validate()
{
    return ctl(true, "{\"contract\":1,\"operation\":\"validate_project\"}");
}

static yyjson_val *result(const char *key)
{
    return yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(reply), "result"), key);
}

static int errors()
{
    return (int)yyjson_get_int(result("errors"));
}

/* The message of the last reply's error. */
static const char *failure()
{
    yyjson_val *error = yyjson_obj_get(yyjson_doc_get_root(reply), "error");
    return yyjson_get_str(yyjson_obj_get(error, "message"));
}

/* The code of the last reply's error. */
static const char *failure_code()
{
    yyjson_val *error = yyjson_obj_get(yyjson_doc_get_root(reply), "error");
    return yyjson_get_str(yyjson_obj_get(error, "code"));
}

/* The message of the diagnostic at `index` in `diagnostics`. */
static const char *message_at(yyjson_val *diagnostics, size_t index)
{
    return yyjson_get_str(yyjson_obj_get(yyjson_arr_get(diagnostics, index), "message"));
}

static const char *first_message(yyjson_val *diagnostics)
{
    return message_at(diagnostics, 0);
}

static av_doc *read_asset(const char *file)
{
    char *text = tool_read(file);
    char error[256];
    av_doc *doc = text ? asset_document_read(text, error, sizeof error) : NULL;
    free(text);
    return doc;
}

static void write_asset(const char *file, const av_doc *doc)
{
    char *text = asset_document_write(av_doc_root(doc));
    assert(text);
    tool_write(file, text);
    free(text);
}

static void set_manifest(const char *key, const char *value)
{
    char manifest[256];
    path(manifest, sizeof manifest, "game/project.chibi");
    av_doc *doc = read_asset(manifest);
    assert(doc);
    av_obj_set(doc, av_doc_root(doc), key, av_str(doc, value));
    write_asset(manifest, doc);
    av_doc_free(doc);
}

static void set_start_scene(const char *scene)
{
    set_manifest("start_scene", scene);
}

static void set_application(const char *key, const char *value)
{
    char manifest[256];
    path(manifest, sizeof manifest, "game/project.chibi");
    av_doc *doc = read_asset(manifest);
    assert(doc);
    av_value *application = av_obj_add_obj(doc, av_doc_root(doc), "application");
    av_obj_add_str(doc, application, key, value);
    write_asset(manifest, doc);
    av_doc_free(doc);
}

static void setup()
{
    strcpy(root, "/tmp/chibi-validate-project-XXXXXX");
    assert(mkdtemp(root));
    path(log_path, sizeof log_path, "output");
    int created = ctl(false, "{\"contract\":1,\"operation\":\"new_project\",\"path\":\"game\",\"name\":\"Game\"}");
    assert(created == 0);
}

static void teardown()
{
    yyjson_doc_free(reply);
    reply = NULL;
    tool_remove_tree(root);
}

TEST_CASE(new_project_is_valid)
{
    REQUIRE(validate() == 0);
    REQUIRE(yyjson_get_bool(result("valid")) && errors() == 0);
    REQUIRE(!yyjson_arr_size(result("project")) && !yyjson_arr_size(result("problems")));
    return true;
}

/* Public family constructors must produce kinds accepted by strict readers. */
TEST_CASE(public_asset_constructors_reach_native_readers)
{
    char folder[256];
    const char *folders[] = { "game/content/materials", "game/content/scenarios" };
    for (size_t i = 0; i < sizeof folders / sizeof *folders; i++) {
        path(folder, sizeof folder, folders[i]);
        REQUIRE(mkdir(folder, 0700) == 0);
    }
    static const char
        *header = "package asset\nimport data \"game:assets\"\n"
                  "pub Build :: () -> byte[] { return data.Encode(";
    char manifest[512];
    int length = snprintf(manifest, sizeof manifest, "%sdata.Project(\"Game\", %d, \"content\", \"scenes/Start.chibi\", [])) }\n", header, CHIBI_PROJECT_VERSION);
    REQUIRE(length > 0 && length < (int)sizeof manifest);
    write_file("game/project.chibi", manifest);
    const char *files[] = { "game/content/scenes/Start.chibi",
                            "game/content/materials/ConstructorSurface.chibi",
                            "game/content/input.chibi",
                            "game/content/scenarios/Constructor.chibi" };
    const char *values[] = {
        "data.Scene(\"Start\", [], [])",
        "data.Graph(\"ConstructorSurface\", \"material\", 3, 2, ["
        "data.GraphDefinition.Named(\"event\", \"\", ["
        "data.GraphNode.Named(1, \"Shader Surface Output\", [])], [], [])], "
        "[])",
        "data.InputMap([data.InputAction.Controller(\"Confirm\", 0, [\"A\"], "
        "false)])",
        "data.Scenario(\"Start\", 2, [data.Keyframe.At(0, ["
        "data.Field.Named(\"controllers\", data.List([data.Object(["
        "data.Field.Named(\"connected\", data.Value.Bool(true)),"
        "data.Field.Named(\"buttons\", "
        "data.List([data.Value.Text(\"A\")]))])]))])], [])"
    };
    for (size_t i = 0; i < sizeof files / sizeof *files; i++) {
        char source[1024];
        length = snprintf(source, sizeof source, "%s%s) }\n", header, values[i]);
        REQUIRE(length > 0 && length < (int)sizeof source);
        write_file(files[i], source);
        path(folder, sizeof folder, files[i]);
        av_doc *doc = read_asset(folder);
        const av_value *value = av_doc_root(doc);
        bool typed = false;
        if (i == 0) {
            const av_value *version = av_obj_get(value, "version");
            typed = av_is_uint(version) && av_get_uint(version) == 1;
        } else if (i == 1) {
            const av_value *version = av_obj_get(value, "version");
            const av_value *counter = av_obj_get(value, "id_counter");
            const av_value *graph = av_arr_get(av_obj_get(value, "graphs"), 0);
            const av_value *id = av_obj_get(av_arr_get(av_obj_get(graph, "nodes"), 0), "id");
            typed = av_is_uint(version) && av_get_uint(version) == 3 &&
                    av_is_uint(counter) && av_get_uint(counter) == 2 &&
                    av_is_uint(id) && av_get_uint(id) == 1;
        } else if (i == 2) {
            const av_value *slot = av_obj_get(av_obj_get(value, "Confirm"), "controller");
            typed = av_is_uint(slot) && av_get_uint(slot) == 0;
        } else {
            const av_value *frames = av_obj_get(value, "frames");
            const av_value *frame = av_obj_get(av_arr_get(av_obj_get(value, "keys"), 0), "frame");
            typed = av_is_uint(frames) && av_get_uint(frames) == 2 &&
                    av_is_uint(frame) && av_get_uint(frame) == 0;
        }
        av_doc_free(doc);
        REQUIRE(typed);
    }
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")) && errors() == 0);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"inspect_graph\",\"kind\":\"material\",\"name\":\"ConstructorSurface\"}") ==
        0
    );
    REQUIRE(yyjson_get_bool(yyjson_obj_get(result("validation"), "valid")));
    yyjson_val *nodes = yyjson_obj_get(result("graph"), "nodes");
    REQUIRE(yyjson_arr_size(nodes) == 1);
    const char *type = yyjson_get_str(yyjson_obj_get(yyjson_arr_get(nodes, 0), "type"));
    REQUIRE(type && !strcmp(type, "Shader Surface Output"));
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"scenario\":\"Constructor\"}") == 0
    );
    REQUIRE(yyjson_is_uint(result("frames")) && yyjson_get_uint(result("frames")) == 2);
    REQUIRE(
        yyjson_is_uint(result("authored_entities")) &&
        !yyjson_get_uint(result("authored_entities"))
    );
    return true;
}

TEST_CASE(stopped_runs_are_reported)
{
    /* Invalid durations must refuse before the child can create its marker. */
    char marker[160];
    path(marker, sizeof marker, "must-not-spawn");
    const char *mark[] = { "/bin/sh", "-c", ": > must-not-spawn", NULL };
    const double invalid[] = { -1, NAN, INFINITY };
    REQUIRE(access(marker, F_OK) == -1 && errno == ENOENT);
    for (int i = 0; i < (int)(sizeof invalid / sizeof *invalid); i++) {
        REQUIRE(tool_run_within(mark, root, log_path, invalid[i]) == 2);
        REQUIRE(access(marker, F_OK) == -1 && errno == ENOENT);
    }
    /* The stopped command's report goes to the test's own stderr. */
    char file[160];
    path(file, sizeof file, "stderr");
    fflush(stderr);
    int saved = dup(STDERR_FILENO),
        captured = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    REQUIRE(saved >= 0 && captured >= 0);
    dup2(captured, STDERR_FILENO);
    close(captured);
    const char *args[] = { "/bin/sh", "-c", "exec sleep 5", NULL };
    int status = tool_run_within(args, root, log_path, 1);
    fflush(stderr);
    dup2(saved, STDERR_FILENO);
    close(saved);
    char *text = tool_read(file);
    bool reported = text &&
                    strstr(text, "stopped after 1 seconds: /bin/sh -c exec sleep 5\n");
    free(text);
    REQUIRE(status == 124 && reported);
    return true;
}

TEST_CASE(ui_style_problems_are_listed)
{
    char folder[256];
    path(folder, sizeof folder, "game/content/ui");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_file("game/content/ui/Hud.rml", "<div class=\"hud\"><p>Score</p></div>\n");
    write_file("game/content/ui/Hud.rcss", ".hud { colr: red; }\n");
    REQUIRE(validate() != 0 && !yyjson_get_bool(result("valid")) && errors() == 1);
    yyjson_val *problem = yyjson_arr_get_first(result("problems"));
    REQUIRE(yyjson_arr_size(result("problems")) == 1);
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(problem, "name")), "Hud"));
    yyjson_val *diagnostics = yyjson_obj_get(problem, "diagnostics");
    REQUIRE(yyjson_arr_size(diagnostics) == 1);
    REQUIRE(
        !strcmp(yyjson_get_str(yyjson_obj_get(yyjson_arr_get_first(diagnostics), "rule_name")), "RMLUI")
    );
    REQUIRE(strstr(first_message(diagnostics), "colr"));
    return true;
}

TEST_CASE(start_scene_outside_scenes_is_parsed)
{
    write_file("broken.chibi", "package asset\npub Build :: () -> byte[] {\n");
    set_start_scene("../../broken.chibi");
    REQUIRE(validate() != 0 && errors() == 1 && !yyjson_arr_size(result("problems")));
    REQUIRE(yyjson_arr_size(result("project")) == 1);
    const char *message = first_message(result("project"));
    REQUIRE(strstr(message, "start scene ../../broken.chibi: ") == message);
    REQUIRE(strstr(message, "line ") && strstr(message, "column "));

    write_file(
        "unknown_prototype.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Out\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Hero\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hero\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    set_start_scene("../../unknown_prototype.chibi");
    REQUIRE(validate() != 0 && errors() == 1);
    REQUIRE(strstr(first_message(result("project")), "Hero"));

    write_file(
        "empty.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Empty\")),\n"
        "            data.Field.Named(\"entities\", data.List([]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    set_start_scene("../../empty.chibi");
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    return true;
}

TEST_CASE(cataloged_start_scene_is_reported_once)
{
    write_file("game/content/scenes/Start.chibi", "package asset\npub Build :: () -> byte[] {\n");
    set_start_scene("../content/scenes/Start.chibi");
    REQUIRE(validate() != 0 && !yyjson_arr_size(result("project")));
    yyjson_val *problem = yyjson_arr_get_first(result("problems"));
    REQUIRE(yyjson_arr_size(result("problems")) == 1);
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(problem, "name")), "Start"));
    REQUIRE(errors() == (int)yyjson_arr_size(yyjson_obj_get(problem, "diagnostics")));
    return true;
}

TEST_CASE(missing_ui_documents_are_reported)
{
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"ui\", data.Value.Text(\"Pause\")),\n"
        "            data.Field.Named(\"entities\", data.List([]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    set_manifest("ui", "Hud");
    REQUIRE(validate() != 0 && errors() == 2);
    REQUIRE(!strcmp(first_message(result("project")), "Unknown UI document 'Hud'"));
    yyjson_val *problem = yyjson_arr_get_first(result("problems"));
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(problem, "name")), "Start"));
    REQUIRE(
        !strcmp(first_message(yyjson_obj_get(problem, "diagnostics")), "Unknown UI document 'Pause'")
    );

    char folder[256], hud[256];
    path(folder, sizeof folder, "game/content/ui");
    REQUIRE(mkdir(folder, 0700) == 0);
    /* Preparation only reads regular files. */
    path(hud, sizeof hud, "game/content/ui/Hud.rml");
    REQUIRE(mkdir(hud, 0700) == 0);
    REQUIRE(validate() != 0 && errors() == 3);
    REQUIRE(!strcmp(first_message(result("project")), "Unknown UI document 'Hud'"));
    REQUIRE(!strcmp(message_at(result("project"), 1), "UI document 'Hud' is not a regular file"));
    REQUIRE(rmdir(hud) == 0);
    /* Preparation follows a link, but the catalog never does, so linked
     * markup is never checked and is refused, named or not. */
    char broken[256], extra[256];
    write_file("broken.rml", "<div>\n");
    path(broken, sizeof broken, "broken.rml");
    REQUIRE(symlink(broken, hud) == 0);
    REQUIRE(validate() != 0 && errors() == 3);
    REQUIRE(!strcmp(first_message(result("project")), "Unknown UI document 'Hud'"));
    REQUIRE(unlink(hud) == 0);
    write_file("game/content/ui/Hud.rml", "<div>Score</div>\n");
    write_file("game/content/ui/Pause.rml", "<div>Paused</div>\n");
    path(extra, sizeof extra, "game/content/ui/Extra.rml");
    REQUIRE(symlink(broken, extra) == 0);
    REQUIRE(validate() != 0 && errors() == 1);
    REQUIRE(!strcmp(first_message(result("project")), "UI document 'Extra' is not a regular file"));
    REQUIRE(unlink(extra) == 0);
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));

    /* A Ui document is looked up the same way, by name only, in the
     * prototype and in the scene that places it. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Menu\"}") == 0
    );
    REQUIRE(remove_authored_prototype("Menu"));
    write_file(
        "game/content/prototypes/Menu.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Menu\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Ui\", data.Object([\n"
        "                    data.Field.Named(\"document\", "
        "data.Value.Text(\"../ui/Hud\"))\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Menu\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Menu\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(validate() != 0 && errors() == 2 && yyjson_arr_size(result("problems")) == 2);
    size_t index, count;
    yyjson_arr_foreach(result("problems"), index, count, problem)
    {
        const char *message = first_message(yyjson_obj_get(problem, "diagnostics"));
        REQUIRE(!strcmp(message, "Unknown UI document '../ui/Hud'"));
    }
    return true;
}

TEST_CASE(catalog_errors_are_reported_once)
{
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Hero\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hero\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(validate() != 0 && errors() == 1);
    yyjson_val *diagnostics = yyjson_obj_get(yyjson_arr_get_first(result("problems")), "diagnostics");
    REQUIRE(yyjson_arr_size(diagnostics) == 1);
    REQUIRE(!strcmp(first_message(diagnostics), "Unknown prototype 'Hero'"));

    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Hero\"}") == 0
    );
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Hero\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hero\")),\n"
        "                    data.Field.Named(\"behaviors\", data.List([\n"
        "                        data.Value.Text(\"Ghost\")\n"
        "                    ]))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(validate() != 0 && errors() == 1);
    diagnostics = yyjson_obj_get(yyjson_arr_get_first(result("problems")), "diagnostics");
    REQUIRE(yyjson_arr_size(diagnostics) == 1);
    REQUIRE(!strcmp(first_message(diagnostics), "Unknown behavior or system 'Ghost'"));

    /* A system is not a behavior, and the scene still gets one error. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"system\",\"name\":\"Ghost\"}") == 0
    );
    REQUIRE(validate() != 0 && errors() == 1);
    diagnostics = yyjson_obj_get(yyjson_arr_get_first(result("problems")), "diagnostics");
    REQUIRE(!strcmp(first_message(diagnostics), "Unknown behavior or system 'Ghost'"));

    /* A source's syntax errors come once, from the catalog, with their lines. */
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Hero\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hero\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"behavior\",\"name\":\"Broken\"}") == 0
    );
    write_file("game/content/behaviors/Broken.chibi", "Broken :: behavior\n\nupdate :: ( {\n");
    REQUIRE(validate() != 0 && errors() == 2);
    diagnostics = yyjson_obj_get(yyjson_arr_get_first(result("problems")), "diagnostics");
    REQUIRE(yyjson_arr_size(diagnostics) == 2);
    yyjson_val *diagnostic;
    size_t index, count;
    yyjson_arr_foreach(diagnostics, index, count, diagnostic)
    {
        REQUIRE(yyjson_obj_get(diagnostic, "line"));
    }

    /* The declaration reports its unknown type, and `Foo(a)`, which names no
     * type, its unknown callable. */
    write_file(
        "game/content/behaviors/Broken.chibi",
        "Broken :: behavior {\n"
        "    a: retain Foo = 0\n"
        "    on Tick :: () {\n"
        "        a = Foo(a)\n"
        "    }\n"
        "}\n"
    );
    REQUIRE(validate() != 0 && errors() == 2);
    diagnostics = yyjson_obj_get(yyjson_arr_get_first(result("problems")), "diagnostics");
    REQUIRE(yyjson_arr_size(diagnostics) == 2);
    REQUIRE(!strcmp(message_at(diagnostics, 0), "Unknown callable 'Foo'"));
    REQUIRE(yyjson_get_int(yyjson_obj_get(yyjson_arr_get(diagnostics, 0), "line")) == 4);
    REQUIRE(!strcmp(message_at(diagnostics, 1), "Unknown type 'Foo'"));
    REQUIRE(yyjson_get_int(yyjson_obj_get(yyjson_arr_get(diagnostics, 1), "line")) == 2);
    return true;
}

TEST_CASE(other_project_versions_are_not_opened)
{
    char manifest[256];
    path(manifest, sizeof manifest, "game/project.chibi");
    char *text = tool_read(manifest);
    char error[256];
    av_doc *doc = text ? asset_document_read(text, error, sizeof error) : NULL;
    REQUIRE(doc);
    int version = av_get_int(av_obj_get(av_doc_root(doc), "version"));
    av_obj_set(doc, av_doc_root(doc), "version", av_sint(doc, version - 1));
    char *old = asset_document_write(av_doc_root(doc));
    av_doc_free(doc);
    free(text);
    REQUIRE(old);
    tool_write(manifest, old);
    REQUIRE(validate() != 0 && errors() == 1);
    const char *message = first_message(result("project"));
    REQUIRE(message && strstr(message, "needs an older build"));
    /* Validation leaves the old project as it was. */
    text = tool_read(manifest);
    bool kept = text && !strcmp(text, old);
    free(text);
    REQUIRE(kept);
    free(old);
    char newer[16384];
    /* A newer build's project needs that build, whatever its fields mean
     * there. */
    snprintf(
        newer,
        sizeof newer,
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.Int(%d)),\n"
        "            data.Field.Named(\"name\", data.Value.UInt(5))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        version + 1
    );
    tool_write(manifest, newer);
    REQUIRE(validate() != 0 && errors() == 1);
    message = first_message(result("project"));
    REQUIRE(message && strstr(message, "needs a newer build"));
    return true;
}

TEST_CASE(application_settings_are_checked)
{
    /* Preparation decodes the icon, so one that is missing or not a PNG
     * fails validation too. */
    set_application("icon", "icon.png");
    REQUIRE(validate() != 0 && errors() == 1);
    const char *message = first_message(result("project"));
    REQUIRE(
        message &&
        strstr(message, "Application icon is not a readable PNG: icon.png: ") == message
    );
    write_file("game/icon.png", "not a png");
    REQUIRE(validate() != 0 && errors() == 1);
    static const unsigned char png[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
        0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x64, 0x60, 0xF8, 0x5F,
        0x0F, 0x00, 0x02, 0x87, 0x01, 0x80, 0xEB, 0x47, 0xBA, 0x92, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
    };
    char icon[256];
    path(icon, sizeof icon, "game/icon.png");
    FILE *file = fopen(icon, "wb");
    REQUIRE(file && fwrite(png, 1, sizeof png, file) == sizeof png && !fclose(file));
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));

    /* The window shows the project name unless a title replaces it. */
    char name[CHIBI_APPLICATION_TEXT_MAX + 2];
    memset(name, 'G', sizeof name - 1);
    name[sizeof name - 1] = '\0';
    set_manifest("name", name);
    REQUIRE(validate() != 0 && errors() == 1);
    REQUIRE(strstr(first_message(result("project")), "set a shorter application title"));
    return true;
}

TEST_CASE(prototype_includes_resolve)
{
    char folder[256];
    path(folder, sizeof folder, "game/content/prototypes");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_file(
        "game/content/prototypes/Hull.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Hull\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Health\", data.Object([\n"
        "                    data.Field.Named(\"current\", "
        "data.Value.UInt(3)),\n"
        "                    data.Field.Named(\"maximum\", "
        "data.Value.UInt(3))\n"
        "                ]))\n"
        "            ])),\n"
        "            data.Field.Named(\"behaviors\", data.List([\n"
        "                data.Value.Text(\"Spin\")\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"behavior\",\"name\":\"Spin\"}") == 0
    );
    write_file(
        "game/content/prototypes/Drone.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Drone\")),\n"
        "            data.Field.Named(\"includes\", data.List([\n"
        "                data.Value.Text(\"Hull\")\n"
        "            ])),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Transform\", data.Object([\n"
        "                    data.Field.Named(\"x\", data.Value.UInt(1)),\n"
        "                    data.Field.Named(\"y\", data.Value.UInt(2))\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Drone\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Drone\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"Drone\"}") ==
        0
    );
    REQUIRE(yyjson_get_int(result("components")) == 2);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") ==
        0
    );
    /* The formatter keeps the include instead of folding Hull's components in. */
    const char *args[] = { program, "--project-root", "game", "--format", NULL };
    REQUIRE(tool_run(args, root, log_path) == 0);
    char drone[256];
    path(drone, sizeof drone, "game/content/prototypes/Drone.chibi");
    char *text = tool_read(drone);
    char error[256];
    av_doc *doc = text ? asset_document_read(text, error, sizeof error) : NULL;
    av_value *value = av_doc_root(doc);
    av_value *includes = av_obj_get(value, "includes");
    const char *include = av_get_str(av_arr_get(includes, 0));
    bool kept = doc && av_arr_size(includes) == 1 && include &&
                !strcmp(include, "Hull") &&
                !av_obj_get(av_obj_get(value, "components"), "Health");
    av_doc_free(doc);
    free(text);
    REQUIRE(kept);
    /* The include is an edge from Drone to Hull, and only Drone's. Hull's
     * behavior stays Hull's reference. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"asset_dependencies\",\"kind\":\"prototype\",\"name\":\"Drone\"}") ==
        0
    );
    yyjson_val *edges = result("edges");
    REQUIRE(yyjson_arr_size(edges) == 1);
    yyjson_val *to = yyjson_obj_get(yyjson_arr_get_first(edges), "to");
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(to, "name")), "Hull"));
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"asset_dependencies\",\"kind\":\"prototype\",\"name\":\"Hull\",\"direction\":\"incoming\"}") ==
        0
    );
    REQUIRE(yyjson_arr_size(result("edges")) == 1);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"asset_dependencies\",\"kind\":\"behavior\",\"name\":\"Spin\",\"direction\":\"incoming\"}") ==
        0
    );
    REQUIRE(yyjson_arr_size(result("edges")) == 1);
    /* One controller first finds Drone's include missing, then sees the Hull
     * it creates, so only its last reply is checked. */
    char hull[256];
    path(hull, sizeof hull, "game/content/prototypes/Hull.chibi");
    REQUIRE(unlink(hull) == 0);
    ctl(true,
        "{\"contract\":1,\"operation\":\"validate_project\"}\n"
        "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\","
        "\"name\":\"Hull\"}\n"
        "{\"contract\":1,\"operation\":\"validate_project\"}");
    REQUIRE(yyjson_get_bool(result("valid")));

    write_file(
        "game/content/prototypes/Drone.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Drone\")),\n"
        "            data.Field.Named(\"includes\", data.List([\n"
        "                data.Value.Text(\"Nope\")\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(validate() != 0 && errors() == 2 && yyjson_arr_size(result("problems")) == 2);
    yyjson_val *problem;
    size_t index, count;
    yyjson_arr_foreach(result("problems"), index, count, problem)
    {
        const char *message = first_message(yyjson_obj_get(problem, "diagnostics"));
        REQUIRE(!strcmp(message, "prototype include Nope is unavailable"));
    }
    /* An include names a prototype, not its file. */
    write_file(
        "game/content/prototypes/Drone.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Drone\")),\n"
        "            data.Field.Named(\"includes\", data.List([\n"
        "                data.Value.Text(\"Hull.chibi\")\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(validate() != 0 && errors() == 2);
    problem = yyjson_arr_get_first(result("problems"));
    const char *message = first_message(yyjson_obj_get(problem, "diagnostics"));
    REQUIRE(!strcmp(message, "prototype include Hull.chibi is unavailable"));
    return true;
}

TEST_CASE(scenarios_load_graph_assets)
{
    /* A scenario registers each graph asset once, in its one world. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"material\",\"name\":\"Glow\"}") == 0
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Dot\"}") == 0
    );
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Dot\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Dot\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") ==
        0
    );
    return true;
}

static const char *const
    SHAPES_LIBRARY = "Shapes :: library {\n"
                     "    Disc :: (X: float = 0.0, Y: float = 0.0) -> (Value: "
                     "float = 0.0) {\n"
                     "        return field_circle(X, Y, 40.0)\n"
                     "    }\n"
                     "}\n";

/* A field that samples Shapes.Disc at its coordinates. */
static const char *const DISC_FIELD =
    "package asset\n"
    "\n"
    "import data \"game:assets\"\n"
    "\n"
    "pub Build :: () -> byte[] {\n"
    "    return data.Encode(\n"
    "        data.Object([\n"
    "            data.Field.Named(\"version\", data.Value.UInt(3)),\n"
    "            data.Field.Named(\"name\", data.Value.Text(\"Disc\")),\n"
    "            data.Field.Named(\"domain\", data.Value.Text(\"field\")),\n"
    "            data.Field.Named(\"id_counter\", data.Value.UInt(21)),\n"
    "            data.Field.Named(\"graphs\", data.List([\n"
    "                data.Object([\n"
    "                    data.Field.Named(\"kind\", "
    "data.Value.Text(\"event\")),\n"
    "                    data.Field.Named(\"name\", data.Value.Text(\"\")),\n"
    "                    data.Field.Named(\"nodes\", data.List([\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"id\", "
    "data.Value.UInt(10)),\n"
    "                            data.Field.Named(\"type\", "
    "data.Value.Text(\"Field X\"))\n"
    "                        ]),\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"id\", "
    "data.Value.UInt(12)),\n"
    "                            data.Field.Named(\"type\", "
    "data.Value.Text(\"Field Y\"))\n"
    "                        ]),\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"id\", "
    "data.Value.UInt(14)),\n"
    "                            data.Field.Named(\"type\", "
    "data.Value.Text(\"Call Pure Function\")),\n"
    "                            data.Field.Named(\"reference_name\", "
    "data.Value.Text(\"Disc\")),\n"
    "                            data.Field.Named(\"reference_asset\", "
    "data.Value.Text(\"Shapes\")),\n"
    "                            data.Field.Named(\"inputs\", data.Object([\n"
    "                                data.Field.Named(\"X\", data.Object([\n"
    "                                    data.Field.Named(\"id\", "
    "data.Value.UInt(15)),\n"
    "                                    data.Field.Named(\"type\", "
    "data.Value.Text(\"float\"))\n"
    "                                ])),\n"
    "                                data.Field.Named(\"Y\", data.Object([\n"
    "                                    data.Field.Named(\"id\", "
    "data.Value.UInt(16)),\n"
    "                                    data.Field.Named(\"type\", "
    "data.Value.Text(\"float\"))\n"
    "                                ]))\n"
    "                            ])),\n"
    "                            data.Field.Named(\"outputs\", data.Object([\n"
    "                                data.Field.Named(\"Value\", "
    "data.Object([\n"
    "                                    data.Field.Named(\"id\", "
    "data.Value.UInt(17)),\n"
    "                                    data.Field.Named(\"type\", "
    "data.Value.Text(\"float\"))\n"
    "                                ]))\n"
    "                            ]))\n"
    "                        ]),\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"id\", "
    "data.Value.UInt(18)),\n"
    "                            data.Field.Named(\"type\", "
    "data.Value.Text(\"Field Output\"))\n"
    "                        ])\n"
    "                    ])),\n"
    "                    data.Field.Named(\"edges\", data.List([\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"from\", "
    "data.Value.Text(\"10.Value\")),\n"
    "                            data.Field.Named(\"to\", "
    "data.Value.Text(\"14.X\"))\n"
    "                        ]),\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"from\", "
    "data.Value.Text(\"12.Value\")),\n"
    "                            data.Field.Named(\"to\", "
    "data.Value.Text(\"14.Y\"))\n"
    "                        ]),\n"
    "                        data.Object([\n"
    "                            data.Field.Named(\"from\", "
    "data.Value.Text(\"14.Value\")),\n"
    "                            data.Field.Named(\"to\", "
    "data.Value.Text(\"18.Value\"))\n"
    "                        ])\n"
    "                    ]))\n"
    "                ])\n"
    "            ]))\n"
    "        ])\n"
    "    )\n"
    "}\n";

TEST_CASE(fields_call_source_libraries)
{
    /* A field calls a .chibi library function by its library and name. */
    const char *folders[] = { "game/content/libraries", "game/content/fields" };
    for (int i = 0; i < 2; i++) {
        char folder[256];
        path(folder, sizeof folder, folders[i]);
        REQUIRE(mkdir(folder, 0700) == 0);
    }
    write_file("game/content/libraries/Shapes.chibi", SHAPES_LIBRARY);
    write_file("game/content/fields/Disc.chibi", DISC_FIELD);
    REQUIRE(validate() == 0 && errors() == 0);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"preview_field\",\"kind\":\"field\",\"name\":\"Disc\",\"width\":16,\"height\":16}") ==
        0
    );
    /* Every sample of the 32-unit square lies inside the 40-unit disc. */
    REQUIRE(yyjson_get_real(result("maximum")) < 0);
    /* Native preparation rejects a graph/source library name collision,
     * even when the graph's filename differs from its declared name. */
    char folder[256], collision[256];
    path(folder, sizeof folder, "game/content/shader-libraries");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_file(
        "game/content/shader-libraries/Different.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(3)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Shapes\")),\n"
        "            data.Field.Named(\"domain\", "
        "data.Value.Text(\"shader-library\")),\n"
        "            data.Field.Named(\"id_counter\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"graphs\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"kind\", "
        "data.Value.Text(\"event\")),\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Functions\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    static const char
        refusal[] = "Native graph declaration shares a source library's name";
    REQUIRE(validate() != 0 && errors() > 0);
    const char *collision_message = first_message(result("project"));
    REQUIRE(collision_message && strstr(collision_message, refusal));
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") !=
        0
    );
    REQUIRE(failure_code() && !strcmp(failure_code(), "validation_failed"));
    path(collision, sizeof collision, "game/content/shader-libraries/Different.chibi");
    REQUIRE(unlink(collision) == 0);
    REQUIRE(validate() == 0 && errors() == 0);
    /* Without the function, the field's call has nothing to resolve. */
    write_file(
        "game/content/libraries/Shapes.chibi",
        "Shapes :: library {\n"
        "}\n"
    );
    REQUIRE(validate() != 0 && errors() == 1);
    yyjson_val *problem = yyjson_arr_get(result("problems"), 0);
    const char *message = first_message(yyjson_obj_get(problem, "diagnostics"));
    REQUIRE(!strcmp(message, "Shapes: missing library function"));
    return true;
}

TEST_CASE(particle_shader_bindings_resolve_library_functions)
{
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"shader-library\",\"name\":\"RainParticles\"}") ==
        0
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Rain\"}") == 0
    );
    char fixture[4096];
    snprintf(fixture, sizeof fixture, "%s/test/fixtures/feature-coverage/content/shader-libraries/RainParticles.chibi", tool_root());
    char *source = tool_read(fixture);
    REQUIRE(source);
    write_file("game/content/shader-libraries/RainParticles.chibi", source);
    free(source);
    REQUIRE(remove_authored_prototype("Rain"));
    write_file(
        "game/content/prototypes/Rain.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Rain\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"ParticleField3D\", data.Object([\n"
        "                    data.Field.Named(\"count\", "
        "data.Value.UInt(128)),\n"
        "                    data.Field.Named(\"seed\", data.Value.UInt(7)),\n"
        "                    data.Field.Named(\"time\", "
        "data.Value.Real(1.0)),\n"
        "                    data.Field.Named(\"additive\", "
        "data.Value.Bool(true))\n"
        "                ])),\n"
        "                data.Field.Named(\"Geometry3D\", data.Object([\n"
        "                    data.Field.Named(\"primitive\", "
        "data.Value.UInt(5)),\n"
        "                    data.Field.Named(\"width\", "
        "data.Value.Real(0.6)),\n"
        "                    data.Field.Named(\"height\", "
        "data.Value.Real(4.0)),\n"
        "                    data.Field.Named(\"depth\", "
        "data.Value.Real(0.6))\n"
        "                ])),\n"
        "                data.Field.Named(\"Shader\", data.Object([\n"
        "                    data.Field.Named(\"material\", "
        "data.Value.Text(\"RainParticles.Fall\"))\n"
        "                ])),\n"
        "                data.Field.Named(\"Transform3D\", data.Object([\n"
        "                    data.Field.Named(\"scale\", data.Object([\n"
        "                        data.Field.Named(\"x\", "
        "data.Value.Real(1.0)),\n"
        "                        data.Field.Named(\"y\", "
        "data.Value.Real(1.0)),\n"
        "                        data.Field.Named(\"z\", "
        "data.Value.Real(1.0))\n"
        "                    ]))\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"presentation\", "
        "data.Value.Text(\"3d\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Rain\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Rain\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    write_file(
        "game/content/prototypes/Rain.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Rain\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"ParticleField3D\", data.Object([\n"
        "                    data.Field.Named(\"count\", "
        "data.Value.UInt(128))\n"
        "                ])),\n"
        "                data.Field.Named(\"Geometry3D\", data.Object([\n"
        "                    data.Field.Named(\"primitive\", "
        "data.Value.UInt(5)),\n"
        "                    data.Field.Named(\"width\", "
        "data.Value.Real(1.0)),\n"
        "                    data.Field.Named(\"height\", "
        "data.Value.Real(1.0)),\n"
        "                    data.Field.Named(\"depth\", "
        "data.Value.Real(1.0))\n"
        "                ])),\n"
        "                data.Field.Named(\"Shader\", data.Object([\n"
        "                    data.Field.Named(\"material\", "
        "data.Value.Text(\"RainParticles.Missing\"))\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(validate() != 0 && errors() > 0);
    return true;
}

static void write_bytes(const char *relative, const unsigned char *bytes, size_t length)
{
    char file[256];
    path(file, sizeof file, relative);
    FILE *out = fopen(file, "wb");
    assert(out);
    size_t written = fwrite(bytes, 1, length, out);
    int closed = fclose(out);
    assert(written == length && closed == 0);
    (void)written, (void)closed;
}

/* What the scenario refuses with, or NULL when it runs. */
static const char *tone_refusal()
{
    if (ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") ==
        0) {
        return NULL;
    }
    yyjson_val *error = yyjson_obj_get(yyjson_doc_get_root(reply), "error");
    return yyjson_get_str(yyjson_obj_get(error, "message"));
}

TEST_CASE(sounds_must_decode)
{
    /* Players decode an Ogg sound before they play it, so a scenario, like
     * preparation and validation, refuses one whose Vorbis audio they cannot
     * decode. */
    static const char refusal[] = "sounds/Tone.ogg: holds no Vorbis audio that "
                                  "players can decode";
    char fixture[4096];
    snprintf(fixture, sizeof fixture, "%s/test/fixtures/sounds/Tone.ogg", tool_root());
    FILE *in = fopen(fixture, "rb");
    assert(in);
    unsigned char tone[4096];
    size_t length = fread(tone, 1, sizeof tone, in);
    fclose(in);
    assert(length > OGG_FIRST_PAGE + 150 && length < sizeof tone);
    char folder[256];
    path(folder, sizeof folder, "game/content/sounds");
    REQUIRE(mkdir(folder, 0700) == 0);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Dot\"}") == 0
    );
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Dot\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Dot\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    write_bytes("game/content/sounds/Tone.ogg", tone, length);
    REQUIRE(!tone_refusal());
    REQUIRE(validate() == 0);

    /* Its first page, then one that ends the stream: whole pages, but no
     * comment or setup header and no audio. */
    unsigned char bare[OGG_FIRST_PAGE + OGG_PAGE_HEADER] = { 0 };
    memcpy(bare, tone, OGG_FIRST_PAGE);
    memcpy(bare + OGG_FIRST_PAGE, "OggS", 4);
    bare[OGG_FIRST_PAGE + 5] = OGG_END_OF_STREAM;
    write_bytes("game/content/sounds/Tone.ogg", bare, sizeof bare);
    const char *message = tone_refusal();
    REQUIRE(message && !strcmp(message, refusal));
    /* Validation lists it too, as a problem of the project, */
    REQUIRE(validate() != 0 && errors() == 1);
    message = first_message(result("project"));
    REQUIRE(message && !strcmp(message, refusal));
    /* even beside another asset's problems, which keep preparation from
     * reaching the sounds. */
    write_file("game/content/prototypes/Broken.chibi", "package asset\npub Build :: () -> byte[] {\n");
    REQUIRE(validate() != 0 && yyjson_arr_size(result("problems")) == 1);
    message = first_message(result("project"));
    REQUIRE(message && !strcmp(message, refusal));
    char broken[256];
    path(broken, sizeof broken, "game/content/prototypes/Broken.chibi");
    REQUIRE(unlink(broken) == 0);

    /* Every header intact, but its one audio page, the last ~200 bytes,
     * garbled. */
    for (size_t i = length - 150; i < length; i++) {
        tone[i] ^= 0xFF;
    }
    write_bytes("game/content/sounds/Tone.ogg", tone, length);
    message = tone_refusal();
    REQUIRE(message && !strcmp(message, refusal));
    return true;
}

/* The smallest WAV players take: one silent 16-bit mono frame at 44.1 kHz. */
static const unsigned char SILENCE[] = {
    'R',  'I',  'F', 'F', 38,   0,    0,    0, 'W', 'A', 'V', 'E',
    'f',  'm',  't', ' ', 16,   0,    0,    0, 1,   0,   1,   0,
    0x44, 0xAC, 0,   0,   0x88, 0x58, 0x01, 0, 2,   0,   16,  0,
    'd',  'a',  't', 'a', 2,    0,    0,    0, 0,   0
};

static void write_silences(int first, int end)
{
    for (int i = first; i < end; i++) {
        char name[64];
        snprintf(name, sizeof name, "game/content/sounds/%04d.wav", i);
        write_bytes(name, SILENCE, sizeof SILENCE);
    }
}

TEST_CASE(resources_are_limited_together)
{
    /* Even a new project carries resources besides its sounds, so sounds
     * that fit the player's resource count on their own overflow it with
     * them, and compiling refuses the project. */
    char folder[256];
    path(folder, sizeof folder, "game/content/sounds");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_silences(0, 1);
    REQUIRE(validate() == 0);
    write_silences(1, PLAYER_MAX_ASSETS - 1);
    REQUIRE(validate() != 0 && errors() == 1);
    const char *message = first_message(result("project"));
    REQUIRE(message && strstr(message, "cannot be read within the resource limit"));
    return true;
}

/* Whether `text` is there and reads `expected`. */
static bool reads(const char *text, const char *expected)
{
    return text && !strcmp(text, expected);
}

/* Whether the catalog lists `member` among the members of the asset found
 * by `query`. */
static bool catalog_lists(const char *query, const char *member)
{
    char request[256];
    snprintf(request, sizeof request, "{\"contract\":1,\"operation\":\"search_assets\",\"query\":\"%s\"}", query);
    if (ctl(true, request) != 0) {
        return false;
    }
    yyjson_val *members = yyjson_obj_get(yyjson_arr_get(result("assets"), 0), "members");
    for (size_t i = 0; i < yyjson_arr_size(members); i++) {
        if (reads(yyjson_get_str(yyjson_obj_get(yyjson_arr_get(members, i), "name")), member)) {
            return true;
        }
    }
    return false;
}

/* Runs chibi_ctl --format on the project, with "--check" to only compare. */
static int format(const char *check)
{
    const char *args[] = { program, "--project-root", "game", "--format", check,
                           NULL };
    return tool_run(args, root, log_path);
}

TEST_CASE(prototypes_are_found_by_declared_name)
{
    /* The player finds a prototype by the name it declares, whatever its
     * file is called, and so does chibi_ctl. A file named after one
     * prototype may even declare another. */
    char folder[256];
    path(folder, sizeof folder, "game/content/prototypes");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_file(
        "game/content/prototypes/hull_base.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Hull\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Health\", data.Object([\n"
        "                    data.Field.Named(\"current\", "
        "data.Value.UInt(3)),\n"
        "                    data.Field.Named(\"maximum\", "
        "data.Value.UInt(3))\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    write_file(
        "game/content/prototypes/Hull.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Drone\")),\n"
        "            data.Field.Named(\"includes\", data.List([\n"
        "                data.Value.Text(\"Hull\")\n"
        "            ])),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Transform\", data.Object([\n"
        "                    data.Field.Named(\"x\", data.Value.UInt(1)),\n"
        "                    data.Field.Named(\"y\", data.Value.UInt(2))\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    /* Nor need a name be one a file could take. */
    write_file(
        "game/content/prototypes/grunt.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", "
        "data.Value.Text(\"enemies/grunt\")),\n"
        "            data.Field.Named(\"includes\", data.List([\n"
        "                data.Value.Text(\"Hull\")\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Hull\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hull\"))\n"
        "                ]),\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Drone\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Drone\"))\n"
        "                ]),\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Grunt\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"enemies/grunt\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"Drone\"}") ==
        0
    );
    REQUIRE(yyjson_get_int(result("components")) == 2);
    REQUIRE(reads(yyjson_get_str(result("path")), "content/prototypes/Hull.chibi"));
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"enemies/grunt\"}") ==
        0
    );
    REQUIRE(reads(yyjson_get_str(result("path")), "content/prototypes/grunt.chibi"));
    /* Or one longer than a file name can be. */
    char long_name[301], text[16384], request[600];
    memset(long_name, 'L', sizeof long_name - 1);
    long_name[sizeof long_name - 1] = '\0';
    snprintf(
        text,
        sizeof text,
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"%s\"))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        long_name
    );
    write_file("game/content/prototypes/long.chibi", text);
    snprintf(request, sizeof request, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"%s\"}", long_name);
    REQUIRE(ctl(true, request) == 0 && reads(yyjson_get_str(result("name")), long_name));
    /* But not one longer than the player takes, which a shorter copy would
     * misname. */
    char huge[PROTOTYPE_NAME_MAX + 2], huge_file[256];
    memset(huge, 'H', sizeof huge - 1);
    huge[sizeof huge - 1] = '\0';
    av_doc *huge_doc = av_doc_new();
    av_value *huge_value = av_obj(huge_doc);
    av_doc_set_root(huge_doc, huge_value);
    av_obj_add_str(huge_doc, huge_value, "name", huge);
    char *huge_text = asset_document_write(huge_value);
    assert(huge_text);
    write_file("game/content/prototypes/huge.chibi", huge_text);
    free(huge_text);
    av_doc_free(huge_doc);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"path\":\"content/prototypes/huge.chibi\"}") !=
        0
    );
    REQUIRE(reads(failure(), "content/prototypes/huge.chibi declares a name longer than 1024 bytes"));
    /* And a new prototype waits until every prototype can be read. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Wing\"}") != 0
    );
    REQUIRE(reads(failure_code(), "validation_failed"));
    path(huge_file, sizeof huge_file, "game/content/prototypes/huge.chibi");
    REQUIRE(unlink(huge_file) == 0);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") ==
        0
    );
    REQUIRE(ctl(true, "{\"contract\":1,\"operation\":\"list_assets\"}") == 0);
    yyjson_val *asset;
    size_t index, count;
    int listed = 0;
    yyjson_arr_foreach(result("assets"), index, count, asset)
    {
        const char *name = yyjson_get_str(yyjson_obj_get(asset, "name"));
        listed += reads(yyjson_get_str(yyjson_obj_get(asset, "path")), "content/prototypes/hull_base.chibi") &&
                  reads(name, "Hull");
    }
    REQUIRE(listed == 1);

    /* A new prototype cannot take a name a file already declares. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Hull\"}") != 0
    );
    REQUIRE(
        reads(failure(), "content/prototypes/hull_base.chibi already declares the prototype 'Hull'")
    );
    /* Two that declare one name are refused, as the player refuses them. */
    write_file(
        "game/content/prototypes/hull_copy.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Hull\"))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"Hull\"}") !=
        0
    );
    REQUIRE(failure() && strstr(failure(), "both declare the prototype 'Hull'"));
    REQUIRE(reads(failure_code(), "asset_conflict"));
    REQUIRE(validate() != 0);
    /* Even when one of them is the file named after it, which a lookup
     * reads first, the project is refused. */
    char copy[256];
    path(copy, sizeof copy, "game/content/prototypes/hull_copy.chibi");
    REQUIRE(unlink(copy) == 0);
    write_file(
        "game/content/prototypes/Drone.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Drone\"))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(validate() != 0 && errors() == 1);
    /* A search that cannot read the folder, or a file in it, fails rather
     * than finding no declaration, since what it missed could declare the
     * name, and new_asset stops there too. Reads refuse a linked folder. */
    char linked[256];
    path(linked, sizeof linked, "game/content/linked");
    REQUIRE(rename(folder, linked) == 0 && symlink("linked", folder) == 0);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"enemies/grunt\"}") !=
        0
    );
    REQUIRE(reads(failure(), "asset directory is not a directory: prototypes"));
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Wing\"}") != 0
    );
    REQUIRE(reads(failure(), "asset directory is not a directory: prototypes"));
    /* Nor can it list a file in the folder's place. */
    REQUIRE(unlink(folder) == 0);
    write_file("game/content/prototypes", "");
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"enemies/grunt\"}") !=
        0
    );
    REQUIRE(reads(failure(), "asset directory is not a directory: prototypes"));
    return true;
}

#if CHIBI_TEST_MODEL_IMPORT
/* Whether the project holds the file `relative`. */
static bool holds(const char *relative)
{
    char file[256];
    path(file, sizeof file, relative);
    return access(file, F_OK) == 0;
}

TEST_CASE(models_import_into_the_project)
{
    /* chibi_ctl names a model for its file, and what it writes is a valid
     * project that --format keeps as it is. */
    static const float uvs[8] = { 0, 1, 0, 0, 1, 0, 1, 1 };
    static const uint8_t grain[4] = { 200, 150, 100, 255 };
    static const char members[] = "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
                                  "\"nodes\":[{\"mesh\":0}],"
                                  "\"meshes\":[{\"primitives\":["
                                  "{" GLTF_FIXTURE_SQUARE ",\"material\":0}]}],"
                                  "\"materials\":[{\"name\":\"Wood\","
                                  "\"pbrMetallicRoughness\":{"
                                  "\"baseColorTexture\":{\"index\":0}}}],"
                                  "\"textures\":[{\"source\":0}]";
    gltf_fixture f = { 0 };
    gltf_fixture_square(&f, uvs);
    gltf_fixture_png(&f, "grain.png", 2, 2, grain);
    char crate[256], barrel[256];
    path(crate, sizeof crate, "Crate.glb");
    path(barrel, sizeof barrel, "old barrel.glb");
    bool written = gltf_fixture_write(&f, crate, members) &&
                   gltf_fixture_write(&f, barrel, members);
    gltf_fixture_free(&f);
    REQUIRE(written);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"import_model\",\"source\":\"Crate.glb\"}") == 0
    );
    REQUIRE(reads(yyjson_get_str(result("prototype")), "Crate"));
    static const char *const files[] = { "content/textures/Crate/grain.png",
                                         "content/materials/Crate_Wood.chibi",
                                         "content/meshes/Crate.mesh",
                                         "content/prototypes/Crate.chibi" };
    REQUIRE(yyjson_arr_size(result("files")) == 4 && yyjson_arr_size(result("warnings")) == 0);
    for (int i = 0; i < 4; i++) {
        REQUIRE(reads(yyjson_get_str(yyjson_arr_get(result("files"), (size_t)i)), files[i]));
    }
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")) && errors() == 0);
    const char *check[] = { program,    "--project-root", "game",
                            "--format", "--check",        NULL };
    REQUIRE(tool_run(check, root, log_path) == 0);

    /* Importing it again would take the prototype's name. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"import_model\",\"source\":\"Crate.glb\"}") != 0
    );
    REQUIRE(reads(failure(), "content/prototypes/Crate.chibi already declares the prototype 'Crate'"));
    REQUIRE(reads(failure_code(), "asset_conflict"));
    /* A file name that is no asset name needs a name of its own, which no
     * prototype may already declare, whatever its file is called. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"import_model\",\"source\":\"old barrel.glb\"}") != 0
    );
    REQUIRE(
        reads(failure(), "Name the import with name; 'old barrel' is not an asset name: Start with a letter; use letters, digits, - and _")
    );
    REQUIRE(reads(failure_code(), "invalid_request"));
    write_file(
        "game/content/prototypes/props.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Barrel\"))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"import_model\",\"source\":\"old barrel.glb\",\"name\":\"Barrel\"}") !=
        0
    );
    REQUIRE(reads(failure(), "content/prototypes/props.chibi already declares the prototype 'Barrel'"));
    REQUIRE(!holds("game/content/meshes/Barrel.mesh") && !holds("game/content/textures/Barrel"));
    /* A model it cannot read writes nothing. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"import_model\",\"source\":\"Drum.glb\",\"name\":\"Drum\"}") !=
        0
    );
    REQUIRE(failure() && !strncmp(failure(), "Cannot read ", strlen("Cannot read ")));
    REQUIRE(reads(failure_code(), "validation_failed"));
    REQUIRE(!holds("game/content/meshes/Drum.mesh"));
    return true;
}
#endif

TEST_CASE(manifest_content_folder_is_used)
{
    char from[256], to[256];
    path(from, sizeof from, "game/content");
    path(to, sizeof to, "game/assets");
    REQUIRE(rename(from, to) == 0);
    set_manifest("content", "assets");
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    /* Only the content folder is read before a request, so the manifest's
     * other problems are still listed with the project's. */
    char manifest_file[256];
    path(manifest_file, sizeof manifest_file, "game/project.chibi");
    char *saved = tool_read(manifest_file);
    assert(saved);
    set_application("fullscreen", "yes");
    REQUIRE(validate() != 0 && errors() == 1);
    const char *listed = first_message(result("project"));
    REQUIRE(listed && strstr(listed, "fullscreen a boolean"));
    tool_write(manifest_file, saved);
    free(saved);
    /* A start scene's content/ path stays relative to the project root, as
     * earlier builds read it, so with the content in assets/ it is missing. */
    set_start_scene("content/scenes/Start.chibi");
    REQUIRE(validate() != 0 && errors() == 1);
    set_start_scene("scenes/Start.chibi");
    ctl(true, "{\"contract\":1,\"operation\":\"status\"}");
    REQUIRE(reads(yyjson_get_str(result("content_root")), "assets"));

    /* Requests and replies keep naming assets by their content/ paths. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Hero\"}") == 0
    );
    REQUIRE(reads(yyjson_get_str(result("path")), "content/prototypes/Hero.chibi"));
    char hero[256];
    path(hero, sizeof hero, "game/assets/prototypes/Hero.chibi");
    REQUIRE(access(hero, F_OK) == 0);
    write_file(
        "game/assets/prototypes/Hero.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Hero\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Transform\", data.Object([\n"
        "                    data.Field.Named(\"x\", data.Value.UInt(1)),\n"
        "                    data.Field.Named(\"y\", data.Value.UInt(2))\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    );\n"
        "}\n"
    );
    write_file(
        "game/assets/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Hero\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hero\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"path\":\"content/prototypes/Hero.chibi\"}") ==
        0
    );
    REQUIRE(yyjson_get_int(result("components")) == 1);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") ==
        0
    );
    /* The input map is formatted where the content is too. Extra spacing
     * ensures the check/write/check sequence exercises a source rewrite. */
    write_file(
        "game/assets/input.chibi",
        "package  asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Dash\", data.Object([\n"
        "                data.Field.Named(\"sources\", data.List([\n"
        "                    data.Value.Text(\"Aim Stick\")\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(format("--check") != 0 && format(NULL) == 0 && format("--check") == 0);
    char map[256];
    path(map, sizeof map, "game/assets/input.chibi");
    char *formatted = tool_read(map);
    bool canonical = reads(
        formatted,
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Dash\", data.Object([\n"
        "                data.Field.Named(\"sources\", data.List([\n"
        "                    data.Value.Text(\"Aim Stick\")\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    free(formatted);
    REQUIRE(canonical);

    char folder[256];
    path(folder, sizeof folder, "game/assets/ui");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_file("game/assets/ui/Hud.rml", "<div class=\"hud\">Score</div>\n");
    ctl(true, "{\"contract\":1,\"operation\":\"find_references\",\"ui_class\":\"hud\"}");
    yyjson_val *use = yyjson_arr_get_first(result("uses"));
    REQUIRE(reads(yyjson_get_str(yyjson_obj_get(use, "path")), "content/ui/Hud.rml"));

    write_file("game/assets/prototypes/Broken.chibi", "package asset\npub Build :: () -> byte[] {\n");
    REQUIRE(validate() != 0 && yyjson_arr_size(result("problems")) == 1);
    yyjson_val *problem = yyjson_arr_get_first(result("problems"));
    REQUIRE(reads(yyjson_get_str(yyjson_obj_get(problem, "path")), "content/prototypes/Broken.chibi"));

    /* A project too old to open still names where its content is. */
    char manifest[256];
    path(manifest, sizeof manifest, "game/project.chibi");
    char *text = tool_read(manifest);
    assert(text);
    char error[256];
    av_doc *doc = asset_document_read(text, error, sizeof error);
    assert(doc);
    av_obj_set(doc, av_doc_root(doc), "version", av_sint(doc, 1));
    char *old = asset_document_write(av_doc_root(doc));
    assert(old);
    av_doc_free(doc);
    free(text);
    tool_write(manifest, old);
    REQUIRE(ctl(true, "{\"contract\":1,\"operation\":\"status\"}") == 0);
    REQUIRE(reads(yyjson_get_str(result("content_root")), "assets"));
    text = tool_read(manifest);
    REQUIRE(text && !strcmp(text, old));
    free(text);
    free(old);
    return true;
}

TEST_CASE(scenes_run_under_their_own_name)
{
    /* A request may name a scene's file, but its scripts see the scene's
     * name, so reloading it restarts the scene that is running. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Dot\"}") == 0
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"behavior\",\"name\":\"Again\"}") == 0
    );
    write_file(
        "game/content/behaviors/Again.chibi",
        "Again :: behavior {\n"
        "    on Tick :: () {\n"
        "        reload_scene()\n"
        "    }\n"
        "}\n"
    );
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Dot\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Dot\")),\n"
        "                    data.Field.Named(\"behaviors\", data.List([\n"
        "                        data.Value.Text(\"Again\")\n"
        "                    ]))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Start.chibi\",\"frames\":2}") ==
        0
    );
    REQUIRE(reads(yyjson_get_str(result("path")), "content/scenes/Start.chibi"));

    /* Only a name that ends with the extension names the file. */
    char start[256], boss[256];
    path(start, sizeof start, "game/content/scenes/Start.chibi");
    path(boss, sizeof boss, "game/content/scenes/Boss.chibi.v2.chibi");
    REQUIRE(rename(start, boss) == 0);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Boss.chibi.v2\",\"frames\":2}") ==
        0
    );
    REQUIRE(reads(yyjson_get_str(result("path")), "content/scenes/Boss.chibi.v2.chibi"));
    return true;
}

/* The x of the GlobalTransform2D on the snapshot entity named `name`, or on
 * the first unnamed one when `name` is NULL. */
static double global_x(const char *name)
{
    yyjson_val *entity, *component;
    size_t i, count, j, components;
    yyjson_arr_foreach(result("entities"), i, count, entity)
    {
        const char *named = yyjson_get_str(yyjson_obj_get(entity, "name"));
        if (name ? !reads(named, name) : named != NULL) {
            continue;
        }
        yyjson_arr_foreach(yyjson_obj_get(entity, "components"), j, components, component)
        {
            if (reads(yyjson_get_str(yyjson_obj_get(component, "name")), "GlobalTransform2D")) {
                yyjson_val *position = yyjson_obj_get(
                    yyjson_arr_get(yyjson_obj_get(component, "fields"), 0),
                    "value"
                );
                return yyjson_get_num(yyjson_obj_get(position, "x"));
            }
        }
    }
    return NAN;
}

TEST_CASE(scene_children_are_placed_by_their_parent)
{
    char folder[256], scene[256];
    path(folder, sizeof folder, "game/content/prototypes");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_file(
        "game/content/prototypes/Hull.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Hull\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Transform\", data.Object([\n"
        "                    data.Field.Named(\"x\", data.Value.UInt(0)),\n"
        "                    data.Field.Named(\"y\", data.Value.UInt(0))\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    /* A root and a child may share a name; siblings may not. Deliberate
     * extra spacing makes the format check/write/check exercise a rewrite. */
    write_file(
        "game/content/scenes/Start.chibi",
        "package  asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Ship\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hull\")),\n"
        "                    data.Field.Named(\"components\", data.Object([\n"
        "                        data.Field.Named(\"Transform\", "
        "data.Object([\n"
        "                            data.Field.Named(\"x\", "
        "data.Value.UInt(100)),\n"
        "                            data.Field.Named(\"y\", "
        "data.Value.UInt(5))\n"
        "                        ]))\n"
        "                    ])),\n"
        "                    data.Field.Named(\"children\", data.List([\n"
        "                        data.Object([\n"
        "                            data.Field.Named(\"name\", "
        "data.Value.Text(\"Turret\")),\n"
        "                            data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hull\")),\n"
        "                            data.Field.Named(\"components\", "
        "data.Object([\n"
        "                                data.Field.Named(\"Transform\", "
        "data.Object([\n"
        "                                    data.Field.Named(\"x\", "
        "data.Value.UInt(10)),\n"
        "                                    data.Field.Named(\"y\", "
        "data.Value.UInt(0))\n"
        "                                ]))\n"
        "                            ]))\n"
        "                        ])\n"
        "                    ]))\n"
        "                ]),\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Turret\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hull\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    REQUIRE(format("--check") != 0 && format(NULL) == 0 && format("--check") == 0);
    path(scene, sizeof scene, "game/content/scenes/Start.chibi");
    char *text = tool_read(scene);
    char error[256];
    av_doc *doc = text ? asset_document_read(text, error, sizeof error) : NULL;
    av_value *ship = av_arr_get(av_obj_get(av_doc_root(doc), "entities"), 0);
    av_value *turret = av_arr_get(av_obj_get(ship, "children"), 0);
    const char *name = av_get_str(av_obj_get(turret, "name"));
    bool nested = doc && name && !strcmp(name, "Turret");
    av_doc_free(doc);
    free(text);
    REQUIRE(nested);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"runtime_snapshot\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") ==
        0
    );
    REQUIRE(global_x("Ship.Turret") == 110 && global_x("Turret") == 0);
    /* The catalog lists a child by its path. */
    REQUIRE(catalog_lists("Start", "Ship.Turret"));
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Ship\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hull\")),\n"
        "                    data.Field.Named(\"children\", data.List([\n"
        "                        data.Object([\n"
        "                            data.Field.Named(\"name\", "
        "data.Value.Text(\"Turret\")),\n"
        "                            data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hull\"))\n"
        "                        ]),\n"
        "                        data.Object([\n"
        "                            data.Field.Named(\"name\", "
        "data.Value.Text(\"Turret\")),\n"
        "                            data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hull\"))\n"
        "                        ])\n"
        "                    ]))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(validate() != 0);
    return true;
}

/* A scene's Joint names its other by path: Swing hangs below Anchor. Hung
 * from the world's origin instead, it would swing toward it. */
TEST_CASE(scene_joints_name_their_other_by_path)
{
    char folder[256];
    path(folder, sizeof folder, "game/content/prototypes");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_file(
        "game/content/prototypes/Post.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Post\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Body\", data.Object([])),\n"
        "                data.Field.Named(\"Collider\", data.Object([\n"
        "                    data.Field.Named(\"category\", "
        "data.Value.UInt(1)),\n"
        "                    data.Field.Named(\"mask\", data.Value.UInt(1)),\n"
        "                    data.Field.Named(\"radius\", "
        "data.Value.Real(4.0))\n"
        "                ])),\n"
        "                data.Field.Named(\"Transform\", data.Object([]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    write_file(
        "game/content/prototypes/Weight.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Weight\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Body\", data.Object([\n"
        "                    data.Field.Named(\"type\", "
        "data.Value.Text(\"Dynamic\"))\n"
        "                ])),\n"
        "                data.Field.Named(\"Collider\", data.Object([\n"
        "                    data.Field.Named(\"category\", "
        "data.Value.UInt(2)),\n"
        "                    data.Field.Named(\"mask\", data.Value.UInt(2)),\n"
        "                    data.Field.Named(\"radius\", "
        "data.Value.Real(4.0))\n"
        "                ])),\n"
        "                data.Field.Named(\"Joint\", data.Object([\n"
        "                    data.Field.Named(\"kind\", "
        "data.Value.Text(\"Distance\"))\n"
        "                ])),\n"
        "                data.Field.Named(\"Orientation\", data.Object([])),\n"
        "                data.Field.Named(\"Transform\", data.Object([])),\n"
        "                data.Field.Named(\"Velocity\", data.Object([]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    write_file(
        "game/content/prototypes/Fall.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Fall\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Gravity\", data.Object([\n"
        "                    data.Field.Named(\"acceleration\", data.Object([\n"
        "                        data.Field.Named(\"y\", "
        "data.Value.Real(-320.0))\n"
        "                    ]))\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    const char *scene =
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Anchor\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Post\")),\n"
        "                    data.Field.Named(\"components\", data.Object([\n"
        "                        data.Field.Named(\"Transform\", "
        "data.Object([\n"
        "                            data.Field.Named(\"x\", "
        "data.Value.Int(-200)),\n"
        "                            data.Field.Named(\"y\", "
        "data.Value.UInt(0))\n"
        "                        ]))\n"
        "                    ]))\n"
        "                ]),\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Swing\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Weight\")),\n"
        "                    data.Field.Named(\"components\", data.Object([\n"
        "                        data.Field.Named(\"Joint\", data.Object([\n"
        "                            data.Field.Named(\"other\", "
        "data.Value.Text(\"%s\"))\n"
        "                        ])),\n"
        "                        data.Field.Named(\"Transform\", "
        "data.Object([\n"
        "                            data.Field.Named(\"x\", "
        "data.Value.Int(-200)),\n"
        "                            data.Field.Named(\"y\", "
        "data.Value.Int(-50))\n"
        "                        ]))\n"
        "                    ]))\n"
        "                ]),\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Fall\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Fall\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n";
    char text[16384];
    snprintf(text, sizeof text, scene, "Anchor");
    write_file("game/content/scenes/Start.chibi", text);
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"runtime_snapshot\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":60}") ==
        0
    );
    REQUIRE(fabs(global_x("Swing") + 200) < 1);
    /* A path no entity has is a problem, and the scenario refuses it. */
    snprintf(text, sizeof text, scene, "Anchors");
    write_file("game/content/scenes/Start.chibi", text);
    REQUIRE(validate() != 0 && errors() == 1);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"run_scenario\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") !=
        0
    );
    REQUIRE(strstr(failure(), "Swing: Joint other names Anchors, but no entity has that path"));
    return true;
}

/* A prototype's children come with each instance of it, placed by it. */
TEST_CASE(prototype_children_come_with_their_instance)
{
    char folder[256], ship[256];
    path(folder, sizeof folder, "game/content/prototypes");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_file(
        "game/content/prototypes/Hull.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Hull\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Transform\", data.Object([\n"
        "                    data.Field.Named(\"x\", data.Value.UInt(0)),\n"
        "                    data.Field.Named(\"y\", data.Value.UInt(0))\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    write_file(
        "game/content/prototypes/Ship.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Ship\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Transform\", data.Object([\n"
        "                    data.Field.Named(\"x\", data.Value.UInt(0)),\n"
        "                    data.Field.Named(\"y\", data.Value.UInt(0))\n"
        "                ]))\n"
        "            ])),\n"
        "            data.Field.Named(\"children\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Gun\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hull\")),\n"
        "                    data.Field.Named(\"components\", data.Object([\n"
        "                        data.Field.Named(\"Transform\", "
        "data.Object([\n"
        "                            data.Field.Named(\"x\", "
        "data.Value.UInt(4)),\n"
        "                            data.Field.Named(\"y\", "
        "data.Value.UInt(0))\n"
        "                        ]))\n"
        "                    ]))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Flagship\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Ship\")),\n"
        "                    data.Field.Named(\"components\", data.Object([\n"
        "                        data.Field.Named(\"Transform\", "
        "data.Object([\n"
        "                            data.Field.Named(\"x\", "
        "data.Value.UInt(100)),\n"
        "                            data.Field.Named(\"y\", "
        "data.Value.UInt(0))\n"
        "                        ]))\n"
        "                    ]))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    REQUIRE(format(NULL) == 0 && format("--check") == 0);
    path(ship, sizeof ship, "game/content/prototypes/Ship.chibi");
    char *text = tool_read(ship);
    /* Formatting preserves the authored unsigned x=4 and y=0 on the child. */
    char error[256];
    av_doc *doc = text ? asset_document_read(text, error, sizeof error) : NULL;
    av_value *gun = av_arr_get(av_obj_get(av_doc_root(doc), "children"), 0);
    const char *name = av_get_str(av_obj_get(gun, "name"));
    av_value *transform = av_obj_get(av_obj_get(gun, "components"), "Transform");
    av_value *x = av_obj_get(transform, "x"), *y = av_obj_get(transform, "y");
    bool nested = doc && name && !strcmp(name, "Gun") && av_is_obj(transform) &&
                  av_obj_size(transform) == 2 && av_is_uint(x) &&
                  av_get_uint(x) == 4 && av_is_uint(y) && av_get_uint(y) == 0;
    av_doc_free(doc);
    free(text);
    REQUIRE(nested);
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"runtime_snapshot\",\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1}") ==
        0
    );
    /* Snapshots name only the scene's own objects. */
    REQUIRE(yyjson_arr_size(result("entities")) == 2);
    REQUIRE(global_x("Flagship") == 100 && global_x(NULL) == 104);
    REQUIRE(catalog_lists("Ship", "Gun"));
    /* A child is an edge to the prototype it uses. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"asset_dependencies\",\"kind\":\"prototype\",\"name\":\"Ship\"}") ==
        0
    );
    yyjson_val *edges = result("edges");
    yyjson_val *to = yyjson_obj_get(yyjson_arr_get_first(edges), "to");
    REQUIRE(
        yyjson_arr_size(edges) == 1 &&
        reads(yyjson_get_str(yyjson_obj_get(to, "name")), "Hull")
    );
    /* A child's own behaviors are checked with the scene that spawns it. */
    write_file(
        "game/content/prototypes/Ship.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Ship\")),\n"
        "            data.Field.Named(\"children\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Gun\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hull\")),\n"
        "                    data.Field.Named(\"behaviors\", data.List([\n"
        "                        data.Value.Text(\"Missing\")\n"
        "                    ]))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"scene\",\"name\":\"Start\"}") !=
        0
    );
    const char *missing = first_message(result("diagnostics"));
    REQUIRE(missing && strstr(missing, "Missing"));
    /* So are the bindings a child's overrides add. */
    write_file(
        "game/content/prototypes/Ship.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Ship\")),\n"
        "            data.Field.Named(\"children\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Gun\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Hull\")),\n"
        "                    data.Field.Named(\"components\", data.Object([\n"
        "                        data.Field.Named(\"Ui\", data.Object([\n"
        "                            data.Field.Named(\"document\", "
        "data.Value.Text(\"Nowhere\"))\n"
        "                        ]))\n"
        "                    ]))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"prototype\",\"name\":\"Ship\"}") !=
        0
    );
    missing = first_message(result("diagnostics"));
    REQUIRE(missing && !strcmp(missing, "Unknown UI document 'Nowhere'"));
    write_file(
        "game/content/prototypes/Ship.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Ship\")),\n"
        "            data.Field.Named(\"children\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Again\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Ship\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(validate() != 0);
    return true;
}

/* Prototypes `prefix`0 to `prefix`<levels - 1>, each with two children that
 * use the next, so loading `prefix`0 reads 2^levels - 2 other documents. */
static void write_fans(const char *prefix, int levels)
{
    for (int i = 0; i < levels; i++) {
        char file[128], name[128];
        snprintf(file, sizeof file, "game/content/prototypes/%s%d.chibi", prefix, i);
        snprintf(name, sizeof name, "%s%d", prefix, i);
        av_doc *doc = av_doc_new();
        av_value *value = av_obj(doc);
        av_doc_set_root(doc, value);
        av_obj_add_str(doc, value, "name", name);
        if (i + 1 < levels) {
            av_value *children = av_obj_add_arr(doc, value, "children");
            snprintf(name, sizeof name, "%s%d", prefix, i + 1);
            const char *names[] = { "L", "R" };
            for (int j = 0; j < 2; j++) {
                av_value *child = av_arr_add_obj(doc, children);
                av_obj_add_str(doc, child, "name", names[j]);
                av_obj_add_str(doc, child, "prototype", name);
            }
        }
        char *text = asset_document_write(value);
        assert(text);
        write_file(file, text);
        free(text);
        av_doc_free(doc);
    }
}

TEST_CASE(prototype_expansion_is_shared)
{
    /* Each load stays under its own limit, but the project's loads together
     * read more than one load may, so preparing it stops. Fan0 to Fan10 read
     * 4083 documents, and each Pair's two Fan0 children read 4094 more. */
    char folder[256];
    path(folder, sizeof folder, "game/content/prototypes");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_fans("Fan", 11);
    for (int i = 0; i < 4; i++) {
        char file[64], text[16384];
        snprintf(file, sizeof file, "game/content/prototypes/Pair%d.chibi", i);
        snprintf(
            text,
            sizeof text,
            "package asset\n"
            "\n"
            "import data \"game:assets\"\n"
            "\n"
            "pub Build :: () -> byte[] {\n"
            "    return data.Encode(\n"
            "        data.Object([\n"
            "            data.Field.Named(\"name\", "
            "data.Value.Text(\"Pair%d\")),\n"
            "            data.Field.Named(\"children\", data.List([\n"
            "                data.Object([\n"
            "                    data.Field.Named(\"name\", "
            "data.Value.Text(\"L\")),\n"
            "                    data.Field.Named(\"prototype\", "
            "data.Value.Text(\"Fan0\"))\n"
            "                ]),\n"
            "                data.Object([\n"
            "                    data.Field.Named(\"name\", "
            "data.Value.Text(\"R\")),\n"
            "                    data.Field.Named(\"prototype\", "
            "data.Value.Text(\"Fan0\"))\n"
            "                ])\n"
            "            ]))\n"
            "        ])\n"
            "    )\n"
            "}\n",
            i
        );
        write_file(file, text);
    }
    REQUIRE(validate() != 0);
    REQUIRE(errors() == 1);
    const char *message = first_message(result("project"));
    REQUIRE(message && strstr(message, "through their includes and children"));
    return true;
}

TEST_CASE(scene_instances_share_the_expansion)
{
    /* Each instance loads its prototype again, so a scene's instances share
     * one budget too. An instance of Bush0 reads 512 documents: its
     * overlay, Bush0 and Bush0's 510. */
    char folder[256];
    path(folder, sizeof folder, "game/content/prototypes");
    REQUIRE(mkdir(folder, 0700) == 0);
    write_fans("Bush", 9);
    REQUIRE(validate() == 0);
    av_doc *doc = av_doc_new();
    av_value *scene = av_obj(doc);
    av_doc_set_root(doc, scene);
    av_obj_add_uint(doc, scene, "version", 1);
    av_obj_add_str(doc, scene, "name", "Start");
    av_value *entities = av_obj_add_arr(doc, scene, "entities");
    for (int i = 0; i < PROTOTYPE_EXPANSION_MAX / 512; i++) {
        char name[32];
        snprintf(name, sizeof name, "B%d", i);
        av_value *entity = av_arr_add_obj(doc, entities);
        av_obj_add_str(doc, entity, "name", name);
        av_obj_add_str(doc, entity, "prototype", "Bush0");
    }
    char *text = asset_document_write(scene);
    assert(text);
    write_file("game/content/scenes/Start.chibi", text);
    free(text);
    av_doc_free(doc);
    REQUIRE(validate() != 0);
    const char *message = first_message(result("project"));
    REQUIRE(message && strstr(message, "through their includes and children"));
    return true;
}

/* The x a Tick wrote to the scene's only Transform in the last snapshot. */
static double snapshot_x()
{
    yyjson_val *entities = result("entities");
    for (size_t i = 0; i < yyjson_arr_size(entities); i++) {
        yyjson_val *components = yyjson_obj_get(yyjson_arr_get(entities, i), "components");
        for (size_t j = 0; j < yyjson_arr_size(components); j++) {
            yyjson_val *component = yyjson_arr_get(components, j);
            if (reads(yyjson_get_str(yyjson_obj_get(component, "name")), "Transform")) {
                yyjson_val *x = yyjson_arr_get(yyjson_obj_get(component, "fields"), 0);
                assert(reads(yyjson_get_str(yyjson_obj_get(x, "name")), "x"));
                return yyjson_get_num(yyjson_obj_get(x, "value"));
            }
        }
    }
    assert(false);
    return 0;
}

TEST_CASE(components_are_declared_in_chibi_sources)
{
    char folder[256], source[256];
    path(folder, sizeof folder, "game/content/components");
    REQUIRE(mkdir(folder, 0700) == 0);
    path(folder, sizeof folder, "game/content/systems");
    REQUIRE(mkdir(folder, 0700) == 0 || errno == EEXIST);
    write_file(
        "game/content/components/Charge.chibi",
        "// Stored energy.\n"
        "Charge :: component{level : 0 .. 100 charge\n"
        "rate:charge / s}\n"
    );
    write_file(
        "game/content/systems/Recharge.chibi",
        "Recharge :: system(charge: Charge) on Tick :: (dt: float s) {\n"
        "    charge.level += charge.rate * dt\n"
        "}\n"
    );
    REQUIRE(validate() == 0 && yyjson_get_bool(result("valid")));
    /* chibi_ctl formats component sources with the rest of the content. */
    REQUIRE(format("--check") != 0 && format(NULL) == 0 && format("--check") == 0);
    path(source, sizeof source, "game/content/components/Charge.chibi");
    char *formatted = tool_read(source);
    bool canonical = reads(
        formatted,
        "// Stored energy.\n"
        "Charge :: component {\n"
        "    level: 0..100 charge\n"
        "    rate: charge/s\n"
        "}\n"
    );
    free(formatted);
    REQUIRE(canonical);
    /* Formatting never writes through a link out of the project. */
    char outside[256], link_path[256];
    path(outside, sizeof outside, "Outside.chibi");
    path(link_path, sizeof link_path, "game/content/components/Outside.chibi");
    const char *loose = "Outside :: component{x:float}\n";
    write_file("Outside.chibi", loose);
    REQUIRE(symlink(outside, link_path) == 0);
    format(NULL);
    char *kept = tool_read(outside);
    bool untouched = reads(kept, loose);
    free(kept);
    REQUIRE(untouched && unlink(link_path) == 0);
    /* A field's mistake is reported where it is. */
    write_file(
        "game/content/components/Charge.chibi",
        "Charge :: component {\n"
        "    level: flaot\n"
        "}\n"
    );
    REQUIRE(validate() != 0);
    REQUIRE(strstr(failure(), "Charge.chibi: 2:12: Unknown type or unit 'flaot'"));
    write_file(
        "game/content/components/Charge.chibi",
        "Charge :: component {\n"
        "    level: float\n"
        "    job: task\n"
        "}\n"
    );
    REQUIRE(validate() != 0);
    REQUIRE(strstr(failure(), "Charge.chibi: 3:5: A component field cannot hold a task"));
    /* A project may not declare a component the engine already has. */
    write_file(
        "game/content/components/Charge.chibi",
        "Charge :: component {\n"
        "    level: float\n"
        "}\n"
    );
    write_file(
        "game/content/components/Transform.chibi",
        "Transform :: component {\n"
        "    x: float\n"
        "}\n"
    );
    REQUIRE(validate() != 0);
    REQUIRE(strstr(failure(), "Transform.chibi: the name is already an engine type"));
    return true;
}

TEST_CASE(unit_mismatches_are_errors)
{
    char folder[256];
    path(folder, sizeof folder, "game/content/components");
    REQUIRE(mkdir(folder, 0700) == 0 || errno == EEXIST);
    path(folder, sizeof folder, "game/content/systems");
    REQUIRE(mkdir(folder, 0700) == 0 || errno == EEXIST);
    write_file(
        "game/content/components/Charge.chibi",
        "Charge :: component {\n"
        "    level: 0..100 charge\n"
        "    rate: charge/s\n"
        "}\n"
    );
    /* Declared units, the engine's reflected units and dt's seconds. */
    write_file(
        "game/content/systems/Measure.chibi",
        "Measure :: system(charge: Charge, movement: Movement, collider: "
        "Collider) on Tick :: (dt: float s) {\n"
        "    charge.level += charge.rate * dt\n"
        "    charge.level += charge.rate\n"
        "    collider.radius = movement.speed\n"
        "    collider.radius += dt\n"
        "}\n"
    );
    /* Proven unit mismatches prevent the project from running. */
    REQUIRE(validate() != 0 && !yyjson_get_bool(result("valid")) && errors() == 3);
    REQUIRE(yyjson_get_int(result("warnings")) == 0);
    yyjson_val *problem = yyjson_arr_get_first(result("problems"));
    yyjson_val *diagnostics = yyjson_obj_get(problem, "diagnostics");
    REQUIRE(yyjson_arr_size(diagnostics) == 3);
    yyjson_val *first = yyjson_arr_get(diagnostics, 0);
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(first, "severity")), "error"));
    REQUIRE(!strcmp(message_at(diagnostics, 0), "Units differ: charge += charge/s"));
    REQUIRE(yyjson_get_int(yyjson_obj_get(first, "line")) == 3);
    REQUIRE(yyjson_get_int(yyjson_obj_get(first, "column")) == 5);
    REQUIRE(!strcmp(message_at(diagnostics, 1), "Units differ: u = u/s"));
    REQUIRE(!strcmp(message_at(diagnostics, 2), "Units differ: u += s"));
    /* Validating one source reports the same errors and locations. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"validate_asset\",\"kind\":\"system\",\"name\":\"Measure\"}") !=
        0
    );
    REQUIRE(errors() == 3 && yyjson_get_int(result("warnings")) == 0);
    diagnostics = result("diagnostics");
    REQUIRE(yyjson_get_int(yyjson_obj_get(yyjson_arr_get(diagnostics, 0), "severity")) == 1);
    first = yyjson_arr_get(diagnostics, 0);
    REQUIRE(yyjson_get_int(yyjson_obj_get(first, "line")) == 3);
    REQUIRE(yyjson_get_int(yyjson_obj_get(first, "column")) == 5);
    /* One chibi_ctl sees a unit change that leaves the layout as it was. */
    write_file(
        "Charge.chibi",
        "Charge :: component {\n"
        "    level: 0..100 charge\n"
        "    rate: 0..10 charge\n"
        "}\n"
    );
    write_file("request.json", "{\"contract\":1,\"operation\":\"validate_project\"}\n");
    REQUIRE(
        run("{ cat request.json; until grep -q contract \"$1\"; do sleep 0.02; "
            "done; "
            "cp Charge.chibi game/content/components/Charge.chibi; cat "
            "request.json; } | "
            "exec \"$0\" --project-root game") != 0
    );
    diagnostics = yyjson_obj_get(yyjson_arr_get_first(result("problems")), "diagnostics");
    REQUIRE(!strcmp(message_at(diagnostics, 0), "Units differ: charge += charge·s"));
    return true;
}

TEST_CASE(scenarios_hold_input_actions)
{
    /* A scenario's controls reach the input map of the engine that plays it. */
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"prototype\",\"name\":\"Dot\"}") == 0
    );
    REQUIRE(
        ctl(true, "{\"contract\":1,\"operation\":\"new_asset\",\"kind\":\"behavior\",\"name\":\"Jumper\"}") == 0
    );
    write_file(
        "game/content/input.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Jump\", data.Object([\n"
        "                data.Field.Named(\"sources\", data.List([\n"
        "                    data.Value.Text(\"Mouse Left\")\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(remove_authored_prototype("Dot"));
    write_file(
        "game/content/prototypes/Dot.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Dot\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Transform\", data.Object([]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    write_file(
        "game/content/behaviors/Jumper.chibi",
        "Jumper :: behavior {\n"
        "    on Tick :: () {\n"
        "        if input.Jump.held {\n"
        "            self.Transform = vec2(7, 0)\n"
        "        }\n"
        "    }\n"
        "}\n"
    );
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Dot\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Dot\")),\n"
        "                    data.Field.Named(\"behaviors\", data.List([\n"
        "                        data.Value.Text(\"Jumper\")\n"
        "                    ]))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    /* 1 is CONTROL_MOUSE_LEFT, the first Control. */
    const char *request = "{\"contract\":1,\"operation\":\"runtime_snapshot\","
                          "\"kind\":\"scene\",\"name\":\"Start\",\"frames\":2,"
                          "\"input_timeline\":[{\"frame\":0,\"input\":"
                          "{\"controls\":[%s]}}]}";
    char held[256], released[256];
    snprintf(held, sizeof held, request, "1");
    snprintf(released, sizeof released, request, "");
    REQUIRE(ctl(true, held) == 0 && snapshot_x() == 7);
    REQUIRE(ctl(true, released) == 0 && snapshot_x() == 0);
    return true;
}

TEST_CASE(scenarios_hold_controller_slots)
{
    const char *folders[] = { "game/content/prototypes", "game/content/behaviors" };
    for (int i = 0; i < 2; i++) {
        char directory[256];
        path(directory, sizeof directory, folders[i]);
        REQUIRE(mkdir(directory, 0755) == 0 || errno == EEXIST);
    }
    write_file(
        "game/content/input.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Left\", data.Object([\n"
        "                data.Field.Named(\"sources\", data.List([\n"
        "                    data.Value.Text(\"Z\")\n"
        "                ])),\n"
        "                data.Field.Named(\"controller\", "
        "data.Value.UInt(0)),\n"
        "                data.Field.Named(\"buttons\", data.List([\n"
        "                    data.Value.Text(\"Dpad Left\")\n"
        "                ]))\n"
        "            ])),\n"
        "            data.Field.Named(\"Confirm\", data.Object([\n"
        "                data.Field.Named(\"controller\", "
        "data.Value.UInt(1)),\n"
        "                data.Field.Named(\"buttons\", data.List([\n"
        "                    data.Value.Text(\"A\")\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    write_file(
        "game/content/prototypes/Dot.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Dot\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Transform\", data.Object([]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    write_file(
        "game/content/behaviors/Controls.chibi",
        "Controls :: behavior {\n"
        " on Tick :: () {\n"
        " x := 0\n"
        " if input.Left.held { x += 1 }\n"
        " if input.Confirm.held { x += 10 }\n"
        " if controller_connected(0) { x += 100 }\n"
        " if controller_connected(1) { x += 1000 }\n"
        " self.Transform = vec2(x, 0)\n"
        " }\n"
        "}\n"
    );
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Dot\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Dot\")),\n"
        "                    data.Field.Named(\"behaviors\", data.List([\n"
        "                        data.Value.Text(\"Controls\")\n"
        "                    ]))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    /* A sparse sample holds both devices across fixed steps. */
    const char *request = "{\"contract\":1,\"operation\":\"runtime_snapshot\","
                          "\"kind\":\"scene\",\"name\":\"Start\",\"frames\":3,"
                          "\"input_timeline\":[{\"frame\":0,\"input\":{"
                          "\"controllers\":["
                          "{\"connected\":true,\"buttons\":[\"Dpad Left\"]},"
                          "{\"connected\":true,\"buttons\":[1]}]}}]}";
    REQUIRE(ctl(true, request) == 0 && snapshot_x() == 1111);
    /* All slots omitted from an explicit array disconnect; keyboard persists
     * when only controllers change, then releases while slot one rejoins. */
    request = "{\"contract\":1,\"operation\":\"runtime_snapshot\","
              "\"kind\":\"scene\",\"name\":\"Start\",\"frames\":3,"
              "\"input_timeline\":["
              "{\"frame\":0,\"input\":{\"controls\":[40],\"controllers\":[]}},"
              "{\"frame\":1,\"input\":{\"controllers\":["
              "{\"connected\":false},{\"connected\":true,\"buttons\":[\"Dpad "
              "Left\"]}]}},"
              "{\"frame\":2,\"input\":{\"controls\":[],\"controllers\":["
              "{\"connected\":false},{\"connected\":true,\"buttons\":[\"A\"]}]}"
              "}]}";
    REQUIRE(ctl(true, request) == 0 && snapshot_x() == 1010);
    const char *invalid[] = {
        "[{\"connected\":false,\"buttons\":[\"A\"]}]",
        "[{\"connected\":true,\"buttons\":[\"Count\"]}]",
        ("[{\"connected\":false},{\"connected\":false},{\"connected\":false},"
         "{\"connected\":false},{\"connected\":false}]"),
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        char bad[1024];
        snprintf(
            bad,
            sizeof bad,
            "{\"contract\":1,\"operation\":\"runtime_snapshot\","
            "\"kind\":\"scene\",\"name\":\"Start\",\"frames\":1,"
            "\"inputs\":[{\"controllers\":%s}]}",
            invalid[i]
        );
        REQUIRE(ctl(true, bad) != 0 && strstr(failure(), "controller"));
    }
    return true;
}

TEST_CASE(scenarios_hold_pointer_rays_and_reject_external_urls)
{
    const char *folders[] = { "game/content/prototypes", "game/content/behaviors" };
    for (int i = 0; i < 2; i++) {
        char directory[256];
        path(directory, sizeof directory, folders[i]);
        REQUIRE(mkdir(directory, 0755) == 0 || errno == EEXIST);
    }
    write_file(
        "game/content/prototypes/Dot.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Dot\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Transform\", data.Object([]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    write_file(
        "game/content/behaviors/Pointer.chibi",
        "Pointer :: behavior { on Tick :: () {\n"
        " ray := get_pointer_ray()\n"
        " x := -1.0\n"
        " if ray.Active { x = ray.Origin.x + ray.Direction.x }\n"
        " request := await open_url(\"https://example.org/\")\n"
        " if request.Accepted { x = 10000.0 }\n"
        " self.Transform = vec2(x, 0)\n"
        " } }\n"
    );
    write_file(
        "game/content/scenes/Start.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Start\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Dot\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Dot\")),\n"
        "                    data.Field.Named(\"behaviors\", data.List([\n"
        "                        data.Value.Text(\"Pointer\")\n"
        "                    ]))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    const char *base = "{\"contract\":1,\"operation\":\"runtime_snapshot\","
                       "\"kind\":\"scene\",\"name\":\"Start\",\"frames\":3,"
                       "\"input_timeline\":[{\"frame\":0,\"input\":{"
                       "\"pointer\":%s}}%s]}";
    const char *ray = "{\"origin\":{\"x\":6,\"y\":2,\"z\":10},"
                      "\"direction\":{\"x\":1,\"y\":0,\"z\":-1}}";
    char request[768];
    snprintf(request, sizeof request, base, ray, "");
    REQUIRE(ctl(true, request) == 0 && snapshot_x() == 7);
    snprintf(request, sizeof request, base, ray, ",{\"frame\":2,\"input\":{\"pointer\":null}}");
    REQUIRE(ctl(true, request) == 0 && snapshot_x() == -1);
    const char *invalid[] = {
        "{\"origin\":{\"x\":0,\"y\":0,\"z\":0},\"direction\":{\"x\":0,\"y\":0,"
        "\"z\":0}}",
        "{\"origin\":{\"x\":0,\"y\":0},\"direction\":{\"x\":0,\"y\":0,\"z\":-1}"
        "}",
        "{\"origin\":{\"x\":0,\"y\":0,\"z\":0},\"direction\":{\"x\":0,\"y\":0,"
        "\"z\":1e100}}",
    };
    for (int i = 0; i < 3; i++) {
        snprintf(request, sizeof request, base, invalid[i], "");
        REQUIRE(ctl(true, request) != 0 && strstr(failure(), "pointer"));
    }
    return true;
}

void test_tool_validate_units(const char *binary)
{
    bool found = realpath(binary, program);
    assert(found);
    (void)found;
    pu_setup(setup, teardown);
    RUN_TEST_CASE(unit_mismatches_are_errors);
    pu_clear_setup();
}

void test_tool_validate_project(const char *binary)
{
    /* The requests run from the project's temporary folder. */
    bool found = realpath(binary, program);
    assert(found);
    (void)found;
    pu_setup(setup, teardown);
    RUN_TEST_CASE(new_project_is_valid);
    RUN_TEST_CASE(public_asset_constructors_reach_native_readers);
    RUN_TEST_CASE(stopped_runs_are_reported);
    RUN_TEST_CASE(ui_style_problems_are_listed);
    RUN_TEST_CASE(start_scene_outside_scenes_is_parsed);
    RUN_TEST_CASE(cataloged_start_scene_is_reported_once);
    RUN_TEST_CASE(missing_ui_documents_are_reported);
    RUN_TEST_CASE(catalog_errors_are_reported_once);
    RUN_TEST_CASE(other_project_versions_are_not_opened);
    RUN_TEST_CASE(application_settings_are_checked);
    RUN_TEST_CASE(prototype_includes_resolve);
    RUN_TEST_CASE(prototypes_are_found_by_declared_name);
#if CHIBI_TEST_MODEL_IMPORT
    RUN_TEST_CASE(models_import_into_the_project);
#endif
    RUN_TEST_CASE(scenarios_load_graph_assets);
    RUN_TEST_CASE(particle_shader_bindings_resolve_library_functions);
    RUN_TEST_CASE(fields_call_source_libraries);
    RUN_TEST_CASE(sounds_must_decode);
    RUN_TEST_CASE(resources_are_limited_together);
    RUN_TEST_CASE(manifest_content_folder_is_used);
    RUN_TEST_CASE(scenes_run_under_their_own_name);
    RUN_TEST_CASE(scene_children_are_placed_by_their_parent);
    RUN_TEST_CASE(prototype_children_come_with_their_instance);
    RUN_TEST_CASE(scene_joints_name_their_other_by_path);
    RUN_TEST_CASE(scenarios_hold_input_actions);
    RUN_TEST_CASE(scenarios_hold_pointer_rays_and_reject_external_urls);
    RUN_TEST_CASE(scenarios_hold_controller_slots);
    RUN_TEST_CASE(components_are_declared_in_chibi_sources);
    RUN_TEST_CASE(unit_mismatches_are_errors);
    pu_clear_setup();
}

/* The cases that exercise project-wide prototype expansion budgets. */
void test_tool_prototype_expansion(const char *binary)
{
    bool found = realpath(binary, program);
    assert(found);
    (void)found;
    pu_setup(setup, teardown);
    RUN_TEST_CASE(prototype_expansion_is_shared);
    RUN_TEST_CASE(scene_instances_share_the_expansion);
    pu_clear_setup();
}
