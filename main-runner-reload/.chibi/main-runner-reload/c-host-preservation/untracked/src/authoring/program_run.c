#include "authoring/native_bindings.h"
#include "authoring/project_compile.h"
#include "authoring/source_package_files.h"
#include "authoring/source_package_prepare.h"
#include "execution/log.h"
#include "execution/module.h"
#include "execution/service.h"
#include "execution/status.h"
#include "host/native_default.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PROGRAM_OWNER 1

struct authoring_program
{
    char *path, *core;
    native_capabilities *native;
    authoring_packages *packages;
    execution_program *runtime;
    uint64_t entry;
    execution_log_service log;
    source_package_snapshot_options observed;
    char load_error[512];
};

static bool load(
    const char *path,
    const char *core,
    source_package_snapshot_options *inputs,
    source_package_load_error *error
)
{
    struct stat info;
    source_package_collection collection = { "core", core };
    if (!stat(path, &info) && S_ISDIR(info.st_mode)) {
        source_package_disk_options options = { path, &collection, 1 };
        return source_package_files_load(&options, inputs, error);
    }
    source_package_input *files = NULL, *library = NULL;
    if (!source_package_file_read(path, &files, error)) {
        return false;
    }
    if (!source_package_files_read(core, &library, error)) {
        source_package_files_free(files);
        return false;
    }
    for (int i = 0; i < acount(library); i++) {
        apush(files, library[i]);
    }
    afree(library);
    char *root = xstrdup(files[0].path);
    char *slash = strrchr(root, '/');
    slash[slash == root ? 1 : 0] = 0;
    source_package_collection *collections = xmalloc(sizeof *collections);
    *collections = (source_package_collection){ xstrdup("core"), xstrdup(core) };
    *inputs = (source_package_snapshot_options){ root, files, acount(files), collections, 1 };
    return true;
}

static bool unchanged(const source_package_snapshot_options *before, const source_package_snapshot_options *after)
{
    if (!before->project_root || before->file_count != after->file_count) {
        return false;
    }
    for (int i = 0; i < before->file_count; i++) {
        const source_package_input *a = &before->files[i], *b = &after->files[i];
        if (strcmp(a->path, b->path) || a->size != b->size ||
            memcmp(a->text, b->text, a->size)) {
            return false;
        }
    }
    return true;
}

static void report(const char *path, script_range at, const char *message)
{
    if (path) {
        fprintf(stderr, "%s:%d:%d: %s\n", path, at.line, at.column, message);
        return;
    }
    fprintf(stderr, "%s\n", message);
}

static void report_invalid(const authoring_packages *packages)
{
    const source_package_snapshot *snapshot = authoring_packages_snapshot(packages);
    const source_package_snapshot_error *error = source_package_snapshot_error_at(snapshot);
    if (error) {
        int count = 0;
        const script_package_document
            *documents = source_package_snapshot_documents(snapshot, &count);
        int document = error->diagnostic.range.document;
        report(
            document >= 0 && document < count ? documents[document].path : NULL,
            error->diagnostic.range,
            error->diagnostic.message
        );
        return;
    }
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *package = authoring_packages_at(packages, i);
        for (int j = 0; j < package->diagnostic_count; j++) {
            const script_diagnostic *diagnostic = &package->diagnostics[j];
            const script_analysis *analysis = package->analysis;
            int document = diagnostic->range.document;
            report(
                analysis && document >= 0 && document < analysis->document_count
                    ? analysis->documents[document].path
                    : NULL,
                diagnostic->range,
                diagnostic->message
            );
        }
    }
    const char *message = authoring_packages_error(packages);
    if (message) {
        report(NULL, (script_range){ 0 }, message);
    }
}

static const behavior_function *main_function(const authoring_packages *packages)
{
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *package = authoring_packages_at(packages, i);
        if (strcmp(package->identity, "project:")) {
            continue;
        }
        const script_analysis *analysis = package->analysis;
        for (int j = 0; j < analysis->symbol_count; j++) {
            const script_symbol *symbol = &analysis->symbols[j];
            if (symbol->scope != analysis->package_scope ||
                strcmp(symbol->name, "Main")) {
                continue;
            }
            for (int k = 0; k < execution_module_function_count(package->module); k++) {
                execution_module_function_info
                    function = execution_module_function_at(package->module, k);
                if (function.kind == EXECUTION_FUNCTION_AUTHORED &&
                    function.id == symbol->id) {
                    return function.body;
                }
            }
        }
    }
    return NULL;
}

