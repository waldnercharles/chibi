#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yyjson.h>

static char directory[] = "/tmp/chibi-web-storage-XXXXXX";
static char driver_path[1024], log_path[1024];
static yyjson_doc *result;

void test_tool_web_gpu_profile();

/* Node runs the production browser bridge with only browser/FS seams replaced.
 * Scenarios and assertions belong to the C dispatcher, not a second JS suite. */
static const char *driver =
    "const vm=require('node:vm'), host=require('node:fs');\n"
    "const code=host.readFileSync(process.argv[2],'utf8'), "
    "mode=process.argv[3];\n"
    "const files=new Map(), dirs=new "
    "Set(['/','/projects','/projects/starter']);\n"
    "files.set('/projects/starter/project.kdl',new TextEncoder().encode('name "
    "Starter'));\n"
    "const "
    "FS={filesystems:{IDBFS:{}},mount(){},syncfs(_,done){queueMicrotask(()=>"
    "done(null));},\n"
    " analyzePath:p=>({exists:files.has(p)||dirs.has(p)}),stat:p=>({mode:dirs."
    "has(p)?1:0}),isDir:m=>m===1,\n"
    " mkdirTree(p){let base='';for(const part of "
    "p.split('/').filter(Boolean)){base+='/'+part;dirs.add(base);}},\n"
    " writeFile(p,data){if(mode==='write-failure'&&p.endsWith('/content/"
    "scene.kdl'))throw Error('disk full');files.set(p,typeof "
    "data==='string'?new "
    "TextEncoder().encode(data):new Uint8Array(data));},\n"
    " readFile(p,opt){if(!files.has(p))throw Error('missing '+p);return "
    "opt?.encoding?new TextDecoder().decode(files.get(p)):files.get(p);},\n"
    " readdir(p){return ['.','..',...new "
    "Set([...files.keys(),...dirs].filter(x=>x.startsWith(p+'/"
    "')).map(x=>x.slice(p.length+1).split('/')[0]))];},\n"
    " unlink:p=>files.delete(p),rmdir:p=>dirs.delete(p)};\n"
    "let downloads=0,revision='a'.repeat(40),checksum,archive;\n"
    "async function launch(search=''){\n"
    " let ready,aborted='',released=false;const done=new Promise(r=>ready=r);\n"
    " const "
    "location={origin:'https://example.test',href:'https://example.test/game/"
    "dev/'+search,search};\n"
    " const Module={FS,chibiPublishedProject:'project.json',showError(){}};\n"
    " const "
    "context={Module,location,window:{location},navigator:{storage:{}},history:"
    "{replaceState(){}},\n"
    " URL,URLSearchParams,TextEncoder,TextDecoder,Uint8Array,DataView,"
    "ArrayBuffer,Blob,Response,\n"
    " crypto:require('node:crypto').webcrypto,AbortSignal,setTimeout:()=>0,"
    "clearTimeout(){},\n"
    " addRunDependency(){},removeRunDependency(){released=true;Module."
    "onRuntimeInitialized();ready();},\n"
    " abort(message){aborted=message;ready();},\n"
    " async fetch(url){downloads++;if(mode==='download')return new "
    "Response('',{status:404});\n"
    " if(url.pathname.endsWith('.json'))return "
    "mode==='json'?new "
    "Response('{broken'):Response.json({format:1,id:'celeste',revision,archive:"
    "'project.zip',"
    "sha256:mode==='checksum'?'0'.repeat(64):checksum});\n"
    " return new Response(archive);}};\n"
    " vm.runInNewContext(code,context);\n"
    " if(!archive){archive=await Module.chibiZipFiles([['project.kdl',new "
    "TextEncoder().encode('name Celeste')],\n"
    " [mode==='traversal'?'../outside':'content/scene.kdl',new "
    "TextEncoder().encode('original')]]).arrayBuffer();\n"
    " checksum=require('node:crypto').createHash('sha256').update(Buffer.from("
    "archive)).digest('hex');}\n"
    " Module.preRun[0]();await done;return "
    "{root:Module.arguments[1],aborted,released};\n"
    "}\n"
    "(async()=>{\n"
    " if(mode==='explicit'){FS.mkdirTree('/workspace/saved');FS.writeFile('/"
    "workspace/saved/project.kdl','name Saved');}\n"
    " if(mode==='conflict'){FS.mkdirTree('/workspace/"
    "published-celeste-'+revision);FS.writeFile('/workspace/"
    "published-celeste-'+revision+'/project.kdl','name Existing');}\n"
    " let outcome=await "
    "launch(mode==='explicit'?'?project=saved':mode==='sample'?'?project-root="
    "projects/starter':mode==='new'?'?new':'');\n"
    " if(mode==='repeat'||mode==='revision'){FS.writeFile(outcome.root+'"
    "content/scene.kdl','edited');\n"
    " if(mode==='revision')revision='b'.repeat(40);outcome=await launch();}\n"
    " const text=p=>files.has(p)?FS.readFile(p,{encoding:'utf8'}):'';\n"
    " console.log(JSON.stringify({...outcome,downloads,content:text(outcome."
    "root+'content/scene.kdl'),\n"
    " previous:text('/workspace/published-celeste-'+ "
    "'a'.repeat(40)+'/content/scene.kdl'),\n"
    " project:text(outcome.root+'project.kdl'),outside:files.has('/workspace/"
    "outside'),partial:dirs.has('/workspace/published-celeste-'+revision)}));\n"
    "})().catch(error=>{console.error(error);process.exitCode=1;});\n";

