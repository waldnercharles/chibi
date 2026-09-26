/* The ui language service: highlighting, outline, diagnostics, completion,
 * references and edits of ui assets. */
#include "assets/ui_source.h"

#include "support/array.h"
#include "test/suite_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char SAMPLE
    [] = "rml: |\n"
         "  <div id=\"panel\" class=\"card wide\">\n"
         "    <span data-if=\"save.sound\">{{ save.name }}</span>\n"
         "    <use ui=\"MenuButton\" data-event-click=\"emit('Resume')\"/>\n"
         "  </div>\n"
         "rcss: |\n"
         "  // tokens\n"
         "  .card {\n"
         "    color: var(--primary);\n"
         "    &:hover { opacity: 0.5; }\n"
         "    .dark & { width: 10dp; }\n"
         "  }\n";

static const char *const KIND_NAMES[UI_SOURCE_KIND_COUNT] = {
    "text",     "number",   "string",   "keyword", "name", "path",
    "punct",    "comment",  "error",    "warning", "tag",  "attribute",
    "selector", "property", "variable", "binding",
};

/* The spans as "kind:text" words, for comparing with a golden. */
static char *spans_text(const ui_source *s)
{
    size_t size = 1;
    for (int i = 0; i < acount(s->spans); i++) {
        size += 24 + (size_t)(s->spans[i].end - s->spans[i].start);
    }
    char *out = malloc(size);
    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; i < acount(s->spans); i++) {
        const ui_source_span *span = &s->spans[i];
        used += (size_t)snprintf(
            out + used,
            size - used,
            "%s%s:%.*s",
            i ? " " : "",
            KIND_NAMES[span->kind],
            span->end - span->start,
            s->text + span->start
        );
    }
    return out;
}

static bool slice_is(const ui_source *s, int start, int end, const char *expected)
{
    return (size_t)(end - start) == strlen(expected) &&
           !strncmp(s->text + start, expected, (size_t)(end - start));
}

static void highlighting()
{
    ui_source *s = ui_source_parse(
        "rml: '<p class=\"a\" style=\"width: 2dp\">{{ save.x + 1 }}</p>'\n"
        "rcss: '.a { color: #fff; } /* c */'\n",
        false
    );
    char *text = spans_text(s);
    const char *expected =
        "name:rml punct:: punct:< tag:p attribute:class punct:= string:\" "
        "string:a string:\" attribute:style punct:= string:\" property:width "
        "punct:: number:2dp string:\" punct:> punct:{{ binding:save punct:. "
        "binding:x punct:+ number:1 punct:}} punct:</ tag:p punct:> "
        "name:rcss punct:: selector:. selector:a punct:{ property:color "
        "punct:: "
        "number:#fff punct:; punct:} comment:/* c */";
    bool ok = !strcmp(text, expected);
    if (!ok) {
        printf("  spans: %s\n", text);
    }
    test_check(ok, "ui source highlights RML tags, attributes, bindings and inline style, and RCSS, in file offsets");
    test_check(
        !strcmp(ui_source_kind_class(UI_SOURCE_TAG), "tok-keyword tok-tag") &&
            !strcmp(ui_source_kind_class(UI_SOURCE_STRING), "tok-string") &&
            !ui_source_kind_class(UI_SOURCE_TEXT),
        "ui source spans name highlight.c's tok-* classes and their own"
    );
    free(text);
    ui_source_free(s);

    s = ui_source_parse(SAMPLE, false);
    text = spans_text(s);
    ok = strstr(text, "comment:// tokens") &&
         strstr(text, "keyword:& keyword:: keyword:hover") &&
         strstr(text, "variable:--primary") &&
         strstr(text, "name:emit punct:( string:'Resume'") &&
         strstr(text, "selector:. selector:dark keyword:&");
    if (!ok) {
        printf("  spans: %s\n", text);
    }
    test_check(ok, "ui source highlights literal blocks: line comments, nesting, var() and emit()");
    free(text);
    ui_source_free(s);
}

