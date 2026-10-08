#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[1024], bridge[1024], log_path[1024];

static void path(char *out, const char *base, const char *name)
{
    int size = snprintf(out, 1024, "%s/%s", base, name);
    assert(size > 0 && size < 1024);
}

/* Only process/network seams are replaced. Identity, cache validation, locking,
 * snapshot selection, staging and publication decisions run production code. */
static const char *driver =
    "import importlib.util,json,os,shutil,subprocess,sys,time,fcntl\n"
    "from pathlib import Path\n"
    "root=Path(sys.argv[2]); mode=sys.argv[3]\n"
    "sys.path.insert(0,str(Path(sys.argv[1]).parent))\n"
    "spec=importlib.util.spec_from_file_location('publisher',sys.argv[1])\n"
    "p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)\n"
    "which=shutil.which\n"
    "p.shutil.which=lambda name,path=None: str(root/'compiler') if name in "
    "('emcc','cmake') else which(name,path=path)\n"
    "def event(name):\n"
    " with (root/'events').open('a') as f: f.write(name+'\\n')\n"
    "def copy(a,b):\n"
    " if b.exists(): shutil.rmtree(b)\n"
    " shutil.copytree(a,b)\n"
    "def run(*args,cwd=None,capture=False,env=None):\n"
    " cwd=Path(cwd) if cwd else root\n"
    " if args[0]=='gh':\n"
    "  if args[1]=='repo': return 'fixture/repo'\n"
    "  if 'POST' in args: event('post');return '{}'\n"
    "  return "
    "json.dumps({'build_type':'legacy','source':{'branch':'gh-pages','path':'/"
    "'},'html_url':'https://invalid.test/'})\n"
    " if '--version' in args: return 'fixture version 1'\n"
    " if args[0]=='xcrun': return 'fixture-sdk'\n"
    " if args[0]=='xcode-select': return 'fixture-developer'\n"
    " if Path(args[0]).name=='em-config': return str(root/'llvm/bin')\n"
    " if args[0]=='git':\n"
    "  if args[1:4]==('remote','get-url','origin'): return str(root/'remote')\n"
    "  if args[1]=='clone':\n"
    "   source=root/('site' if 'gh-pages' in args else 'engine')\n"
    "   copy(source,Path(args[-1]));return\n"
    "  if args[1]=='rev-parse': return 'a'*40\n"
    "  if args[1]=='ls-remote': return ('b' if (root/'stale').exists() else "
    "'a')*40+' refs/heads/main'\n"
    "  if args[1]=='push':\n"
    "   if any('force' in a or a.startswith('+') for a in args): raise "
    "RuntimeError('force push')\n"
    "   with (root/'site-lock').open('w') as lock:\n"
    "    fcntl.flock(lock,fcntl.LOCK_EX)\n"
    "    "
    "old=(cwd/'revision').read_text();current=(root/'site/"
    "revision').read_text()\n"
    "    if old!=current: raise subprocess.CalledProcessError(1,args)\n"
    "    "
    "(cwd/'revision').write_text(str(int(old)+1));copy(cwd,root/"
    "'site');event('push')\n"
    "   return\n"
    "  return\n"
    " if args[0]=='make':\n"
    "  if 'package-player' in args: raise RuntimeError('unexpected native "
    "package')\n"
    "  if env.get('CCACHE_DISABLE')=='1': raise RuntimeError('disabled "
    "compiler cache')\n"
    "  if env.get('CCACHE_BASEDIR')!=str(cwd): raise "
    "RuntimeError('unnormalized snapshot')\n"
    "  if (cwd/'dirty').exists(): raise RuntimeError('dirty snapshot')\n"
    "  event(args[1]);time.sleep(.15)\n"
    "  out=cwd/('build-web-threads' if args[1]=='web-threads' else "
    "'build-web');out.mkdir(exist_ok=True)\n"
    "  for name in ('chibi.html','chibi.js','chibi.wasm','chibi.data'): "
    "(out/name).write_text(name+' committed')\n"
    "  return\n"
    " if args[0]=='python3':\n"
    "  event('package');out=Path(args[args.index('--output')+1]);out.mkdir()\n"
    "  build=Path(args[args.index('--serial')+1])\n"
    "  for item in build.iterdir(): shutil.copyfile(item,out/item.name)\n"
    "  "
    "(out/'index.html').write_text('launcher');(out/"
    "'.nojekyll').write_text('')\n"
    "  if '--threads' in args:\n"
    "   copy(Path(args[args.index('--threads')+1]),out/'threads')\n"
    "  return\n"
    " raise RuntimeError(args)\n";
