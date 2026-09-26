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
        NULL,
        NULL,
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
        { "rml: '<div/>'\nrcss: [1]", "rcss must be text" },
        { "rml: '<div/>'\nrcss: '</style><div>'", "must not close its style" },
        { "rml: '<div/>'\nrcss: '.a { color: red;'", "rcss line 1: missing }" },
        { "rml: '<body class=\"x\"><p/></body><p/>'", "must hold all the markup" },
        { "rml: '<div><body/></div>'", "remove <body>" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        ui_document d = { 0 };
        char error[256] = { 0 };
        bool ok = ui_document_read(cases[i].yaml, NULL, NULL, &d, error, sizeof error);
        test_check(!ok && strstr(error, cases[i].error) && !d.rml, cases[i].error);
    }
}

/* A scene's root document may wrap its markup in <body ...>, and its RCSS
 * may import stylesheets beside it. */
static void page_document()
{
    char root[] = "/tmp/chibi-ui-page-XXXXXX", path[128];
    assert(mkdtemp(root));
    snprintf(path, sizeof path, "%s/tokens.rcss", root);
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(":root { --a: 1; }\n", file);
    fclose(file);
    ui_document d = { 0 };
    char error[256] = { 0 }, **imports = NULL;
    bool ok = ui_document_read(
        "rml: '<body class=\"page\" "
        "data-class-dark=\"system.dark\"><slot/></body>'\n"
        "rcss: '@import \"tokens.rcss\";'\n",
        root,
        &imports,
        &d,
        error,
        sizeof error
    );
    char *composed = ok ? ui_document_compose(&d, "") : NULL;
    test_check(
        ok && !strcmp(d.body, "class=\"page\" data-class-dark=\"system.dark\"") &&
            !strcmp(d.rml, "<slot/>") && strstr(d.rcss, "body { --a: 1; }") &&
            acount(imports) == 1 &&
            strstr(composed, "<body class=\"page\" data-class-dark=\"system.dark\"><slot/></body>"),
        "a root document's <body> holds the page body's attributes and its "
        "RCSS imports stylesheets"
    );
    free(composed);
    for (int i = 0; i < acount(imports); i++) {
        free(imports[i]);
    }
    afree(imports);
    ui_document_free(&d);
    unlink(path);
    rmdir(root);
}

/* Nested RCSS flattens before the document scopes it. */
static void nested_rcss()
{
    ui_document d = { 0 };
    char error[256] = { 0 };
    bool ok = ui_document_read(
        "rml: <div class=\"menu\"/>\n"
        "rcss: |\n"
        "  // rows\n"
        "  .menu { .item { margin: 0; &:hover { color: var(--fg); } } }\n",
        NULL,
        NULL,
        &d,
        error,
        sizeof error
    );
    char *scoped = ok ? ui_document_scope(d.rcss, "Menu") : NULL;
    test_check(
        scoped && strstr(scoped, "[data-ui=\"Menu\"] .menu .item { margin: 0; }") &&
            strstr(scoped, "[data-ui=\"Menu\"] .menu .item:hover { color: var(--fg); }"),
        "a ui asset's nested RCSS flattens, then scopes to the asset"
    );
    free(scoped);
    ui_document_free(&d);
}

/* An invalid edit keeps the last valid document; a valid one replaces it. */
static void last_valid_document()
{
    ui_document d = { 0 };
    char error[256] = { 0 };
    bool first = ui_document_read("rml: '<p>one</p>'", NULL, NULL, &d, error, sizeof error);
    bool rejected = !ui_document_read("rml: ''", NULL, NULL, &d, error, sizeof error);
    bool kept = d.rml && strcmp(d.rml, "<p>one</p>") == 0;
    bool replaced = ui_document_read("rml: '<p>two</p>'", NULL, NULL, &d, error, sizeof error);
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
    write_widget(root, "Option", "rml: <label><input type=\"radio\" attrs/><slot/></label>\n");
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
                "}\n\n[data-ui=\"Host\"] #host {}\n"
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
                  "}\n\n[data-ui=\"Host\"] #host {}\n"
              ),
        "children fill a widget's slot, widgets nest, and each widget's RCSS "
        "appears once"
    );
    free_names(uses);
    uses = NULL;
    ok = expand_text(
        root,
        "<use id=\"dark\" ui=\"Option\" data-checked=\"save.theme\" "
        "value=\"dark\">Dark</use>",
        text,
        sizeof text,
        &uses
    );
    test_check(
        ok && !strncmp(
                  text,
                  "<div data-ui=\"Option\"><label><input type=\"radio\" "
                  "id=\"dark\" "
                  "data-checked=\"save.theme\" "
                  "value=\"dark\"/>Dark</label></div>|",
                  strlen(
                      "<div data-ui=\"Option\"><label><input type=\"radio\" "
                      "id=\"dark\" "
                      "data-checked=\"save.theme\" "
                      "value=\"dark\"/>Dark</label></div>|"
                  )
              ),
        "a widget element marked attrs takes the use element's attributes"
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
    for (size_t i = 0; i < 4; i++) {
        static const char *const files[] = { "Meter", "Row", "Loop", "Option" };
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

/* Each event a handler emits and each data-keys binding, with the element
 * that names it. */
static void named_events()
{
    ui_document_event *events = NULL;
    ui_document_events(
        "<!-- <b>x</b> <div data-event-click=\"emit('Hidden')\"/> -->"
        "<use ui=\"MenuButton\" id=\"resume\" "
        "data-event-click=\"emit('Resume')\">Resume</use>"
        "<div class=\"row wide\" title=\" data-event-click='emit(Fake)'\" "
        "data-event-click=\"emit('Pick')\"/>"
        "<input type='range' data-event-change=\"emit('Volume')\"/>"
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
    nested_rcss();
    page_document();
    expanded_widgets();
    scoped_styles();
    named_events();
}
