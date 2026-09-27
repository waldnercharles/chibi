/* The visual designer (workstream H): each Hierarchy and Details change
 * writes the document's text as expected, keeping its formatting, as one
 * undo step; and a selection maps between a shown element's data-src, the
 * outline's indexes and the Text Editor's cursor offsets. */
#include "editor/documents/ui_design.h"
#include "editor/studio/studio_internal.h"
#include "render/ui.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "test/suite_internal.h"
#include "ui/rml.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char ASSET[] = "rml: |\n"
                            "  <div id=\"root\" class=\"panel\">\n"
                            "    <span class=\"title\">Title</span>\n"
                            "    <div class=\"row\">\n"
                            "      <button id=\"ok\">OK</button>\n"
                            "      <button id=\"cancel\">Cancel</button>\n"
                            "    </div>\n"
                            "  </div>\n"
                            "rcss: |\n"
                            "  .panel { display: block; width: 200dp; }\n"
                            "  .row button { color: #ff0000; }\n";

static const char RML[] = "<div id=\"root\" class=\"panel\">\n"
                          "  <span class=\"title\">Title</span>\n"
                          "  <div class=\"row\">\n"
                          "    <button id=\"ok\">OK</button>\n"
                          "    <button id=\"cancel\">Cancel</button>\n"
                          "  </div>\n"
                          "</div>\n";

static const char RCSS[] = ".panel { display: block; width: 200dp; }\n"
                           ".row button { color: #ff0000; }\n";

enum
{
    ROOT,
    TITLE,
    ROW,
    OK,
    CANCEL,
};

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file && fputs(text, file) >= 0 && fclose(file) == 0);
}

/* The document opened fresh from `text`. */
static ui_buffer *open_document(const char *folder, const char *name, const char *text)
{
    char path[PATH_MAX], error[256];
    snprintf(path, sizeof path, "%s/%s", folder, name);
    write_text(path, text);
    ui_buffer *b = ui_buffer_open(path, error, sizeof error);
    assert(b);
    return b;
}

/* Whether a change left `rml` and `rcss` as the parts, one undo step deep,
 * and undoing it brings back the text before. */
static bool one_step(ui_buffer *b, const char *rml, const char *rcss, const char *before_rml, const char *before_rcss)
{
    bool written = !strcmp(ui_buffer_text(b, UI_PART_RML), rml) &&
                   !strcmp(ui_buffer_text(b, UI_PART_RCSS), rcss) &&
                   acount(b->undo) == 1;
    if (!written) {
        printf(
            "  rml:\n%s  rcss:\n%s  steps %d\n",
            ui_buffer_text(b, UI_PART_RML),
            ui_buffer_text(b, UI_PART_RCSS),
            acount(b->undo)
        );
    }
    ui_buffer_undo(b);
    return written && !strcmp(ui_buffer_text(b, UI_PART_RML), before_rml) &&
           !strcmp(ui_buffer_text(b, UI_PART_RCSS), before_rcss);
}

static bool markup_step(ui_buffer *b, const char *rml)
{
    return one_step(b, rml, RCSS, RML, RCSS);
}

static void moves(const char *folder)
{
    ui_buffer *b = open_document(folder, "Moves.yaml", ASSET);
    char error[192] = "";
    test_check(
        ui_design_move(b, CANCEL, OK, UI_SOURCE_BEFORE, error, sizeof error) == OK &&
            markup_step(
                b,
                "<div id=\"root\" class=\"panel\">\n"
                "  <span class=\"title\">Title</span>\n"
                "  <div class=\"row\">\n"
                "    <button id=\"cancel\">Cancel</button>\n"
                "    <button id=\"ok\">OK</button>\n"
                "  </div>\n"
                "</div>\n"
            ),
        "Hierarchy: dragging a row above its sibling reorders the markup, "
        "selecting the moved element"
    );
    test_check(
        ui_design_move(b, TITLE, ROW, UI_SOURCE_INSIDE, error, sizeof error) == CANCEL &&
            markup_step(
                b,
                "<div id=\"root\" class=\"panel\">\n"
                "  <div class=\"row\">\n"
                "    <button id=\"ok\">OK</button>\n"
                "    <button id=\"cancel\">Cancel</button>\n"
                "    <span class=\"title\">Title</span>\n"
                "  </div>\n"
                "</div>\n"
            ),
        "Hierarchy: dropping a row onto another reparents it after the "
        "target's children, indented"
    );
    test_check(
        ui_design_move(b, OK, ROW, UI_SOURCE_AFTER, error, sizeof error) == CANCEL &&
            markup_step(
                b,
                "<div id=\"root\" class=\"panel\">\n"
                "  <span class=\"title\">Title</span>\n"
                "  <div class=\"row\">\n"
                "    <button id=\"cancel\">Cancel</button>\n"
                "  </div>\n"
                "  <button id=\"ok\">OK</button>\n"
                "</div>\n"
            ),
        "Hierarchy: dropping below a parent moves the element out, at the "
        "parent's indentation"
    );
    int before = acount(b->undo);
    test_check(
        ui_design_move(b, ROW, OK, UI_SOURCE_INSIDE, error, sizeof error) == -1 &&
            error[0] && acount(b->undo) == before &&
            !strcmp(ui_buffer_text(b, UI_PART_RML), RML),
        "Hierarchy: an element cannot move into itself, and the refusal leaves "
        "no step"
    );
    ui_buffer_free(b);
}

