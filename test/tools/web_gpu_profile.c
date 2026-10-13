#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <yyjson.h>

static char directory[] = "/tmp/chibi-web-gpu-profile-XXXXXX";
static char driver_path[1024], fixture_path[1024], log_path[1024];
static yyjson_doc *result;

/* Node executes the real inline shell against finite GPU/clock seams.
 * Scenarios are bounded; every assertion belongs to this C dispatcher. */
static const char *
    driver = "const vm=require('node:vm'),fs=require('node:fs');\n"
             "const "
             "canvas={addEventListener(){},focus(){}},other={dataset:{},"
             "addEventListener(){}};\n"
             "const "
             "context=vm.createContext({console:{log(){},warn(){},error(){}},"
             "Event,URLSearchParams,\n"
             "location:{search:''},performance:{now:()=>0},devicePixelRatio:1,"
             "navigator:{},\n"
             "window:{dispatchEvent(){}},ResizeObserver:class{observe(){}},\n"
             "document:{getElementById:id=>id==='canvas'?canvas:other,"
             "createElement:()=>({}),body:{append(){}}}});\n"
             "(async()=>{\n"
             "const html=fs.readFileSync(process.argv[2],'utf8');\n"
             "await "
             "vm.runInContext(html.match(/<script>([\\s\\S]*?)<\\/script>/"
             ")[1],context);\n"
             "vm.runInContext(fs.readFileSync(process.argv[3],'utf8'),context);"
             "\n"
             "console.log(JSON.stringify(await "
             "context.gpuProfileFixture(process.argv[4])));\n"
             "})().catch(error=>{console.error(error);process.exitCode=1;});\n";

