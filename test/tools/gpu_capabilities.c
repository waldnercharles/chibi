#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "third_party/yyjson/yyjson.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char directory[128], driver[180], log_path[180];
static yyjson_doc *document;
static char *output;

/* Only transport and OS-response injection live in Python. C owns fixtures and
 * assertions, and the production probe never touches this host's GPU. */
static const char driver_source
    [] = "import json, subprocess, sys\n"
         "from types import SimpleNamespace\n"
         "sys.path.insert(0, sys.argv[1] + '/tools')\n"
         "import gpu_capabilities as gpu\n"
         "fixture = json.loads(sys.argv[2])\n"
         "calls = []\n"
         "def run(command, **options):\n"
         "    calls.append(dict(command=command, timeout=options['timeout'], "
         "check=options['check']))\n"
         "    if fixture == 'timeout':\n"
         "        raise subprocess.TimeoutExpired(command, "
         "options['timeout'])\n"
         "    if fixture == 'failure':\n"
         "        raise subprocess.CalledProcessError(1, command)\n"
         "    return SimpleNamespace(stdout=json.dumps(fixture))\n"
         "gpu.subprocess.run = run\n"
         "gpu.sys.platform = 'darwin'\n"
         "print(json.dumps(dict(probe=gpu.probe(), calls=calls, "
         "vulkan=[gpu.classify_devices([dict(type=2)]), "
         "gpu.classify_devices([dict(type=4)]), "
         "gpu.classify_devices([dict(type=3)])])))\n";

static yyjson_val *probe(const char *fixture)
{
    free(output);
    yyjson_doc_free(document);
    const char *argv[] = { "python3", driver, tool_root(), fixture, NULL };
    assert(!tool_run_within(argv, tool_root(), log_path, 5));
    output = tool_read(log_path);
    assert(output);
    document = yyjson_read(output, strlen(output), 0);
    assert(document);
    return yyjson_obj_get(yyjson_doc_get_root(document), "probe");
}

static bool equals(yyjson_val *value, const char *expected)
{
    const char *text = yyjson_get_str(value);
    return text && !strcmp(text, expected);
}

TEST_CASE(metal_discovery_reports_hardware_without_render_claims)
{
    yyjson_val *value = probe(
        "{\"SPDisplaysDataType\":[{\"sppci_model\":\"Fixture GPU\","
        "\"spdisplays_mtlgpufamilysupport\":\"spdisplays_metal4\","
        "\"spdisplays_ndrvs\":[{\"serial\":\"PRIVATE\"}]}]}"
    );
    REQUIRE(equals(yyjson_obj_get(value, "backend"), "Metal"));
    REQUIRE(equals(yyjson_obj_get(value, "status"), "enumerated"));
    REQUIRE(equals(yyjson_obj_get(value, "scope"), "discovery_only_not_render_validation"));
    REQUIRE(equals(yyjson_obj_get(value, "acceleration"), "hardware"));
    REQUIRE(yyjson_is_true(yyjson_obj_get(value, "hardware_available")));
    yyjson_val *devices = yyjson_obj_get(value, "devices");
    REQUIRE(yyjson_arr_size(devices) == 1);
    REQUIRE(equals(yyjson_obj_get(yyjson_arr_get(devices, 0), "name"), "Fixture GPU"));
    REQUIRE(!strstr(output, "PRIVATE"));
    REQUIRE(!strstr(output, "spdisplays_ndrvs"));
    yyjson_val *root = yyjson_doc_get_root(document);
    yyjson_val *call = yyjson_arr_get(yyjson_obj_get(root, "calls"), 0);
    REQUIRE(yyjson_get_int(yyjson_obj_get(call, "timeout")) == 4);
    REQUIRE(yyjson_is_true(yyjson_obj_get(call, "check")));
    yyjson_val *command = yyjson_obj_get(call, "command");
    REQUIRE(equals(yyjson_arr_get(command, 0), "/usr/sbin/system_profiler"));
    REQUIRE(equals(yyjson_arr_get(command, 1), "SPDisplaysDataType"));
    REQUIRE(equals(yyjson_arr_get(command, 4), "3"));
    yyjson_val *vulkan = yyjson_obj_get(root, "vulkan");
    REQUIRE(equals(yyjson_arr_get(vulkan, 0), "hardware"));
    REQUIRE(equals(yyjson_arr_get(vulkan, 1), "software"));
    REQUIRE(equals(yyjson_arr_get(vulkan, 2), "unknown"));
    return true;
}

TEST_CASE(metal_discovery_requires_explicit_support)
{
    const char *fixtures[] = {
        "{\"SPDisplaysDataType\":[{\"_name\":\"GPU without support "
        "metadata\"}]}",
        "{\"SPDisplaysDataType\":[{\"spdisplays_metal\":\"spdisplays_"
        "unsupported\"}]}",
        "{\"SPDisplaysDataType\":[{\"spdisplays_metal\":\"spdisplays_"
        "metalunsupported\"}]}",
        "{\"SPDisplaysDataType\":[]}",
    };
    for (int i = 0; i < 4; i++) {
        yyjson_val *value = probe(fixtures[i]);
        REQUIRE(equals(yyjson_obj_get(value, "status"), "enumerated"));
        REQUIRE(equals(yyjson_obj_get(value, "acceleration"), "unknown"));
        REQUIRE(yyjson_is_false(yyjson_obj_get(value, "hardware_available")));
    }
    yyjson_val *value = probe(
        "{\"SPDisplaysDataType\":[{\"_name\":\"Older GPU\","
        "\"spdisplays_metal\":\"spdisplays_supported\"}]}"
    );
    REQUIRE(yyjson_is_true(yyjson_obj_get(value, "hardware_available")));
    return true;
}

TEST_CASE(metal_discovery_handles_unavailable_metadata)
{
    const char *fixtures[] = { "\"timeout\"", "\"failure\"", "{}", "[]", "{\"SPDisplaysDataType\":[null]}" };
    for (int i = 0; i < 5; i++) {
        yyjson_val *value = probe(fixtures[i]);
        REQUIRE(equals(yyjson_obj_get(value, "status"), "unavailable"));
        REQUIRE(yyjson_is_false(yyjson_obj_get(value, "hardware_available")));
        REQUIRE(equals(yyjson_obj_get(value, "scope"), "discovery_only_not_render_validation"));
    }
    return true;
}

void test_tool_gpu_capabilities()
{
    snprintf(directory, sizeof directory, "/tmp/chibi-gpu-discovery-XXXXXX");
    assert(mkdtemp(directory));
    snprintf(driver, sizeof driver, "%s/probe.py", directory);
    snprintf(log_path, sizeof log_path, "%s/output.json", directory);
    tool_write(driver, driver_source);
    RUN_TEST_CASE(metal_discovery_reports_hardware_without_render_claims);
    RUN_TEST_CASE(metal_discovery_requires_explicit_support);
    RUN_TEST_CASE(metal_discovery_handles_unavailable_metadata);
    yyjson_doc_free(document);
    document = NULL;
    free(output);
    output = NULL;
    tool_remove_tree(directory);
}
