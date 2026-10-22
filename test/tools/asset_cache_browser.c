#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include "support/binary_cache.h"

#include <emscripten.h>

#define CACHE_DIRECTORY "/cache/chibi-assets"
static const unsigned char specimen[] = { 0, 1, 255, 17 };

static bool write_record(const char *key)
{
    int lock = binary_cache_lock(CACHE_DIRECTORY);
    if (lock < 0) {
        return false;
    }
    bool ok = binary_cache_write(CACHE_DIRECTORY, key, specimen, sizeof specimen, 1024);
    binary_cache_unlock(lock);
    return ok;
}
static bool record_matches(const char *key)
{
    size_t length;
    unsigned char *bytes = binary_cache_read(CACHE_DIRECTORY, key, 1024, &length);
    bool matches = bytes && length == sizeof specimen &&
                   !memcmp(bytes, specimen, length);
    free(bytes);
    return matches;
}
static bool record_missing(const char *key)
{
    size_t length;
    unsigned char *bytes = binary_cache_read(CACHE_DIRECTORY, key, 1024, &length);
    bool missing = !bytes && !length;
    free(bytes);
    return missing;
}
TEST_CASE(cold_session_publishes_real_cache_records)
{
    REQUIRE(record_missing("browser_keep"));
    REQUIRE(record_missing("browser_remove"));
    REQUIRE(write_record("browser_keep"));
    REQUIRE(write_record("browser_remove"));
    REQUIRE(write_record("browser_bad"));
    REQUIRE(record_matches("browser_keep"));
    FILE *file = fopen(CACHE_DIRECTORY "/browser_bad.bin", "r+b");
    REQUIRE(file != NULL);
    REQUIRE(!fseek(file, -1, SEEK_END));
    REQUIRE(fputc(42, file) == 42);
    REQUIRE(!fclose(file));
    REQUIRE(record_missing("browser_bad"));
    return true;
}
TEST_CASE(reloaded_session_restores_removes_and_repairs_records)
{
    REQUIRE(record_matches("browser_keep"));
    REQUIRE(record_matches("browser_remove"));
    REQUIRE(record_missing("browser_bad"));
    int lock = binary_cache_lock(CACHE_DIRECTORY);
    REQUIRE(lock >= 0);
    bool removed = binary_cache_remove(CACHE_DIRECTORY, "browser_remove");
    binary_cache_unlock(lock);
    REQUIRE(removed);
    REQUIRE(record_missing("browser_remove"));
    REQUIRE(write_record("browser_bad"));
    REQUIRE(record_matches("browser_bad"));
    return true;
}
TEST_CASE(real_indexeddb_version_failure_preserves_memory_cache)
{
    REQUIRE(record_missing("browser_keep"));
    REQUIRE(record_missing("browser_bad"));
    REQUIRE(write_record("browser_fallback"));
    REQUIRE(record_matches("browser_fallback"));
    REQUIRE(EM_ASM_INT({
        return typeof Module['chibiAssetCacheDirty'] == 'function';
    }));
    return true;
}
TEST_CASE(blocked_restore_releases_startup_to_the_memory_cache)
{
    REQUIRE(record_missing("browser_keep"));
    REQUIRE(record_missing("browser_late"));
    REQUIRE(write_record("browser_late"));
    REQUIRE(record_matches("browser_late"));
    return true;
}
int test_asset_cache_browser_start(int phase)
{
    pu_display_time(true);
    if (phase == 1) {
        RUN_TEST_CASE(cold_session_publishes_real_cache_records);
    } else if (phase == 2) {
        RUN_TEST_CASE(reloaded_session_restores_removes_and_repairs_records);
    } else if (phase == 3) {
        RUN_TEST_CASE(real_indexeddb_version_failure_preserves_memory_cache);
    } else if (phase == 4) {
        RUN_TEST_CASE(blocked_restore_releases_startup_to_the_memory_cache);
    } else {
        return 2;
    }
    pu_print_stats();
    int status = pu_test_failed() ? 1 : 0;
    EM_ASM({ Module['chibiAssetCacheTestComplete']($0); }, status);
    return status;
}
#else
#include "test/tools/browser.h"
#include "test/tools/fixture.h"

