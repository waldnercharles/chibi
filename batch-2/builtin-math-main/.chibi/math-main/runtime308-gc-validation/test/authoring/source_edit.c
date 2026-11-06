#include "authoring/source_edit.h"

#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

/* Fixed, tiny source candidates; no files, world frames, execution or GPU work. */
static const script_catalog_capability tick = {
    .id = 100,
    .name = "Tick",
    .kind = SCRIPT_CAPABILITY_EVENT,
};
static const script_catalog catalog = { .capabilities = &tick, .capability_count = 1 };
static script_syntax *syntax, *candidate_syntax;
static script_analysis *analysis, *candidate_analysis;
static authoring_source_presentation *view, *candidate_view;
static authoring_edit_result result;
static char error[256];
static void cleanup()
{
    authoring_source_presentation_free(candidate_view);
    authoring_source_presentation_free(view);
    script_analysis_free(candidate_analysis);
    script_analysis_free(analysis);
    script_syntax_free(candidate_syntax);
    script_syntax_free(syntax);
    authoring_edit_result_free(&result);
    syntax = candidate_syntax = NULL;
    analysis = candidate_analysis = NULL;
    view = candidate_view = NULL;
}
static bool project(const char *source)
{
    syntax = script_parse(source);
    analysis = script_analyze_document(syntax, &catalog, "tests/source-edit");
    view = authoring_source_presentation_new(analysis, &catalog, NULL);
    return analysis->valid && view;
}
static const source_presentation_origin *find(source_presentation_kind kind, const char *text)
{
    const char *start = strstr(analysis->text, text);
    if (!start) {
        return NULL;
    }
    int offset = (int)(start - analysis->text);
    for (int i = 0; i < authoring_source_presentation_origin_count(view); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(view, i);
        if (o->kind == kind && !o->pin && o->range.start == offset) {
            return o;
        }
    }
    return NULL;
}
static authoring_edit_request request(authoring_edit_kind kind, const source_presentation_origin *o)
{
    return (authoring_edit_request){ .kind = kind,
                                     .generation = authoring_source_edit_generation(view),
                                     .origin = o->identity };
}
static bool edit(authoring_edit_request r)
{
    bool accepted = authoring_source_edit(analysis, &catalog, view, &r, &result, error, sizeof error);
    if (!accepted) {
        fprintf(stderr, "Source edit kind %d rejected: %.255s\n", r.kind, error);
    }
    return accepted;
}
static bool inspect_result()
{
    candidate_syntax = script_parse(result.text);
    candidate_analysis = script_analyze_document(
        candidate_syntax,
        &catalog,
        analysis->identity
    );
    candidate_view = authoring_source_presentation_new(candidate_analysis, &catalog, NULL);
    bool restored = candidate_analysis->valid && candidate_view &&
                    authoring_source_presentation_restore(candidate_view, result.metadata);
    if (!restored) {
        fprintf(
            stderr,
            "Source edit result syntax=%d semantic=%d view=%d metadata restore "
            "failed; %.191s\n",
            candidate_syntax->diagnostic_count,
            candidate_analysis->valid,
            candidate_view != NULL,
            candidate_analysis->diagnostic_count
                ? candidate_analysis->diagnostics[0].message
                : "no semantic diagnostic"
        );
    }
    return restored;
}
TEST_CASE(source_expression_edits_validate_value_scope_and_generation)
{
    REQUIRE(project("Edit :: behavior { count:f64=1; on Tick :: () { count=2; } }"));
    const source_presentation_origin *o = find(SOURCE_PRESENTATION_EXPRESSION, "2;");
    REQUIRE(o);
    node *n = graph_node_by_id(
        behavior_asset_find_graph(authoring_source_presentation_view(view), o->graph),
        o->node
    );
    REQUIRE(
        graph_set_pin_watch(
            behavior_asset_find_graph(authoring_source_presentation_view(view), o->graph),
            n,
            1,
            0,
            1
        ) == 0
    );
    n->x = 12;
    n->y = 34;
    n->placed = true;
    graph *decorated = behavior_asset_find_graph(authoring_source_presentation_view(view), o->graph);
    graph_comment_box *box = graph_add_comment_box(decorated, "Saved layout", 5, 6, 80, 40);
    REQUIRE(box);
    bool routed = false;
    for (int i = 0; i < acount(decorated->edges); i++) {
        edge *wire = &decorated->edges[i];
        if (wire->from_id == n->id && wire->kind == GRAPH_EDGE_DATA) {
            wire->reroute_count = 1;
            wire->reroutes[0].x = 19;
            wire->reroutes[0].y = 23;
            routed = true;
            break;
        }
    }
    REQUIRE(routed);
    authoring_edit_request r = request(AUTHORING_EDIT_EXPRESSION, o);
    r.expression = "3 + 4";
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "count=3 + 4;") && strstr(analysis->text, "count=2;"));
    REQUIRE(inspect_result());
    bool placed = false;
    for (int i = 0; i < authoring_source_presentation_origin_count(candidate_view);
         i++) {
        const source_presentation_origin
            *p = authoring_source_presentation_origin_at(candidate_view, i);
        if (p->kind != SOURCE_PRESENTATION_EXPRESSION || p->pin ||
            p->range.start != o->range.start ||
            p->range.end != o->range.start + (int)strlen("3 + 4")) {
            continue;
        }
        node *m = graph_node_by_id(
            behavior_asset_find_graph(authoring_source_presentation_view(candidate_view), p->graph),
            p->node
        );
        placed = m->placed && m->x == 12 && m->y == 34 &&
                 (m->output_pins[0].flags & PIN_FLAG_WATCH);
    }
    REQUIRE(placed);
    graph *restored = behavior_asset_find_graph(
        authoring_source_presentation_view(candidate_view),
        o->graph
    );
    REQUIRE(
        acount(restored->comment_boxes) == 1 &&
        !strcmp(restored->comment_boxes[0].title, "Saved layout") &&
        restored->comment_boxes[0].width == 80
    );
    routed = false;
    for (int i = 0; i < acount(restored->edges); i++) {
        const edge *wire = &restored->edges[i];
        routed |= wire->reroute_count == 1 && wire->reroutes[0].x == 19 &&
                  wire->reroutes[0].y == 23;
    }
    REQUIRE(routed);
    authoring_edit_result_free(&result);
    r.expression = "\"wrong type\"";
    REQUIRE(!edit(r) && error[0] && !result.text && !result.metadata);
    r.expression = "3; count=99";
    REQUIRE(!edit(r) && strstr(error, "one value expression"));
    r.expression = "3";
    r.generation++;
    REQUIRE(!edit(r) && strstr(error, "Source changed"));
    script_catalog_capability changed_tick = tick;
    changed_tick.effects = SCRIPT_EFFECT_HOST;
    script_catalog changed_catalog = catalog;
    changed_catalog.capabilities = &changed_tick;
    script_analysis
        *changed = script_analyze_document(syntax, &changed_catalog, analysis->identity);
    REQUIRE(changed->valid);
    r.generation = authoring_source_edit_generation(view);
    bool accepted = authoring_source_edit(changed, &changed_catalog, view, &r, &result, error, sizeof error);
    script_analysis_free(changed);
    REQUIRE(!accepted && strstr(error, "Source changed"));
    /* The following subcase isolates missing watched nodes. Routes are exact
     * existing-edge snapshots and must not be restored after edge deletion. */
    for (int i = 0; i < acount(decorated->edges); i++) {
        decorated->edges[i].reroute_count = 0;
    }
    char *watched = authoring_source_presentation_metadata(view);
    REQUIRE(watched);
    graph *g = behavior_asset_find_graph(authoring_source_presentation_view(view), o->graph);
    REQUIRE(graph_remove_node(g, n) == 0);
    bool watch_restored = authoring_source_presentation_restore(view, watched);
    free(watched);
    REQUIRE(watch_restored);
    char *without_node = authoring_source_presentation_metadata(view);
    REQUIRE(without_node);
    free(without_node);
    return true;
}
TEST_CASE(source_setter_value_edits_preserve_the_destination)
{
    REQUIRE(project("Edit :: behavior { count:f64=0; on Tick :: () { saved:=5; count=7; } }"));
    const source_presentation_origin *statement = find(SOURCE_PRESENTATION_STATEMENT, "count=7;");
    const source_presentation_origin *producer = find(SOURCE_PRESENTATION_SYMBOL, "saved:=5;");
    REQUIRE(statement && producer);
    graph *g = behavior_asset_find_graph(
        authoring_source_presentation_view(view),
        statement->graph
    );
    node *setter = graph_node_by_id(g, statement->node);
    REQUIRE(setter && setter->input_count == 2);
    REQUIRE(!strcmp(setter->input_pins[1].name, "Value"));
    const source_presentation_origin *value = authoring_source_presentation_origin(
        view,
        g->graph_id,
        setter->id,
        setter->input_pins[1].id
    );
    REQUIRE(value && value->kind == SOURCE_PRESENTATION_EXPRESSION && !value->output);
    REQUIRE(value->range.start == (int)(strstr(analysis->text, "7;") - analysis->text));
    authoring_edit_request r = request(AUTHORING_EDIT_EXPRESSION, value);
    r.expression = "9";
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "saved:=5; count=9;"));
    authoring_edit_result_free(&result);
    r = request(AUTHORING_EDIT_CONNECT, value);
    r.producer = producer->identity;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "saved:=5; count=saved;"));
    REQUIRE(inspect_result());
    return true;
}
TEST_CASE(source_variable_get_set_creation_uses_declared_state_and_checked_values)
{
    REQUIRE(project(
        "Variables :: behavior { count: f64 = 1; text: string = \"hello\"; "
        "countValue: f64 = 0;"
        " on Tick :: () { count = 7; } Shadow :: (count: f64) {} }"
    ));
    const source_presentation_origin *state = find(SOURCE_PRESENTATION_SYMBOL, "count: f64");
    const source_presentation_origin *handler = find(SOURCE_PRESENTATION_FUNCTION, "on Tick");
    const source_presentation_origin *target = find(SOURCE_PRESENTATION_EXPRESSION, "7;");
    REQUIRE(state && handler && target);
    uint64_t id = analysis->symbols[state->record].id;
    authoring_edit_request r = request(AUTHORING_EDIT_CREATE_VARIABLE_GET, handler);
    r.symbol = id;
    REQUIRE(edit(r));
    REQUIRE(
        strstr(result.text, "countValue2 := state.count;") &&
        !strstr(result.text, "get_variable")
    );
    REQUIRE(inspect_result());
    authoring_edit_result_free(&result);
    r.previous = target->identity;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "count = state.count;") && !strstr(result.text, "countValue2"));
    authoring_edit_result_free(&result);
    r = request(AUTHORING_EDIT_CREATE_VARIABLE_SET, handler);
    r.symbol = id;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "state.count = 0;") && !strstr(result.text, "set_variable"));
    authoring_edit_result_free(&result);
    r.producer = target->identity;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "state.count = (7);"));
    authoring_edit_result_free(&result);
    const source_presentation_origin *wrong = find(SOURCE_PRESENTATION_SYMBOL, "text: string");
    REQUIRE(wrong);
    r.producer = wrong->identity;
    REQUIRE(!edit(r) && !result.text && error[0]);
    r.producer = 0;
    r.symbol = UINT64_MAX;
    REQUIRE(!edit(r) && !result.text && error[0]);
    const source_presentation_origin *shadow = find(SOURCE_PRESENTATION_FUNCTION, "Shadow :: ");
    REQUIRE(shadow);
    r = request(AUTHORING_EDIT_CREATE_VARIABLE_GET, shadow);
    r.symbol = id;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "countValue2 := state.count;"));
    return true;
}
TEST_CASE(source_variable_creation_uses_identifiers_and_keeps_nonliteral_defaults_editable)
{
    REQUIRE(project("Variables :: behavior { Player_Name: f64 = 1; on Tick :: () {} }"));
    const source_presentation_origin *state = find(SOURCE_PRESENTATION_SYMBOL, "Player_Name: f64");
    const source_presentation_origin *handler = find(SOURCE_PRESENTATION_FUNCTION, "on Tick");
    REQUIRE(state && handler);
    authoring_edit_request r = request(AUTHORING_EDIT_CREATE_VARIABLE_GET, handler);
    r.symbol = analysis->symbols[state->record].id;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "Player_NameValue := state.Player_Name;"));
    authoring_edit_result_free(&result);
    r.kind = AUTHORING_EDIT_CREATE_VARIABLE_SET;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "state.Player_Name = 0;"));
    authoring_edit_result_free(&result);
    r.producer = state->identity;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "state.Player_Name = state.Player_Name;"));
    cleanup();
    REQUIRE(project(
        "Variables :: behavior { Player_Name: f64 = 1; other: f64 = 0;"
        " on Tick :: () { state.Player_Name = state.Player_Name + 1;"
        " other = state.Player_Name; } }"
    ));
    state = find(SOURCE_PRESENTATION_SYMBOL, "Player_Name: f64");
    REQUIRE(state);
    r = request(AUTHORING_EDIT_RENAME_DECLARATION, state);
    r.name = "playerSpeed";
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "playerSpeed: f64 = 1;"));
    REQUIRE(strstr(result.text, "state.playerSpeed = state.playerSpeed + 1;"));
    REQUIRE(strstr(result.text, "other = state.playerSpeed;"));
    REQUIRE(!strstr(result.text, "Player Name"));
    REQUIRE(inspect_result());
    cleanup();
    static const script_catalog_type entity = { .id = 40,
                                                .name = "Entity",
                                                .type = { .storage = PIN_HANDLE,
                                                          .nominal = 40 } };
    static const script_catalog declarations = { .types = &entity,
                                                 .type_count = 1,
                                                 .entity_type = 40,
                                                 .capabilities = &tick,
                                                 .capability_count = 1 };
    syntax = script_parse("Variables :: behavior { player: Entity? = null; on Tick :: () {} }");
    analysis = script_analyze_document(syntax, &declarations, "tests/source-edit");
    REQUIRE(analysis->valid);
    view = authoring_source_presentation_new(analysis, &declarations, NULL);
    REQUIRE(view);
    state = find(SOURCE_PRESENTATION_SYMBOL, "player: Entity");
    handler = find(SOURCE_PRESENTATION_FUNCTION, "on Tick");
    REQUIRE(state && handler);
    r = request(AUTHORING_EDIT_CREATE_VARIABLE_SET, handler);
    r.symbol = analysis->symbols[state->record].id;
    REQUIRE(authoring_source_edit(analysis, &declarations, view, &r, &result, error, sizeof error));
    REQUIRE(strstr(result.text, "state.player = state.player;"));
    candidate_syntax = script_parse(result.text);
    candidate_analysis = script_analyze_document(
        candidate_syntax,
        &declarations,
        analysis->identity
    );
    REQUIRE(candidate_analysis->valid);
    return true;
}
/* A handler that names its payload fields shows every field as an entry pin,
 * an unlisted one under the field's name. A listed field connects by its
 * name; an unlisted one has no source to read until it is listed. */
