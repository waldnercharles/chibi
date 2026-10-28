#include "runtime/text_format.h"

#include "fmt.h"
#include "support/arena.h"
#include "support/array.h"
#include "types/array.h"
#include "types/scalar.h"
#include "types/text.h"
#include "types/value_format.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define FMT_TEXT_MAX_ARGS 12
#define FMT_RENDER_BUF 256

static const char *fmt_text_of(const pin_value *v)
{
    if (v->container == PIN_CONTAINER_SINGLE &&
        (v->type == PIN_STR || v->type == PIN_NAME)) {
        return pin_text(*v).bytes;
    }
    if (v->type == PIN_FLOW && v->container == PIN_CONTAINER_SINGLE) {
        return ""; /* unconnected Generic argument */
    }
    return pin_value_to_string(v);
}

static char *fmt_arena_strdup(arena *a, const char *s)
{
    size_t n = strlen(s);
    char *copy = arena_alloc(a, n + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, s, n + 1);
    return copy;
}

static const char *fmt_default_letter(pin_type t)
{
    switch (t) {
        case PIN_BOOL:
        case PIN_I8:
        case PIN_I16:
        case PIN_I32:
        case PIN_ENUM: return "d";
        case PIN_U8:
        case PIN_U16:
        case PIN_U32: return "u";
        case PIN_I64: return "d";
        case PIN_U64: return "u";
        case PIN_F32:
        case PIN_F64: return "g";
        case PIN_STR:
        case PIN_NAME: return "s";
        default: return "";
    }
}

static int fmt_is_float_conversion(char conversion)
{
    return strchr("aAeEfFgG", conversion) != NULL;
}

static int fmt_numeric_as_double(const pin_value *v, double *out)
{
    switch (v->type) {
        case PIN_BOOL: *out = v->b; return 1;
        case PIN_I8: *out = v->i8; return 1;
        case PIN_U8: *out = v->u8; return 1;
        case PIN_I16: *out = v->i16; return 1;
        case PIN_U16: *out = v->u16; return 1;
        case PIN_I32: *out = v->i; return 1;
        case PIN_U32: *out = v->u; return 1;
        case PIN_I64: *out = (double)v->i64; return 1;
        case PIN_U64: *out = (double)v->u64; return 1;
        case PIN_F32: *out = v->f; return 1;
        case PIN_F64: *out = v->d; return 1;
        case PIN_ENUM: *out = v->e; return 1;
        default: return 0;
    }
}

/* Only scalar conversion families and bounded literal width/precision reach
 * printf. Length modifiers are chosen here from the physical input type. */
static bool fmt_modifier(
    const pin_value *v,
    const char *mods,
    size_t size,
    char *conv,
    size_t capacity,
    char *letter
)
{
    if (v->container != PIN_CONTAINER_SINGLE || size >= 64 || memchr(mods, 0, size)) {
        return false;
    }
    char prefix[64];
    size_t n = 0;
    for (size_t i = 0; i < size; i++) {
        if (mods[i] == '>') {
            continue;
        }
        prefix[n++] = mods[i] == '<' ? '-' : mods[i];
    }
    prefix[n] = 0;
    size_t at = 0;
    while (at < n && strchr("-+ #0", prefix[at])) {
        at++;
    }
    size_t flags = at;
    unsigned width = 0;
    while (at < n && prefix[at] >= '0' && prefix[at] <= '9') {
        width = width * 10 + (unsigned)(prefix[at++] - '0');
        if (width > 255) {
            return false;
        }
    }
    bool precision = at < n && prefix[at] == '.';
    if (precision) {
        at++;
        unsigned digits = 0;
        while (at < n && prefix[at] >= '0' && prefix[at] <= '9') {
            digits = digits * 10 + (unsigned)(prefix[at++] - '0');
            if (digits > 255) {
                return false;
            }
        }
    }
    const char *default_letter = fmt_default_letter(v->type);
    if (!*default_letter) {
        return false;
    }
    *letter = at == n ? *default_letter : prefix[at];
    if (at < n && at + 1 != n) {
        return false;
    }
    bool floating = strchr("aAeEfFgG", *letter) != NULL;
    bool integer = strchr("diouxXc", *letter) != NULL;
    bool string = v->type == PIN_STR || v->type == PIN_NAME;
    bool numeric = v->type >= PIN_BOOL && v->type <= PIN_F64;
    numeric |= v->type == PIN_ENUM;
    if ((string && *letter != 's') || (!string && !numeric) ||
        (!string && !floating && !integer) ||
        ((v->type == PIN_F32 || v->type == PIN_F64) && !floating) ||
        (*letter == 'c' && (precision || v->type == PIN_I64 || v->type == PIN_U64))) {
        return false;
    }
    for (size_t i = 0; i < flags; i++) {
        char flag = prefix[i];
        if ((string || *letter == 'c') && flag != '-') {
            return false;
        }
        if (flag == '#' && !floating && !strchr("oxX", *letter)) {
            return false;
        }
        if ((flag == '+' || flag == ' ') && !floating && !strchr("di", *letter)) {
            return false;
        }
    }
    prefix[at] = 0;
    const char *length = integer && (v->type == PIN_I64 || v->type == PIN_U64)
                           ? "ll"
                           : "";
    snprintf(conv, capacity, "%%%s%s%c", prefix, length, *letter);
    return true;
}

