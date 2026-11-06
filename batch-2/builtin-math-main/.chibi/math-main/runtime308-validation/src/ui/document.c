#include "ui/document.h"

#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
char *ui_document_compose(const ui_document *document, const char *base)
{
    static const char FORMAT[] = "<rml><head><style>%s\n%s</style></"
                                 "head><body%s%s>%s</body></rml>";
    const char *space = document->body ? " " : "",
               *body = document->body ? document->body : "";
    int size = snprintf(
        NULL,
        0,
        FORMAT,
        base,
        document->rcss,
        space,
        body,
        document->rml
    );
    assert(size >= 0);
    char *text = xmalloc((size_t)size + 1);
    snprintf(
        text,
        (size_t)size + 1,
        FORMAT,
        base,
        document->rcss,
        space,
        body,
        document->rml
    );
    return text;
}

void ui_document_free(ui_document *document)
{
    free(document->rml);
    free(document->rcss);
    free(document->body);
    rcss_map_free(&document->map);
    *document = (ui_document){ 0 };
}
