#include "authoring/source_presentation.h"

#include "authoring/application_catalog.h"
#include "authoring/graph_catalog.h"
#include "authoring/source_metadata.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A presentation-only embedding: no native descriptors or execution services.
 * Six fixed source snapshots, no filesystem or runtime work. */
static const script_catalog_capability tick = {
    .id = 100,
    .name = "Tick",
    .kind = SCRIPT_CAPABILITY_EVENT,
};
static const script_catalog catalog = { .capabilities = &tick, .capability_count = 1 };
static script_syntax *syntax;
static script_analysis *analysis;
static authoring_source_presentation *view, *copy;
static char *metadata, *edited;
static native_capabilities *native;
static authoring_graph_catalog *palette;

static void cleanup()
{
    authoring_graph_catalog_free(palette);
    native_capabilities_free(native);
    palette = NULL;
    native = NULL;
    authoring_source_presentation_free(view);
    authoring_source_presentation_free(copy);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    free(metadata);
    free(edited);
    view = copy = NULL;
    analysis = NULL;
    syntax = NULL;
    metadata = edited = NULL;
}
static bool project(const char *text)
{
    syntax = script_parse(text);
    analysis = script_analyze(syntax, &catalog);
    view = authoring_source_presentation_new(analysis, &catalog, NULL);
    return view != NULL;
}
static const source_presentation_origin *literal(const authoring_source_presentation *p)
{
    for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
        if (o->kind == SOURCE_PRESENTATION_EXPRESSION && !o->pin &&
            o->operation == SCRIPT_EXPR_CONSTANT && o->type.storage == PIN_STR) {
            return o;
        }
    }
    return NULL;
}
static node *origin_node(authoring_source_presentation *p, const source_presentation_origin *o)
{
    graph *g = behavior_asset_find_graph(authoring_source_presentation_view(p), o->graph);
    return graph_node_by_id(g, o->node);
}
TEST_CASE(source_view_clone_owns_defaults_and_edit_origins)
{
    REQUIRE(project(
        "behavior View { text: string = \"hello\";"
        " function Pair(n: double) -> (total: double, message: string) {"
        " return (n, text); }"
        " on Tick { current := Pair(2); text = current.message; } }"
    ));
    REQUIRE(analysis->diagnostic_count == 0);
    const source_presentation_origin *o = literal(view);
    REQUIRE(o != NULL);
    node *n = origin_node(view, o);
    REQUIRE(!strcmp(n->output_pins[0].default_value.str, "hello"));
    const char *original_default = n->output_pins[0].default_value.str;
    copy = authoring_source_presentation_clone(view);
    const source_presentation_origin *owned = literal(copy);
    REQUIRE(owned != NULL);
    node *copied = origin_node(copy, owned);
    REQUIRE(copied->desc != n->desc);
    REQUIRE(copied->output_pins[0].default_value.str != original_default);
    REQUIRE(
        copied->desc->outputs[0].default_value.str ==
        copied->output_pins[0].default_value.str
    );
    authoring_source_presentation_free(view);
    view = NULL;
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    REQUIRE(!strcmp(copied->output_pins[0].default_value.str, "hello"));
    REQUIRE(!strcmp(copied->desc->name, "Literal"));
    REQUIRE(!copied->desc->fn && !copied->desc->execution_call);
    edited = authoring_source_presentation_replace(copy, owned, "\"goodbye\"");
    REQUIRE(edited && strstr(edited, "text: string = \"goodbye\""));
    REQUIRE(strstr(authoring_source_presentation_text(copy), "\"hello\""));
    syntax = script_parse(edited);
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(analysis->diagnostic_count == 0);
    /* Layout is optional and must never alter source meaning. */
    char *automatic = authoring_source_presentation_metadata(copy);
    REQUIRE(automatic != NULL);
    copied->x = 123;
    copied->y = 456;
    REQUIRE(!copied->placed);
    char *arranged = authoring_source_presentation_metadata(copy);
    REQUIRE(arranged && !strcmp(automatic, arranged));
    REQUIRE(copied->x == 123 && copied->y == 456);
    free(automatic);
    free(arranged);
    copied->x = 12;
    copied->y = 34;
    copied->placed = true;
    graph *g = behavior_asset_find_graph(
        authoring_source_presentation_view(copy),
        owned->graph
    );
    REQUIRE(graph_set_node_comment(g, copied, "retained comment", true) == 0);
    metadata = authoring_source_presentation_metadata(copy);
    REQUIRE(metadata && strstr(metadata, "\"version\":2"));
    copied->x = 0;
    REQUIRE(authoring_source_presentation_restore(copy, metadata));
    REQUIRE(copied->x == 12 && copied->y == 34 && copied->placed);
    REQUIRE(!strcmp(copied->comment, "retained comment") && copied->comment_hidden);
    char *stale = authoring_source_metadata_bind(metadata, edited);
    REQUIRE(stale != NULL);
    REQUIRE(!authoring_source_presentation_restore(copy, stale));
    free(stale);
    char *invalid = authoring_source_metadata_bind(
        "{\"version\":2,\"source_layout\":[{\"identity\":0}]}",
        authoring_source_presentation_text(copy)
    );
    REQUIRE(invalid != NULL);
    REQUIRE(!authoring_source_presentation_restore(copy, invalid));
    free(invalid);
    REQUIRE(copied->x == 12 && !strcmp(copied->comment, "retained comment"));
    behavior_asset *transferred = authoring_source_presentation_take_view(copy);
    copy = NULL;
    REQUIRE(!strcmp(copied->output_pins[0].default_value.str, "hello"));
    REQUIRE(!strcmp(copied->desc->name, "Literal"));
    behavior_asset_free(transferred);
    return true;
}
TEST_CASE(source_views_keep_named_invalid_drafts)
{
    const char *drafts[] = {
        "behavior Draft { on Tick { missing.x; } }",
        "behavior Draft { on Tick { x := Unknown(1); x.message; } }",
        "behavior Draft { text: string = \"draft\"; on Tick { text = ; } }",
    };
    for (int i = 0; i < 3; i++) {
        cleanup();
        REQUIRE(project(drafts[i]));
        REQUIRE(analysis->diagnostic_count > 0);
        REQUIRE(authoring_source_presentation_origin_count(view) > 0);
        copy = authoring_source_presentation_clone(view);
        authoring_source_presentation_free(view);
        view = NULL;
        REQUIRE(!strcmp(authoring_source_presentation_text(copy), drafts[i]));
    }
    cleanup();
    syntax = script_parse("");
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(authoring_source_presentation_new(analysis, &catalog, NULL) == NULL);
    return true;
}
TEST_CASE(source_view_locals_stay_in_their_callable)
{
    REQUIRE(project(
        "behavior View { count: double = 0;"
        " function Target(n: double) -> (result: double) {"
        " width := n * 8; if (n > 0) { nested := width - 1; return nested; }"
        " return width; }"
        " on Tick { width := Target(count); count = width; } }"
    ));
    REQUIRE(analysis->diagnostic_count == 0);
    behavior_asset *asset = authoring_source_presentation_view(view);
    int widths = 0, nested = 0;
    for (int i = 0; i < authoring_source_presentation_origin_count(view); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(view, i);
        if (o->kind != SOURCE_PRESENTATION_SYMBOL || o->pin) {
            continue;
        }
        const script_symbol *symbol = &analysis->symbols[o->record];
        if (symbol->kind != SCRIPT_LOCAL) {
            continue;
        }
        REQUIRE(o->function >= 0);
        REQUIRE(o->graph != asset->graph->graph_id);
        REQUIRE(o->function == analysis->scopes[symbol->scope].function);
        graph *g = behavior_asset_find_graph(asset, o->graph);
        REQUIRE(g != NULL);
        if (!strcmp(symbol->name, "width")) {
            widths++;
            REQUIRE(!strcmp(g->name, widths == 1 ? "Target" : "Tick"));
        } else if (!strcmp(symbol->name, "nested")) {
            nested++;
            REQUIRE(!strcmp(g->name, "Target"));
        }
    }
    REQUIRE(widths == 2 && nested == 1);
    REQUIRE(acount(asset->graph->nodes) == 2); /* count and its initial value. */
    return true;
}
TEST_CASE(source_view_formulas_preserve_readable_source)
{
    REQUIRE(project(
        "behavior View { count: double = 0;"
        " function Target(n: double) -> (result: double) {"
        " return (n * 8 + 2) / 2; }"
        " on Tick { count += (Target(count) - count) * (1 - 0.01); } }"
    ));
    REQUIRE(analysis->diagnostic_count == 0);
    const char *formulas[] = { "(n * 8 + 2) / 2",
                               "(Target(count) - count) * (1 - 0.01)" };
    int found = 0, calls = 0;
    for (int i = 0; i < authoring_source_presentation_origin_count(view); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(view, i);
        if (o->kind != SOURCE_PRESENTATION_EXPRESSION || o->pin) {
            continue;
        }
        if (o->operation == SCRIPT_EXPR_CALL) {
            calls++;
        }
        if (o->operation != SCRIPT_EXPR_BINARY) {
            continue;
        }
        int at = found++;
        REQUIRE(at < 2);
        REQUIRE(o->range.end - o->range.start == (int)strlen(formulas[at]));
        REQUIRE(!strncmp(
            authoring_source_presentation_text(view) + o->range.start,
            formulas[at],
            strlen(formulas[at])
        ));
        node *n = origin_node(view, o);
        REQUIRE(n->input_count == at && n->output_count == 1);
        edited = authoring_source_presentation_replace(view, o, "3.0");
        REQUIRE(edited != NULL);
        script_syntax *replacement = script_parse(edited);
        script_analysis *checked = script_analyze(replacement, &catalog);
        REQUIRE(checked->diagnostic_count == 0);
        script_analysis_free(checked);
        script_syntax_free(replacement);
        free(edited);
        edited = NULL;
    }
    REQUIRE(found == 2 && calls == 1); /* Target remains available for source edits. */
    cleanup();
    REQUIRE(project(
        "behavior View { count: double = 0;"
        " function Next() -> (result: double) { count += 1; return count; }"
        " on Tick { count = Next() + 2; } }"
    ));
    REQUIRE(analysis->diagnostic_count == 0);
    bool effectful_call = false;
    for (int i = 0; i < authoring_source_presentation_origin_count(view); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(view, i);
        if (o->kind == SOURCE_PRESENTATION_EXPRESSION && !o->pin &&
            o->operation == SCRIPT_EXPR_CALL && (o->effects & SCRIPT_EFFECT_WRITE)) {
            effectful_call = true;
        }
    }
    REQUIRE(effectful_call);
    return true;
}
static bool native_palette_composition_keeps_declared_capabilities()
{
    char error[512] = { 0 };
    native = authoring_application_catalog_new(true, error, sizeof error);
    REQUIRE(native != NULL);
    palette = authoring_graph_catalog_new(
        (authoring_graph_catalog_config){
            .native = native,
            .types = GAME_TYPE_PACK,
            .components = { GAME_REGISTRY_COMPONENTS, GAME_REGISTRY_COMPONENT_COUNT } },
        error,
        sizeof error
    );
    REQUIRE(palette != NULL);
    const script_catalog *declarations = native_capabilities_catalog(native);
    native_capabilities_free(native);
    native = NULL;
    int count;
    const registry_pack *const *packs = authoring_graph_catalog_packs(palette, &count);
    REQUIRE(registry_validate_packs(packs, count));
    for (int i = 0; i < declarations->capability_count; i++) {
        const script_catalog_capability *capability = &declarations->capabilities[i];
        const char *name = capability->qualified_name ? capability->qualified_name
                                                      : capability->name;
        const node_desc *descriptor = NULL;
        for (int j = 0; j < count && !descriptor; j++) {
            for (int k = 0; k < packs[j]->node_count; k++) {
                if (!strcmp(packs[j]->nodes[k]->name, name)) {
                    descriptor = packs[j]->nodes[k];
                    break;
                }
            }
        }
        REQUIRE(descriptor != NULL);
        if (capability->category) {
            REQUIRE(
                descriptor->category && !strcmp(descriptor->category, capability->category)
            );
        }
        if (capability->keywords) {
            REQUIRE(
                descriptor->keywords && !strcmp(descriptor->keywords, capability->keywords)
            );
        }
        if (capability->description) {
            REQUIRE(
                descriptor->description &&
                !strcmp(descriptor->description, capability->description)
            );
        }
        if (capability->component && descriptor->metadata_only) {
            const script_catalog_type
                *type = script_catalog_type_by_id(declarations, capability->component);
            bool matching = type && descriptor->component &&
                            descriptor->component->id == type->component_id;
            if (!matching) {
                fprintf(
                    stderr,
                    "Palette component mismatch %s: nominal=%" PRIu64
                    " ECS=%" PRIu64 " attached=%" PRIu64 "\n",
                    name,
                    capability->component,
                    type ? type->component_id : 0,
                    descriptor->component ? descriptor->component->id : 0
                );
            }
            REQUIRE(type && type->component_id);
            REQUIRE(matching);
            REQUIRE(descriptor->type == descriptor->component->type);
        }
        if (descriptor->metadata_only) {
            REQUIRE(
                !descriptor->fn && !descriptor->read && !descriptor->read_inputs &&
                !descriptor->flow && !descriptor->lower && !descriptor->foldable
            );
        }
    }
    authoring_graph_catalog_free(palette);
    palette = NULL;
    return true;
}
void test_source_presentation()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_view_clone_owns_defaults_and_edit_origins);
    RUN_TEST_CASE(source_views_keep_named_invalid_drafts);
    RUN_TEST_CASE(source_view_locals_stay_in_their_callable);
    RUN_TEST_CASE(source_view_formulas_preserve_readable_source);
    RUN_TEST_CASE(native_palette_composition_keeps_declared_capabilities);
    pu_clear_setup();
}