static void outline()
{
    ui_source *s = ui_source_parse(SAMPLE, false);
    const ui_source_element *e = s->elements;
    bool elements = acount(s->elements) == 3 && !strcmp(e[0].tag, "div") &&
                    !strcmp(e[0].id, "panel") &&
                    !strcmp(e[0].classes, "card wide") && e[0].parent == -1 &&
                    e[0].line == 2 && !strcmp(e[1].tag, "span") &&
                    e[1].parent == 0 && e[1].line == 3 && !strcmp(e[2].tag, "use") &&
                    e[2].self_closing && e[2].parent == 0 && e[2].line == 4;
    bool ranges = elements &&
                  slice_is(s, e[1].start, e[1].end, "<span data-if=\"save.sound\">{{ save.name }}</span>") &&
                  slice_is(s, e[1].start, e[1].open_end, "<span data-if=\"save.sound\">") &&
                  slice_is(s, e[1].close_start, e[1].end, "</span>") &&
                  slice_is(s, e[0].close_start, e[0].end, "</div>") &&
                  acount(e[2].attributes) == 2 &&
                  slice_is(s, e[2].attributes[1].name_start, e[2].attributes[1].name_end, "data-event-click") &&
                  slice_is(s, e[2].attributes[1].value_start, e[2].attributes[1].value_end, "emit('Resume')");
    test_check(elements && ranges, "ui source outlines elements with tag, id, classes, parent, lines and byte ranges");
    char *value = elements ? ui_source_attribute_value(s, 1, "data-if") : NULL;
    test_check(value && !strcmp(value, "save.sound"), "ui source reads an attribute's value");
    free(value);

    const ui_source_rule *r = s->rules;
    bool rules = acount(s->rules) == 3 && !strcmp(r[0].selector, ".card") &&
                 r[0].parent == -1 && r[0].line == 8 &&
                 !strcmp(r[1].selector, "&:hover") && r[1].parent == 0 &&
                 !strcmp(r[2].selector, ".dark &") && r[2].parent == 0 &&
                 r[2].line == 11 && acount(r[0].declarations) == 1 &&
                 slice_is(
                     s,
                     r[0].declarations[0].property_start,
                     r[0].declarations[0].property_end,
                     "color"
                 ) &&
                 slice_is(
                     s,
                     r[0].declarations[0].value_start,
                     r[0].declarations[0].value_end,
                     "var(--primary)"
                 ) &&
                 slice_is(s, r[1].start, r[1].end, "&:hover { opacity: 0.5; }");
    test_check(rules, "ui source outlines nested rules with selectors, declarations and ranges");
    int div = ui_source_element_at(s, e[1].start + 3);
    test_check(div == 1 && ui_source_element_at(s, e[0].start + 1) == 0, "ui source finds the innermost element at an offset");
    test_check(!acount(s->diagnostics), "a valid ui asset has no diagnostics, Sass nesting included");
    int line, column;
    ui_source_position(s, e[1].start, &line, &column);
    test_check(
        line == 3 && column == 5 && ui_source_offset(s, 3, 5) == e[1].start,
        "ui source converts offsets to lines and columns and back"
    );
    ui_source_free(s);

    s = ui_source_parse(":root { --x: 1dp; }\n.a { width: var(--x); }\n", true);
    test_check(
        acount(s->rules) == 2 && acount(s->blocks) == 1 &&
            s->blocks[0].language == UI_SOURCE_RCSS && !acount(s->diagnostics),
        "ui source parses a whole stylesheet"
    );
    ui_source_free(s);
}

static void diagnostics()
{
    ui_source *s = ui_source_parse(
        "rml: |\n"
        "  <div>\n"
        "    <span>x</div>\n"
        "  </span>\n"
        "rcss: |\n"
        "  .a { color: red; }\n"
        "  .b { color: ; \n",
        false
    );
    static const char *const EXPECTED[] = {
        "3:5 <span> is not closed",
        "4:3 </span> closes no open element",
        "7:3 unexpected \".b { color: ;\"",
    };
    bool ok = acount(s->diagnostics) == 3;
    for (int i = 0; i < acount(s->diagnostics); i++) {
        const ui_source_diagnostic *d = &s->diagnostics[i];
        char got[256];
        snprintf(got, sizeof got, "%d:%d %s", d->line, d->column, d->message);
        if (i >= 3 || strcmp(got, EXPECTED[i])) {
            printf("  diagnostic %s\n", got);
            ok = false;
        }
    }
    test_check(ok, "ui source reports markup and style syntax errors at their file lines");
    ui_source_free(s);

    s = ui_source_parse("rml: '<div/>'\nstyle: dark\n", false);
    ok = acount(s->diagnostics) == 1 &&
         strstr(s->diagnostics[0].message, "unknown key style");
    test_check(ok, "ui source reports what ui_document_read rejects");
    ui_source_free(s);
}

