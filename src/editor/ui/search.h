#ifndef CHIBI_EDITOR_UI_SEARCH_H
#define CHIBI_EDITOR_UI_SEARCH_H

/* All query words must match. Names rank above secondary text such as paths,
 * categories and keywords. Returns -1 for no match; an empty query scores 0. */
int editor_search_score(const char *query, const char *name, const char *secondary);

typedef struct
{
    int index;
    int score;
} editor_search_result;

/* Descending relevance, retaining catalog order for equal scores. */
int editor_search_compare(const void *a, const void *b);

#endif
