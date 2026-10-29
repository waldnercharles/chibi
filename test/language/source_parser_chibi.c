#include "compiler_fixture.h"
#include "support/array.h"
#include "support/file_io.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_syntax.h"
#include "types/text.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The authored parser against Tree-sitter, the parser behind
 * compiler_syntax_parse. SourceParserDiff.chibi compares the two trees row by
 * row inside one package, so this file only supplies inputs: every .chibi file
 * below packages/, test/ and tools/, plus the sources of
 * test/language/syntax_corpus, all read at run time. Text Tree-sitter rejects
 * has to be rejected as well. One call per file on each backend, at most 6,000
 * steps per byte. No children, services or GPU. */
enum
{
    MAX_SOURCE = 512 * 1024,
    STEPS_PER_BYTE = 6000,
    BACKENDS = 2
};
static const char IDENTITY[] = "project:source_parser_diff";
static const char *const ROOTS[] = { "packages", "test", "tools" };
static const char *const PARSER_FILES[] = {
    "tools/compiler/source_tokenizer/Tokenizer.chibi",
    "tools/compiler/syntax_diagnostics/Diagnostics.chibi",
    "tools/compiler/source_parser/Tree.chibi",
    "tools/compiler/source_parser/Types.chibi",
    "tools/compiler/source_parser/Expressions.chibi",
    "tools/compiler/source_parser/Statements.chibi",
    "tools/compiler/source_parser/Declarations.chibi",
    "tools/compiler/source_parser/Parser.chibi",
    "test/language/SourceParserDiff.chibi",
    "packages/core/strings/Strings.chibi",
};
static const char *const PARSER_PATHS[] = {
    "source_tokenizer/Tokenizer.chibi", "syntax_diagnostics/Diagnostics.chibi",
    "source_parser/Tree.chibi",         "source_parser/Types.chibi",
    "source_parser/Expressions.chibi",  "source_parser/Statements.chibi",
    "source_parser/Declarations.chibi", "source_parser/Parser.chibi",
    "source_parser_diff/Diff.chibi",    "/sdk/core/strings/Strings.chibi",
};
#define PARSER_COUNT ((int)(sizeof PARSER_FILES / sizeof *PARSER_FILES))

static compiler_fixture fixture;
static char *sources[PARSER_COUNT];
static arena input, detached;
static int files, bytes;

static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
}
static bool prepare()
{
    if (fixture.owners[0]) {
        return true;
    }
    source_package_input inputs[PARSER_COUNT];
    for (int i = 0; i < PARSER_COUNT; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", CHIBI_SOURCE_ROOT, PARSER_FILES[i]);
        sources[i] = file_read_text(path);
        REQUIRE(sources[i]);
        inputs[i] = (source_package_input){ PARSER_PATHS[i],
                                            sources[i],
                                            strlen(sources[i]) };
    }
    REQUIRE(
        compiler_fixture_prepare_packs(&fixture, inputs, PARSER_COUNT, &COMPILER_SYNTAX_DEFINITION_PACK, 1)
    );
    return true;
}
/* The parser's difference from Tree-sitter's tree for `text`; false when the
 * trees differ or the call fails. */
