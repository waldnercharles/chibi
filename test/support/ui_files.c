/* UI asset fixtures: a ui asset's markup and styles as its two files. */
#include "assets/ui_document.h"
#include "test/suite_internal.h"

#include <assert.h>

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(text, file);
    fclose(file);
}

void test_write_ui(const char *folder, const char *name, const char *rml, const char *rcss)
{
    char *markup = ui_document_path(folder, name, false);
    char *styles = ui_document_path(folder, name, true);
    write_text(markup, rml);
    if (rcss) {
        write_text(styles, rcss);
    } else {
        unlink(styles);
    }
    free(styles);
    free(markup);
}

void test_remove_ui(const char *folder, const char *name)
{
    char *markup = ui_document_path(folder, name, false);
    char *styles = ui_document_path(folder, name, true);
    unlink(markup);
    unlink(styles);
    free(styles);
    free(markup);
}