static ui_source_project sample_project()
{
    ui_source_project p = { 0 };
    const char *const widgets[] = { "MenuButton", "MenuCheck", "TextField" };
    const char *const classes[] = { "card", "wide", "title" };
    const char *const variables[] = { "--primary", "--muted" };
    const char *const properties[] = { "color", "opacity", "width", "height" };
    const char *const keys[] = { "sound", "name", "theme" };
    for (int i = 0; i < 3; i++) {
        ui_source_list_add(&p.widgets, widgets[i]);
        ui_source_list_add(&p.classes, classes[i]);
        ui_source_list_add(&p.save_keys, keys[i]);
    }
    for (int i = 0; i < 2; i++) {
        ui_source_list_add(&p.variables, variables[i]);
    }
    for (int i = 0; i < 4; i++) {
        ui_source_list_add(&p.properties, properties[i]);
    }
    return p;
}

/* The labels completion offers where `text` has its '^', joined by spaces,
 * and the word it replaces. */
static void complete_at(const char *marked, bool stylesheet, const ui_source_project *p, char *out, size_t size)
{
    const char *bar = strchr(marked, '^');
    assert(bar);
    char text[512];
    snprintf(text, sizeof text, "%.*s%s", (int)(bar - marked), marked, bar + 1);
    ui_source *s = ui_source_parse(text, stylesheet);
    ui_source_completions c;
    ui_source_complete(s, (int)(bar - marked), p, &c);
    size_t used = (size_t)snprintf(out, size, "[%.*s]", c.end - c.start, text + c.start);
    for (int i = 0; i < acount(c.items); i++) {
        used += (size_t)snprintf(out + used, size - used, " %s", c.items[i].label);
    }
    ui_source_completions_free(&c);
    ui_source_free(s);
}

static void completion()
{
    ui_source_project p = sample_project();
    static const struct
    {
        const char *text;
        bool stylesheet;
        const char *expected;
    } cases[] = {
        { "rml: |\n  <sp^", false, "[sp] span" },
        { "rml: |\n  <div data-i^", false, "[data-i] data-if" },
        { "rml: |\n  <div data-ev^", false, "[data-ev] data-event-blur data-event-change data-event-click data-event-dblclick data-event-focus data-event-keydown data-event-keyup data-event-mousedown data-event-mouseout data-event-mouseover data-event-mouseup data-event-submit" },
        { "rml: |\n  <use u^", false, "[u] ui" },
        { "rml: |\n  <use ui=\"Menu^\"/>", false, "[Menu] MenuButton MenuCheck" },
        { "rml: |\n  <div class=\"card w^\">", false, "[w] wide" },
        { "rml: |\n  <div data-class-t^", false, "[t] title" },
        { "rml: |\n  <div data-if=\"save.th^\">", false, "[th] theme" },
        { "rml: |\n  <p>{{ save.^", false, "[] name sound theme" },
        { "rml: |\n  <div style=\"wi^\">", false, "[wi] width" },
        { "rml: |\n  <div style=\"color: var(--m^\">", false, "[--m] --muted" },
        { "rml: <p/>\nrcss: |\n  .ca^ { }", false, "[ca] card" },
        { "rml: <p/>\nrcss: |\n  .a {\n    op^\n  }", false, "[op] opacity" },
        { "rml: <p/>\nrcss: |\n  .a { color: var(--p^) }", false, "[--p] --primary" },
        { ".a { &:hover { he^ } }", true, "[he] height" },
        { "sp^ { }", true, "[sp] span" },
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        char got[512];
        complete_at(cases[i].text, cases[i].stylesheet, &p, got, sizeof got);
        if (strcmp(got, cases[i].expected)) {
            printf(
                "  completing %s\n    got      %s\n    expected %s\n",
                cases[i].text,
                got,
                cases[i].expected
            );
            ok = false;
        }
    }
    test_check(ok, "ui source completes tags, attributes, widgets, classes, save keys, properties and custom properties");
    ui_source_project_free(&p);
}

/* `text` after the edits one operation makes. */
static char *edited(const char *text, text_edit *edits)
{
    char *out = text_edits_apply(text, edits, acount(edits));
    ui_source_edits_free(edits);
    return out;
}

