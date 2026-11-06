#include "authoring/source_presentation.h"

#include "authoring/application_catalog.h"
#include "authoring/graph_catalog.h"
#include "graph/authoring/graph_edit.h"
#include "graph/authoring/layout.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

/* A presentation-only embedding: no native descriptors or execution services.
 * Fixed small source snapshots, no filesystem or runtime work. */
static const script_catalog_parameter duration = { .name = "Duration",
                                                   .type = { .storage = PIN_F64 } };
static const char *const completed[] = { "Completed" };
static const script_catalog_capability capabilities[] = {
    { .id = 100, .name = "Tick", .kind = SCRIPT_CAPABILITY_EVENT },
    { .id = 101,
      .name = "Delay",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .parameters = &duration,
      .parameter_count = 1,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_SUSPEND },
};
static const script_catalog catalog = { .capabilities = capabilities,
                                        .capability_count = 2 };
static script_syntax *syntax;
static script_analysis *analysis;
static authoring_source_presentation *view, *copy;
static char *metadata, *edited;
static native_capabilities *native;
static authoring_graph_catalog *palette;

static char *write_snapshot(yyjson_mut_doc *doc)
{
    char *out = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return out;
}
static yyjson_mut_doc *read_snapshot(const char *json)
{
    yyjson_doc *read = yyjson_read(json, strlen(json), 0);
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(read, NULL);
    yyjson_doc_free(read);
    return doc;
}
/* The snapshot bound to some other text. */
static char *stale_snapshot(const char *json)
{
    yyjson_mut_doc *doc = read_snapshot(json);
    yyjson_mut_obj_put(
        yyjson_mut_doc_get_root(doc),
        yyjson_mut_str(doc, "source_hash"),
        yyjson_mut_str(doc, "0000000000000000")
    );
    return write_snapshot(doc);
}
/* The snapshot with its first node record naming no origin. */
static char *unanchored_snapshot(const char *json)
{
    yyjson_mut_doc *doc = read_snapshot(json);
    yyjson_mut_val *layout = yyjson_mut_obj_get(yyjson_mut_doc_get_root(doc), "source_layout");
    yyjson_mut_val *record = yyjson_mut_arr_get(layout, 0);
    yyjson_mut_obj_put(record, yyjson_mut_str(doc, "identity"), yyjson_mut_uint(doc, 0));
    return write_snapshot(doc);
}

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
    REQUIRE(
        project("View :: behavior { text: string = \"hello\"; Pair :: (n: f64) -> (total: f64, message: string) { return (n, text); } on Tick :: () { current := Pair(2); text = current.message; } }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    const source_presentation_origin *o = literal(view);
    REQUIRE(o != NULL);
    node *n = origin_node(view, o);
    REQUIRE(!strcmp(pin_text(n->output_pins[0].default_value).bytes, "hello"));
    copy = authoring_source_presentation_clone(view);
    const source_presentation_origin *owned = literal(copy);
    REQUIRE(owned != NULL);
    node *copied = origin_node(copy, owned);
    REQUIRE(copied->desc != n->desc);
    /* Text is shared by reference, so the copy keeps the literal's text alive
     * after the original is freed below. */
    REQUIRE(
        pin_text(copied->desc->outputs[0].default_value).bytes ==
        pin_text(copied->output_pins[0].default_value).bytes
    );
    authoring_source_presentation_free(view);
    view = NULL;
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    REQUIRE(!strcmp(pin_text(copied->output_pins[0].default_value).bytes, "hello"));
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
    char *stale = stale_snapshot(metadata);
    REQUIRE(stale != NULL);
    REQUIRE(!authoring_source_presentation_restore(copy, stale));
    free(stale);
    char *invalid = unanchored_snapshot(metadata);
    REQUIRE(invalid != NULL);
    REQUIRE(!authoring_source_presentation_restore(copy, invalid));
    free(invalid);
    REQUIRE(copied->x == 12 && !strcmp(copied->comment, "retained comment"));
    behavior_asset *transferred = authoring_source_presentation_take_view(copy);
    copy = NULL;
    REQUIRE(!strcmp(pin_text(copied->output_pins[0].default_value).bytes, "hello"));
    REQUIRE(!strcmp(copied->desc->name, "Literal"));
    behavior_asset_free(transferred);
    return true;
}
TEST_CASE(source_views_keep_named_invalid_drafts)
{
    const char *drafts[] = {
        "Draft :: behavior { on Tick :: () { missing.x; } }",
        "Draft :: behavior { on Tick :: () { x := Unknown(1); x.message; } }",
        ("Draft :: behavior { text: string = \"draft\"; on Tick :: () { text = "
         "; } }"),
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
    REQUIRE(
        project("View :: behavior { count: f64 = 0; Target :: (n: f64) -> (result: f64) { width := n * 8; if (n > 0) { nested := width - 1; return nested; } return width; } on Tick :: () { width := Target(count); count = width; } }")
    );
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
TEST_CASE(source_view_entries_have_layout_priority)
{
    REQUIRE(
        project("View :: behavior { count: f64 = 0; Target :: (n: f64) -> (result: f64) { unused := 7; return n + 1; } on Tick :: () { unused := 8; count = Target(count); } }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    behavior_asset *asset = authoring_source_presentation_view(view);
    const char *names[] = { "Target", "Tick" };
    for (int i = 0; i < 2; i++) {
        graph *g = behavior_asset_find_graph_by_name(asset, names[i]);
        REQUIRE(g != NULL);
        node *entry = graph_node_by_id(g, g->entry_id);
        REQUIRE(entry != NULL);
        int values = 0;
        for (int j = 0; j < authoring_source_presentation_origin_count(view); j++) {
            const source_presentation_origin *o = authoring_source_presentation_origin_at(view, j);
            if (o->graph != g->graph_id || o->kind != SOURCE_PRESENTATION_SYMBOL ||
                o->pin || strcmp(analysis->symbols[o->record].name, "unused")) {
                continue;
            }
            node *n = origin_node(view, o);
            values++;
            REQUIRE(graph_layout_box(g, entry).band < graph_layout_box(g, n).band);
        }
        REQUIRE(values > 0);
        REQUIRE(entry->desc->metadata_only && !entry->desc->fn);
    }
    return true;
}
TEST_CASE(source_view_formulas_preserve_readable_source)
{
    REQUIRE(
        project("View :: behavior { count: f64 = 0; Target :: (n: f64) -> (result: f64) { return (n * 8 + 2) / 2; } on Tick :: () { count += (Target(count) - count) * (1 - 0.01); } }")
    );
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
    REQUIRE(
        project("View :: behavior { count: f64 = 0; Next :: () -> (result: f64) { count += 1; return count; } on Tick :: () { count = Next() + 2; } }")
    );
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
/* Select source-owned nodes by authored text, independently of descriptor
   names or allocation order. Structural block origins need no visible node. */
static const source_presentation_origin *source_origin(
    const authoring_source_presentation *p,
    source_presentation_kind kind,
    const char *text
)
{
    const char *source = authoring_source_presentation_text(p);
    const char *at = strstr(source, text);
    if (!at) {
        return NULL;
    }
    int start = (int)(at - source);
    const source_presentation_origin *found = NULL;
    for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
        if (o->kind == kind && o->node && !o->pin && o->range.start == start &&
            (!found || o->range.end < found->range.end)) {
            found = o;
        }
    }
    return found;
}
static node *statement_at(const char *text)
{
    const source_presentation_origin *o = source_origin(view, SOURCE_PRESENTATION_STATEMENT, text);
    return o ? origin_node(view, o) : NULL;
}
static bool flow_edge(const graph *g, const node *from, const char *output, const node *to, const char *input)
{
    if (!from || !to) {
        return false;
    }
    for (int i = 0; i < acount(g->edges); i++) {
        const edge *e = &g->edges[i];
        if (e->kind != GRAPH_EDGE_EXEC || e->from_id != from->id || e->to_id != to->id) {
            continue;
        }
        for (int out = 0; out < from->output_count; out++) {
            if (from->output_pins[out].id != e->from_pin_id ||
                strcmp(from->output_pins[out].name, output)) {
                continue;
            }
            for (int in = 0; in < to->input_count; in++) {
                if (to->input_pins[in].id == e->to_pin_id &&
                    !strcmp(to->input_pins[in].name, input)) {
                    return true;
                }
            }
        }
    }
    return false;
}
static bool no_outgoing_flow(const graph *g, const node *n)
{
    for (int i = 0; i < acount(g->edges); i++) {
        if (g->edges[i].kind == GRAPH_EDGE_EXEC && g->edges[i].from_id == n->id) {
            return false;
        }
    }
    return true;
}
TEST_CASE(source_view_linear_flow_follows_authored_order)
{
    REQUIRE(
        project("Counter :: behavior { count: f64 = 0; on Tick :: () { count = 1; count += 2; count *= 3; } }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    graph *g = behavior_asset_find_graph_by_name(authoring_source_presentation_view(view), "Tick");
    REQUIRE(g);
    node *first = statement_at("count = 1");
    node *second = statement_at("count += 2");
    node *third = statement_at("count *= 3");
    REQUIRE(first && second && third);
    REQUIRE(flow_edge(g, graph_node_by_id(g, g->entry_id), "Then", first, "Exec"));
    REQUIRE(flow_edge(g, first, "Then", second, "Exec"));
    REQUIRE(flow_edge(g, second, "Then", third, "Exec"));
    REQUIRE(g->return_id == 0);
    REQUIRE(no_outgoing_flow(g, third));
    for (int i = 0; i < acount(g->nodes); i++) {
        REQUIRE(strcmp(g->nodes[i]->desc->name, "Block"));
    }
    return true;
}
TEST_CASE(source_view_branch_terminals_do_not_rejoin)
{
    REQUIRE(
        project("Inventory :: behavior { items: f64 = 0; Inspect :: (limit: f64) -> (total: f64) { if items > limit { return 1; } else { items = 2; } items = 3; return items; } on Tick :: () {} }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    graph *g = behavior_asset_find_graph_by_name(authoring_source_presentation_view(view), "Inspect");
    REQUIRE(g);
    node *branch = statement_at("if items > limit");
    node *stop = statement_at("return 1");
    node *live = statement_at("items = 2");
    node *after = statement_at("items = 3");
    node *result = statement_at("return items");
    REQUIRE(branch && stop && live && after && result);
    REQUIRE(flow_edge(g, graph_node_by_id(g, g->entry_id), "Then", branch, "Exec"));
    REQUIRE(flow_edge(g, branch, "True", stop, "Exec"));
    REQUIRE(flow_edge(g, branch, "False", live, "Exec"));
    REQUIRE(flow_edge(g, live, "Then", after, "Exec"));
    REQUIRE(flow_edge(g, after, "Then", result, "Exec"));
    REQUIRE(no_outgoing_flow(g, stop) && no_outgoing_flow(g, result));
    REQUIRE(branch->output_count == 2);
    return true;
}
TEST_CASE(source_view_loop_break_and_continue_have_distinct_paths)
{
    REQUIRE(
        project("Inventory :: behavior { count: f64 = 0; on Tick :: () { while count < 10 { count += 1; if count == 2 { continue; } if count == 5 { break; } count += 3; } count = 20; } }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    graph *g = behavior_asset_find_graph_by_name(authoring_source_presentation_view(view), "Tick");
    REQUIRE(g);
    node *loop = statement_at("while count < 10");
    node *first = statement_at("count += 1");
    node *next = statement_at("continue");
    node *stop = statement_at("break");
    node *last = statement_at("count += 3");
    node *after = statement_at("count = 20");
    REQUIRE(loop && first && next && stop && last && after);
    REQUIRE(flow_edge(g, loop, "Body", first, "Exec"));
    REQUIRE(flow_edge(g, loop, "Completed", after, "Exec"));
    REQUIRE(flow_edge(g, next, "Then", loop, "Continue"));
    REQUIRE(flow_edge(g, stop, "Then", after, "Exec"));
    REQUIRE(flow_edge(g, last, "Then", loop, "Continue"));
    REQUIRE(!flow_edge(g, next, "Then", after, "Exec"));
    REQUIRE(!flow_edge(g, stop, "Then", loop, "Continue"));
    return true;
}
TEST_CASE(source_view_await_completion_precedes_following_statement)
{
    REQUIRE(
        project("Counter :: behavior { count: f64 = 0; on Tick :: () { count = 1; await Delay(0.1); count = 2; } }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    graph *g = behavior_asset_find_graph_by_name(authoring_source_presentation_view(view), "Tick");
    REQUIRE(g);
    node *before = statement_at("count = 1");
    node *wait = statement_at("await Delay");
    node *after = statement_at("count = 2");
    REQUIRE(before && wait && after);
    REQUIRE(!strcmp(wait->desc->name, "Await Delay"));
    for (int i = 0; i < wait->output_count; i++) {
        REQUIRE(wait->output_pins[i].kind == PIN_EXEC);
    }
    REQUIRE(flow_edge(g, before, "Then", wait, "Exec"));
    REQUIRE(flow_edge(g, wait, "Completed", after, "Exec"));
    REQUIRE(!flow_edge(g, before, "Then", after, "Exec"));
    return true;
}
TEST_CASE(source_view_handler_exits_are_explicit_and_completion_is_natural)
{
    REQUIRE(
        project("Counter :: behavior { count: f64 = 0; on Tick :: () { if count == 0 { return; } count = 2; } }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    graph *g = behavior_asset_find_graph_by_name(authoring_source_presentation_view(view), "Tick");
    REQUIRE(g && !g->return_id);
    node *branch = statement_at("if count == 0");
    node *stop = statement_at("return;");
    node *live = statement_at("count = 2");
    REQUIRE(branch && stop && live);
    REQUIRE(!strcmp(stop->desc->name, "End Event"));
    REQUIRE(flow_edge(g, branch, "True", stop, "Exec"));
    REQUIRE(flow_edge(g, branch, "False", live, "Exec"));
    REQUIRE(no_outgoing_flow(g, stop) && no_outgoing_flow(g, live));
    for (int i = 0; i < acount(g->nodes); i++) {
        REQUIRE(strcmp(g->nodes[i]->desc->name, "Return"));
    }
    return true;
}
/* A loop's node says when it counts down and when its range stops early. */
TEST_CASE(source_view_names_reverse_and_half_open_loops)
{
    REQUIRE(
        project("Counter :: behavior { count: int = 0; on Tick :: () { for < i in 0..<3 { count += i; } for i in 0..3 { count += i; } } }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    node *reverse = statement_at("for < i");
    node *forward = statement_at("for i");
    REQUIRE(reverse && forward);
    REQUIRE(!strcmp(reverse->desc->name, "For Reverse") && !strcmp(forward->desc->name, "For"));
    bool before = false, last = false;
    for (int i = 0; i < reverse->input_count; i++) {
        before |= !strcmp(reverse->input_pins[i].name, "Before");
    }
    for (int i = 0; i < forward->input_count; i++) {
        last |= !strcmp(forward->input_pins[i].name, "Last");
    }
    REQUIRE(before && last);
    return true;
}
static int named_pin(const pin_spec *pins, int count, pin_kind kind, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (pins[i].kind == kind && !strcmp(pins[i].name, name)) {
            return i;
        }
    }
    return -1;
}
TEST_CASE(source_view_set_nodes_keep_statement_and_value_origins)
{
    REQUIRE(
        project("Inventory :: behavior { items: f64 = 0; on Tick :: () { available := items + 1; items = 7; items += 3; saved := items; } }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    node *declared = statement_at("available :=");
    REQUIRE(declared);
    int locals = 0;
    for (int i = 0; i < authoring_source_presentation_origin_count(view); i++) {
        const source_presentation_origin *local = authoring_source_presentation_origin_at(view, i);
        if (local->kind == SOURCE_PRESENTATION_SYMBOL && !local->pin &&
            !strcmp(analysis->symbols[local->record].name, "available")) {
            locals++;
            REQUIRE(origin_node(view, local) == declared);
        }
    }
    REQUIRE(locals == 1);
    int available = named_pin(declared->output_pins, declared->output_count, PIN_DATA, "available");
    REQUIRE(available >= 0);
    const source_presentation_origin
        *initializer = source_origin(view, SOURCE_PRESENTATION_EXPRESSION, "items + 1");
    REQUIRE(initializer);
    graph *local_graph = behavior_asset_find_graph(
        authoring_source_presentation_view(view),
        initializer->graph
    );
    int consumers = 0;
    for (int i = 0; i < acount(local_graph->edges); i++) {
        consumers += local_graph->edges[i].kind == GRAPH_EDGE_DATA &&
                     local_graph->edges[i].from_id == initializer->node;
    }
    REQUIRE(consumers == 1);
    const source_presentation_origin *o = source_origin(view, SOURCE_PRESENTATION_STATEMENT, "items = 7");
    REQUIRE(o);
    node *set = origin_node(view, o);
    REQUIRE(!strcmp(set->desc->name, "Set items"));
    REQUIRE(set->desc->description && strstr(set->desc->description, "this behavior instance's"));
    REQUIRE(strstr(set->desc->description, "'items'"));
    REQUIRE(
        declared->desc->description &&
        strstr(declared->desc->description, "local variable 'available'")
    );
    bool described_read = false;
    for (int i = 0; i < acount(local_graph->nodes); i++) {
        node *candidate = local_graph->nodes[i];
        if (!strcmp(candidate->desc->name, "Get items")) {
            described_read = candidate->desc->description &&
                             strstr(candidate->desc->description, "Reads this behavior instance's");
        }
    }
    REQUIRE(described_read);
    int target = named_pin(set->input_pins, set->input_count, PIN_DATA, "Target");
    int value = named_pin(set->input_pins, set->input_count, PIN_DATA, "Value");
    REQUIRE(target < 0 && value >= 0);
    REQUIRE(set->input_count == 2);
    const source_presentation_origin *rhs = authoring_source_presentation_origin(
        view,
        o->graph,
        o->node,
        set->input_pins[value].id
    );
    REQUIRE(rhs && rhs->kind == SOURCE_PRESENTATION_EXPRESSION);
    edited = authoring_source_presentation_replace(view, rhs, "9");
    REQUIRE(edited && strstr(edited, "items = 9; items += 3"));
    const source_presentation_origin
        *lhs = source_origin(view, SOURCE_PRESENTATION_EXPRESSION, "items = 7");
    REQUIRE(lhs && lhs->writable);
    REQUIRE(origin_node(view, lhs) == set);
    free(edited);
    edited = authoring_source_presentation_replace(view, lhs, "available");
    REQUIRE(edited && strstr(edited, "available = 7; items += 3"));
    REQUIRE(!strcmp(statement_at("items += 3")->desc->name, "Add to items"));
    copy = authoring_source_presentation_clone(view);
    const source_presentation_origin
        *cloned = source_origin(copy, SOURCE_PRESENTATION_STATEMENT, "items = 7");
    REQUIRE(cloned && cloned->identity == o->identity && cloned->record == o->record);
    char *changed = authoring_source_presentation_replace(copy, cloned, "items = 11;");
    REQUIRE(changed && strstr(changed, "items = 11; items += 3"));
    free(changed);
    node *cloned_set = origin_node(copy, cloned);
    REQUIRE(cloned_set->desc->description != set->desc->description);
    REQUIRE(cloned_set->desc->category != set->desc->category);
    authoring_source_presentation_free(view);
    view = NULL;
    REQUIRE(strstr(cloned_set->desc->description, "this behavior instance's"));
    REQUIRE(strstr(cloned_set->desc->description, "'items'"));
    REQUIRE(!strcmp(cloned_set->desc->category, "Source"));
    return true;
}
TEST_CASE(source_view_variable_reads_capture_one_named_value)
{
    REQUIRE(
        project("Inventory :: behavior { items_left: f64 = 0; on Tick :: () { itemsValue := state.items_left; state.items_left = 9; } }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    node *read = statement_at("itemsValue :=");
    node *set = statement_at("state.items_left = 9");
    graph *g = behavior_asset_find_graph_by_name(authoring_source_presentation_view(view), "Tick");
    REQUIRE(read && set && g);
    REQUIRE(!strcmp(read->desc->name, "Get items_left"));
    REQUIRE(!strcmp(set->desc->name, "Set items_left"));
    REQUIRE(read->input_count == 1 && read->input_pins[0].kind == PIN_EXEC);
    REQUIRE(named_pin(read->output_pins, read->output_count, PIN_DATA, "itemsValue") >= 0);
    REQUIRE(strstr(read->desc->description, "Captures its value in local 'itemsValue'"));
    REQUIRE(flow_edge(g, graph_node_by_id(g, g->entry_id), "Then", read, "Exec"));
    REQUIRE(flow_edge(g, read, "Then", set, "Exec"));
    REQUIRE(!graph_node_is_inline(g, read));
    int reads = 0;
    for (int i = 0; i < acount(g->nodes); i++) {
        reads += !strcmp(g->nodes[i]->desc->name, "Get items_left");
    }
    REQUIRE(reads == 1);
    const source_presentation_origin *o = source_origin(view, SOURCE_PRESENTATION_EXPRESSION, "state.items_left;");
    REQUIRE(o && origin_node(view, o) == read);
    edited = authoring_source_presentation_replace(view, o, "5");
    REQUIRE(edited && strstr(edited, "itemsValue := 5; state.items_left = 9"));
    return true;
}
TEST_CASE(source_view_indexed_setters_keep_address_parts_editable)
{
    REQUIRE(
        project("Inventory :: behavior { items: f64[] = [1, 2]; slot: int = 0; on Tick :: () { items[slot] = 7; } }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    node *set = statement_at("items[slot] = 7");
    REQUIRE(set && !strcmp(set->desc->name, "Set items[slot]"));
    REQUIRE(set->input_count == 4);
    const char *names[] = { "Collection", "Index", "Value" };
    const char *ranges[] = { "items", "slot", "7" };
    graph *g = behavior_asset_find_graph_by_name(authoring_source_presentation_view(view), "Tick");
    REQUIRE(g);
    for (int i = 0; i < 3; i++) {
        int pin = named_pin(set->input_pins, set->input_count, PIN_DATA, names[i]);
        REQUIRE(pin >= 0);
        const source_presentation_origin *o = authoring_source_presentation_origin(
            view,
            g->graph_id,
            set->id,
            set->input_pins[pin].id
        );
        REQUIRE(o && o->kind == SOURCE_PRESENTATION_EXPRESSION);
        REQUIRE(o->range.end - o->range.start == (int)strlen(ranges[i]));
        REQUIRE(!strncmp(
            authoring_source_presentation_text(view) + o->range.start,
            ranges[i],
            strlen(ranges[i])
        ));
    }
    const source_presentation_origin *address = NULL;
    for (int i = 0; i < authoring_source_presentation_origin_count(view); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(view, i);
        if (o->kind == SOURCE_PRESENTATION_EXPRESSION && !o->pin &&
            o->graph == g->graph_id && o->node == set->id) {
            address = o;
            break;
        }
    }
    REQUIRE(address && address->range.end - address->range.start == 11);
    REQUIRE(
        !strncmp(authoring_source_presentation_text(view) + address->range.start, "items[slot]", 11)
    );
    return true;
}
TEST_CASE(source_view_component_setters_expose_entity_not_component_read)
{
    static const script_catalog_field fields[] = {
        { .id = 20, .name = "x", .type = { .storage = PIN_F64 }, .writable = true }
    };
    static const script_catalog_type types[] = { { .id = 10,
                                                   .name = "Position",
                                                   .type = { .storage = PIN_STRUCT,
                                                             .nominal = 10 },
                                                   .fields = fields,
                                                   .field_count = 1,
                                                   .size = sizeof(double),
                                                   .component = true,
                                                   .component_id = 501 } };
    static const script_catalog declarations = { .types = types,
                                                 .type_count = 1,
                                                 .entity_type = 40,
                                                 .capabilities = capabilities,
                                                 .capability_count = 2 };
    const char *sources[] = {
        "Motion :: behavior { on Tick :: () { self.Position.x = 7; } }",
        "Motion :: system(position: ^Position) { reduce self.Position.x += 7; "
        "}",
        "Motion :: system(position: ^Position) { reduce min(self.Position.x, "
        "7); }",
        "Motion :: system(position: ^Position) { reduce max(self.Position.x, "
        "7); }",
        "Motion :: system(position: ^Position) { reduce unordered "
        "self.Position.x += 7; }",
    };
    const char *titles[] = { "Set self.Position.x",
                             "Reduce add self.Position.x",
                             "Reduce min self.Position.x",
                             "Reduce max self.Position.x",
                             "Reduce unordered add self.Position.x" };
    const char *pins[] = { "Entity", "Value" }, *ranges[] = { "self", "7" };
    for (int sample = 0; sample < 5; sample++) {
        cleanup();
        syntax = script_parse(sources[sample]);
        REQUIRE(syntax->diagnostic_count == 0);
        analysis = script_analyze(syntax, &declarations);
        REQUIRE(analysis->diagnostic_count == 0);
        view = authoring_source_presentation_new(analysis, &declarations, NULL);
        REQUIRE(view);
        const source_presentation_origin *statement = source_origin(
            view,
            SOURCE_PRESENTATION_STATEMENT,
            sample ? "reduce" : "self.Position.x = 7"
        );
        REQUIRE(statement);
        REQUIRE(authoring_source_presentation_matches(view, analysis, &declarations));
        if (sample) {
            script_statement *reduction = &analysis->statements[statement->record];
            script_reduction_op original = reduction->reduction;
            reduction->reduction = original == SCRIPT_REDUCTION_ADD
                                     ? SCRIPT_REDUCTION_MIN
                                     : SCRIPT_REDUCTION_ADD;
            REQUIRE(!authoring_source_presentation_matches(view, analysis, &declarations));
            reduction->reduction = original;
            REQUIRE(authoring_source_presentation_matches(view, analysis, &declarations));
        }
        node *set = origin_node(view, statement);
        REQUIRE(set && set->input_count == 3 && !strcmp(set->desc->name, titles[sample]));
        graph *g = behavior_asset_find_graph(
            authoring_source_presentation_view(view),
            statement->graph
        );
        REQUIRE(g);
        for (int i = 0; i < 2; i++) {
            int pin = named_pin(set->input_pins, set->input_count, PIN_DATA, pins[i]);
            REQUIRE(pin >= 0);
            const source_presentation_origin *o = authoring_source_presentation_origin(
                view,
                g->graph_id,
                set->id,
                set->input_pins[pin].id
            );
            REQUIRE(o && o->kind == SOURCE_PRESENTATION_EXPRESSION);
            REQUIRE(o->range.end - o->range.start == (int)strlen(ranges[i]));
            REQUIRE(!strncmp(
                authoring_source_presentation_text(view) + o->range.start,
                ranges[i],
                strlen(ranges[i])
            ));
        }
        const source_presentation_origin *target = NULL;
        for (int i = 0; i < authoring_source_presentation_origin_count(view); i++) {
            const source_presentation_origin *o = authoring_source_presentation_origin_at(view, i);
            if (o->kind == SOURCE_PRESENTATION_EXPRESSION && !o->pin &&
                o->graph == g->graph_id && o->node == set->id) {
                target = o;
                break;
            }
        }
        REQUIRE(target && target->range.end - target->range.start == 15);
        REQUIRE(
            !strncmp(authoring_source_presentation_text(view) + target->range.start, "self.Position.x", 15)
        );
        for (int i = 0; i < acount(g->nodes); i++) {
            REQUIRE(strcmp(g->nodes[i]->desc->name, "Get Position"));
        }
    }
    /* An `if` comparison is a value query, which has its own node. */
    cleanup();
    syntax = script_parse("Motion :: system(position: ^Position , if position.x > 0) { position.x = 7; }");
    REQUIRE(syntax->diagnostic_count == 0);
    analysis = script_analyze(syntax, &declarations);
    REQUIRE(analysis->diagnostic_count == 0 && analysis->query >= 0);
    view = authoring_source_presentation_new(analysis, &declarations, NULL);
    REQUIRE(view);
    bool filter = false;
    behavior_asset *asset = authoring_source_presentation_view(view);
    for (int i = 0; i < acount(asset->graphs); i++) {
        for (int j = 0; j < acount(asset->graphs[i]->nodes); j++) {
            filter |= !strncmp(asset->graphs[i]->nodes[j]->desc->name, "If", 2);
        }
    }
    REQUIRE(filter);
    return true;
}
TEST_CASE(source_view_authored_calls_keep_named_typed_editable_ports)
{
    REQUIRE(
        project("Inventory :: behavior { count: f64 = 0; Pair :: (amount: f64, text: string) -> (total: f64, message: string) { return (amount + 1, text); } Other :: () -> (value: f64) { return 3; } on Tick :: () { current := Pair(text: \"apple\", amount: 2); count = current.total; await Delay(0.1); } }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    const source_presentation_origin *o = source_origin(view, SOURCE_PRESENTATION_EXPRESSION, "Pair(text:");
    REQUIRE(o);
    node *call = origin_node(view, o);
    graph *g = behavior_asset_find_graph(authoring_source_presentation_view(view), o->graph);
    graph *callee = behavior_asset_find_graph_by_name(authoring_source_presentation_view(view), "Pair");
    REQUIRE(callee && callee != g);
    REQUIRE(
        authoring_source_presentation_call_graph(view, g->graph_id, call->id) ==
        callee->graph_id
    );
    REQUIRE(authoring_source_presentation_call_graph(view, g->graph_id, g->entry_id) == 0);
    node *native_call = statement_at("await Delay(0.1)");
    REQUIRE(
        native_call &&
        authoring_source_presentation_call_graph(view, g->graph_id, native_call->id) == 0
    );
    REQUIRE(call->desc->display_class == NODE_DISPLAY_FUNCTION);
    REQUIRE(call->desc->pure_kind == NODE_PURE_FUNCTION);
    REQUIRE(node_behavior_for(call) == NODE_BEHAVIOR_PURE);
    REQUIRE(!graph_node_is_inline(g, call));
    int amount = named_pin(call->input_pins, call->input_count, PIN_DATA, "amount");
    int text = named_pin(call->input_pins, call->input_count, PIN_DATA, "text");
    int total = named_pin(call->output_pins, call->output_count, PIN_DATA, "total");
    int message = named_pin(call->output_pins, call->output_count, PIN_DATA, "message");
    REQUIRE(amount >= 0 && text >= 0 && total >= 0 && message >= 0);
    REQUIRE(text < amount); /* authored named arguments stay in source order */
    REQUIRE(call->input_pins[amount].type == PIN_F64);
    REQUIRE(call->input_pins[text].type == PIN_STR);
    REQUIRE(call->output_pins[total].type == PIN_F64);
    REQUIRE(call->output_pins[message].type == PIN_STR);
    const source_presentation_origin *value = authoring_source_presentation_origin(
        view,
        o->graph,
        o->node,
        call->input_pins[amount].id
    );
    REQUIRE(value && value->kind == SOURCE_PRESENTATION_EXPRESSION);
    edited = authoring_source_presentation_replace(view, value, "5");
    REQUIRE(edited && strstr(edited, "Pair(text: \"apple\", amount: 5)"));
    copy = authoring_source_presentation_clone(view);
    const source_presentation_origin
        *cloned = source_origin(copy, SOURCE_PRESENTATION_EXPRESSION, "Pair(text:");
    REQUIRE(cloned && cloned->identity == o->identity);
    node *owned = origin_node(copy, cloned);
    REQUIRE(
        authoring_source_presentation_call_graph(copy, cloned->graph, owned->id) ==
        callee->graph_id
    );
    REQUIRE(owned->input_pins[amount].name != call->input_pins[amount].name);
    REQUIRE(!strcmp(owned->input_pins[amount].name, "amount"));
    REQUIRE(owned->input_pins[amount].type == PIN_F64);
    owned->x = 123;
    owned->y = 456;
    owned->placed = true;
    metadata = authoring_source_presentation_metadata(copy);
    REQUIRE(metadata);
    owned->x = 0;
    owned->y = 0;
    REQUIRE(authoring_source_presentation_restore(copy, metadata));
    REQUIRE(owned->x == 123 && owned->y == 456);
    return true;
}
TEST_CASE(source_view_effectful_authored_calls_remain_explicit)
{
    REQUIRE(
        project("Counter :: behavior { count: f64 = 0; Next :: (amount: f64) -> (total: f64) { count += amount; return count; } on Tick :: () { count = Next(2) + 1; } }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    const source_presentation_origin *o = source_origin(view, SOURCE_PRESENTATION_EXPRESSION, "Next(2)");
    REQUIRE(o && (o->effects & SCRIPT_EFFECT_WRITE));
    node *call = origin_node(view, o);
    graph *g = behavior_asset_find_graph(authoring_source_presentation_view(view), o->graph);
    REQUIRE(call->desc->display_class == NODE_DISPLAY_FUNCTION);
    REQUIRE(named_pin(call->input_pins, call->input_count, PIN_EXEC, "Exec") >= 0);
    REQUIRE(named_pin(call->output_pins, call->output_count, PIN_EXEC, "Then") >= 0);
    REQUIRE(!graph_node_is_inline(g, call));
    bool incoming = false, outgoing = false;
    for (int i = 0; i < acount(g->edges); i++) {
        if (g->edges[i].kind == GRAPH_EDGE_EXEC) {
            incoming |= g->edges[i].to_id == call->id;
            outgoing |= g->edges[i].from_id == call->id;
        }
    }
    REQUIRE(incoming && outgoing);
    return true;
}
TEST_CASE(source_view_small_value_calls_fold_with_editable_origins)
{
    REQUIRE(
        project("Counter :: behavior { count: f64 = 0; Double :: (amount: f64) -> (total: f64) { return amount * 2; } on Tick :: () { count = Double(2); } }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    const source_presentation_origin *o = source_origin(view, SOURCE_PRESENTATION_EXPRESSION, "Double(2)");
    REQUIRE(o);
    node *call = origin_node(view, o);
    node *set = statement_at("count = Double");
    graph *g = behavior_asset_find_graph(authoring_source_presentation_view(view), o->graph);
    REQUIRE(call && set && g);
    REQUIRE(call->desc->display_class == NODE_DISPLAY_FUNCTION);
    REQUIRE(graph_node_is_inline(g, call));
    REQUIRE(graph_inline_owner(g, call) == set);
    const source_presentation_origin *argument = authoring_source_presentation_origin(
        view,
        o->graph,
        call->id,
        call->input_pins[0].id
    );
    REQUIRE(argument && argument->kind == SOURCE_PRESENTATION_EXPRESSION);
    edited = authoring_source_presentation_replace(view, argument, "3");
    REQUIRE(edited && strstr(edited, "count = Double(3)"));
    REQUIRE(graph_set_pin_watch(g, call, true, 0, true) == 0);
    REQUIRE(!graph_node_is_inline(g, call) && !graph_node_can_inline(g, call));
    REQUIRE(graph_set_pin_watch(g, call, true, 0, false) == 0);
    REQUIRE(graph_set_breakpoint(g, call, true) == 0);
    REQUIRE(!graph_node_is_inline(g, call) && !graph_node_can_inline(g, call));
    REQUIRE(graph_set_breakpoint(g, call, false) == 0);
    /* A second consumer keeps a named call visible even when both uses read
     * the same result. Visual folding must not suggest duplicated execution. */
    node *consumer = graph_add_node(g, set->desc);
    int value = named_pin(set->input_pins, set->input_count, PIN_DATA, "Value");
    REQUIRE(value >= 0);
    REQUIRE(graph_connect(g, call, 0, consumer, value) == 0);
    REQUIRE(!graph_node_is_inline(g, call));
    return true;
}
TEST_CASE(source_view_namespace_fields_are_named_values)
{
    static const script_catalog_field fields[] = { { .id = 20,
                                                     .name = "Level",
                                                     .type = { .storage = PIN_HANDLE,
                                                               .nominal = 31 },
                                                     .read = 200,
                                                     .read_key = "Level" } };
    static const script_catalog_type types[] = {
        { .id = 10,
          .name = "SceneGlobals",
          .type = { .storage = PIN_STR, .nominal = 10, .flags = SCRIPT_TYPE_NAMESPACE },
          .fields = fields,
          .field_count = 1 },
        { .id = 31, .name = "entity", .type = { .storage = PIN_HANDLE, .nominal = 31 } }
    };
    static const script_catalog_global globals[] = {
        { .id = 30,
          .name = "globals",
          .type = { .storage = PIN_STR, .nominal = 10, .flags = SCRIPT_TYPE_NAMESPACE } }
    };
    static const script_catalog_parameter key = { .name = "key",
                                                  .type = { .storage = PIN_STR } };
    static const script_catalog_parameter entity = { .name = "entity",
                                                     .type = { .storage = PIN_HANDLE,
                                                               .nominal = 31 } };
    static const script_catalog_capability declarations[] = {
        { .id = 100, .name = "Tick", .kind = SCRIPT_CAPABILITY_EVENT },
        { .id = 200,
          .name = "GetGlobal",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = &key,
          .parameter_count = 1,
          .results = &entity,
          .result_count = 1,
          .effects = SCRIPT_EFFECT_READ,
          .pure = true }
    };
    static const script_catalog scene = { .types = types,
                                          .type_count = 2,
                                          .globals = globals,
                                          .global_count = 1,
                                          .capabilities = declarations,
                                          .capability_count = 2,
                                          .entity_type = 31 };
    syntax = script_parse("View :: behavior { player:entity; players:entity[]; known:entity=self; on Tick :: () { level := globals.Level; } }");
    analysis = script_analyze(syntax, &scene);
    REQUIRE(analysis->diagnostic_count == 0);
    view = authoring_source_presentation_new(analysis, &scene, NULL);
    REQUIRE(view);
    const char *const state_declarations[] = { "player:entity", "players:entity[]" };
    for (int i = 0; i < 2; i++) {
        const source_presentation_origin *
            empty = source_origin(view, SOURCE_PRESENTATION_SYMBOL, state_declarations[i]);
        REQUIRE(empty);
        node *variable = origin_node(view, empty);
        REQUIRE(variable && variable->input_count == 0 && variable->output_count == 1);
        REQUIRE(variable->output_pins[0].type == PIN_HANDLE);
        REQUIRE(variable->output_pins[0].container == (i ? PIN_CONTAINER_ARRAY : PIN_CONTAINER_SINGLE));
        for (int j = 0; j < authoring_source_presentation_origin_count(view); j++) {
            const source_presentation_origin
                *candidate = authoring_source_presentation_origin_at(view, j);
            REQUIRE(
                candidate->kind != SOURCE_PRESENTATION_EXPRESSION ||
                candidate->record != analysis->symbols[empty->record].initializer
            );
        }
    }
    const source_presentation_origin *known = source_origin(view, SOURCE_PRESENTATION_SYMBOL, "known:entity");
    REQUIRE(known && origin_node(view, known)->input_count == 1);
    const source_presentation_origin *o = source_origin(view, SOURCE_PRESENTATION_EXPRESSION, "globals.Level");
    REQUIRE(o);
    node *read = origin_node(view, o);
    REQUIRE(read && !strcmp(read->desc->name, "Get Level"));
    REQUIRE(read->desc->description && strstr(read->desc->description, "scene entity"));
    REQUIRE(strstr(read->desc->description, "'Level' in globals"));
    REQUIRE(read->input_count == 0 && read->output_count == 1);
    REQUIRE(read->output_pins[0].type == PIN_HANDLE);
    REQUIRE(!strcmp(read->output_pins[0].type_name, "entity"));
    graph *g = behavior_asset_find_graph(authoring_source_presentation_view(view), o->graph);
    REQUIRE(g && graph_node_is_inline(g, read));
    for (int i = 0; i < acount(g->nodes); i++) {
        REQUIRE(strcmp(g->nodes[i]->desc->name, "Get globals"));
    }
    edited = authoring_source_presentation_replace(view, o, "self");
    REQUIRE(edited && strstr(edited, "level := self;"));
    REQUIRE(authoring_source_presentation_matches(view, analysis, &scene));
    script_catalog_field rebound = fields[0];
    rebound.read_key = "OtherLevel";
    script_catalog_type rebound_types[] = { types[0], types[1] };
    rebound_types[0].fields = &rebound;
    script_catalog changed = scene;
    changed.types = rebound_types;
    REQUIRE(!authoring_source_presentation_matches(view, analysis, &changed));
    return true;
}
TEST_CASE(source_view_short_circuit_effects_are_not_eager_flow)
{
    REQUIRE(
        project("Counter :: behavior { count: f64 = 0; ready: bool = false; Check :: (amount: f64) -> (ok: bool) { count += amount; return true; } on Tick :: () { ready = ready && Check(1); } }")
    );
    REQUIRE(analysis->diagnostic_count == 0);
    const source_presentation_origin *o = source_origin(view, SOURCE_PRESENTATION_EXPRESSION, "ready && Check(1)");
    const source_presentation_origin
        *called = source_origin(view, SOURCE_PRESENTATION_EXPRESSION, "Check(1)");
    REQUIRE(o && !called);
    node *condition = origin_node(view, o);
    node *set = statement_at("ready = ready");
    graph *g = behavior_asset_find_graph(authoring_source_presentation_view(view), o->graph);
    REQUIRE(set && g);
    REQUIRE(!strcmp(condition->desc->name, "Evaluate Condition"));
    REQUIRE(flow_edge(g, graph_node_by_id(g, g->entry_id), "Then", condition, "Exec"));
    REQUIRE(flow_edge(g, condition, "Then", set, "Exec"));
    edited = authoring_source_presentation_replace(view, o, "true");
    REQUIRE(edited && strstr(edited, "ready = true;"));
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
            .types = TYPE_PACK,
            .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT } },
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
TEST_CASE(source_view_keeps_constant_definitions)
{
    REQUIRE(project("View :: behavior { Base :: 2; Run :: () { Local :: Base + 1; } }"));
    REQUIRE(analysis->valid);
    int constants = 0;
    for (int i = 0; i < authoring_source_presentation_origin_count(view); i++) {
        const source_presentation_origin *origin = authoring_source_presentation_origin_at(view, i);
        if (origin->kind != SOURCE_PRESENTATION_SYMBOL || origin->pin ||
            analysis->symbols[origin->record].kind != SCRIPT_CONSTANT) {
            continue;
        }
        REQUIRE(origin_node(view, origin));
        constants++;
    }
    REQUIRE(constants == 2);
    return true;
}

void test_source_presentation()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_view_keeps_constant_definitions);
    RUN_TEST_CASE(source_view_clone_owns_defaults_and_edit_origins);
    RUN_TEST_CASE(source_views_keep_named_invalid_drafts);
    RUN_TEST_CASE(source_view_locals_stay_in_their_callable);
    RUN_TEST_CASE(source_view_entries_have_layout_priority);
    RUN_TEST_CASE(source_view_formulas_preserve_readable_source);
    RUN_TEST_CASE(source_view_linear_flow_follows_authored_order);
    RUN_TEST_CASE(source_view_branch_terminals_do_not_rejoin);
    RUN_TEST_CASE(source_view_loop_break_and_continue_have_distinct_paths);
    RUN_TEST_CASE(source_view_await_completion_precedes_following_statement);
    RUN_TEST_CASE(source_view_set_nodes_keep_statement_and_value_origins);
    RUN_TEST_CASE(source_view_variable_reads_capture_one_named_value);
    RUN_TEST_CASE(source_view_handler_exits_are_explicit_and_completion_is_natural);
    RUN_TEST_CASE(source_view_indexed_setters_keep_address_parts_editable);
    RUN_TEST_CASE(source_view_component_setters_expose_entity_not_component_read);
    RUN_TEST_CASE(source_view_authored_calls_keep_named_typed_editable_ports);
    RUN_TEST_CASE(source_view_effectful_authored_calls_remain_explicit);
    RUN_TEST_CASE(source_view_small_value_calls_fold_with_editable_origins);
    RUN_TEST_CASE(source_view_namespace_fields_are_named_values);
    RUN_TEST_CASE(source_view_short_circuit_effects_are_not_eager_flow);
    RUN_TEST_CASE(native_palette_composition_keeps_declared_capabilities);
    RUN_TEST_CASE(source_view_names_reverse_and_half_open_loops);
    pu_clear_setup();
}