static const char *fixture[] = {
    "// Finite fake-browser seam for the C-owned web-storage driver. Returns "
    "observed\n"
    "// values; C/pico owns assertions. No network, real GPU, or generated "
    "player.\n"
    "async function gpuProfileFixture(mode) {\n"
    "  let now = 0, queries = 0, buffers = 0, submits = 0, mapped = 0;\n"
    "  let resolves = 0, copies = 0, begins = 0, elapsedBegins = 0, destroyed "
    "= 0;\n"
    "  let glAvailable = false, disjoint = false, contextLost = false, glClock "
    "= 0, disjointReads = 0;\n"
    "  const maps = [], completions = [], descriptors = [];\n"
    "  globalThis.performance = { now: () => now };\n"
    "  globalThis.GPUBufferUsage = { QUERY_RESOLVE: 1, COPY_SRC: 2, COPY_DST: "
    "4, MAP_READ: 8 };\n"
    "  globalThis.GPUMapMode = { READ: 1 };\n"
    "  const device = {\n"
    "    features: new Set(['timestamp-query']),\n"
    "    createQuerySet() { queries++; return { destroy() { destroyed++; } }; "
    "},\n"
    "    createBuffer() {\n"
    "      buffers++;\n"
    "      return {\n"
    "        data: new BigUint64Array(256),\n"
    "        mapAsync() { mapped++; return new Promise(resolve => "
    "maps.push(resolve)); },\n"
    "        getMappedRange(offset, length) { return "
    "this.data.buffer.slice(offset, offset + length); },\n"
    "        unmap() {}, destroy() { destroyed++; },\n"
    "      };\n"
    "    },\n"
    "    createCommandEncoder() {\n"
    "      return {\n"
    "        beginRenderPass(descriptor) { begins++; "
    "descriptors.push(descriptor); return { end() {} }; },\n"
    "        beginComputePass(descriptor) { begins++; "
    "descriptors.push(descriptor); return { end() {} }; },\n"
    "        resolveQuerySet(query, first, count, target) {\n"
    "          resolves++;\n"
    "          for (let i = 0; i < count; i += 2) {\n"
    "            target.data[i] = BigInt(i) * 1000000n;\n"
    "            target.data[i + 1] = target.data[i] + 2000000n;\n"
    "          }\n"
    "        },\n"
    "        copyBufferToBuffer(source, so, target, to, bytes) {\n"
    "          copies++;\n"
    "          target.data = source.data.slice(so / 8, (so + bytes) / 8);\n"
    "        },\n"
    "        finish() { return {}; },\n"
    "      };\n"
    "    },\n"
    "    queue: {\n"
    "      submit() { submits++; },\n"
    "      onSubmittedWorkDone() { return new Promise(resolve => "
    "completions.push(resolve)); },\n"
    "    },\n"
    "  };\n"
    "  const extension = { TIMESTAMP_EXT: 1, QUERY_COUNTER_BITS_EXT: 2, "
    "GPU_DISJOINT_EXT: 3,\n"
    "    queryCounterEXT(query) { query.time = glClock++ * 3000000; } };\n"
    "  const gl = {\n"
    "    QUERY_RESULT_AVAILABLE: 4, QUERY_RESULT: 5,\n"
    "    getExtension() { return extension; },\n"
    "    getQuery() { return mode === 'gl-unavailable' ? 0 : 64; },\n"
    "    getParameter() { disjointReads++; return disjoint; },\n"
    "    isContextLost() { return contextLost; },\n"
    "    createQuery() { queries++; return {}; },\n"
    "    deleteQ",
    "uery() {},\n"
    "    beginQuery() { elapsedBegins++; },\n"
    "    getQueryParameter(query, name) { return name === 4 ? glAvailable : "
    "query.time; },\n"
    "  };\n"
    "  const Module = { canvas: { getContext() { return gl; } },\n"
    "    chibiGraphics: { renderer: mode.startsWith('gl-') ? 'WebGL 2' : "
    "'WebGPU' } };\n"
    "  const profile = createGpuProfile(Module, device);\n"
    "  const flush = async () => { for (let i = 0; i < 8; i++) await "
    "Promise.resolve(); };\n"
    "  const draw = (passes = 1) => {\n"
    "    const encoder = device.createCommandEncoder();\n"
    "    const attachments = [{ view: 'preserved' }];\n"
    "    for (let i = 0; i < passes; i++) encoder.beginRenderPass({ "
    "colorAttachments: attachments, label: 'preserved' }).end();\n"
    "    device.queue.submit([encoder.finish()]);\n"
    "  };\n"
    "  let lateCommand;\n"
    "  profile.beginFrame(mode !== 'gl-2d');\n"
    "  if (mode === 'pass-limit') draw(129);\n"
    "  else if (mode === 'encoder-limit') { for (let i = 0; i < 17; i++) "
    "draw(); }\n"
    "  else if (mode === 'late-submit') {\n"
    "    const encoder = device.createCommandEncoder();\n"
    "    encoder.beginRenderPass({ colorAttachments: [{ view: 'preserved' }], "
    "label: 'preserved' }).end();\n"
    "    lateCommand = encoder.finish();\n"
    "  }\n"
    "  else if (!mode.startsWith('gl-')) draw(2);\n"
    "  profile.endFrame();\n"
    "  await flush();\n"
    "  const before = profile.report();\n"
    "  const destroyedBefore = destroyed;\n"
    "  if (mode === 'pending-limit') {\n"
    "    now = 1000; profile.beginFrame(true); draw(); profile.endFrame();\n"
    "    now = 2000; profile.beginFrame(true); draw(); profile.endFrame();\n"
    "    await flush();\n"
    "  }\n"
    "  const pendingBefore = profile.report().pendingFrames;\n"
    "  if (mode.startsWith('gl-')) {\n"
    "    now = 7;\n"
    "    glAvailable = mode !== 'gl-disjoint' && mode !== 'gl-lost';\n"
    "    disjoint = mode === 'gl-disjoint';\n"
    "    contextLost = mode === 'gl-lost';\n"
    "    profile.beginFrame(mode !== 'gl-transition'); profile.endFrame();\n"
    "  } else {\n"
    "    now += 7;\n"
    "    completions.forEach(resolve => resolve());\n"
    "    await flush();\n"
    "  }\n"
    "  const afterCompletion = profile.report();\n"
    "  if (lateCommand) { device.queue.submit([lateCommand]); await flush(); "
    "}\n"
    "  maps.forEach(resolve => resolve());\n"
    "  await flush();\n"
    "  const result = profile.report();\n"
    "  return { before, afterCompletion, result, pendingBefore, queries, "
    "buffers, submits,\n"
    "    mapped, resolves, copies, begins, elapsedBegins, destroyedBefore, "
    "destroyed, disjointReads,\n"
    "    descriptorsPreserved: descriptors.every(d => d.label === 'preserved' "
    "&& d.colorAttachments[0].view === 'preserved'),\n"
    "    timestampedPasses: descriptors.filter(d => "
    "d.timestampWrites).length,\n"
    "    history: profile.",
    "history(),\n"
    "  };\n"
    "}\n"
};

