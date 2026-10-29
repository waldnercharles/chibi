#include "assets/project.h"
#include "authoring/project_compile.h"
#include "authoring/project_format.h"
#include "authoring/project_prepare.h"
#include "authoring/source_catalog.h"
#include "runtime/component_catalog.h"
#include "support/file_io.h"
#include "support/xalloc.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_target.h"
#include "types/catalog.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Explicit final application composition, shared with the prepared player. */
int type_catalog_pack_count()
{
    return 1;
}
type_catalog_pack type_catalog_pack_at(int index)
{
    assert(index == 0);
    (void)index;
    return TYPE_PACK;
}
static int usage(const char *program)
{
    fprintf(
        stderr,
        "Usage: %s --project-root PATH (--output FILE [--modules FILE | "
        "--resources-only] | "
        "--check [--explain-costs [--json]] | --test | "
        "--format [--check] | "
        "--docs | --stage-one-input "
        "FILE) [--scene PATH]\n",
        program
    );
    return 2;
}
#ifdef __EMSCRIPTEN__
/* The browser-target compiler only prepares projects for export. */
static int test(const char *)
{
    fprintf(stderr, "This compiler prepares browser exports; it runs no tests\n");
    return 2;
}
static int format(const char *, bool)
{
    fprintf(stderr, "This compiler prepares browser exports; it formats nothing\n");
    return 2;
}
static int docs(const char *)
{
    fprintf(stderr, "This compiler prepares browser exports; it writes no documentation\n");
    return 2;
}
#else
/* A folder of the SDK's share/chibi beside this program's directory, such as
 * its core collection; NULL, having said why, when it has none. */
static char *shared(const char *name, const char *what)
{
    char *bin = file_executable_dir();
    size_t size = strlen(bin) + strlen(name) + sizeof "/../share/chibi/";
    char *installed = xmalloc(size);
    snprintf(installed, size, "%s/../share/chibi/%s", bin, name);
    char *folder = realpath(installed, NULL);
    if (!folder) {
        fprintf(stderr, "The SDK has no %s at %s: %s\n", what, installed, strerror(errno));
    }
    free(installed);
    free(bin);
    return folder;
}
static char *core_collection()
{
    return shared("core", "core collection");
}
/* Runs the tests of the packages below project. */
static int test(const char *project)
{
    char *root = realpath(project, NULL);
    if (!root) {
        fprintf(stderr, "%s: %s\n", project, strerror(errno));
        return 1;
    }
    char *core = core_collection();
    int status = core ? authoring_project_test(root, core) : 1;
    free(core);
    free(root);
    return status;
}
/* Writes the documentation site of the root at project with the SDK's site
 * generator. */
static int docs(const char *project)
{
    char *root = realpath(project, NULL);
    if (!root) {
        fprintf(stderr, "%s: %s\n", project, strerror(errno));
        return 1;
    }
    char *generator = shared("docs", "site generator");
    char *core = generator ? core_collection() : NULL;
    int status = core ? authoring_project_docs(root, generator, core) : 1;
    free(core);
    free(generator);
    free(root);
    return status;
}
/* Formats the package sources below a folder that holds no project.kdl. */
static int format_packages(const char *folder, bool check)
{
    char *root = realpath(folder, NULL);
    if (!root) {
        fprintf(stderr, "%s: %s\n", folder, strerror(errno));
        return 1;
    }
    int differing = authoring_packages_format(root, check);
    free(root);
    return differing < 0 || (check && differing) ? 1 : 0;
}
/* Formats the project's assets with the composition chibi build uses, or the
 * package sources below a folder without a project.kdl; with check only
 * reports what differs. Fails for an older project, which it does not
 * migrate, an asset that differs under check, an invalid one or a failed
 * write. */
static int format(const char *project, bool check)
{
    if (chibi_project_package_folder(project)) {
        return format_packages(project, check);
    }
    char error[512] = { 0 };
    chibi_project read = { 0 };
    bool current = !chibi_project_read(&read, project, error, sizeof error);
    chibi_project_free(&read);
    authoring_application application;
    if (!current || !authoring_application_begin(&application, error, sizeof error)) {
        fprintf(stderr, "format: %s\n", error);
        return 1;
    }
    int differing = authoring_project_format(project, application.native, check);
    authoring_application_end(&application);
    return differing < 0 || (check && differing) ? 1 : 0;
}
#endif
static bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    bool written = file && fputs(text, file) >= 0;
    return file && !fclose(file) && written;
}
/* Writes stage one's CompileBehaviorsWithCatalog input: the project sources
 * and what their modules link against in the catalog they compile
 * against. Reports the executable source count on stdout only after the
 * complete input has been written. */
