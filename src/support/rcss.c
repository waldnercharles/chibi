#include "support/rcss.h"

#include "support/array.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put(char **out, const char *text, size_t length)
{
    if (!length) {
        return;
    }
    int start = acount(*out);
    asetlen(*out, start + (int)length);
    memcpy(*out + start, text, length);
}

static void put_text(char **out, const char *text)
{
    put(out, text, strlen(text));
}

static char *take(char **out)
{
    int length = acount(*out);
    char *text = xmalloc((size_t)length + 1);
    if (length) {
        memcpy(text, *out, (size_t)length);
    }
    text[length] = '\0';
    afree(*out);
    *out = NULL;
    return text;
}

static void free_strings(char **strings)
{
    for (int i = 0; i < acount(strings); i++) {
        free(strings[i]);
    }
    afree(strings);
}

/* ---------- Expansion ---------- */

/* Every file one expansion reads, so each is read once, and where its
 * message goes. */
typedef struct
{
    char **files;
    char *error;
    size_t error_size;
} expansion;

typedef struct
{
    expansion *x;
    const char *s; /* comment-free source */
    size_t at;
    int file;         /* its index in x->files, or -1 for the text expanded */
    const char *name; /* for messages */
    const char *directory;
    size_t counted; /* the bytes line_at has counted, */
    int line;       /* and the line after them */
} reader;

/* Expanded RCSS and the origin of each line it ends. */
typedef struct
{
    char *text; /* support/array.h, without a terminator */
    rcss_origin *lines;
} output;

/* The 1-based line of the source's byte `at`. */
static int line_at(reader *r, size_t at)
{
    if (at < r->counted) {
        r->counted = 0;
        r->line = 1;
    }
    for (; r->counted < at && r->s[r->counted]; r->counted++) {
        r->line += r->s[r->counted] == '\n';
    }
    return r->line;
}

static rcss_origin origin(reader *r, size_t at)
{
    return (rcss_origin){ r->file, line_at(r, at) };
}

/* Appends `length` bytes of `text` from line `at`, which each line break
 * written moves down. */
static void emit(output *o, rcss_origin *at, const char *text, size_t length)
{
    put(&o->text, text, length);
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\n') {
            apush(o->lines, *at);
            at->line++;
        }
    }
}

static void emit_text(output *o, rcss_origin *at, const char *text)
{
    emit(o, at, text, strlen(text));
}

static void emit_output(output *o, const output *from)
{
    put(&o->text, from->text, (size_t)acount(from->text));
    for (int i = 0; i < acount(from->lines); i++) {
        apush(o->lines, from->lines[i]);
    }
}

static void output_free(output *o)
{
    afree(o->text);
    afree(o->lines);
}

static bool fail(reader *r, const char *format, ...)
{
    int line = line_at(r, r->at);
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);
    if (r->name[0]) {
        snprintf(r->x->error, r->x->error_size, "%s line %d: %s", r->name, line, message);
        return false;
    }
    snprintf(r->x->error, r->x->error_size, "line %d: %s", line, message);
    return false;
}

/* The source without comments; newlines inside block comments stay, so line
 * numbers hold. A `//` inside parentheses, as in url(//x), is not a comment. */
static char *strip_comments(const char *s, bool *closed)
{
    char *out = NULL;
    char quote = 0;
    int depth = 0;
    *closed = true;
    for (const char *p = s; *p; p++) {
        if (quote) {
            put(&out, p, 1);
            if (*p == '\\' && p[1]) {
                put(&out, ++p, 1);
            } else if (*p == quote) {
                quote = 0;
            }
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            const char *end = strstr(p + 2, "*/");
            *closed &= end != NULL;
            const char *last = end ? end + 1 : p + strlen(p) - 1;
            for (const char *c = p; c <= last; c++) {
                if (*c == '\n') {
                    put(&out, c, 1);
                }
            }
            p = last;
            continue;
        }
        if (p[0] == '/' && p[1] == '/' && depth == 0) {
            while (p[1] && p[1] != '\n') {
                p++;
            }
            continue;
        }
        quote = *p == '"' || *p == '\'' ? *p : 0;
        depth += *p == '(' ? 1 : *p == ')' && depth ? -1 : 0;
        put(&out, p, 1);
    }
    return take(&out);
}

/* Moves to the first `;`, `{` or `}` outside quotes and parentheses and
 * returns it, or 0 at the end. */