static void setup()
{
    strcpy(directory, "/tmp/chibi-web-storage-XXXXXX");
    assert(mkdtemp(directory));
    snprintf(driver_path, sizeof driver_path, "%s/driver.cjs", directory);
    snprintf(log_path, sizeof log_path, "%s/output.json", directory);
    tool_write(driver_path, driver);
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
    const char *studio = getenv("CHIBI_STUDIO_SOURCE");
    snprintf(source, sizeof source, "%s/web/web_storage.js", studio ? studio : "../chibi-studio");
    const char *argv[] = { "node", driver_path, source, mode, NULL };
    if (tool_run(argv, tool_root(), log_path)) {
        return false;
    }
    char *text = tool_read(log_path);
    result = yyjson_read(text, strlen(text), 0);
    free(text);
    return result != NULL;
}

static yyjson_val *field(const char *key)
{
    return yyjson_obj_get(yyjson_doc_get_root(result), key);
}

static const char *text(const char *key)
{
    return yyjson_get_str(field(key));
}

TEST_CASE(published_source_opens_instead_of_starter)
{
    REQUIRE(run("initial"));
    REQUIRE(yyjson_get_bool(field("released")));
    REQUIRE(!*text("aborted"));
    REQUIRE(!strcmp(text("project"), "name Celeste"));
    REQUIRE(!strcmp(text("content"), "original"));
    REQUIRE(strstr(text("root"), "/workspace/published-celeste-") == text("root"));
    return true;
}

TEST_CASE(reopening_a_revision_preserves_edits_without_redownloading)
{
    REQUIRE(run("repeat"));
    REQUIRE(!strcmp(text("content"), "edited"));
    REQUIRE(yyjson_get_int(field("downloads")) == 3);
    return true;
}

TEST_CASE(new_revision_retains_the_previous_workspace)
{
    REQUIRE(run("revision"));
    REQUIRE(!strcmp(text("content"), "original"));
    REQUIRE(!strcmp(text("previous"), "edited"));
    REQUIRE(yyjson_get_int(field("downloads")) == 4);
    return true;
}

TEST_CASE(explicit_workspace_overrides_published_default)
{
    REQUIRE(run("explicit"));
    REQUIRE(!strcmp(text("root"), "/workspace/saved/"));
    REQUIRE(!strcmp(text("project"), "name Saved"));
    REQUIRE(yyjson_get_int(field("downloads")) == 0);
    return true;
}

