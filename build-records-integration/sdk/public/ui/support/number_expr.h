#ifndef CHIBI_SUPPORT_NUMBER_EXPR_H
#define CHIBI_SUPPORT_NUMBER_EXPR_H

/* Numbers as a number field shows and reads them. */

#include <stdbool.h>

/* The buffer number_format writes. */
#define NUMBER_TEXT 64

/* `value` with `precision` decimals and no trailing zeros, or none when
 * `integer`; "-" when it is not finite, and never "-0". */
void number_format(char out[static NUMBER_TEXT], double value, int precision, bool integer);

/* Arithmetic as typed into a number field: + - * / with parentheses, unary
 * signs, pi and a leading "=", as "=2*pi" or "120*1.5". A leading *, / or +
 * applies to `current`, as "*2" doubles it. False for text that is not a
 * whole expression or whose result is not finite. */
bool number_expr_eval(const char *text, double current, double *out);

#endif