static char scan(reader *r)
{
    char quote = 0;
    int depth = 0;
    for (; r->s[r->at]; r->at++) {
        char c = r->s[r->at];
        if (quote) {
            r->at += c == '\\' && r->s[r->at + 1];
            quote = c == quote ? 0 : quote;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            continue;
        }
        depth += c == '(' ? 1 : c == ')' && depth ? -1 : 0;
        if (!depth && (c == ';' || c == '{' || c == '}')) {
            return c;
        }
    }
    return 0;
}

/* The text trimmed and on one line: each line break, with the space around
 * it, becomes one space. */
static char *trimmed(const char *s, size_t length)
{
    while (length && isspace((unsigned char)*s)) {
        s++;
        length--;
    }
    while (length && isspace((unsigned char)s[length - 1])) {
        length--;
    }
    char *out = NULL;
    for (size_t i = 0; i < length; i++) {
        if (s[i] != '\n' && s[i] != '\r') {
            put(&out, s + i, 1);
            continue;
        }
        while (acount(out) && isspace((unsigned char)alast(out))) {
            asetlen(out, acount(out) - 1);
        }
        put_text(&out, " ");
        while (i + 1 < length && isspace((unsigned char)s[i + 1])) {
            i++;
        }
    }
    return take(&out);
}

static bool at_rule(const char *chunk, const char *name)
{
    size_t n = strlen(name);
    return chunk[0] == '@' && !strncmp(chunk + 1, name, n) &&
           (!chunk[n + 1] || isspace((unsigned char)chunk[n + 1]) ||
            chunk[n + 1] == '(' || chunk[n + 1] == '"' || chunk[n + 1] == '\'');
}

/* The selector list split at commas outside parentheses, brackets and quotes. */
static char **split(const char *list)
{
    char **parts = NULL;
    char quote = 0;
    int depth = 0;
    const char *start = list;
    for (const char *p = list;; p++) {
        if (quote) {
            quote = *p == quote ? 0 : quote;
            if (!*p) {
                break;
            }
            continue;
        }
        if (*p == '"' || *p == '\'') {
            quote = *p;
            continue;
        }
        depth += *p == '(' || *p == '['            ? 1
               : (*p == ')' || *p == ']') && depth ? -1
                                                   : 0;
        if (!*p || (*p == ',' && !depth)) {
            apush(parts, trimmed(start, (size_t)(p - start)));
            start = p + 1;
        }
        if (!*p) {
            break;
        }
    }
    return parts;
}

/* The selectors with `:root` as `body`, which is RmlUi's root element. */
static char *root_as_body(const char *selectors)
{
    char *out = NULL;
    for (const char *p = selectors; *p;) {
        bool root = !strncmp(p, ":root", 5) && !isalnum((unsigned char)p[5]) &&
                    p[5] != '-' && p[5] != '_';
        put_text(&out, root ? "body" : (char[]){ *p, 0 });
        p += root ? 5 : 1;
    }
    return take(&out);
}

/* Each inner selector under each enclosing one: `&` takes the enclosing
 * selector's place, and a selector without one descends from it. */
static char *combine(reader *r, const char *enclosing, const char *written)
{
    char *chunk = root_as_body(written);
    char **inner = split(chunk), **outer = enclosing ? split(enclosing) : NULL;
    free(chunk);
    char *out = NULL;
    bool ok = true;
    for (int o = 0; ok && o < (enclosing ? acount(outer) : 1); o++) {
        const char *parent = enclosing ? outer[o] : NULL;
        for (int i = 0; ok && i < acount(inner); i++) {
            const char *s = inner[i];
            ok = parent || !strchr(s, '&');
            if (acount(out)) {
                put_text(&out, ", ");
            }
            if (parent && !strchr(s, '&')) {
                put_text(&out, parent);
                put_text(&out, " ");
            }
            for (const char *p = s; ok && *p; p++) {
                if (*p == '&') {
                    put_text(&out, parent);
                } else {
                    put(&out, p, 1);
                }
            }
        }
    }
    free_strings(inner);
    free_strings(outer);
    if (!ok) {
        afree(out);
        fail(r, "& needs an enclosing rule");
        return NULL;
    }
    return take(&out);
}

/* Copies a block that passes through, braces and all, from its `{`. */
static bool copy_block(reader *r, const char *prelude, output *out)
{
    size_t start = r->at;
    int depth = 0;
    char quote = 0;
    for (; r->s[r->at]; r->at++) {
        char c = r->s[r->at];
        if (quote) {
            quote = c == quote ? 0 : quote;
            continue;
        }
        quote = c == '"' || c == '\'' ? c : 0;
        depth += c == '{' ? 1 : c == '}' ? -1 : 0;
        if (!depth) {
            rcss_origin at = origin(r, start);
            emit_text(out, &at, prelude);
            emit_text(out, &at, " ");
            emit(out, &at, r->s + start, r->at - start + 1);
            emit_text(out, &at, "\n");
            r->at++;
            return true;
        }
    }
    return fail(r, "%s is missing its }", prelude);
}