/* A created handler lists every payload field with its type, so each pin
 * can be wired; a handler for an event without a payload lists none. */
TEST_CASE(source_created_handlers_list_payload_fields)
{
    static const script_catalog_parameter fields[] = {
        { .name = "amount", .type = { .storage = PIN_F64 }, .unit = "charge" },
        { .name = "step", .type = { .storage = PIN_F64 }, .unit = "s" },
    };
    static const script_catalog_capability events[] = {
        { .id = 100, .name = "Tick", .kind = SCRIPT_CAPABILITY_EVENT },
        { .id = 101,
          .name = "Impulse",
          .kind = SCRIPT_CAPABILITY_EVENT,
          .parameters = fields,
          .parameter_count = 2 },
    };
    static const script_catalog with = { .capabilities = events,
                                         .capability_count = 2 };
    const char *names[] = { "Impulse", "Tick" };
    const char *headers[] = {
        "on Impulse :: (amount: f64 charge, step: f64 s) {}",
        "on Tick :: () {}"
    };
    for (int i = 0; i < 2; i++) {
        syntax = script_parse("Edit :: behavior { total:f64=0; }");
        analysis = script_analyze_document(syntax, &with, "tests/source-edit");
        view = authoring_source_presentation_new(analysis, &with, NULL);
        REQUIRE(analysis->valid && view);
        authoring_edit_request r = { .kind = AUTHORING_EDIT_CREATE_EVENT,
                                     .generation = authoring_source_edit_generation(view),
                                     .reference = names[i] };
        REQUIRE(authoring_source_edit(analysis, &with, view, &r, &result, error, sizeof error));
        REQUIRE(strstr(result.text, headers[i]));
        script_syntax *created = script_parse(result.text);
        script_analysis
            *checked = script_analyze_document(created, &with, analysis->identity);
        REQUIRE(checked->valid);
        script_analysis_free(checked);
        script_syntax_free(created);
        cleanup();
    }
    return true;
}
TEST_CASE(source_connections_read_listed_payload_fields)
{
    static const script_catalog_parameter fields[] = {
        { .name = "amount", .type = { .storage = PIN_F64 } },
        { .name = "scale", .type = { .storage = PIN_F64 } },
    };
    static const script_catalog_capability impulse = { .id = 101,
                                                       .name = "Impulse",
                                                       .kind = SCRIPT_CAPABILITY_EVENT,
                                                       .parameters = fields,
                                                       .parameter_count = 2 };
    static const script_catalog events = { .capabilities = &impulse,
                                           .capability_count = 1 };
    syntax = script_parse("Edit :: behavior { total:f64=0; on Impulse :: (scale: f64) { total=7; } }");
    analysis = script_analyze_document(syntax, &events, "tests/source-edit");
    view = authoring_source_presentation_new(analysis, &events, NULL);
    REQUIRE(analysis->valid && view);
    graph *g = behavior_asset_find_graph_by_name(authoring_source_presentation_view(view), "Impulse");
    node *entry = g ? graph_node_by_id(g, g->entry_id) : NULL;
    REQUIRE(entry && entry->output_count == 3);
    REQUIRE(!strcmp(entry->output_pins[1].name, "amount"));
    REQUIRE(!strcmp(entry->output_pins[2].name, "scale"));
    const source_presentation_origin *target = find(SOURCE_PRESENTATION_EXPRESSION, "7;"),
                                     *pins[2] = { 0 };
    for (int i = 0; i < authoring_source_presentation_origin_count(view); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(view, i);
        for (int k = 0; k < 2; k++) {
            if (o->kind == SOURCE_PRESENTATION_SYMBOL &&
                o->pin == entry->output_pins[k + 1].id) {
                pins[k] = o;
            }
        }
    }
    REQUIRE(target && pins[0] && pins[1]);
    authoring_edit_request r = request(AUTHORING_EDIT_CONNECT, target);
    r.producer = pins[0]->identity;
    REQUIRE(!authoring_source_edit(analysis, &events, view, &r, &result, error, sizeof error));
    REQUIRE(strstr(error, "no current producer"));
    r.producer = pins[1]->identity;
    REQUIRE(authoring_source_edit(analysis, &events, view, &r, &result, error, sizeof error));
    REQUIRE(strstr(result.text, "total=scale;"));
    return true;
}
TEST_CASE(source_connections_reference_retained_results_and_reject_repeated_effects)
{
    REQUIRE(
        project("Edit :: behavior { count:f64=0; Pair :: () -> (total:f64,message:string) { count+=1; return (count,\"snapshot\"); } Identity :: (amount:f64)->(answer:f64) { return amount; } on Tick :: () { saved:=Pair(); count=7; } }")
    );
    const source_presentation_origin *target = find(SOURCE_PRESENTATION_EXPRESSION, "7;");
    const source_presentation_origin *producer = find(SOURCE_PRESENTATION_EXPRESSION, "Pair(); count");
    REQUIRE(target && producer);
    authoring_edit_request r = request(AUTHORING_EDIT_CONNECT, target);
    r.producer = producer->identity;
    r.producer_result = 0;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "count=saved.total;") && !strstr(result.text, "count=(Pair())"));
    authoring_edit_result_free(&result);
    r.producer_result = 1;
    bool accepted = edit(r);
    if (accepted) {
        fprintf(
            stderr,
            "source-edit accepted result %d: %s\n",
            r.producer_result,
            result.text
        );
        script_syntax *probe = script_parse(result.text);
        script_analysis
            *facts = script_analyze_document(probe, &catalog, analysis->identity);
        for (int i = 0; i < facts->expression_count; i++) {
            const script_expression *e = &facts->expressions[i];
            if (e->kind == SCRIPT_EXPR_MEMBER) {
                fprintf(
                    stderr,
                    "source-edit member type=%d flags=%u offset=%zu "
                    "results=%d\n",
                    e->type.storage,
                    e->type.flags,
                    e->offset,
                    e->result_count
                );
            }
        }
        script_analysis_free(facts);
        script_syntax_free(probe);
    }
    REQUIRE(!accepted && error[0]);
    authoring_edit_result_free(&result);
    r = request(AUTHORING_EDIT_CREATE_CAPABILITY, target);
    r.previous = target->identity;
    r.producer = producer->identity;
    r.capability_name = "Identity";
    r.producer_result = 0;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "saved.total") && strstr(result.text, "count=created_"));
    authoring_edit_result_free(&result);
    r.producer_result = 1;
    REQUIRE(!edit(r) && error[0] && !result.text);
    cleanup();
    REQUIRE(
        project("Edit :: behavior { count:f64=0; Next :: ()->(answer:f64) { count+=1; return count; } on Tick :: () { count=7; count=Next()+1; } }")
    );
    target = find(SOURCE_PRESENTATION_EXPRESSION, "7;");
    producer = find(SOURCE_PRESENTATION_EXPRESSION, "Next()+1;");
    REQUIRE(target && producer);
    r = request(AUTHORING_EDIT_CONNECT, target);
    r.producer = producer->identity;
    REQUIRE(!edit(r) && strstr(error, "evaluate it twice"));
    cleanup();
    REQUIRE(
        project("Edit :: behavior { count:f64=1; on Tick :: () { saved:=count+2; if (true) { saved:f64=10; count=7; } } }")
    );
    target = find(SOURCE_PRESENTATION_EXPRESSION, "7;");
    producer = find(SOURCE_PRESENTATION_EXPRESSION, "count+2;");
    REQUIRE(target && producer);
    r = request(AUTHORING_EDIT_CONNECT, target);
    r.producer = producer->identity;
    REQUIRE(!edit(r) && strstr(error, "different declaration"));
    return true;
}
TEST_CASE(source_statement_moves_keep_comments_and_validate_declaration_order)
{
    REQUIRE(
        project("Edit :: behavior { count:f64=0; on Tick :: () {\n // first\n count=1; // attached\n count=2;\n } }")
    );
    const source_presentation_origin *first = find(SOURCE_PRESENTATION_STATEMENT, "count=1;");
    const source_presentation_origin *second = find(SOURCE_PRESENTATION_STATEMENT, "count=2;");
    REQUIRE(first && second);
    authoring_edit_request r = request(AUTHORING_EDIT_MOVE_AFTER, first);
    r.producer = second->identity;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "count=2;\n // first\n count=1; // attached\n"));
    REQUIRE(inspect_result());
    authoring_edit_result_free(&result);
    r = request(AUTHORING_EDIT_DELETE_STATEMENT, first);
    REQUIRE(edit(r));
    REQUIRE(
        !strstr(result.text, "first") && !strstr(result.text, "attached") &&
        strstr(result.text, "count=2;")
    );
    cleanup();
    REQUIRE(project("Edit :: behavior { count:f64=0; on Tick :: () { current:=1; count=current; } }"));
    first = find(SOURCE_PRESENTATION_STATEMENT, "current:=1;");
    second = find(SOURCE_PRESENTATION_STATEMENT, "count=current;");
    REQUIRE(first && second);
    r = request(AUTHORING_EDIT_MOVE_AFTER, first);
    r.producer = second->identity;
    REQUIRE(!edit(r) && error[0]);
    return true;
}
TEST_CASE(source_fused_call_edits_select_expression_or_statement_by_operation)
{
    REQUIRE(project(
        "Edit :: behavior { count:f64=0; Add :: (amount:f64)->() { "
        "count+=amount; } "
        "on Tick :: () {\n // first call\n Add(1.0); // attached\n "
        "count=2.0;\n "
        "Add(3.0);\n } }"
    ));
    const source_presentation_origin *first = find(SOURCE_PRESENTATION_EXPRESSION, "Add(1.0)");
    const source_presentation_origin *last = find(SOURCE_PRESENTATION_EXPRESSION, "Add(3.0)");
    const source_presentation_origin *statement = find(SOURCE_PRESENTATION_STATEMENT, "Add(1.0)");
    REQUIRE(first && last && statement && first->node == statement->node);
    REQUIRE(authoring_source_presentation_origin(view, first->graph, first->node, 0) == first);
    authoring_edit_request r = request(AUTHORING_EDIT_DELETE_STATEMENT, first);
    REQUIRE(edit(r));
    REQUIRE(
        !strstr(result.text, "Add(1.0)") && !strstr(result.text, "first call") &&
        !strstr(result.text, "attached") && strstr(result.text, "Add(3.0);")
    );
    authoring_edit_result_free(&result);

    r = request(AUTHORING_EDIT_MOVE_AFTER, first);
    r.producer = last->identity;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "Add(3.0);\n // first call\n Add(1.0); // attached\n"));
    authoring_edit_result_free(&result);

    r = request(AUTHORING_EDIT_EXPRESSION, first);
    r.expression = "Add(4.0)";
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "Add(4.0); // attached") && !strstr(result.text, "Add(1.0)"));
    authoring_edit_result_free(&result);

    r = request(AUTHORING_EDIT_INSERT_AFTER, first);
    r.fragment = "count=5.0;";
    REQUIRE(edit(r));
    const char *anchor = strstr(result.text, "Add(1.0); // attached\n");
    const char *inserted = strstr(result.text, "count=5.0;\n");
    const char *following = strstr(result.text, "count=2.0;");
    REQUIRE(anchor && inserted && following && anchor < inserted && inserted < following);
    authoring_edit_result_free(&result);

    r = request(AUTHORING_EDIT_EXTRACT_FUNCTION, first);
    r.name = "Advance";
    REQUIRE(edit(r));
    REQUIRE(inspect_result());
    REQUIRE(strstr(result.text, "Advance :: ()") && strstr(result.text, "Add(1.0);"));
    return true;
}
TEST_CASE(source_fused_action_call_mute_edits_its_owned_declaration)
{
    REQUIRE(project(
        "Edit :: behavior { Count :: ()->(value:f64) { return 1.0; } "
        "action act=Count(); on Tick :: () { act(); } }"
    ));
    const source_presentation_origin *call = find(SOURCE_PRESENTATION_EXPRESSION, "act();");
    REQUIRE(call);
    authoring_edit_request r = request(AUTHORING_EDIT_SET_DISABLED, call);
    r.disabled = true;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "disabled action act=Count();") && strstr(result.text, "act(); }"));
    REQUIRE(inspect_result());
    cleanup();

    REQUIRE(project(
        "Edit :: behavior { Count :: ()->(value:f64) { return 1.0; } "
        "disabled action act=Count(); on Tick :: () { act(); } }"
    ));
    call = find(SOURCE_PRESENTATION_EXPRESSION, "act();");
    REQUIRE(call);
    r = request(AUTHORING_EDIT_SET_DISABLED, call);
    r.disabled = false;
    REQUIRE(edit(r));
    REQUIRE(!strstr(result.text, "disabled") && strstr(result.text, "action act=Count();"));
    REQUIRE(inspect_result());
    return true;
}
TEST_CASE(source_selection_transactions_preserve_names_and_comments)
{
    REQUIRE(
        project("Edit :: behavior { count:f64=0; on Tick :: () {\n // snapshot\n current:=1;\n count=current;\n count=2;\n } }")
    );
    const source_presentation_origin *local = find(SOURCE_PRESENTATION_STATEMENT, "current:=1;");
    const source_presentation_origin *use = find(SOURCE_PRESENTATION_STATEMENT, "count=current;");
    REQUIRE(local && use);
    uint64_t ids[] = { local->identity, use->identity };
    char *clipboard = authoring_source_clipboard(view, ids, 2, error, sizeof error);
    REQUIRE(clipboard && strstr(clipboard, "snapshot") && strstr(clipboard, "count=current;"));
    free(clipboard);
    authoring_edit_request r = request(AUTHORING_EDIT_DUPLICATE_SELECTION, local);
    r.origins = ids;
    r.origin_count = 2;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "copy_") && strstr(result.text, "count=copy_"));
    REQUIRE(inspect_result());
    authoring_edit_result_free(&result);
    r.kind = AUTHORING_EDIT_DELETE_SELECTION;
    REQUIRE(edit(r));
    REQUIRE(
        !strstr(result.text, "snapshot") && !strstr(result.text, "current") &&
        strstr(result.text, "count=2;")
    );
    authoring_edit_result_free(&result);
    r.kind = AUTHORING_EDIT_WRAP_IF;
    r.expression = "true";
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "if (true)") && strstr(result.text, "snapshot"));
    authoring_edit_result_free(&result);
    r = request(AUTHORING_EDIT_APPEND_BODY, use);
    r.fragment = "} } Escaped :: behavior {";
    REQUIRE(!edit(r) && error[0]);
    cleanup();
    REQUIRE(project("Edit :: behavior { old:=1; on Tick :: () { current:=old; } }"));
    const source_presentation_origin *named = find(SOURCE_PRESENTATION_SYMBOL, "old:=1;");
    REQUIRE(named);
    r = request(AUTHORING_EDIT_RENAME_DECLARATION, named);
    r.name = "renamed";
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "renamed:=1") && strstr(result.text, "current:=renamed"));
    authoring_edit_result_free(&result);
    r.kind = AUTHORING_EDIT_SET_DISABLED;
    r.disabled = true;
    REQUIRE(!edit(r) && error[0] && !result.text);
    r.kind = AUTHORING_EDIT_CREATE_FUNCTION;
    r.name = "Helper";
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "Helper :: () {}"));
    cleanup();
    REQUIRE(project("Edit :: behavior { old:=1; on Tick :: () { renamed:=2; current:=old; } }"));
    named = find(SOURCE_PRESENTATION_SYMBOL, "old:=1;");
    REQUIRE(named);
    r = request(AUTHORING_EDIT_RENAME_DECLARATION, named);
    r.name = "renamed";
    REQUIRE(!edit(r) && error[0] && !result.text);
    return true;
}
TEST_CASE(source_optional_arguments_and_typed_values_are_checked_atomically)
{
    REQUIRE(
        project("Edit :: behavior { count:f64=0; Add :: (a:f64=2,b:f64=3)->(total:f64) { return a+b; } on Tick :: () { current:=Add(a:1,b:4); count=current; } }")
    );
    const source_presentation_origin *first = find(SOURCE_PRESENTATION_EXPRESSION, "1,b:4");
    const source_presentation_origin *second = find(SOURCE_PRESENTATION_EXPRESSION, "4);");
    const source_presentation_origin *producer = find(SOURCE_PRESENTATION_EXPRESSION, "0; Add :: ");
    REQUIRE(first && second && producer);
    authoring_edit_request r = request(AUTHORING_EDIT_DISCONNECT, first);
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "Add(b:4)") && !strstr(result.text, "Add(a:1"));
    authoring_edit_result_free(&result);
    r = request(AUTHORING_EDIT_RECONNECT, second);
    r.previous = first->identity;
    r.producer = producer->identity;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "Add(b:(0))"));
    authoring_edit_result_free(&result);
    pin_value number = { .type = PIN_F64, .d = 1.25 };
    r = request(AUTHORING_EDIT_VALUE, first);
    r.value = &number;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "Add(a:1.25,b:4)"));
    authoring_edit_result_free(&result);
    pin_value text = { .type = PIN_STR,
                       .text = &(const pin_string){ .bytes = "wrong",
                                                    .length = sizeof("wrong") - 1 } };
    r.value = &text;
    REQUIRE(!edit(r) && error[0]);
    cleanup();
    REQUIRE(
        project("Edit :: behavior { count:f64=0; Required :: (a:f64)->(total:f64) { return a; } on Tick :: () { count=Required(1); } }")
    );
    first = find(SOURCE_PRESENTATION_EXPRESSION, "1);");
    REQUIRE(first);
    r = request(AUTHORING_EDIT_DISCONNECT, first);
    REQUIRE(!edit(r) && error[0] && !result.text);
    return true;
}
TEST_CASE(source_extraction_preserves_ref_inputs_and_escaping_locals)
{
    REQUIRE(project("Edit :: behavior { count:f64=0; on Tick :: () { x:=1.0; x+=2.0; count=x; } }"));
    const source_presentation_origin *first = find(SOURCE_PRESENTATION_STATEMENT, "x:=1.0;");
    const source_presentation_origin *second = find(SOURCE_PRESENTATION_STATEMENT, "x+=2.0;");
    REQUIRE(first && second);
    uint64_t selection[] = { first->identity, second->identity };
    authoring_edit_request r = request(AUTHORING_EDIT_EXTRACT_FUNCTION, first);
    r.origins = selection;
    r.origin_count = 2;
    r.name = "Advance";
    REQUIRE(edit(r));
    REQUIRE(inspect_result());
    REQUIRE(strstr(result.text, "Advance :: ()"));
    REQUIRE(strstr(result.text, "x+=2.0;"));
    REQUIRE(strstr(result.text, "x := refactor_result_1;"));
    REQUIRE(strstr(analysis->text, "x:=1.0; x+=2.0; count=x;"));
    cleanup();
    REQUIRE(project("Edit :: behavior { count:f64=0; on Tick :: () { x:=1.0; x+=2.0; count=x; } }"));
    second = find(SOURCE_PRESENTATION_STATEMENT, "x+=2.0;");
    REQUIRE(second);
    r = request(AUTHORING_EDIT_EXTRACT_MACRO, second);
    r.name = "Advance";
    REQUIRE(edit(r));
    REQUIRE(inspect_result());
    REQUIRE(strstr(result.text, "refactor_input_1_0: ^number"));
    REQUIRE(strstr(result.text, "refactor_input_1_0^+=2.0;"));
    REQUIRE(strstr(result.text, "Advance(&x)"));
    REQUIRE(strstr(result.text, "after entry"));
    REQUIRE(strstr(result.text, "exit Then;"));
    cleanup();
    REQUIRE(project("Edit :: behavior { count:f64=0; on Tick :: () { x:=1.0; x+=2.0; count=x; } }"));
    second = find(SOURCE_PRESENTATION_STATEMENT, "x+=2.0;");
    REQUIRE(second);
    r = request(AUTHORING_EDIT_EXTRACT_FUNCTION, second);
    r.name = "Advance";
    REQUIRE(edit(r) && inspect_result());
    REQUIRE(strstr(result.text, "refactor_input_1_0: ^number"));
    REQUIRE(strstr(result.text, "Advance(&x)"));
    cleanup();
    REQUIRE(project("Edit :: behavior { Early :: ()->() { return; } on Tick :: () {} }"));
    const source_presentation_origin *ret = find(SOURCE_PRESENTATION_STATEMENT, "return;");
    REQUIRE(ret);
    r = request(AUTHORING_EDIT_EXTRACT_FUNCTION, ret);
    r.name = "Moved";
    REQUIRE(!edit(r) && error[0] && !result.text);
    const char *retained[] = {
        "Edit :: behavior { Next :: () { visits:retain int=0; "
        "visits+=1; } on Tick :: () { Next(); } }",
        "Edit :: system() { Next :: () { visits:retain int=0; "
        "visits+=1; } Next(); }"
    };
    for (int i = 0; i < 2; i++) {
        cleanup();
        REQUIRE(project(retained[i]));
        first = find(SOURCE_PRESENTATION_SYMBOL, "visits:retain");
        REQUIRE(first);
        r = request(AUTHORING_EDIT_EXTRACT_FUNCTION, first);
        r.name = "Moved";
        REQUIRE(!edit(r) && strstr(error, "storage identity") && !result.text);
    }
    return true;
}
TEST_CASE(source_pointer_calls_are_created_and_inlining_is_rejected)
{
    REQUIRE(
        project("Edit :: behavior { count:f64=0; Bump :: (value:^f64) { value^+=1; } on Tick :: () {} }")
    );
    const source_presentation_origin *handler = find(SOURCE_PRESENTATION_FUNCTION, "on Tick");
    const source_presentation_origin *producer = find(SOURCE_PRESENTATION_SYMBOL, "count:f64");
    REQUIRE(handler && producer);
    authoring_edit_request r = request(AUTHORING_EDIT_CREATE_CAPABILITY, handler);
    r.capability_name = "Bump";
    r.producer = producer->identity;
    REQUIRE(edit(r) && inspect_result());
    REQUIRE(strstr(result.text, "Bump(&count)"));
    cleanup();
    REQUIRE(
        project("Edit :: behavior { count:f64=0; Bump :: (value:^f64) { value^+=1; } on Tick :: () { Bump(value:&count); } }")
    );
    const source_presentation_origin *call = find(SOURCE_PRESENTATION_EXPRESSION, "Bump(value:&count)");
    REQUIRE(call);
    REQUIRE(!edit(request(AUTHORING_EDIT_INLINE_CALL, call)));
    REQUIRE(strstr(error, "Inlining pointer parameters requires local aliases"));
    REQUIRE(!result.text && !result.metadata);
    REQUIRE(strstr(analysis->text, "Bump(value:&count);"));
    return true;
}
/* Inlining spells a char parameter as char. */
TEST_CASE(source_inline_spells_char_parameters)
{
    REQUIRE(
        project("Edit :: behavior { count:int=0; Next :: (letter:char)->(code:int) { return letter+1; } on Tick :: () { count=Next('a'); } }")
    );
    const source_presentation_origin *call = find(SOURCE_PRESENTATION_EXPRESSION, "Next('a')");
    REQUIRE(call);
    REQUIRE(edit(request(AUTHORING_EDIT_INLINE_CALL, call)));
    REQUIRE(inspect_result());
    REQUIRE(strstr(result.text, "letter: char = ('a');"));
    return true;
}
TEST_CASE(source_inline_renames_resolved_locals_and_preserves_early_return)
{
    REQUIRE(
        project("Edit :: behavior { count:f64=0; Add :: (input:f64)->(amount:f64) { temp:=input+1.0; if(input>0.0) { return temp; } return 0.0; } on Tick :: () { temp:=9.0; count=Add(2.0); } }")
    );
    const source_presentation_origin *call = find(SOURCE_PRESENTATION_EXPRESSION, "Add(2.0)");
    REQUIRE(call);
    authoring_edit_request r = request(AUTHORING_EDIT_INLINE_CALL, call);
    graph *call_graph = behavior_asset_find_graph(authoring_source_presentation_view(view), call->graph);
    node *call_node = graph_node_by_id(call_graph, call->node);
    call_node->breakpoint = 1;
    REQUIRE(!edit(r) && strstr(error, "breakpoint or watched value") && !result.text);
    call_node->breakpoint = 0;
    graph_set_node_comment(call_graph, call_node, "Keep this explanation", false);
    REQUIRE(!edit(r) && strstr(error, "node comment") && !result.text);
    graph_set_node_comment(call_graph, call_node, "", false);
    const source_presentation_origin *definition = find(SOURCE_PRESENTATION_FUNCTION, "Add :: ");
    REQUIRE(definition);
    graph *definition_graph = behavior_asset_find_graph(
        authoring_source_presentation_view(view),
        definition->graph
    );
    node *definition_node = graph_node_by_id(definition_graph, definition->node);
    REQUIRE(graph_set_pin_watch(definition_graph, definition_node, true, 1, true) == 0);
    REQUIRE(!edit(r) && strstr(error, "breakpoint or watched value") && !result.text);
    REQUIRE(graph_set_pin_watch(definition_graph, definition_node, true, 1, false) == 0);
    REQUIRE(edit(r));
    REQUIRE(inspect_result());
    REQUIRE(strstr(result.text, "goto Add_Completed;"));
    REQUIRE(strstr(result.text, "Add_Completed:"));
    REQUIRE(strstr(result.text, "input: f64"));
    REQUIRE(strstr(result.text, "temp_2:=input+1.0;"));
    REQUIRE(!strstr(result.text, "inline_"));
    REQUIRE(strstr(result.text, "temp:=9.0;"));
    REQUIRE(!strstr(result.text, "count=Add(2.0)"));
    REQUIRE(strstr(analysis->text, "count=Add(2.0)"));
    cleanup();
    REQUIRE(project(
        "Edit :: behavior { count:f64=0;"
        " Scale :: (input_speed:f64,_return:f64)->(sum_total:f64) {"
        " local_value:=input_speed+_return; return "
        "local_value; }"
        " on Tick :: () { local_value:=9.0; count=Scale(2.0,1.0); } }"
    ));
    call = find(SOURCE_PRESENTATION_EXPRESSION, "Scale(2.0,1.0)");
    REQUIRE(call);
    r = request(AUTHORING_EDIT_INLINE_CALL, call);
    REQUIRE(edit(r));
    REQUIRE(inspect_result());
    REQUIRE(strstr(result.text, "input_speed: f64"));
    REQUIRE(strstr(result.text, "_return: f64"));
    REQUIRE(strstr(result.text, "local_value_2:=input_speed+_return;"));
    REQUIRE(strstr(result.text, "count=sum_total;"));
    REQUIRE(!strstr(result.text, "goto ") && !strstr(result.text, "Completed"));
    REQUIRE(!strstr(result.text, "inline_"));
    cleanup();
    REQUIRE(
        project("Edit :: behavior { count:f64=0; Add :: (input:f64)->(amount:f64) { return input; } on Tick :: () { count=1.0+Add(2.0); } }")
    );
    call = find(SOURCE_PRESENTATION_EXPRESSION, "Add(2.0)");
    REQUIRE(call);
    r = request(AUTHORING_EDIT_INLINE_CALL, call);
    REQUIRE(!edit(r) && error[0] && !result.text);
    cleanup();
    REQUIRE(
        project("Edit :: behavior { count:f64=1; Read :: (input:f64)->(amount:f64) { return count; } on Tick :: () { count:=9.0; current:=Read(0.0); } }")
    );
    call = find(SOURCE_PRESENTATION_EXPRESSION, "Read(0.0)");
    REQUIRE(call);
    r = request(AUTHORING_EDIT_INLINE_CALL, call);
    REQUIRE(!edit(r) && strstr(error, "shadows") && !result.text);
    const char *retained[] = {
        "Edit :: behavior { Next :: () -> int { visits:retain int=0; "
        "visits+=1; return visits; } on Tick :: () { current:=Next(); } }",
        "Edit :: system() { Next :: () -> int { visits:retain int=0; "
        "visits+=1; return visits; } current:=Next(); }"
    };
    for (int i = 0; i < 2; i++) {
        cleanup();
        REQUIRE(project(retained[i]));
        call = find(SOURCE_PRESENTATION_EXPRESSION, "Next();");
        REQUIRE(call);
        r = request(AUTHORING_EDIT_INLINE_CALL, call);
        REQUIRE(!edit(r) && strstr(error, "persistent") && !result.text);
    }
    return true;
}