static void structure(const char *folder)
{
    ui_buffer *b = open_document(folder, "Structure.yaml", ASSET);
    char error[192] = "";
    test_check(
        ui_design_insert(b, ROW, UI_SOURCE_INSIDE, "<use ui=\"Chip\"/>", error, sizeof error) ==
                5 &&
            markup_step(
                b,
                "<div id=\"root\" class=\"panel\">\n"
                "  <span class=\"title\">Title</span>\n"
                "  <div class=\"row\">\n"
                "    <button id=\"ok\">OK</button>\n"
                "    <button id=\"cancel\">Cancel</button>\n"
                "    <use ui=\"Chip\"/>\n"
                "  </div>\n"
                "</div>\n"
            ),
        "Library: a widget added to a container is its last child, on its own "
        "line"
    );
    test_check(
        ui_design_duplicate(b, OK, error, sizeof error) == CANCEL &&
            markup_step(
                b,
                "<div id=\"root\" class=\"panel\">\n"
                "  <span class=\"title\">Title</span>\n"
                "  <div class=\"row\">\n"
                "    <button id=\"ok\">OK</button>\n"
                "    <button id=\"ok\">OK</button>\n"
                "    <button id=\"cancel\">Cancel</button>\n"
                "  </div>\n"
                "</div>\n"
            ),
        "Hierarchy: Duplicate copies the element after itself and selects the "
        "copy"
    );
    test_check(
        ui_design_wrap(b, ROW, "div", error, sizeof error) == ROW &&
            markup_step(
                b,
                "<div id=\"root\" class=\"panel\">\n"
                "  <span class=\"title\">Title</span>\n"
                "  <div>\n"
                "    <div class=\"row\">\n"
                "      <button id=\"ok\">OK</button>\n"
                "      <button id=\"cancel\">Cancel</button>\n"
                "    </div>\n"
                "  </div>\n"
                "</div>\n"
            ),
        "Hierarchy: Wrap in Div puts the element and its children one level "
        "deeper inside a new div"
    );
    test_check(
        ui_design_delete(b, OK, error, sizeof error) == OK &&
            markup_step(
                b,
                "<div id=\"root\" class=\"panel\">\n"
                "  <span class=\"title\">Title</span>\n"
                "  <div class=\"row\">\n"
                "    <button id=\"cancel\">Cancel</button>\n"
                "  </div>\n"
                "</div>\n"
            ),
        "Hierarchy: Delete removes the element's lines and selects its next "
        "sibling"
    );
    test_check(
        ui_design_delete(b, CANCEL, error, sizeof error) == OK &&
            ui_design_delete(b, OK, error, sizeof error) == ROW &&
            acount(b->undo) == 2,
        "Hierarchy: deleting a last child selects its previous sibling, then "
        "its parent"
    );
    ui_buffer_undo(b);
    ui_buffer_undo(b);
    ui_buffer_free(b);
}