/* Render one argument through its placeholder modifier. Numeric values may
 * be promoted to double for a floating-point conversion; no modifier uses
 * the canonical pin display. The result is an arena copy. `mods` runs for
 * `mods_len` bytes inside the authored format, ending at the closing '}'. */
static const char *fmt_render_arg(const pin_value *v, const char *mods, size_t mods_len, arena *a)
{
    if (mods_len == 0 || memchr(mods, '*', mods_len)) {
        return fmt_arena_strdup(a, fmt_text_of(v));
    }
    if (v->type == PIN_FLOW) {
        return fmt_arena_strdup(a, "");
    }

    char conv[80], letter;
    if (!fmt_modifier(v, mods, mods_len, conv, sizeof conv, &letter)) {
        return fmt_arena_strdup(a, fmt_text_of(v));
    }

    char buf[FMT_RENDER_BUF];
    if (fmt_is_float_conversion(letter)) {
        double value;
        if (!fmt_numeric_as_double(v, &value)) {
            return fmt_arena_strdup(a, fmt_text_of(v));
        }
        snprintf(buf, sizeof buf, conv, value);
        return fmt_arena_strdup(a, buf);
    }
    bool signed_conversion = letter == 'd' || letter == 'i' || letter == 'c';
    switch (v->type) {
        case PIN_BOOL:
        case PIN_I8:
        case PIN_U8:
        case PIN_I16:
        case PIN_U16:
        case PIN_I32:
        case PIN_U32:
        case PIN_ENUM: {
            unsigned value = v->type == PIN_BOOL ? (unsigned)v->b
                           : v->type == PIN_I8   ? (unsigned)v->i8
                           : v->type == PIN_U8   ? v->u8
                           : v->type == PIN_I16  ? (unsigned)v->i16
                           : v->type == PIN_U16  ? v->u16
                           : v->type == PIN_I32  ? (unsigned)v->i
                           : v->type == PIN_U32  ? v->u
                                                 : (unsigned)v->e;
            if (signed_conversion) {
                snprintf(buf, sizeof buf, conv, (int)value);
            } else {
                snprintf(buf, sizeof buf, conv, value);
            }
            break;
        }
        case PIN_I64:
        case PIN_U64: {
            unsigned long long value = v->type == PIN_I64
                                         ? (unsigned long long)v->i64
                                         : v->u64;
            if (signed_conversion) {
                snprintf(buf, sizeof buf, conv, (long long)value);
            } else {
                snprintf(buf, sizeof buf, conv, value);
            }
            break;
        }
        case PIN_STR:
        case PIN_NAME:
            snprintf(buf, sizeof buf, conv, pin_text(*v).bytes);
            break;
        default: return fmt_arena_strdup(a, fmt_text_of(v));
    }
    return fmt_arena_strdup(a, buf);
}