TEST_CASE(source_signatures_and_bindings_preserve_resolved_body_and_scope)
{
    REQUIRE(
        project("Edit :: behavior { a:f64=0.5; count:f64=0; Add :: (a:f64)->(amount:f64) { return a; } on Tick :: () { count=Add(2); } }")
    );
    const source_presentation_origin *fn = find(SOURCE_PRESENTATION_FUNCTION, "Add :: ");
    const source_presentation_origin *target = find(SOURCE_PRESENTATION_EXPRESSION, "2);");
    const source_presentation_origin *state = find(SOURCE_PRESENTATION_SYMBOL, "a:f64=0.5");
    REQUIRE(fn && target && state);
    authoring_edit_request r = request(AUTHORING_EDIT_SIGNATURE, fn);
    r.signature = "(a:number)->(amount:f64)";
    REQUIRE(edit(r));
    REQUIRE(
        strstr(result.text, "Add :: (a:number)->(amount:f64)") &&
        strstr(result.text, "{ return a; }")
    );
    authoring_edit_result_free(&result);
    r.signature = "(b:f64)->(amount:f64)";
    REQUIRE(!edit(r) && error[0] && !result.text);
    r.signature = "()->() {} Escaped :: ()->()";
    REQUIRE(!edit(r) && error[0] && !result.text);
    r = request(AUTHORING_EDIT_BIND_INPUT, target);
    r.binding = AUTHORING_BIND_SYMBOL;
    r.symbol = analysis->symbols[state->record].id;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "count=Add(a)"));
    authoring_edit_result_free(&result);
    cleanup();
    REQUIRE(project("Edit :: behavior { a:f64=1; count:f64=0; on Tick :: () { a:=9; count=2; } }"));
    state = find(SOURCE_PRESENTATION_SYMBOL, "a:f64=1;");
    target = find(SOURCE_PRESENTATION_EXPRESSION, "2;");
    REQUIRE(state && target);
    r = request(AUTHORING_EDIT_BIND_INPUT, target);
    r.binding = AUTHORING_BIND_SYMBOL;
    r.symbol = analysis->symbols[state->record].id;
    REQUIRE(!edit(r) && error[0] && !result.text);
    return true;
}

