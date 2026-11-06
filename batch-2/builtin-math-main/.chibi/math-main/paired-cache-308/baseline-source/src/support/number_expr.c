#include "support/number_expr.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* An expression's text holds at most this many bytes after the current
 * value is put before a relative operator. */
#define EXPR_TEXT 256

void number_format(char out[static NUMBER_TEXT], double value, int precision, bool integer)
{
    if (!isfinite(value)) {
        snprintf(out, NUMBER_TEXT, "-");
        return;
    }
    if (integer) {
        snprintf(out, NUMBER_TEXT, "%.0f", value);
        return;
    }
    snprintf(out, NUMBER_TEXT, "%.*f", precision > 0 ? precision : 0, value);
    if (strchr(out, '.')) {
        size_t length = strlen(out);
        while (length && out[length - 1] == '0') {
            out[--length] = 0;
        }
        if (length && out[length - 1] == '.') {
            out[--length] = 0;
        }
    }
    if (strcmp(out, "-0") == 0) {
        snprintf(out, NUMBER_TEXT, "0");
    }
}

typedef struct expr
{
    const char *p;
    bool ok;
} expr;

static double expr_sum(expr *e);

static void expr_space(expr *e)
{
    while (*e->p == ' ') {
        e->p++;
    }
}

static bool expr_pi(const char *p)
{
    return (p[0] == 'p' || p[0] == 'P') && (p[1] == 'i' || p[1] == 'I');
}

static double expr_primary(expr *e)
{
    expr_space(e);
    if (*e->p == '(') {
        e->p++;
        double v = expr_sum(e);
        expr_space(e);
        e->ok &= *e->p == ')';
        if (*e->p) {
            e->p++;
        }
        return v;
    }
    if (*e->p == '-') {
        e->p++;
        return -expr_primary(e);
    }
    if (*e->p == '+') {
        e->p++;
        return expr_primary(e);
    }
    if (expr_pi(e->p)) {
        e->p += 2;
        return M_PI;
    }
    char *end = NULL;
    double v = strtod(e->p, &end);
    e->ok &= end != e->p;
    e->p = end;
    return v;
}

static double expr_product(expr *e)
{
    double v = expr_primary(e);
    for (;;) {
        expr_space(e);
        char op = *e->p;
        if (op != '*' && op != '/') {
            return v;
        }
        e->p++;
        double rhs = expr_primary(e);
        v = op == '*' ? v * rhs : v / rhs;
    }
}

static double expr_sum(expr *e)
{
    double v = expr_product(e);
    for (;;) {
        expr_space(e);
        char op = *e->p;
        if (op != '+' && op != '-') {
            return v;
        }
        e->p++;
        double rhs = expr_product(e);
        v = op == '+' ? v + rhs : v - rhs;
    }
}

static double parse(const char *s)
{
    if (!s[0]) {
        return NAN;
    }
    expr e = { s, true };
    double v = expr_sum(&e);
    expr_space(&e);
    return e.ok && !*e.p && isfinite(v) ? v : NAN;
}

bool number_expr_eval(const char *text, double current, double *out)
{
    assert(text && out);
    while (*text == ' ') {
        text++;
    }
    if (*text == '=') {
        text++;
    }
    char s[EXPR_TEXT] = "";
    if (*text == '*' || *text == '/' || *text == '+') {
        number_format(s, current, 12, false);
    }
    size_t base = strlen(s);
    if (base + strlen(text) >= sizeof s) {
        return false;
    }
    snprintf(s + base, sizeof s - base, "%s", text);
    double value = parse(s);
    if (!isfinite(value)) {
        return false;
    }
    *out = value;
    return true;
}
