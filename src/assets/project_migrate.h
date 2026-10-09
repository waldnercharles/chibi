#ifndef CHIBI_ASSETS_PROJECT_MIGRATE_H
#define CHIBI_ASSETS_PROJECT_MIGRATE_H

typedef struct av_doc av_doc;

#include <stddef.h>

/* Upgrades the sources under `content`, and the canonical `start_scene` path
 * wherever it lies, from project version `from` to `from + 1`. Returns 0, or
 * -1 with `error` set. */
int chibi_project_migrate_content(
    const char *content,
    const char *start_scene,
    int from,
    char *error,
    size_t error_size
);
/* Version 11 starts a task for an unawaited call to a function that waits.
 * Given every version 10 source of a project, returns an owned array with an
 * owned copy of each source that gains `await` before the calls that blocked
 * under version 10, or NULL for a source that does not change. */
char **chibi_migrate_task_calls(const char *const *sources, int count);
/* Version 12 removes the `&` reference operator: a writable value passes to a
 * `ref` parameter directly. Returns an owned copy of `source` without its
 * unary `&` operators, binding a `fallback` through a leading `&x` as
 * `binding: x`, or NULL when the source does not change. */
char *chibi_migrate_references(const char *source);
/* Version 13 removes `result Name: T = e` declarations: each bare `return` of
 * their function returns the declared values in signature order, and a body
 * without a `return` ends with one. Returns an owned copy of `source`, or NULL
 * when it declares no results. */
char *chibi_migrate_results(const char *source);
/* Version 14 removes `value` declarations: each read of one becomes its
 * expression, which a typed declaration casts. Sets `migrated` to an owned
 * copy of `source`, or NULL when it declares none. When a declaration where
 * a value is read would capture a name the value reads, returns -1 and
 * describes it in `error`. */
int chibi_migrate_values(const char *source, char **migrated, char *error, size_t error_size);
/* Version 15 scene entities without a `behaviors` list keep their
 * prototype's behaviors. Returns an owned copy of a version 14 scene in which
 * every such entity lists none, or NULL when the text does not change or is
 * not a scene. */
char *chibi_migrate_scene_behaviors(const char *scene);
/* Version 16 moves GameActionBinding components into the project's input map.
 * Adds the action of each enabled binding in a scene or prototype `asset` to
 * the input map document `map`, and returns an owned copy of the asset
 * without them, or NULL when it has none. */
char *chibi_migrate_action_bindings(const char *asset, av_doc *map);
/* Version 17 removes `local` declarations: each handler, function or system
 * that reads one declares it with `:=` before its first statement that reads
 * it, in the innermost block holding those reads, and `local.X` reads become
 * `X`. Sets `migrated` to an owned copy of `source`, or NULL when it declares
 * none. A read outside any handler, such as an action argument, becomes the
 * local's expression. Returns -1 and describes the problem in `error` when a
 * declaration would capture a name the local reads. */
int chibi_migrate_locals(const char *source, char **migrated, char *error, size_t error_size);

#endif