TEST_CASE(source_variable_types_preserve_comments_values_and_checked_uses)
{
    REQUIRE(
        project("// state file\n/* typed variables */\nEdit :: behavior { /* keep state */ value:bool = /* keep default */ false; on Tick :: () { copy:=value; } }")
    );
    const source_presentation_origin *state = find(SOURCE_PRESENTATION_SYMBOL, "value:bool");
    REQUIRE(state);
    authoring_edit_request append = request(AUTHORING_EDIT_APPEND_DECLARATIONS, state);
    append.fragment = "score:int=0;";
    REQUIRE(edit(append));
    REQUIRE(strstr(result.text, "// state file\n/* typed variables */"));
    REQUIRE(strstr(result.text, "score:int=0;"));
    authoring_edit_result_free(&result);
    authoring_edit_request retype = request(AUTHORING_EDIT_RETYPE_VARIABLE, state);
    retype.signature = "int";
    REQUIRE(edit(retype));
    REQUIRE(strstr(result.text, "/* keep state */ value:int = /* keep default */ 0"));
    REQUIRE(strstr(result.text, "copy:=value"));
    REQUIRE(inspect_result());
    cleanup();
    REQUIRE(
        project("Edit :: behavior { amount:= /* keep inferred */ 3; on Tick :: () { amount=amount+1; } }")
    );
    state = find(SOURCE_PRESENTATION_SYMBOL, "amount:=");
    REQUIRE(state);
    retype = request(AUTHORING_EDIT_RETYPE_VARIABLE, state);
    retype.signature = "number";
    REQUIRE(edit(retype));
    REQUIRE(strstr(result.text, "amount: number = /* keep inferred */ 3"));
    REQUIRE(strstr(result.text, "amount=amount+1"));
    REQUIRE(inspect_result());
    authoring_edit_result_free(&result);
    retype.signature = "bool";
    REQUIRE(!edit(retype)); /* Existing arithmetic must remain well typed. */
    retype.signature = "MissingType";
    REQUIRE(!edit(retype));
    retype.signature = "int; injected:int";
    REQUIRE(!edit(retype));
    cleanup();
    REQUIRE(project("Edit :: behavior { values:[string: int]; }"));
    state = find(SOURCE_PRESENTATION_SYMBOL, "values:[string: int]");
    REQUIRE(state);
    retype = request(AUTHORING_EDIT_RETYPE_VARIABLE, state);
    retype.signature = "[string: number]";
    REQUIRE(edit(retype));
    REQUIRE(strstr(result.text, "values:[string: number]"));
    REQUIRE(inspect_result());
    cleanup();
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
    static const script_catalog_parameter operands[] = {
        { .name = "a", .type = { .storage = PIN_STRUCT, .nominal = 10 } },
        { .name = "b", .type = { .storage = PIN_STRUCT, .nominal = 10 } }
    };
    static const script_catalog_parameter output[] = {
        { .name = "value", .type = { .storage = PIN_STRUCT, .nominal = 10 } }
    };
    static const script_catalog_capability component_caps[] = {
        { .id = 100, .name = "Tick", .kind = SCRIPT_CAPABILITY_EVENT },
        { .id = 31,
          .name = "VectorAdd",
          .operator_name = "+",
          .parameters = operands,
          .parameter_count = 2,
          .results = output,
          .result_count = 1,
          .pure = true }
    };
    static const script_catalog components = { .types = types,
                                               .type_count = 1,
                                               .entity_type = 40,
                                               .capabilities = component_caps,
                                               .capability_count = 2 };
    syntax = script_parse("// component compound assignment\nEdit :: behavior { on Tick :: () { self.Position += self.Position; } }");
    analysis = script_analyze_document(syntax, &components, "tests/source-edit");
    REQUIRE(analysis->valid);
    view = authoring_source_presentation_new(analysis, &components, NULL);
    REQUIRE(view);
    int member_start = (int)(strstr(analysis->text, "self.Position") - analysis->text);
    int decorated = 0;
    for (int i = 0; i < authoring_source_presentation_origin_count(view); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(view, i);
        if (o->kind != SOURCE_PRESENTATION_EXPRESSION || o->pin ||
            o->operation != SCRIPT_EXPR_COMPONENT || o->range.start != member_start ||
            strncmp(analysis->text + o->range.start, "self.Position", 13)) {
            continue;
        }
        graph *g = behavior_asset_find_graph(authoring_source_presentation_view(view), o->graph);
        node *n = graph_node_by_id(g, o->node);
        n->x = n->desc->display_class + 37;
        n->y = 51;
        n->placed = true;
        graph_set_node_comment(g, n, n->desc->name, false);
        bool output_pin = n->desc->display_class != NODE_DISPLAY_FLOW;
        pin_spec *pins = output_pin ? n->output_pins : n->input_pins;
        int pin_count = output_pin ? n->output_count : n->input_count;
        bool watched = false;
        for (int j = 0; j < pin_count; j++) {
            if (pins[j].kind == PIN_DATA) {
                REQUIRE(!graph_set_pin_watch(g, n, output_pin, j, true));
                watched = true;
            }
        }
        REQUIRE(watched);
        decorated++;
    }
    REQUIRE(decorated == 2);
    append = (authoring_edit_request){ .kind = AUTHORING_EDIT_APPEND_DECLARATIONS,
                                       .generation = authoring_source_edit_generation(view),
                                       .fragment = "NewVariable:entity;" };
    REQUIRE(
        authoring_source_edit(analysis, &components, view, &append, &result, error, sizeof error)
    );
    candidate_syntax = script_parse(result.text);
    candidate_analysis = script_analyze_document(
        candidate_syntax,
        &components,
        analysis->identity
    );
    REQUIRE(candidate_analysis->valid);
    candidate_view = authoring_source_presentation_new(candidate_analysis, &components, NULL);
    REQUIRE(
        candidate_view &&
        authoring_source_presentation_restore(candidate_view, result.metadata)
    );
    int retained = 0;
    for (int i = 0; i < authoring_source_presentation_origin_count(candidate_view);
         i++) {
        const source_presentation_origin
            *o = authoring_source_presentation_origin_at(candidate_view, i);
        if (o->kind != SOURCE_PRESENTATION_EXPRESSION || o->pin ||
            o->operation != SCRIPT_EXPR_COMPONENT || o->range.start != member_start ||
            strncmp(candidate_analysis->text + o->range.start, "self.Position", 13)) {
            continue;
        }
        graph *g = behavior_asset_find_graph(
            authoring_source_presentation_view(candidate_view),
            o->graph
        );
        node *n = graph_node_by_id(g, o->node);
        REQUIRE(n->placed && n->x == n->desc->display_class + 37 && n->y == 51);
        REQUIRE(n->comment && !strcmp(n->comment, n->desc->name));
        bool output_pin = n->desc->display_class != NODE_DISPLAY_FLOW;
        pin_spec *pins = output_pin ? n->output_pins : n->input_pins;
        int pin_count = output_pin ? n->output_count : n->input_count;
        for (int j = 0; j < pin_count; j++) {
            if (pins[j].kind == PIN_DATA) {
                REQUIRE(pins[j].flags & PIN_FLAG_WATCH);
            }
        }
        retained++;
    }
    REQUIRE(retained == decorated);
    return true;
}