TEST_CASE(checksum_failure_stops_startup)
{
    REQUIRE(run("checksum"));
    REQUIRE(strstr(text("aborted"), "checksum"));
    REQUIRE(!yyjson_get_bool(field("released")));
    REQUIRE(!*text("root"));
    return true;
}

TEST_CASE(download_failure_stops_startup)
{
    REQUIRE(run("download"));
    REQUIRE(strstr(text("aborted"), "HTTP 404"));
    REQUIRE(!yyjson_get_bool(field("released")));
    return true;
}

TEST_CASE(explicit_sample_overrides_published_default)
{
    REQUIRE(run("sample"));
    REQUIRE(!strcmp(text("project"), "name Starter"));
    REQUIRE(yyjson_get_int(field("downloads")) == 0);
    return true;
}

TEST_CASE(new_project_overrides_published_default)
{
    REQUIRE(run("new"));
    REQUIRE(!strcmp(text("project"), "name Starter"));
    REQUIRE(yyjson_get_int(field("downloads")) == 0);
    return true;
}

TEST_CASE(invalid_manifest_stops_startup)
{
    REQUIRE(run("json"));
    REQUIRE(*text("aborted"));
    REQUIRE(!yyjson_get_bool(field("released")));
    return true;
}

TEST_CASE(workspace_without_marker_is_preserved)
{
    REQUIRE(run("conflict"));
    REQUIRE(strstr(text("aborted"), "conflicts"));
    REQUIRE(yyjson_get_bool(field("partial")));
    REQUIRE(yyjson_get_int(field("downloads")) == 1);
    return true;
}

TEST_CASE(failed_import_is_removed)
{
    REQUIRE(run("write-failure"));
    REQUIRE(strstr(text("aborted"), "disk full"));
    REQUIRE(!yyjson_get_bool(field("partial")));
    REQUIRE(!yyjson_get_bool(field("released")));
    return true;
}

TEST_CASE(archive_cannot_escape_its_workspace)
{
    REQUIRE(run("traversal"));
    REQUIRE(strstr(text("aborted"), "Invalid published project path"));
    REQUIRE(!yyjson_get_bool(field("outside")));
    REQUIRE(!yyjson_get_bool(field("released")));
    return true;
}

TEST_CASE(browser_export_does_not_require_native_process_apis)
{
    char include[1024], header[1024], source[1024];
    snprintf(include, sizeof include, "%s/editor", directory);
    REQUIRE(!mkdir(include, 0700));
    snprintf(include, sizeof include, "%s/editor/studio", directory);
    REQUIRE(!mkdir(include, 0700));
    snprintf(header, sizeof header, "%s/editor/studio/studio_internal.h", directory);
    tool_write(header, "#include <stdbool.h>\ntypedef struct studio studio;\n");
    const char *studio = getenv("CHIBI_STUDIO_SOURCE");
    snprintf(source, sizeof source, "%s/src/editor/studio/studio_export.c", studio ? studio : "../chibi-studio");
    /* Compile the actual browser branch against only its opaque Studio contract.
     * Native SDL process APIs are deliberately unavailable in this fixture. */
    const char *argv[] = { "cc",
                           "-std=c2x",
                           "-Wall",
                           "-Wextra",
                           "-Werror",
                           "-fsyntax-only",
                           "-D__EMSCRIPTEN__",
                           "-I",
                           directory,
                           source,
                           NULL };
    REQUIRE(tool_run(argv, tool_root(), log_path) == 0);
    return true;
}

