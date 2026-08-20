/* core/fmt_nodes.c -- CAT-38 Format Text, registered as EXTRA_NODES.
 *
 * Pure node: one format input, a user-extensible argument group (PIN-14)
 * whose pins are Generic wildcards (PIN-11), and one String output.
 * Evaluating it rewrites the format through the vendored fmt library
 * (third_party/fmt, a fork of stclib/STC c11/fmt.h) and renders each
 * argument through the canonical pin display (UI-39) before substitution.
 * The result is built in the arena, so it lives as long as the other
 * scratch-owned values of the step.
 *
 * Placeholders: {} consumes the next argument in order, {N} names a
 * position, {<:>MODS} carries printf width/alignment (strings only), and
 * {{ and }} print literal braces. A placeholder with no matching argument
 * renders as the empty string: the parser reports it as an error, which we
 * ignore, and it simply writes nothing for that placeholder. Mixing {} and
 * {N} in one format is undefined, as with printf positional arguments, and
 * a literal percent sign must be written %%.
 *
 * The trailing argument pin is the spare: it stays Generic (the PIN_FLOW
 * marker) until a connection adopts its type, and reverts when the edge is
 * removed.
 */
#include "fmt_nodes.h"

#include "arena.h"
#include "fmt.h"
#include "pin_fmt.h"

#include <string.h>

#define FMT_TEXT_MAX_ARGS 12

static const char *fmt_text_of(const pin_value *v)
{
    if (v->type == PIN_STR || v->type == PIN_NAME) return v->str ? v->str : "";
    if (v->type == PIN_FLOW) return ""; /* unconnected Generic argument */
    return pin_value_to_string(v);
}

static char *fmt_arena_strdup(arena *a, const char *s)
{
    size_t n = strlen(s);
    char *copy = arena_alloc(a, n + 1);
    if (!copy) return NULL;
    memcpy(copy, s, n + 1);
    return copy;
}

static pin_value fmt_text(const pin_value *inputs, int input_count, arena *a)
{
    pin_value r = { .type = PIN_STR, .type_name = "const char *", .str = "" };
    const char *format = fmt_text_of(&inputs[0]);
    int nargs = input_count - 1;
    if (nargs > FMT_TEXT_MAX_ARGS) nargs = FMT_TEXT_MAX_ARGS;

    /* Every argument renders to a string first (fmt's _fc type dispatch
     * cannot see pin_value's runtime type), so all placeholders are %s. The
     * copies live in the arena: pin_value_to_string's ring is only eight
     * slots and up to twelve arguments would clobber each other. */
    const char *args[FMT_TEXT_MAX_ARGS] = { 0 };
    for (int i = 0; i < nargs; i++) {
        args[i] = fmt_arena_strdup(a, fmt_text_of(&inputs[i + 1]));
        if (!args[i]) return r; /* OOM: bail with an empty result */
    }
    const char *specs[FMT_TEXT_MAX_ARGS] = { 0 };
    for (int i = 0; i < nargs; i++) specs[i] = "s";

    /* The fmt rewrite grows each placeholder by at most a few characters;
     * the fork sizes its scratch at len + nargs*8 + 1. */
    char *rewritten = arena_alloc(a, strlen(format) + (size_t)nargs * 8 + 1);
    if (!rewritten) return r;
    (void)_fmt_parse(rewritten, nargs, format, specs);

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
        if (out) r.str = out;
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
