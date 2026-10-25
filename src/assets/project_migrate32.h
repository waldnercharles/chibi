#ifndef CHIBI_PROJECT_MIGRATE32_H
#define CHIBI_PROJECT_MIGRATE32_H

#include <stddef.h>

/* Version 33 spells sized numbers with Odin's names and makes `number` a
 * 32-bit float: `float` becomes `number`, `double` `f64`, `byte` `u8`, `int8`
 * `i8`, `int16` `i16`, `uint16` `u16`, `uint` `u32`, `int64` `i64` and
 * `uint64` `u64`, in types, conversion calls and proven argument values.
 * `number` keeps its spelling. Sets `migrated` to an owned rewrite, or NULL
 * when nothing changes. Returns -1 with `error` set when the source declares
 * one of the new names, since it would then hide the type. */
int chibi_migrate32_number_names(const char *source, char **migrated, char *error, size_t size);
/* Checks all bindings and rewrite roles without producing edits. The project
 * coordinator calls this on every source before filtering or staging files. */
int chibi_migrate32_preflight(const char *source, char *error, size_t size);
/* The version 33 name of the version 32 scalar type `name`, or NULL when it
 * keeps its spelling. */
const char *chibi_migrate32_number_name(const char *name);

#endif
