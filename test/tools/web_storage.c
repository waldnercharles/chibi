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

/* Node runs the production browser bridge with only browser/FS seams replaced.
 * Scenarios and assertions belong to the C dispatcher, not a second JS suite. */
static const char *driver =
    "const vm=require('node:vm'), host=require('node:fs');\n"
    "const code=host.readFileSync(process.argv[2],'utf8'), "
    "mode=process.argv[3];\n"
    "const "
    "saving=mode.startsWith('save-'),shell=host.readFileSync(process.argv[2]"
    ".replace(/web_storage\\.js$/,'web_shell.html'),'utf8').match(/"
    "<script>([\\s\\S]*?)<\\/script>/)[1];\n"
    "const files=new Map(), dirs=new "
    "Set(['/','/projects','/projects/starter']);\n"
    "files.set('/projects/starter/project.chibi',new "
    "TextEncoder().encode('name "
    "Starter'));\n"
    "let held=false,active,maxSyncs=0;const durable=new Map(),syncs=[];\n"
    "const finish=(job,error)=>{if(!error){durable.clear();for(const [p,data] "
    "of job.files)"
    "durable.set(p,data);}job.done(error);};\n"
    "const FS={filesystems:{IDBFS:{}},mount(){},syncfs(populate,done){"
    "if(populate){queueMicrotask(()=>done(null));return;}"
    "const job={done,files:new Map(files)};if(held){syncs.push(job);"
    "maxSyncs=Math.max(maxSyncs,syncs.length);return;}queueMicrotask(()=>"
    "finish(job,null));},\n"
    " analyzePath:p=>({exists:files.has(p)||dirs.has(p)}),stat:p=>({mode:dirs."
    "has(p)?1:0}),isDir:m=>m===1,\n"
    " mkdirTree(p){let base='';for(const part of "
    "p.split('/').filter(Boolean)){base+='/'+part;dirs.add(base);}},\n"
    " writeFile(p,data){if(mode==='write-failure'&&p.endsWith('/content/"
    "scene.chibi'))throw Error('disk full');files.set(p,typeof "
    "data==='string'?new "
    "TextEncoder().encode(data):new Uint8Array(data));},\n"
    " readFile(p,opt){if(!files.has(p))throw Error('missing '+p);return "
    "opt?.encoding?new TextDecoder().decode(files.get(p)):files.get(p);},\n"
    " readdir(p){return ['.','..',...new "
    "Set([...files.keys(),...dirs].filter(x=>x.startsWith(p+'/"
    "')).map(x=>x.slice(p.length+1).split('/')[0]))];},\n"
    " unlink:p=>files.delete(p),rmdir:p=>dirs.delete(p)};\n"
    "let downloads=0,revision='a'.repeat(40),checksum,archive;\n";

static const char *driver_launch =
    "async launch :: fn(search=''){\n"
    " let ready,aborted='',released=false;const done=new Promise(r=>ready=r);\n"
    " const "
    "location={origin:'https://example.test',href:'https://example.test/game/"
    "dev/'+search,search};\n"
    " const handlers={},inputs=[],timers=new "
    "Map(),status={dataset:{},hidden:true,buttons:[],"
    "set textContent(text){this.text=text;this.buttons=[];},get "
    "textContent(){return this.text||'';},"
    "append(button){this.buttons.push(button);}},"
    "canvas={addEventListener(){}};let nextTimer=0;\n"
    " const "
    "context={location,window:{location,addEventListener(name,run){handlers["
    "name]=run;}},"
    "document:{getElementById:id=>id==='status'?status:canvas,createElement:"
    "tag=>{"
    "const element={click(){}};if(tag==='input')inputs.push(element);return "
    "element;}},console,"
    "navigator:{storage:{}},history:"
    "{replaceState(){}},\n"
    " URL,URLSearchParams,TextEncoder,TextDecoder,Uint8Array,DataView,"
    "ArrayBuffer,Blob,Response,\n"
    " crypto:require('node:crypto').webcrypto,AbortSignal,"
    "setTimeout(run){timers.set(++nextTimer,run);return nextTimer;},"
    "clearTimeout:id=>timers.delete(id),\n"
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
    " vm.createContext(context);vm.runInContext(shell,context);const "
    "Module=context.Module;"
    "Object.assign(Module,{FS,chibiPublishedProject:'project.json'});"
    "vm.runInContext(code,context);\n"
    " if(!archive){archive=await Module.chibiZipFiles([['project.chibi',new "
    "TextEncoder().encode('name Celeste')],\n"
    " [mode==='traversal'?'../outside':'content/scene.chibi',new "
    "TextEncoder().encode('original')]]).arrayBuffer();\n"
    " checksum=require('node:crypto').createHash('sha256').update(Buffer.from("
    "archive)).digest('hex');}\n"
    " Module.preRun[0]();await "
    "done;Module.setStatus('');active={Module,handlers,inputs,timers,status,"
    "location};"
    "return "
    "{root:Module.arguments[1],aborted,released};\n"
    "}\n"
    "async startImport :: fn(){const "
    "{Module,inputs}=active,encode=text=>new TextEncoder().encode(text);"
    "const buffer=await Module.chibiZipFiles([['project.chibi',encode('name "
    "Imported')],"
    "['content/scene.chibi',encode('imported')]]).arrayBuffer();"
    "Module.chibiImportProject();const input=inputs.at(-1);"
    "input.files=[{name:'imported.zip',arrayBuffer:async()=>buffer}];"
    "active.importing=input.onchange();return '/workspace/imported/';}\n";