static void setup()
{
    strcpy(directory, "/tmp/chibi-web-gpu-profile-XXXXXX");
    assert(mkdtemp(directory));
    snprintf(driver_path, sizeof driver_path, "%s/driver.cjs", directory);
    snprintf(fixture_path, sizeof fixture_path, "%s/fixture.js", directory);
    snprintf(log_path, sizeof log_path, "%s/output.json", directory);
    tool_write(driver_path, driver);
    FILE *output = fopen(fixture_path, "w");
    assert(output);
    for (int i = 0; i < (int)(sizeof fixture / sizeof *fixture); i++) {
        assert(fputs(fixture[i], output) >= 0);
    }
    assert(!fclose(output));
}

static void teardown()
{
    yyjson_doc_free(result);
    result = NULL;
    tool_remove_tree(directory);
}

static bool run(const char *mode)
{
    char source[1024];
    snprintf(source, sizeof source, "%s/src/host/web/index.html", tool_root());
    const char *argv[] = { "node", driver_path, source, fixture_path, mode, NULL };
    if (tool_run(argv, tool_root(), log_path)) {
        return false;
    }
    char *output = tool_read(log_path);
    yyjson_doc_free(result);
    result = yyjson_read(output, strlen(output), 0);
    free(output);
    return result != NULL;
}

static yyjson_val *field(const char *key)
{
    return yyjson_obj_get(yyjson_doc_get_root(result), key);
}

static yyjson_val *member(const char *object, const char *key)
{
    return yyjson_obj_get(field(object), key);
}

TEST_CASE(gpu_timestamp_result_waits_for_mapping_without_extra_submits)
{
    REQUIRE(run("normal"));
    REQUIRE(yyjson_is_null(member("before", "executionMs")));
    REQUIRE(yyjson_is_null(member("afterCompletion", "executionMs")));
    REQUIRE(yyjson_get_num(member("result", "executionMs")) == 4);
    REQUIRE(yyjson_get_num(member("result", "completionMs")) == 7);
    REQUIRE(yyjson_get_int(member("result", "passCount")) == 2);
    REQUIRE(yyjson_get_int(member("result", "queryCount")) == 4);
    REQUIRE(yyjson_get_int(member("result", "encoderCount")) == 1);
    REQUIRE(yyjson_get_bool(field("descriptorsPreserved")));
    REQUIRE(yyjson_get_int(field("submits")) == 1);
    REQUIRE(yyjson_get_int(field("resolves")) == 1);
    REQUIRE(yyjson_get_int(field("copies")) == 1);
    REQUIRE(yyjson_get_int(field("destroyed")) == 3);
    return true;
}

