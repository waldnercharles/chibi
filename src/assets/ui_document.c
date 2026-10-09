#include "assets/ui_document.h"

#include "support/array.h"
#include "support/rcss.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The asset holds the body; composition supplies the document around it. */
static const char *const DOCUMENT_TAGS[] = { "<rml", "<head", "<body" };

static const char *tag_end(const char *p);

/* Whether `rml` opens the element `tag` ("<head"), by its whole name, so a
   <header> is not a <head>. */
static bool holds_tag(const char *rml, const char *tag)
{
    size_t length = strlen(tag);
    for (const char *at = strstr(rml, tag); at; at = strstr(at + 1, tag)) {
        char next = at[length];
        if (next == '>' || next == '/' || isspace((unsigned char)next)) {
            return true;
        }
    }
    return false;
}

/* The line breaks in [p, end). */
static int breaks(const char *p, const char *end)
{
    int count = 0;
    for (; p < end; p++) {
        count += *p == '\n';
    }
    return count;
}

static bool fail(char *error, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
    return false;
}

/* Splits markup wrapped in <body ...>...</body> into the body's attributes
 * and its content; other markup is all content. */
static bool split_body(const char *rml, ui_document *out, char *error, size_t error_size)
{
    const char *start = rml + strspn(rml, " \t\r\n");
    bool wrapped = !strncmp(start, "<body", 5) &&
                   (isspace((unsigned char)start[5]) || start[5] == '>' ||
                    start[5] == '/');
    const char *open_end = wrapped ? tag_end(start) : NULL;
    if (!wrapped) {
        out->rml = xstrdup(rml);
        out->rml_line = 1;
        return true;
    }
    if (!open_end) {
        return fail(error, error_size, "rml: <body> needs a >");
    }
    bool empty = open_end[-2] == '/';
    const char *attrs = start + 5, *attrs_end = open_end - (empty ? 2 : 1);
    const char *inner = open_end, *inner_end = open_end;
    if (!empty) {
        inner_end = strstr(inner, "</body>");
        const char *after = inner_end ? inner_end + 7 : NULL;
        if (!after || after[strspn(after, " \t\r\n")]) {
            return fail(error, error_size, "rml: <body> must hold all the markup and end with </body>");
        }
    }
    attrs += strspn(attrs, " \t\r\n");
    out->body = xmalloc((size_t)(attrs_end - attrs) + 1);
    snprintf(out->body, (size_t)(attrs_end - attrs) + 1, "%s", attrs);
    out->rml = xmalloc((size_t)(inner_end - inner) + 1);
    snprintf(out->rml, (size_t)(inner_end - inner) + 1, "%s", inner);
    out->rml_line = 1 + breaks(rml, inner);
    return true;
}

static bool read_parts(
    const char *rml,
    const char *rcss,
    const char *directory,
    char ***imports,
    ui_document *out,
    char *error,
    size_t error_size
)
{
    if (!rml[strspn(rml, " \t\r\n")]) {
        return fail(error, error_size, "rml must be nonempty markup");
    }
    if (strstr(rcss, "</style")) {
        return fail(error, error_size, "rcss must not close its style block");
    }
    if (!split_body(rml, out, error, error_size)) {
        return false;
    }
    for (size_t i = 0; i < sizeof DOCUMENT_TAGS / sizeof *DOCUMENT_TAGS; i++) {
        if (holds_tag(out->rml, DOCUMENT_TAGS[i])) {
            return fail(error, error_size, "rml holds body markup; remove %s>", DOCUMENT_TAGS[i]);
        }
    }
    char message[200];
    char *flat = rcss_expand(rcss, directory, &out->map, message, sizeof message);
    for (int i = 0; imports && i < acount(out->map.files); i++) {
        apush(*imports, xstrdup(out->map.files[i]));
    }
    if (!flat) {
        return fail(error, error_size, "rcss %s", message);
    }
    out->rcss = flat;
    return true;
}

bool ui_document_read(
    const char *rml,
    const char *rcss,
    const char *directory,
    char ***imports,
    ui_document *out,
    char *error,
    size_t error_size
)
{
    assert(rml && out);
    ui_document next = { 0 };
    if (!read_parts(rml, rcss ? rcss : "", directory, imports, &next, error, error_size)) {
        ui_document_free(&next);
        return false;
    }
    ui_document_free(out);
    *out = next;
    return true;
}

char *ui_document_path(const char *ui_root, const char *name, bool style)
{
    const char *extension = style ? UI_STYLE_EXTENSION : UI_MARKUP_EXTENSION;
    size_t size = strlen(ui_root) + strlen(name) + strlen(extension) + 2;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s%s", ui_root, name, extension);
    return path;
}