static void attributes(const char *folder)
{
    ui_buffer *b = open_document(folder, "Attributes.yaml", ASSET);
    char error[192] = "";
    const char *renamed = "<div id=\"root\" class=\"panel\">\n"
                          "  <span class=\"title\">Title</span>\n"
                          "  <div class=\"row\">\n"
                          "    <button id=\"confirm\">OK</button>\n"
                          "    <button id=\"cancel\">Cancel</button>\n"
                          "  </div>\n"
                          "</div>\n";
    test_check(
        ui_design_set_attribute(b, OK, "id", "confirm", error, sizeof error) &&
            markup_step(b, renamed),
        "Details: editing the id rewrites its value alone"
    );
    const char *classed = "<div id=\"root\" class=\"panel\">\n"
                          "  <span class=\"title big\">Title</span>\n"
                          "  <div class=\"row\">\n"
                          "    <button id=\"ok\">OK</button>\n"
                          "    <button id=\"cancel\">Cancel</button>\n"
                          "  </div>\n"
                          "</div>\n";
    test_check(
        ui_design_class(b, TITLE, "big", true, error, sizeof error) &&
            markup_step(b, classed),
        "Details: adding a class chip appends it to the class attribute"
    );
    const char *unclassed = "<div id=\"root\" class=\"panel\">\n"
                            "  <span>Title</span>\n"
                            "  <div class=\"row\">\n"
                            "    <button id=\"ok\">OK</button>\n"
                            "    <button id=\"cancel\">Cancel</button>\n"
                            "  </div>\n"
                            "</div>\n";
    test_check(
        ui_design_class(b, TITLE, "title", false, error, sizeof error) &&
            markup_step(b, unclassed),
        "Details: removing the last class chip removes the attribute"
    );
    const char *bound = "<div id=\"root\" class=\"panel\">\n"
                        "  <span class=\"title\">Title</span>\n"
                        "  <div class=\"row\">\n"
                        "    <button id=\"ok\" "
                        "data-event-click=\"emit('Ok')\">OK</button>\n"
                        "    <button id=\"cancel\">Cancel</button>\n"
                        "  </div>\n"
                        "</div>\n";
    test_check(
        ui_design_set_attribute(b, OK, "data-event-click", "emit('Ok')", error, sizeof error) &&
            markup_step(b, bound),
        "Details: adding a binding writes it after the element's attributes"
    );
    const char *retyped = "<div id=\"root\" class=\"panel\">\n"
                          "  <span class=\"title\">Title</span>\n"
                          "  <div class=\"row\">\n"
                          "    <button id=\"ok\">Okay</button>\n"
                          "    <button id=\"cancel\">Cancel</button>\n"
                          "  </div>\n"
                          "</div>\n";
    test_check(
        ui_design_set_text(b, OK, "Okay", error, sizeof error) &&
            markup_step(b, retyped) &&
            !ui_design_set_text(b, ROW, "No", error, sizeof error) &&
            acount(b->undo) == 0,
        "Details: an element holding text alone is retyped; one holding "
        "elements is not"
    );
    ui_buffer_free(b);
}

/* A scrub's changes, joined, stay one step whose undo is the text before. */
static void styles(const char *folder)
{
    ui_buffer *b = open_document(folder, "Styles.yaml", ASSET);
    char error[192] = "";
    const char
        *styled = "<div id=\"root\" class=\"panel\" style=\"width: 130dp;\">\n"
                  "  <span class=\"title\">Title</span>\n"
                  "  <div class=\"row\">\n"
                  "    <button id=\"ok\">OK</button>\n"
                  "    <button id=\"cancel\">Cancel</button>\n"
                  "  </div>\n"
                  "</div>\n";
    bool scrubbed = ui_design_set_style(b, ROOT, "width", "120dp", false, error, sizeof error) &&
                    ui_design_set_style(b, ROOT, "width", "125dp", true, error, sizeof error) &&
                    ui_design_set_style(b, ROOT, "width", "130dp", true, error, sizeof error);
    test_check(scrubbed && markup_step(b, styled), "Details: a scrubbed inline style value is one undo step");
    const char *rule = ".panel { display: block; width: 240dp; }\n"
                       ".row button { color: #ff0000; }\n";
    bool picked = ui_design_set_declaration(b, 0, 1, "220dp", false, error, sizeof error) &&
                  ui_design_set_declaration(b, 0, 1, "240dp", true, error, sizeof error);
    test_check(picked && one_step(b, RML, rule, RML, RCSS), "Details: a matched rule's declaration value edits in the rcss block as one step");
    int steps = acount(b->undo);
    test_check(
        !ui_design_set_declaration(b, 0, 1, "1dp; color: red", false, error, sizeof error) &&
            acount(b->undo) == steps && !strcmp(ui_buffer_text(b, UI_PART_RCSS), RCSS),
        "Details: a value that would end its declaration is refused"
    );
    ui_buffer_free(b);
}