TEST_CASE(canvas_layout_changes_notify_the_platform_resize_handler)
{
    /* Execute each real shell with browser seams, leaving scenarios and
     * assertions here. The browser integration checks SDL's backing buffer. */
    tool_write(
        driver_path,
        "const vm=require('node:vm'),fs=require('node:fs');\n"
        "const html=fs.readFileSync(process.argv[2],'utf8');\n"
        "let callback,target;const events=[];\n"
        "const canvas={addEventListener(){},focus(){}};\n"
        "const status={dataset:{},append(){},addEventListener(){}};\n"
        "const context={console,Event,navigator:{},URLSearchParams,"
        "location:{search:''},performance:{now:()=>0},\n"
        "window:{addEventListener(){},dispatchEvent:e=>events.push(e.type)},\n"
        "document:{getElementById:id=>id==='canvas'?canvas:status,"
        "createElement:()=>({}),body:{append(){}}},\n"
        "ResizeObserver:class{constructor(fn){callback=fn;}observe(node){"
        "target=node;}}};\n"
        "vm.runInNewContext(html.match(/<script>([\\s\\S]*?)<\\/script>/"
        ")[1],context);\n"
        "for(const [width,height] of JSON.parse(process.argv[3]))"
        "callback?.([{target:canvas,contentRect:{width,height}}]);\n"
        "console.log(JSON.stringify({observed:target===canvas,events}));\n"
    );
    const char *shells[] = { "src/host/web/index.html", "tools/web_shell.html" };
    for (int i = 0; i < 2; i++) {
        char source[1024];
        snprintf(source, sizeof source, "%s/%s", tool_root(), shells[i]);
        const char *argv[] = { "node",
                               driver_path,
                               source,
                               "[[0,0],[320,180],[0,0],[640,360]]",
                               NULL };
        REQUIRE(tool_run(argv, tool_root(), log_path) == 0);
        char *output = tool_read(log_path);
        result = yyjson_read(output, strlen(output), 0);
        free(output);
        REQUIRE(result);
        REQUIRE(yyjson_get_bool(field("observed")));
        yyjson_val *events = field("events");
        REQUIRE(yyjson_arr_size(events) == 4);
        for (int event = 0; event < 4; event++) {
            REQUIRE(!strcmp(yyjson_get_str(yyjson_arr_get(events, event)), "resize"));
        }
        yyjson_doc_free(result);
        result = NULL;
    }
    return true;
}

/* The browser loads no generated player here. C supplies callbacks, time and
 * device events; the production shell decides what to request and display. */
static const char *player_shell_driver =
    "const vm=require('node:vm'),fs=require('node:fs');\n"
    "const html=fs.readFileSync(process.argv[2],'utf8');\n"
    "const input=JSON.parse(process.argv[3]);\n"
    "let now=0,adapters=0,devices=0,prevented=false,lost;\n"
    "const listeners={},scripts=[],snapshots=[];\n"
    "const canvas={width:960,height:540,clientWidth:320,clientHeight:180,"
    "addEventListener:(name,fn)=>listeners[name]=fn,focus(){}};\n"
    "const status={dataset:{},hidden:false,textContent:''};\n"
    "const diagnostics={hidden:true,textContent:''};\n"
    "const copyReport={hidden:true,textContent:'Copy "
    "report',addEventListener(){}};\n"
    "const device={lost:new Promise(resolve=>lost=resolve)};\n"
    "const adapter={limits:{maxColorAttachmentBytesPerSample:64},features:[],"
    "async requestDevice(){devices++;return device;}};\n"
    "const context={console:{log(){},warn(){},error(){}},Event,URLSearchParams,"
    "location:{search:input.search},performance:{now:()=>now},devicePixelRatio:"
    "3,\n"
    "navigator:{gpu:{async requestAdapter(){adapters++;return adapter;}}},\n"
    "window:{dispatchEvent(){}},ResizeObserver:class{observe(){}},\n"
    "document:{getElementById:id=>({canvas,status,diagnostics,'copy-report':"
    "copyReport})[id],"
    "createElement:()=>({}),body:{append:script=>scripts.push(script.src)}}};\n"
    "const snapshot=()=>({...context.Module.chibiGraphics,"
    "hidden:diagnostics.hidden,text:diagnostics.textContent,"
    "statusHidden:status.hidden,statusText:status.textContent,"
    "hasFrame:typeof context.Module.chibiFrame==='function',"
    "timingEnabled:context.Module.chibiTimingEnabled===true,"
    "report:context.Module.chibiReport?.()});\n"
    "(async()=>{\n"
    "await "
    "vm.runInNewContext(html.match(/<script>([\\s\\S]*?)<\\/script>/"
    ")[1],context);\n"
    "for(const action of input.actions||[]){\n"
    "if(action.at!==undefined)now=action.at;\n"
    "for(let i=0;i<(action.repeat||1);i++){now+=action.step||0;\n"
    "if(action.call)context.Module[action.call](...(action.args||[]));}\n"
    "if(action.event)listeners[action.event]({preventDefault(){prevented=true;}"
    "});\n"
    "if(action.deviceError)device.onuncapturederror({error:{message:action."
    "deviceError}});\n"
    "if(action.deviceLost){lost({reason:action.deviceLost});await "
    "Promise.resolve();}\n"
    "if(action.snapshot)snapshots.push(snapshot());}\n"
    "console.log(JSON.stringify({...snapshot(),adapters,devices,scripts,"
    "prevented,"
    "adopted:context.Module.preinitializedWebGPUDevice===device,snapshots}));\n"
    "})().catch(error=>{console.error(error);process.exitCode=1;});\n";