bool ui_document_load(
    const char *ui_root,
    const char *name,
    char ***imports,
    ui_document *out,
    char *error,
    size_t error_size
)
{
    char *rml_path = ui_document_path(ui_root, name, false);
    char *rcss_path = ui_document_path(ui_root, name, true);
    char *rml = text_file_read(rml_path), *rcss = text_file_read(rcss_path);
    bool ok = rml ? ui_document_read(rml, rcss, ui_root, imports, out, error, error_size)
                  : fail(error, error_size, "is missing");
    free(rcss);
    free(rml);
    free(rcss_path);
    free(rml_path);
    return ok;
}

/* ---------- Widgets ---------- */

/* How deeply widgets may nest inside one another. */
#define UI_USE_DEPTH 8

/* One <use> element in markup. */
typedef struct use_tag
{
    const char *open, *close; /* its first character and one past its end */
    const char *attrs, *attrs_end; /* the open tag's attributes */
    const char *ui, *ui_end;       /* the ui attribute within them */
    const char *inner, *inner_end; /* its children, empty when self-closing */
    char name[128];
} use_tag;

typedef enum
{
    USE_NONE,
    USE_FOUND,
    USE_BROKEN,
} use_scan;

typedef struct expansion
{
    const char *ui_root;
    char **names;         /* every widget looked up */
    ui_document *widgets; /* parallel to names; empty until read */
    const char *stack[UI_USE_DEPTH];
    int depth;
    bool sources; /* mark each element with where it was written */
    char *error;
    size_t error_size;
    char *rcss; /* the scoped RCSS joined so far (rcss_join) */
    rcss_map map;
} expansion;

static void put(char **out, const char *text, size_t length)
{
    if (!length) {
        return;
    }
    int start = acount(*out);
    asetlen(*out, start + (int)length);
    memcpy(*out + start, text, length);
}

static const char *find_use_open(const char *p)
{
    for (; (p = strstr(p, "<use")); p += 4) {
        if (isspace((unsigned char)p[4]) || p[4] == '/' || p[4] == '>') {
            return p;
        }
    }
    return NULL;
}

/* One past the '>' closing the tag at `p`, skipping quoted values; NULL when
 * unterminated. */
static const char *tag_end(const char *p)
{
    char quote = 0;
    for (; *p; p++) {
        if (quote) {
            quote = *p == quote ? 0 : quote;
            continue;
        }
        if (*p == '"' || *p == '\'') {
            quote = *p;
            continue;
        }
        if (*p == '>') {
            return p + 1;
        }
    }
    return NULL;
}

/* The </use> that closes an element whose children start at `p`. */
static const char *matching_close(const char *p)
{
    for (int depth = 1;;) {
        const char *open = find_use_open(p);
        const char *close = strstr(p, "</use>");
        if (!close) {
            return NULL;
        }
        if (open && open < close) {
            const char *end = tag_end(open);
            if (!end) {
                return NULL;
            }
            depth += end[-2] != '/';
            p = end;
            continue;
        }
        if (--depth == 0) {
            return close;
        }
        p = close + 6;
    }
}

static bool read_use_name(use_tag *tag)
{
    for (const char *p = tag->attrs; p + 4 < tag->attrs_end; p++) {
        if (!isspace((unsigned char)p[0]) || strncmp(p + 1, "ui=", 3) != 0) {
            continue;
        }
        char quote = p[4];
        const char *name = p + 5;
        const char *end = quote == '"' || quote == '\'' ? strchr(name, quote) : NULL;
        size_t length = end ? (size_t)(end - name) : 0;
        if (!length || length >= sizeof tag->name ||
            memchr(name, '/', length) || memchr(name, '"', length)) {
            return false;
        }
        memcpy(tag->name, name, length);
        tag->name[length] = '\0';
        tag->ui = p + 1;
        tag->ui_end = end + 1;
        return true;
    }
    return false;
}

static use_scan next_use(const char *text, use_tag *tag)
{
    const char *open = find_use_open(text);
    if (!open) {
        return USE_NONE;
    }
    const char *end = tag_end(open);
    if (!end) {
        return USE_BROKEN;
    }
    bool empty = end[-2] == '/';
    *tag = (use_tag){ .open = open,
                      .close = end,
                      .attrs = open + 4,
                      .attrs_end = end - (empty ? 2 : 1),
                      .inner = end,
                      .inner_end = end };
    if (!empty) {
        tag->inner_end = matching_close(end);
        if (!tag->inner_end) {
            return USE_BROKEN;
        }
        tag->close = tag->inner_end + 6;
    }
    return read_use_name(tag) ? USE_FOUND : USE_BROKEN;
}

/* The widget asset, read once per expansion; NULL with the error set. */
static const ui_document *widget_named(expansion *x, const char *name)
{
    for (int i = 0; i < acount(x->names); i++) {
        if (!strcmp(x->names[i], name)) {
            return &x->widgets[i];
        }
    }
    apush(x->names, xstrdup(name));
    apush(x->widgets, (ui_document){ 0 });
    char why[160];
    bool read = ui_document_load(x->ui_root, name, NULL, &alast(x->widgets), why, sizeof why);
    if (read && alast(x->widgets).body) {
        snprintf(why, sizeof why, "has a <body>, which only a scene's root document has");
        read = false;
    }
    if (!read) {
        fail(x->error, x->error_size, "widget %s %s", name, why);
        return NULL;
    }
    ui_document *widget = &alast(x->widgets);
    if (x->sources) {
        char *marked = ui_document_sources(widget->rml, name, widget->rml_line);
        free(widget->rml);
        widget->rml = marked;
    }
    return widget;
}

