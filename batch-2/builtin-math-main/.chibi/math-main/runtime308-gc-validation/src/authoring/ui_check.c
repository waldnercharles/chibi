#include "authoring/ui_check.h"

#include "assets/ui_document.h"
#include "render/ui.h"
#include "support/array.h"
#include "support/rcss.h"
#include "support/xalloc.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The markup's document opens with a line of its own, so its lines are the
 * .rml file's plus one, and closes on another. It has a face, as a page's
 * body has, so text lays out. */
static const char DOCUMENT[] =
    "<rml><head><style>body { display: block; width: 100%%; height: 100%%; "
    "font-family: Inter; }"
    "scrollbarvertical { width: 8dp; margin-left: 0dp; }"
    "button:not([disabled]) { tab-index: auto; }"
    "scrollbarvertical, scrollbarhorizontal, scrollbarcorner, "
    "scrollbarvertical slidertrack, scrollbarhorizontal slidertrack, "
    "scrollbarvertical sliderbar, scrollbarhorizontal sliderbar { "
    "pointer-events: auto; }"
    "scrollbarvertical slidertrack { background-color: transparent; }"
    "scrollbarvertical sliderbar { margin: 2dp; border-radius: 3dp; "
    "background-color: var(--scrollbar, #777); }"
    "scrollbarvertical sliderbar:hover { "
    "background-color: var(--scrollbar-hover, #aaa); }"
    "scrollbarhorizontal { height: 8dp; }"
    "scrollbarhorizontal sliderbar { margin: 2dp; border-radius: 3dp; "
    "background-color: var(--scrollbar, #777); }"
    "scrollbarhorizontal sliderbar:hover { "
    "background-color: var(--scrollbar-hover, #aaa); }"
    "scrollbarcorner { background-color: transparent; }</style></head>"
    "<body data-model=\"" UI_DATA_MODEL "\"%s%s>\n%s%s</body></rml>";
/* What RmlUi calls the markup's document in its messages. */
#define MARKUP_URL "markup"

static void ignore_emit(void *, const rml_event *, const char *, int, const char *const *)
{
}

/* The line a message names: "...:12..." or "on line 12". */
static int named_line(const char *text)
{
    const char *on = strstr(text, "on line ");
    if (on) {
        return atoi(on + 8);
    }
    for (const char *p = strchr(text, ':'); p; p = strchr(p + 1, ':')) {
        if (isdigit((unsigned char)p[1])) {
            return atoi(p + 1);
        }
    }
    return 0;
}

/* The line of the first place `block` holds the message's first quoted
 * text, as the expression a data binding warning quotes; 0 when none. */
static int quoted_line(const char *text, const char *block)
{
    for (const char *open = strpbrk(text, "'\""); open;
         open = strpbrk(open + 1, "'\"")) {
        const char *close = strchr(open + 1, *open);
        if (!close || close == open + 1) {
            continue;
        }
        char quoted[256];
        snprintf(quoted, sizeof quoted, "%.*s", (int)(close - open - 1), open + 1);
        const char *found = strstr(block, quoted);
        if (!found) {
            continue;
        }
        int line = 1;
        for (const char *p = block; p < found; p++) {
            line += *p == '\n';
        }
        return line;
    }
    return 0;
}

/* A data expression's warning comes as its message, the expression quoted
 * on a line of its own, a caret under the fault, and the view it left out:
 * one message. */
static bool continues(const char *text)
{
    return !strncmp(text, "  \"", 3) || strspn(text, " ^") == strlen(text) ||
           !strncmp(text, "Could not add data-", 19);
}

static char **joined_warnings(char **warnings)
{
    char **joined = NULL;
    for (int i = 0; i < acount(warnings); i++) {
        if (!acount(joined) || !continues(warnings[i])) {
            apush(joined, warnings[i]);
            continue;
        }
        char **last = &joined[acount(joined) - 1];
        size_t size = strlen(*last) + strlen(warnings[i]) + 2;
        char *text = xmalloc(size);
        snprintf(text, size, "%s\n%s", *last, warnings[i]);
        free(*last);
        free(warnings[i]);
        *last = text;
    }
    afree(warnings);
    return joined;
}

/* The text with its digits dropped, to see the same warning at two lines. */
static bool same_warning(const char *a, const char *b)
{
    for (;;) {
        a += strspn(a, "0123456789");
        b += strspn(b, "0123456789");
        if (*a != *b) {
            return false;
        }
        if (!*a) {
            return true;
        }
        a++;
        b++;
    }
}

static bool among(char **warnings, const char *text)
{
    for (int i = 0; i < acount(warnings); i++) {
        if (same_warning(warnings[i], text)) {
            return true;
        }
    }
    return false;
}

static int line_count(const char *text)
{
    int lines = 1;
    for (const char *p = text; *p; p++) {
        lines += *p == '\n' && p[1];
    }
    return lines;
}

