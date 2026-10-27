#include "test/tools/browser.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#define BROWSER_RESULT_CAPACITY (256 * 1024)

static const char *selectors[] = {
    "--wasm",          "--execution",   "--program",
    "--publication",   "--row-array",   "--modules",
    "--query-program", "--row-builder", "--outliner-projection"
};

/* JS only boots independent instances of the C dispatcher and transports their
 * output. Test cases, benchmark loops and all pass/fail assertions stay in C. */
static const char *page_template =
    "<!doctype html><meta charset=utf-8><title>Chibi C Wasm "
    "checks</title><script>\n"
    "const selector=new URLSearchParams(location.search).get('case');\n"
    "if(selector){\n"
    " let output='',errors='',finished=false,truncated=false;\n"
    " const append=(text,error)=>{text=String(text)+'\\n';"
    "if(output.length+errors.length+text.length>32768){truncated=true;return;}"
    "if(error)errors+=text;else output+=text;};\n"
    " const finish=status=>{if(finished)return;finished=true;"
    "parent.postMessage({selector,status,output,errors,truncated,"
    "isolated:crossOriginIsolated},location.origin);};\n"
    " window.Module={arguments:[selector],print:text=>append(text,false),"
    "printErr:text=>append(text,true),onExit:finish,"
    "onAbort:reason=>{append(reason,true);finish(255);}};\n"
    " window.addEventListener('error',event=>{append(event.message,true);"
    "finish(255);});\n"
    " window.addEventListener('unhandledrejection',event=>{append(event.reason,"
    "true);finish(255);});\n"
    " const "
    "script=document.createElement('script');script.src='/test_language.js';"
    "script.onerror=()=>{append('driver failed to load',true);finish(255);};"
    "document.head.append(script);\n"
    "}else{\n"
    " const cases=%s;\n"
    " const results=[];let frame;\n"
    " const "
    "next=()=>{if(frame)frame.remove();frame=document.createElement('iframe');"
    "frame.src='/?case='+encodeURIComponent(cases[results.length]);"
    "document.body.append(frame);};\n"
    " window.addEventListener('message',event=>{"
    "if(event.source!==frame.contentWindow||event.origin!==location.origin)"
    "return;"
    "results.push(event.data);if(results.length<cases.length){next();return;}"
    "fetch('/result',{method:'POST',body:JSON.stringify({"
    "user_agent:navigator.userAgent,results})});});\n"
    " window.addEventListener('DOMContentLoaded',next);\n"
    "}\n</script>";

static yyjson_doc *reply;

static void cleanup()
{
    yyjson_doc_free(reply);
    reply = NULL;
}

static void print_lines(const char *selector, const char *output)
{
    static const char *markers[] = { "Summary:", "BENCH ", "ALLOC ", "PUBLISH " };
    for (const char *line = output; *line;) {
        const char *end = strchr(line, '\n');
        int size = end ? (int)(end - line) : (int)strlen(line);
        for (int i = 0; i < (int)(sizeof markers / sizeof markers[0]); i++) {
            const char *marker = strstr(line, markers[i]);
            if (marker && marker < line + size) {
                printf("Browser %s: %.*s\n", selector, (int)(line + size - marker), marker);
                break;
            }
        }
        line += size + (end != NULL);
    }
}

TEST_CASE(browser_runs_c_wasm_language_checks)
{
    const char *mode = getenv("CHIBI_WASM_BENCHMARK_ONLY");
    bool benchmark_only = mode && !strcmp(mode, "1");
    const char *benchmark[] = { "--script-benchmark" };
    const char *const *cases = benchmark_only ? benchmark : selectors;
    int count = benchmark_only ? 1 : (int)(sizeof selectors / sizeof selectors[0]);
    const char *arguments = benchmark_only ? "['--script-benchmark']" : "['--wasm','--execution','--program','--publication','--row-array','--modules','--query-program','--row-builder','--outliner-projection']";
    char page[8192];
    int page_size = snprintf(page, sizeof page, page_template, arguments);
    REQUIRE(page_size > 0 && page_size < (int)sizeof page);
    char root[PATH_MAX], candidate[PATH_MAX];
    snprintf(candidate, sizeof candidate, "%s/build-wasm-language", tool_root());
    const char *selected = getenv("CHIBI_WASM_TEST_ROOT");
    if (!selected) {
        selected = candidate;
    }
    REQUIRE(selected[0] == '/' && realpath(selected, root));
    static const char *files[] = { "test_language.js",
                                   "test_language.wasm",
                                   "test_language.worker.js",
                                   NULL };
    char result[BROWSER_RESULT_CAPACITY] = { 0 };
    REQUIRE(tool_browser(root, page, files, result, sizeof result));
    reply = yyjson_read(result, strlen(result), 0);
    REQUIRE(reply != NULL);
    yyjson_val *record = yyjson_doc_get_root(reply);
    const char *browser = yyjson_get_str(yyjson_obj_get(record, "user_agent"));
    REQUIRE(browser != NULL && strstr(browser, "Chrome/") != NULL);
    printf("\nBrowser C driver: %s\n", browser);
    yyjson_val *results = yyjson_obj_get(record, "results");
    REQUIRE(yyjson_arr_size(results) == (size_t)count);
    for (int i = 0; i < count; i++) {
        yyjson_val *row = yyjson_arr_get(results, (size_t)i);
        const char *selector = yyjson_get_str(yyjson_obj_get(row, "selector"));
        const char *output = yyjson_get_str(yyjson_obj_get(row, "output"));
        const char *errors = yyjson_get_str(yyjson_obj_get(row, "errors"));
        REQUIRE(selector && !strcmp(selector, cases[i]) && output && errors);
        print_lines(selector, output);
        if (yyjson_get_int(yyjson_obj_get(row, "status")) != 0) {
            fprintf(stderr, "Browser %s output:\n%s%s", selector, output, errors);
        }
        REQUIRE(yyjson_is_int(yyjson_obj_get(row, "status")));
        REQUIRE(yyjson_get_int(yyjson_obj_get(row, "status")) == 0);
        REQUIRE(!yyjson_get_bool(yyjson_obj_get(row, "truncated")));
        REQUIRE(yyjson_get_bool(yyjson_obj_get(row, "isolated")));
        const char *summary = strstr(output, "Summary: Passed:");
        unsigned passed, failed, total, suites, assertions;
        REQUIRE(summary != NULL);
        REQUIRE(
            sscanf(
                summary,
                "Summary: Passed: %u Failed: %u Total: %u Suites: %u Asserts: "
                "%u",
                &passed,
                &failed,
                &total,
                &suites,
                &assertions
            ) == 5
        );
        REQUIRE(passed > 0 && !failed && total == passed && assertions > 0);
    }
    return true;
}

void test_tool_wasm_browser()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(browser_runs_c_wasm_language_checks);
    pu_clear_setup();
}
