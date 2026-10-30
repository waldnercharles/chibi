#ifndef CHIBI_AUTHORING_PROGRAM_H
#define CHIBI_AUTHORING_PROGRAM_H

#include "authoring/source_package_io.h"
#include "execution/module.h"
#include "runtime/native_capabilities.h"
#include "runtime/tasks.h"

typedef struct authoring_program authoring_program;
typedef struct
{
    native_capabilities *native; /* Retained by the session. */
    behavior_backend backend;
    /* Rows and names are copied; service values outlive the session. */
    const service_binding *services;
    bool (*cancelled)(void *);
    void *cancel_user;
    /* Optional borrowed executor for pure @tests. Its submission scope belongs
     * to the host; ordinary program calls do not use it. Cancellation may be
     * polled concurrently by these workers. */
    const task_system *test_tasks;
} authoring_program_options;
typedef struct
{
    int status;
    const char *path, *message;
    int start, end, line, column;
} authoring_program_diagnostic;
typedef struct
{
    const execution_program *caller;
    /* The application handles cancellation through its own service calls and
     * completes its cleanup and exit policy. Enclosing scope constraints still
     * apply; authored program.call always remains interruptible. */
    bool cooperative;
} authoring_program_call_options;

/* Sessions are confined to their controlling thread. Preparation never alters
 * the accepted generation. Commit uses the runtime's retained-state migration.
 * Binding callbacks and borrowed service/cancellation values outlive the
 * session. Diagnostic text is borrowed until its next mutating operation. */
authoring_program *authoring_program_new(const authoring_program_options *);
/* Compiles the root package, that of the first source file directly in the
 * project root, with the packages it imports on the authored compiler. The
 * inputs are borrowed for the call. Replaces any prior unpublished candidate. */
bool authoring_program_compile(authoring_program *, const source_package_snapshot_options *inputs);
/* A non-empty entry must name a runnable root-package function; an empty one
 * requires none, for programs that start from `Main` or run a `Frame` hook. */
bool authoring_program_prepare(authoring_program *, const char *entry, bool restart);
bool authoring_program_commit(authoring_program *);
void authoring_program_discard(authoring_program *);
/* Calls the accepted zero-argument void/int entry synchronously. Returns a
 * runtime status separately from the authored result. NULL options select an
 * interruptible native call. Authored callers must own a different runtime
 * and managed heap. Calls reject active sessions and unpublished prepared
 * replacements. Cooperative hosts suppress only this session's cancellation
 * callback for this invocation. An active enclosing scope retains its shared
 * budget, cancellation, purity and VM restrictions. */
int authoring_program_call(authoring_program *, const authoring_program_call_options *, int *result);
/* Whether the accepted root package has a runnable zero-argument void/int
 * function of that name. Calling it by name looks the function up in the
 * accepted generation, so an update that committed earlier is already in effect. */
bool authoring_program_has(const authoring_program *, const char *name);
int authoring_program_call_entry(
    authoring_program *,
    const char *name,
    const authoring_program_call_options *,
    int *result
);
const authoring_program_diagnostic *authoring_program_error(const authoring_program *);
const execution_program *authoring_program_runtime(const authoring_program *);
bool authoring_program_busy(const authoring_program *);
void authoring_program_free(authoring_program *);

#endif
