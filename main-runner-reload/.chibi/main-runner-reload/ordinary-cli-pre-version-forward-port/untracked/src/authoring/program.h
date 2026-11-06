#ifndef CHIBI_AUTHORING_PROGRAM_H
#define CHIBI_AUTHORING_PROGRAM_H

#include "authoring/source_package_prepare.h"
#include "execution/module.h"
#include "runtime/native_capabilities.h"
#include "runtime/tasks.h"

typedef struct authoring_program authoring_program;
typedef struct
{
    native_capabilities *native; /* Retained by the session. */
    script_lower_bindings bindings;
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
     * completes its cleanup and exit policy. Nested invocations choose their
     * own policy; authored program.call always remains interruptible. */
    bool cooperative;
} authoring_program_call_options;

/* Sessions are confined to their controlling thread. Preparation never alters
 * the accepted generation. Commit uses the runtime's retained-state migration.
 * Binding callbacks and borrowed service/cancellation values outlive the
 * session. Diagnostic text is borrowed until its next mutating operation. */
authoring_program *authoring_program_new(const authoring_program_options *);
/* Takes ownership of the snapshot on success and failure, including busy
 * rejection. Replaces any prior unpublished candidate. */
bool authoring_program_compile(authoring_program *, source_package_snapshot *);
bool authoring_program_prepare(authoring_program *, const char *entry, bool restart);
bool authoring_program_commit(authoring_program *);
void authoring_program_discard(authoring_program *);
/* Calls the accepted zero-argument void/int entry synchronously. Returns a
 * runtime status separately from the authored result. NULL options select an
 * interruptible native call. Authored callers must own a different runtime
 * and managed heap. Calls reject active sessions and unpublished prepared
 * replacements. Cooperative hosts explicitly opt out of instruction-level
 * cancellation for this invocation only. */
int authoring_program_call(authoring_program *, const authoring_program_call_options *, int *result);
const authoring_program_diagnostic *authoring_program_error(const authoring_program *);
const execution_program *authoring_program_runtime(const authoring_program *);
bool authoring_program_busy(const authoring_program *);
void authoring_program_free(authoring_program *);

#endif