static bool expand(expansion *x, const char *rml, char **out);

/* Writes the widget's markup, with `inner` in its slot. */
static void put_filled(char **out, const char *body, const char *inner)
{
    const char *slot = strstr(body, "<slot/>");
    size_t slot_length = 7;
    if (!slot) {
        slot = strstr(body, "<slot />");
        slot_length = 8;
    }
    if (!slot) {
        put(out, body, strlen(body));
        return;
    }
    put(out, body, (size_t)(slot - body));
    put(out, inner, strlen(inner));
    put(out, slot + slot_length, strlen(slot + slot_length));
}

/* The widget element's attrs attribute, as Vue's v-bind="$attrs"; NULL
 * when the widget marks none. */
static const char *attrs_marker(const char *body)
{
    for (const char *p = body; (p = strstr(p, " attrs")); p += 6) {
        if (isspace((unsigned char)p[6]) || p[6] == '/' || p[6] == '>') {
            return p;
        }
    }
    return NULL;
}

/* Writes the attributes in [start, end) after one space, trimmed; nothing
 * when there are none. */
static void put_attribute_text(char **out, const char *start, const char *end)
{
    while (start < end && isspace((unsigned char)*start)) {
        start++;
    }
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    if (start < end) {
        put(out, " ", 1);
        put(out, start, (size_t)(end - start));
    }
}

/* Past the data-src ui_document_sources puts first in a tag's attributes,
 * or `attrs` when there is none. */
static const char *past_source(const char *attrs)
{
    static const char SOURCE[] = " data-src=\"";
    const char *close = strncmp(attrs, SOURCE, sizeof SOURCE - 1)
                          ? NULL
                          : strchr(attrs + sizeof SOURCE - 1, '"');
    return close ? close + 1 : attrs;
}

/* The widget's markup with the use element's attributes, but ui and data-src,
 * in place of its attrs marker; NULL when it has none. */
static char *put_attrs(const char *body, const use_tag *tag)
{
    const char *marker = attrs_marker(body);
    if (!marker) {
        return NULL;
    }
    char *out = NULL;
    put(&out, body, (size_t)(marker - body));
    put_attribute_text(&out, past_source(tag->attrs), tag->ui);
    put_attribute_text(&out, tag->ui_end, tag->attrs_end);
    put(&out, marker + 6, strlen(marker + 6) + 1);
    return out;
}

static bool expand_use(expansion *x, const use_tag *tag, char **out)
{
    for (int i = 0; i < x->depth; i++) {
        if (!strcmp(x->stack[i], tag->name)) {
            return fail(x->error, x->error_size, "widget %s uses itself", tag->name);
        }
    }
    if (x->depth == UI_USE_DEPTH) {
        return fail(x->error, x->error_size, "widgets nest deeper than %d", UI_USE_DEPTH);
    }
    const ui_document *widget = widget_named(x, tag->name);
    if (!widget) {
        return false;
    }
    const char *widget_rml = widget->rml; /* the array may move while expanding */
    size_t inner_length = (size_t)(tag->inner_end - tag->inner);
    char *children = xmalloc(inner_length + 1);
    memcpy(children, tag->inner, inner_length);
    children[inner_length] = '\0';
    char *inner = NULL, *body = NULL;
    bool ok = expand(x, children, &inner);
    x->stack[x->depth++] = tag->name;
    ok = ok && expand(x, widget_rml, &body);
    x->depth--;
    if (ok) {
        /* A widget element marked attrs takes the use element's attributes
           but data-src, which stays on the div. */
        char *marked = put_attrs(body, tag);
        const char *kept = marked ? past_source(tag->attrs) : tag->ui;
        put(out, "<div", 4);
        put(out, tag->attrs, (size_t)(kept - tag->attrs));
        if (marked) {
            put(out, " ", 1);
        }
        put(out, "data-", 5);
        put(out, tag->ui, (size_t)((marked ? tag->ui_end : tag->attrs_end) - tag->ui));
        put(out, ">", 1);
        put_filled(out, marked ? marked : body, inner);
        put(out, "</div>", 6);
        afree(marked);
    }
    free(children);
    afree(inner);
    afree(body);
    return ok;
}