static pin_string fmt_render_text(const pin_value *value, const char *mods, size_t size, arena *storage)
{
    if (value->container != PIN_CONTAINER_SINGLE ||
        (value->type != PIN_STR && value->type != PIN_NAME)) {
        char conv[80], letter;
        if (size && !memchr(mods, '*', size) &&
            fmt_modifier(value, mods, size, conv, sizeof conv, &letter) &&
            letter == 'c') {
            char buffer[FMT_RENDER_BUF];
            int length = snprintf(
                buffer,
                sizeof buffer,
                conv,
                (int)(unsigned char)behavior_scalar_bits(*value)
            );
            assert(length >= 0 && length < FMT_RENDER_BUF);
            char *bytes = arena_alloc(storage, (size_t)length + 1);
            memcpy(bytes, buffer, (size_t)length + 1);
            return (pin_string){ .bytes = bytes, .length = (uint32_t)length };
        }
        const char *text = fmt_render_arg(value, mods, size, storage);
        return (pin_string){ .bytes = text ? text : "",
                             .length = text ? (uint32_t)strlen(text) : 0 };
    }
    pin_string text = pin_text(*value);
    char conv[80], letter;
    if (!size || memchr(mods, '*', size) ||
        !fmt_modifier(value, mods, size, conv, sizeof conv, &letter)) {
        return text;
    }
    const char *p = conv + 1;
    bool left = false;
    while (*p == '-') {
        left = true;
        p++;
    }
    size_t width = 0;
    while (*p >= '0' && *p <= '9') {
        width = width * 10 + (unsigned)(*p++ - '0');
    }
    size_t length = text.length;
    if (*p == '.') {
        size_t precision = 0;
        for (p++; *p >= '0' && *p <= '9'; p++) {
            precision = precision * 10 + (unsigned)(*p - '0');
        }
        if (length > precision) {
            length = precision;
        }
    }
    size_t total = width > length ? width : length;
    char *bytes = arena_alloc(storage, total + 1);
    memset(bytes, ' ', total);
    memcpy(bytes + (left ? 0 : total - length), text.bytes, length);
    bytes[total] = 0;
    return (pin_string){ .bytes = bytes, .length = (uint32_t)total };
}
pin_value native_format_text(const pin_value *inputs, int input_count, arena *storage)
{
    assert(input_count >= 1);
    pin_value result = { .type = PIN_STR, .type_name = "const char *" };
    bool scalar_text = inputs[0].container == PIN_CONTAINER_SINGLE &&
                       (inputs[0].type == PIN_STR || inputs[0].type == PIN_NAME);
    pin_string format = scalar_text
                          ? pin_text(inputs[0])
                          : (pin_string){ .bytes = fmt_text_of(inputs), .length = 0 };
    if (!scalar_text) {
        format.length = (uint32_t)strlen(format.bytes);
    }
    if (format.length > INT_MAX) {
        return result;
    }
    char *bytes = NULL;
    int sequential = 0, placeholders = 0;
    for (size_t at = 0; at < format.length;) {
        char c = format.bytes[at];
        if ((c == '{' || c == '}') && at + 1 < format.length &&
            format.bytes[at + 1] == c) {
            apush(bytes, c);
            at += 2;
            continue;
        }
        if (c != '{') {
            apush(bytes, c);
            at++;
            continue;
        }
        size_t q = at + 1;
        int index = -1;
        while (q < format.length && format.bytes[q] >= '0' && format.bytes[q] <= '9') {
            index = index < 0 ? 0 : index;
            index = index >= FMT_TEXT_MAX_ARGS ? FMT_TEXT_MAX_ARGS
                                               : index * 10 + format.bytes[q] - '0';
            q++;
        }
        if (q == format.length || (format.bytes[q] != ':' && format.bytes[q] != '}')) {
            apush(bytes, c);
            at++;
            continue;
        }
        size_t mods = q;
        bool modified = format.bytes[q] == ':';
        if (modified) {
            mods = ++q;
        }
        while (q < format.length && format.bytes[q] != '}') {
            q++;
        }
        if (q == format.length) {
            apush(bytes, c);
            at++;
            continue;
        }
        at = q + 1;
        index = index >= 0 ? index : sequential++;
        if (index >= input_count - 1 || index >= FMT_TEXT_MAX_ARGS ||
            placeholders >= FMT_TEXT_MAX_ARGS) {
            continue;
        }
        pin_string text = fmt_render_text(
            &inputs[index + 1],
            format.bytes + mods,
            modified ? q - mods : 0,
            storage
        );
        size_t maximum = (size_t)acount(bytes) + (format.length - at);
        if (maximum > INT_MAX || text.length > (size_t)INT_MAX - maximum) {
            afree(bytes);
            return result;
        }
        for (uint32_t n = 0; n < text.length; n++) {
            apush(bytes, text.bytes[n]);
        }
        placeholders++;
    }
    const char *error = pin_value_text(storage, &result, bytes, (size_t)acount(bytes));
    afree(bytes);
    if (error) {
        return (pin_value){ .type = PIN_STR };
    }
    return result;
}

static const char *fmt_text_call(arena *storage, const pin_value *arguments, int count, pin_value *result)
{
    *result = native_format_text(arguments, count, storage);
    return result->text ? NULL : "text formatting allocation failed";
}

pin_value native_text_equal(const pin_value *inputs, int, arena *)
{
    return (pin_value){ .type = PIN_BOOL,
                        .type_name = "bool",
                        .b = pin_text_equal(pin_text(inputs[0]), pin_text(inputs[1])) };
}

static const char *text_equal_call(arena *storage, const pin_value *arguments, int count, pin_value *result)
{
    *result = native_text_equal(arguments, count, storage);
    return NULL;
}

