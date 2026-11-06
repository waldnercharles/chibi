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

#include <assert.h>
#include <errno.h>
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
    source_package_snapshot_options sources;
    uint64_t revision, error_revision, pending_entry;
    char load_error[512];
    authoring_packages *candidate;
    execution_program *prepared;
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

static void capture_diagnostic(
    authoring_program *program,
    const char *path,
    script_range at,
    const char *message
)
{
    if (path) {
        snprintf(
            program->load_error,
            sizeof program->load_error,
            "%.300s:%d:%d: %.175s",
            path,
            at.line,
            at.column,
            message
        );
    } else {
        snprintf(program->load_error, sizeof program->load_error, "%s", message);
    }
    program->error_revision++;
}

static void capture_invalid(authoring_program *program, const authoring_packages *packages)
{
    const source_package_snapshot *snapshot = authoring_packages_snapshot(packages);
    const source_package_snapshot_error *error = source_package_snapshot_error_at(snapshot);
    if (error) {
        int count = 0;
        const script_package_document
            *documents = source_package_snapshot_documents(snapshot, &count);
        int document = error->diagnostic.range.document;
        capture_diagnostic(
            program,
            document >= 0 && document < count ? documents[document].path : NULL,
            error->diagnostic.range,
            error->diagnostic.message
        );
        return;
    }
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *package = authoring_packages_at(packages, i);
        if (!package->diagnostic_count) {
            continue;
        }
        const script_diagnostic *diagnostic = &package->diagnostics[0];
        const script_analysis *analysis = package->analysis;
        int document = diagnostic->range.document;
        capture_diagnostic(
            program,
            analysis && document >= 0 && document < analysis->document_count
                ? analysis->documents[document].path
                : NULL,
            diagnostic->range,
            diagnostic->message
        );
        return;
    }
    const char *message = authoring_packages_error(packages);
    capture_diagnostic(program, NULL, (script_range){ 0 }, message ? message : "Program compilation failed");
}

