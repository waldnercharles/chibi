#include "runtime/text_format.h"

#include "fmt.h"
#include "support/arena.h"
#include "types/array.h"
#include "types/value_format.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#define FMT_TEXT_MAX_ARGS 12
#define FMT_RENDER_BUF 256

static const char *fmt_text_of(const pin_value *v)
{
    if (v->container == PIN_CONTAINER_SINGLE &&
        (v->type == PIN_STR || v->type == PIN_NAME)) {
        return v->str ? v->str : "";
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
    if (v->container != PIN_CONTAINER_SINGLE || size >= 64) {
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
            snprintf(buf, sizeof buf, conv, v->str ? v->str : "");
            break;
        default: return fmt_arena_strdup(a, fmt_text_of(v));
    }
    return fmt_arena_strdup(a, buf);
}

pin_value native_format_text(const pin_value *inputs, int input_count, arena *a)
{
    assert(input_count >= 1);
    pin_value r = { .type = PIN_STR, .type_name = "const char *", .str = "" };
    const char *format = fmt_text_of(&inputs[0]);
    int nargs = input_count - 1;
    if (nargs > FMT_TEXT_MAX_ARGS) {
        nargs = FMT_TEXT_MAX_ARGS;
    }

    /* Reduce every placeholder to a bare {} and render each argument
     * through its modifier, so fmt only assembles %s conversions while the
     * value keeps its own type for precision formatting. */
    const char *args[FMT_TEXT_MAX_ARGS] = { 0 };
    char *sanitized = arena_alloc(a, strlen(format) + 1);
    if (!sanitized) {
        return r;
    }
    char *w = sanitized;
    int seq = 0, at = 0;
    for (const char *p = format; *p;) {
        if (p[0] == '{' && p[1] == '{') {
            *w++ = '{';
            *w++ = '{';
            p += 2;
            continue;
        }
        if (p[0] == '}' && p[1] == '}') {
            *w++ = '}';
            *w++ = '}';
            p += 2;
            continue;
        }
        if (p[0] != '{') {
            *w++ = *p++;
            continue;
        }
        const char *q = p + 1;
        int index = -1;
        if (*q >= '0' && *q <= '9') {
            index = 0;
            while (*q >= '0' && *q <= '9') {
                index = index >= FMT_TEXT_MAX_ARGS ? FMT_TEXT_MAX_ARGS
                                                   : index * 10 + (*q - '0');
                q++;
            }
        }
        if (*q != ':' && *q != '}') {
            *w++ = *p++;
            continue;
        } /* literal '{' */
        const char *mods = NULL;
        if (*q == ':') {
            mods = ++q;
        }
        while (*q && *q != '}') {
            q++;
        }
        if (*q != '}') {
            *w++ = *p++;
            continue;
        } /* malformed: literal */
        int arg_index = index >= 0 ? index : seq++;
        size_t mods_len = mods ? (size_t)(q - mods) : 0;
        p = q + 1;
        if (arg_index >= nargs || at >= FMT_TEXT_MAX_ARGS) {
            continue; /* empty */
        }
        args[at] = fmt_render_arg(&inputs[arg_index + 1], mods, mods_len, a);
        if (!args[at]) {
            return r; /* OOM */
        }
        at++;
        *w++ = '{';
        *w++ = '}';
    }
    *w = '\0';

    const char *specs[FMT_TEXT_MAX_ARGS] = { 0 };
    for (int i = 0; i < at; i++) {
        specs[i] = "s";
    }
    size_t length = strlen(sanitized), extra = (size_t)at * 8 + 1;
    if (length > (SIZE_MAX - extra) / 2) {
        return r;
    }
    /* fmt escapes every literal percent for printf, doubling that byte. */
    char *rewritten = arena_alloc(a, length * 2 + extra);
    if (!rewritten) {
        return r;
    }
    (void)_fmt_parse(rewritten, at, sanitized, specs);

    fmt_stream ss = { 0 };
    _fmt_sprint(
        &ss,
        rewritten,
        args[0],
        args[1],
        args[2],
        args[3],
        args[4],
        args[5],
        args[6],
        args[7],
        args[8],
        args[9],
        args[10],
        args[11]
    );
    if (ss.data) {
        char *out = fmt_arena_strdup(a, ss.data);
        fmt_close(&ss);
        if (out) {
            r.str = out;
        }
    }
    return r;
}

static const char *fmt_text_call(arena *storage, const pin_value *arguments, int count, pin_value *result)
{
    *result = native_format_text(arguments, count, storage);
    return result->str ? NULL : "text formatting allocation failed";
}

pin_value native_text_equal(const pin_value *inputs, int, arena *)
{
    return (pin_value){ .type = PIN_BOOL,
                        .type_name = "bool",
                        .b = !strcmp(fmt_text_of(&inputs[0]), fmt_text_of(&inputs[1])) };
}

static const char *text_equal_call(arena *storage, const pin_value *arguments, int count, pin_value *result)
{
    *result = native_text_equal(arguments, count, storage);
    return NULL;
}

const node_host_call NATIVE_FORMAT_TEXT_HOST = { .value = fmt_text_call,
                                                 .variadic = true };
const node_host_call NATIVE_TEXT_EQUAL_HOST = { .value = text_equal_call };
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