/* Appends the expanded markup to `out`, NUL-terminated. */
static bool expand(expansion *x, const char *rml, char **out)
{
    use_tag tag;
    use_scan scan;
    const char *p = rml;
    while ((scan = next_use(p, &tag)) == USE_FOUND) {
        put(out, p, (size_t)(tag.open - p));
        if (!expand_use(x, &tag, out)) {
            return false;
        }
        p = tag.close;
    }
    if (scan == USE_BROKEN) {
        return fail(x->error, x->error_size, "each <use> needs ui=\"Name\" and an end");
    }
    put(out, p, strlen(p) + 1);
    return true;
}

/* ---------- Scoping ---------- */

/* Copies the comment or whitespace at `p`; returns past it, or `p`. */
static const char *skip_space(const char *p, char **out)
{
    for (;;) {
        if (isspace((unsigned char)*p)) {
            put(out, p++, 1);
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            const char *end = strstr(p + 2, "*/");
            end = end ? end + 2 : p + strlen(p);
            put(out, p, (size_t)(end - p));
            p = end;
            continue;
        }
        return p;
    }
}

/* One past the '}' that closes the block whose '{' is at `p`. */
static const char *block_end(const char *p)
{
    int depth = 0;
    for (; *p; p++) {
        depth += *p == '{';
        depth -= *p == '}';
        if (!depth) {
            return p + 1;
        }
    }
    return p;
}

static bool name_char(char c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '_';
}

/* The length of the class qualifiers `.name` and `:not(.name, ...)` that
 * start `s`, and whether one is a :not; zero when a negation holds anything
 * but classes. */
static size_t class_qualifiers(const char *s, size_t length, bool *negated)
{
    size_t at = 0;
    while (at < length) {
        if (s[at] == '.') {
            for (at++; at < length && name_char(s[at]); at++) {
            }
            continue;
        }
        if (strncmp(s + at, ":not(", 5) != 0) {
            break;
        }
        size_t close = at + 5;
        while (close < length && (name_char(s[close]) || strchr(". ,", s[close]))) {
            close++;
        }
        if (close >= length || s[close] != ')') {
            return 0;
        }
        *negated = true;
        at = close + 1;
    }
    return at;
}

/* Whether `rest` is nothing, or a descendant combinator and what follows. */
static bool descends(const char *rest, size_t length)
{
    size_t space = 0;
    while (space < length && isspace((unsigned char)rest[space])) {
        space++;
    }
    return space == length || (space > 0 && !strchr(">+~", rest[space]));
}

/* Writes `s` on one line, so that a second copy adds no line to the map. */
static void put_flat(char **out, const char *s, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        put(out, s[i] == '\n' ? " " : s + i, 1);
    }
}

/* Writes one selector matching inside the scope. `body` names the scoped
 * element itself, and the classes after it name the page's body too, which
 * the scoped element does not carry: `body.dark .row` matches inside the
 * scope when the element or the page's body has `dark`, and
 * `body:not(.dark) .row` when the page's body lacks it. Any other qualifier
 * or combinator after `body` asks only the element. */
static void put_selector(char **out, const char *scope, const char *s, size_t length)
{
    while (length && isspace((unsigned char)*s)) {
        s++;
        length--;
    }
    while (length && isspace((unsigned char)s[length - 1])) {
        length--;
    }
    bool body = length >= 4 && !strncmp(s, "body", 4) &&
                (length == 4 || !name_char(s[4]));
    if (!body) {
        put(out, scope, strlen(scope));
        put(out, " ", 1);
        put(out, s, length);
        return;
    }
    bool negated = false;
    size_t classes = class_qualifiers(s + 4, length - 4, &negated);
    const char *rest = s + 4 + classes;
    size_t rest_length = length - 4 - classes;
    bool page = classes && descends(rest, rest_length);
    if (!page || !negated) {
        put(out, scope, strlen(scope));
        put(out, s + 4, length - 4);
    }
    if (!page) {
        return;
    }
    if (!negated) {
        put(out, ", ", 2);
    }
    put(out, "body", 4);
    put(out, s + 4, classes);
    put(out, " ", 1);
    put(out, scope, strlen(scope));
    if (negated) {
        put(out, rest, rest_length);
        return;
    }
    put_flat(out, rest, rest_length);
}

/* Writes the selector list [p, end) with each selector scoped. */
static void put_selectors(char **out, const char *scope, const char *p, const char *end)
{
    int parens = 0;
    const char *start = p;
    for (; p < end; p++) {
        parens += (*p == '(') - (*p == ')');
        if (*p == ',' && !parens) {
            put_selector(out, scope, start, (size_t)(p - start));
            put(out, ", ", 2);
            start = p + 1;
        }
    }
    put_selector(out, scope, start, (size_t)(end - start));
}

/* Scopes the rules from `p` until a '}' at this level or the end; returns
 * where it stopped. @media blocks are scoped too; other at-rules are kept. */
