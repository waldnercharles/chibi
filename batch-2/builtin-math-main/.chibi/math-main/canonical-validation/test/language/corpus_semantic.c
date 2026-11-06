#include "authoring/application_catalog.h"
#include "authoring/component_declarations.h"
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
static declared_components *fixture_components;
static char *fixture_text[5];
static void cleanup()
{
    script_analysis_free(analysis);
    authoring_sources_free(prepared);
    source_catalog_free(project);
    authoring_language_catalog_free(native);
    declared_components_free(fixture_components);
    fixture_components = NULL;
    for (size_t i = 0; i < sizeof fixture_text / sizeof *fixture_text; i++) {
        free(fixture_text[i]);
        fixture_text[i] = NULL;
    }
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
static size_t unit_source_bytes, unit_component_bytes;
typedef struct
{
    const char *root, *component;
    const char *sources[5];
    int count;
} unit_fixture;
static const unit_fixture declared_unit_fixture = {
    .root = CHIBI_SOURCE_ROOT "/test/fixtures/declared-components/content",
    .component = "Charge.chibi",
    .sources = { "systems/Recharge.chibi" },
    .count = 1,
};
static const unit_fixture native_unit_fixture = {
    .root = CHIBI_SOURCE_ROOT "/test/fixtures/feature-coverage/content",
    .component = "Heat.chibi",
    .sources = { "systems/Cool.chibi",
                 "systems/Drift.chibi",
                 "behaviors/PhysicsProbe.chibi",
                 "behaviors/PlatformRiderGravity.chibi",
                 "behaviors/Physics3DProbe.chibi" },
    .count = 5,
};
static bool read_unit_source(const char *path, char **text, size_t *total, size_t limit)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Cannot read unit fixture: %s\n", path);
        return false;
    }
    bool valid = fseek(file, 0, SEEK_END) == 0;
    long size = valid ? ftell(file) : -1;
    valid = size >= 0 && size <= UNIT_FILE_BYTES &&
          *total + (size_t)size <= limit && fseek(file, 0, SEEK_SET) == 0;
    if (valid) {
        *text = xmalloc((size_t)size + 1);
        valid = fread(*text, 1, (size_t)size, file) == (size_t)size &&
                !ferror(file) && fgetc(file) == EOF;
        (*text)[size] = 0;
        valid = valid && strlen(*text) == (size_t)size;
        *total += (size_t)size;
    }
    fclose(file);
    if (!valid) {
        fprintf(stderr, "Unit fixture exceeds its bounds or is unreadable: %s\n", path);
    }
    return valid;
}
static bool unit_component_bound(const unit_fixture *fixture)
{
    char directory[MAX_PATH];
    REQUIRE(
        snprintf(directory, sizeof directory, "%s/components", fixture->root) <
        (int)sizeof directory
    );
    DIR *dir = opendir(directory);
    REQUIRE(dir != NULL);
    int entries = 0, components = 0;
    bool valid = true;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (++entries > UNIT_COMPONENT_ENTRIES) {
            valid = false;
            break;
        }
        size_t length = strlen(entry->d_name);
        if (entry->d_name[0] != '.' && length > 6 &&
            !strcmp(entry->d_name + length - 6, ".chibi")) {
            components++;
            valid = valid && !strcmp(entry->d_name, fixture->component);
        }
    }
    closedir(dir);
    REQUIRE(valid && components == 1);
    char path[MAX_PATH], *text = NULL;
    REQUIRE(
        snprintf(path, sizeof path, "%s/%s", directory, fixture->component) < (int)sizeof path
    );
    valid = read_unit_source(path, &text, &unit_component_bytes, UNIT_COMPONENT_BYTES);
    free(text);
    return valid;
}
static bool check_unit_fixture(const unit_fixture *fixture)
{
    REQUIRE(unit_component_bound(fixture));
    char paths[5][MAX_PATH];
    for (int i = 0; i < fixture->count; i++) {
        REQUIRE(
            snprintf(paths[i], sizeof paths[i], "%s/%s", fixture->root, fixture->sources[i]) <
            (int)sizeof paths[i]
        );
        REQUIRE(read_unit_source(paths[i], &fixture_text[i], &unit_source_bytes, UNIT_SOURCE_BYTES));
    }
    char error[256] = { 0 };
    native_capabilities *table = authoring_application_catalog_new(false, error, sizeof error);
    if (table) {
        bool loaded = authoring_component_declarations_load(
            fixture->root,
            native_capabilities_catalog(table),
            &fixture_components,
            error,
            sizeof error
        );
        if (loaded) {
            /* The loader installs the owned component set in the project slot,
             * which the language catalog copies exactly as project preparation does. */
            const authoring_language_catalog_input input = { .native = table };
            native = authoring_language_catalog_new(&input, error, sizeof error);
        }
        native_capabilities_free(table);
    }
    if (!native) {
        fprintf(stderr, "%s: unit fixture catalog: %s\n", fixture->root, error);
    }
    REQUIRE(native != NULL && fixture_components != NULL);
    /* These six sources depend only on native declarations and the loaded
     * components. This checks their real unit metadata without discovering
     * unrelated project declarations or executing any gameplay. */
    int failures = 0;
    for (int i = 0; i < fixture->count; i++) {
        script_syntax *syntax = script_parse(fixture_text[i]);
        analysis = script_analyze_document(
            syntax,
            authoring_language_catalog_view(native),
            fixture->sources[i]
        );
        if (!analysis->valid) {
            failures++;
            for (int j = 0; j < analysis->diagnostic_count && j < 8; j++) {
                const script_diagnostic *diagnostic = &analysis->diagnostics[j];
                fprintf(
                    stderr,
                    "%s:%d:%d: %s\n",
                    paths[i],
                    diagnostic->range.line,
                    diagnostic->range.column,
                    diagnostic->message
                );
            }
        }
        script_analysis_free(analysis);
        script_syntax_free(syntax);
        analysis = NULL;
    }
    printf("Unit fixture: %s, %d sources, %d failures\n", fixture->root, fixture->count, failures);
    REQUIRE(failures == 0);
    return true;
}
TEST_CASE(language_declared_unit_fixture)
{
    return check_unit_fixture(&declared_unit_fixture);
}
TEST_CASE(language_native_unit_fixtures)
{
    return check_unit_fixture(&native_unit_fixture);
}
void test_language_unit_fixtures()
{
    unit_source_bytes = 0;
    unit_component_bytes = 0;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(language_declared_unit_fixture);
    RUN_TEST_CASE(language_native_unit_fixtures);
    pu_clear_setup();
}