static const char *driver_setup =
    "p.run=run\n"
    "real_process=p.subprocess.run\n"
    "def process(args,**kw):\n"
    " if args[:3]==['emcmake','cmake','--version']:\n"
    "  return "
    "subprocess.CompletedProcess(args,0,stdout='version',stderr='-DCMAKE_"
    "TOOLCHAIN_FILE='+str(root/'emscripten/cmake/Modules/Platform/"
    "Emscripten.cmake'))\n"
    " if args[:3]==['git','diff','--cached']: return "
    "subprocess.CompletedProcess(args,1)\n"
    " return real_process(args,**kw)\n"
    "p.subprocess.run=process\n"
    "def wait(url,commit,hashes):\n"
    " if p.file_hashes(root/'site',hashes)!=hashes: raise RuntimeError('live "
    "bytes differ')\n"
    " event('wait')\n"
    "p.wait_for_deployment=wait\n"
    "os.environ.pop('CCACHE_DISABLE',None)\n"
    "os.environ['CC']=sys.executable;os.environ['CXX']=sys.executable\n"
    "os.environ['EM_CONFIG']=str(root/'emscripten-config')\n"
    "if mode=='identity':\n"
    " env=p.build_environment(root)\n"
    " rows=[p.build_identity('a',{},'',False,env)[0],p.build_identity('b',{},''"
    ",False,env)[0],\n"
    " p.build_identity('a',{'projects/"
    "game':'b'},'',False,env)[0],p.build_identity('a',{},'wss://"
    "other',False,env)[0],\n"
    " p.build_identity('a',{},'',True,env)[0]]\n"
    " env['CFLAGS']='-DCHANGED';rows.append(p.build_identity('a',{},'',False,"
    "env)[0])\n"
    " env.pop('CFLAGS',None);(root/'compiler').write_text('changed compiler "
    "bytes')\n"
    " rows.append(p.build_identity('a',{},'',False,env)[0])\n"
    " (root/'compiler').write_text('compiler bytes')\n"
    " (root/'emscripten-config').write_text('changed SDK configuration')\n"
    " rows.append(p.build_identity('a',{},'',False,env)[0])\n"
    " (root/'emscripten-config').write_text('SDK configuration')\n"
    " (root/'emscripten/tools/sidecar.py').write_text('changed sidecar')\n"
    " rows.append(p.build_identity('a',{},'',False,env)[0])\n"
    " (root/'identities').write_text('\\n'.join(rows)+'\\n');sys.exit(0)\n"
    "if mode=='concurrent':\n"
    " children=[subprocess.Popen([sys.executable,*sys.argv[:3],'publish']) for "
    "_ in range(2)]\n"
    " statuses=[child.wait() for child in children]\n"
    " sys.exit(0 if 0 in statuses else 1)\n"
    "p.publish(root,1,'',threads=mode=='threads')\n"
    "marker=json.loads((root/'site/BUILD_ARTIFACT.json').read_text())\n"
    "(root/'identity').write_text(marker['identity'])\n";

static int invoke(const char *mode)
{
    char source[1024];
    path(source, tool_root(), "tools/publish_web.py");
    const char *argv[] = { "python3", bridge, source, directory, mode, NULL };
    return tool_run(argv, directory, log_path);
}

static int count(const char *name)
{
    char filename[1024];
    path(filename, directory, "events");
    char *text = tool_read(filename);
    int found = 0;
    if (text) {
        for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
            found += !strcmp(line, name);
        }
    }
    free(text);
    return found;
}

static void reset_site()
{
    char filename[1024];
    path(filename, directory, "site");
    tool_remove_tree(filename);
    assert(!mkdir(filename, 0700));
    path(filename, directory, "site/revision");
    tool_write(filename, "0");
}

TEST_CASE(publication_identity_covers_commits_projects_and_configuration)
{
    REQUIRE(invoke("identity") == 0);
    char filename[1024];
    path(filename, directory, "identities");
    char *text = tool_read(filename);
    REQUIRE(text != NULL);
    char *ids[9];
    int length = 0;
    for (char *id = strtok(text, "\n"); id && length < 9; id = strtok(NULL, "\n")) {
        ids[length++] = id;
    }
    bool unique = length == 9;
    for (int i = 0; i < length; i++) {
        unique &= strlen(ids[i]) == 64;
        for (int j = 0; j < i; j++) {
            unique &= strcmp(ids[i], ids[j]) != 0;
        }
    }
    free(text);
    REQUIRE(unique);
    return true;
}

