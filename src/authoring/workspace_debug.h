#ifndef CHIBI_WORKSPACE_DEBUG_H
#define CHIBI_WORKSPACE_DEBUG_H
#include "execution/inspection.h"
#include "runtime/behaviors/source.h"
#include "runtime/declared_components.h"
typedef struct workspace_debug workspace_debug;
typedef struct entities entities;
/* Resolver is called synchronously at quiescent boundaries. It returns the
 * current registry for this session, or NULL after retirement. */
typedef source_registry *(*workspace_debug_registry)(void *, int session);
workspace_debug *workspace_debug_new(workspace_debug_registry, void *);
void workspace_debug_free(workspace_debug *);
/* Optional borrowed type resolver is retained per observation. Live mutation
 * enters the host's complete resource/type scope, then leaves it once. */
/* Resolver returns the current session's entity service at a synchronous,
 * quiescent boundary. No pointer may survive the host call. */
void workspace_debug_set_entities(workspace_debug *, entities *(*)(void *, int session));
entities *workspace_debug_entities(workspace_debug *, int session);
void workspace_debug_set_source_root(workspace_debug *, const char *(*)(void *, int session));
bool workspace_debug_source(
    workspace_debug *,
    int handle,
    const char *asset,
    const char **path,
    const char **text,
    char *error,
    size_t
);
typedef struct
{
    const char *prototype;
    uint64_t creation;
    int object;
} workspace_debug_owner;
void workspace_debug_set_owner_resolver(
    workspace_debug *,
    bool (*)(void *, int session, uint64_t owner, workspace_debug_owner *)
);
bool workspace_debug_owner_read(workspace_debug *, int session, uint64_t owner, workspace_debug_owner *);
void workspace_debug_set_types(workspace_debug *, declared_components *(*)(void *, int session));
void workspace_debug_set_scope(
    workspace_debug *,
    void *(*enter)(void *, int session, const char **error),
    void (*leave)(void *, void *token)
);
source_registry *workspace_debug_sources(workspace_debug *, int session);
/* At most 16 owned observations. Monotonic handles never alias retired ones.
 * Inspectors and module generations survive program/world retirement. */
bool workspace_debug_open(
    workspace_debug *,
    int session,
    const char *name,
    uint64_t owner,
    int *handle,
    char *error,
    size_t
);
bool workspace_debug_close(workspace_debug *, int handle);
typedef struct
{
    int session;
    const char *name;
    uint64_t owner;
    bool live;
    execution_debug_cursor cursor;
    const execution_inspector *inspector;
    const execution_module *module;
    const execution_program_failure *failure;
    declared_components *types;
} workspace_debug_view;
bool workspace_debug_read(workspace_debug *, int handle, workspace_debug_view *);
bool workspace_debug_control(
    workspace_debug *,
    int handle,
    const char *operation,
    uint64_t module,
    uint64_t function,
    uint64_t occurrence,
    bool enabled,
    char *error,
    size_t
);
#endif
