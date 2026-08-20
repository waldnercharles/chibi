/* core/fmt_nodes.c -- CAT-38 Format Text, registered as EXTRA_NODES.
 *
 * Pure node: one format input, a user-extensible argument group (PIN-14)
 * whose pins are Generic wildcards (PIN-11), and one String output.
 * Evaluating it substitutes placeholders in the format string with the
 * matching argument's rendered text; a placeholder with no matching
 * argument renders as the empty string. The result is built in the arena,
 * so it lives as long as the other scratch-owned values of the step.
 *
 * Placeholders follow the fmt convention: {} consumes the next argument in
 * order, {N} names an argument position, and {{ and }} print literal
 * braces. The trailing argument pin is the spare: it stays Generic (the
 * PIN_FLOW marker) until a connection adopts its type, and reverts when the
 * edge is removed. A non-string argument renders through
 * pin_value_to_string, the canonical pin display (UI-39).
 */
#include "fmt_nodes.h"

#include "arena.h"
#include "pin_fmt.h"

#include <string.h>

static const char *fmt_text_of(const pin_value *v)
{
    if (v->type == PIN_STR || v->type == PIN_NAME) return v->str ? v->str : "";
    if (v->type == PIN_FLOW) return ""; /* unconnected Generic argument */
    return pin_value_to_string(v);
}

static void fmt_write_text(char **w, const char *text)
{
    size_t n = strlen(text);
    memcpy(*w, text, n);
    *w += n;
}

static pin_value fmt_text(const pin_value *inputs, int input_count, arena *a)
{
    pin_value r = { .type = PIN_STR, .type_name = "const char *", .str = "" };
    const char *format = fmt_text_of(&inputs[0]);
    size_t need = strlen(format) + 1;
    for (int i = 1; i < input_count; i++)
        need += strlen(fmt_text_of(&inputs[i]));
    char *out = arena_alloc(a, need);
    if (!out) return r;
    char *w = out;
    int auto_index = 0;
    for (const char *p = format; *p;) {
        if (p[0] == '{' && p[1] == '{') {
            *w++ = '{';
            p += 2;
            continue;
        }
        if (p[0] == '}' && p[1] == '}') {
            *w++ = '}';
            p += 2;
            continue;
        }
        if (p[0] == '{') {
            const char *q = p + 1;
            if (*q == '}') {
                /* {} consumes the next argument in order. */
                if (auto_index < input_count - 1)
                    fmt_write_text(&w, fmt_text_of(&inputs[auto_index + 1]));
                auto_index++;
                p = q + 1;
                continue;
            }
            if (*q >= '0' && *q <= '9') {
                int index = 0;
                while (*q >= '0' && *q <= '9')
                    index = index * 10 + (*q - '0'), q++;
                if (*q == '}') {
                    const char *arg = index + 1 < input_count
                                        ? fmt_text_of(&inputs[index + 1])
                                        : NULL;
                    if (arg) fmt_write_text(&w, arg);
                    p = q + 1;
                    continue;
                }
            }
        }
        *w++ = *p++;
    }
    *w = '\0';
    r.str = out;
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
