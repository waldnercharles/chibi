#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char program[PATH_MAX];
static char directory[] = "/tmp/chibi-render-fixture-XXXXXX";
static bool owns_directory;
static yyjson_doc *reply;
static char *output;

static void cleanup()
{
    if (pu_test_failed() && output) {
        fputs(output, stderr);
    }
    yyjson_doc_free(reply);
    free(output);
    if (owns_directory) {
        tool_remove_tree(directory);
    }
}

static yyjson_val *named(yyjson_val *array, const char *name)
{
    size_t index, count;
    yyjson_val *item;
    yyjson_arr_foreach(array, index, count, item)
    {
        yyjson_val *value = yyjson_obj_get(item, "name");
        if (name ? yyjson_equals_str(value, name) : !value) {
            return item;
        }
    }
    return NULL;
}

static double field(yyjson_val *entity, const char *component, const char *name)
{
    yyjson_val *record = named(yyjson_obj_get(entity, "components"), component);
    yyjson_val *value = yyjson_obj_get(named(yyjson_obj_get(record, "fields"), name), "value");
    return yyjson_is_num(value) ? yyjson_get_num(value) : NAN;
}

TEST_CASE(rendering_fixture_spawns_a_frame_part_and_preserves_its_source_pose)
{
    REQUIRE(mkdtemp(directory));
    owns_directory = true;
    char project[PATH_MAX], request[96], log[96];
    int size = snprintf(project, sizeof project, "%s/test/fixtures/feature-coverage", tool_root());
    REQUIRE(size > 0 && size < (int)sizeof project);
    snprintf(request, sizeof request, "%s/request.json", directory);
    snprintf(log, sizeof log, "%s/output.log", directory);
    tool_write(
        request,
        "{\"contract\":1,\"operation\":\"runtime_snapshot\",\"kind\":\"scene\","
        "\"name\":\"Rendering3D\",\"frames\":4,\"entity_limit\":10,"
        "\"component_limit\":16,\"field_limit\":16,\"behavior_limit\":1}\n"
    );
    const char *args[] = {
        "/bin/sh", "-c",    "exec \"$0\" --project-root \"$1\" < \"$2\"",
        program,   project, request,
        NULL
    };
    REQUIRE(tool_run(args, project, log) == 0);
    output = tool_read(log);
    const char *json = output ? strstr(output, "{\"contract\"") : NULL;
    REQUIRE(json);
    reply = yyjson_read(json, strcspn(json, "\n"), 0);
    REQUIRE(reply);
    yyjson_val *result = yyjson_obj_get(yyjson_doc_get_root(reply), "result");
    REQUIRE(yyjson_get_int(yyjson_obj_get(result, "frames")) == 4);
    REQUIRE(yyjson_get_int(yyjson_obj_get(result, "live_entities")) == 10);
    REQUIRE(!yyjson_get_bool(yyjson_obj_get(result, "entities_truncated")));
    yyjson_val *entities = yyjson_obj_get(result, "entities");
    REQUIRE(yyjson_arr_size(entities) == 10);
    yyjson_val *frame = named(entities, "Frame"), *beam = named(entities, NULL);
    REQUIRE(frame && beam);
    yyjson_val *behaviors = yyjson_obj_get(frame, "behaviors");
    REQUIRE(yyjson_arr_size(behaviors) == 1);
    yyjson_val *behavior = yyjson_arr_get_first(behaviors);
    REQUIRE(yyjson_equals_str(yyjson_obj_get(behavior, "asset"), "RenderFrameProbe"));
    REQUIRE(yyjson_is_null(yyjson_obj_get(behavior, "last_failure")));

    /* The first beam of the 60-unit frame is its lower, back horizontal edge. */
    REQUIRE(field(beam, "Geometry3D", "primitive") == 0);
    REQUIRE(fabs(field(beam, "Geometry3D", "width") - 60) < .001);
    REQUIRE(fabs(field(beam, "Geometry3D", "height") - 10.8) < .001);
    REQUIRE(fabs(field(beam, "Geometry3D", "depth") - 10.8) < .001);
    REQUIRE(fabs(field(beam, "Geometry3D", "elevation") - 11.4) < .001);
    REQUIRE(fabs(field(beam, "VisualTransform", "yaw") - .5) < .001);
    REQUIRE(field(beam, "VisualTransform", "scale") == 1);
    /* Allow the renderer's deterministic trigonometry approximation. */
    REQUIRE(fabs(field(beam, "Transform", "x") - (-100 + 24.6 * sin(.5))) < .1);
    REQUIRE(fabs(field(beam, "Transform", "y") - (70 - 24.6 * cos(.5))) < .1);

    REQUIRE(fabs(field(frame, "VisualTransform", "yaw") - .5) < .001);
    REQUIRE(field(frame, "VisualTransform", "pitch") == 0);
    REQUIRE(field(frame, "VisualTransform", "roll") == 0);
    REQUIRE(field(frame, "VisualTransform", "scale") == 1);
    REQUIRE(field(frame, "Geometry3D", "elevation") == 36);
    return true;
}

void test_tool_render_fixture(const char *binary)
{
    assert(realpath(binary, program));
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(rendering_fixture_spawns_a_frame_part_and_preserves_its_source_pose);
    pu_clear_setup();
}