static bool differential(const char *label, const char *text)
{
    size_t size = strlen(text) + 1;
    int budget = getenv("CHIBI_PARSER_BUDGET") ? atoi(getenv("CHIBI_PARSER_BUDGET")) : (int)(size * STEPS_PER_BYTE) + 100000;
    for (int b = 0; b < BACKENDS; b++) {
        arena_free(&input);
        arena_free(&detached);
        char *copy = arena_alloc(&input, size);
        memcpy(copy, text, size);
        pin_value argument = { .type = PIN_STR,
                               .text = &(
                                   const pin_string
                               ){ .bytes = copy, .length = (uint32_t)strlen(copy) } },
                  output = { 0 };
        bool called = compiler_fixture_call(&fixture, b, IDENTITY, getenv("CHIBI_PARSER_ENTRY") ? getenv("CHIBI_PARSER_ENTRY") : "Differ", &argument, 1, budget, &detached, &output);
        if (!called) {
            fprintf(stderr, "%s (backend %d): the comparison did not finish\n", label, b);
        }
        REQUIRE(called);
        REQUIRE(output.type == PIN_STR);
        const char *difference = pin_text(output).bytes;
        if (difference && difference[0]) {
            fprintf(stderr, "%s (backend %d): %s\n", label, b, difference);
            return false;
        }
    }
    return true;
}
static int compare_paths(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}
static bool has_suffix(const char *name, const char *suffix)
{
    size_t length = strlen(name), tail = strlen(suffix);
    return length > tail && !strcmp(name + length - tail, suffix);
}
static void collect(const char *directory, const char *suffix, char ***paths)
{
    DIR *dir = opendir(directory);
    if (!dir) {
        return;
    }
    for (struct dirent *entry; (entry = readdir(dir));) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        size_t size = strlen(directory) + strlen(entry->d_name) + 2;
        char *path = xmalloc(size);
        snprintf(path, size, "%s/%s", directory, entry->d_name);
        struct stat info;
        if (stat(path, &info) == 0 && S_ISDIR(info.st_mode)) {
            collect(path, suffix, paths);
            free(path);
        } else if (has_suffix(entry->d_name, suffix)) {
            apush(*paths, path);
        } else {
            free(path);
        }
    }
    closedir(dir);
}
/* A file that cannot be read or is over the cap fails by name, so the files
 * after it are not skipped silently. */
static bool corpus_file(const char *path)
{
    char *text = file_read_text(path);
    if (!text) {
        fprintf(stderr, "source parser: cannot read %s\n", path);
        return false;
    }
    size_t size = strlen(text);
    bool valid = size <= MAX_SOURCE;
    if (!valid) {
        fprintf(stderr, "source parser: %s is %zu bytes, over %d\n", path, size, MAX_SOURCE);
    }
    valid = valid && differential(path, text);
    bytes += (int)size;
    free(text);
    return valid;
}
static bool corpus(const char *const *roots, int count, const char *suffix)
{
    char **paths = NULL;
    for (int r = 0; r < count; r++) {
        char root[512];
        snprintf(root, sizeof root, "%s/%s", CHIBI_SOURCE_ROOT, roots[r]);
        collect(root, suffix, &paths);
    }
    qsort(paths, (size_t)alen(paths), sizeof *paths, compare_paths);
    bool valid = alen(paths) > 0;
    for (int i = 0; valid && i < alen(paths); i++) {
        valid = corpus_file(paths[i]);
        files++;
    }
    for (int i = 0; i < alen(paths); i++) {
        free(paths[i]);
    }
    afree(paths);
    return valid;
}
TEST_CASE(source_parser_repository)
{
    REQUIRE(prepare());
    REQUIRE(corpus(ROOTS, (int)(sizeof ROOTS / sizeof *ROOTS), ".chibi"));
    REQUIRE(files > 100);
    return true;
}
/* Sources for constructs the repository does not use, kept as text files so
 * that package scans do not read them. Some are invalid on purpose. */
TEST_CASE(source_parser_constructs)
{
    static const char *const SYNTAX_ROOT[] = { "test/language/syntax_corpus" };
    REQUIRE(prepare());
    REQUIRE(corpus(SYNTAX_ROOT, 1, ".chibi.txt"));
    return true;
}
void test_language_source_parser_chibi()
{
    files = bytes = 0;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_parser_repository);
    printf("Source parser corpus: %d files, %d bytes\n", files, bytes);
    RUN_TEST_CASE(source_parser_constructs);
    pu_clear_setup();
    cleanup();
    compiler_fixture_free(&fixture);
    for (int i = 0; i < PARSER_COUNT; i++) {
        free(sources[i]);
        sources[i] = NULL;
    }
}
