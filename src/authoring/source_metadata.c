#include "authoring/source_metadata.h"

#include "support/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#define METADATA_MAX_BYTES (8 * 1024 * 1024)
#define METADATA_MAX_DEPTH 256
#define METADATA_MAX_TOKENS 262144

typedef struct
{
    TSNode node;
    uint32_t start, end;
} token;

static void hash_text(const char *text, char out[17])
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; text[i]; i++) {
        hash ^= (unsigned char)text[i];
        hash *= UINT64_C(1099511628211);
    }
    snprintf(out, 17, "%016llx", (unsigned long long)hash);
}

static yyjson_doc *read_metadata(const char *json)
{
    if (!json || strlen(json) > METADATA_MAX_BYTES) {
        return NULL;
    }
    int depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; json[i]; i++) {
        char c = json[i];
        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                quoted = false;
            }
            continue;
        }
        if (c == '"') {
            quoted = true;
        } else if (c == '{' || c == '[') {
            if (++depth > METADATA_MAX_DEPTH) {
                return NULL;
            }
        } else if (c == '}' || c == ']') {
            if (--depth < 0) {
                return NULL;
            }
        }
    }
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    if (!yyjson_is_obj(yyjson_doc_get_root(doc))) {
        yyjson_doc_free(doc);
        return NULL;
    }
    return doc;
}

static bool matches(const yyjson_val *root, const char *text)
{
    const char *bound = yyjson_get_str(yyjson_obj_get(root, "source_hash"));
    char expected[17];
    hash_text(text, expected);
    return bound && !strcmp(bound, expected);
}

bool authoring_source_metadata_matches(const char *json, const char *text)
{
    if (strlen(text) > METADATA_MAX_BYTES) {
        return false;
    }
    yyjson_doc *doc = read_metadata(json);
    bool valid = doc && matches(yyjson_doc_get_root(doc), text);
    yyjson_doc_free(doc);
    return valid;
}

static char *write_bound(yyjson_mut_doc *doc, const char *text)
{
    char hash[17];
    hash_text(text, hash);
    yyjson_mut_obj_put(
        yyjson_mut_doc_get_root(doc),
        yyjson_mut_str(doc, "source_hash"),
        yyjson_mut_strcpy(doc, hash)
    );
    size_t length = 0;
    char *out = yyjson_mut_write(doc, 0, &length);
    if (length > METADATA_MAX_BYTES) {
        free(out);
        return NULL;
    }
    return out;
}

char *authoring_source_metadata_bind(const char *json, const char *text)
{
    if (strlen(text) > METADATA_MAX_BYTES) {
        return NULL;
    }
    yyjson_doc *doc = read_metadata(json);
    if (!doc) {
        return NULL;
    }
    yyjson_mut_doc *copy = yyjson_doc_mut_copy(doc, NULL);
    char *out = write_bound(copy, text);
    yyjson_mut_doc_free(copy);
    yyjson_doc_free(doc);
    return out;
}

static bool tokens(const script_syntax *syntax, token **out)
{
    if (syntax->diagnostic_count || strlen(syntax->text) > METADATA_MAX_BYTES) {
        return false;
    }
    int depth = 0;
    TSTreeCursor cursor = ts_tree_cursor_new(ts_tree_root_node(syntax->tree));
    bool valid = true;
    for (;;) {
        if (ts_tree_cursor_goto_first_child(&cursor)) {
            if (++depth > METADATA_MAX_DEPTH) {
                valid = false;
                break;
            }
            continue;
        }
        TSNode n = ts_tree_cursor_current_node(&cursor);
        uint32_t start = ts_node_start_byte(n), end = ts_node_end_byte(n);
        if (end > start) {
            if (acount(*out) == METADATA_MAX_TOKENS) {
                valid = false;
                break;
            }
            apush(*out, ((token){ n, start, end }));
        }
        while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
            if (!ts_tree_cursor_goto_parent(&cursor)) {
                goto done;
            }
            depth--;
        }
    }
done:
    ts_tree_cursor_delete(&cursor);
    return valid;
}

static bool equivalent(const script_syntax *a, const script_syntax *b, const token *x, const token *y)
{
    if (acount(x) != acount(y)) {
        return false;
    }
    for (int i = 0; i < acount(x); i++) {
        size_t length = x[i].end - x[i].start;
        if (length != y[i].end - y[i].start ||
            strcmp(ts_node_type(x[i].node), ts_node_type(y[i].node)) ||
            ts_node_is_named(x[i].node) != ts_node_is_named(y[i].node) ||
            memcmp(a->text + x[i].start, b->text + y[i].start, length)) {
            return false;
        }
    }
    return true;
}

