#include "assets/ui_document.h"

#include "support/array.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static void write_widget(const char *root, const char *name, const char *yaml)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s.yaml", root, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(yaml, file);
    fclose(file);
}

static void free_names(char **names)
{
    for (int i = 0; i < acount(names); i++) {
        free(names[i]);
    }
    afree(names);
}

/* Expands `rml` against `root`; returns the markup or the error. */
static bool expand_text(const char *root, const char *rml, char *out, size_t size, char ***uses)
{
    ui_document d = { .rml = strdup(rml), .rcss = strdup("#host {}") };
    bool ok = ui_document_expand(&d, root, uses, out, size);
    if (ok) {
        snprintf(out, size, "%s|%s", d.rml, d.rcss);
    }
    ui_document_free(&d);
    return ok;
}

static void expanded_widgets()
{
    char root[] = "/tmp/chibi-ui-widgets-XXXXXX";
    assert(mkdtemp(root));
    write_widget(root, "Meter", "rml: <div class=\"fill\"/><slot/>\nrcss: '.fill { height: 4dp; }'\n");
    write_widget(root, "Row", "rml: <span><slot/></span>\n");
    write_widget(root, "Loop", "rml: <use ui=\"Loop\"/>\n");
    char text[512];
    char **uses = NULL;
    bool ok = expand_text(root, "<use ui=\"Meter\" id=\"hull\" class=\"wide\"/>", text, sizeof text, &uses);
    test_check(
        ok &&
            !strcmp(
                text,
                "<div data-ui=\"Meter\" id=\"hull\" class=\"wide\"><div "
                "class=\"fill\"/></div>"
                "|.fill { height: 4dp; }\n#host {}"
            ) &&
            acount(uses) == 1 && !strcmp(uses[0], "Meter"),
        "a used widget becomes a div with its markup, and its RCSS precedes "
        "the host's"
    );
    free_names(uses);
    uses = NULL;
    ok = expand_text(
        root,
        "<use ui=\"Row\"><use ui=\"Meter\">Hull</use></use><use ui=\"Meter\"/>",
        text,
        sizeof text,
        &uses
    );
    test_check(
        ok && !strcmp(
                  text,
                  "<div data-ui=\"Row\"><span><div data-ui=\"Meter\"><div "
                  "class=\"fill\"/>Hull</div>"
                  "</span></div><div data-ui=\"Meter\"><div "
                  "class=\"fill\"/></div>"
                  "|.fill { height: 4dp; }\n#host {}"
              ),
        "children fill a widget's slot, widgets nest, and each widget's RCSS "
        "appears once"
    );
    free_names(uses);
    static const struct
    {
        const char *rml, *error;
    } failures[] = {
        { "<use ui=\"Loop\"/>", "widget Loop uses itself" },
        { "<use ui=\"Gone\"/>", "widget Gone is missing" },
        { "<use id=\"x\"/>", "needs ui=" },
        { "<use ui=\"Row\">", "needs ui=" },
    };
    for (size_t i = 0; i < sizeof failures / sizeof *failures; i++) {
        uses = NULL;
        ok = expand_text(root, failures[i].rml, text, sizeof text, &uses);
        test_check(!ok && strstr(text, failures[i].error), failures[i].error);
        free_names(uses);
    }
    char **names = NULL;
    ui_document_use_names("<use ui=\"Row\"><use ui=\"Meter\"/></use><p/>", &names);
    test_check(
        acount(names) == 2 && !strcmp(names[0], "Row") && !strcmp(names[1], "Meter"),
        "a ui asset names the widgets its markup uses"
    );
    free_names(names);
    for (size_t i = 0; i < 3; i++) {
        static const char *const files[] = { "Meter", "Row", "Loop" };
        char path[256];
        snprintf(path, sizeof path, "%s/%s.yaml", root, files[i]);
        unlink(path);
    }
    rmdir(root);
}

void test_ui_document()
{
    composed_document();
    rejected_documents();
    last_valid_document();
    expanded_widgets();
}
