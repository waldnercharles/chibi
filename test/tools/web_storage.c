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
static const char *driver[] = {
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
    "scene.kdl'))throw Error('disk full');"
    "if(mode==='create-failure'&&p==='/workspace/new-game-2/content/prototypes/"
    "Light.kdl')"
    "throw Error('disk full');files.set(p,typeof "
    "data==='string'?new "
    "TextEncoder().encode(data):new Uint8Array(data));},\n"
    " readFile(p,opt){if(!files.has(p))throw Error('missing '+p);return "
    "opt?.encoding?new TextDecoder().decode(files.get(p)):files.get(p);},\n"
    " readdir(p){return ['.','..',...new "
    "Set([...files.keys(),...dirs].filter(x=>x.startsWith(p+'/"
    "')).map(x=>x.slice(p.length+1).split('/')[0]))];},\n"
    " unlink:p=>files.delete(p),rmdir:p=>dirs.delete(p)};\n"
    "const "
    "starterFiles=['scenes/Start.kdl','prototypes/Cube.kdl','prototypes/"
    "Camera.kdl','prototypes/Light.kdl'];\n"
    "const "
    "starter=host.realpathSync(require('node:path').dirname(process.argv[2])+'/"
    "../projects/starter/content');\n"
    "for(const name of starterFiles){const "
    "p='/projects/starter/content/"
    "'+name;FS.mkdirTree(p.slice(0,p.lastIndexOf('/')));"
    "FS.writeFile(p,host.readFileSync(starter+'/'+name));}\n",
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
    "{root:Module.arguments[1],aborted,released,create:Module."
    "chibiCreateProject};\n"
    "}\n",
    "(async()=>{\n"
    " if(mode.startsWith('create')){FS.mkdirTree('/workspace/new-game/"
    "content');"
    "FS.writeFile('/workspace/new-game/project.kdl','name Existing');"
    "FS.writeFile('/workspace/new-game/content/notes','keep me');}\n"
    " if(mode==='explicit'){FS.mkdirTree('/workspace/saved');FS.writeFile('/"
    "workspace/saved/project.kdl','name Saved');}\n"
    " if(mode==='conflict'){FS.mkdirTree('/workspace/"
    "published-celeste-'+revision);FS.writeFile('/workspace/"
    "published-celeste-'+revision+'/project.kdl','name Existing');}\n"
    " let outcome=await "
    "launch(mode==='explicit'?'?project=saved':mode==='sample'?'?project-root="
    "projects/"
    "starter':mode==='new'?'?new':mode.startsWith('create')?'?project-root="
    "projects/starter':'');\n"
    " if(mode==='repeat'||mode==='revision'){FS.writeFile(outcome.root+'"
    "content/scene.kdl','edited');\n"
    " if(mode==='revision')revision='b'.repeat(40);outcome=await launch();}\n"
    " const text=p=>files.has(p)?FS.readFile(p,{encoding:'utf8'}):'';\n"
    " let created={};if(mode.startsWith('create')){"
    "FS.writeFile(outcome.root+'content/scenes/Start.kdl','user-edited scene');"
    "FS.writeFile(outcome.root+'content/prototypes/Cube.kdl','user-edited "
    "cube');"
    "created=JSON.parse(outcome.create('New Game',21));}\n"
    " const newRoot='/workspace/new-game-2/';\n"
    " console.log(JSON.stringify({...outcome,created,downloads,createdProject:"
    "text(newRoot+'project.kdl'),"
    "scene:text(newRoot+'content/scenes/Start.kdl'),cube:text(newRoot+'content/"
    "prototypes/Cube.kdl'),"
    "camera:text(newRoot+'content/prototypes/"
    "Camera.kdl'),light:text(newRoot+'content/prototypes/Light.kdl'),"
    "copied:starterFiles.every(name=>text(newRoot+'content/'+name)===text('/"
    "projects/starter/content/'+name)),"
    "createdPartial:[...files.keys(),...dirs].some(path=>path===newRoot.slice("
    "0,-1)||path.startsWith(newRoot)),"
    "existing:text('/workspace/new-game/project.kdl'),notes:text('/workspace/"
    "new-game/content/notes'),"
    "content:text(outcome."
    "root+'content/scene.kdl'),\n"
    " previous:text('/workspace/published-celeste-'+ "
    "'a'.repeat(40)+'/content/scene.kdl'),\n"
    " project:text(outcome.root+'project.kdl'),outside:files.has('/workspace/"
    "outside'),partial:dirs.has('/workspace/published-celeste-'+revision)}));\n"
    "})().catch(error=>{console.error(error);process.exitCode=1;});\n"
};

