/* core/fmt_nodes.c -- CAT-38 Format Text, registered as EXTRA_NODES.
 *
 * Pure node: one String format input, a user-extensible group of String
 * argument inputs (PIN-14, minimum zero args), and one String output.
 * Evaluating it substitutes each {0}, {1}, ... in the format string with the
 * corresponding argument's value; a placeholder whose index has no matching
 * argument renders as the empty string. The result is built in the arena, so
 * it lives as long as the other scratch-owned values of the step.
 */
#include "fmt_nodes.h"

#include "arena.h"

#include <string.h>

static pin_value fmt_text(const pin_value *inputs, int input_count, arena *a)
{
    pin_value r = { .type = PIN_STR, .type_name = "const char *", .str = "" };
    const char *format = inputs[0].str ? inputs[0].str : "";
    size_t need = strlen(format) + 1;
    for (int i = 1; i < input_count; i++)
        need += inputs[i].str ? strlen(inputs[i].str) : 0;
    char *out = arena_alloc(a, need);
    if (!out) return r;
    char *w = out;
    for (const char *p = format; *p;) {
        /* A placeholder is '{' followed by digits followed by '}'; anything
         * else is copied verbatim, so "{{" and "}" stay literal. */
        if (p[0] == '{' && p[1] >= '0' && p[1] <= '9') {
            const char *q = p + 1;
            int index = 0;
            while (*q >= '0' && *q <= '9') index = index * 10 + (*q - '0'), q++;
            if (*q == '}') {
                const char *arg = index + 1 < input_count ? inputs[index + 1].str
                                                          : NULL;
                if (arg) {
                    size_t n = strlen(arg);
                    memcpy(w, arg, n);
                    w += n;
                }
                p = q + 1;
                continue;
            }
        }
        *w++ = *p++;
    }
    *w = '\0';
    r.str = out;
    return r;
}

static pin_spec fmt_text_inputs[] = {
    { "Format", PIN_STR, "const char *", PIN_DATA },
};
static pin_spec fmt_text_outputs[] = {
    { "Result", PIN_STR, "const char *", PIN_DATA },
};
node_desc NODE_Format_Text = {
    .name = "Format Text",
    .input_count = 1,
    .output_count = 1,
    .inputs = fmt_text_inputs,
    .outputs = fmt_text_outputs,
    .pure_kind = NODE_PURE_CALL,
    .fn = fmt_text,
    .display_class = NODE_DISPLAY_PURE,
    .category = "Text",
    .keywords = "format string text printf substitute",
};