static bool expect_text(const char *label, const char *got, const char *expected)
{
    bool ok = got && !strcmp(got, expected);
    if (!ok) {
        printf("  %s\n--- got\n%s--- expected\n%s---\n", label, got ? got : "(null)", expected);
    }
    return ok;
}

static const char EDIT_SAMPLE[] = "rml: |\n"
                                  "  <div id=\"panel\" class=\"card\">\n"
                                  "    <span>A</span>\n"
                                  "    <img src=\"a.png\"/>\n"
                                  "  </div>\n"
                                  "  <p>Tail</p>\n"
                                  "rcss: '.card { width: 1dp; }'\n";

typedef enum
{
    SET_ATTRIBUTE,
    REPLACE_ATTRIBUTE,
    REMOVE_ATTRIBUTE,
    ADD_CLASS,
    REMOVE_CLASS,
    SET_STYLE,
    INSERT_CHILD,
    OPEN_ELEMENT,
    INSERT_BEFORE,
    INSERT_AFTER,
    MOVE,
    DELETE,
} edit_case;

static bool apply_case(ui_source *s, edit_case which, text_edit **edits)
{
    char error[160];
    switch (which) {
        case SET_ATTRIBUTE:
            return ui_source_set_attribute(s, 1, "data-if", "save.sound", edits, error, sizeof error);
        case REPLACE_ATTRIBUTE:
            return ui_source_set_attribute(s, 0, "id", "main \"box\"", edits, error, sizeof error);
        case REMOVE_ATTRIBUTE:
            return ui_source_remove_attribute(s, 0, "id", edits, error, sizeof error);
        case ADD_CLASS:
            return ui_source_add_class(s, 0, "wide", edits, error, sizeof error);
        case REMOVE_CLASS:
            return ui_source_remove_class(s, 0, "card", edits, error, sizeof error);
        case SET_STYLE:
            return ui_source_set_style(s, 1, "width", "4dp", edits, error, sizeof error);
        case INSERT_CHILD:
            return ui_source_insert_element(s, 0, UI_SOURCE_INSIDE, "<p>New</p>", edits, error, sizeof error);
        case OPEN_ELEMENT:
            return ui_source_insert_element(s, 2, UI_SOURCE_INSIDE, "<span/>", edits, error, sizeof error);
        case INSERT_BEFORE:
            return ui_source_insert_element(s, 3, UI_SOURCE_BEFORE, "<hr/>", edits, error, sizeof error);
        case INSERT_AFTER:
            return ui_source_insert_element(s, 1, UI_SOURCE_AFTER, "<div>\n  <b/>\n</div>", edits, error, sizeof error);
        case MOVE:
            return ui_source_move_element(s, 3, 0, UI_SOURCE_INSIDE, edits, error, sizeof error);
        case DELETE:
            return ui_source_delete_element(s, 2, edits, error, sizeof error);
    }
    return false;
}

/* Edits within one line: a middle class, style declarations replaced and
 * removed, and elements that share their line. */