static const char *driver_run =
    "(async()=>{\n"
    " if(mode==='explicit'||saving){FS.mkdirTree('/workspace/"
    "saved');FS.writeFile('/"
    "workspace/saved/project.chibi','name Saved');}\n"
    " if(saving){FS.writeFile('/workspace/.last','saved');"
    "FS.writeFile('/workspace/saved/content/scene.chibi','original');"
    "for(const [p,data] of files)durable.set(p,data);}\n"
    " if(mode==='conflict'){FS.mkdirTree('/workspace/"
    "published-celeste-'+revision);FS.writeFile('/workspace/"
    "published-celeste-'+revision+'/project.chibi','name Existing');}\n"
    " let outcome=await "
    "launch(mode==='explicit'||saving?'?project=saved':mode==='sample'?'?"
    "project-root="
    "projects/starter':mode==='new'?'?new':'');\n"
    " if(mode==='repeat'||mode==='revision'){FS.writeFile(outcome.root+'"
    "content/scene.chibi','edited');\n"
    " if(mode==='revision')revision='b'.repeat(40);outcome=await launch();}\n"
    " if(saving){\n"
    " const "
    "{Module,handlers,timers,status}=active,path=outcome.root+'content/"
    "scene.chibi';\n"
    " const warning=()=>{const "
    "event={defaultPrevented:false,preventDefault(){this.defaultPrevented=true;"
    "}};"
    "handlers.beforeunload(event);return event.defaultPrevented;};\n"
    " const timer=()=>{const "
    "pending=[...timers.values()];timers.clear();for(const run of "
    "pending)run();};\n"
    " const settle=async()=>{for(let i=0;i<12;i++)await Promise.resolve();};\n"
    " const saved=()=>new TextDecoder().decode(durable.get(path));\n"
    " const "
    "save=text=>{Module.chibiUnsaved=true;Module.chibiSaveAsset(path,text);"
    "FS.writeFile(path,text);Module.chibiUnsaved=false;};\n"
    " held=true;let created='';if(mode==='save-import-failure'){created=await "
    "startImport();await settle();}"
    "else if(mode==='save-open-failure'){"
    "created=JSON.parse(Module.chibiCreateProject('Created',1)).root;"
    "Module.chibiOpenProject(created,false);}else save('edited');const "
    "before=warning(),pendingStatus=status.textContent;"
    "timer();await settle();const during=warning();\n"
    " let retained=false,failure='',first='',afterFirst=false,retryLabel='';\n"
    " if(mode==='save-race'){save('second');timer();await settle();}\n"
    " if(mode==='save-completion-race')queueMicrotask(()=>queueMicrotask(()=>{"
    "save('second');Module.chibiOpenProject(outcome.root);}));\n"
    " finish(syncs.shift(),mode.endsWith('-failure')?Error('disk "
    "full'):null);await settle();await active.importing;\n"
    " if(mode==='save-race'||mode==='save-completion-race'){first=saved();"
    "afterFirst=warning();if(syncs.length)finish(syncs."
    "shift(),null);await settle();}\n"
    " const "
    "openStatus=Module.chibiProjectOpenStatus(),opened=active.location.search,"
    "createdBefore=created?durable.has(created+'project.chibi'):false,fatal='"
    "error' in status.dataset;\n"
    " if(mode.endsWith('-failure')){retained=warning();failure=status."
    "textContent;"
    "first=saved();"
    "const retry=status.buttons[0];retryLabel=retry?.textContent||'';"
    "if(retry){retry.onclick();await "
    "settle();finish(syncs.shift(),null);}await "
    "settle();}\n"
    " console.log(JSON.stringify({before,during,pendingStatus,after:warning(),"
    "saved:saved(),"
    "status:status.textContent,maxSyncs,retained,failure,first,afterFirst,"
    "retryLabel,openStatus,opened,createdBefore,createdAfter:created?durable."
    "has(created+'project.chibi'):false,fatal,openedAfter:active.location."
    "search,"
    "createdContent:created?new "
    "TextDecoder().decode(durable.get(created+'content/scene.chibi')):''}));"
    "return;\n"
    " }\n"
    " const text=p=>files.has(p)?FS.readFile(p,{encoding:'utf8'}):'';\n"
    " console.log(JSON.stringify({...outcome,downloads,content:text(outcome."
    "root+'content/scene.chibi'),\n"
    " previous:text('/workspace/published-celeste-'+ "
    "'a'.repeat(40)+'/content/scene.chibi'),\n"
    " project:text(outcome.root+'project.chibi'),outside:files.has('/workspace/"
    "outside'),partial:dirs.has('/workspace/published-celeste-'+revision)}));\n"
    "})().catch(error=>{console.error(error);process.exitCode=1;});\n";

