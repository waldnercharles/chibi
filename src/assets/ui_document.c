#include "assets/ui_document.h"

#include "support/array.h"
#include "support/asset_text.h"
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

static bool fail(char *error, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(error, size, format, args);
    va_end(args);
    return false;
}

static bool read_root(yyjson_val *root, ui_document *out, char *error, size_t error_size)
{
    if (!yyjson_is_obj(root)) {
        return fail(error, error_size, "a ui asset must be a mapping");
    }
    yyjson_obj_iter iterator = yyjson_obj_iter_with(root);
    yyjson_val *key;
    while ((key = yyjson_obj_iter_next(&iterator))) {
        const char *name = yyjson_get_str(key);
        if (strcmp(name, "rml") != 0 && strcmp(name, "rcss") != 0) {
            return fail(error, error_size, "unknown key %s", name);
        }
    }
    yyjson_val *rml = yyjson_obj_get(root, "rml");
    yyjson_val *rcss = yyjson_obj_get(root, "rcss");
    if (!yyjson_is_str(rml) || !yyjson_get_len(rml)) {
        return fail(error, error_size, "rml must be nonempty markup");
    }
    if (rcss && !yyjson_is_str(rcss)) {
        return fail(error, error_size, "rcss must be text");
    }
    for (size_t i = 0; i < sizeof DOCUMENT_TAGS / sizeof *DOCUMENT_TAGS; i++) {
        if (strstr(yyjson_get_str(rml), DOCUMENT_TAGS[i])) {
            return fail(error, error_size, "rml holds body markup; remove %s>", DOCUMENT_TAGS[i]);
        }
    }
    if (rcss && strstr(yyjson_get_str(rcss), "</style")) {
        return fail(error, error_size, "rcss must not close its style block");
    }
    out->rml = xstrdup(yyjson_get_str(rml));
    out->rcss = xstrdup(rcss ? yyjson_get_str(rcss) : "");
    return true;
}

bool ui_document_read(const char *text, ui_document *out, char *error, size_t error_size)
{
    assert(text && out);
    yyjson_doc *doc = asset_text_read(text, error, error_size);
    if (!doc) {
        return false;
    }
    ui_document next = { 0 };
    bool ok = read_root(yyjson_doc_get_root(doc), &next, error, error_size);
    yyjson_doc_free(doc);
    if (!ok) {
        ui_document_free(&next);
        return false;
    }
    ui_document_free(out);
    *out = next;
    return true;
}

/* ---------- Widgets ---------- */

/* How deeply widgets may nest inside one another. */
#define UI_USE_DEPTH 8

/* One <use> element in markup. */
typedef struct use_tag
{
    const char *open, *close; /* its first character and one past its end */
    const char *attrs, *attrs_end; /* the open tag's attributes */
    const char *ui;                /* the ui attribute within them */
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
    char *error;
    size_t error_size;
} expansion;

static void put(char **out, const char *text, size_t length)
{
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
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.yaml", x->ui_root, name);
    char *text = text_file_read(path);
    char why[160] = "is missing";
    bool read = text && ui_document_read(text, &alast(x->widgets), why, sizeof why);
    free(text);
    if (!read) {
        fail(x->error, x->error_size, "widget %s %s", name, why);
        return NULL;
    }
    return &alast(x->widgets);
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
        put(out, "<div", 4);
        put(out, tag->attrs, (size_t)(tag->ui - tag->attrs));
        put(out, "data-", 5);
        put(out, tag->ui, (size_t)(tag->attrs_end - tag->ui));
        put(out, ">", 1);
        put_filled(out, body, inner);
        put(out, "</div>", 6);
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

/* Writes one selector matching inside the scope: `body` names the scoped
 * element itself. */
static void put_selector(char **out, const char *scope, const char *s, size_t length)
{
    while (length && isspace((unsigned char)*s)) {
        s++;
        length--;
    }
    while (length && isspace((unsigned char)s[length - 1])) {
        length--;
    }
    put(out, scope, strlen(scope));
    if (length >= 4 && !strncmp(s, "body", 4) && (length == 4 || !name_char(s[4]))) {
        put(out, s + 4, length - 4);
        return;
    }
    put(out, " ", 1);
    put(out, s, length);
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

/* Appends `rcss` scoped to `name`, with a line break, when there is any. */
static void put_scoped(char **out, const char *rcss, const char *name)
{
    if (!rcss[0]) {
        return;
    }
    char *scoped = ui_document_scope(rcss, name);
    put(out, scoped, strlen(scoped));
    put(out, "\n", 1);
    free(scoped);
}

bool ui_document_expand(
    ui_document *document,
    const char *name,
    const char *ui_root,
    char ***uses,
    char *error,
    size_t error_size
)
{
    expansion x = { .ui_root = ui_root, .error = error, .error_size = error_size };
    char *rml = NULL;
    bool ok = expand(&x, document->rml, &rml);
    if (ok) {
        char *rcss = NULL;
        for (int i = 0; i < acount(x.widgets); i++) {
            put_scoped(&rcss, x.widgets[i].rcss, x.names[i]);
        }
        put_scoped(&rcss, document->rcss, name);
        put(&rcss, "", 1);
        ui_document_free(document);
        *document = (ui_document){ .rml = xstrdup(rml), .rcss = xstrdup(rcss) };
        afree(rcss);
    }
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

/* The quoted value of the next `name` attribute at or after `p` in `rml`,
 * with its length; NULL when there is none. */
static const char *attribute_value(const char *rml, const char *p, const char *name, size_t *length)
{
    size_t name_length = strlen(name);
    for (; (p = strstr(p, name)); p += name_length) {
        if (p == rml || !isspace((unsigned char)p[-1]) || p[name_length] != '=') {
            continue;
        }
        char quote = p[name_length + 1];
        const char *value = p + name_length + 2;
        const char *end = quote == '"' || quote == '\'' ? strchr(value, quote) : NULL;
        if (end) {
            *length = (size_t)(end - value);
            return value;
        }
    }
    return NULL;
}

static void push_name(char ***names, const char *name, size_t length)
{
    if (length) {
        apush(*names, memcpy(xcalloc(length + 1, 1), name, length));
    }
}

void ui_document_event_names(const char *rml, char ***names)
{
    size_t length;
    for (const char *v = rml; (v = attribute_value(rml, v, "data-event", &length));
         v += length) {
        push_name(names, v, length);
    }
    for (const char *v = rml; (v = attribute_value(rml, v, "data-keys", &length));
         v += length) {
        /* Bindings are "Key:Event Key:Event". */
        for (const char *p = v; p < v + length;) {
            const char *end = p + strcspn(p, " ");
            end = end < v + length ? end : v + length;
            const char *colon = memchr(p, ':', (size_t)(end - p));
            if (colon) {
                push_name(names, colon + 1, (size_t)(end - colon - 1));
            }
            p = end + 1;
        }
    }
}

char *ui_document_compose(const ui_document *document, const char *base)
{
    static const char FORMAT[] = "<rml><head><style>%s\n%s</style></"
                                 "head><body>%s</body></rml>";
    int size = snprintf(NULL, 0, FORMAT, base, document->rcss, document->rml);
    assert(size >= 0);
    char *text = xmalloc((size_t)size + 1);
    snprintf(text, (size_t)size + 1, FORMAT, base, document->rcss, document->rml);
    return text;
}

void ui_document_free(ui_document *document)
{
    free(document->rml);
    free(document->rcss);
    *document = (ui_document){ 0 };
}
