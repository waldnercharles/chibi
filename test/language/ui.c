#include "assets/ui_document.h"
#include "assets/ui_source.h"
#include "runtime/ui/documents.h"
#include "runtime/ui/font.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdlib.h>
#include <string.h>

static ui_source *source, *formatted, *changed, *styles;
static ui_document document;
static text_edit *edits;
static ui_document_event_range *ranges;
static char *text, *replacement, *value;
static prepared_ui_document prepared_ui, decoded_ui;
static ui_font font, decoded_font;
static unsigned char *ui_bytes, *font_bytes;
static ui_source_resource *dependencies;
static ui_documents *prepared_table;

static void cleanup()
{
    prepared_ui_document_free(&prepared_ui);
    prepared_ui_document_free(&decoded_ui);
    ui_font_free(&font);
    ui_font_free(&decoded_font);
    ui_documents_free(prepared_table);
    prepared_table = NULL;
    free(ui_bytes);
    free(font_bytes);
    ui_bytes = font_bytes = NULL;
    ui_source_resources_free(dependencies);
    dependencies = NULL;
    ui_source_free(source);
    ui_source_free(formatted);
    ui_source_free(changed);
    ui_source_free(styles);
    source = formatted = changed = styles = NULL;
    ui_document_free(&document);
    ui_source_edits_free(edits);
    edits = NULL;
    afree(ranges);
    free(text);
    free(replacement);
    free(value);
    text = replacement = value = NULL;
}

TEST_CASE(ui_source_public_edit_preserves_markup_meaning)
{
    const char *markup = "<div class=\"card\"><span data-if=\"save.enabled\">"
                         "{{ save.title }}</span></div>";
    source = ui_source_parse(markup, UI_SOURCE_RML);
    REQUIRE(acount(source->diagnostics) == 0);
    REQUIRE(acount(source->elements) == 2);
    text = ui_source_format(markup, UI_SOURCE_RML);
    formatted = ui_source_parse(text, UI_SOURCE_RML);
    REQUIRE(acount(formatted->diagnostics) == 0);
    REQUIRE(acount(formatted->elements) == 2);
    REQUIRE(formatted->elements[1].parent == 0);
    value = ui_source_attribute_value(formatted, 1, "data-if");
    REQUIRE(value && !strcmp(value, "save.enabled"));
    free(value);
    value = NULL;
    REQUIRE(strstr(text, "{{ save.title }}") != NULL);
    char error[192];
    REQUIRE(
        ui_source_set_attribute(formatted, 0, "class", "wide & bright", &edits, error, sizeof error)
    );
    replacement = text_edits_apply(text, edits, acount(edits));
    ui_source_edits_free(edits);
    edits = NULL;
    ui_source_free(source);
    ui_source_free(formatted);
    source = formatted = NULL;
    free(text);
    text = NULL;
    changed = ui_source_parse(replacement, UI_SOURCE_RML);
    REQUIRE(acount(changed->diagnostics) == 0);
    value = ui_source_attribute_value(changed, 0, "class");
    REQUIRE(value && !strcmp(value, "wide & bright"));
    REQUIRE(strstr(replacement, "{{ save.title }}") != NULL);
    styles = ui_source_parse(".card { &:hover { opacity: .5; } }", UI_SOURCE_RCSS);
    REQUIRE(acount(styles->diagnostics) == 0);
    REQUIRE(acount(styles->rules) == 2);
    return true;
}

TEST_CASE(ui_document_public_update_keeps_last_valid_snapshot)
{
    char markup[] = "<button data-event-click=\"emit('Pulse')\" "
                    "data-keys=\"Space:Pulse\"/>";
    char error[192];
    REQUIRE(
        ui_document_read(markup, ".card { &:hover { opacity: .5; } }", NULL, NULL, &document, error, sizeof error)
    );
    markup[1] = 'X';
    REQUIRE(!strncmp(document.rml, "<button", 7));
    REQUIRE(strstr(document.rcss, ".card:hover { opacity: .5; }") != NULL);
    char *original = document.rml;
    REQUIRE(!ui_document_read("<div/>", ".bad {", NULL, NULL, &document, error, sizeof error));
    REQUIRE(document.rml == original);
    REQUIRE(strstr(document.rml, "emit('Pulse')") != NULL);
    ui_document_event_ranges(document.rml, &ranges);
    REQUIRE(acount(ranges) == 2);
    for (int i = 0; i < acount(ranges); i++) {
        REQUIRE(ranges[i].end - ranges[i].start == 5);
        REQUIRE(!strncmp(document.rml + ranges[i].start, "Pulse", 5));
    }
    return true;
}

