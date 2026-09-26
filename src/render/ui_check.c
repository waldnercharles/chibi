#include "render/ui_check.h"

#include "assets/ui_document.h"
#include "render/ui.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The markup's document opens with a line of its own, so its lines are the
 * rml block's plus one, and closes on another. It has a face, as a page's
 * body has, so text lays out. */
static const char
    DOCUMENT[] = "<rml><head><style>body { font-family: Inter; }</style></head>"
                 "<body data-model=\"" UI_DATA_MODEL
                 "\"%s%s>\n%s%s</body></rml>";
/* What RmlUi calls the markup's document in its messages. */
#define MARKUP_URL "markup"

static void ignore_emit(void *, const rml_event *, const char *) {}

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

static bool reported(ui_check_message *messages, const char *text)
{
    for (int i = 0; i < acount(messages); i++) {
        if (same_warning(messages[i].text, text)) {
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

/* The warning with the document's line it names given as the block's. */
static char *relocated(char *text, int line)
{
    char document[64], block[32];
    snprintf(block, sizeof block, "line %d", line);
    snprintf(document, sizeof document, MARKUP_URL ":%d", line + 1);
    text = replaced(text, document, block);
    snprintf(document, sizeof document, "line %d of " MARKUP_URL, line + 1);
    return replaced(text, document, block);
}

/* Loads `markup` in a document with the page's body attributes, as the
 * check's `part`, keeping each warning it has not yet reported. What the
 * document's closing line shows only follows from an earlier warning. */
static void check_markup(
    rml_context *context,
    const char *body,
    const char *markup,
    ui_check_part part,
    ui_check_message **out
)
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
    char **warnings = joined_warnings(rml_backend_take_warnings());
    int lines = line_count(markup);
    for (int i = 0; i < acount(warnings); i++) {
        int line = named_line(warnings[i]);
        if (line > lines + 1 ||
            (part == UI_CHECK_WIDGETS && reported(*out, warnings[i]))) {
            free(warnings[i]);
            continue;
        }
        line = part != UI_CHECK_RML ? 0
             : line                 ? line - 1
                                    : quoted_line(warnings[i], markup);
        char *message = part == UI_CHECK_RML && line ? relocated(warnings[i], line)
                                                     : warnings[i];
        apush(*out, ((ui_check_message){ part, line, message }));
    }
    afree(warnings);
    free(text);
}

static void check_style(rml_context *context, const char *rcss, ui_check_message **out)
{
    rml_element *document = rml_context_load_document_from_memory(context, "<rml><head></head><body/></rml>", "style");
    rml_backend_keep_warnings();
    rml_document_set_style_sheet(document, rcss);
    char **warnings = joined_warnings(rml_backend_take_warnings());
    for (int i = 0; i < acount(warnings); i++) {
        int line = named_line(warnings[i]);
        apush(
            *out,
            ((ui_check_message){ UI_CHECK_RCSS,
                                 line ? line : quoted_line(warnings[i], rcss),
                                 warnings[i] })
        );
    }
    afree(warnings);
    rml_document_close(document);
    rml_context_update(context);
}

static void free_names(char **names)
{
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
}

/* The markup a GameUi shows, widgets expanded; false with the error when a
 * widget cannot be. */
static bool check_widgets(
    rml_context *context,
    const ui_document *document,
    const char *name,
    const char *directory,
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
    ui_document expanded = { .rml = xstrdup(document->rml), .rcss = xstrdup("") };
    bool ok = !widgets ||
              ui_document_expand(&expanded, name, directory, &uses, error, error_size);
    if (ok && widgets) {
        check_markup(context, NULL, expanded.rml, UI_CHECK_WIDGETS, out);
    }
    free_names(uses);
    ui_document_free(&expanded);
    return ok;
}

bool ui_check(
    const char *content_root,
    const char *name,
    const char *text,
    ui_check_message **messages,
    char *error,
    size_t error_size
)
{
    char directory[1024];
    snprintf(directory, sizeof directory, "%s/ui", content_root);
    ui_document document = { 0 };
    if (!ui_document_read(text, directory, NULL, &document, error, error_size)) {
        return false;
    }
    rml_backend *backend = rml_backend_create(1280, 720, 1);
    rml_context *context = rml_backend_context(backend);
    ui_data data;
    ui_data_init(&data, context, NULL, ignore_emit, NULL);
    check_markup(context, document.body, document.rml, UI_CHECK_RML, messages);
    check_style(context, document.rcss, messages);
    /* A scene's page shows its markup as written; a GameUi expands widgets. */
    bool ok = document.body ||
              check_widgets(context, &document, name, directory, messages, error, error_size);
    ui_data_free(&data);
    rml_backend_destroy(backend);
    ui_document_free(&document);
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
        free(messages[i].text);
    }
    afree(messages);
}