TEST_CASE(source_retype_compact_inferred_retention_preserves_owner)
{
    const char *sources[] = {
        "Edit :: behavior { count:retain=/* keep */1; }",
        ("Edit :: behavior { F :: () -> int { count:retain=/* keep */1; return "
         "0; } }"),
        "Edit :: system() { count:retain=/* keep */1; }",
    };
    for (int i = 0; i < 3; i++) {
        cleanup();
        REQUIRE(project(sources[i]));
        const source_presentation_origin *state = find(SOURCE_PRESENTATION_SYMBOL, "count:");
        REQUIRE(state);
        authoring_edit_request retype = request(AUTHORING_EDIT_RETYPE_VARIABLE, state);
        retype.signature = "int";
        REQUIRE(edit(retype));
        REQUIRE(strstr(result.text, "count:retain int =/* keep */1"));
        REQUIRE(inspect_result());
        bool retained = false;
        for (int s = 0; s < candidate_analysis->symbol_count; s++) {
            const script_symbol *symbol = &candidate_analysis->symbols[s];
            if (!strcmp(symbol->name, "count")) {
                REQUIRE(symbol->kind == (i == 2 ? SCRIPT_SHARED_STATE : SCRIPT_STATE));
                REQUIRE(symbol->type.storage == PIN_I32);
                retained = true;
            }
        }
        REQUIRE(retained);
    }
    return true;
}