static int stage_one_input(const char *project, const char *output)
{
    char error[512] = { 0 };
    authoring_application application;
    if (!authoring_application_begin(&application, error, sizeof error)) {
        fprintf(stderr, "stage-one input: %s\n", error);
        return 1;
    }
    authoring_project_options options = { .project = project,
                                          .native = application.native };
    authoring_project_catalog *catalog = authoring_project_catalog_open(&options, error, sizeof error);
    size_t length = 0;
    char *json = NULL;
    int count = 0;
    if (catalog) {
        const source_catalog *sources = authoring_project_catalog_sources(catalog);
        count = source_catalog_source_count(sources);
        const char **identities = xcalloc((size_t)count + 1, sizeof *identities);
        const char **texts = xcalloc((size_t)count + 1, sizeof *texts);
        for (int i = 0; i < count; i++) {
            identities[i] = source_catalog_source_at(sources, i)->identity;
            texts[i] = source_catalog_source_at(sources, i)->text;
        }
        json = compiler_behavior_catalog_input_json(
            source_catalog_view(sources),
            application.native,
            identities,
            texts,
            count,
            &length
        );
        free(identities);
        free(texts);
    }
    authoring_project_catalog_free(catalog);
    authoring_application_end(&application);
    bool written = json && write_text(output, json);
    free(json);
    if (written) {
        written = printf("%d\n", count) >= 0 && !fflush(stdout);
    }
    if (!written) {
        fprintf(stderr, "stage-one input: %s\n", error[0] ? error : "could not write the input");
    }
    return written ? 0 : 1;
}
/* The bytes of a file, or NULL. */
static unsigned char *read_bytes(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    unsigned char *bytes = NULL;
    long size = fseek(file, 0, SEEK_END) ? -1 : ftell(file);
    if (size > 0 && (size_t)size <= PACKAGE_MAX_BYTES && !fseek(file, 0, SEEK_SET)) {
        bytes = xmalloc((size_t)size);
        if (fread(bytes, 1, (size_t)size, file) != (size_t)size) {
            free(bytes);
            bytes = NULL;
        }
    }
    fclose(file);
    *length = bytes ? (size_t)size : 0;
    return bytes;
}
/* --output with --modules writes the artifact assembled from a closure of the
 * project's prepared modules, as the stage-one compiler writes it. */
static int assemble(const char *project, const char *closure, const char *output)
{
    char error[512] = { 0 };
    size_t length = 0;
    unsigned char *bytes = read_bytes(closure, &length);
    int count = 0;
    uint64_t root = 0;
    package_module *modules = bytes
                                ? compiler_artifact_rows(bytes, length, &count, &root, error, sizeof error)
                                : NULL;
    int status = modules
                   ? authoring_project_compile_modules(project, modules, count, output)
                   : 1;
    if (!modules) {
        fprintf(stderr, "modules: %s\n", bytes ? error : "the closure cannot be read");
    }
    compiler_artifact_rows_free(modules, count);
    free(bytes);
    return status;
}
/* --check prepares the project without migrating it or writing anything;
 * --test runs its package tests; --format rewrites its assets canonically, or
 * with --check reports the ones that are not; --docs writes its documentation
 * site. */
int main(int argc, char **argv)
{
    const char *project = NULL, *scene = NULL, *output = NULL, *target = NULL,
               *modules = NULL;
    bool check = false, tests = false, formats = false, site = false, resources = false;
    bool explain = false, json = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        }
        if (!strcmp(argv[i], "--check") && !check) {
            check = true;
            continue;
        }
        if (!strcmp(argv[i], "--explain-costs") && !explain) {
            explain = true;
            continue;
        }
        if (!strcmp(argv[i], "--json") && !json) {
            json = true;
            continue;
        }
        if (!strcmp(argv[i], "--test") && !tests) {
            tests = true;
            continue;
        }
        if (!strcmp(argv[i], "--format") && !formats) {
            formats = true;
            continue;
        }
        if (!strcmp(argv[i], "--docs") && !site) {
            site = true;
            continue;
        }
        if (!strcmp(argv[i], "--resources-only") && !resources) {
            resources = true;
            continue;
        }
        if (i + 1 == argc) {
            return usage(argv[0]);
        }
        if (!strcmp(argv[i], "--project-root") && !project) {
            project = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--scene") && !scene) {
            scene = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--output") && !output) {
            output = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--stage-one-input") && !target) {
            target = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--modules") && !modules) {
            modules = argv[++i];
            continue;
        }
        return usage(argv[0]);
    }
    int modes = (check && !formats) + tests + formats + site +
                (output != NULL) + (target != NULL);
    if (!project || !*project || modes != 1 || (output && !*output) ||
        (target && !*target) || ((tests || formats || site || target) && scene) ||
        (modules && (!output || !*modules || scene)) ||
        (resources && (!output || modules || scene)) ||
        (explain && (!check || formats)) || (json && !explain)) {
        return usage(argv[0]);
    }
    if (resources) {
        return authoring_project_compile_modules(project, NULL, 0, output);
    }
    if (modules) {
        return assemble(project, modules, output);
    }
    if (target) {
        return stage_one_input(project, target);
    }
    if (site) {
        return docs(project);
    }
    if (tests) {
        return test(project);
    }
    if (formats) {
        return format(project, check);
    }
    if (explain) {
        return authoring_project_explain_costs(project, scene, json);
    }
    return authoring_project_compile(project, scene, output);
}