TEST_CASE(gpu_profile_bounds_queries_encoders_and_pending_samples)
{
    REQUIRE(run("pass-limit"));
    REQUIRE(yyjson_get_bool(member("result", "partial")));
    REQUIRE(yyjson_is_null(member("result", "executionMs")));
    REQUIRE(yyjson_get_int(member("result", "queryCount")) == 256);
    REQUIRE(yyjson_get_int(member("result", "passCount")) == 128);
    REQUIRE(yyjson_get_int(field("timestampedPasses")) == 128);
    REQUIRE(yyjson_get_int(field("begins")) == 129);
    REQUIRE(yyjson_get_int(field("submits")) == 1);
    REQUIRE(run("encoder-limit"));
    REQUIRE(yyjson_get_bool(member("result", "partial")));
    REQUIRE(yyjson_is_null(member("result", "executionMs")));
    REQUIRE(yyjson_get_int(member("result", "encoderCount")) == 16);
    REQUIRE(yyjson_get_int(member("result", "queryCount")) == 32);
    REQUIRE(yyjson_get_int(field("submits")) == 17);
    REQUIRE(run("pending-limit"));
    REQUIRE(yyjson_get_int(field("pendingBefore")) == 2);
    REQUIRE(yyjson_get_int(field("queries")) == 2);
    REQUIRE(yyjson_get_int(field("buffers")) == 4);
    REQUIRE(yyjson_get_int(field("submits")) == 3);
    REQUIRE(yyjson_arr_size(field("history")) == 2);
    REQUIRE(yyjson_get_int(member("result", "pendingFrames")) == 0);
    return true;
}

TEST_CASE(gpu_late_submission_keeps_resources_and_does_not_mix_frame_times)
{
    REQUIRE(run("late-submit"));
    REQUIRE(yyjson_get_bool(member("before", "partial")));
    REQUIRE(yyjson_get_int(member("before", "pendingFrames")) == 1);
    REQUIRE(yyjson_get_int(field("destroyedBefore")) == 0);
    REQUIRE(yyjson_get_bool(member("result", "partial")));
    REQUIRE(yyjson_is_null(member("result", "executionMs")));
    REQUIRE(yyjson_get_int(member("result", "pendingFrames")) == 0);
    REQUIRE(yyjson_get_int(field("destroyed")) == 3);
    REQUIRE(yyjson_arr_size(field("history")) == 1);
    return true;
}

TEST_CASE(webgl_timestamp_pairs_preserve_elapsed_queries_and_disjoint_ownership)
{
    REQUIRE(run("gl-ready"));
    REQUIRE(yyjson_get_num(member("result", "executionMs")) == 3);
    REQUIRE(yyjson_is_null(member("result", "completionMs")));
    REQUIRE(yyjson_get_int(field("queries")) == 2);
    REQUIRE(yyjson_get_int(field("elapsedBegins")) == 0);
    REQUIRE(yyjson_get_int(field("submits")) == 0);
    REQUIRE(run("gl-unavailable"));
    REQUIRE(!strcmp(yyjson_get_str(member("result", "status")), "unavailable"));
    REQUIRE(yyjson_is_null(member("result", "executionMs")));
    REQUIRE(yyjson_get_int(field("queries")) == 0);
    REQUIRE(yyjson_get_int(field("disjointReads")) == 0);
    const char *failures[] = { "gl-disjoint", "gl-lost", "gl-transition" };
    for (int i = 0; i < 3; i++) {
        REQUIRE(run(failures[i]));
        REQUIRE(yyjson_get_bool(member("result", "partial")));
        REQUIRE(yyjson_is_null(member("result", "executionMs")));
        REQUIRE(yyjson_get_int(member("result", "pendingFrames")) == 0);
        REQUIRE(yyjson_get_int(field("elapsedBegins")) == 0);
        if (i > 0) {
            REQUIRE(yyjson_get_int(field("disjointReads")) == 2);
        }
    }
    REQUIRE(run("gl-2d"));
    REQUIRE(!strcmp(yyjson_get_str(member("result", "status")), "unavailable"));
    REQUIRE(yyjson_get_int(field("queries")) == 0);
    REQUIRE(yyjson_get_int(field("disjointReads")) == 0);
    return true;
}

void test_tool_web_gpu_profile()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(gpu_timestamp_result_waits_for_mapping_without_extra_submits);
    RUN_TEST_CASE(gpu_profile_bounds_queries_encoders_and_pending_samples);
    RUN_TEST_CASE(gpu_late_submission_keeps_resources_and_does_not_mix_frame_times);
    RUN_TEST_CASE(webgl_timestamp_pairs_preserve_elapsed_queries_and_disjoint_ownership);
    pu_setup(NULL, NULL);
}