static void setup()
{
    strcpy(directory, "/tmp/chibi-web-storage-XXXXXX");
    assert(mkdtemp(directory));
    snprintf(driver_path, sizeof driver_path, "%s/driver.cjs", directory);
    snprintf(log_path, sizeof log_path, "%s/output.json", directory);
    FILE *file = fopen(driver_path, "wb");
    assert(file);
    for (int i = 0; i < (int)(sizeof driver / sizeof *driver); i++) {
        assert(fputs(driver[i], file) >= 0);
    }
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

TEST_CASE(new_browser_projects_copy_the_bundled_3d_starter)
{
    REQUIRE(run("create"));
    yyjson_val *created = field("created");
    REQUIRE(yyjson_is_str(yyjson_obj_get(created, "root")));
    REQUIRE(!strcmp(yyjson_get_str(yyjson_obj_get(created, "root")), "/workspace/new-game-2/"));
    REQUIRE(strstr(text("createdProject"), "name \"New Game\""));
    REQUIRE(strstr(text("scene"), "presentation \"3d\""));
    REQUIRE(strstr(text("scene"), "name=\"Cube\" prototype=\"Cube\""));
    REQUIRE(strstr(text("scene"), "name=\"Camera\" prototype=\"Camera\""));
    REQUIRE(strstr(text("scene"), "name=\"Light\" prototype=\"Light\""));
    REQUIRE(strstr(text("cube"), "Geometry3D"));
    REQUIRE(strstr(text("camera"), "Camera"));
    REQUIRE(strstr(text("light"), "DirectionalLight"));
    REQUIRE(yyjson_get_bool(field("copied")));
    REQUIRE(!strcmp(text("existing"), "name Existing") && !strcmp(text("notes"), "keep me"));
    return true;
}

TEST_CASE(failed_browser_project_creation_removes_all_partial_files)
{
    REQUIRE(run("create-failure"));
    REQUIRE(yyjson_is_str(yyjson_obj_get(field("created"), "error")));
    REQUIRE(strstr(yyjson_get_str(yyjson_obj_get(field("created"), "error")), "disk full"));
    REQUIRE(!yyjson_get_bool(field("createdPartial")));
    REQUIRE(!strcmp(text("existing"), "name Existing") && !strcmp(text("notes"), "keep me"));
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

void test_tool_web_storage()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(published_source_opens_instead_of_starter);
    RUN_TEST_CASE(reopening_a_revision_preserves_edits_without_redownloading);
    RUN_TEST_CASE(new_revision_retains_the_previous_workspace);
    RUN_TEST_CASE(explicit_workspace_overrides_published_default);
    RUN_TEST_CASE(explicit_sample_overrides_published_default);
    RUN_TEST_CASE(new_project_overrides_published_default);
    RUN_TEST_CASE(new_browser_projects_copy_the_bundled_3d_starter);
    RUN_TEST_CASE(failed_browser_project_creation_removes_all_partial_files);
    RUN_TEST_CASE(invalid_manifest_stops_startup);
    RUN_TEST_CASE(workspace_without_marker_is_preserved);
    RUN_TEST_CASE(failed_import_is_removed);
    RUN_TEST_CASE(checksum_failure_stops_startup);
    RUN_TEST_CASE(download_failure_stops_startup);
    RUN_TEST_CASE(archive_cannot_escape_its_workspace);
    RUN_TEST_CASE(browser_export_does_not_require_native_process_apis);
    pu_setup(NULL, NULL);
}