static void print(void *, const execution_log_message *message)
{
    fwrite(message->text, 1, message->text_length, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static const authoring_package *root_package(const authoring_packages *packages)
{
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *package = authoring_packages_at(packages, i);
        if (!strcmp(package->identity, "project:")) {
            return package;
        }
    }
    return NULL;
}

static bool valid_main(const behavior_function *main)
{
    int parameters = 0, results = 0;
    behavior_function_arguments(main, &parameters);
    const pin_value *signature = behavior_function_results(main, &results);
    if (parameters || results > 1 ||
        (results &&
         (signature[0].type != PIN_I32 || signature[0].container != PIN_CONTAINER_SINGLE ||
          behavior_function_kind(main, BEHAVIOR_BANK_RESULT, 0) != BEHAVIOR_VALUE))) {
        fprintf(stderr, "Main must take no arguments and return nothing or int\n");
        return false;
    }
    return true;
}

bool authoring_program_reload(authoring_program *program)
{
    source_package_load_error error = { 0 };
    source_package_snapshot_options inputs = { 0 };
    if (!load(program->path, program->core, &inputs, &error)) {
        char message[sizeof program->load_error];
        snprintf(message, sizeof message, "%.300s: %.190s", error.path, error.message);
        if (strcmp(program->load_error, message)) {
            fprintf(stderr, "%s: %s\n", error.path, error.message);
            snprintf(program->load_error, sizeof program->load_error, "%s", message);
        }
        return false;
    }
    program->load_error[0] = 0;
    if (unchanged(&program->observed, &inputs)) {
        source_package_files_release(&inputs);
        return false;
    }
    source_package_files_release(&program->observed);
    program->observed = inputs;
    source_package_snapshot *snapshot = source_package_snapshot_new(&program->observed);
    script_lower_bindings bindings = authoring_native_bindings(program->native);
    authoring_packages *packages = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(program->native),
        &bindings,
        behavior_default_backend(),
        program->packages
    );
    if (!authoring_packages_valid(packages)) {
        report_invalid(packages);
        authoring_packages_free(packages);
        return false;
    }
    const behavior_function *main = main_function(packages);
    if (!main || !valid_main(main)) {
        if (!main) {
            fprintf(stderr, "No runnable Main function in the root package\n");
        }
        authoring_packages_free(packages);
        return false;
    }
    const authoring_package *root = root_package(packages);
    uint64_t entry = 0;
    for (int i = 0; i < execution_module_function_count(root->module); i++) {
        execution_module_function_info function = execution_module_function_at(root->module, i);
        if (function.body == main) {
            entry = function.id;
            break;
        }
    }
    execution_program *next;
    if (program->runtime) {
        next = execution_program_prepare_replace(program->runtime, root->module, false);
    } else {
        service_binding *services = NULL;
        apush(services, ((service_binding){ EXECUTION_LOG_SERVICE, &program->log }));
        next = execution_program_new(root->module, NULL, services);
        afree(services);
    }
    if (!next) {
        fprintf(stderr, "Main runtime preparation failed; keeping the last valid program\n");
        authoring_packages_free(packages);
        return false;
    }
    if (program->runtime) {
        execution_program_commit_replace(program->runtime, next);
    } else {
        program->runtime = next;
        if (!execution_program_attach(program->runtime, PROGRAM_OWNER)) {
            fprintf(stderr, "Main instance preparation failed\n");
            execution_program_free(program->runtime);
            program->runtime = NULL;
            authoring_packages_free(packages);
            return false;
        }
    }
    authoring_packages_free(program->packages);
    program->packages = packages;
    program->entry = entry;
    return true;
}

void authoring_program_free(authoring_program *program)
{
    if (!program) {
        return;
    }
    execution_program_free(program->runtime);
    authoring_packages_free(program->packages);
    native_capabilities_free(program->native);
    source_package_files_release(&program->observed);
    free(program->path);
    free(program->core);
    free(program);
}

authoring_program *authoring_program_open(const char *path, const char *core)
{
    authoring_program *program = xcalloc(1, sizeof *program);
    program->path = xstrdup(path);
    program->core = xstrdup(core);
    program->log.emit = print;
    char error[512];
    program->native = host_native_standalone_catalog_new(NULL, 0, error, sizeof error);
    if (!program->native) {
        fprintf(stderr, "Program preparation failed: %s\n", error);
        authoring_program_free(program);
        return NULL;
    }
    if (!authoring_program_reload(program)) {
        authoring_program_free(program);
        return NULL;
    }
    return program;
}

int authoring_program_call(authoring_program *program)
{
    execution_program_clear_error(program->runtime);
    execution_instance *instance = execution_program_instance(program->runtime, PROGRAM_OWNER);
    int code = 0;
    int status = execution_instance_call(instance, program->entry, &code, 0, 0, UINT_MAX);
    if (status != FLOW_OK) {
        const execution_program_failure *failure = execution_program_error(
            program->runtime
        );
        report(
            failure && failure->asset_name[0] ? failure->asset_name : NULL,
            (script_range){ .line = failure ? failure->line : 0,
                            .column = failure ? failure->column : 0 },
            failure && failure->message[0] ? failure->message : "Main execution failed"
        );
        return 1;
    }
    if (code < 0 || code > 255) {
        fprintf(stderr, "Main returned an exit status outside 0..255\n");
        return 1;
    }
    return code;
}

int authoring_program_run(const char *path, const char *core)
{
    authoring_program *program = authoring_program_open(path, core);
    if (!program) {
        return 1;
    }
    int status = authoring_program_call(program);
    authoring_program_free(program);
    return status;
}
