#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char program[4096], root[128], log_path[160];
static yyjson_doc *reply;

static void path(char *out, size_t size, const char *name)
{
    int n = snprintf(out, size, "%s/%s", root, name);
    assert(n > 0 && (size_t)n < size);
}

static void setup()
{
    strcpy(root, "/tmp/chibi-tetris-XXXXXX");
    assert(mkdtemp(root));
    path(log_path, sizeof log_path, "output.log");
}

static void cleanup()
{
    if (pu_test_failed()) {
        char *output = tool_read(log_path);
        if (output) {
            fputs(output, stderr);
        }
        free(output);
    }
    yyjson_doc_free(reply);
    reply = NULL;
    tool_remove_tree(root);
}

static bool clone_project()
{
    char source[4096], destination[256];
    int n = snprintf(source, sizeof source, "%s/examples/tetris", tool_root());
    assert(n > 0 && n < (int)sizeof source);
    path(destination, sizeof destination, "tetris");
#ifdef __APPLE__
    const char *args[] = { "cp", "-cR", source, destination, NULL };
#else
    const char *args[] = { "cp",   "--reflink=auto", "-R",
                           source, destination,      NULL };
#endif
    return tool_run(args, root, log_path) == 0;
}

static yyjson_val *result(const char *key)
{
    return yyjson_obj_get(yyjson_obj_get(yyjson_doc_get_root(reply), "result"), key);
}

static int request(const char *text)
{
    char filename[256];
    path(filename, sizeof filename, "request.json");
    tool_write(filename, text);
    const char *args[] = { "/bin/sh",
                           "-c",
                           "exec \"$0\" --project-root tetris < request.json",
                           program,
                           NULL };
    int status = tool_run(args, root, log_path);
    char *output = tool_read(log_path);
    const char *json = output ? strstr(output, "{\"contract\"") : NULL;
    yyjson_doc_free(reply);
    reply = json ? yyjson_read(json, strlen(json), YYJSON_READ_STOP_WHEN_DONE) : NULL;
    free(output);
    return reply ? status : -1;
}

static const char *ui_text(const char *selector)
{
    yyjson_val *texts = result("ui");
    size_t index, count;
    yyjson_val *text;
    yyjson_arr_foreach(texts, index, count, text)
    {
        if (yyjson_equals_str(yyjson_obj_get(text, "element"), selector)) {
            return yyjson_get_str(yyjson_obj_get(text, "text"));
        }
    }
    return NULL;
}

static bool ui_equals(const char *selector, const char *expected)
{
    const char *text = ui_text(selector);
    return text && !strcmp(text, expected);
}

static bool board_succeeded()
{
    size_t index, count;
    yyjson_val *entity;
    yyjson_arr_foreach(result("entities"), index, count, entity)
    {
        if (!yyjson_equals_str(yyjson_obj_get(entity, "name"), "Board")) {
            continue;
        }
        yyjson_val *behavior = yyjson_arr_get_first(yyjson_obj_get(entity, "behaviors"));
        return yyjson_equals_str(yyjson_obj_get(behavior, "asset"), "Tetris") &&
               yyjson_is_null(yyjson_obj_get(behavior, "last_failure"));
    }
    return false;
}

static int snapshot(int frames, const char *timeline)
{
    char text[16640];
    int n = snprintf(
        text,
        sizeof text,
        "{\"contract\":1,\"operation\":\"runtime_snapshot\",\"kind\":\"scene\","
        "\"name\":\"Start\",\"frames\":%d,\"entity_limit\":3,\"behavior_"
        "limit\":1,"
        "\"input_timeline\":[%s]}",
        frames,
        timeline
    );
    assert(n > 0 && n < (int)sizeof text);
    return request(text);
}

/* The copied production behavior supplies all rules. Only initialize its state
 * after Begin has built its tables and cells; no native game implementation. */
static void initialize(const char *fixture)
{
    char filename[256];
    path(filename, sizeof filename, "tetris/content/behaviors/Tetris.chibi");
    char *source = tool_read(filename);
    const char *anchor = "        ready = true\n";
    char *at = source ? strstr(source, anchor) : NULL;
    assert(at && !strstr(at + strlen(anchor), anchor));
    size_t before = (size_t)(at - source), inserted = strlen(fixture);
    char *updated = xmalloc(strlen(source) + inserted + 1);
    memcpy(updated, source, before);
    memcpy(updated + before, fixture, inserted);
    strcpy(updated + before + inserted, at);
    tool_write(filename, updated);
    free(updated);
    free(source);
}

static int press(char *timeline, size_t size, int n, int step, int control)
{
    int added = snprintf(
        timeline + n,
        size - (size_t)n,
        "%s{\"frame\":%d,\"input\":{\"controls\":[%d]}},"
        "{\"frame\":%d,\"input\":{\"controls\":[]}}",
        n ? "," : "",
        step,
        control,
        step + 1
    );
    assert(added > 0 && (size_t)added < size - (size_t)n);
    return n + added;
}

static int play(int turns, bool restart)
{
    enum
    {
        SPACE = 4,
        ENTER = 9,
        UP = 11,
        LAST_STEP = 160,
        FRAMES = 170
    };
    char timeline[16384] = "";
    int n = 0;
    for (int step = 20; step + 2 * turns + 1 < LAST_STEP; step += 2) {
        for (int t = 0; t < turns; t++, step += 2) {
            n = press(timeline, sizeof timeline, n, step, UP);
        }
        n = press(timeline, sizeof timeline, n, step, SPACE);
    }
    if (restart) {
        n = press(timeline, sizeof timeline, n, LAST_STEP, ENTER);
    }
    return snapshot(FRAMES, timeline);
}

TEST_CASE(tetris_example_plays)
{
    REQUIRE(clone_project());
    REQUIRE(request("{\"contract\":1,\"operation\":\"validate_project\"}") == 0);
    REQUIRE(yyjson_get_int(result("errors")) == 0);
    /* Rotations near the ceiling must not kick a cell above stored rows. */
    REQUIRE(play(2, false) == 0 && board_succeeded());
    REQUIRE(ui_equals("#over", "GAME OVER"));
    const char *score = ui_text("#score");
    REQUIRE(score && atoi(score) > 0);
    /* Unrotated pieces fill the spawn columns; Enter then restarts. */
    REQUIRE(play(0, true) == 0 && board_succeeded());
    REQUIRE(ui_equals("#score", "0") && !ui_text("#over"));
    return true;
}

TEST_CASE(tetris_stops_the_tick_when_hold_tops_out)
{
    REQUIRE(clone_project());
    initialize(
        "        held = 2\n        piece = 3\n        turn = 0\n        px = "
        "3\n"
        "        py = 10\n        board[4] = 7\n        score = 123\n        "
        "Paint()\n"
    );
    /* Shift=5 and Space=4: blocked held O spawn plus hard drop in one step. */
    const char *timeline = "{\"frame\":20,\"input\":{\"controls\":[5,4]}},"
                           "{\"frame\":21,\"input\":{\"controls\":[]}}";
    REQUIRE(snapshot(22, timeline) == 0 && board_succeeded());
    REQUIRE(ui_equals("#over", "GAME OVER") && ui_equals("#score", "123"));
    return true;
}

void test_tool_tetris(const char *binary)
{
    assert(realpath(binary, program));
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(tetris_example_plays);
    RUN_TEST_CASE(tetris_stops_the_tick_when_hold_tops_out);
    pu_clear_setup();
}