TEST_CASE(publication_reuses_verified_bytes_and_rebuilds_corruption)
{
    REQUIRE(invoke("publish") == 0);
    REQUIRE(count("web-serial") == 1 && count("package") == 1);
    REQUIRE(count("push") == 1 && count("post") == 1);
    REQUIRE(count("web-threads") == 0);
    REQUIRE(invoke("publish") == 0);
    REQUIRE(count("web-serial") == 1 && count("package") == 1);
    REQUIRE(count("push") == 1 && count("post") == 1 && count("wait") == 2);
    reset_site();
    REQUIRE(invoke("publish") == 0);
    REQUIRE(count("web-serial") == 1 && count("package") == 1 && count("push") == 2);
    char filename[1024], cache[1024];
    path(filename, directory, "identity");
    char *identity = tool_read(filename);
    REQUIRE(identity != NULL);
    int size = snprintf(cache, sizeof cache, ".cache/publish-web/%s/bundle/chibi.js", identity);
    free(identity);
    REQUIRE(size > 0 && size < (int)sizeof cache);
    path(filename, directory, cache);
    tool_write(filename, "corrupt");
    reset_site();
    REQUIRE(invoke("publish") == 0);
    REQUIRE(count("web-serial") == 2 && count("package") == 2 && count("push") == 3);
    path(filename, directory, "stale");
    tool_write(filename, "yes");
    REQUIRE(invoke("publish") != 0);
    REQUIRE(count("push") == 3 && count("post") == 3 && count("wait") == 4);
    assert(!unlink(filename));
    REQUIRE(invoke("threads") == 0);
    REQUIRE(count("web-serial") == 3 && count("web-threads") == 1);
    return true;
}

TEST_CASE(concurrent_publication_fills_each_artifact_once)
{
    char filename[1024];
    path(filename, directory, ".cache");
    tool_remove_tree(filename);
    path(filename, directory, "events");
    tool_write(filename, "");
    reset_site();
    REQUIRE(invoke("concurrent") == 0);
    REQUIRE(count("web-serial") == 1 && count("package") == 1);
    REQUIRE(count("push") == 1 && count("post") == 1);
    return true;
}

void test_tool_publish()
{
    strcpy(directory, "/tmp/chibi-publish-test-XXXXXX");
    assert(mkdtemp(directory));
    path(bridge, directory, "bridge.py");
    path(log_path, directory, "command.log");
    FILE *file = fopen(bridge, "w");
    assert(file);
    assert(fputs(driver, file) >= 0);
    assert(fputs(driver_setup, file) >= 0);
    assert(!fclose(file));
    char filename[1024];
    path(filename, directory, "compiler");
    tool_write(filename, "compiler bytes");
    path(filename, directory, "emscripten-config");
    tool_write(filename, "SDK configuration");
    const char *folders[] = { "emscripten",
                              "emscripten/cmake",
                              "emscripten/cmake/Modules",
                              "emscripten/cmake/Modules/Platform",
                              "emscripten/tools",
                              "llvm",
                              "llvm/bin",
                              "llvm/lib" };
    for (int i = 0; i < (int)(sizeof folders / sizeof *folders); i++) {
        path(filename, directory, folders[i]);
        assert(!mkdir(filename, 0700));
    }
    const char *inputs[] = {
        "emscripten/cmake/Modules/Platform/Emscripten.cmake",
        "emscripten/tools/sidecar.py",
        "emscripten/emcc",
        "emscripten/em++",
        "emscripten/em-config",
        "llvm/bin/clang",
        "llvm/bin/wasm-ld"
    };
    for (int i = 0; i < (int)(sizeof inputs / sizeof *inputs); i++) {
        path(filename, directory, inputs[i]);
        tool_write(filename, "selected tool input");
    }
    path(filename, directory, "engine");
    assert(!mkdir(filename, 0700));
    path(filename, directory, "engine/projects");
    assert(!mkdir(filename, 0700));
    path(filename, directory, "site");
    assert(!mkdir(filename, 0700));
    path(filename, directory, "site/revision");
    tool_write(filename, "0");
    path(filename, directory, "dirty");
    tool_write(filename, "uncommitted authored work");
    RUN_TEST_CASE(publication_identity_covers_commits_projects_and_configuration);
    RUN_TEST_CASE(publication_reuses_verified_bytes_and_rebuilds_corruption);
    RUN_TEST_CASE(concurrent_publication_fills_each_artifact_once);
    tool_remove_tree(directory);
}
