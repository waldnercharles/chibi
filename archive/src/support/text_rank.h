#ifndef CHIBI_SUPPORT_TEXT_RANK_H
#define CHIBI_SUPPORT_TEXT_RANK_H

/* Ranked lists, as the command palette, pickers and dropdowns show them
 * (cmdk): entries a query matches (support/text_search.h's score plus the
 * entry's boost, such as recent use), best first, with each group's rows
 * kept together and groups ordered by their best match. With no query the
 * entries keep their order, grouped by first appearance. */

#include <stdbool.h>

/* Rows a group keeps while a query filters, with TEXT_RANK_LIMIT. */
#define TEXT_RANK_GROUP_LIMIT 8

typedef struct text_rank_entry
{
    const char *name;      /* matched first */
    const char *secondary; /* keywords; NULL for none */
    const char *group;     /* NULL or "" for none */
    int boost;             /* added to a match's score */
} text_rank_entry;

typedef struct text_rank_row
{
    int index; /* into the entries */
    int group; /* numbered by first appearance */
} text_rank_row;

enum
{
    /* The entries come ranked: shown in order, grouped, never filtered. */
    TEXT_RANK_ORDERED = 1,
    /* A filtering query keeps TEXT_RANK_GROUP_LIMIT rows per group. */
    TEXT_RANK_LIMIT = 2,
};

/* The rows to show, in order; the caller afrees them. */
text_rank_row *text_rank(const text_rank_entry *entries, int count, const char *query, unsigned flags);

#endif
