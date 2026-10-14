#include "authoring/css_values.h"

#include "runtime/color_value.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum
{
    LENGTH = 1,
    NUMBER = 2,
    PERCENT = 4,
    COLOR = 8
};
typedef struct
{
    const char *name, *initial, *keywords;
    int flags;
    bool inherited, shorthand;
} property;
#include "css_values_metadata.inc"
static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static bool bounded(const char *text, size_t max)
{
    return text && strnlen(text, max + 1) <= max;
}
static const property *find(const char *name)
{
    size_t low = 0, high = sizeof PROPERTIES / sizeof *PROPERTIES;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        int order = strcmp(name, PROPERTIES[mid].name);
        if (!order) {
            return &PROPERTIES[mid];
        }
        if (order < 0) {
            high = mid;
        } else {
            low = mid + 1;
        }
    }
    return NULL;
}
static bool keyword(const char *options, const char *text)
{
    for (const char *at = options; *at;) {
        const char *end = strchr(at, ',');
        if (!end) {
            end = at + strlen(at);
        }
        if ((size_t)(end - at) == strlen(text) &&
            !strncmp(at, text, (size_t)(end - at))) {
            return true;
        }
        at = *end ? end + 1 : end;
    }
    return false;
}
static bool unit_allowed(const property *p, const char *unit, double value)
{
    if (!*unit) {
        return (p->flags & NUMBER) || ((p->flags & LENGTH) && value == 0);
    }
    if (!strcmp(unit, "%")) {
        return (p->flags & PERCENT) != 0;
    }
    static const char *const LENGTH_UNITS[] = { "px", "dp", "em", "rem",
                                                "vw", "vh", "in", "cm",
                                                "mm", "pt", "pc" };
    if (p->flags & LENGTH) {
        for (size_t i = 0; i < sizeof LENGTH_UNITS / sizeof *LENGTH_UNITS; i++) {
            if (!strcmp(unit, LENGTH_UNITS[i])) {
                return true;
            }
        }
    }
    return false;
}
static bool numeric(const property *p, const char *text, double *value, char unit[8])
{
    const char *at = text;
    if (*at == '+' || *at == '-') {
        at++;
    }
    bool digit = false;
    while (isdigit((unsigned char)*at)) {
        digit = true;
        at++;
    }
    if (*at == '.') {
        at++;
        while (isdigit((unsigned char)*at)) {
            digit = true;
            at++;
        }
    }
    if (!digit) {
        return false;
    }
    if (*at == 'e' || *at == 'E') {
        const char *exponent = at + 1;
        if (*exponent == '+' || *exponent == '-') {
            exponent++;
        }
        if (isdigit((unsigned char)*exponent)) {
            at = exponent;
            while (isdigit((unsigned char)*at)) {
                at++;
            }
        }
    }
    size_t length = strlen(at);
    if (length >= 8) {
        return false;
    }
    char *end;
    double number = strtod(text, &end);
    if (end != at || !isfinite(number) || fabs(number) > FLT_MAX ||
        !unit_allowed(p, at, number)) {
        return false;
    }
    if (!strcmp(p->name, "z-index") && trunc(number) != number) {
        return false;
    }
    *value = number;
    memcpy(unit, at, length + 1);
    return true;
}
static bool color(const char *text, float rgba[4])
{
    char expanded[10];
    if (!strcmp(text, "transparent")) {
        text = "#00000000";
    } else if (!strcmp(text, "black")) {
        text = "#000000ff";
    } else if (!strcmp(text, "white")) {
        text = "#ffffffff";
    }
    size_t n = strlen(text);
    if (*text == '#' && (n == 4 || n == 5)) {
        expanded[0] = '#';
        for (size_t i = 1; i < n; i++) {
            expanded[i * 2 - 1] = text[i];
            expanded[i * 2] = text[i];
        }
        expanded[n * 2 - 1] = 0;
        text = expanded;
    }
    return *text == '#' && color_value_from_hex(text, rgba, NULL, 0);
}
css_value css_value_query(const char *name, const char *literal)
{
    css_value result = { .kind = "text", .initial = "", .keywords = "", .error = "" };
    if (!bounded(name, CSS_VALUE_PROPERTY_MAX) ||
        !bounded(literal, CSS_VALUE_TEXT_MAX)) {
        result.error = "CSS property/value exceeds bounds or is missing";
        return result;
    }
    result.ok = true;
    const property *p = find(name);
    if (!p) {
        return result;
    }
    result.known = true;
    result.initial = p->initial;
    result.keywords = p->keywords;
    result.inherited = p->inherited;
    result.shorthand = p->shorthand;
    result.length = (p->flags & LENGTH) != 0;
    result.number = (p->flags & NUMBER) != 0;
    result.percent = (p->flags & PERCENT) != 0;
    result.color = (p->flags & COLOR) != 0;
    result.integer = !strcmp(name, "z-index");
    if (p->shorthand) {
        return result;
    }
    char text[CSS_VALUE_TEXT_MAX + 1];
    const char *start = literal;
    while (isspace((unsigned char)*start)) {
        start++;
    }
    size_t bytes = strlen(start);
    while (bytes && isspace((unsigned char)start[bytes - 1])) {
        bytes--;
    }
    memcpy(text, start, bytes);
    text[bytes] = 0;
    if (keyword(p->keywords, text)) {
        result.kind = "keyword";
        result.simple = true;
    } else if ((p->flags & COLOR) && color(text, result.rgba)) {
        result.kind = "color";
        result.simple = true;
    } else if (numeric(p, text, &result.value, result.unit)) {
        result.kind = result.length ? "length" : "number";
        result.simple = true;
    }
    return result;
}
bool css_value_format(
    const char *name,
    const char *current,
    const char *operation,
    double number,
    const char *unit,
    const char *choice,
    const float rgba[4],
    char *out,
    size_t size,
    char *error,
    size_t error_size
)
{
    css_value value = css_value_query(name, current);
    if (!value.ok || !value.known || value.shorthand || !bounded(operation, 16) ||
        !bounded(unit, 7) || !bounded(choice, CSS_VALUE_TEXT_MAX)) {
        return fail(error, error_size, "Expected a known scalar CSS property and bounded format inputs");
    }
    const property *p = find(name);
    char text[CSS_VALUE_TEXT_MAX + 1];
    if (!value.simple) {
        return fail(error, error_size, "Complex CSS values remain literal; edit their source expression");
    }
    if (!strcmp(operation, "keyword")) {
        if (!keyword(p->keywords, choice)) {
            return fail(error, error_size, "Keyword is not valid for this CSS property");
        }
        snprintf(text, sizeof text, "%s", choice);
    } else if (!strcmp(operation, "number")) {
        if (strcmp(value.kind, "length") && strcmp(value.kind, "number")) {
            return fail(error, error_size, "Current CSS value is not a simple number or length");
        }
        if (!isfinite(number) || fabs(number) > FLT_MAX ||
            (value.integer && trunc(number) != number)) {
            return fail(error, error_size, "CSS number must be finite and match the property's integer requirement");
        }
        if (*unit && strcmp(unit, value.unit)) {
            return fail(error, error_size, "CSS units must be preserved; edit source to change the unit explicitly");
        }
        if (!unit_allowed(p, value.unit, number)) {
            return fail(error, error_size, "Number/unit is not valid for this CSS property");
        }
        snprintf(text, sizeof text, "%.17g%s", number == 0 ? 0 : number, value.unit);
    } else if (!strcmp(operation, "color")) {
        if (strcmp(value.kind, "color") || !rgba ||
            !color_value_to_hex(rgba, text, error, error_size)) {
            return fail(error, error_size, "Current CSS value is not a simple color or components are invalid");
        }
    } else {
        return fail(error, error_size, "Unknown CSS format operation");
    }
    size_t bytes = strlen(text);
    if (!out || size <= bytes) {
        return fail(error, error_size, "CSS output buffer is too small");
    }
    memcpy(out, text, bytes + 1);
    if (error_size) {
        error[0] = 0;
    }
    return true;
}