const node_host_call NATIVE_FORMAT_TEXT_HOST = { .value = fmt_text_call,
                                                 .variadic = true };
const node_host_call NATIVE_TEXT_EQUAL_HOST = { .value = text_equal_call };
static const char *text_bytes(
    const pin_value *arguments,
    int count,
    int expected,
    const char **text,
    size_t *length
)
{
    if (!arguments || count != expected || arguments[0].container != PIN_CONTAINER_SINGLE ||
        (arguments[0].type != PIN_STR && arguments[0].type != PIN_NAME)) {
        return "Invalid text byte operation arguments";
    }
    for (int i = 1; i < count; i++) {
        if (arguments[i].container != PIN_CONTAINER_SINGLE ||
            arguments[i].type != PIN_I32) {
            return "Invalid text byte operation index";
        }
    }
    pin_string view = pin_text(arguments[0]);
    *text = view.bytes;
    *length = view.length;
    if (*length > INT_MAX) {
        return "Text byte length exceeds int range";
    }
    return NULL;
}
static const char *text_byte_length_call(arena *, const pin_value *arguments, int count, pin_value *result)
{
    const char *text;
    size_t length;
    const char *error = text_bytes(arguments, count, 1, &text, &length);
    if (error) {
        return error;
    }
    *result = (pin_value){ .type = PIN_I32, .type_name = "int", .i = (int)length };
    return NULL;
}
static const char *text_byte_at_call(arena *, const pin_value *arguments, int count, pin_value *result)
{
    const char *text;
    size_t length;
    const char *error = text_bytes(arguments, count, 2, &text, &length);
    if (error) {
        return error;
    }
    int index = arguments[1].i;
    *result = (pin_value){ .type = PIN_I32,
                           .type_name = "int",
                           .i = index >= 0 && (size_t)index < length
                                  ? (unsigned char)text[index]
                                  : -1 };
    return NULL;
}
static const char *text_slice_call(arena *storage, const pin_value *arguments, int count, pin_value *result)
{
    const char *text;
    size_t length;
    const char *error = text_bytes(arguments, count, 3, &text, &length);
    if (error) {
        return error;
    }
    size_t start = arguments[1].i > 0 ? (size_t)arguments[1].i : 0;
    if (start > length) {
        start = length;
    }
    size_t take = arguments[2].i > 0 ? (size_t)arguments[2].i : 0;
    if (take > length - start) {
        take = length - start;
    }
    return pin_value_text(storage, result, text + start, take);
}
static const char *text_bytes_call(arena *storage, const pin_value *arguments, int count, pin_value *result)
{
    const char *text;
    size_t length;
    const char *error = text_bytes(arguments, count, 1, &text, &length);
    if (error) {
        return error;
    }
    pin_array *bytes = pin_array_snapshot_allocate(storage, PIN_I32, "int", (int)length);
    for (size_t i = 0; i < length; i++) {
        bytes->items[i] = (pin_value){ .type = PIN_I32,
                                       .type_name = bytes->type_name,
                                       .i = (unsigned char)text[i] };
    }
    *result = (pin_value){ .type = PIN_I32, .container = PIN_CONTAINER_ARRAY, .ptr = bytes };
    return NULL;
}
const node_host_call NATIVE_TEXT_BYTE_LENGTH_CALL = { .value = text_byte_length_call };
const node_host_call NATIVE_TEXT_BYTE_AT_CALL = { .value = text_byte_at_call };
const node_host_call NATIVE_TEXT_SLICE_CALL = { .value = text_slice_call };
const node_host_call NATIVE_TEXT_BYTES_CALL = { .value = text_bytes_call };
#define SNAPSHOT(KEY, FN, VARIADIC)                                                                       \
    static const char *KEY##_snapshot(arena *storage, const pin_value *inputs, int count, pin_value *out) \
    {                                                                                                     \
        pin_value value = FN(inputs, count, storage);                                                     \
        return pin_value_snapshot_to_arena(storage, out, &value)                                          \
                 ? "Cannot retain native value result"                                                    \
                 : NULL;                                                                                  \
    }                                                                                                     \
    const node_host_call NATIVE_##KEY##_CALL = { .value = KEY##_snapshot,                                 \
                                                 .variadic = VARIADIC };
SNAPSHOT(FORMAT_TEXT, native_format_text, true)
SNAPSHOT(TEXT_EQUAL, native_text_equal, false)
#undef SNAPSHOT
const node_access NATIVE_TEXT_ACCESS = { .flags = NODE_EFFECT_PURE };
