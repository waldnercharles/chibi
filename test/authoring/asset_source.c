#include "assets/asset_source.h"

#include "authoring/source_intelligence.h"
#include "graph/model/registry.h"
#include "support/array.h"
#include "support/asset_document.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TEST_CASE(asset_source_navigates_event_payloads)
{
    const char *text = "Payload :: behavior { on Tick :: (dt: float) { "
                       "first:=dt; second:=dt; } }";
    const script_catalog_parameter payload = { .name = "dt",
                                               .type = { .storage = PIN_F32 } };
    const script_catalog_capability event = { .id = 100,
                                              .name = "Tick",
                                              .kind = SCRIPT_CAPABILITY_EVENT,
                                              .parameters = &payload,
                                              .parameter_count = 1,
                                              .completion_outcome = -1 };
    const script_catalog catalog = { .capabilities = &event,
                                     .capability_count = 1,
                                     .tick_event = 100 };
    script_syntax *syntax = script_parse(text);
    script_analysis *analysis = script_analyze(syntax, &catalog);
    REQUIRE(analysis->valid);
    const authoring_document document = { .path = "Payload.chibi",
                                          .syntax = syntax,
                                          .analysis = analysis };
    authoring_navigation *navigation = authoring_navigation_new(&document, 1, &catalog);
    int offset = (int)(strstr(text, "first:=dt") - text) + 7;
    const authoring_reference *use = authoring_navigation_at(navigation, 0, offset);
    REQUIRE(use);
    const authoring_reference *definition = authoring_navigation_definition(navigation, use->target);
    REQUIRE(definition && definition->location.range.start == (int)(strstr(text, "dt:") - text));
    int references = 0;
    for (int i = 0; i < authoring_navigation_reference_count(navigation); i++) {
        const authoring_reference *reference = authoring_navigation_reference_at(navigation, i);
        references += reference->kind == AUTHORING_READ &&
                      authoring_target_equal(reference->target, use->target);
    }
    REQUIRE(references == 2);
    char *hover = authoring_source_hover(navigation, 0, offset);
    REQUIRE(hover && !strcmp(hover, "dt: float"));
    free(hover);
    authoring_navigation_free(navigation);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    return true;
}

/* Canonical source edits preserve values and source-owned helper computations. */
TEST_CASE(asset_source_reconcile_keeps_authored_text)
{
    char error[256];
    av_doc *doc = av_doc_new();
    av_value *root = av_obj(doc);
    av_doc_set_root(doc, root);
    av_obj_add_int(doc, root, "amount", 12);
    av_value *list = av_obj_add_arr(doc, root, "items");
    av_arr_append(list, av_sint(doc, 1));
    char *source = asset_document_write(root);
    REQUIRE(source);
    av_obj_set(doc, root, "amount", av_sint(doc, 24));
    av_arr_append(list, av_sint(doc, 2));
    char *edited = asset_source_reconcile(source, root, error, sizeof error);
    REQUIRE(edited);
    av_doc *check = asset_document_read(edited, error, sizeof error);
    REQUIRE(check && av_equals(av_doc_root(check), root));
    av_doc_free(check);
    free(edited);
    free(source);
    av_doc_free(doc);

    source = xstrdup(
        "package asset\nimport data \"game:assets\"\n"
        "// keep helper and formula\n"
        "Amount :: (base: int) -> int { return base * 2 }\n"
        "pub Build :: () -> byte[] { return data.Encode(data.Object(["
        "data.Field.Named(\"amount\", data.Value.Int(int64(Amount(6))))])) }\n"
    );
    doc = asset_document_read(source, error, sizeof error);
    REQUIRE(doc);
    edited = asset_source_reconcile(source, av_doc_root(doc), error, sizeof error);
    REQUIRE(edited && !strcmp(source, edited));
    free(edited);
    av_obj_add_arr(doc, av_doc_root(doc), "new_items");
    edited = asset_source_reconcile(source, av_doc_root(doc), error, sizeof error);
    /* An unsupported edit must not erase the helper/formula to serialize data. */
    REQUIRE(!edited || (strstr(edited, "Amount ::") && strstr(edited, "Amount(6)")));
    free(edited);
    av_doc_free(doc);
    free(source);
    return true;
}

/* Incomplete ordinary Chibi retains token highlighting and source diagnostics. */
TEST_CASE(asset_source_parses_incomplete_drafts)
{
    const char
        *text = "package asset\nimport data \"game:assets\"\n"
                "pub Build :: () -> byte[] { return data.Encode(data.Object(["
                "data.Field.Named(\"name\", data.Value.Text(\"unterminated";
    asset_source *source = asset_source_parse(text);
    REQUIRE(!source->document && acount(source->diagnostics) > 0);
    REQUIRE(acount(source->spans) > 0);
    int line = 0, column = 0;
    asset_source_position(source, (int)(strstr(text, "pub Build") - text), &line, &column);
    REQUIRE(line == 3 && column == 1);
    bool string = false;
    for (int i = 0; i < acount(source->spans); i++) {
        string |= !strcmp(source->spans[i].cls, "tok-string");
    }
    REQUIRE(string);
    asset_source_free(source);
    return true;
}

void test_asset_source()
{
    RUN_TEST_CASE(asset_source_navigates_event_payloads);
    RUN_TEST_CASE(asset_source_reconcile_keeps_authored_text);
    RUN_TEST_CASE(asset_source_parses_incomplete_drafts);
}
