#include "authoring/program.h"

#include "authoring/package_compile.h"
#include "execution/function_builder.h"
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
    package_collection *accepted, *candidate;
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

typedef struct
{
    int start, end, line, column;
} position;

static void diagnostic(
    authoring_program *program,
    int status,
    const char *path,
    position at,
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
    diagnostic(program, FLOW_ERR_STATE, NULL, (position){ 0 }, "Program is executing");
    return false;
}

static const execution_module *root(const package_collection *packages)
{
    for (int i = 0; i < package_collection_count(packages); i++) {
        if (!strcmp(package_collection_name(packages, i), "project:")) {
            return package_collection_module(packages, i);
        }
    }
    return NULL;
}

/* The id of the root package's function of that name when it is a runnable
 * entry, else 0; `found` says whether a function of the name exists. */
static uint64_t entry(const execution_module *module, const char *name, bool *found)
{
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info function = execution_module_function_at(module, i);
        if (function.kind != EXECUTION_FUNCTION_AUTHORED || !function.entry ||
            strcmp(function.entry, name)) {
            continue;
        }
        *found = true;
        int arguments = 0, results = 0;
        behavior_function_arguments(function.body, &arguments);
        const pin_value *signature = behavior_function_results(function.body, &results);
        if (!arguments && results <= 1 &&
            (!results ||
             (signature[0].type == PIN_I32 && signature[0].container == PIN_CONTAINER_SINGLE &&
              behavior_function_kind(function.body, BEHAVIOR_BANK_RESULT, 0) == BEHAVIOR_VALUE))) {
            return function.id;
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
    package_collection_free(program->candidate);
    program->candidate = NULL;
    program->pending_entry = 0;
}

bool authoring_program_compile(authoring_program *program, const source_package_snapshot_options *inputs)
{
    assert(program && inputs);
    if (!idle(program)) {
        return false;
    }
    authoring_program_discard(program);
    clear_error(program);
    package_compile_error error;
    program->candidate = package_compile(
        inputs,
        NULL,
        NULL,
        program->options.native,
        program->options.backend,
        &error
    );
    if (!program->candidate) {
        diagnostic(
            program,
            FLOW_ERR_COMPILE,
            *error.path ? error.path : NULL,
            (position){ .line = error.line, .column = error.column },
            error.message
        );
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
    if (!program->candidate) {
        diagnostic(program, FLOW_ERR_STATE, NULL, (position){ 0 }, "No compiled program candidate");
        return false;
    }
    const execution_module *module = root(program->candidate);
    bool found = false;
    program->pending_entry = module && *name ? entry(module, name, &found) : 0;
    if (*name && !program->pending_entry) {
        char message[256];
        snprintf(
            message,
            sizeof message,
            found ? "%.180s must take no arguments and return nothing or int"
                  : "No runnable %.180s in the root package",
            name
        );
        diagnostic(program, FLOW_ERR_ARGUMENT, NULL, (position){ 0 }, message);
        return false;
    }
    if (program->runtime) {
        program->prepared = execution_program_prepare_replace(
            program->runtime,
            (execution_module *)module,
            restart
        );
    } else {
        program->prepared = execution_program_new((execution_module *)module, NULL, program->services);
        if (program->prepared &&
            !execution_program_attach(program->prepared, PROGRAM_OWNER)) {
            execution_program_free(program->prepared);
            program->prepared = NULL;
        }
    }
    if (!program->prepared) {
        diagnostic(program, FLOW_ERR_STATE, NULL, (position){ 0 }, "Program runtime preparation failed");
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
        diagnostic(program, FLOW_ERR_STATE, NULL, (position){ 0 }, "No prepared program candidate");
        return false;
    }
    if (program->runtime) {
        execution_program_commit_replace(program->runtime, program->prepared);
    } else {
        program->runtime = program->prepared;
    }
    program->prepared = NULL;
    package_collection_free(program->accepted);
    program->accepted = program->candidate;
    program->candidate = NULL;
    program->entry = program->pending_entry;
    program->pending_entry = 0;
    clear_error(program);
    return true;
}

static int invoke(
    authoring_program *program,
    uint64_t function,
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
        diagnostic(program, FLOW_ERR_STATE, NULL, (position){ 0 }, "Program is not ready for an isolated call");
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
    int status = execution_instance_call(instance, function, result, 0, 0, UINT_MAX);
    behavior_execution_scope_end(&scope);
    program->calling = false;
    if (status != FLOW_OK) {
        const execution_program_failure *error = execution_program_error(program->runtime);
        diagnostic(
            program,
            status,
            error && error->asset_name[0] ? error->asset_name : NULL,
            (position){ .start = error ? error->start : 0,
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

int authoring_program_call(
    authoring_program *program,
    const authoring_program_call_options *options,
    int *result
)
{
    return invoke(program, program->entry, options, result);
}

bool authoring_program_has(const authoring_program *program, const char *name)
{
    assert(program && name);
    const execution_module *module = program->accepted ? root(program->accepted) : NULL;
    bool found = false;
    return module && entry(module, name, &found);
}

int authoring_program_call_entry(
    authoring_program *program,
    const char *name,
    const authoring_program_call_options *options,
    int *result
)
{
    assert(program && name && result);
    if (!idle(program)) {
        return FLOW_ERR_STATE;
    }
    const execution_module *module = program->accepted ? root(program->accepted) : NULL;
    bool found = false;
    uint64_t function = module ? entry(module, name, &found) : 0;
    if (!function) {
        char message[256];
        snprintf(
            message,
            sizeof message,
            found ? "%.180s must take no arguments and return nothing or int"
                  : "No runnable %.180s in the root package",
            name
        );
        diagnostic(program, FLOW_ERR_ARGUMENT, NULL, (position){ 0 }, message);
        return FLOW_ERR_ARGUMENT;
    }
    return invoke(program, function, options, result);
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
    package_collection_free(program->accepted);
    native_capabilities_free(program->options.native);
    for (int i = 0; i < acount(program->services); i++) {
        free((char *)program->services[i].type_name);
    }
    afree(program->services);
    clear_error(program);
    free(program);
}