#include <limits.h>
#include <yyjson.h>

static const char page_head[] =
    "<!doctype html><meta charset=utf-8><title>Chibi real browser "
    "cache</title><script>\n"
    "const storage='/cache/chibi-assets-storage';\n"
    "const phase=Number(new URLSearchParams(location.search).get('phase'));\n"
    "if(phase){\n"
    " let output='',errors='',finished=false;\n"
    " const append=(text,error)=>{const "
    "line=String(text)+'\\n';if(output.length+errors.length+line.length>8192)"
    "return;if(error)errors+=line;else output+=line;};\n"
    " const "
    "finish=status=>{if(finished)return;finished=true;parent.postMessage({"
    "phase,status,output,errors,isolated:crossOriginIsolated},location.origin);"
    "};\n"
    " window.Module={arguments:['--asset-cache-browser-worker',String(phase)],"
    "print:text=>append(text,false),printErr:text=>append(text,true),"
    "chibiAssetCacheTestComplete:finish,onAbort:error=>{append(error,true);"
    "finish(255);}};\n"
    " // Observe completion while forwarding the unchanged real IDBFS "
    "operation.\n"
    " let wrapped=false;Module.monitorRunDependencies=()=>{const "
    "backend=Module.FS?.filesystems?.IDBFS;if(phase!==4||wrapped||!backend)"
    "return;wrapped=true;Module.chibiCacheRestoreCallbacks=0;const "
    "sync=backend.syncfs;backend.syncfs=(mount,populate,done)=>sync.call("
    "backend,mount,populate,error=>{if(populate)Module."
    "chibiCacheRestoreCallbacks++;done(error);});};\n"
    " addEventListener('error',event=>{append(event.message,true);finish(255);}"
    ");\n"
    " addEventListener('unhandledrejection',event=>{append(event.reason,true);"
    "finish(255);});\n"
    " const "
    "script=document.createElement('script');script.src='/"
    "test_tools.js';script.onerror=()=>finish(255);document.head.append(script)"
    ";\n"
    "}else{\n";
