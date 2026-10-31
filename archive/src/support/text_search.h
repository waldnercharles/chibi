#ifndef CHIBI_SUPPORT_TEXT_SEARCH_H
#define CHIBI_SUPPORT_TEXT_SEARCH_H

#include <stdbool.h>
#include <stddef.h>

/* All query words must match. Names rank above secondary text such as paths,
 * categories and keywords. Returns -1 for no match; an empty query scores 0. */
int text_search_score(const char *query, const char *name, const char *secondary);
/* The same for callers that type whole words, such as agents: no initials or
 * letters scattered through a name, and typos only in five or more letters. */
int text_search_word_score(const char *query, const char *name, const char *secondary);
/* Name matching with the same fuzzy ranking and byte positions for highlighting.
 * `hits` may be NULL; otherwise its first hit_count entries are cleared, then
 * matched name bytes are marked. Empty queries score 0 without highlighting. */
int text_search_match(const char *query, const char *name, bool *hits, size_t hit_count);

#endif
