#ifndef CHIBI_ASSETS_PROJECT_MIGRATE_H
#define CHIBI_ASSETS_PROJECT_MIGRATE_H

typedef struct av_doc av_doc;

#include <stdbool.h>
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
/* Version 20 declares components in .chibi. Returns the owned source of the
 * component a content/components KDL file declares, with each description as
 * `//` lines above what it describes, or NULL with `error` set when the file
 * is not one valid declaration or a name is a reserved word in .chibi. */
char *chibi_migrate_component(const char *kdl, char *error, size_t error_size);

/* Version 23 removes GAME. component namespaces and native Game type prefixes.
 * Semantic names are rewritten in .chibi source or KDL; unrelated strings and
 * comments stay intact. Returns an owned copy, or NULL when nothing changes
 * or KDL parsing fails (with error set). */
char *chibi_migrate_prefixes(const char *text, bool source, char *error, size_t error_size);

/* Version 24 replaces function declarations and match statements with :: and
 * switch. Leaves comments and names intact; *migrated stays NULL when
 * unchanged. Returns 0 on success or -1 with error set for invalid source. */
int chibi_migrate_declarations(const char *source, char **migrated, char *error, size_t error_size);

/* Version 25 spells explicit state/shared storage as retain/retain(system)
 * traits after the declaration name. Preserves comments and implicit root
 * declarations; *migrated stays NULL when unchanged. */
int chibi_migrate_retention(const char *source, char **migrated, char *error, size_t error_size);

/* Version 26 moves program kinds after their names, removes the named fn and
 * foreach spellings, and makes registration-owned retain implicit in systems.
 * Per-row system state fails rather than silently changing its lifetime.
 * Removes entity-owner metadata and intentionally unifies all source depth
 * limits at the new runtime limit of 64. Custom owner contracts fail. */
int chibi_migrate_syntax(const char *source, char **migrated, char *error, size_t error_size);

/* Version 27 spells conversions T(value): `cast<T>(v)` becomes `T(v)`, a
 * constant `N :: cast<T>(v)` becomes `N: T : v`, and a cast to
 * handle<ecs.entity> becomes entity(v). Sets `migrated` to an owned copy,
 * or NULL when the source has no casts. Returns -1 with `error` set for a
 * cast to a type with no name spelling outside a constant declaration, or a
 * result the version 27 grammar rejects. */
int chibi_migrate_conversions(const char *source, char **migrated, char *error, size_t error_size);

/* Version 28 reads parameters by name: `entry.X` of a parameter X becomes
 * `X`, and otherwise entry names the callable's only parameter, so `entry.x`
 * becomes `p.x` and a bare `entry` becomes `p`. An `after entry` handler
 * stays. Sets `migrated` to an owned copy, or NULL when the source reads no
 * parameter through entry. Returns -1 with `error` set when entry names no
 * parameter, when a local, constant, loop, handler or pattern binding of the
 * parameter's name would capture the rewritten read, or when the version 28
 * grammar rejects the result. */
int chibi_migrate_entry(const char *source, char **migrated, char *error, size_t error_size);

/* Version 29 runs a function's own body: the statements of an `after entry`
 * handler in a function move after the function's own statements, before the
 * handlers that follow, which is when they ran. Macros keep theirs. Sets
 * `migrated` to an owned copy, or NULL when no function has one. Returns -1
 * with `error` set when the handler names its payload or reads `event`,
 * declares a name the function also uses, or reads a parameter the
 * function's statements assign or pass to a callable declaring a `ref`
 * parameter; when a one-line function holds other statements; or when the
 * current grammar rejects the result. */
int chibi_migrate_after_entry(const char *source, char **migrated, char *error, size_t error_size);

#endif
