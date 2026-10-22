#include "host/build_values.h"

#include <ctype.h>
#include <string.h>

static void *allocate(build_values *values, size_t size)
{
    void *data = arena_alloc_bounded(&values->memory, size, BUILD_STORAGE_LIMIT);
    if (!data) {
        values->error = "build value storage limit exceeded";
    }
    return data;
}
char *build_values_text(build_values *values, const char *text, size_t size)
{
    if (size > BUILD_TEXT_LIMIT) {
        values->error = "build text limit exceeded";
        return NULL;
    }
    char *copy = allocate(values, size + 1);
    if (copy) {
        memcpy(copy, text, size);
        copy[size] = 0;
    }
    return copy;
}
pin_array *build_values_array(build_values *values, int count)
{
    if (count < 0 || count > BUILD_ARRAY_LIMIT) {
        values->error = "build array limit exceeded";
        return NULL;
    }
    pin_array *array = allocate(values, sizeof(*array));
    if (!array) {
        return NULL;
    }
    *array = (pin_array){ .element_type = PIN_STR, .count = count, .capacity = count };
    if (count) {
        array->items = allocate(values, sizeof(*array->items) * count);
        if (!array->items) {
            return NULL;
        }
    }
    return array;
}
bool build_values_string(const pin_value *value)
{
    return value->type == PIN_STR && value->container == PIN_CONTAINER_SINGLE &&
           value->str &&
           strnlen(value->str, BUILD_TEXT_LIMIT + 1) <= BUILD_TEXT_LIMIT;
}
bool build_values_strings(const pin_value *value)
{
    const pin_array *array = value->ptr;
    if (value->type != PIN_STR || value->container != PIN_CONTAINER_ARRAY ||
        !array || array->element_type != PIN_STR || array->count < 0 ||
        array->count > BUILD_ARRAY_LIMIT || (array->count && !array->items)) {
        return false;
    }
    size_t bytes = 0;
    for (int i = 0; i < array->count; i++) {
        if (!build_values_string(&array->items[i])) {
            return false;
        }
        size_t size = strlen(array->items[i].str) + 1;
        if (size > BUILD_TEXT_LIMIT - bytes) {
            return false;
        }
        bytes += size;
    }
    return true;
}
static const char *text_result(build_values *values, pin_value *result, const char *text, size_t size)
{
    result->str = build_values_text(values, text, size);
    return result->str ? NULL : values->error;
}
const char *build_values_join(build_values *values, const pin_value *arguments, pin_value *results)
{
    if (!build_values_strings(&arguments[0]) || !build_values_string(&arguments[1])) {
        return "invalid build string arguments";
    }
    const pin_array *array = arguments[0].ptr;
    size_t size = 0, separator = strlen(arguments[1].str);
    for (int i = 0; i < array->count; i++) {
        size_t addition = strlen(array->items[i].str) + (i ? separator : 0);
        if (addition > BUILD_TEXT_LIMIT - size) {
            return "build text limit exceeded";
        }
        size += addition;
    }
    char *text = allocate(values, size + 1);
    if (!text) {
        return values->error;
    }
    size_t offset = 0;
    for (int i = 0; i < array->count; i++) {
        if (i) {
            memcpy(text + offset, arguments[1].str, separator);
            offset += separator;
        }
        size_t length = strlen(array->items[i].str);
        memcpy(text + offset, array->items[i].str, length);
        offset += length;
    }
    text[size] = 0;
    results[0].str = text;
    return NULL;
}
/* Every occurrence of a non-empty `find` in text, left to right, replaced by
 * `with`. */
