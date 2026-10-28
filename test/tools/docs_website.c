#include "support/sha256.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char revision[] = "3333333333333333333333333333333333333333";
static char root[PATH_MAX], docs[PATH_MAX], engine[PATH_MAX], output[PATH_MAX];
static char log_path[PATH_MAX], helper[PATH_MAX];

/* Inject one file-copy failure into the real packager, not a replacement
 * implementation. C owns the scenario and its atomic-publication assertions. */
static const char copy_failure[] =
    "import importlib.util,sys\n"
    "from pathlib import Path\n"
    "sys.dont_write_bytecode=True\n"
    "spec=importlib.util.spec_from_file_location('docs_package',sys.argv[1])\n"
    "module=importlib.util.module_from_spec(spec);spec.loader.exec_module("
    "module)\n"
    "original=module.shutil.copyfile\n"
    "def copy(source,target,*args,**kwargs):\n"
    " if Path(source).name=='app.js': raise OSError('injected copy failure')\n"
    " return original(source,target,*args,**kwargs)\n"
    "module.shutil.copyfile=copy\n"
    "module.package(Path(sys.argv[2]),Path(sys.argv[3]),sys.argv[4],Path(sys."
    "argv[5]))\n";

static void path(char result[PATH_MAX], const char *relative)
{
    int n = snprintf(result, PATH_MAX, "%s/%s", root, relative);
    assert(n > 0 && n < PATH_MAX);
}

static void write_text(const char *relative, const char *text)
{
    char file[PATH_MAX];
    path(file, relative);
    tool_write(file, text);
}

static void file_hash(const char *file, char result[65])
{
    FILE *input = fopen(file, "rb");
    assert(input);
    sha256 hash;
    sha256_begin(&hash);
    unsigned char bytes[4096];
    size_t count;
    while ((count = fread(bytes, 1, sizeof bytes, input))) {
        sha256_add(&hash, bytes, count);
    }
    assert(!ferror(input) && !fclose(input));
    sha256_end(&hash, result);
}

static bool has(const char *file, const char *text)
{
    char *content = tool_read(file);
    bool found = content && strstr(content, text);
    free(content);
    return found;
}

static bool same(const char *a, const char *b)
{
    char before[65], after[65];
    file_hash(a, before);
    file_hash(b, after);
    return !strcmp(before, after);
}

static void make_engine(const char *version, int api)
{
    char js[65], wasm[65], text[1024];
    sha256_hex("formatter JS\n", 13, js);
    sha256_hex("formatter WASM\n", 15, wasm);
    int n = snprintf(
        text,
        sizeof text,
        "{\"format\":1,\"api\":%d,\"revision\":\"%s\",\"max_source_bytes\":"
        "262144,"
        "\"files\":{\"engine.mjs\":\"%s\",\"engine.wasm\":\"%s\"}}\n",
        api,
        version,
        js,
        wasm
    );
    assert(n > 0 && n < (int)sizeof text);
    write_text("formatter/engine.json", text);
}

/* pico_unit calls these through void (*)(void); keep that exact callback type
 * for the function-type sanitizer rather than an old-style declaration. */
static void setup(void)
{
    const char *temporary = getenv("TMPDIR");
    int n = snprintf(root, sizeof root, "%s/chibi-docs-website-XXXXXX", temporary ? temporary : "/tmp");
    assert(n > 0 && n < (int)sizeof root && mkdtemp(root));
    path(docs, "docs");
    path(engine, "formatter");
    path(output, "output");
    path(log_path, "compose.log");
    assert(!mkdir(docs, 0700) && !mkdir(engine, 0700));
    write_text("docs/index.html", "<!doctype html><main>Unchanged home</main>\n");
    write_text("docs/style.css", "/* original syntax palette */ body { color: #123; }\n");
    write_text("docs/formatter.html", "<!doctype html><main>Formatter guide</main>\n");
    write_text("formatter/engine.mjs", "formatter JS\n");
    write_text("formatter/engine.wasm", "formatter WASM\n");
    write_text("docs/another.html", "<main>Unchanged guide</main>\n");
    make_engine(revision, 1);
}

static void cleanup(void)
{
    tool_remove_tree(root);
}

