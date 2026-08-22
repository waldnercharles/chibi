/* core/fmt_nodes.c -- CAT-38 Format Text, registered as EXTRA_NODES.
 *
 * Pure node: one format input, a user-extensible argument group (PIN-14)
 * whose pins are Generic wildcards (PIN-11), and one String output.
 *
 * Placeholders follow the fmt convention: {} consumes the next argument in
 * order, {N} names a position, {MODS} and {N:MODS} carry printf modifiers,
 * and {{ and }} print literal braces. Arguments render through the
 * canonical pin display (pin_value_to_string, UI-39) when the placeholder
 * carries no modifier; a modifier opts into a typed render, where numeric
 * values use the matching C conversion family so precision and width apply:
 * {:.2f} on a Real renders 3.14, {:x} on an Integer renders ff. The '<'
 * and '>' alignment modifiers translate like fmt's: '<' becomes the '-'
 * flag and '>' right-aligns by default. Dynamic '*' width is not
 * supported. A literal percent must be written %%, and mixing {} with {N}
 * in one format is undefined, as with printf positional arguments.
 *
 * Implementation: fmt's _fc type dispatch is compile-time, so it cannot
 * see a pin_value's runtime type. The node walks the authored format,
 * renders each modified argument with its own type, and hands the fmt
 * library a reduced format of bare {} placeholders to assemble as %s into
 * a bounded fmt_stream, then copies the result into the arena. A
 * placeholder whose argument is missing (or beyond the cap) renders empty
 * per CAT-38.
 *
 * The trailing argument pin is the spare: it stays Generic (the PIN_FLOW
 * marker) until a connection adopts its type, and reverts when the edge is
 * removed.
 */
#include "fmt_nodes.h"

#include "../third_party/fmt/fmt.h"
#include "arena.h"
#include "pin_fmt.h"

#include <string.h>

#define FMT_TEXT_MAX_ARGS 12
#define FMT_CONV_LETTERS "aAeEfFgGdioxXuUncCsSp"
#define FMT_RENDER_BUF 256

static const char *fmt_text_of(const pin_value *v)
{
    if (v->type == PIN_STR || v->type == PIN_NAME) {
        return v->str ? v->str : "";
    }
    if (v->type == PIN_FLOW) {
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
        case PIN_I32: return "d";
        case PIN_U8:
        case PIN_U16:
        case PIN_U32: return "u";
        case PIN_I64: return "lld";
        case PIN_U64: return "llu";
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

    char mods_conv[64];
    size_t m = 0;
    for (size_t i = 0; i < mods_len && m + 1 < sizeof mods_conv; i++) {
        char c = mods[i];
        if (c == '>') {
            continue; /* right-align is the printf default */
        }
        mods_conv[m++] = c == '<' ? '-' : c;
    }
    mods_conv[m] = '\0';
    int has_letter = m > 0 && strchr(FMT_CONV_LETTERS, mods_conv[m - 1]) != NULL;
    char conv[80];
    snprintf(conv, sizeof conv, "%%%s%s", mods_conv, has_letter ? "" : fmt_default_letter(v->type));

    char buf[FMT_RENDER_BUF];
    if (has_letter && fmt_is_float_conversion(mods_conv[m - 1])) {
        double value;
        if (!fmt_numeric_as_double(v, &value)) {
            return fmt_arena_strdup(a, fmt_text_of(v));
        }
        snprintf(buf, sizeof buf, conv, value);
        return fmt_arena_strdup(a, buf);
    }
    switch (v->type) {
        case PIN_BOOL: snprintf(buf, sizeof buf, conv, v->b ? 1 : 0); break;
        case PIN_I8: snprintf(buf, sizeof buf, conv, (int)v->i8); break;
        case PIN_U8: snprintf(buf, sizeof buf, conv, (unsigned)v->u8); break;
        case PIN_I16: snprintf(buf, sizeof buf, conv, (int)v->i16); break;
        case PIN_U16: snprintf(buf, sizeof buf, conv, (unsigned)v->u16); break;
        case PIN_I32: snprintf(buf, sizeof buf, conv, v->i); break;
        case PIN_U32: snprintf(buf, sizeof buf, conv, v->u); break;
        case PIN_I64: snprintf(buf, sizeof buf, conv, (long long)v->i64); break;
        case PIN_U64:
            snprintf(buf, sizeof buf, conv, (unsigned long long)v->u64);
            break;
        case PIN_F32: snprintf(buf, sizeof buf, conv, (double)v->f); break;
        case PIN_F64: snprintf(buf, sizeof buf, conv, v->d); break;
        case PIN_STR:
        case PIN_NAME:
            snprintf(buf, sizeof buf, conv, v->str ? v->str : "");
            break;
        default: return fmt_arena_strdup(a, "");
    }
    return fmt_arena_strdup(a, buf);
}

static pin_value fmt_text(const pin_value *inputs, int input_count, arena *a)
{
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
                index = index * 10 + (*q - '0'), q++;
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
    char *rewritten = arena_alloc(a, strlen(sanitized) + (size_t)at * 8 + 1);
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

static pin_spec fmt_text_inputs[] = {
    { .name = "Format", .type = PIN_STR, .type_name = "const char *", .kind = PIN_DATA },
    /* The trailing Generic spare; every arg pin is a wildcard, so the
     * editor appends another spare whenever the last one is taken. */
    { .name = "Arg0", .type = PIN_FLOW, .kind = PIN_DATA, .flags = PIN_FLAG_WILDCARD },
};
static pin_spec fmt_text_outputs[] = {
    { "Result", PIN_STR, "const char *", PIN_DATA },
};
node_desc NODE_Format_Text = {
    .name = "Format Text",
    .input_count = 2,
    .output_count = 1,
    .inputs = fmt_text_inputs,
    .outputs = fmt_text_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = fmt_text,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Text",
    .keywords = "format string text printf substitute",
};
