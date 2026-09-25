#include "assets/ui_document.h"

#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
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