/* Insertions indent as the markup around them does, four spaces here. */
static void keeps_indentation(const char *folder)
{
    ui_buffer *b = open_document(
        folder,
        "Wide.yaml",
        "rml: |\n"
        "    <div class=\"a\">\n"
        "        <span>x</span>\n"
        "    </div>\n"
    );
    char error[192] = "";
    test_check(
        ui_design_insert(b, 0, UI_SOURCE_INSIDE, "<button>Button</button>", error, sizeof error) ==
                2 &&
            one_step(
                b,
                "<div class=\"a\">\n"
                "    <span>x</span>\n"
                "    <button>Button</button>\n"
                "</div>\n",
                "",
                "<div class=\"a\">\n"
                "    <span>x</span>\n"
                "</div>\n",
                ""
            ),
        "Library: an element added keeps the document's own indentation step"
    );
    ui_buffer_free(b);
}

/* ---------- Selection ---------- */

static int offset_of(const char *text, const char *needle)
{
    const char *at = strstr(text, needle);
    assert(at);
    return (int)(at - text);
}

static void selection_maps()
{
    ui_source *source = ui_source_parse_block(RML, UI_SOURCE_RML);
    test_check(
        ui_design_source_line("Card:12", "Card") == 12 &&
            !ui_design_source_line("Cards:3", "Card") &&
            !ui_design_source_line("Other:1", "Card"),
        "Selection: data-src names a line of the document's own asset only"
    );
    test_check(
        ui_design_child_on_line(source, -1, 1, 0) == ROOT &&
            ui_design_child_on_line(source, ROOT, 3, 0) == ROW &&
            ui_design_child_on_line(source, ROW, 4, 0) == OK &&
            ui_design_child_on_line(source, ROOT, 4, 0) == -1 &&
            ui_design_child_on_line(source, ROW, 4, 1) == -1,
        "Selection: a shown element's line and place under its parent name one "
        "outline index"
    );
    test_check(
        ui_design_element_at(source, offset_of(RML, "OK")) == OK &&
            ui_design_element_at(source, offset_of(RML, "    <button id=\"ok\"")) == OK &&
            ui_design_element_at(source, offset_of(RML, "  </div>")) == ROW &&
            ui_design_element_at(source, offset_of(RML, "class=\"title\"")) == TITLE,
        "Selection: the Text Editor's cursor selects the element it is in, or "
        "the one its line's space leads to"
    );
    const char *insert = "  <p>New</p>\n";
    int at = offset_of(RML, "  <div class=\"row\">");
    size_t length = strlen(RML) + strlen(insert) + 1;
    char *typed = malloc(length);
    snprintf(typed, length, "%.*s%s%s", at, RML, insert, RML + at);
    ui_source *after = ui_source_parse_block(typed, UI_SOURCE_RML);
    test_check(
        ui_design_follow(source, after, TITLE) == TITLE &&
            ui_design_follow(source, after, ROW) == ROW + 1 &&
            ui_design_follow(source, after, CANCEL) == CANCEL + 1 &&
            ui_design_follow(source, after, ROOT) == ROOT,
        "Selection: an undo or typing elsewhere leaves the selection on its "
        "element"
    );
    ui_source_free(after);
    free(typed);
    at = offset_of(RML, "title\"") + 5;
    length = strlen(RML) + 5;
    typed = malloc(length);
    snprintf(typed, length, "%.*s big%s", at, RML, RML + at);
    after = ui_source_parse_block(typed, UI_SOURCE_RML);
    test_check(
        ui_design_follow(source, after, TITLE) == TITLE &&
            ui_design_follow(source, after, OK) == OK,
        "Selection: a change inside the selected element's start tag keeps it "
        "selected"
    );
    ui_source_free(after);
    free(typed);
    ui_source_free(source);
}

/* A shown document: two elements on one line, a widget whose own element
 * is not the document's, and the use's child in the widget's slot. */