TEST_CASE(source_capability_creation_uses_properties_operators_and_positional_arguments)
{
    const script_catalog_field fields[] = {
        { .id = 20, .name = "x", .type = { .storage = PIN_F64 }, .writable = true }
    };
    const script_catalog_type types[] = { { .id = 10,
                                            .name = "Position",
                                            .type = { .storage = PIN_STRUCT, .nominal = 10 },
                                            .fields = fields,
                                            .field_count = 1,
                                            .size = sizeof(double),
                                            .component = true,
                                            .component_id = 501 } };
    const script_catalog_parameter owner[] = { { .name = "Entity",
                                                 .type = { .storage = PIN_HANDLE,
                                                           .nominal = 40 },
                                                 .default_owner = true } };
    const script_catalog_parameter setter[] = { owner[0],
                                                { .name = "Value",
                                                  .type = { .storage = PIN_F64 } } };
    const script_catalog_parameter scalar[] = { { .name = "Value",
                                                  .type = { .storage = PIN_F64 } } };
    const script_catalog_parameter operands[] = {
        { .name = "Left",
          .type = { .storage = PIN_F64 },
          .has_default = true,
          .default_value = { .type = PIN_F64, .d = 0 } },
        { .name = "Right",
          .type = { .storage = PIN_F64 },
          .has_default = true,
          .default_value = { .type = PIN_F64, .d = 0 } }
    };
    const script_catalog_parameter component[] = {
        { .name = "Value", .type = { .storage = PIN_STRUCT, .nominal = 10 } }
    };
    const script_catalog_parameter entity[] = {
        { .name = "Value", .type = { .storage = PIN_HANDLE, .nominal = 40 } }
    };
    const script_catalog_parameter boolean[] = { { .name = "Value",
                                                   .type = { .storage = PIN_BOOL } } };
    const script_catalog_capability capabilities[] = {
        tick,
        { .id = 31,
          .name = "get_position",
          .parameters = owner,
          .parameter_count = 1,
          .results = component,
          .result_count = 1,
          .component = 10,
          .component_access = SCRIPT_COMPONENT_ACCESS_READ,
          .component_entity = 0,
          .component_value = -1,
          .effects = SCRIPT_EFFECT_READ,
          .pure = true },
        { .id = 32,
          .name = "set_position_x",
          .parameters = setter,
          .parameter_count = 2,
          .component = 10,
          .component_field = 20,
          .component_access = SCRIPT_COMPONENT_ACCESS_WRITE,
          .component_entity = 0,
          .component_value = 1,
          .effects = SCRIPT_EFFECT_WRITE },
        { .id = 33,
          .name = "Identity",
          .parameters = scalar,
          .parameter_count = 1,
          .results = scalar,
          .result_count = 1,
          .pure = true },
        { .id = 34,
          .name = "Add",
          .operator_name = "+",
          .parameters = operands,
          .parameter_count = 2,
          .results = scalar,
          .result_count = 1,
          .pure = true },
        { .id = script_catalog_id("capability", "Self"),
          .name = "Self",
          .results = entity,
          .result_count = 1,
          .pure = true },
        { .id = 36,
          .name = "has_position",
          .parameters = owner,
          .parameter_count = 1,
          .results = boolean,
          .result_count = 1,
          .component = 10,
          .component_access = SCRIPT_COMPONENT_ACCESS_HAS,
          .component_entity = 0,
          .component_value = -1,
          .effects = SCRIPT_EFFECT_READ,
          .pure = true },
    };
    const script_catalog active = { .types = types,
                                    .type_count = 1,
                                    .entity_type = 40,
                                    .capabilities = capabilities,
                                    .capability_count = 7 };
    syntax = script_parse("Edit :: behavior { amount: f64 = 2; on Tick :: () {} }");
    analysis = script_analyze_document(syntax, &active, "tests/source-edit");
    REQUIRE(analysis->valid);
    view = authoring_source_presentation_new(analysis, &active, NULL);
    REQUIRE(view);
    const source_presentation_origin *handler = find(SOURCE_PRESENTATION_FUNCTION, "on Tick");
    const source_presentation_origin *producer = find(SOURCE_PRESENTATION_SYMBOL, "amount: f64");
    REQUIRE(handler && producer);
    for (int id = 31; id <= 36; id++) {
        authoring_edit_request r = request(AUTHORING_EDIT_CREATE_CAPABILITY, handler);
        r.capability = id == 35 ? script_catalog_id("capability", "Self")
                                : (uint64_t)id;
        r.producer = id == 32 || id == 33 || id == 34 ? producer->identity : 0;
        r.field = id == 31 ? "x" : NULL;
        REQUIRE(authoring_source_edit(analysis, &active, view, &r, &result, error, sizeof error));
        if (id == 31) {
            REQUIRE(strstr(result.text, "self.Position.x"));
            REQUIRE(!strstr(result.text, "get_position("));
        } else if (id == 32) {
            REQUIRE(strstr(result.text, "self.Position.x = "));
            REQUIRE(!strstr(result.text, "set_position_x("));
        } else if (id == 33) {
            REQUIRE(strstr(result.text, "Identity("));
            REQUIRE(!strstr(result.text, "Identity(Value:"));
        } else if (id == 34) {
            REQUIRE(strstr(result.text, ") + ("));
            REQUIRE(strstr(result.text, "(f64("));
            REQUIRE(!strstr(result.text, "Add("));
        } else if (id == 35) {
            REQUIRE(strstr(result.text, " := self;"));
            REQUIRE(!strstr(result.text, "Self("));
        } else {
            REQUIRE(strstr(result.text, "self.Position != nil"));
            REQUIRE(!strstr(result.text, "has_position("));
        }
        candidate_syntax = script_parse(result.text);
        candidate_analysis = script_analyze_document(
            candidate_syntax,
            &active,
            analysis->identity
        );
        REQUIRE(candidate_analysis->valid);
        script_analysis_free(candidate_analysis);
        script_syntax_free(candidate_syntax);
        candidate_analysis = NULL;
        candidate_syntax = NULL;
        authoring_edit_result_free(&result);
    }
    return true;
}