static bool run_player_shell(const char *input)
{
    tool_write(driver_path, player_shell_driver);
    char source[1024];
    snprintf(source, sizeof source, "%s/src/host/web/index.html", tool_root());
    const char *argv[] = { "node", driver_path, source, input, NULL };
    if (tool_run(argv, tool_root(), log_path)) {
        return false;
    }
    char *output = tool_read(log_path);
    yyjson_doc_free(result);
    result = yyjson_read(output, strlen(output), 0);
    free(output);
    return result != NULL;
}

TEST_CASE(player_diagnostics_are_opt_in_and_webgl_override_skips_device_request)
{
    REQUIRE(run_player_shell("{\"search\":\"\"}"));
    REQUIRE(yyjson_get_bool(field("hidden")));
    REQUIRE(!yyjson_get_bool(field("hasFrame")));
    REQUIRE(!yyjson_get_bool(field("asyncify")));
    REQUIRE(yyjson_get_int(field("adapters")) == 1);
    REQUIRE(yyjson_get_int(field("devices")) == 1);
    REQUIRE(yyjson_get_bool(field("adopted")));
    REQUIRE(yyjson_arr_size(field("scripts")) == 1);
    REQUIRE(!strcmp(yyjson_get_str(yyjson_arr_get(field("scripts"), 0)), "chibi-player.js"));

    REQUIRE(run_player_shell("{\"search\":\"?renderer=webgl\"}"));
    REQUIRE(yyjson_get_bool(field("hidden")));
    REQUIRE(!yyjson_get_bool(field("hasFrame")));
    REQUIRE(yyjson_get_int(field("adapters")) == 0);
    REQUIRE(yyjson_get_int(field("devices")) == 0);
    REQUIRE(!yyjson_get_bool(field("adopted")));
    REQUIRE(yyjson_arr_size(field("scripts")) == 1);
    return true;
}

TEST_CASE(player_diagnostics_measure_completed_frames_after_startup)
{
    REQUIRE(run_player_shell(
        "{\"search\":\"?diagnostics=1\",\"actions\":["
        "{\"call\":\"chibiRenderer\",\"args\":[\"WebGL 2\",true]},"
        "{\"at\":5000,\"call\":\"chibiFrame\",\"snapshot\":true},"
        "{\"call\":\"chibiFrame\",\"repeat\":60,\"step\":16.666666666667}]}"
    ));
    REQUIRE(yyjson_get_bool(field("hasFrame")));
    REQUIRE(!yyjson_get_bool(field("hidden")));
    /* A requested device does not determine the renderer the runtime selected. */
    REQUIRE(yyjson_get_bool(field("adopted")));
    REQUIRE(!strcmp(text("renderer"), "WebGL 2"));
    REQUIRE(yyjson_get_bool(field("asyncify")));
    REQUIRE(strstr(text("text"), "Asyncify on"));
    REQUIRE(yyjson_get_num(yyjson_obj_get(yyjson_arr_get(field("snapshots"), 0), "fps")) == 0);
    REQUIRE(yyjson_get_num(field("fps")) > 59.9);
    REQUIRE(yyjson_get_num(field("fps")) < 60.1);
    REQUIRE(yyjson_get_num(field("frameMs")) > 16.6);
    REQUIRE(yyjson_get_num(field("frameMs")) < 16.7);
    REQUIRE(strstr(text("text"), "WebGL 2"));
    REQUIRE(strstr(text("text"), "960 ") && strstr(text("text"), "540"));
    REQUIRE(strstr(text("text"), "320 ") && strstr(text("text"), "180"));
    REQUIRE(strstr(text("text"), "DPR 3"));
    return true;
}