static bool advance(TSTreeCursor *cursor, int *depth)
{
    for (;;) {
        if (ts_tree_cursor_goto_first_child(cursor)) {
            (*depth)++;
        } else {
            while (!ts_tree_cursor_goto_next_sibling(cursor)) {
                if (!ts_tree_cursor_goto_parent(cursor)) {
                    return false;
                }
                (*depth)--;
            }
        }
        TSNode node = ts_tree_cursor_current_node(cursor);
        if (ts_node_start_byte(node) != ts_node_end_byte(node)) {
            return true;
        }
    }
}

static bool same_tree(TSNode a, TSNode b, const char *before, const char *after)
{
    TSTreeCursor ca = ts_tree_cursor_new(a), cb = ts_tree_cursor_new(b);
    int da = 0, db = 0;
    bool same = true;
    for (;;) {
        a = ts_tree_cursor_current_node(&ca);
        b = ts_tree_cursor_current_node(&cb);
        const char *fa = ts_tree_cursor_current_field_name(&ca),
                   *fb = ts_tree_cursor_current_field_name(&cb);
        if (da != db || strcmp(ts_node_type(a), ts_node_type(b)) ||
            ts_node_is_named(a) != ts_node_is_named(b) ||
            (fa == NULL) != (fb == NULL) || (fa && strcmp(fa, fb))) {
            same = false;
            break;
        }
        if (!ts_node_child_count(a) && !ts_node_child_count(b)) {
            uint32_t sa = ts_node_start_byte(a), sb = ts_node_start_byte(b);
            uint32_t length = ts_node_end_byte(a) - sa;
            if (length != ts_node_end_byte(b) - sb ||
                memcmp(before + sa, after + sb, length)) {
                same = false;
                break;
            }
        }
        bool next_a = advance(&ca, &da), next_b = advance(&cb, &db);
        if (!next_a || !next_b) {
            same = next_a == next_b;
            break;
        }
    }
    ts_tree_cursor_delete(&ca);
    ts_tree_cursor_delete(&cb);
    return same;
}

static int token_at(const token *values, uint64_t offset)
{
    int lo = 0, hi = acount(values);
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (values[mid].start < offset) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo < acount(values) && values[lo].start == offset ? lo : -1;
}

char *authoring_source_metadata_remap(const char *json, const script_syntax *before, const script_syntax *after)
{
    if (strlen(before->text) > METADATA_MAX_BYTES ||
        strlen(after->text) > METADATA_MAX_BYTES) {
        return NULL;
    }
    yyjson_doc *doc = read_metadata(json);
    yyjson_val *root = yyjson_doc_get_root(doc);
    if (!doc || !matches(root, before->text) ||
        yyjson_get_uint(yyjson_obj_get(root, "version")) != 2) {
        yyjson_doc_free(doc);
        return NULL;
    }
    token *old = NULL, *next = NULL;
    bool valid = tokens(before, &old) && tokens(after, &next) &&
                 equivalent(before, after, old, next) &&
                 same_tree(
                     ts_tree_root_node(before->tree),
                     ts_tree_root_node(after->tree),
                     before->text,
                     after->text
                 );
    yyjson_mut_doc *copy = valid ? yyjson_doc_mut_copy(doc, NULL) : NULL;
    yyjson_mut_val *anchors = yyjson_mut_obj_get(yyjson_mut_doc_get_root(copy), "anchors");
    if (anchors && (!yyjson_mut_is_arr(anchors) ||
                    yyjson_mut_arr_size(anchors) > METADATA_MAX_TOKENS)) {
        valid = false;
    }
    size_t i, count;
    yyjson_mut_val *anchor;
    yyjson_mut_arr_foreach(anchors, i, count, anchor)
    {
        yyjson_mut_val *start = yyjson_mut_obj_get(anchor, "start");
        uint64_t offset = yyjson_mut_get_uint(start);
        int at = token_at(old, offset);
        if (!yyjson_mut_is_obj(anchor) || !yyjson_mut_is_uint(start) ||
            at < 0 || !yyjson_mut_get_uint(yyjson_mut_obj_get(anchor, "id")) ||
            !yyjson_mut_get_str(yyjson_mut_obj_get(anchor, "kind"))) {
            valid = false;
            break;
        }
        yyjson_mut_obj_put(
            anchor,
            yyjson_mut_str(copy, "start"),
            yyjson_mut_uint(copy, next[at].start)
        );
    }
    char *out = valid ? write_bound(copy, after->text) : NULL;
    yyjson_mut_doc_free(copy);
    yyjson_doc_free(doc);
    afree(old);
    afree(next);
    return out;
}