static void inline_edits()
{
    static const char SAMPLE_LINE[] = "rml: |\n"
                                      "  <p class=\"a b c\" style=\"width: "
                                      "1dp; height: 2dp;\">Text<b>x</b></p>\n"
                                      "  <hr/>\n";
    static const struct
    {
        const char *label;
        const char *expected;
    } cases[] = {
        { "remove a middle class", "  <p class=\"a c\" style=\"width: 1dp; height: 2dp;\">Text<b>x</b></p>\n  <hr/>\n" },
        { "replace a style property", "  <p class=\"a b c\" style=\"width: 3dp; height: 2dp;\">Text<b>x</b></p>\n  <hr/>\n" },
        { "remove the last style property", "  <p class=\"a b c\" style=\"width: 1dp;\">Text<b>x</b></p>\n  <hr/>\n" },
        { "remove the first style property", "  <p class=\"a b c\" style=\"height: 2dp;\">Text<b>x</b></p>\n  <hr/>\n" },
        { "insert a child before an inline close tag", "  <p class=\"a b c\" style=\"width: 1dp; height: 2dp;\">Text<b>x</b><i/></p>\n  <hr/>\n" },
        { "delete an element that shares its line", "  <p class=\"a b c\" style=\"width: 1dp; height: 2dp;\">Text</p>\n  <hr/>\n" },
        { "move an element after one on its own line", "  <p class=\"a b c\" style=\"width: 1dp; height: 2dp;\">Text</p>\n  <hr/>\n  <b>x</b>\n" },
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        ui_source *s = ui_source_parse(SAMPLE_LINE, false);
        text_edit *list = NULL;
        char error[160];
        bool applied = i == 0
                         ? ui_source_remove_class(s, 0, "b", &list, error, sizeof error)
                     : i == 1
                         ? ui_source_set_style(s, 0, "width", "3dp", &list, error, sizeof error)
                     : i == 2
                         ? ui_source_set_style(s, 0, "height", NULL, &list, error, sizeof error)
                     : i == 3
                         ? ui_source_set_style(s, 0, "width", NULL, &list, error, sizeof error)
                     : i == 4
                         ? ui_source_insert_element(s, 0, UI_SOURCE_INSIDE, "<i/>", &list, error, sizeof error)
                     : i == 5
                         ? ui_source_delete_element(s, 1, &list, error, sizeof error)
                         : ui_source_move_element(s, 1, 2, UI_SOURCE_AFTER, &list, error, sizeof error);
        char *text = applied ? edited(SAMPLE_LINE, list) : NULL;
        char expected[512];
        snprintf(expected, sizeof expected, "rml: |\n%s", cases[i].expected);
        ok &= expect_text(cases[i].label, text, expected);
        free(text);
        ui_source_free(s);
    }
    test_check(ok, "ui source edits classes, style declarations and elements that share a line");
}

