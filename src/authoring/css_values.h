#ifndef CHIBI_CSS_VALUES_H
#define CHIBI_CSS_VALUES_H
#include <stdbool.h>
#include <stddef.h>
enum
{
    CSS_VALUE_TEXT_MAX = 4096,
    CSS_VALUE_PROPERTY_MAX = 127
};
typedef struct
{
    bool ok, known, simple, inherited, shorthand, length, number, percent,
        color, integer;
    const char *kind, *initial, *keywords, *error;
    double value;
    char unit[8];
    float rgba[4]; /* Linear RGB, straight alpha, using runtime/color_value. */
} css_value;
/* CPU-only pinned RCSS scalar metadata. Unknown properties, shorthands and
 * complex expressions remain literal text: ok=true/simple=false. No evaluation,
 * normalization or conversion of var/calc, percentages or relative lengths. */
css_value css_value_query(const char *property, const char *literal);
/* Fixed operation number/color/keyword. Current must be a recognized simple
 * literal of that property. Empty unit preserves Current's unit; other units
 * require exact equality, so this never silently converts relative lengths.
 * Failure preserves output. Complex expressions always require source editing. */
bool css_value_format(
    const char *property,
    const char *current,
    const char *operation,
    double number,
    const char *unit,
    const char *keyword,
    const float rgba[4],
    char *out,
    size_t size,
    char *error,
    size_t error_size
);
#endif