static const char *scope_rules(const char *p, const char *scope, char **out)
{
    for (;;) {
        p = skip_space(p, out);
        if (!*p || *p == '}') {
            return p;
        }
        const char *open = strchr(p, '{');
        const char *stop = strchr(p, ';');
        if (*p == '@' && stop && (!open || stop < open)) {
            put(out, p, (size_t)(stop + 1 - p));
            p = stop + 1;
            continue;
        }
        if (!open) {
            put(out, p, strlen(p));
            return p + strlen(p);
        }
        if (*p == '@' && strncmp(p, "@media", 6) != 0) {
            const char *end = block_end(open);
            put(out, p, (size_t)(end - p));
            p = end;
            continue;
        }
        if (*p == '@') {
            put(out, p, (size_t)(open + 1 - p));
            p = scope_rules(open + 1, scope, out);
            if (*p == '}') {
                put(out, "}", 1);
                p++;
            }
            continue;
        }
        put_selectors(out, scope, p, open);
        put(out, " ", 1);
        const char *end = block_end(open);
        put(out, open, (size_t)(end - open));
        p = end;
    }
}

char *ui_document_scope(const char *rcss, const char *name)
{
    char scope[160];
    snprintf(scope, sizeof scope, "[data-ui=\"%s\"]", name);
    char *out = NULL;
    const char *p = rcss;
    while (*(p = scope_rules(p, scope, &out)) == '}') {
        p++; /* a stray close brace ends nothing */
    }
    put(&out, "", 1);
    char *text = xstrdup(out);
    afree(out);
    return text;
}

/* ---------- Sources ---------- */

/* Whether the tag opening at `p` is a `name` element. */
static bool tag_named(const char *p, const char *name)
{
    size_t length = strlen(name);
    return !strncmp(p + 1, name, length) && !name_char(p[1 + length]);
}

/* Past the markup opening at `p`: a comment, or a start tag, whose quoted
 * values may hold '<'; else past the '<'. NULL when it is unterminated. */
static const char *markup_end(const char *p)
{
    if (!strncmp(p, "<!--", 4)) {
        const char *end = strstr(p, "-->");
        return end ? end + 3 : NULL;
    }
    return isalpha((unsigned char)p[1]) ? tag_end(p) : p + 1;
}

static void put_source(char **out, const char *asset, int line)
{
    char mark[320];
    int length = snprintf(mark, sizeof mark, " data-src=\"%s:%d\"", asset, line);
    assert(length > 0 && (size_t)length < sizeof mark);
    put(out, mark, (size_t)length);
}

char *ui_document_sources(const char *rml, const char *asset, int first)
{
    char *out = NULL;
    const char *copied = rml;
    int line = first;
    for (const char *p = strchr(rml, '<'); p; p = strchr(p, '<')) {
        const char *end = markup_end(p);
        if (!end) {
            break;
        }
        if (isalpha((unsigned char)p[1]) && !tag_named(p, "slot")) {
            const char *name_end = p + 1;
            while (name_char(*name_end)) {
                name_end++;
            }
            line += breaks(copied, p);
            put(&out, copied, (size_t)(name_end - copied));
            put_source(&out, asset, line);
            copied = name_end;
        }
        p = end;
    }
    put(&out, copied, strlen(copied) + 1);
    char *text = xstrdup(out);
    afree(out);
    return text;
}

/* Appends asset `name`'s RCSS, scoped to it, to the expansion's, naming its
 * own lines its file's. */
static void put_scoped(expansion *x, ui_document *asset, const char *name)
{
    if (!asset->rcss[0]) {
        return;
    }
    char *path = ui_document_path(x->ui_root, name, true);
    rcss_map_own(&asset->map, path);
    free(path);
    char *scoped = ui_document_scope(asset->rcss, name);
    assert(
        breaks(scoped, scoped + strlen(scoped)) ==
        breaks(asset->rcss, asset->rcss + strlen(asset->rcss))
    ); /* so the map holds */
    rcss_join(&x->rcss, &x->map, scoped, &asset->map);
    free(scoped);
}

bool ui_document_expand(
    ui_document *document,
    const char *name,
    const char *ui_root,
    bool sources,
    char ***uses,
    char *error,
    size_t error_size
)
{
    expansion x = { .ui_root = ui_root,
                    .sources = sources,
                    .error = error,
                    .error_size = error_size };
    char *marked = sources
                     ? ui_document_sources(document->rml, name, document->rml_line)
                     : NULL;
    char *rml = NULL;
    bool ok = expand(&x, marked ? marked : document->rml, &rml);
    free(marked);
    if (ok) {
        for (int i = 0; i < acount(x.widgets); i++) {
            put_scoped(&x, &x.widgets[i], x.names[i]);
        }
        put_scoped(&x, document, name);
        apush(x.rcss, '\0');
        ui_document_free(document);
        *document = (ui_document){ .rml = xstrdup(rml),
                                   .rcss = xstrdup(x.rcss),
                                   .map = x.map };
        x.map = (rcss_map){ 0 };
    }
    afree(x.rcss);
    afree(rml);
    for (int i = 0; i < acount(x.names); i++) {
        apush(*uses, x.names[i]);
        ui_document_free(&x.widgets[i]);
    }
    afree(x.names);
    afree(x.widgets);
    return ok;
}