static void edits()
{
    static const struct
    {
        edit_case which;
        const char *label;
        const char *expected_rml;
    } cases[] = {
        { SET_ATTRIBUTE, "set a new attribute", "  <div id=\"panel\" class=\"card\">\n    <span data-if=\"save.sound\">A</span>\n    <img src=\"a.png\"/>\n  </div>\n  <p>Tail</p>\n" },
        { REPLACE_ATTRIBUTE, "replace an attribute", "  <div id=\"main &quot;box&quot;\" class=\"card\">\n    <span>A</span>\n    <img src=\"a.png\"/>\n  </div>\n  <p>Tail</p>\n" },
        { REMOVE_ATTRIBUTE, "remove an attribute", "  <div class=\"card\">\n    <span>A</span>\n    <img src=\"a.png\"/>\n  </div>\n  <p>Tail</p>\n" },
        { ADD_CLASS, "add a class", "  <div id=\"panel\" class=\"card wide\">\n    <span>A</span>\n    <img src=\"a.png\"/>\n  </div>\n  <p>Tail</p>\n" },
        { REMOVE_CLASS, "remove the last class", "  <div id=\"panel\">\n    <span>A</span>\n    <img src=\"a.png\"/>\n  </div>\n  <p>Tail</p>\n" },
        { SET_STYLE, "set a style property", "  <div id=\"panel\" class=\"card\">\n    <span style=\"width: 4dp;\">A</span>\n    <img src=\"a.png\"/>\n  </div>\n  <p>Tail</p>\n" },
        { INSERT_CHILD, "insert a last child", "  <div id=\"panel\" class=\"card\">\n    <span>A</span>\n    <img src=\"a.png\"/>\n    <p>New</p>\n  </div>\n  <p>Tail</p>\n" },
        { OPEN_ELEMENT, "insert into a self-closing element", "  <div id=\"panel\" class=\"card\">\n    <span>A</span>\n    <img src=\"a.png\">\n      <span/>\n    </img>\n  </div>\n  <p>Tail</p>\n" },
        { INSERT_BEFORE, "insert before an element", "  <div id=\"panel\" class=\"card\">\n    <span>A</span>\n    <img src=\"a.png\"/>\n  </div>\n  <hr/>\n  <p>Tail</p>\n" },
        { INSERT_AFTER, "insert multi-line markup after an element", "  <div id=\"panel\" class=\"card\">\n    <span>A</span>\n    <div>\n      <b/>\n    </div>\n    <img src=\"a.png\"/>\n  </div>\n  <p>Tail</p>\n" },
        { MOVE, "move an element into another", "  <div id=\"panel\" class=\"card\">\n    <span>A</span>\n    <img src=\"a.png\"/>\n    <p>Tail</p>\n  </div>\n" },
        { DELETE, "delete an element", "  <div id=\"panel\" class=\"card\">\n    <span>A</span>\n  </div>\n  <p>Tail</p>\n" },
    };
    bool ok = true;
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        ui_source *s = ui_source_parse(EDIT_SAMPLE, false);
        text_edit *list = NULL;
        bool applied = apply_case(s, cases[i].which, &list);
        char *text = applied ? edited(EDIT_SAMPLE, list) : NULL;
        char expected[1024];
        snprintf(
            expected,
            sizeof expected,
            "rml: |\n%srcss: '.card { width: 1dp; }'\n",
            cases[i].expected_rml
        );
        ok &= expect_text(cases[i].label, text, expected);
        free(text);
        ui_source_free(s);
    }
    test_check(ok, "ui source edits attributes, classes, inline style and elements, keeping the block's indentation");
    inline_edits();

    ui_source *s = ui_source_parse(EDIT_SAMPLE, false);
    text_edit *list = NULL;
    ui_source_set_block(s, UI_SOURCE_RCSS, ".card {\n  width: 2dp;\n}\n", &list);
    char *text = edited(EDIT_SAMPLE, list);
    ok = expect_text("set a block", text, "rml: |\n  <div id=\"panel\" class=\"card\">\n    <span>A</span>\n    <img src=\"a.png\"/>\n  </div>\n  <p>Tail</p>\nrcss: |\n  .card {\n    width: 2dp;\n  }\n");
    free(text);
    ui_source_free(s);

    const char inline_rml[] = "rml: <div id=\"a\"><p/></div>\n";
    s = ui_source_parse(inline_rml, false);
    list = NULL;
    char error[160];
    ui_source_insert_element(s, 1, UI_SOURCE_AFTER, "<b/>", &list, error, sizeof error);
    text = edited(inline_rml, list);
    ok &= expect_text("edit an inline block", text, "rml: <div id=\"a\"><p/><b/></div>\n");
    free(text);
    ui_source_free(s);

    s = ui_source_parse(inline_rml, false);
    list = NULL;
    ui_source_insert_element(s, -1, UI_SOURCE_INSIDE, "<b/>", &list, error, sizeof error);
    ui_source_set_block(s, UI_SOURCE_RCSS, "b { color: red; }", &list);
    text = edited(inline_rml, list);
    ok &= expect_text("grow an inline block into a literal one", text, "rml: |-\n  <div id=\"a\"><p/></div>\n  <b/>\nrcss: 'b { color: red; }'\n");
    free(text);
    ui_source_free(s);
    test_check(ok, "ui source replaces whole blocks and rewrites inline scalars as the asset writer does");

    s = ui_source_parse(EDIT_SAMPLE, false);
    list = NULL;
    bool refused = !ui_source_move_element(s, 0, 1, UI_SOURCE_INSIDE, &list, error, sizeof error) &&
                   strstr(error, "itself") && !list &&
                   !ui_source_remove_class(s, 0, "missing", &list, error, sizeof error) &&
                   !ui_source_insert_element(s, -1, UI_SOURCE_AFTER, "<b/>", &list, error, sizeof error);
    test_check(refused, "ui source refuses edits it cannot make, with a reason");
    ui_source_free(s);
}

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(text, file);
    fclose(file);
}