const char *build_values_replace(build_values *values, const pin_value *arguments, pin_value *results)
{
    if (!build_values_string(&arguments[0]) || !build_values_string(&arguments[1]) ||
        !build_values_string(&arguments[2]) || !arguments[1].str[0]) {
        return "invalid build string arguments";
    }
    const char *text = arguments[0].str, *find = arguments[1].str,
               *with = arguments[2].str;
    size_t find_size = strlen(find), with_size = strlen(with), size = strlen(text);
    for (const char *at = strstr(text, find); at; at = strstr(at + find_size, find)) {
        if (with_size > find_size && with_size - find_size > BUILD_TEXT_LIMIT - size) {
            return "build text limit exceeded";
        }
        size = size - find_size + with_size;
    }
    char *result = allocate(values, size + 1);
    if (!result) {
        return values->error;
    }
    char *out = result;
    for (const char *at; (at = strstr(text, find)); text = at + find_size) {
        memcpy(out, text, (size_t)(at - text));
        out += at - text;
        memcpy(out, with, with_size);
        out += with_size;
    }
    strcpy(out, text);
    results[0].str = result;
    return NULL;
}
const char *build_values_starts_with(build_values *, const pin_value *arguments, pin_value *results)
{
    if (!build_values_string(&arguments[0]) || !build_values_string(&arguments[1])) {
        return "invalid build string arguments";
    }
    results[0]
        .b = !strncmp(arguments[0].str, arguments[1].str, strlen(arguments[1].str));
    return NULL;
}
static const char *split(build_values *values, const pin_value *arguments, pin_value *results, bool after)
{
    if (!build_values_string(&arguments[0]) ||
        !build_values_string(&arguments[1]) || !arguments[1].str[0]) {
        return "invalid build string arguments";
    }
    const char *text = arguments[0].str;
    const char *delimiter = strstr(text, arguments[1].str);
    if (after) {
        const char *suffix = delimiter ? delimiter + strlen(arguments[1].str) : "";
        return text_result(values, results, suffix, strlen(suffix));
    }
    return text_result(values, results, text, delimiter ? (size_t)(delimiter - text) : strlen(text));
}
const char *build_values_before(build_values *values, const pin_value *arguments, pin_value *results)
{
    return split(values, arguments, results, false);
}
const char *build_values_after(build_values *values, const pin_value *arguments, pin_value *results)
{
    return split(values, arguments, results, true);
}
const char *build_values_lines(build_values *values, const pin_value *arguments, pin_value *results)
{
    if (!build_values_string(arguments)) {
        return "invalid build string arguments";
    }
    const char *text = arguments[0].str;
    int count = text[0] ? 1 : 0;
    for (const char *p = text; *p; p++) {
        if (*p == '\n' && p[1]) {
            count++;
        }
    }
    pin_array *array = build_values_array(values, count);
    if (!array) {
        return values->error;
    }
    for (int i = 0; i < count; i++) {
        const char *end = strchr(text, '\n');
        size_t size = end ? (size_t)(end - text) : strlen(text);
        if (size && text[size - 1] == '\r') {
            size--;
        }
        char *line = build_values_text(values, text, size);
        if (!line) {
            return values->error;
        }
        array->items[i] = (pin_value){ .type = PIN_STR, .str = line };
        text = end ? end + 1 : text + strlen(text);
    }
    results[0].ptr = array;
    return NULL;
}
const char *build_values_trim(build_values *values, const pin_value *arguments, pin_value *results)
{
    if (!build_values_string(arguments)) {
        return "invalid build string arguments";
    }
    const char *text = arguments[0].str;
    while (*text && isspace((unsigned char)*text)) {
        text++;
    }
    size_t size = strlen(text);
    while (size && isspace((unsigned char)text[size - 1])) {
        size--;
    }
    return text_result(values, results, text, size);
}
const char *build_values_concat(build_values *values, const pin_value *arguments, pin_value *results)
{
    if (!build_values_strings(&arguments[0]) || !build_values_strings(&arguments[1])) {
        return "invalid build string array";
    }
    const pin_array *left = arguments[0].ptr, *right = arguments[1].ptr;
    int count = left->count + right->count;
    size_t bytes = 0;
    for (int i = 0; i < count; i++) {
        const char *text = i < left->count ? left->items[i].str
                                           : right->items[i - left->count].str;
        size_t size = strlen(text) + 1;
        if (size > BUILD_TEXT_LIMIT - bytes) {
            return "build text limit exceeded";
        }
        bytes += size;
    }
    pin_array *array = build_values_array(values, count);
    if (!array) {
        return values->error;
    }
    for (int i = 0; i < array->count; i++) {
        const char *text = i < left->count ? left->items[i].str
                                           : right->items[i - left->count].str;
        char *copy = build_values_text(values, text, strlen(text));
        if (!copy) {
            return values->error;
        }
        array->items[i] = (pin_value){ .type = PIN_STR, .str = copy };
    }
    results[0].ptr = array;
    return NULL;
}
const char *build_values_length(build_values *, const pin_value *arguments, pin_value *results)
{
    if (!build_values_strings(arguments)) {
        return "invalid build string array";
    }
    const pin_array *array = arguments[0].ptr;
    results[0].i = array->count;
    return NULL;
}