/* Replaces the first `from` in the owned text with `to`. */
static char *replaced(char *text, const char *from, const char *to)
{
    const char *at = strstr(text, from);
    if (!at) {
        return text;
    }
    size_t size = strlen(text) - strlen(from) + strlen(to) + 1;
    char *out = xmalloc(size);
    snprintf(out, size, "%.*s%s%s", (int)(at - text), text, to, at + strlen(from));
    free(text);
    return out;
}

/* The warning with the document's line it names, `line`, given as `place`
 * instead, as "markup:4" becomes "line 3". */
static char *relocated(char *text, int line, const char *place)
{
    char document[64];
    snprintf(document, sizeof document, MARKUP_URL ":%d", line);
    text = replaced(text, document, place);
    snprintf(document, sizeof document, "line %d of " MARKUP_URL, line);
    return replaced(text, document, place);
}

/* A warning's line in `text`: the line it names, less the `offset` lines
 * of the document before `text`, or where `text` holds what it quotes; 0
 * when neither. */
static int warning_line(const char *warning, const char *text, int offset)
{
    int named = named_line(warning);
    return named ? named - offset : quoted_line(warning, text);
}

/* The offset in `text` of the start of 1-based line `line`. */
static int line_offset(const char *text, int line)
{
    const char *p = text;
    for (int i = 1; i < line && (p = strchr(p, '\n')); i++) {
        p++;
    }
    return p ? (int)(p - text) : (int)strlen(text);
}

/* The data-src="<asset>:<line>" of the element whose start tag holds
 * `at`, or the nearest before it, in markup ui_document_expand marked. */
static bool element_source(const char *markup, int at, char *asset, size_t size, int *line)
{
    static const char ATTRIBUTE[] = "data-src=\"";
    const char *found = NULL;
    for (const char *p = markup; (p = strstr(p, ATTRIBUTE)) && p - markup <= at; p++) {
        found = p + sizeof ATTRIBUTE - 1;
    }
    const char *colon = found ? strchr(found, ':') : NULL;
    if (!colon) {
        return false;
    }
    snprintf(asset, size, "%.*s", (int)(colon - found), found);
    *line = atoi(colon + 1);
    return true;
}

/* Loads `markup` in a document with the page's body attributes and returns
 * the warnings RmlUi logs, bar those only the document's closing line
 * shows, which follow from an earlier one. */
static char **load_markup(rml_context *context, const char *body, const char *markup)
{
    const char *space = body ? " " : "", *attributes = body ? body : "";
    const char *close = markup[0] && markup[strlen(markup) - 1] == '\n' ? "" : "\n";
    int size = snprintf(NULL, 0, DOCUMENT, space, attributes, markup, close) + 1;
    char *text = xmalloc((size_t)size);
    snprintf(text, (size_t)size, DOCUMENT, space, attributes, markup, close);
    rml_backend_keep_warnings();
    rml_element *document = rml_context_load_document_from_memory(context, text, MARKUP_URL);
    if (document) {
        rml_document_show(document, RML_MODAL_NONE, RML_FOCUS_NONE, RML_SCROLL_FLAG_NONE);
        rml_context_update(context);
        rml_document_close(document);
        rml_context_update(context);
    }
    free(text);
    char **warnings = joined_warnings(rml_backend_take_warnings()), **kept = NULL;
    for (int i = 0; i < acount(warnings); i++) {
        if (named_line(warnings[i]) > line_count(markup) + 1) {
            free(warnings[i]);
            continue;
        }
        apush(kept, warnings[i]);
    }
    afree(warnings);
    return kept;
}

/* The asset's markup alone, whose lines are its .rml file's; returns what
 * RmlUi logged, as it logged it. */
static char **check_markup(rml_context *context, const ui_document *document, ui_check_message **out)
{
    char **warnings = load_markup(context, document->body, document->rml);
    for (int i = 0; i < acount(warnings); i++) {
        int named = named_line(warnings[i]);
        int line = warning_line(warnings[i], document->rml, 1);
        line = line ? document->rml_line + line - 1 : 0;
        char place[32];
        snprintf(place, sizeof place, "line %d", line);
        char *text = xstrdup(warnings[i]);
        text = named ? relocated(text, named, place) : text;
        apush(*out, ((ui_check_message){ UI_CHECK_RML, NULL, line, text }));
    }
    return warnings;
}

/* The markup a Ui shows, each element marked with the asset and line
 * that wrote it, for what the markup alone, which logged `shown`, did not
 * show. */
static void check_expanded(
    rml_context *context,
    const char *name,
    const char *markup,
    char **shown,
    ui_check_message **out
)
{
    char **warnings = load_markup(context, NULL, markup);
    for (int i = 0; i < acount(warnings); i++) {
        if (among(shown, warnings[i])) {
            free(warnings[i]);
            continue;
        }
        int named = named_line(warnings[i]);
        int line = warning_line(warnings[i], markup, 1);
        int at = line_offset(markup, line + 1) - 1;
        char asset[128], place[192];
        int source_line = 0;
        bool found = line &&
                     element_source(markup, at, asset, sizeof asset, &source_line);
        ui_check_message m = { UI_CHECK_WIDGETS, NULL, 0, warnings[i] };
        if (found) {
            m.source = strcmp(asset, name) ? xstrdup(asset) : NULL;
            m.line = source_line;
            snprintf(place, sizeof place, "%s line %d", asset, source_line);
            m.text = named ? relocated(warnings[i], named, place) : warnings[i];
        }
        apush(*out, m);
    }
    afree(warnings);
}