static int compose()
{
    const char *arguments[] = { "python3",  helper,       "compose",
                                "--docs",   docs,         "--formatter",
                                engine,     "--revision", revision,
                                "--output", output,       NULL };
    return tool_run(arguments, root, log_path);
}

static bool no_staging()
{
    DIR *directory = opendir(root);
    assert(directory);
    bool clean = true;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (!strncmp(entry->d_name, "docs-site-", 10)) {
            clean = false;
        }
    }
    assert(!closedir(directory));
    return clean;
}

static bool refused(const char *message)
{
    REQUIRE(compose() == 1);
    REQUIRE(access(output, F_OK) != 0);
    REQUIRE(has(log_path, message));
    REQUIRE(no_staging());
    return true;
}

TEST_CASE(docs_website_composes_exact_artifacts)
{
    REQUIRE(compose() == 0);
    const char *unchanged[] = { "index.html", "style.css", "another.html" };
    for (int i = 0; i < 3; i++) {
        char before[PATH_MAX], after[PATH_MAX];
        snprintf(before, sizeof before, "%s/%s", docs, unchanged[i]);
        snprintf(after, sizeof after, "%s/%s", output, unchanged[i]);
        REQUIRE(same(before, after));
    }
    char file[PATH_MAX];
    path(file, "output/formatter.html");
    REQUIRE(has(file, "Formatter guide") && has(file, "id=\"formatter-app\""));
    const char *artifacts[] = { "engine.mjs", "engine.wasm", "engine.json" };
    for (int i = 0; i < 3; i++) {
        char before[PATH_MAX], after[PATH_MAX];
        snprintf(before, sizeof before, "%s/%s", engine, artifacts[i]);
        snprintf(after, sizeof after, "%s/formatter/%s", output, artifacts[i]);
        REQUIRE(same(before, after));
    }
    const char *ui[] = { "formatter/app.css",
                         "formatter/app.js",
                         "formatter/state.js",
                         "formatter/worker.js" };
    for (int i = 0; i < 4; i++) {
        char before[PATH_MAX], after[PATH_MAX];
        snprintf(before, sizeof before, "%s/tools/docs/%s", tool_root(), ui[i]);
        snprintf(after, sizeof after, "%s/%s", output, ui[i]);
        REQUIRE(same(before, after));
    }
    path(file, "output/website.json");
    REQUIRE(has(file, revision) && has(file, "formatter/engine.wasm"));
    path(file, "output/.nojekyll");
    REQUIRE(access(file, F_OK) == 0 && no_staging());
    return true;
}

TEST_CASE(docs_website_refuses_wrong_formatter_identity)
{
    make_engine("4444444444444444444444444444444444444444", 1);
    REQUIRE(refused("Formatter identity does not match"));
    make_engine(revision, 2);
    return refused("Formatter identity does not match");
}

TEST_CASE(docs_website_refuses_corrupt_formatter)
{
    write_text("formatter/engine.wasm", "corrupted");
    return refused("Formatter artifact checksum mismatch");
}

TEST_CASE(docs_website_refuses_missing_formatter)
{
    char file[PATH_MAX];
    path(file, "formatter/engine.wasm");
    REQUIRE(unlink(file) == 0);
    return refused("Expected the complete formatter build");
}

TEST_CASE(docs_website_refuses_extra_formatter)
{
    write_text("formatter/unexpected.js", "extra");
    return refused("Expected the complete formatter build");
}

TEST_CASE(docs_website_refuses_bad_guide_markup)
{
    write_text("docs/formatter.html", "<main>unfinished guide");
    REQUIRE(refused("Unexpected documentation guide structure"));
    write_text("docs/formatter.html", "<main>ambiguous</main></main>");
    REQUIRE(refused("Unexpected documentation guide structure"));
    return true;
}

TEST_CASE(docs_website_refuses_missing_guide_and_symlink)
{
    char file[PATH_MAX];
    path(file, "docs/formatter.html");
    REQUIRE(unlink(file) == 0);
    REQUIRE(refused("Build the docs manifest"));
    REQUIRE(symlink("index.html", file) == 0);
    return refused("Website inputs cannot contain symlinks");
}

