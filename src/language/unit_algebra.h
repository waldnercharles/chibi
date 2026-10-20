#ifndef CHIBI_LANGUAGE_UNIT_ALGEBRA_H
#define CHIBI_LANGUAGE_UNIT_ALGEBRA_H

#include <stdbool.h>
#include <stddef.h>

enum
{
    SCRIPT_UNIT_TERMS = 8,
    SCRIPT_UNIT_TEXT = 192
};
typedef enum
{
    SCRIPT_UNIT_UNKNOWN,
    SCRIPT_UNIT_LITERAL,
    SCRIPT_UNIT_KNOWN,
    SCRIPT_UNIT_INVALID,
} script_unit_kind;
typedef struct
{
    const char *name;
    int length, power;
} script_unit_term;
typedef struct
{
    script_unit_kind kind;
    int count;
    script_unit_term terms[SCRIPT_UNIT_TERMS];
} script_unit;

/* Pure unit policy. Parsed/product terms borrow their original text; preserve
 * those buffers while using the values. Text length must fit int. NULL means
 * unknown; empty/malformed text means invalid. No unit-name registry is used. */
script_unit script_unit_parse(const char *);
/* Compares dimensions, ignoring term order and kind. Callers check kind when
 * distinguishing unknown/literal/invalid from known dimensionless values. */
bool script_unit_same(script_unit, script_unit);
/* sign is 1 for multiplication, -1 for division. Overflow includes the eight
 * term accumulation limit and signed power overflow. Non-known factors yield
 * unknown, except two literals yield literal. No diagnostics are published. */
script_unit script_unit_product(script_unit, script_unit, int sign, bool *overflow);
/* size is positive. Writes at most size-1 bytes and NUL; a final incomplete
 * UTF-8 sequence is removed. SCRIPT_UNIT_TEXT is the diagnostic caller's usual
 * capacity, not a parsing/name limit. */
void script_unit_format(script_unit, char *, size_t size);
/* Shared diagnostic/output trimming: remove a final incomplete UTF-8 sequence
 * from an already NUL-terminated buffer, without changing complete bytes. */
void script_unit_whole_characters(char *);

#endif