void ui_document_use_names(const char *rml, char ***names)
{
    use_tag tag;
    for (const char *p = rml; next_use(p, &tag) == USE_FOUND; p = tag.inner) {
        apush(*names, xstrdup(tag.name));
    }
}

/* ---------- Events ---------- */

/* The quoted value of attribute `name` in the tag's attributes [p, end),
 * with its length; NULL when the tag lacks it. */
static const char *attribute_span(const char *p, const char *end, const char *name, size_t *size)
{
    size_t length = strlen(name);
    for (char quoted = 0; p < end; p++) {
        if (quoted) {
            quoted = *p == quoted ? 0 : quoted;
            continue;
        }
        if (*p == '"' || *p == '\'') {
            quoted = *p;
            continue;
        }
        if (!isspace((unsigned char)*p) || (size_t)(end - p) < length + 3 ||
            strncmp(p + 1, name, length) || p[length + 1] != '=') {
            continue;
        }
        char quote = p[length + 2];
        const char *value = p + length + 3;
        const char *close = quote == '"' || quote == '\''
                              ? memchr(value, quote, (size_t)(end - value))
                              : NULL;
        *size = close ? (size_t)(close - value) : 0;
        return close ? value : NULL;
    }
    return NULL;
}

/* Copies the quoted value of attribute `name` in the tag's attributes
 * [p, end) into `out`; false when the tag lacks it. */
static bool tag_attribute(const char *p, const char *end, const char *name, char *out, size_t size)
{
    size_t length;
    const char *value = attribute_span(p, end, name, &length);
    if (value) {
        snprintf(out, size, "%.*s", (int)length, value);
    }
    return value != NULL;
}

/* How an event names its element: "#id", else "tag.class" with the first
 * class, else the tag, where a <use>'s tag is its widget. */
static void element_label(const char *tag, const char *attrs, const char *end, char *out, size_t size)
{
    char id[128], widget[128], classes[256];
    if (tag_attribute(attrs, end, "id", id, sizeof id) && id[0]) {
        snprintf(out, size, "#%s", id);
        return;
    }
    if (!strcmp(tag, "use") && tag_attribute(attrs, end, "ui", widget, sizeof widget)) {
        tag = widget;
    }
    if (!tag_attribute(attrs, end, "class", classes, sizeof classes) || !classes[0]) {
        snprintf(out, size, "%s", tag);
        return;
    }
    snprintf(out, size, "%s.%.*s", tag, (int)strcspn(classes, " "), classes);
}

