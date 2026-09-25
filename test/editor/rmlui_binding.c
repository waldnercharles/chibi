/* RmlUi binding calls that load documents from text and reload their styles. */
#include "editor/rmlui/backend.h"
#include "editor/rmlui/rml.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(text, file);
    fclose(file);
}

static bool red(rml_element *element)
{
    rml_color c = rml_element_color(element);
    return c.r == 255 && c.g == 0 && c.b == 0;
}

static bool loads_from_memory(rml_context *context)
{
    rml_element *doc = rml_context_load_document_from_memory(
        context,
        "<rml><head><style>div { color: #ff0000; }</style></head>"
        "<body><div id=\"a\" class=\"x\"/><div class=\"x\"/></body></rml>",
        "[memory]"
    );
    if (!doc) {
        return false;
    }
    rml_context_update(context);
    rml_element *a = rml_element_by_id(doc, "a");
    rml_element *matches[1];
    bool ok = a && rml_element_owner_document(a) == doc &&
              rml_element_query_selector_all(doc, ".x", matches, 1) == 2 &&
              matches[0] == a && red(a);
    rml_document_close(doc);
    return ok;
}

static bool reloads_style_sheet(rml_context *context)
{
    char root[] = "/tmp/chibi-rml-reload-XXXXXX";
    assert(mkdtemp(root));
    char rml[256], rcss[256];
    snprintf(rml, sizeof rml, "%s/doc.rml", root);
    snprintf(rcss, sizeof rcss, "%s/doc.rcss", root);
    write_file(rcss, "#a { color: #ff0000; }\n");
    write_file(
        rml,
        "<rml><head><link type=\"text/rcss\" href=\"doc.rcss\"/></head>"
        "<body><div id=\"a\"/></body></rml>"
    );
    rml_element *doc = rml_context_load_document(context, rml);
    assert(doc);
    rml_context_update(context);
    rml_element *a = rml_element_by_id(doc, "a");
    bool before = red(a);
    write_file(rcss, "#a { color: #00ff00; }\n");
    rml_document_reload_style_sheet(doc);
    rml_context_update(context);
    rml_color after = rml_element_color(a);
    rml_document_close(doc);
    unlink(rcss);
    unlink(rml);
    rmdir(root);
    return before && after.r == 0 && after.g == 255 && after.b == 0;
}

bool test_rmlui_binding()
{
    editor_rml_backend *backend = editor_rml_backend_create(320, 240, 1);
    rml_context *context = editor_rml_backend_context(backend);
    bool ok = loads_from_memory(context) && reloads_style_sheet(context);
    editor_rml_backend_destroy(backend);
    return ok;
}