static const behavior_function *entry_function(const authoring_packages *packages, const char *name)
{
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *package = authoring_packages_at(packages, i);
        if (strcmp(package->identity, "project:")) {
            continue;
        }
        const script_analysis *analysis = package->analysis;
        for (int j = 0; j < analysis->symbol_count; j++) {
            const script_symbol *symbol = &analysis->symbols[j];
            if (symbol->scope != analysis->package_scope || strcmp(symbol->name, name)) {
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

static bool valid_entry(const behavior_function *main)
{
    int parameters = 0, results = 0;
    behavior_function_arguments(main, &parameters);
    const pin_value *signature = behavior_function_results(main, &results);
    if (parameters || results > 1 ||
        (results &&
         (signature[0].type != PIN_I32 || signature[0].container != PIN_CONTAINER_SINGLE ||
          behavior_function_kind(main, BEHAVIOR_BANK_RESULT, 0) != BEHAVIOR_VALUE))) {
        return false;
    }
    return true;
}

static void load_failure(authoring_program *program, const char *path, const char *message)
{
    char text[sizeof program->load_error];
    snprintf(text, sizeof text, "%.300s: %.190s", path, message);
    if (strcmp(program->load_error, text)) {
        snprintf(program->load_error, sizeof program->load_error, "%s", text);
        program->error_revision++;
    }
}

bool authoring_program_select(authoring_program *program, const char *path)
{
    assert(!program->path && !program->runtime);
    program->path = realpath(path, NULL);
    if (!program->path) {
        load_failure(program, path, strerror(errno));
        return false;
    }
    return true;
}

uint64_t authoring_program_snapshot(authoring_program *program)
{
    source_package_load_error error = { 0 };
    source_package_snapshot_options inputs = { 0 };
    if (!program->path) {
        load_failure(program, "program", "No source target selected");
        return 0;
    }
    if (!load(program->path, program->core, &inputs, &error)) {
        load_failure(program, error.path, error.message);
        return 0;
    }
    program->load_error[0] = 0;
    if (unchanged(&program->sources, &inputs)) {
        source_package_files_release(&inputs);
        return program->revision;
    }
    source_package_files_release(&program->sources);
    program->sources = inputs;
    program->revision++;
    assert(program->revision);
    return program->revision;
}

uint64_t authoring_program_error_revision(const authoring_program *program)
{
    return program->error_revision;
}

const char *authoring_program_error(const authoring_program *program)
{
    return program->load_error;
}

bool authoring_program_compile(authoring_program *program)
{
    assert(program->sources.project_root && !program->candidate && !program->prepared);
    source_package_snapshot *snapshot = source_package_snapshot_new(&program->sources);
    script_lower_bindings bindings = authoring_native_bindings(program->native);
    program->candidate = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(program->native),
        &bindings,
        behavior_default_backend(),
        program->packages
    );
    if (!authoring_packages_valid(program->candidate)) {
        capture_invalid(program, program->candidate);
        return false;
    }
    return true;
}

bool authoring_program_prepare(authoring_program *program, const char *name)
{
    assert(
        program->candidate && authoring_packages_valid(program->candidate) &&
        !program->prepared
    );
    const behavior_function *entry = entry_function(program->candidate, name);
    if (!entry || !valid_entry(entry)) {
        char message[256];
        snprintf(
            message,
            sizeof message,
            entry ? "%.200s must take no arguments and return nothing or int"
                  : "No runnable %.200s in the root package",
            name
        );
        capture_diagnostic(program, NULL, (script_range){ 0 }, message);
        return false;
    }
    const authoring_package *root = root_package(program->candidate);
    for (int i = 0; i < execution_module_function_count(root->module); i++) {
        execution_module_function_info function = execution_module_function_at(root->module, i);
        if (function.body == entry) {
            program->pending_entry = function.id;
            break;
        }
    }
    if (program->runtime) {
        program->prepared = execution_program_prepare_replace(program->runtime, root->module, false);
    } else {
        service_binding *services = NULL;
        apush(services, ((service_binding){ EXECUTION_LOG_SERVICE, &program->log }));
        program->prepared = execution_program_new(root->module, NULL, services);
        afree(services);
        if (program->prepared &&
            !execution_program_attach(program->prepared, PROGRAM_OWNER)) {
            execution_program_free(program->prepared);
            program->prepared = NULL;
        }
    }
    if (!program->prepared) {
        load_failure(program, name, "runtime preparation failed; keeping the last valid program");
        return false;
    }
    return true;
}

void authoring_program_commit(authoring_program *program)
{
    assert(program->prepared && program->candidate);
    if (program->runtime) {
        execution_program_commit_replace(program->runtime, program->prepared);
    } else {
        program->runtime = program->prepared;
    }
    program->prepared = NULL;
    authoring_packages_free(program->packages);
    program->packages = program->candidate;
    program->candidate = NULL;
    program->entry = program->pending_entry;
}

void authoring_program_discard(authoring_program *program)
{
    execution_program_free(program->prepared);
    program->prepared = NULL;
    authoring_packages_free(program->candidate);
    program->candidate = NULL;
    program->pending_entry = 0;
}

void authoring_program_free(authoring_program *program)
{
    if (!program) {
        return;
    }
    authoring_program_discard(program);
    execution_program_free(program->runtime);
    authoring_packages_free(program->packages);
    native_capabilities_free(program->native);
    source_package_files_release(&program->sources);
    free(program->path);
    free(program->core);
    free(program);
}

authoring_program *authoring_program_new(const char *core)
{
    authoring_program *program = xcalloc(1, sizeof *program);
    program->core = xstrdup(core);
    program->log.emit = print;
    char error[512];
    program->native = host_native_standalone_catalog_new(NULL, 0, error, sizeof error);
    if (!program->native) {
        fprintf(stderr, "Program preparation failed: %s\n", error);
        authoring_program_free(program);
        return NULL;
    }
    return program;
}

int authoring_program_call(authoring_program *program, const execution_program *caller)
{
    assert(program->runtime && caller && caller != program->runtime);
    assert(
        execution_program_managed_heap((execution_program *)caller) !=
        execution_program_managed_heap(program->runtime)
    );
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
    return code;
}