static bool expand_text(expansion *x, const char *text, int file, const char *directory, output *out);

/* Expands the file an @import on line `at` names in its place, once. */
static bool import(reader *r, const char *chunk, rcss_origin at, output *out)
{
    const char *open = strpbrk(chunk, "\"'");
    const char *close = open ? strchr(open + 1, *open) : NULL;
    if (!close) {
        return fail(r, "@import needs a quoted file name");
    }
    if (!r->directory) {
        emit_text(out, &at, chunk);
        emit_text(out, &at, ";\n");
        return true;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/%.*s", r->directory, (int)(close - open - 1), open + 1);
    char ***files = &r->x->files;
    for (int i = 0; i < acount(*files); i++) {
        if (!strcmp((*files)[i], path)) {
            return true;
        }
    }
    apush(*files, xstrdup(path));
    char *source = text_file_read(path);
    if (!source) {
        return fail(r, "cannot read %s", path);
    }
    char directory[1024];
    const char *slash = strrchr(path, '/');
    snprintf(directory, sizeof directory, "%.*s", (int)(slash - path), path);
    bool ok = expand_text(r->x, source, acount(*files) - 1, directory, out);
    free(source);
    return ok;
}

static bool expand_block(reader *r, const char *selectors, rcss_origin at, bool in_block, output *out);

/* Expands the rules of a nested @media block, from past its `{`, inside the
 * block. */
static bool expand_media(reader *r, const char *selectors, const char *prelude, rcss_origin at, output *out)
{
    output inner = { 0 };
    bool ok = expand_block(r, selectors, at, true, &inner);
    emit_text(out, &at, prelude);
    emit_text(out, &at, " {\n");
    emit_output(out, &inner);
    rcss_origin close = origin(r, r->at - 1);
    emit_text(out, &close, "}\n");
    output_free(&inner);
    return ok;
}

/* Expands a block's contents, whose rules sit under `selectors` (NULL at the
 * top level) written on line `at`, consuming its closing brace. */
static bool expand_block(reader *r, const char *selectors, rcss_origin at, bool in_block, output *out)
{
    char *declarations = NULL;
    output nested = { 0 };
    bool ok = true;
    for (;;) {
        while (isspace((unsigned char)r->s[r->at])) {
            r->at++;
        }
        if (!r->s[r->at] || r->s[r->at] == '}') {
            ok = r->s[r->at] ? in_block || fail(r, "unexpected }")
                             : !in_block || fail(r, "missing }");
            r->at += r->s[r->at] == '}';
            break;
        }
        size_t start = r->at;
        rcss_origin here = origin(r, start);
        char end = scan(r);
        char *chunk = trimmed(r->s + start, r->at - start);
        if (end == '{') {
            r->at++;
            if (at_rule(chunk, "media")) {
                ok = expand_media(r, selectors, chunk, here, &nested);
            } else if (chunk[0] == '@') {
                r->at--;
                ok = selectors ? fail(r, "%s belongs at the top level", chunk)
                               : copy_block(r, chunk, &nested);
            } else {
                char *combined = combine(r, selectors, chunk);
                ok = combined && expand_block(r, combined, here, true, &nested);
                free(combined);
            }
        } else if (at_rule(chunk, "import")) {
            ok = selectors ? fail(r, "@import belongs at the top level")
                           : import(r, chunk, here, &nested);
            r->at += end == ';';
        } else if (chunk[0]) {
            ok = selectors || fail(r, "%s needs a rule", chunk);
            put_text(&declarations, " ");
            put_text(&declarations, chunk);
            put_text(&declarations, ";");
            r->at += end == ';';
        } else {
            r->at += end == ';';
        }
        free(chunk);
        if (!ok) {
            break;
        }
    }
    if (ok && selectors && acount(declarations)) {
        emit_text(out, &at, selectors);
        emit_text(out, &at, " {");
        emit(out, &at, declarations, (size_t)acount(declarations));
        emit_text(out, &at, " }\n");
    }
    if (ok) {
        emit_output(out, &nested);
    }
    afree(declarations);
    output_free(&nested);
    return ok;
}

/* Expands `text`, file `file` of the expansion or -1 for the text expanded
 * itself, onto `out`. */
static bool expand_text(expansion *x, const char *text, int file, const char *directory, output *out)
{
    bool closed;
    char *source = strip_comments(text, &closed);
    const char *path = file < 0 ? "" : x->files[file];
    const char *slash = strrchr(path, '/');
    reader r = { .x = x,
                 .s = source,
                 .file = file,
                 .name = slash ? slash + 1 : path,
                 .directory = directory,
                 .line = 1 };
    bool ok = closed ? expand_block(&r, NULL, origin(&r, 0), false, out)
                     : fail(&r, "a comment is missing its */");
    free(source);
    return ok;
}

char *rcss_expand(const char *text, const char *directory, rcss_map *map, char *error, size_t error_size)
{
    assert(!map || (!map->files && !map->lines));
    expansion x = { .error = error, .error_size = error_size };
    output out = { 0 };
    bool ok = expand_text(&x, text, -1, directory, &out);
    char *flat = ok ? take(&out.text) : NULL;
    if (!map) {
        free_strings(x.files);
    } else {
        map->files = x.files;
    }
    if (map && ok) {
        map->lines = out.lines;
        out.lines = NULL;
    }
    output_free(&out);
    return flat;
}

/* ---------- Custom properties ---------- */

/* How deeply rules nest before rcss_variables stops naming their selectors. */
#define VARIABLE_DEPTH 16

/* Past the comment starting at `p`, or `p` when none does. */
static const char *skip_comment(const char *p, int parens)
{
    if (p[0] == '/' && p[1] == '*') {
        const char *end = strstr(p + 2, "*/");
        return end ? end + 2 : p + strlen(p);
    }
    if (p[0] == '/' && p[1] == '/' && !parens) {
        const char *end = strchr(p, '\n');
        return end ? end : p + strlen(p);
    }
    return p;
}

static bool blank(const char *start, const char *end)
{
    for (; start < end; start++) {
        if (!isspace((unsigned char)*start)) {
            return false;
        }
    }
    return true;
}

/* The selector in [start, end), trimmed, with each run of spaces as one. */
static void collapse(char *out, size_t size, const char *start, const char *end)
{
    size_t n = 0;
    bool space = false;
    for (; start < end && n + 1 < size; start++) {
        if (isspace((unsigned char)*start)) {
            space = n > 0;
            continue;
        }
        if (space && n + 2 < size) {
            out[n++] = ' ';
        }
        space = false;
        out[n++] = *start;
    }
    out[n] = '\0';
}

/* Adds the declaration in [start, end) when it sets a custom property. */
static void add_variable(
    const char *text,
    const char *start,
    const char *end,
    const char *selector,
    rcss_variable **out
)
{
    while (start < end && isspace((unsigned char)*start)) {
        start++;
    }
    const char *colon = memchr(start, ':', (size_t)(end - start));
    if (end - start < 3 || strncmp(start, "--", 2) != 0 || !colon) {
        return;
    }
    const char *name_end = colon, *value = colon + 1;
    while (name_end > start && isspace((unsigned char)name_end[-1])) {
        name_end--;
    }
    while (value < end && isspace((unsigned char)*value)) {
        value++;
    }
    while (end > value && isspace((unsigned char)end[-1])) {
        end--;
    }
    rcss_variable v = { .start = (int)(value - text), .end = (int)(end - text) };
    snprintf(v.selector, sizeof v.selector, "%s", selector);
    snprintf(v.name, sizeof v.name, "%.*s", (int)(name_end - start), start);
    apush(*out, v);
}

void rcss_variables(const char *text, rcss_variable **variables)
{
    char selectors[VARIABLE_DEPTH][128];
    int depth = 0, parens = 0;
    char quote = 0;
    const char *segment = text;
    for (const char *p = text; *p; p++) {
        if (quote) {
            p += *p == '\\' && p[1];
            quote = *p == quote ? 0 : quote;
            continue;
        }
        const char *after = skip_comment(p, parens);
        if (after != p) {
            segment = blank(segment, p) ? after : segment;
            p = after - 1;
            continue;
        }
        if (*p == '"' || *p == '\'') {
            quote = *p;
            continue;
        }
        parens += *p == '(' ? 1 : *p == ')' && parens ? -1 : 0;
        if (parens) {
            continue;
        }
        if (*p == '{') {
            if (depth < VARIABLE_DEPTH) {
                collapse(selectors[depth], sizeof selectors[depth], segment, p);
            }
            depth++;
            segment = p + 1;
            continue;
        }
        if (*p != ';' && *p != '}') {
            continue;
        }
        if (depth > 0 && depth <= VARIABLE_DEPTH) {
            add_variable(text, segment, p, selectors[depth - 1], variables);
        }
        depth -= *p == '}' && depth > 0;
        segment = p + 1;
    }
}
