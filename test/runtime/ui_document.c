#include "assets/ui_document.h"

#include "test/suite_internal.h"

#include <stdlib.h>
#include <string.h>

static void composed_document()
{
    ui_document d = { 0 };
    char error[256] = { 0 };
    bool ok = ui_document_read(
        "rml: |\n"
        "  <div id=\"score\">0</div>\n"
        "rcss: |\n"
        "  #score { font-size: 24dp; }\n",
        &d,
        error,
        sizeof error
    );
    char *text = ok ? ui_document_compose(&d, "body { font-family: Inter; }") : NULL;
    test_check(
        ok && text &&
            strcmp(
                text,
                "<rml><head><style>body { font-family: Inter; }\n"
                "#score { font-size: 24dp; }\n"
                "</style></head><body><div id=\"score\">0</div>\n"
                "</body></rml>"
            ) == 0,
        "a ui asset composes its RCSS and markup into one RML document"
    );
    free(text);
    ui_document_free(&d);
}

static void rejected_documents()
{
    static const struct
    {
        const char *yaml;
        const char *error;
    } cases[] = {
        { "[rml]", "must be a mapping" },
        { "rcss: 'div {}'", "rml must be nonempty" },
        { "rml: ''", "rml must be nonempty" },
        { "rml: '<div/>'\nstyle: dark", "unknown key style" },
        { "rml: '<body><div/></body>'", "remove <body>" },
        { "rml: '<div/>'\nrcss: [1]", "rcss must be text" },
        { "rml: '<div/>'\nrcss: '</style><div>'", "must not close its style" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        ui_document d = { 0 };
        char error[256] = { 0 };
        bool ok = ui_document_read(cases[i].yaml, &d, error, sizeof error);
        test_check(!ok && strstr(error, cases[i].error) && !d.rml, cases[i].error);
    }
}

/* An invalid edit keeps the last valid document; a valid one replaces it. */
static void last_valid_document()
{
    ui_document d = { 0 };
    char error[256] = { 0 };
    bool first = ui_document_read("rml: '<p>one</p>'", &d, error, sizeof error);
    bool rejected = !ui_document_read("rml: ''", &d, error, sizeof error);
    bool kept = d.rml && strcmp(d.rml, "<p>one</p>") == 0;
    bool replaced = ui_document_read("rml: '<p>two</p>'", &d, error, sizeof error);
    test_check(
        first && rejected && kept && replaced && strcmp(d.rml, "<p>two</p>") == 0,
        "an invalid ui asset keeps the last valid document"
    );
    ui_document_free(&d);
}

void test_ui_document()
{
    composed_document();
    rejected_documents();
    last_valid_document();
}