static char *copy_text(const char *text, size_t length)
{
    char *copy = xmalloc(length + 1);
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

/* The event among events[first...] named `name`, or -1. */
static int named_event(const ui_document_event *events, int first, const char *name, size_t length)
{
    for (int i = first; i < acount(events); i++) {
        if (strlen(events[i].name) == length && !strncmp(events[i].name, name, length)) {
            return i;
        }
    }
    return -1;
}

/* Appends the events one "Key:Event Key:Event" binding list names; keys
 * bound to the same event join as "Enter or Space". */
static void push_key_events(ui_document_event **events, const char *bindings, const char *element)
{
    int first = acount(*events);
    for (const char *p = bindings; *p;) {
        p += strspn(p, " ");
        const char *binding = p;
        p += strcspn(p, " ");
        const char *colon = memchr(binding, ':', (size_t)(p - binding));
        if (!colon || colon == binding || colon + 1 == p) {
            continue;
        }
        char key[64];
        snprintf(key, sizeof key, "%.*s", (int)(colon - binding), binding);
        size_t length = (size_t)(p - colon - 1);
        int bound = named_event(*events, first, colon + 1, length);
        if (bound < 0) {
            apush(
                *events,
                ((ui_document_event){ .name = copy_text(colon + 1, length),
                                      .element = xstrdup(element),
                                      .key = xstrdup(key) })
            );
            continue;
        }
        char joined[256];
        snprintf(joined, sizeof joined, "%s or %s", (*events)[bound].key, key);
        free((*events)[bound].key);
        (*events)[bound].key = xstrdup(joined);
    }
}

/* An event a data-event-* handler sends: the Name of each emit('Name'). */
typedef struct
{
    const char *at;
    size_t length;
    bool change; /* the handler is data-event-change or data-event-input */
} emit_span;

/* Appends the emit('Name') calls of the data-event-* handlers among the
 * tag's attributes [p, end), in order. */
static void emit_spans(const char *p, const char *end, emit_span **spans)
{
    static const char PREFIX[] = "data-event-";
    const size_t prefix = sizeof PREFIX - 1;
    for (char quoted = 0; p < end; p++) {
        if (quoted) {
            quoted = *p == quoted ? 0 : quoted;
            continue;
        }
        if (*p == '"' || *p == '\'') {
            quoted = *p;
            continue;
        }
        if (!isspace((unsigned char)*p) || (size_t)(end - p) <= prefix + 1 ||
            strncmp(p + 1, PREFIX, prefix)) {
            continue;
        }
        const char *type = p + 1 + prefix, *equals = type;
        while (equals < end && name_char(*equals)) {
            equals++;
        }
        if (end - equals < 2 || *equals != '=' ||
            (equals[1] != '"' && equals[1] != '\'')) {
            continue;
        }
        const char *value = equals + 2;
        const char *close = memchr(value, equals[1], (size_t)(end - value));
        if (!close) {
            return;
        }
        bool change = (equals - type == 6 && !strncmp(type, "change", 6)) ||
                      (equals - type == 5 && !strncmp(type, "input", 5));
        for (const char *e = value; e + 6 < close; e++) {
            if (strncmp(e, "emit(", 5) || (e[5] != '"' && e[5] != '\'')) {
                continue;
            }
            const char *name = e + 6;
            const char *stop = memchr(name, e[5], (size_t)(close - name));
            if (stop && stop > name) {
                apush(*spans, ((emit_span){ name, (size_t)(stop - name), change }));
                e = stop;
            }
        }
        p = close;
    }
}

/* Appends the events of the tag opening at `open` and ending before `end`. */
static void push_tag_events(ui_document_event **events, const char *open, const char *end)
{
    char tag[64], element[256], value[256];
    int length = 0;
    while (name_char(open[1 + length])) {
        length++;
    }
    snprintf(tag, sizeof tag, "%.*s", length, open + 1);
    const char *attrs = open + 1 + length;
    element_label(tag, attrs, end, element, sizeof element);
    emit_span *emits = NULL;
    emit_spans(attrs, end, &emits);
    for (int i = 0; i < acount(emits); i++) {
        apush(
            *events,
            ((ui_document_event){ .name = copy_text(emits[i].at, emits[i].length),
                                  .element = xstrdup(element),
                                  .input = emits[i].change })
        );
    }
    afree(emits);
    if (tag_attribute(attrs, end, "data-keys", value, sizeof value)) {
        push_key_events(events, value, element);
    }
    if (tag_attribute(attrs, end, "data-area-keys", value, sizeof value)) {
        push_key_events(events, value, element);
    }
}

void ui_document_events(const char *rml, ui_document_event **events)
{
    for (const char *p = strchr(rml, '<'); p; p = strchr(p, '<')) {
        const char *end = strncmp(p, "<!--", 4) ? tag_end(p) : strstr(p, "-->");
        if (!end) {
            return;
        }
        if (isalpha((unsigned char)p[1])) {
            push_tag_events(events, p, end);
        }
        p = end;
    }
}

void ui_document_events_free(ui_document_event *events)
{
    for (int i = 0; i < acount(events); i++) {
        free(events[i].name);
        free(events[i].element);
        free(events[i].key);
    }
    afree(events);
}

/* ---------- Bindings ---------- */

static bool identifier_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/* The binding named `name`, added in order as needed. */
static ui_document_binding *binding(ui_document_binding **bindings, const char *name, size_t length)
{
    for (int i = 0; i < acount(*bindings); i++) {
        if (strlen((*bindings)[i].name) == length &&
            !strncmp((*bindings)[i].name, name, length)) {
            return &(*bindings)[i];
        }
    }
    ui_document_binding added = { 0 };
    snprintf(added.name, sizeof added.name, "%.*s", (int)length, name);
    apush(*bindings, added);
    return &alast(*bindings);
}

/* Each <prefix>.<word> from `text` on, as `name` of `length`; NULL at the
   end. A prefix inside a longer word or path does not count. */
static const char *next_member(const char *text, const char *at, const char *prefix, size_t *length)
{
    size_t size = strlen(prefix);
    for (const char *p = at; (p = strstr(p, prefix)); p += size) {
        bool whole = (p == text || (!identifier_char(p[-1]) && p[-1] != '.')) &&
                     p[size] == '.';
        size_t n = 0;
        while (whole && identifier_char(p[size + 1 + n])) {
            n++;
        }
        if (n) {
            *length = n;
            return p + size + 1;
        }
    }
    return NULL;
}

/* A data-for="item : self.items" at the tag opening at `p` makes items a
   list whose rows bind the item.<field>s after it. */
static void list_binding(ui_document_binding **bindings, const char *rml, const char *p, const char *end)
{
    char value[128];
    if (!tag_attribute(p, end, "data-for", value, sizeof value)) {
        return;
    }
    char *colon = strchr(value, ':');
    size_t iterator = strspn(value, " ");
    size_t iterator_length = strcspn(value + iterator, " ,:");
    const char *expression = colon ? colon + 1 + strspn(colon + 1, " ") : "";
    if (strncmp(expression, "self.", 5) || !iterator_length) {
        return;
    }
    size_t length = 0;
    while (identifier_char(expression[5 + length])) {
        length++;
    }
    if (!length) {
        return;
    }
    ui_document_binding *list = binding(bindings, expression + 5, length);
    list->list = true;
    char name[64];
    snprintf(name, sizeof name, "%.*s", (int)iterator_length, value + iterator);
    size_t field;
    for (const char *f = next_member(rml, end, name, &field); f;
         f = next_member(rml, f, name, &field)) {
        char word[64];
        snprintf(word, sizeof word, "%.*s", (int)field, f);
        bool known = false;
        for (const char *w = list->fields; *w && !known;
             w += strcspn(w, ",") + (w[strcspn(w, ",")] ? 2 : 0)) {
            known = strcspn(w, ",") == field && !strncmp(w, word, field);
        }
        size_t used = strlen(list->fields);
        if (!known && used + field + 2 < sizeof list->fields) {
            snprintf(list->fields + used, sizeof list->fields - used, "%s%s", used ? ", " : "", word);
        }
    }
}

void ui_document_bindings(const char *rml, ui_document_binding **bindings)
{
    for (const char *p = strchr(rml, '<'); p; p = strchr(p, '<')) {
        const char *end = strncmp(p, "<!--", 4) ? tag_end(p) : strstr(p, "-->");
        if (!end) {
            break;
        }
        if (isalpha((unsigned char)p[1])) {
            list_binding(bindings, rml, p, end);
        }
        p = end;
    }
    /* Lists come first, then the values in the order they first appear. */
    size_t length;
    for (const char *at = next_member(rml, rml, "self", &length); at;
         at = next_member(rml, at, "self", &length)) {
        binding(bindings, at, length);
    }
}

/* ---------- Renames ---------- */

/* Where markup names a widget or an event. */
typedef struct
{
    const char *at;
    size_t length;
} name_span;

static void use_spans(const char *rml, name_span **spans)
{
    use_tag tag;
    for (const char *p = rml; next_use(p, &tag) == USE_FOUND; p = tag.inner) {
        apush(*spans, ((name_span){ tag.ui + 4, strlen(tag.name) }));
    }
}

/* Event names in one key-binding attribute of the tag opening at `open`. */
static void key_event_spans(const char *open, const char *end, const char *attribute, name_span **spans)
{
    size_t length;
    const char *value = attribute_span(open, end, attribute, &length);
    const char *stop = value ? value + length : NULL;
    for (const char *p = value; p && p < stop;) {
        p += strspn(p, " ");
        const char *binding = p;
        while (p < stop && *p != ' ') {
            p++;
        }
        const char *colon = memchr(binding, ':', (size_t)(p - binding));
        if (colon && colon != binding && colon + 1 < p) {
            apush(*spans, ((name_span){ colon + 1, (size_t)(p - colon - 1) }));
        }
    }
}

static void tag_event_spans(const char *open, const char *end, name_span **spans)
{
    emit_span *emits = NULL;
    emit_spans(open, end, &emits);
    for (int i = 0; i < acount(emits); i++) {
        apush(*spans, ((name_span){ emits[i].at, emits[i].length }));
    }
    afree(emits);
    key_event_spans(open, end, "data-keys", spans);
    key_event_spans(open, end, "data-area-keys", spans);
}

static void event_spans(const char *rml, name_span **spans)
{
    for (const char *p = strchr(rml, '<'); p; p = strchr(p, '<')) {
        const char *end = strncmp(p, "<!--", 4) ? tag_end(p) : strstr(p, "-->");
        if (!end) {
            return;
        }
        if (isalpha((unsigned char)p[1])) {
            tag_event_spans(p, end, spans);
        }
        p = end;
    }
}

static int span_order(const void *a, const void *b)
{
    const char *left = ((const name_span *)a)->at;
    const char *right = ((const name_span *)b)->at;
    return (left > right) - (left < right);
}

void ui_document_event_ranges(const char *rml, ui_document_event_range **ranges)
{
    name_span *spans = NULL;
    event_spans(rml, &spans);
    asort(spans, span_order);
    for (int i = 0; i < acount(spans); i++) {
        int start = (int)(spans[i].at - rml);
        apush(*ranges, ((ui_document_event_range){ start, start + (int)spans[i].length }));
    }
    afree(spans);
}

char *ui_document_rename(const char *rml, bool event, const char *from, const char *to, int *count)
{
    name_span *spans = NULL;
    if (event) {
        event_spans(rml, &spans);
    } else {
        use_spans(rml, &spans);
    }
    asort(spans, span_order);
    char *out = NULL;
    const char *p = rml;
    *count = 0;
    for (int i = 0; i < acount(spans); i++) {
        const name_span *span = &spans[i];
        if (span->length != strlen(from) || memcmp(span->at, from, span->length)) {
            continue;
        }
        put(&out, p, (size_t)(span->at - p));
        put(&out, to, strlen(to));
        p = span->at + span->length;
        ++*count;
    }
    put(&out, p, strlen(p) + 1);
    char *text = xstrdup(out);
    afree(out);
    afree(spans);
    return text;
}
