#include "authoring/program.h"

#include "execution/status.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROGRAM_OWNER 1

struct authoring_program
{
    authoring_program_options options;
    service_binding *services;
    authoring_packages *accepted, *candidate;
    execution_program *runtime, *prepared;
    uint64_t entry, pending_entry;
    bool calling;
    authoring_program_diagnostic diagnostic;
};

static void clear_error(authoring_program *program)
{
    free((char *)program->diagnostic.path);
    free((char *)program->diagnostic.message);
    program->diagnostic = (authoring_program_diagnostic){ 0 };
}

static void diagnostic(
    authoring_program *program,
    int status,
    const char *path,
    script_range at,
    const char *message
)
{
    clear_error(program);
    program->diagnostic = (authoring_program_diagnostic){ .status = status,
                                                          .path = path ? xstrdup(path)
                                                                       : NULL,
                                                          .message = xstrdup(message),
                                                          .start = at.start,
                                                          .end = at.end,
                                                          .line = at.line,
                                                          .column = at.column };
}

static bool idle(authoring_program *program)
{
    if (!program->calling) {
        return true;
    }
    diagnostic(program, FLOW_ERR_STATE, NULL, (script_range){ 0 }, "Program is executing");
    return false;
}

static void invalid(authoring_program *program)
{
    const source_package_snapshot *snapshot = authoring_packages_snapshot(
        program->candidate
    );
    const source_package_snapshot_error *error = source_package_snapshot_error_at(snapshot);
    if (error) {
        int count = 0;
        const script_package_document
            *documents = source_package_snapshot_documents(snapshot, &count);
        int document = error->diagnostic.range.document;
        diagnostic(
            program,
            FLOW_ERR_COMPILE,
            document >= 0 && document < count ? documents[document].path : NULL,
            error->diagnostic.range,
            error->diagnostic.message
        );
        return;
    }
    for (int i = 0; i < authoring_packages_count(program->candidate); i++) {
        const authoring_package *package = authoring_packages_at(program->candidate, i);
        if (!package->diagnostic_count) {
            continue;
        }
        const script_diagnostic *error = &package->diagnostics[0];
        int document = error->range.document;
        const script_analysis *analysis = package->analysis;
        diagnostic(
            program,
            FLOW_ERR_COMPILE,
            analysis && document >= 0 && document < analysis->document_count
                ? analysis->documents[document].path
                : NULL,
            error->range,
            error->message
        );
        return;
    }
    const char *message = authoring_packages_error(program->candidate);
    diagnostic(program, FLOW_ERR_COMPILE, NULL, (script_range){ 0 }, message ? message : "Program compilation failed");
}

static const authoring_package *root(const authoring_packages *packages)
{
    for (int i = 0; i < authoring_packages_count(packages); i++) {
        const authoring_package *package = authoring_packages_at(packages, i);
        if (!strcmp(package->identity, "project:")) {
            return package;
        }
    }
    return NULL;
}

static uint64_t entry(const authoring_package *package, const char *name, bool *found)
{
    const script_analysis *analysis = package->analysis;
    for (int i = 0; i < analysis->symbol_count; i++) {
        const script_symbol *symbol = &analysis->symbols[i];
        if (symbol->scope != analysis->package_scope || strcmp(symbol->name, name)) {
            continue;
        }
        for (int j = 0; j < execution_module_function_count(package->module); j++) {
            execution_module_function_info
                function = execution_module_function_at(package->module, j);
            if (function.kind != EXECUTION_FUNCTION_AUTHORED ||
                function.id != symbol->id) {
                continue;
            }
            *found = true;
            int arguments = 0, results = 0;
            behavior_function_arguments(function.body, &arguments);
            const pin_value *signature = behavior_function_results(function.body, &results);
            if (!arguments && results <= 1 &&
                (!results ||
                 (signature[0].type == PIN_I32 && signature[0].container == PIN_CONTAINER_SINGLE &&
                  behavior_function_kind(function.body, BEHAVIOR_BANK_RESULT, 0) ==
                      BEHAVIOR_VALUE))) {
                return function.id;
            }
        }
    }
    return 0;
}

authoring_program *authoring_program_new(const authoring_program_options *options)
{
    assert(options && options->native);
    authoring_program *program = xcalloc(1, sizeof *program);
    program->options = *options;
    program->options.native = native_capabilities_retain(options->native);
    for (int i = 0; i < acount(options->services); i++) {
        service_binding binding = options->services[i];
        binding.type_name = xstrdup(binding.type_name);
        apush(program->services, binding);
    }
    program->options.services = program->services;
    return program;
}

void authoring_program_discard(authoring_program *program)
{
    assert(program && !program->calling);
    execution_program_free(program->prepared);
    program->prepared = NULL;
    authoring_packages_free(program->candidate);
    program->candidate = NULL;
    program->pending_entry = 0;
}

bool authoring_program_compile(authoring_program *program, source_package_snapshot *snapshot)
{
    assert(program && snapshot);
    if (!idle(program)) {
        source_package_snapshot_free(snapshot);
        return false;
    }
    authoring_program_discard(program);
    clear_error(program);
    program->candidate = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(program->options.native),
        &program->options.bindings,
        program->options.backend,
        program->accepted
    );
    if (!authoring_packages_valid(program->candidate)) {
        invalid(program);
        return false;
    }
    return true;
}