TEST_CASE(docs_website_preserves_existing_output)
{
    REQUIRE(mkdir(output, 0700) == 0);
    write_text("output/existing.txt", "original site");
    REQUIRE(compose() == 1);
    char file[PATH_MAX];
    path(file, "output/existing.txt");
    REQUIRE(has(file, "original site") && has(log_path, "Output must be a new directory"));
    return true;
}

TEST_CASE(docs_website_refuses_overlapping_output)
{
    path(output, "docs/nested");
    return refused("Output overlaps an input");
}

TEST_CASE(docs_website_refuses_prepackaged_input)
{
    char directory[PATH_MAX];
    path(directory, "docs/formatter");
    REQUIRE(mkdir(directory, 0700) == 0);
    write_text("docs/formatter/old.js", "stale browser tool");
    return refused("Input is already packaged");
}

TEST_CASE(docs_website_refuses_stale_manifest)
{
    write_text("docs/website.json", "stale inventory");
    return refused("Input is already packaged");
}

TEST_CASE(docs_website_preserves_dangling_output_link)
{
    REQUIRE(symlink("missing-directory", output) == 0);
    REQUIRE(compose() == 1 && has(log_path, "Output must be a new directory"));
    struct stat info;
    REQUIRE(lstat(output, &info) == 0 && S_ISLNK(info.st_mode) && no_staging());
    return true;
}

TEST_CASE(docs_website_refuses_duplicate_panel)
{
    write_text("docs/formatter.html", "<main><section id=\"formatter-app\"></section></main>");
    return refused("Unexpected documentation guide structure");
}

TEST_CASE(docs_website_refuses_special_file)
{
    char file[PATH_MAX];
    path(file, "docs/pipe");
    REQUIRE(mkfifo(file, 0600) == 0);
    return refused("Website inputs cannot contain symlinks or special files");
}

TEST_CASE(docs_website_refuses_formatter_symlink)
{
    char file[PATH_MAX];
    path(file, "formatter/engine.wasm");
    REQUIRE(unlink(file) == 0 && symlink("engine.mjs", file) == 0);
    return refused("Website inputs cannot contain symlinks");
}

TEST_CASE(docs_website_copy_failure_is_atomic)
{
    const char *arguments[] = { "python3", "-c",     copy_failure, helper, docs,
                                engine,    revision, output,       NULL };
    REQUIRE(tool_run(arguments, root, log_path) == 1);
    REQUIRE(access(output, F_OK) != 0 && no_staging());
    REQUIRE(has(log_path, "injected copy failure"));
    char file[PATH_MAX];
    path(file, "docs/index.html");
    REQUIRE(has(file, "Unchanged home"));
    path(file, "formatter/engine.mjs");
    REQUIRE(has(file, "formatter JS"));
    return true;
}

void test_tool_docs_website()
{
    int n = snprintf(helper, sizeof helper, "%s/tools/docs/package.py", tool_root());
    assert(n > 0 && n < (int)sizeof helper);
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(docs_website_composes_exact_artifacts);
    RUN_TEST_CASE(docs_website_refuses_wrong_formatter_identity);
    RUN_TEST_CASE(docs_website_refuses_corrupt_formatter);
    RUN_TEST_CASE(docs_website_refuses_missing_formatter);
    RUN_TEST_CASE(docs_website_refuses_extra_formatter);
    RUN_TEST_CASE(docs_website_refuses_bad_guide_markup);
    RUN_TEST_CASE(docs_website_refuses_missing_guide_and_symlink);
    RUN_TEST_CASE(docs_website_preserves_existing_output);
    RUN_TEST_CASE(docs_website_refuses_overlapping_output);
    RUN_TEST_CASE(docs_website_refuses_prepackaged_input);
    RUN_TEST_CASE(docs_website_refuses_stale_manifest);
    RUN_TEST_CASE(docs_website_preserves_dangling_output_link);
    RUN_TEST_CASE(docs_website_refuses_duplicate_panel);
    RUN_TEST_CASE(docs_website_refuses_special_file);
    RUN_TEST_CASE(docs_website_refuses_formatter_symlink);
    RUN_TEST_CASE(docs_website_copy_failure_is_atomic);
    pu_setup(NULL, NULL);
}