void test_source_edit()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_capability_creation_uses_properties_operators_and_positional_arguments);
    RUN_TEST_CASE(source_expression_edits_validate_value_scope_and_generation);
    RUN_TEST_CASE(source_setter_value_edits_preserve_the_destination);
    RUN_TEST_CASE(source_variable_get_set_creation_uses_declared_state_and_checked_values);
    RUN_TEST_CASE(source_variable_creation_uses_identifiers_and_keeps_nonliteral_defaults_editable);
    RUN_TEST_CASE(source_variable_types_preserve_comments_values_and_checked_uses);
    RUN_TEST_CASE(source_retype_compact_inferred_retention_preserves_owner);
    RUN_TEST_CASE(source_connections_reference_retained_results_and_reject_repeated_effects);
    RUN_TEST_CASE(source_connections_read_listed_payload_fields);
    RUN_TEST_CASE(source_created_handlers_list_payload_fields);
    RUN_TEST_CASE(source_statement_moves_keep_comments_and_validate_declaration_order);
    RUN_TEST_CASE(source_fused_call_edits_select_expression_or_statement_by_operation);
    RUN_TEST_CASE(source_fused_action_call_mute_edits_its_owned_declaration);
    RUN_TEST_CASE(source_selection_transactions_preserve_names_and_comments);
    RUN_TEST_CASE(source_optional_arguments_and_typed_values_are_checked_atomically);
    RUN_TEST_CASE(source_extraction_preserves_ref_inputs_and_escaping_locals);
    RUN_TEST_CASE(source_pointer_calls_are_created_and_inlining_is_rejected);
    RUN_TEST_CASE(source_inline_spells_char_parameters);
    RUN_TEST_CASE(source_inline_renames_resolved_locals_and_preserves_early_return);
    RUN_TEST_CASE(source_signatures_and_bindings_preserve_resolved_body_and_scope);
    pu_clear_setup();
}
