#ifndef CHIBI_JSON_QUERY_H
#define CHIBI_JSON_QUERY_H
#include <stdbool.h>
#include <stddef.h>

enum
{
    JSON_QUERY_MAX_BYTES = 32 * 1024 * 1024,
    JSON_QUERY_MAX_VALUES = 100000,
    JSON_QUERY_MAX_DEPTH = 64,
    JSON_QUERY_MAX_POINTER = 4096
};
typedef struct
{
    bool found, boolean;
    const char *kind; /* Static: null, object, array, string, boolean, integer or number. */
    char *string, *json; /* Owned; empty string when not applicable. */
    double number;
    int count; /* Object members, array elements or string UTF-8 bytes. */
} json_query_result;
/* Initialize to zero before the first query. Each query frees previous output.
 * Reject duplicate object keys, embedded NULs and excessive nesting/size.
 * Missing values succeed with found=false; malformed pointers fail. */
bool query_json(const char *text, const char *pointer, json_query_result *, char *error, size_t error_size);
void json_query_result_free(json_query_result *);
#endif
