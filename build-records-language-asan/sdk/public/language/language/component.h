#ifndef CHIBI_LANGUAGE_COMPONENT_H
#define CHIBI_LANGUAGE_COMPONENT_H
#include "capabilities/catalog.h"
#include "language/syntax.h"

/* One field of a component a .chibi source declares:
 *
 *     // Stored energy that refills over time.
 *     component Charge {
 *         level: float
 *         // Units gained per second.
 *         rate: 0..10 u/s
 *         delay: s
 *     }
 *
 * A field's type is a scalar or struct type name, or a measure: a number with
 * an optional range and an optional unit, such as `banana/basket`. Unit names
 * mean units only in a field's type, so they stay free names everywhere else.
 * A lone name is a type unless no type has it and it is a common unit, such
 * as `s`. The `//` comments directly above the component and each field
 * describe them. */
typedef struct
{
    char *name;
    char *type;        /* the authored type name; "number" for a measure */
    char *unit;        /* display text, such as "u/s²"; NULL without one */
    char *description; /* NULL without one */
    double min, max; /* inclusive stored-value contract; unbounded when equal */
    script_range range;
} script_component_field;
typedef struct
{
    char *name, *description;
    script_range range;
    script_component_field *fields; /* support/array.h */
} script_component;

/* Reads the one component `syntax` declares, or appends to *diagnostics and
 * returns NULL. Types are looked up as built-in types, then in `catalog`. */
script_component *script_component_read(
    const script_syntax *,
    const script_catalog *,
    script_diagnostic **diagnostics
);
void script_component_free(script_component *);
/* Common unambiguous unit names that may stand alone in a numeric type slot. */
bool script_unit_shorthand(const char *);
#endif
