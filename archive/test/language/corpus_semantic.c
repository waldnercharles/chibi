#include "authoring/application_catalog.h"
#include "authoring/language_catalog.h"
#include "authoring/source_catalog.h"
#include "authoring/source_collection.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MAX_FILES = 256,
    MAX_PROJECTS = 16,
    MAX_PATH = 4096,
    MAX_TOTAL_BYTES = 8 * 1024 * 1024
};
static const char *manifest_path, *content_root;
static authoring_language_catalog *native;
static source_catalog *project;
static script_analysis *analysis;
static authoring_sources *prepared;
static bool preparing;
static behavior_backend preparation_backend;
static FILE *manifest;
static int aggregate_files;
static size_t aggregate_bytes;
static void cleanup()
{
    script_analysis_free(analysis);
    authoring_sources_free(prepared);
    source_catalog_free(project);
    authoring_language_catalog_free(native);
    if (manifest) {
        fclose(manifest);
    }
    analysis = NULL;
    project = NULL;
    prepared = NULL;
    native = NULL;
    manifest = NULL;
}
static bool check_corpus()
{
    char error[192];
    native_capabilities *table = authoring_application_catalog_new(true, error, sizeof error);
    if (table) {
        const authoring_language_catalog_input input = { .native = table };
        native = authoring_language_catalog_new(&input, error, sizeof error);
        native_capabilities_free(table);
    }
    if (!native) {
        fprintf(stderr, "Native corpus catalog: %s\n", error);
    }
    REQUIRE(native != NULL);
    project = source_catalog_new(content_root, authoring_language_catalog_view(native));
    int failures = source_catalog_diagnostic_count(project);
    for (int i = 0; i < failures; i++) {
        const source_catalog_diagnostic *diagnostic = source_catalog_diagnostic_at(project, i);
        fprintf(
            stderr,
            "%s:%d:%d: declaration: %s\n",
            diagnostic->path,
            diagnostic->range.line,
            diagnostic->range.column,
            diagnostic->message
        );
    }
    manifest = fopen(manifest_path, "rb");
    REQUIRE(manifest != NULL);
    bool seen[MAX_FILES] = { 0 };
    char path[MAX_PATH];
    int files = 0;
    size_t total = 0;
    for (; files < MAX_FILES && fgets(path, sizeof path, manifest); files++) {
        size_t size = strlen(path);
        REQUIRE(size > 0 && path[size - 1] == '\n');
        path[--size] = 0;
        if (size && path[size - 1] == '\r') {
            path[--size] = 0;
        }
        REQUIRE(size > 0);
        int index = -1;
        for (int i = 0; i < source_catalog_source_count(project); i++) {
            if (!strcmp(path, source_catalog_source_at(project, i)->path)) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            fprintf(stderr, "Manifest source outside catalog: %s\n", path);
        }
        REQUIRE(index >= 0 && !seen[index]);
        seen[index] = true;
        const source_catalog_source *source = source_catalog_source_at(project, index);
        total += strlen(source->text);
        aggregate_files++;
        aggregate_bytes += strlen(source->text);
        REQUIRE(total <= MAX_TOTAL_BYTES);
        REQUIRE(aggregate_files <= MAX_FILES && aggregate_bytes <= MAX_TOTAL_BYTES);
        if (!preparing) {
            analysis = script_analyze_document(
                source->syntax,
                source_catalog_view(project),
                source->identity
            );
            if (!analysis->valid) {
                failures++;
                fprintf(stderr, "Corpus semantic errors: %s\n", path);
                for (int i = 0; i < analysis->diagnostic_count && i < 8; i++) {
                    fprintf(
                        stderr,
                        "  %d:%d %s\n",
                        analysis->diagnostics[i].range.line,
                        analysis->diagnostics[i].range.column,
                        analysis->diagnostics[i].message
                    );
                }
            }
            script_analysis_free(analysis);
            analysis = NULL;
        }
    }
    REQUIRE(files > 0 && !ferror(manifest));
    REQUIRE(fgetc(manifest) == EOF);
    REQUIRE(files == source_catalog_source_count(project));
    if (preparing) {
        script_lower_bindings bindings = authoring_language_bindings(native);
        prepared = authoring_sources_prepare_catalog(project, &bindings, preparation_backend);
        project = NULL; /* The collection consumes the validated declaration snapshot. */
        REQUIRE(authoring_sources_count(prepared) == files);
        for (int i = 0; i < files; i++) {
            const authoring_source *source = authoring_sources_at(prepared, i);
            if (source->valid && source->module &&
                execution_module_backend(source->module) == preparation_backend) {
                continue;
            }
            failures++;
            fprintf(stderr, "Corpus preparation errors: %s\n", source->path);
            for (int j = 0; j < source->diagnostic_count && j < 8; j++) {
                fprintf(
                    stderr,
                    "  %d:%d %s\n",
                    source->diagnostics[j].range.line,
                    source->diagnostics[j].range.column,
                    source->diagnostics[j].message
                );
            }
        }
        REQUIRE(authoring_sources_valid(prepared) == (failures == 0));
    }
    printf(
        "Retained %s corpus: %d files, %zu bytes, %d failures\n",
        preparing ? "preparation" : "semantic",
        files,
        total,
        failures
    );
    REQUIRE(failures == 0);
    return true;
}
TEST_CASE(language_real_corpus_semantics)
{
    return check_corpus();
}
TEST_CASE(language_real_corpus_preparation)
{
    return check_corpus();
}
int run_language_semantic_corpus(const char *file, const char *root)
{
    preparing = false;
    aggregate_files = 0;
    aggregate_bytes = 0;
    manifest_path = file;
    content_root = root;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(language_real_corpus_semantics);
    pu_clear_setup();
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}
/* Each row names one content root and its exact source manifest, separated by
 * a tab. All projects share the same file/byte ceiling and outer supervisor. */