/* RCSS with its imports read and nesting flattened (rcss_expand); each
 * warning at the line it came from, in the text `map` traces or an
 * imported file. */
static void check_style(rml_context *context, const char *rcss, const rcss_map *map, ui_check_message **out)
{
    rml_element *sheet = rml_context_load_document_from_memory(context, "<rml><head></head><body/></rml>", "style");
    rml_backend_keep_warnings();
    rml_document_set_named_style_sheet(sheet, "style", rcss);
    char **warnings = joined_warnings(rml_backend_take_warnings());
    for (int i = 0; i < acount(warnings); i++) {
        int line = warning_line(warnings[i], rcss, 0);
        rcss_origin origin = line > 0 && line <= acount(map->lines)
                               ? map->lines[line - 1]
                               : (rcss_origin){ -1, 0 };
        const char *source = origin.file >= 0 ? map->files[origin.file] : NULL;
        apush(
            *out,
            ((ui_check_message){ UI_CHECK_RCSS,
                                 source ? xstrdup(source) : NULL,
                                 origin.line,
                                 warnings[i] })
        );
    }
    afree(warnings);
    rml_document_close(sheet);
    rml_context_update(context);
}

static void free_names(char **names)
{
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
}

/* Expands the asset's widgets and checks what that shows; false with the
 * error when a widget cannot be. */
static bool check_widgets(
    rml_context *context,
    const ui_document *document,
    const char *name,
    const char *directory,
    char **shown,
    ui_check_message **out,
    char *error,
    size_t error_size
)
{
    char **uses = NULL;
    ui_document_use_names(document->rml, &uses);
    bool widgets = acount(uses) > 0;
    free_names(uses);
    uses = NULL;
    ui_document expanded = { .rml = xstrdup(document->rml),
                             .rcss = xstrdup(""),
                             .rml_line = document->rml_line };
    bool ok = !widgets ||
              ui_document_expand(&expanded, name, directory, true, &uses, error, error_size);
    if (ok && widgets) {
        check_expanded(context, name, expanded.rml, shown, out);
    }
    free_names(uses);
    ui_document_free(&expanded);
    return ok;
}

bool ui_check(
    const char *content_root,
    const char *name,
    const char *rml,
    const char *rcss,
    ui_check_message **messages,
    char *error,
    size_t error_size
)
{
    rml_backend *backend = rml_backend_create(1280, 720, 1);
    bool ok = ui_check_in(rml_backend_context(backend), content_root, name, rml, rcss, messages, error, error_size);
    rml_backend_destroy(backend);
    return ok;
}

bool ui_check_in(
    rml_context *context,
    const char *content_root,
    const char *name,
    const char *rml,
    const char *rcss,
    ui_check_message **messages,
    char *error,
    size_t error_size
)
{
    char directory[1024];
    snprintf(directory, sizeof directory, "%s/ui", content_root);
    ui_document document = { 0 };
    if (!ui_document_read(rml, rcss, directory, NULL, &document, error, error_size)) {
        return false;
    }
    ui_data data;
    ui_data_init(&data, context, NULL, ignore_emit, NULL);
    char **shown = check_markup(context, &document, messages);
    check_style(context, document.rcss, &document.map, messages);
    /* A scene's page shows its markup as written; a Ui expands widgets. */
    bool ok = document.body ||
              check_widgets(context, &document, name, directory, shown, messages, error, error_size);
    free_names(shown);
    ui_data_free(&data);
    ui_document_free(&document);
    return ok;
}

bool ui_check_stylesheet(
    rml_context *context,
    const char *directory,
    const char *text,
    ui_check_message **messages,
    char *error,
    size_t error_size
)
{
    rcss_map map = { 0 };
    char *rcss = rcss_expand(text, directory, &map, error, error_size);
    if (rcss) {
        check_style(context, rcss, &map, messages);
    }
    rcss_map_free(&map);
    bool ok = rcss != NULL;
    free(rcss);
    return ok;
}

void ui_check_property_names(char ***names)
{
    for (int id = 1; id <= UINT8_MAX; id++) {
        const char *property = rml_property_name((uint8_t)id);
        const char *shorthand = rml_shorthand_name((uint8_t)id);
        if (property) {
            apush(*names, xstrdup(property));
        }
        if (shorthand) {
            apush(*names, xstrdup(shorthand));
        }
    }
}

void ui_check_messages_free(ui_check_message *messages)
{
    for (int i = 0; i < acount(messages); i++) {
        free(messages[i].source);
        free(messages[i].text);
    }
    afree(messages);
}
