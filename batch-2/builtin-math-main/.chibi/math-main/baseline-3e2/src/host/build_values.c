#include "host/build_values.h"

#include "types/text.h"

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
const pin_string *build_values_text(build_values *values, const char *text, size_t size)
{
    if (size > BUILD_TEXT_LIMIT) {
        values->error = "build text limit exceeded";
        return NULL;
    }
    pin_string *copy = allocate(values, sizeof(*copy) + size + 1);
    if (copy) {
        char *bytes = (char *)(copy + 1);
        memcpy(bytes, text, size);
        bytes[size] = 0;
        *copy = (pin_string){ .bytes = bytes, .length = (uint32_t)size };
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
           pin_text(*value).length <= BUILD_TEXT_LIMIT;
}
const char *build_values_cstring(build_values *values, pin_value value, const char **out)
{
    if (!build_values_string(&value)) {
        return "invalid build string arguments";
    }
    pin_string text = pin_text(value);
    if (memchr(text.bytes, 0, text.length)) {
        return "C string contains embedded NUL";
    }
    if (text.bytes[text.length]) {
        char *copy = arena_alloc_bounded(&values->scratch, (size_t)text.length + 1, BUILD_STORAGE_LIMIT);
        if (!copy) {
            values->error = "build value storage limit exceeded";
            return values->error;
        }
        memcpy(copy, text.bytes, text.length);
        copy[text.length] = 0;
        *out = copy;
        return NULL;
    }
    *out = text.bytes;
    return NULL;
}
void build_values_begin_call(build_values *values)
{
    arena_reset(&values->scratch);
}
void build_values_free(build_values *values)
{
    arena_free(&values->memory);
    arena_free(&values->scratch);
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
        size_t size = pin_text(array->items[i]).length + 1;
        if (size > BUILD_TEXT_LIMIT - bytes) {
            return false;
        }
        bytes += size;
    }
    return true;
}
static const char *text_result(build_values *values, pin_value *result, const char *text, size_t size)
{
    result->text = build_values_text(values, text, size);
    return result->text ? NULL : values->error;
}
const char *build_values_join(build_values *values, const pin_value *arguments, pin_value *results)
{
    if (!build_values_strings(&arguments[0]) || !build_values_string(&arguments[1])) {
        return "invalid build string arguments";
    }
    const pin_array *array = arguments[0].ptr;
    pin_string separator_text = pin_text(arguments[1]);
    size_t size = 0, separator = separator_text.length;
    for (int i = 0; i < array->count; i++) {
        size_t addition = pin_text(array->items[i]).length + (i ? separator : 0);
        if (addition > BUILD_TEXT_LIMIT - size) {
            return "build text limit exceeded";
        }
        size += addition;
    }
    pin_string *joined = allocate(values, sizeof(*joined) + size + 1);
    if (!joined) {
        return values->error;
    }
    char *text = (char *)(joined + 1);
    *joined = (pin_string){ .bytes = text, .length = (uint32_t)size };
    size_t offset = 0;
    for (int i = 0; i < array->count; i++) {
        if (i) {
            memcpy(text + offset, separator_text.bytes, separator);
            offset += separator;
        }
        size_t length = pin_text(array->items[i]).length;
        memcpy(text + offset, pin_text(array->items[i]).bytes, length);
        offset += length;
    }
    text[size] = 0;
    results[0].text = joined;
    return NULL;
}
static size_t find_text(pin_string text, pin_string find, size_t start)
{
    while (start + find.length <= text.length) {
        const char *at = memchr(
            text.bytes + start,
            (unsigned char)find.bytes[0],
            text.length - start - find.length + 1
        );
        if (!at) {
            return text.length;
        }
        start = (size_t)(at - text.bytes);
        if (!memcmp(at, find.bytes, find.length)) {
            return start;
        }
        start++;
    }
    return text.length;
}
/* Every occurrence of a non-empty `find` in text, left to right, replaced by
 * `with`. Byte lengths preserve embedded NUL in all three arguments. */
