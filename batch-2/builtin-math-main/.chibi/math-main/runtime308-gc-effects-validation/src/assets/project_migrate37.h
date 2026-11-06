#ifndef CHIBI_PROJECT_MIGRATE37_H
#define CHIBI_PROJECT_MIGRATE37_H

#include <stddef.h>

/* Version 38 flattens system query bindings, replaces without/where with if
 * entries, and binds dt implicitly. Every old component binding becomes ^T,
 * including bindings only read, to preserve live access across suspension.
 * Join generators and sourced components move into the same list. Comments,
 * unrelated literals, ordinary callable parameters and event handlers remain.
 * Whole-component values and lvalues become name^; member receivers, change
 * tests, presence checks and lexical shadows retain their spelling. Optional
 * whole-component reads fail rather than introducing an unchecked dereference.
 * Compact R.T provider types become ^T from R, including partially upgraded
 * ^R.T types. Imported qualified component types keep their namespace. Paths
 * with more than two names or two sources fail for explicit disambiguation.
 * The query-only split provider alias becomes from; callable split parameters
 * retain their spelling and meaning.
 *
 * Results validate against the current grammar before publication. Already
 * migrated pointers remain pointers; current query lists retain snapshot
 * binding semantics while any remaining compact providers are expanded.
 * A plain value-only system header is valid in both grammars: the version 37
 * manifest gives it the old live-component meaning, so it must become ^T.
 * After that change rerunning is idempotent.
 *
 * Returns -1 with error set and no result for invalid old source, an untyped
 * legacy query binding, a non-Tick system payload, or a removed `query expr;`
 * declaration. Move that declaration's membership condition into an `if`
 * entry by hand, keeping any referenced declaration in scope. */
int chibi_migrate37_queries(const char *source, char **migrated, char *error, size_t size);

#endif
