#include "editor/ui/search.h"

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

static int token_score(const char *text, const char *query, int count, bool fuzzy)
{
    if (!text || !text[0]) {
        return -1;
    }
    int best = -1;
    int initials = 0, starts = 0, sequence = 0;
    for (int i = 0; text[i]; i++) {
        bool start = boundary(text, i);
        if (equal(text + i, query, count)) {
            bool end = !text[i + count] || boundary(text, i + count) ||
                       !isalnum((unsigned char)text[i + count]);
            int score = start ? (end ? 600 : 500) : 360;
            score += i == 0 ? 140 : 0;
            if (score > best) {
                best = score;
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
        if (one_edit(text + i, end - i, query, count)) {
            best = 180;
        }
    }
    int abbreviation = starts == count ? 340 : 280;
    if (initials == count && best < abbreviation) {
        best = abbreviation;
    }
    if (fuzzy && sequence == count && best < 100) {
        best = 100;
    }
    if (best < 180 && one_edit(text, (int)strlen(text), query, count)) {
        best = 180;
    }
    return best;
}

int editor_search_score(const char *query, const char *name, const char *secondary)
{
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
        int primary = token_score(name, word, count, true);
        int extra = token_score(secondary, word, count, false);
        if (extra >= 0) {
            extra = extra / 2;
        }
        int best = primary > extra ? primary : extra;
        if (best < 0) {
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

int editor_search_compare(const void *a, const void *b)
{
    const editor_search_result *left = a, *right = b;
    int score = right->score - left->score;
    return score ? score : left->index - right->index;
}