static void setup()
{
    strcpy(directory, "/tmp/chibi-web-storage-XXXXXX");
    assert(mkdtemp(directory));
    snprintf(driver_path, sizeof driver_path, "%s/driver.cjs", directory);
    snprintf(log_path, sizeof log_path, "%s/output.json", directory);
    FILE *file = fopen(driver_path, "w");
    assert(file);
    assert(fputs(driver, file) >= 0);
    assert(fputs(driver_launch, file) >= 0);
    assert(fputs(driver_run, file) >= 0);
    assert(!fclose(file));
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

TEST_CASE(saved_document_warns_before_unload_until_persistence_completes)
{
    REQUIRE(run("save-reload"));
    REQUIRE(yyjson_get_bool(field("before")));
    REQUIRE(yyjson_get_bool(field("during")));
    REQUIRE(!strcmp(text("pendingStatus"), "Saving project..."));
    REQUIRE(!yyjson_get_bool(field("after")));
    REQUIRE(!strcmp(text("saved"), "edited"));
    REQUIRE(!*text("status"));
    return true;
}

TEST_CASE(save_during_persistence_waits_for_the_newer_contents)
{
    REQUIRE(run("save-race"));
    REQUIRE(yyjson_get_bool(field("before")));
    REQUIRE(yyjson_get_bool(field("afterFirst")));
    REQUIRE(!strcmp(text("first"), "edited"));
    REQUIRE(yyjson_get_int(field("maxSyncs")) == 1);
    REQUIRE(!yyjson_get_bool(field("after")));
    REQUIRE(!strcmp(text("saved"), "second"));
    return true;
}

TEST_CASE(save_after_flush_completion_starts_a_new_flush_before_opening)
{
    REQUIRE(run("save-completion-race"));
    REQUIRE(!strcmp(text("first"), "edited"));
    REQUIRE(yyjson_get_bool(field("afterFirst")));
    REQUIRE(!yyjson_get_bool(field("after")));
    REQUIRE(!strcmp(text("saved"), "second"));
    return true;
}

TEST_CASE(failed_persistence_warns_until_a_successful_retry)
{
    REQUIRE(run("save-failure"));
    REQUIRE(yyjson_get_bool(field("retained")));
    REQUIRE(strstr(text("failure"), "disk full"));
    REQUIRE(!strcmp(text("retryLabel"), "Retry"));
    REQUIRE(!strcmp(text("first"), "original"));
    REQUIRE(!yyjson_get_bool(field("after")));
    REQUIRE(!strcmp(text("saved"), "edited"));
    REQUIRE(!*text("status"));
    return true;
}

TEST_CASE(created_project_failure_keeps_storage_retry_after_inline_error)
{
    REQUIRE(run("save-open-failure"));
    REQUIRE(yyjson_get_bool(field("retained")));
    REQUIRE(strstr(text("failure"), "disk full"));
    REQUIRE(!strcmp(text("retryLabel"), "Retry"));
    REQUIRE(!strcmp(text("openStatus"), "error:disk full"));
    REQUIRE(!strcmp(text("opened"), "?project=saved"));
    REQUIRE(!yyjson_get_bool(field("createdBefore")));
    REQUIRE(yyjson_get_bool(field("createdAfter")));
    REQUIRE(!yyjson_get_bool(field("after")));
    REQUIRE(!*text("status"));
    return true;
}

TEST_CASE(imported_project_failure_keeps_storage_retry)
{
    REQUIRE(run("save-import-failure"));
    REQUIRE(yyjson_get_bool(field("retained")));
    REQUIRE(strstr(text("failure"), "disk full"));
    REQUIRE(!yyjson_get_bool(field("fatal")));
    REQUIRE(!strcmp(text("retryLabel"), "Retry"));
    REQUIRE(!strcmp(text("opened"), "?project=saved"));
    REQUIRE(!strcmp(text("openedAfter"), "?project=saved"));
    REQUIRE(!yyjson_get_bool(field("createdBefore")));
    REQUIRE(yyjson_get_bool(field("createdAfter")));
    REQUIRE(!strcmp(text("createdContent"), "imported"));
    REQUIRE(!yyjson_get_bool(field("after")));
    REQUIRE(!*text("status"));
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
    RUN_TEST_CASE(saved_document_warns_before_unload_until_persistence_completes);
    RUN_TEST_CASE(save_during_persistence_waits_for_the_newer_contents);
    RUN_TEST_CASE(save_after_flush_completion_starts_a_new_flush_before_opening);
    RUN_TEST_CASE(failed_persistence_warns_until_a_successful_retry);
    RUN_TEST_CASE(created_project_failure_keeps_storage_retry_after_inline_error);
    RUN_TEST_CASE(imported_project_failure_keeps_storage_retry);
    RUN_TEST_CASE(browser_export_does_not_require_native_process_apis);
    pu_setup(NULL, NULL);
}
