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
    bool ok = ui_document_expand(&d, "Host", root, uses, out, size);
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
                "|[data-ui=\"Meter\"] .fill { height: 4dp; "
                "}\n[data-ui=\"Host\"] #host {}\n"
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
                  "|[data-ui=\"Meter\"] .fill { height: 4dp; "
                  "}\n[data-ui=\"Host\"] #host {}\n"
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

static void scoped_styles()
{
    static const struct
    {
        const char *rcss, *scoped, *check;
    } cases[] = {
        { "#a, .b > p { color: red; }",
          "[data-ui=\"Hud\"] #a, [data-ui=\"Hud\"] .b > p { color: red; }",
          "each selector in a list matches inside the scoped element" },
        { "body { color: red; } body.paused #title {}",
          "[data-ui=\"Hud\"] { color: red; } [data-ui=\"Hud\"].paused #title "
          "{}",
          "body names the scoped element itself" },
        { "bodyguard {}",
          "[data-ui=\"Hud\"] bodyguard {}",
          "only a whole body name is the scoped element" },
        { "p:not(.a, .b) {}",
          "[data-ui=\"Hud\"] p:not(.a, .b) {}",
          "commas inside parentheses stay in one selector" },
        { "@media (min-width: 10dp) { p { x: 1; } }",
          "@media (min-width: 10dp) { [data-ui=\"Hud\"] p { x: 1; } }",
          "rules in a media block are scoped" },
        { "@keyframes pulse { from { x: 0; } to { x: 1; } } p {}",
          "@keyframes pulse { from { x: 0; } to { x: 1; } } [data-ui=\"Hud\"] "
          "p {}",
          "other at-rules are kept as written" },
        { "/* note */ p {}", "/* note */ [data-ui=\"Hud\"] p {}", "comments are kept" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        char *scoped = ui_document_scope(cases[i].rcss, "Hud");
        test_check(!strcmp(scoped, cases[i].scoped), cases[i].check);
        if (strcmp(scoped, cases[i].scoped) != 0) {
            printf("  got: %s\n", scoped);
        }
        free(scoped);
    }
}

/* Each data-event and data-keys binding, with the element that names it. */
static void named_events()
{
    ui_document_event *events = NULL;
    ui_document_events(
        "<!-- <b>x</b> <div data-event=\"Hidden\"/> -->"
        "<use ui=\"MenuButton\" id=\"resume\" "
        "data-event=\"Resume\">Resume</use>"
        "<div class=\"row wide\" title=\"a data-event='Fake'\" "
        "data-event=\"Pick\"/>"
        "<input type='range' data-event='Volume'/>"
        "<use ui=\"Dialog\" data-keys=\"Escape:Close Enter:Choose "
        "Space:Choose\"/>",
        &events
    );
    static const struct
    {
        const char *name, *element, *key;
        bool input;
    } expected[] = {
        { "Resume", "#resume", NULL, false },
        { "Pick", "div.row", NULL, false },
        { "Volume", "input", NULL, true },
        { "Close", "Dialog", "Escape", false },
        { "Choose", "Dialog", "Enter or Space", false },
    };
    bool same = acount(events) == (int)(sizeof expected / sizeof *expected);
    for (int i = 0; same && i < acount(events); i++) {
        same = !strcmp(events[i].name, expected[i].name) &&
               !strcmp(events[i].element, expected[i].element) &&
               (expected[i].key
                    ? events[i].key && !strcmp(events[i].key, expected[i].key)
                    : !events[i].key) &&
               events[i].input == expected[i].input;
    }
    test_check(same, "a ui asset names the events its elements send and the elements that send them");
    ui_document_events_free(events);
}

void test_ui_document()
{
    composed_document();
    rejected_documents();
    last_valid_document();
    expanded_widgets();
    scoped_styles();
    named_events();
}