TEST_CASE(prepared_ui_records_own_resources_and_reject_partial_input)
{
    char error[192];
    size_t length = 0;
    prepared_ui = (prepared_ui_document){
        .name = xstrdup("Hud"),
        .markup = xstrdup("<img src='content/art/a.png#nearest'/><span>Score</span>"),
        .style = xstrdup(".score { font-family: Inter; }"),
        .body = xstrdup("class='game'")
    };
    apush(prepared_ui.resources, ((ui_resource){ UI_RESOURCE_IMAGE, xstrdup("content/art/a.png") }));
    apush(prepared_ui.resources, ((ui_resource){ UI_RESOURCE_FONT, xstrdup("font/Inter/400/normal") }));
    REQUIRE(ui_document_prepared_write(&prepared_ui, &ui_bytes, &length, error, sizeof error));
    prepared_ui_document_free(&prepared_ui);
    size_t remaining = 8192;
    REQUIRE(
        ui_document_prepared_read_budget(ui_bytes, length, &remaining, &decoded_ui, error, sizeof error)
    );
    REQUIRE(!strcmp(decoded_ui.body, "class='game'") && acount(decoded_ui.resources) == 2);
    REQUIRE(
        strstr(decoded_ui.markup, "#nearest") &&
        !strcmp(decoded_ui.resources[1].key, "font/Inter/400/normal")
    );
    char *saved = decoded_ui.markup;
    size_t before = remaining;
    REQUIRE(
        !ui_document_prepared_read_budget(NULL, length, &remaining, &decoded_ui, error, sizeof error)
    );
    REQUIRE(
        !ui_document_prepared_read_budget(ui_bytes, length - 1, &remaining, &decoded_ui, error, sizeof error)
    );
    ui_bytes[8] = 2;
    REQUIRE(
        !ui_document_prepared_read_budget(ui_bytes, length, &remaining, &decoded_ui, error, sizeof error)
    );
    ui_bytes[8] = 1;
    REQUIRE(decoded_ui.markup == saved && remaining == before);
    size_t zero = 0;
    REQUIRE(
        !ui_document_prepared_read_budget(ui_bytes, length, &zero, &decoded_ui, error, sizeof error)
    );
    REQUIRE(decoded_ui.markup == saved && zero == 0);
    prepared_table = ui_documents_new_budget(&remaining, true);
    REQUIRE(prepared_table);
    REQUIRE(!ui_documents_add_owned_budget(prepared_table, &decoded_ui, &zero));
    REQUIRE(decoded_ui.markup == saved && zero == 0);
    REQUIRE(ui_documents_add_owned_budget(prepared_table, &decoded_ui, &remaining));
    REQUIRE(!decoded_ui.markup && ui_documents_count(prepared_table) == 1);
    REQUIRE(!strcmp(ui_documents_at(prepared_table, 0)->name, "Hud"));
    /* Neutral envelope validates extents, not font interpretation; real font
     * acceptance belongs to RmlUi staging. This one-table fixture is bounded. */
    unsigned char *face = xcalloc(28, 1);
    face[1] = 1;
    face[5] = 1;
    memcpy(face + 12, "head", 4);
    face[23] = 28;
    font = (ui_font){ .key = xstrdup("font/Test/400/normal"),
                      .family = xstrdup("Test"),
                      .data = face,
                      .length = 28,
                      .weight = 400,
                      .fallback = true };
    REQUIRE(ui_font_prepared_write(&font, &font_bytes, &length, error, sizeof error));
    ui_font_free(&font);
    REQUIRE(
        ui_font_prepared_read_budget(font_bytes, length, &remaining, &decoded_font, error, sizeof error)
    );
    REQUIRE(
        decoded_font.length == 28 && decoded_font.weight == 400 &&
        decoded_font.fallback && !strcmp(decoded_font.family, "Test")
    );
    const unsigned char *saved_face = decoded_font.data;
    before = remaining;
    REQUIRE(
        !ui_font_prepared_read_budget(font_bytes, length - 1, &remaining, &decoded_font, error, sizeof error)
    );
    REQUIRE(decoded_font.data == saved_face && remaining == before);
    ((unsigned char *)decoded_font.data)[23] = 29;
    unsigned char *invalid = NULL;
    size_t invalid_size = 0;
    REQUIRE(!ui_font_prepared_write(&decoded_font, &invalid, &invalid_size, error, sizeof error));
    REQUIRE(!invalid && invalid_size == 0);
    return true;
}
TEST_CASE(prepared_ui_dependencies_use_owned_syntax_facts)
{
    char error[192];
    REQUIRE(ui_source_resources(
        "<img src='content/art/a.png#nearest'/><span "
        "style='font-family:var(--face)'>Hello</span>",
        ":root { --face: Inter; } .a { font-family: Noto Sans, serif; "
        "background-image:url('content/art/b.png'); }",
        &dependencies,
        error,
        sizeof error
    ));
    bool image = false, inter = false, noto = false;
    for (int i = 0; i < acount(dependencies); i++) {
        image |= dependencies[i].kind == UI_SOURCE_RESOURCE_IMAGE_URL &&
                 !strcmp(dependencies[i].text, "content/art/a.png#nearest");
        inter |= dependencies[i].kind == UI_SOURCE_RESOURCE_FONT_FAMILY &&
                 !strcmp(dependencies[i].text, "Inter");
        noto |= dependencies[i].kind == UI_SOURCE_RESOURCE_FONT_FAMILY &&
                !strcmp(dependencies[i].text, "Noto Sans");
    }
    REQUIRE(image && inter && noto);
    ui_source_resource *saved = dependencies;
    REQUIRE(!ui_source_resources("<use widget='Card'/>", "", &dependencies, error, sizeof error));
    REQUIRE(dependencies == saved);
    REQUIRE(!ui_source_resources(
        "<span/>",
        "@font-face { font-family: Test; src:url('content/font/a.ttf'); }",
        &dependencies,
        error,
        sizeof error
    ));
    REQUIRE(dependencies == saved);
    return true;
}

void test_ui_authoring()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(ui_source_public_edit_preserves_markup_meaning);
    RUN_TEST_CASE(ui_document_public_update_keeps_last_valid_snapshot);
    RUN_TEST_CASE(prepared_ui_records_own_resources_and_reject_partial_input);
    RUN_TEST_CASE(prepared_ui_dependencies_use_owned_syntax_facts);
    pu_clear_setup();
}
