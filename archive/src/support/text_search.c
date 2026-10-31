#include "support/text_search.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

static int lower(char c)
{
    return tolower((unsigned char)c);
}

static bool boundary(const char *text, int i)
{
    if (!i || !isalnum((unsigned char)text[i - 1])) {
        return true;
    }
    return isupper((unsigned char)text[i]) &&
           (islower((unsigned char)text[i - 1]) ||
            (isupper((unsigned char)text[i - 1]) &&
             islower((unsigned char)text[i + 1])));
}

static bool equal(const char *a, const char *b, int length)
{
    for (int i = 0; i < length; i++) {
        if (!a[i] || lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return true;
}

/* One misspelling or adjacent transposition, only for words of four or more
 * characters. Short queries should not flood the list with unrelated nodes. */
static bool one_edit(const char *word, int length, const char *query, int count)
{
    if (count < 4 || length < count - 1 || length > count + 1) {
        return false;
    }
    int i = 0;
    while (i < length && i < count && lower(word[i]) == lower(query[i])) {
        i++;
    }
    if (length < count) {
        return equal(word + i, query + i + 1, length - i);
    }
    if (length > count) {
        return equal(word + i + 1, query + i, count - i);
    }
    if (i == count || equal(word + i + 1, query + i + 1, count - i - 1)) {
        return true;
    }
    return i + 1 < count && lower(word[i]) == lower(query[i + 1]) &&
           lower(word[i + 1]) == lower(query[i]) &&
           equal(word + i + 2, query + i + 2, count - i - 2);
}

/* How loosely a query word matches: a name also by its letters in order, text
 * by initials and one typo in four or more letters, and words only from a word
 * start, with one typo in five or more letters. */
typedef enum
{
    MATCH_NAME,
    MATCH_TEXT,
    MATCH_WORDS,
} match_mode;

static void mark_hit(bool *hits, size_t size, int at)
{
    if (hits && (size_t)at < size) {
        hits[at] = true;
    }
}

/* Highlight the letters retained by a one-edit match, omitting an inserted
 * or substituted name letter; a transposition retains both letters. */
static void mark_edit(const char *text, int length, const char *query, int count, bool *hits, size_t size, int at)
{
    int i = 0;
    while (i < length && i < count && lower(text[i]) == lower(query[i])) {
        mark_hit(hits, size, at + i++);
    }
    bool transpose = length == count && i + 1 < count &&
                     lower(text[i]) == lower(query[i + 1]) &&
                     lower(text[i + 1]) == lower(query[i]);
    if ((transpose || length < count) && i < length) {
        mark_hit(hits, size, at + i);
    }
    for (i++; i < length; i++) {
        mark_hit(hits, size, at + i);
    }
}

static int token_score(const char *text, const char *query, int count, match_mode mode, bool *hits, size_t hit_count)
{
    if (!text || !text[0]) {
        return -1;
    }
    int best = -1;
    int initials = 0, starts = 0, sequence = 0;
    enum
    {
        CONTIGUOUS,
        EDIT,
        INITIALS,
        SEQUENCE
    } matched = CONTIGUOUS;
    int offset = 0, length = count;
    bool typos = count >= (mode == MATCH_WORDS ? 5 : 4);
    for (int i = 0; text[i]; i++) {
        bool start = boundary(text, i);
        if ((start || mode != MATCH_WORDS) && equal(text + i, query, count)) {
            bool end = !text[i + count] || boundary(text, i + count) ||
                       !isalnum((unsigned char)text[i + count]);
            int score = start ? (end ? 600 : 500) : 360;
            score += i == 0 ? 140 : 0;
            if (score > best) {
                best = score;
                matched = CONTIGUOUS;
                offset = i;
                length = count;
            }
        }
        starts += start && isalnum((unsigned char)text[i]);
        if (initials < count && start && lower(text[i]) == lower(query[initials])) {
            initials++;
        }
        if (sequence < count && lower(text[i]) == lower(query[sequence])) {
            sequence++;
        }
        if (best >= 0 || !start || !isalnum((unsigned char)text[i])) {
            continue;
        }
        int end = i + 1;
        while (text[end] && isalnum((unsigned char)text[end]) && !boundary(text, end)) {
            end++;
        }
        if (typos && one_edit(text + i, end - i, query, count)) {
            best = 180;
            matched = EDIT;
            offset = i;
            length = end - i;
        }
    }
    int abbreviation = starts == count ? 340 : 280;
    if (mode != MATCH_WORDS && initials == count && best < abbreviation) {
        best = abbreviation;
        matched = INITIALS;
    }
    if (mode == MATCH_NAME && sequence == count && best < 100) {
        best = 100;
        matched = SEQUENCE;
    }
    if (typos && best < 180 && one_edit(text, (int)strlen(text), query, count)) {
        best = 180;
        matched = EDIT;
        offset = 0;
        length = (int)strlen(text);
    }
    if (!hits || best < 0) {
        return best;
    }
    if (matched == EDIT) {
        mark_edit(text + offset, length, query, count, hits, hit_count, offset);
        return best;
    }
    if (matched == CONTIGUOUS) {
        for (int i = offset; i < offset + length; i++) {
            mark_hit(hits, hit_count, i);
        }
        return best;
    }
    int part = 0;
    for (int i = 0; text[i] && part < count; i++) {
        if (lower(text[i]) == lower(query[part]) &&
            (matched == SEQUENCE || boundary(text, i))) {
            mark_hit(hits, hit_count, i);
            part++;
        }
    }
    return best;
}

static int search_score(
    const char *query,
    const char *name,
    const char *secondary,
    match_mode mode,
    bool *hits,
    size_t hit_count
)
{
    if (hits) {
        memset(hits, 0, hit_count * sizeof *hits);
    }
    if (!query) {
        return 0;
    }
    while (isspace((unsigned char)*query)) {
        query++;
    }
    int score = 0, words = 0;
    const char *word = query;
    while (*word) {
        int count = 0;
        while (word[count] && !isspace((unsigned char)word[count])) {
            count++;
        }
        int primary = token_score(name, word, count, mode, hits, hit_count);
        int extra = token_score(secondary, word, count, mode == MATCH_WORDS ? MATCH_WORDS : MATCH_TEXT, NULL, 0);
        if (extra >= 0) {
            extra = extra / 2;
        }
        int best = primary > extra ? primary : extra;
        if (best < 0) {
            if (hits) {
                memset(hits, 0, hit_count * sizeof *hits);
            }
            return -1;
        }
        score += best;
        words++;
        word += count;
        while (isspace((unsigned char)*word)) {
            word++;
        }
    }
    if (!words) {
        return 0;
    }
    int length = (int)(word - query);
    while (length && isspace((unsigned char)query[length - 1])) {
        length--;
    }
    bool exact = name && equal(name, query, length) && !name[length];
    return score / words + (exact ? 1000 : 0);
}

int text_search_score(const char *query, const char *name, const char *secondary)
{
    return search_score(query, name, secondary, MATCH_NAME, NULL, 0);
}

int text_search_word_score(const char *query, const char *name, const char *secondary)
{
    return search_score(query, name, secondary, MATCH_WORDS, NULL, 0);
}

int text_search_match(const char *query, const char *name, bool *hits, size_t hit_count)
{
    return search_score(query, name, NULL, MATCH_NAME, hits, hit_count);
}
