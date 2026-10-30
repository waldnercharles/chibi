/* Ranked lists (text_rank.h). */
#include "support/text_rank.h"

#include "support/array.h"
#include "support/text_search.h"

#include <stdlib.h>
#include <string.h>

typedef struct ranked
{
    text_rank_row row;
    int score;
    int group_score; /* the best score in its group */
} ranked;

static const char *group_of(const text_rank_entry *entry)
{
    return entry->group ? entry->group : "";
}

/* Numbers the rows' groups by first appearance and gives each row its
   group's best score. */
static void group_rows(ranked *rows, const text_rank_entry *entries)
{
    const char **names = NULL;
    int *best = NULL;
    for (int i = 0; i < acount(rows); i++) {
        const char *name = group_of(&entries[rows[i].row.index]);
        int g = 0;
        while (g < acount(names) && strcmp(names[g], name)) {
            g++;
        }
        if (g == acount(names)) {
            apush(names, name);
            apush(best, rows[i].score);
        }
        rows[i].row.group = g;
        best[g] = rows[i].score > best[g] ? rows[i].score : best[g];
    }
    for (int i = 0; i < acount(rows); i++) {
        rows[i].group_score = best[rows[i].row.group];
    }
    afree(names);
    afree(best);
}

/* Groups by best score, then by first appearance; rows by score, then by
   entry order. */
static int compare_rows(const void *a, const void *b)
{
    const ranked *x = a, *y = b;
    if (x->group_score != y->group_score) {
        return x->group_score > y->group_score ? -1 : 1;
    }
    if (x->row.group != y->row.group) {
        return x->row.group - y->row.group;
    }
    if (x->score != y->score) {
        return x->score > y->score ? -1 : 1;
    }
    return x->row.index - y->row.index;
}

static void limit_groups(ranked *rows)
{
    int kept = 0, run = 0, group = -1;
    for (int i = 0; i < acount(rows); i++) {
        run = rows[i].row.group == group ? run + 1 : 0;
        group = rows[i].row.group;
        if (run < TEXT_RANK_GROUP_LIMIT) {
            rows[kept++] = rows[i];
        }
    }
    asetlen(rows, kept);
}

text_rank_row *text_rank(const text_rank_entry *entries, int count, const char *query, unsigned flags)
{
    bool ordered = flags & TEXT_RANK_ORDERED;
    bool filter = !ordered && query[strspn(query, " ")];
    ranked *rows = NULL;
    for (int i = 0; i < count; i++) {
        int score = ordered ? count - i : 1;
        if (filter) {
            score = text_search_score(query, entries[i].name, entries[i].secondary);
            if (score < 0) {
                continue;
            }
            score += entries[i].boost;
        }
        apush(rows, (ranked){ .row.index = i, .score = score });
    }
    text_rank_row *out = NULL;
    if (!acount(rows)) {
        return out;
    }
    group_rows(rows, entries);
    asort(rows, compare_rows);
    if (filter && (flags & TEXT_RANK_LIMIT)) {
        limit_groups(rows);
    }
    asetlen(out, acount(rows));
    for (int i = 0; i < acount(rows); i++) {
        out[i] = rows[i].row;
    }
    afree(rows);
    return out;
}