static const char
    CARD[] = "rml: |\n"
             "  <div id=\"card\">\n"
             "    <span class=\"a\">A</span><span class=\"b\">B</span>\n"
             "    <use ui=\"Chip\" id=\"chip\">\n"
             "      <span class=\"inside\">Slot</span>\n"
             "    </use>\n"
             "  </div>\n"
             "rcss: |\n"
             "  #card { display: block; width: 300dp; height: 200dp; }\n"
             "  span { display: inline-block; width: 20dp; height: 20dp; }\n";

static const char
    CHIP[] = "rml: |\n"
             "  <div class=\"chip\">\n"
             "    <span class=\"dot\"/>\n"
             "    <slot/>\n"
             "  </div>\n"
             "rcss: |\n"
             "  .chip { display: block; width: 100dp; height: 40dp; }\n";

static rml_element *query(rml_context *context, const char *selector)
{
    for (int i = 0; i < rml_context_document_count(context); i++) {
        rml_element *found = rml_element_query_selector(rml_context_document(context, i), selector);
        if (found) {
            return found;
        }
    }
    return NULL;
}

static void shown_maps(const char *folder)
{
    char ui[PATH_MAX], path[PATH_MAX];
    snprintf(ui, sizeof ui, "%s/ui", folder);
    assert(mkdir(ui, 0700) == 0);
    snprintf(path, sizeof path, "%s/Card.yaml", ui);
    write_text(path, CARD);
    snprintf(path, sizeof path, "%s/Chip.yaml", ui);
    write_text(path, CHIP);
    ecs_world *world = ecs_world_new();
    for (int i = 0; i < GAME_REGISTRY_COMPONENT_COUNT; i++) {
        assert(ecs_world_register_component(world, GAME_REGISTRY_COMPONENTS[i], NULL) == ECS_OK);
    }
    ui_world w = { .ecs = world };
    render_ui *shown = render_ui_new(folder);
    render_ui_set_overlay(shown, "Card", "dark");
    render_ui_update(shown, &w, 1280, 720, 1.0 / 60);
    rml_context *context = render_ui_context(shown);
    char error[256];
    snprintf(path, sizeof path, "%s/Card.yaml", ui);
    ui_buffer *b = ui_buffer_open(path, error, sizeof error);
    assert(b);
    ui_source *rml = ui_design_parse(b, UI_PART_RML);
    bool round_trip = acount(rml->elements) == 5;
    for (int i = 0; i < acount(rml->elements); i++) {
        rml_element *el = studio_designer_shown(context, rml, "Card", i);
        round_trip &= el && studio_designer_shown_source(context, rml, "Card", el) == i;
    }
    test_check(round_trip, "Selection: every element of the document maps to its shown element and back by data-src");
    rml_element *a = query(context, ".a"), *b_span = query(context, ".b"),
                *dot = query(context, ".dot"), *inside = query(context, ".inside");
    test_check(
        studio_designer_shown_source(context, rml, "Card", a) == 1 &&
            studio_designer_shown_source(context, rml, "Card", b_span) == 2 &&
            studio_designer_shown_source(context, rml, "Card", dot) == 3 &&
            studio_designer_shown_source(context, rml, "Card", inside) == 4,
        "Selection: elements on one line stay apart, a widget's own element "
        "picks its use, and slot content its own element"
    );
    ui_source_free(rml);
    ui_buffer_free(b);
    render_ui_free(shown);
    ecs_world_free(world);
    unlink(path);
    snprintf(path, sizeof path, "%s/Chip.yaml", ui);
    unlink(path);
    rmdir(ui);
}

void test_ui_designer()
{
    char pattern[] = "/tmp/chibi-ui-designer-XXXXXX", folder[PATH_MAX];
    assert(mkdtemp(pattern) && realpath(pattern, folder));
    moves(folder);
    structure(folder);
    attributes(folder);
    styles(folder);
    keeps_indentation(folder);
    selection_maps();
    shown_maps(folder);
    static const char *const FILES[] = { "Moves.yaml",
                                         "Structure.yaml",
                                         "Attributes.yaml",
                                         "Styles.yaml",
                                         "Wide.yaml" };
    for (size_t i = 0; i < sizeof FILES / sizeof *FILES; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", folder, FILES[i]);
        unlink(path);
    }
    rmdir(folder);
}
