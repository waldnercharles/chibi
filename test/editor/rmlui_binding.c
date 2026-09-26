/* RmlUi binding calls that load documents from text and replace their styles. */
#include "ui/rml.h"
#include "ui/rml_backend.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* A later sheet wins over an earlier one; an empty sheet changes nothing. */
static bool replaces_style_sheet(rml_context *context)
{
    rml_element *doc = rml_context_load_document_from_memory(
        context,
        "<rml><head/><body><div id=\"a\"/></body></rml>",
        "[memory]"
    );
    assert(doc);
    rml_element *a = rml_element_by_id(doc, "a");
    bool set = rml_document_set_style_sheet(doc, "#a { color: #ff0000; }\n#a { color: #00ff00; }\n");
    bool empty = rml_document_set_style_sheet(doc, "");
    rml_context_update(context);
    rml_color c = rml_element_color(a);
    rml_document_close(doc);
    return set && !empty && c.r == 0 && c.g == 255 && c.b == 0;
}

bool test_rmlui_binding()
{
    rml_backend *backend = rml_backend_create(320, 240, 1);
    rml_context *context = rml_backend_context(backend);
    bool ok = loads_from_memory(context) && replaces_style_sheet(context);
    rml_backend_destroy(backend);
    return ok;
}
