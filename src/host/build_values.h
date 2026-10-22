#ifndef CHIBI_HOST_BUILD_VALUES_H
#define CHIBI_HOST_BUILD_VALUES_H

#include "support/arena.h"
#include "types/array.h"

/* One text value: room for a build graph's compile_commands.json, which
 * passed 1 MB at about 580 sources. */
#define BUILD_TEXT_LIMIT (16 * 1024 * 1024)
#define BUILD_ARRAY_LIMIT 8192
/* Values are never freed. An up-to-date build graph holds every compile
 * record it reads, about 15 MB for 362 sources with sanitizers. The limit
 * counts the arena's doubling blocks, so it holds about half as many bytes of
 * values: room for the whole engine, while still stopping a runaway program. */
#define BUILD_STORAGE_LIMIT (256 * 1024 * 1024)

typedef struct
{
    arena memory;
    const char *error;
} build_values;

const pin_string *build_values_text(build_values *, const char *, size_t);
pin_array *build_values_array(build_values *, int);
const char *build_values_cstring(build_values *, pin_value, const char **);
bool build_values_string(const pin_value *);
bool build_values_strings(const pin_value *);
const char *build_values_join(build_values *, const pin_value *, pin_value *);
const char *build_values_starts_with(build_values *, const pin_value *, pin_value *);
const char *build_values_replace(build_values *values, const pin_value *arguments, pin_value *results);
const char *build_values_before(build_values *, const pin_value *, pin_value *);
const char *build_values_after(build_values *, const pin_value *, pin_value *);
const char *build_values_lines(build_values *, const pin_value *, pin_value *);
const char *build_values_trim(build_values *, const pin_value *, pin_value *);
const char *build_values_concat(build_values *, const pin_value *, pin_value *);
const char *build_values_length(build_values *, const pin_value *, pin_value *);
#endif