TEST_CASE(player_diagnostics_show_device_errors_and_loss)
{
    REQUIRE(run_player_shell(
        "{\"search\":\"?diagnostics=1\",\"actions\":["
        "{\"call\":\"chibiRenderer\",\"args\":[\"WebGPU\"]},"
        "{\"deviceError\":\"test validation failure\"}]}"
    ));
    REQUIRE(!yyjson_get_bool(field("hidden")));
    REQUIRE(!strcmp(text("renderer"), "WebGPU"));
    REQUIRE(!strcmp(text("error"), "test validation failure"));
    REQUIRE(strstr(text("text"), "test validation failure"));

    REQUIRE(run_player_shell(
        "{\"search\":\"?diagnostics=1\",\"actions\":["
        "{\"call\":\"chibiRenderer\",\"args\":[\"WebGPU\"]},"
        "{\"deviceLost\":\"unknown\"}]}"
    ));
    REQUIRE(!yyjson_get_bool(field("hidden")));
    REQUIRE(!yyjson_get_bool(field("statusHidden")));
    REQUIRE(strstr(text("error"), "Graphics device lost"));
    REQUIRE(!strcmp(text("error"), text("statusText")));
    REQUIRE(yyjson_get_num(field("fps")) == 0);
    return true;
}

TEST_CASE(player_diagnostics_show_context_loss_and_player_stop)
{
    REQUIRE(run_player_shell(
        "{\"search\":\"?diagnostics=1&renderer=webgl\",\"actions\":["
        "{\"call\":\"chibiRenderer\",\"args\":[\"WebGL 2\"]},"
        "{\"event\":\"webglcontextlost\"}]}"
    ));
    REQUIRE(yyjson_get_bool(field("prevented")));
    REQUIRE(!yyjson_get_bool(field("hidden")));
    REQUIRE(!yyjson_get_bool(field("statusHidden")));
    REQUIRE(strstr(text("error"), "Graphics context lost"));
    REQUIRE(!strcmp(text("error"), text("statusText")));
    REQUIRE(yyjson_get_num(field("fps")) == 0);

    REQUIRE(run_player_shell(
        "{\"search\":\"?diagnostics=1\",\"actions\":["
        "{\"call\":\"printErr\",\"args\":[\"test render failure\"]},"
        "{\"call\":\"chibiStopped\"}]}"
    ));
    REQUIRE(!yyjson_get_bool(field("hidden")));
    REQUIRE(!yyjson_get_bool(field("statusHidden")));
    REQUIRE(!strcmp(text("error"), "test render failure"));
    REQUIRE(!strcmp(text("error"), text("statusText")));
    REQUIRE(yyjson_get_num(field("fps")) == 0);
    return true;
}