const char *build_values_replace(build_values *values, const pin_value *arguments, pin_value *results)
{
    if (!build_values_string(&arguments[0]) || !build_values_string(&arguments[1]) ||
        !build_values_string(&arguments[2])) {
        return "invalid build string arguments";
    }
    pin_string text = pin_text(arguments[0]), find = pin_text(arguments[1]),
               with = pin_text(arguments[2]);
    if (!find.length) {
        return "invalid build string arguments";
    }
    size_t size = text.length;
    for (size_t at = find_text(text, find, 0); at < text.length;
         at = find_text(text, find, at + find.length)) {
        if (with.length > find.length &&
            with.length - find.length > BUILD_TEXT_LIMIT - size) {
            return "build text limit exceeded";
        }
        size = size - find.length + with.length;
    }
    pin_string *result = allocate(values, sizeof(*result) + size + 1);
    if (!result) {
        return values->error;
    }
    char *bytes = (char *)(result + 1), *out = bytes;
    *result = (pin_string){ .bytes = bytes, .length = (uint32_t)size };
    size_t start = 0;
    for (size_t at = find_text(text, find, start); at < text.length;
         at = find_text(text, find, start)) {
        memcpy(out, text.bytes + start, at - start);
        out += at - start;
        memcpy(out, with.bytes, with.length);
        out += with.length;
        start = at + find.length;
    }
    memcpy(out, text.bytes + start, text.length - start);
    bytes[size] = 0;
    results[0].text = result;
    return NULL;
}
const char *build_values_starts_with(build_values *, const pin_value *arguments, pin_value *results)
{
    if (!build_values_string(&arguments[0]) || !build_values_string(&arguments[1])) {
        return "invalid build string arguments";
    }
    pin_string text = pin_text(arguments[0]), prefix = pin_text(arguments[1]);
    results[0].b = prefix.length <= text.length &&
                   !memcmp(text.bytes, prefix.bytes, prefix.length);
    return NULL;
}
static const char *split(build_values *values, const pin_value *arguments, pin_value *results, bool after)
{
    if (!build_values_string(&arguments[0]) || !build_values_string(&arguments[1])) {
        return "invalid build string arguments";
    }
    pin_string text = pin_text(arguments[0]), delimiter = pin_text(arguments[1]);
    if (!delimiter.length) {
        return "invalid build string arguments";
    }
    size_t offset = 0;
    while (offset + delimiter.length <= text.length &&
           memcmp(text.bytes + offset, delimiter.bytes, delimiter.length)) {
        offset++;
    }
    bool found = offset + delimiter.length <= text.length;
    if (after) {
        size_t start = found ? offset + delimiter.length : text.length;
        return text_result(values, results, text.bytes + start, text.length - start);
    }
    return text_result(values, results, text.bytes, found ? offset : text.length);
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
    pin_string text = pin_text(arguments[0]);
    int count = text.length ? 1 : 0;
    for (size_t i = 0; i + 1 < text.length; i++) {
        count += text.bytes[i] == '\n';
    }
    pin_array *array = build_values_array(values, count);
    if (!array) {
        return values->error;
    }
    size_t start = 0;
    for (int i = 0; i < count; i++) {
        size_t end = start;
        while (end < text.length && text.bytes[end] != '\n') {
            end++;
        }
        size_t size = end - start;
        if (size && text.bytes[start + size - 1] == '\r') {
            size--;
        }
        const pin_string *line = build_values_text(values, text.bytes + start, size);
        if (!line) {
            return values->error;
        }
        array->items[i] = (pin_value){ .type = PIN_STR, .text = line };
        start = end + 1;
    }
    results[0].ptr = array;
    return NULL;
}
const char *build_values_trim(build_values *values, const pin_value *arguments, pin_value *results)
{
    if (!build_values_string(arguments)) {
        return "invalid build string arguments";
    }
    pin_string text = pin_text(arguments[0]);
    size_t start = 0, end = text.length;
    while (start < end && isspace((unsigned char)text.bytes[start])) {
        start++;
    }
    while (end > start && isspace((unsigned char)text.bytes[end - 1])) {
        end--;
    }
    return text_result(values, results, text.bytes + start, end - start);
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
        pin_string text = pin_text(
            i < left->count ? left->items[i] : right->items[i - left->count]
        );
        size_t size = text.length + 1;
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
        pin_string text = pin_text(
            i < left->count ? left->items[i] : right->items[i - left->count]
        );
        const pin_string *copy = build_values_text(values, text.bytes, text.length);
        if (!copy) {
            return values->error;
        }
        array->items[i] = (pin_value){ .type = PIN_STR, .text = copy };
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
