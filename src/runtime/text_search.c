#include "runtime/text_search.h"

#include <stdint.h>
#include <string.h>

/* Reject overlong encodings, surrogates, unfinished sequences and scalars
 * outside Unicode. The byte ceiling also bounds validation of hostile input. */
static int decode(const char *text, uint32_t *out, int limit, bool fold)
{
    int count = 0, bytes = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        if (bytes >= limit) {
            return -1;
        }
        unsigned char lead = *p++;
        bytes++;
        uint32_t cp = lead;
        int rest = 0;
        uint32_t minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) {
            cp = lead & 31;
            rest = 1;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            cp = lead & 15;
            rest = 2;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            cp = lead & 7;
            rest = 3;
            minimum = 0x10000;
        } else if (lead >= 0x80) {
            return -1;
        }
        for (int i = 0; i < rest; i++) {
            if (bytes >= limit || (*p & 0xc0) != 0x80) {
                return -1;
            }
            cp = (cp << 6) | (*p++ & 63);
            bytes++;
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
            return -1;
        }
        if (fold && cp >= 'A' && cp <= 'Z') {
            cp += 'a' - 'A';
        }
        out[count++] = cp;
    }
    return count;
}
static int substring(const uint32_t *query, int q, const uint32_t *text, int n)
{
    if (!q) {
        return 0;
    }
    for (int i = 0; i <= n - q; i++) {
        if (text[i] == query[0] && !memcmp(text + i, query, (size_t)q * sizeof *text)) {
            return i;
        }
    }
    return -1;
}
static bool prepare(
    const char *query,
    const char *text,
    bool fold,
    uint32_t *q,
    uint32_t *t,
    int *qn,
    int *tn
)
{
    *qn = decode(query ? query : "", q, TEXT_SEARCH_QUERY_MAX, fold);
    if (*qn < 0) {
        return false;
    }
    *tn = decode(text ? text : "", t, TEXT_SEARCH_TEXT_MAX, fold);
    return *tn >= 0;
}
bool text_search_contains(const char *query, const char *text, bool ignorecase)
{
    uint32_t q[TEXT_SEARCH_QUERY_MAX], t[TEXT_SEARCH_TEXT_MAX];
    int qn, tn;
    return prepare(query, text, ignorecase, q, t, &qn, &tn) &&
           substring(q, qn, t, tn) >= 0;
}
static bool boundary(uint32_t cp)
{
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '_' || cp == '-' ||
           cp == '/' || cp == '.' || cp == ':';
}
int text_search_rank(const char *query, const char *text, bool ignorecase)
{
    uint32_t q[TEXT_SEARCH_QUERY_MAX], t[TEXT_SEARCH_TEXT_MAX];
    int qn, tn;
    if (!prepare(query, text, ignorecase, q, t, &qn, &tn)) {
        return 0;
    }
    if (!qn) {
        return 1;
    }
    int at = substring(q, qn, t, tn);
    if (at == 0 && qn == tn) {
        return 100000;
    }
    if (at == 0) {
        return 80000 - (tn - qn);
    }
    if (at > 0) {
        return 60000 - at;
    }
    int matched = 0, previous = -2, score = 20000;
    for (int i = 0; i < tn && matched < qn; i++) {
        if (t[i] == q[matched]) {
            if (!matched) {
                score -= i;
            } else {
                score -= i - previous - 1;
            }
            if (i == previous + 1) {
                score += 20;
            }
            if (!i || boundary(t[i - 1])) {
                score += 30;
            }
            previous = i;
            matched++;
        }
    }
    return matched == qn ? score : 0;
}