static const char page_body[] =
    " const rows=[];let frame,reported=false,heldDelete;\n"
    " const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));\n"
    " const snapshot=()=>new Promise((resolve,reject)=>{const "
    "open=indexedDB.open(storage);open.onerror=()=>reject(open.error);open."
    "onsuccess=()=>{const "
    "db=open.result;if(!db.objectStoreNames.contains('FILE_DATA')){db.close();"
    "resolve({version:db.version,keys:[]});return;}const "
    "read=db.transaction('FILE_DATA','readonly').objectStore('FILE_DATA')."
    "getAllKeys();read.onerror=()=>{db.close();reject(read.error);};read."
    "onsuccess=()=>{const "
    "value={version:db.version,keys:read.result};db.close();resolve(value);};};"
    "});\n"
    " const persisted=async phase=>{let state;for(let "
    "i=0;i<160;i++){state=await snapshot();const "
    "names=state.keys.map(key=>String(key).split('/"
    "').pop());if(names.includes('browser_keep.bin')&&names.includes('browser_"
    "bad.bin')&&(phase===1?names.includes('browser_remove.bin'):!names."
    "includes('browser_remove.bin')))return state;await sleep(50);}return "
    "state;};\n"
    " const closeFrame=()=>{const "
    "files=frame?.contentWindow.Module?.FS?.filesystems?.IDBFS?.dbs;if(files)"
    "for(const db of "
    "Object.values(files))db.close();frame?.remove();frame=null;};\n"
    " const upgrade=()=>new Promise((resolve,reject)=>{const "
    "timer=setTimeout(()=>reject(Error('real IndexedDB upgrade "
    "blocked')),5000);const "
    "open=indexedDB.open(storage,22);open.onerror=()=>{clearTimeout(timer);"
    "reject(open.error);};open.onsuccess=()=>{clearTimeout(timer);const "
    "version=open.result.version;open.result.close();resolve(version);};});\n"
    " const blockRestore=()=>new Promise((resolve,reject)=>{const "
    "open=indexedDB.open(storage);open.onerror=()=>reject(open.error);open."
    "onsuccess=()=>{const held=open.result;held.onversionchange=()=>{};let "
    "finishDelete;const deleted=new "
    "Promise(resolve=>finishDelete=resolve);const "
    "timer=setTimeout(()=>{held.close();reject(Error('real deletion did not "
    "block'));},5000);const "
    "request=indexedDB.deleteDatabase(storage);request.onblocked=()=>{"
    "clearTimeout(timer);resolve({held,deleted});};request.onsuccess=()=>"
    "finishDelete(null);request.onerror=()=>{clearTimeout(timer);finishDelete("
    "request.error);reject(request.error);};};});\n"
    " const restoreCompleted=async()=>{for(let i=0;i<100;i++){const "
    "count=frame.contentWindow.Module.chibiCacheRestoreCallbacks||0;if(count)"
    "return count;await sleep(50);}return 0;};\n"
    " const "
    "activeBytes=()=>Array.from(frame.contentWindow.Module.FS.readFile('/cache/"
    "chibi-assets/browser_late.bin'));\n"
    " const "
    "report=body=>{if(reported)return;reported=true;fetch('/"
    "result',{method:'POST',body:JSON.stringify(body)});};\n"
    " const "
    "launch=phase=>{frame=document.createElement('iframe');frame.src='/"
    "?phase='+phase;document.body.append(frame);};\n"
    " addEventListener('message',async "
    "event=>{if(!frame||event.source!==frame.contentWindow||event.origin!=="
    "location.origin)return;try{const "
    "row=event.data;rows.push(row);if(row.phase<3){const state=await "
    "persisted(row.phase);row.durable_keys=state.keys;if(row.phase===2){"
    "closeFrame();row.database_version=await "
    "upgrade();launch(3);}else{closeFrame();launch(2);}}else "
    "if(row.phase===3){await sleep(800);const state=await "
    "snapshot();row.database_version=state.version;row.durable_keys=state.keys;"
    "closeFrame();heldDelete=await "
    "blockRestore();launch(4);}else{row.late_before=activeBytes();heldDelete."
    "held.close();const error=await heldDelete.deleted;if(error)throw "
    "error;const state=await "
    "snapshot();row.database_version=state.version;row.durable_keys=state.keys;"
    "row.restore_callbacks=await "
    "restoreCompleted();row.late_after=activeBytes();closeFrame();report({user_"
    "agent:navigator.userAgent,results:rows});}}catch(error){report({user_"
    "agent:navigator.userAgent,results:rows,error:String(error)});}});\n"
    " addEventListener('DOMContentLoaded',()=>launch(1));\n"
    "}\n"
    "</script>\n";