static int run_corpora(const char *file, bool prepare, behavior_backend backend)
{
    preparing = prepare;
    preparation_backend = backend;
    FILE *projects = fopen(file, "rb");
    if (!projects) {
        fprintf(stderr, "Cannot read semantic project manifest: %s\n", file);
        return 2;
    }
    aggregate_files = 0;
    aggregate_bytes = 0;
    char row[MAX_PATH * 2];
    int count = 0, status = 0;
    pu_setup(NULL, cleanup);
    for (; count < MAX_PROJECTS && fgets(row, sizeof row, projects); count++) {
        size_t size = strlen(row);
        char *separator = strchr(row, '\t');
        if (!size || row[size - 1] != '\n' || !separator || separator == row ||
            separator + 1 == row + size - 1) {
            status = 2;
            break;
        }
        row[--size] = 0;
        *separator++ = 0;
        if (strlen(row) >= MAX_PATH || strlen(separator) >= MAX_PATH ||
            strchr(separator, '\t')) {
            status = 2;
            break;
        }
        content_root = row;
        manifest_path = separator;
        printf(
            "%s project: %s\n",
            preparing
                ? preparation_backend == BEHAVIOR_SLJIT ? "SLJIT preparation" : "BYTECODE preparation"
                : "Semantic",
            content_root
        );
        if (preparing) {
            RUN_TEST_CASE(language_real_corpus_preparation);
        } else {
            RUN_TEST_CASE(language_real_corpus_semantics);
        }
    }
    if (!count || ferror(projects) || fgetc(projects) != EOF) {
        status = 2;
    }
    fclose(projects);
    pu_clear_setup();
    printf(
        "%s project corpus: %d projects, %d files, %zu bytes\n",
        preparing ? "Preparation" : "Semantic",
        count,
        aggregate_files,
        aggregate_bytes
    );
    pu_print_stats();
    return status ? status : pu_test_failed() ? 1 : 0;
}

int run_language_semantic_corpora(const char *file)
{
    return run_corpora(file, false, BEHAVIOR_BYTECODE);
}
int run_language_prepare_corpora(const char *file)
{
    return run_corpora(file, true, BEHAVIOR_BYTECODE);
}

int run_language_prepare_corpora_sljit(const char *file)
{
    return run_corpora(file, true, BEHAVIOR_SLJIT);
}

/* Fixed source-only rollout coverage. These inputs deliberately exclude the
 * scene runner, compiler, external processes and general project manifests. */
enum
{
    UNIT_FILE_BYTES = 16 * 1024,
    UNIT_SOURCE_BYTES = 64 * 1024,
    UNIT_COMPONENT_BYTES = 16 * 1024,
    UNIT_COMPONENT_ENTRIES = 32
};
typedef struct
{
    const char *root, *component;
    const char *sources[5];
    int count;
} unit_fixture;
