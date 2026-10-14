#ifndef CHIBI_SHADER_PUBLISH_H
#define CHIBI_SHADER_PUBLISH_H
#include "runtime/player.h"

/* Thread-confined resource generations. A context owns shader/model records,
 * values and overrides, images, meshes, skies, parts and prepared UI documents.
 * Enter only between completed simulation/render calls; complete GPU submission
 * before leaving. Nested scopes restore exactly the previous collection and its
 * cache/revision identities. A scope pins both contexts until leave; no resource
 * IO, compilation, upload or invalidation occurs merely by entering/leaving.
 * Scope tokens must be zero-initialized, kept alive and never copied while
 * active. Leave in reverse entry order before retiring either player.
 * Registered font faces are immutable shared engine resources: identical faces
 * reuse registrations, conflicting bytes/fallback reject preparation, and the
 * existing 128-face/64-MiB engine-lifetime budget applies. */
typedef struct render_resource_context render_resource_context;
typedef struct
{
    void *previous, *entered;
    unsigned depth;
    bool active;
} render_resource_scope;
/* An empty mutable authoring collection. Contexts created here are
 * caller-owned; clear source-owned authoring records before free as their APIs
 * require. Free returns false without changes while pinned by any scope. */
render_resource_context *render_resource_context_new();
bool render_resource_context_free(render_resource_context *, char *error, size_t size);
bool render_resource_scope_enter(
    render_resource_context *,
    render_resource_scope *,
    char *error,
    size_t size
);
bool render_resource_scope_leave(render_resource_scope *, char *error, size_t size);
/* Borrowed prepared collection, valid until its player retires. It may be
 * entered for that world's simulation/render without disturbing the app world. */
render_resource_context *render_resource_context_for_world(const world *);

/* Copied player hooks. The content root is borrowed during stage; it resolves
 * only declared external typed artifacts. Publication selects the collection;
 * replaced collections remain owned until their players retire. */
player_resource_host render_prepared_resources(const char *content_root);
/* Same validated preparation, but commit leaves the caller's collection active.
 * Use context_for_world + an explicit scope for simulation/render/publication. */
player_resource_host render_prepared_resources_detached(const char *content_root);
#endif
