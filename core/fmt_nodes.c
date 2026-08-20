/* core/fmt_nodes.c -- CAT-38 Format Text, registered as EXTRA_NODES.
 *
 * Pure node: one format input, a user-extensible group of argument inputs
 * (PIN-14, minimum zero args), and one String output. Evaluating it
 * substitutes each {0}, {1}, ... in the format string with the
 * corresponding argument's rendered text; a placeholder whose index has no
 * matching argument renders as the empty string. The result is built in the
 * arena, so it lives as long as the other scratch-owned values of the step.
 *
 * Arguments are String by default and retypeable (PIN-3). A non-string
 * argument renders through pin_value_to_string, the canonical pin display
 * (UI-39), so an Integer or Real argument formats like it reads on the node
 * without a crash and without a manual To String.
 */
#include "fmt_nodes.h"

#include "arena.h"
#include "pin_fmt.h"

#include <string.h>

static const char *fmt_text_of(const pin_value *v)
{
    if (v->type == PIN_STR || v->type == PIN_NAME) return v->str ? v->str : "";
    return pin_value_to_string(v);
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
    for (const char *p = format; *p;) {
        /* A placeholder is '{' followed by digits followed by '}'; anything
         * else is copied verbatim, so "{{" and "}" stay literal. */
        if (p[0] == '{' && p[1] >= '0' && p[1] <= '9') {
            const char *q = p + 1;
            int index = 0;
            while (*q >= '0' && *q <= '9') index = index * 10 + (*q - '0'), q++;
            if (*q == '}') {
                const char *arg = index + 1 < input_count
                                    ? fmt_text_of(&inputs[index + 1])
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