bool authoring_program_prepare(authoring_program *program, const char *name, bool restart)
{
    assert(program && name);
    if (!idle(program)) {
        return false;
    }
    execution_program_free(program->prepared);
    program->prepared = NULL;
    program->pending_entry = 0;
    if (!program->candidate || !authoring_packages_valid(program->candidate)) {
        diagnostic(program, FLOW_ERR_STATE, NULL, (script_range){ 0 }, "No compiled program candidate");
        return false;
    }
    const authoring_package *package = root(program->candidate);
    bool found = false;
    program->pending_entry = package ? entry(package, name, &found) : 0;
    if (!program->pending_entry) {
        char message[256];
        snprintf(
            message,
            sizeof message,
            found ? "%.180s must take no arguments and return nothing or int"
                  : "No runnable %.180s in the root package",
            name
        );
        diagnostic(program, FLOW_ERR_ARGUMENT, NULL, (script_range){ 0 }, message);
        return false;
    }
    if (program->runtime) {
        program->prepared = execution_program_prepare_replace(
            program->runtime,
            package->module,
            restart
        );
    } else {
        program->prepared = execution_program_new(package->module, NULL, program->services);
        if (program->prepared &&
            !execution_program_attach(program->prepared, PROGRAM_OWNER)) {
            execution_program_free(program->prepared);
            program->prepared = NULL;
        }
    }
    if (!program->prepared) {
        diagnostic(program, FLOW_ERR_STATE, NULL, (script_range){ 0 }, "Program runtime preparation failed");
        return false;
    }
    clear_error(program);
    return true;
}

bool authoring_program_commit(authoring_program *program)
{
    assert(program);
    if (!idle(program)) {
        return false;
    }
    if (!program->prepared || !program->candidate) {
        diagnostic(program, FLOW_ERR_STATE, NULL, (script_range){ 0 }, "No prepared program candidate");
        return false;
    }
    if (program->runtime) {
        execution_program_commit_replace(program->runtime, program->prepared);
    } else {
        program->runtime = program->prepared;
    }
    program->prepared = NULL;
    authoring_packages_free(program->accepted);
    program->accepted = program->candidate;
    program->candidate = NULL;
    program->entry = program->pending_entry;
    program->pending_entry = 0;
    clear_error(program);
    return true;
}

int authoring_program_call(
    authoring_program *program,
    const authoring_program_call_options *options,
    int *result
)
{
    assert(program && result);
    const execution_program *caller = options ? options->caller : NULL;
    if (!idle(program)) {
        return FLOW_ERR_STATE;
    }
    if (!program->runtime || program->prepared ||
        (caller && execution_program_managed_heap((execution_program *)caller) ==
                       execution_program_managed_heap(program->runtime))) {
        diagnostic(program, FLOW_ERR_STATE, NULL, (script_range){ 0 }, "Program is not ready for an isolated call");
        return FLOW_ERR_STATE;
    }
    clear_error(program);
    execution_program_clear_error(program->runtime);
    program->calling = true;
    behavior_execution_scope scope = { .remaining = UINT64_MAX,
                                       .cancelled = options && options->cooperative
                                                      ? NULL
                                                      : program->options.cancelled,
                                       .user = program->options.cancel_user,
                                       .prefer_compiled = true,
                                       .inherit = true };
    behavior_execution_scope_begin(&scope);
    execution_instance *instance = execution_program_instance(program->runtime, PROGRAM_OWNER);
    int status = execution_instance_call(instance, program->entry, result, 0, 0, UINT_MAX);
    behavior_execution_scope_end(&scope);
    program->calling = false;
    if (status != FLOW_OK) {
        const execution_program_failure *error = execution_program_error(program->runtime);
        diagnostic(
            program,
            status,
            error && error->asset_name[0] ? error->asset_name : NULL,
            (script_range){ .start = error ? error->start : 0,
                            .end = error ? error->end : 0,
                            .line = error ? error->line : 0,
                            .column = error ? error->column : 0 },
            error && error->message[0] ? error->message : "Program invocation failed"
        );
    } else {
        clear_error(program);
    }
    return status;
}

const authoring_program_diagnostic *authoring_program_error(const authoring_program *program)
{
    assert(program);
    return &program->diagnostic;
}

const execution_program *authoring_program_runtime(const authoring_program *program)
{
    assert(program);
    return program->runtime;
}

bool authoring_program_busy(const authoring_program *program)
{
    assert(program);
    return program->calling;
}

void authoring_program_free(authoring_program *program)
{
    if (!program) {
        return;
    }
    assert(!program->calling);
    authoring_program_discard(program);
    execution_program_free(program->runtime);
    authoring_packages_free(program->accepted);
    native_capabilities_free(program->options.native);
    for (int i = 0; i < acount(program->services); i++) {
        free((char *)program->services[i].type_name);
    }
    afree(program->services);
    clear_error(program);
    free(program);
}