TEST_CASE(player_profile_separates_frame_work_from_callback_gaps)
{
    REQUIRE(run_player_shell("{\"search\":\"?diagnostics=1\"}"));
    REQUIRE(!yyjson_get_bool(field("timingEnabled")));
    REQUIRE(run_player_shell(
        "{\"search\":\"?profile=1&dpr=1\",\"actions\":["
        "{\"call\":\"chibiFrameTimings\",\"args\":[{\"startMs\":100,\"endMs\":"
        "110,"
        "\"totalMs\":10,\"updateMs\":2,\"simulationMs\":3,\"drawMs\":4,"
        "\"presentMs\":0.5,\"otherMs\":0.5}]},"
        "{\"call\":\"chibiFrameTimings\",\"args\":[{\"startMs\":125,\"endMs\":"
        "145,"
        "\"totalMs\":20,\"updateMs\":1,\"simulationMs\":2,\"drawMs\":3,"
        "\"presentMs\":4,\"otherMs\":10}]}]}"
    ));
    REQUIRE(yyjson_get_bool(field("timingEnabled")));
    REQUIRE(!yyjson_get_bool(field("hidden")));
    yyjson_val *timing = field("timings");
    REQUIRE(yyjson_get_num(yyjson_obj_get(timing, "totalMs")) == 15);
    REQUIRE(yyjson_get_num(yyjson_obj_get(timing, "gapMs")) == 15);
    REQUIRE(yyjson_get_num(yyjson_obj_get(timing, "simulationMs")) == 2.5);
    REQUIRE(yyjson_get_num(yyjson_obj_get(timing, "drawMs")) == 3.5);
    REQUIRE(strstr(text("text"), "Main-thread wall"));
    REQUIRE(strstr(text("text"), "Between callbacks"));
    REQUIRE(strstr(text("text"), "requested 1"));
    yyjson_val *report = field("report");
    REQUIRE(yyjson_get_int(yyjson_obj_get(report, "requestedDpr")) == 1);
    yyjson_val *samples = yyjson_obj_get(report, "frames");
    REQUIRE(yyjson_arr_size(samples) == 2);
    REQUIRE(yyjson_is_null(yyjson_obj_get(yyjson_arr_get(samples, 0), "gapMs")));
    REQUIRE(yyjson_get_num(yyjson_obj_get(yyjson_arr_get(samples, 1), "gapMs")) == 15);
    return true;
}

TEST_CASE(player_profile_bounds_report_history)
{
    REQUIRE(run_player_shell(
        "{\"search\":\"?profile=1\",\"actions\":["
        "{\"call\":\"chibiFrameTimings\",\"repeat\":130,\"args\":[{"
        "\"startMs\":10,\"endMs\":20,\"totalMs\":10,\"updateMs\":2,"
        "\"simulationMs\":3,\"drawMs\":4,\"presentMs\":0.5,\"otherMs\":0.5}]}]}"
    ));
    yyjson_val *report = field("report");
    REQUIRE(yyjson_arr_size(yyjson_obj_get(report, "frames")) == 120);
    REQUIRE(yyjson_get_int(yyjson_obj_get(report, "totalFrames")) == 130);
    REQUIRE(yyjson_is_null(yyjson_obj_get(report, "requestedDpr")));
    return true;
}

void test_tool_web_storage()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(published_source_opens_instead_of_starter);
    RUN_TEST_CASE(reopening_a_revision_preserves_edits_without_redownloading);
    RUN_TEST_CASE(new_revision_retains_the_previous_workspace);
    RUN_TEST_CASE(explicit_workspace_overrides_published_default);
    RUN_TEST_CASE(explicit_sample_overrides_published_default);
    RUN_TEST_CASE(new_project_overrides_published_default);
    RUN_TEST_CASE(invalid_manifest_stops_startup);
    RUN_TEST_CASE(workspace_without_marker_is_preserved);
    RUN_TEST_CASE(failed_import_is_removed);
    RUN_TEST_CASE(checksum_failure_stops_startup);
    RUN_TEST_CASE(download_failure_stops_startup);
    RUN_TEST_CASE(archive_cannot_escape_its_workspace);
    RUN_TEST_CASE(browser_export_does_not_require_native_process_apis);
    RUN_TEST_CASE(canvas_layout_changes_notify_the_platform_resize_handler);
    RUN_TEST_CASE(player_diagnostics_are_opt_in_and_webgl_override_skips_device_request);
    RUN_TEST_CASE(player_diagnostics_measure_completed_frames_after_startup);
    RUN_TEST_CASE(player_diagnostics_show_device_errors_and_loss);
    RUN_TEST_CASE(player_diagnostics_show_context_loss_and_player_stop);
    RUN_TEST_CASE(player_profile_separates_frame_work_from_callback_gaps);
    RUN_TEST_CASE(player_profile_bounds_report_history);
    test_tool_web_gpu_profile();
    pu_setup(NULL, NULL);
}