static void references()
{
    char root[] = "/tmp/chibi-ui-source-XXXXXX", folder[96], path[160];
    assert(mkdtemp(root));
    snprintf(folder, sizeof folder, "%s/ui", root);
    assert(!mkdir(folder, 0700));
    snprintf(path, sizeof path, "%s/theme", folder);
    assert(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/theme/tokens.rcss", folder);
    write_file(path, ":root { --primary: #fff; }\n.card { color: var(--primary); }\n");
    snprintf(path, sizeof path, "%s/Button.yaml", folder);
    write_file(path, "rml: <button class=\"card\"><slot/></button>\n");
    snprintf(path, sizeof path, "%s/Menu.yaml", folder);
    write_file(
        path,
        "rml: |\n"
        "  <div class=\"card\" data-if=\"save.open\">\n"
        "    <use ui=\"Button\">Go</use>\n"
        "  </div>\n"
        "rcss: '.card { border-color: var(--primary); }'\n"
    );
    ui_source_project p = { 0 };
    ui_source_project_scan(&p, root);
    bool scanned = acount(p.files) == 3 && acount(p.widgets) == 2 &&
                   !strcmp(p.widgets[0], "Button") && acount(p.classes) == 1 &&
                   !strcmp(p.classes[0], "card") && acount(p.variables) == 1 &&
                   !strcmp(p.variables[0], "--primary") &&
                   acount(p.save_keys) == 1 && !strcmp(p.save_keys[0], "open");
    test_check(scanned, "ui source learns a project's widgets, classes, custom properties and save keys");

    char *text = NULL;
    snprintf(path, sizeof path, "%s/Menu.yaml", folder);
    FILE *file = fopen(path, "rb");
    char buffer[512] = { 0 };
    fread(buffer, 1, sizeof buffer - 1, file);
    fclose(file);
    text = buffer;
    ui_source *s = ui_source_parse(text, false);
    ui_source_symbol widget = ui_source_symbol_at(s, (int)(strstr(text, "Button") - text) + 2);
    ui_source_symbol card = ui_source_symbol_at(s, (int)(strstr(text, "card") - text));
    ui_source_symbol primary = ui_source_symbol_at(s, (int)(strstr(text, "--primary") - text) + 3);
    bool symbols = widget.kind == UI_SYMBOL_WIDGET &&
                   !strcmp(widget.name, "Button") &&
                   card.kind == UI_SYMBOL_CLASS && !strcmp(card.name, "card") &&
                   primary.kind == UI_SYMBOL_VARIABLE &&
                   !strcmp(primary.name, "--primary");
    test_check(symbols, "ui source finds the widget, class or custom property at an offset");

    ui_source_location *found = NULL;
    ui_source_references(&p, &widget, &found);
    bool widgets = acount(found) == 2 && found[0].definition &&
                   strstr(found[0].path, "Button.yaml") && !found[1].definition &&
                   strstr(found[1].path, "Menu.yaml") && found[1].line == 3;
    ui_source_locations_free(found);
    found = NULL;
    ui_source_references(&p, &primary, &found);
    bool variables = acount(found) == 3 && found[0].definition &&
                     strstr(found[0].path, "tokens.rcss") && found[0].line == 1 &&
                     !found[1].definition && !found[2].definition;
    ui_source_locations_free(found);
    found = NULL;
    ui_source_references(&p, &card, &found);
    int definitions = 0;
    for (int i = 0; i < acount(found); i++) {
        definitions += found[i].definition;
    }
    bool classes = acount(found) == 4 && definitions == 2 && found[0].definition;
    ui_source_locations_free(found);
    test_check(widgets && variables && classes, "ui source lists a symbol's definitions, then its references, across the project");
    ui_source_free(s);
    ui_source_project_free(&p);
    char command[160];
    snprintf(command, sizeof command, "rm -rf %s", root);
    assert(system(command) == 0);
}

/* Every shipped ui asset and stylesheet parses without a diagnostic. */
static void shipped()
{
    static const char *const CONTENT[] = { "content",
                                           "projects/arena2d/content",
                                           "projects/SpaceCadet/content",
                                           "projects/studio/content" };
    bool ok = true;
    int files = 0;
    for (size_t c = 0; c < sizeof CONTENT / sizeof *CONTENT; c++) {
        ui_source_project p = { 0 };
        ui_source_project_scan(&p, CONTENT[c]);
        for (int i = 0; i < acount(p.files); i++) {
            FILE *file = fopen(p.files[i].path, "rb");
            assert(file);
            fseek(file, 0, SEEK_END);
            long size = ftell(file);
            fseek(file, 0, SEEK_SET);
            char *text = calloc(1, (size_t)size + 1);
            fread(text, 1, (size_t)size, file);
            fclose(file);
            ui_source *s = ui_source_parse(text, !p.files[i].name);
            for (int d = 0; d < acount(s->diagnostics); d++) {
                printf(
                    "  %s:%d:%d: %s\n",
                    p.files[i].path,
                    s->diagnostics[d].line,
                    s->diagnostics[d].column,
                    s->diagnostics[d].message
                );
                ok = false;
            }
            files++;
            ui_source_free(s);
            free(text);
        }
        ui_source_project_free(&p);
    }
    test_check(ok && files > 30, "every shipped ui asset and stylesheet parses without a syntax error");
}

void test_ui_source()
{
    highlighting();
    outline();
    diagnostics();
    completion();
    edits();
    references();
    shipped();
}
