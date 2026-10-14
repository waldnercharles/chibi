#ifndef CHIBI_TEXT_SEARCH_H
#define CHIBI_TEXT_SEARCH_H
#include <stdbool.h>
/* Deterministic Unicode scalar matching, with locale-independent ASCII case
 * folding when requested. Other scalars compare exactly; no normalization.
 * Empty query matches any valid text. Malformed UTF-8 or an exceeded byte
 * budget returns false/zero, never a match of a silently truncated prefix.
 * Ranking: zero absent; exact > prefix > substring > ordered subsequence.
 * Scores measure presentation order only, not semantic similarity. */
enum
{
    TEXT_SEARCH_TEXT_MAX = 16384,
    TEXT_SEARCH_QUERY_MAX = 256
};
bool text_search_contains(const char *query, const char *text, bool ignorecase);
int text_search_rank(const char *query, const char *text, bool ignorecase);
#endif