static bool has_key(yyjson_val *keys, const char *name)
{
    char path[256];
    snprintf(path, sizeof path, "/cache/chibi-assets-storage/%s.bin", name);
    for (size_t i = 0; i < yyjson_arr_size(keys); i++) {
        const char *key = yyjson_get_str(yyjson_arr_get(keys, i));
        if (key && !strcmp(key, path)) {
            return true;
        }
    }
    return false;
}
TEST_CASE(real_browser_persists_restores_and_survives_storage_failure)
{
    const char *root = getenv("CHIBI_ASSET_CACHE_BROWSER_ROOT");
    REQUIRE(root != NULL && root[0] == '/');
    static const char *files[] = { "test_tools.js", "test_tools.wasm", NULL };
    char page[8192], reply[32768] = { 0 };
    _Static_assert(sizeof page_head + sizeof page_body <= sizeof page, "Browser bootstrap fits");
    snprintf(page, sizeof page, "%s%s", page_head, page_body);
    REQUIRE(tool_browser(root, page, files, reply, sizeof reply));
    yyjson_doc *doc = yyjson_read(reply, strlen(reply), 0);
    REQUIRE(doc != NULL);
    yyjson_val *result = yyjson_doc_get_root(doc);
    const char *agent = yyjson_get_str(yyjson_obj_get(result, "user_agent"));
    bool browser = agent && strstr(agent, "Chrome/");
    yyjson_val *rows = yyjson_obj_get(result, "results");
    bool passed = browser && !yyjson_obj_get(result, "error") &&
                  yyjson_arr_size(rows) == 4;
    for (size_t i = 0; i < yyjson_arr_size(rows); i++) {
        yyjson_val *row = yyjson_arr_get(rows, i);
        const char *output = yyjson_get_str(yyjson_obj_get(row, "output"));
        const char *errors = yyjson_get_str(yyjson_obj_get(row, "errors"));
        printf("Browser cache phase %zu:\n%s%s", i + 1, output ? output : "", errors ? errors : "");
        unsigned success = 0, failed = 0, total = 0, suites = 0, assertions = 0;
        const char *summary = output ? strstr(output, "Summary: Passed:") : NULL;
        passed &= yyjson_get_int(yyjson_obj_get(row, "phase")) == (int)i + 1 &&
                  yyjson_get_int(yyjson_obj_get(row, "status")) == 0 &&
                  yyjson_get_bool(yyjson_obj_get(row, "isolated")) && summary &&
                  sscanf(
                      summary,
                      "Summary: Passed: %u Failed: %u Total: %u Suites: %u "
                      "Asserts: %u",
                      &success,
                      &failed,
                      &total,
                      &suites,
                      &assertions
                  ) == 5 &&
                  success == 1 && !failed && total == 1 && assertions > 0;
        yyjson_val *keys = yyjson_obj_get(row, "durable_keys");
        if (i < 3) {
            passed &= has_key(keys, "browser_keep") && has_key(keys, "browser_bad");
            passed &= has_key(keys, "browser_remove") == (i == 0);
            passed &= !has_key(keys, "browser_fallback");
            if (i > 0) {
                passed &= yyjson_get_int(yyjson_obj_get(row, "database_version")) == 22;
            }
        } else {
            yyjson_val *before = yyjson_obj_get(row, "late_before");
            yyjson_val *after = yyjson_obj_get(row, "late_after");
            passed &= yyjson_get_int(yyjson_obj_get(row, "database_version")) == 21 &&
                      yyjson_get_int(yyjson_obj_get(row, "restore_callbacks")) == 1 &&
                      yyjson_arr_size(before) == 148 &&
                      yyjson_arr_size(after) == 148 &&
                      !has_key(keys, "browser_late");
            for (size_t byte = 0; byte < yyjson_arr_size(before); byte++) {
                yyjson_val *a = yyjson_arr_get(before, byte),
                           *b = yyjson_arr_get(after, byte);
                passed &= yyjson_is_int(a) && yyjson_is_int(b) &&
                          yyjson_get_uint(a) <= 255 &&
                          yyjson_get_uint(a) == yyjson_get_uint(b);
            }
            static const unsigned char expected[] = { 0, 1, 255, 17 };
            for (size_t byte = 0; byte < sizeof expected; byte++) {
                passed &= yyjson_get_uint(yyjson_arr_get(after, 144 + byte)) ==
                          expected[byte];
            }
        }
    }
    if (!passed) {
        fprintf(stderr, "Browser cache reply: %s\n", reply);
    }
    yyjson_doc_free(doc);
    REQUIRE(passed);
    return true;
}
void test_tool_asset_cache_browser()
{
    RUN_TEST_CASE(real_browser_persists_restores_and_survives_storage_failure);
}
#endif
