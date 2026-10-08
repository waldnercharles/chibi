#include "authoring/source_edit.h"

#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

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
    REQUIRE(project("behavior Edit { count:double=1; on Tick { count=2; } }"));
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
TEST_CASE(source_connections_reference_retained_results_and_reject_repeated_effects)
{
    REQUIRE(
        project("behavior Edit { count:double=0; function Pair() -> (total:double,message:string) { count+=1; return (count,\"snapshot\"); } function Identity(amount:double)->(answer:double) { return amount; } local saved=Pair(); on Tick { count=7; } }")
    );
    const source_presentation_origin *target = find(SOURCE_PRESENTATION_EXPRESSION, "7;");
    const source_presentation_origin *producer = find(SOURCE_PRESENTATION_EXPRESSION, "Pair(); on");
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
        project("behavior Edit { count:double=0; function Next()->(answer:double) { count+=1; return count; } value repeated=Next(); on Tick { count=7; } }")
    );
    target = find(SOURCE_PRESENTATION_EXPRESSION, "7;");
    producer = find(SOURCE_PRESENTATION_EXPRESSION, "Next(); on");
    REQUIRE(target && producer);
    r = request(AUTHORING_EDIT_CONNECT, target);
    r.producer = producer->identity;
    REQUIRE(!edit(r) && strstr(error, "evaluate it twice"));
    cleanup();
    REQUIRE(
        project("behavior Edit { count:double=1; local saved=count+2; on Tick { saved:double=10; count=7; } }")
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
        project("behavior Edit { count:double=0; on Tick {\n // first\n count=1; // attached\n count=2;\n } }")
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
    REQUIRE(project("behavior Edit { count:double=0; on Tick { current:=1; count=current; } }"));
    first = find(SOURCE_PRESENTATION_STATEMENT, "current:=1;");
    second = find(SOURCE_PRESENTATION_STATEMENT, "count=current;");
    REQUIRE(first && second);
    r = request(AUTHORING_EDIT_MOVE_AFTER, first);
    r.producer = second->identity;
    REQUIRE(!edit(r) && error[0]);
    return true;
}
TEST_CASE(source_selection_transactions_preserve_names_and_comments)
{
    REQUIRE(
        project("behavior Edit { count:double=0; on Tick {\n // snapshot\n current:=1;\n count=current;\n count=2;\n } }")
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
    r.fragment = "} } behavior Escaped {";
    REQUIRE(!edit(r) && error[0]);
    cleanup();
    REQUIRE(project("behavior Edit { value old=1; on Tick { current:=old; } }"));
    const source_presentation_origin *named = find(SOURCE_PRESENTATION_SYMBOL, "old=1;");
    REQUIRE(named);
    r = request(AUTHORING_EDIT_RENAME_DECLARATION, named);
    r.name = "renamed";
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "value renamed=1") && strstr(result.text, "current:=renamed"));
    authoring_edit_result_free(&result);
    r.kind = AUTHORING_EDIT_SET_DISABLED;
    r.disabled = true;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "disabled value old=1;"));
    authoring_edit_result_free(&result);
    r.kind = AUTHORING_EDIT_CREATE_FUNCTION;
    r.name = "Helper";
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "function \"Helper\"()->() {}"));
    cleanup();
    REQUIRE(project("behavior Edit { value old=1; on Tick { renamed:=2; current:=old; } }"));
    named = find(SOURCE_PRESENTATION_SYMBOL, "old=1;");
    REQUIRE(named);
    r = request(AUTHORING_EDIT_RENAME_DECLARATION, named);
    r.name = "renamed";
    REQUIRE(!edit(r) && error[0] && !result.text);
    return true;
}
TEST_CASE(source_optional_arguments_and_typed_values_are_checked_atomically)
{
    REQUIRE(
        project("behavior Edit { count:double=0; function Add(a:double=2,b:double=3)->(total:double) { return a+b; } on Tick { current:=Add(a:1,b:4); count=current; } }")
    );
    const source_presentation_origin *first = find(SOURCE_PRESENTATION_EXPRESSION, "1,b:4");
    const source_presentation_origin *second = find(SOURCE_PRESENTATION_EXPRESSION, "4);");
    const source_presentation_origin *producer = find(SOURCE_PRESENTATION_EXPRESSION, "0; function");
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
    pin_value text = { .type = PIN_STR, .str = "wrong" };
    r.value = &text;
    REQUIRE(!edit(r) && error[0]);
    cleanup();
    REQUIRE(
        project("behavior Edit { count:double=0; function Required(a:double)->(total:double) { return a; } on Tick { count=Required(1); } }")
    );
    first = find(SOURCE_PRESENTATION_EXPRESSION, "1);");
    REQUIRE(first);
    r = request(AUTHORING_EDIT_DISCONNECT, first);
    REQUIRE(!edit(r) && error[0] && !result.text);
    return true;
}
TEST_CASE(source_extraction_preserves_ref_inputs_and_escaping_locals)
{
    REQUIRE(project("behavior Edit { count:double=0; on Tick { x:=1.0; x+=2.0; count=x; } }"));
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
    REQUIRE(strstr(result.text, "function \"Advance\"()"));
    REQUIRE(strstr(result.text, "x+=2.0;"));
    REQUIRE(strstr(result.text, "\"x\" := refactor_result_1;"));
    REQUIRE(strstr(analysis->text, "x:=1.0; x+=2.0; count=x;"));
    cleanup();
    REQUIRE(project("behavior Edit { count:double=0; on Tick { x:=1.0; x+=2.0; count=x; } }"));
    second = find(SOURCE_PRESENTATION_STATEMENT, "x+=2.0;");
    REQUIRE(second);
    r = request(AUTHORING_EDIT_EXTRACT_MACRO, second);
    r.name = "Advance";
    REQUIRE(edit(r));
    REQUIRE(inspect_result());
    REQUIRE(strstr(result.text, "refactor_input_1_0: ref \"double\""));
    REQUIRE(strstr(result.text, "after entry"));
    REQUIRE(strstr(result.text, "exit Then;"));
    cleanup();
    REQUIRE(project("behavior Edit { function Early()->() { return; } on Tick {} }"));
    const source_presentation_origin *ret = find(SOURCE_PRESENTATION_STATEMENT, "return;");
    REQUIRE(ret);
    r = request(AUTHORING_EDIT_EXTRACT_FUNCTION, ret);
    r.name = "Moved";
    REQUIRE(!edit(r) && error[0] && !result.text);
    return true;
}
TEST_CASE(source_inline_renames_resolved_locals_and_preserves_early_return)
{
    REQUIRE(
        project("behavior Edit { count:double=0; function Add(input:double)->(amount:double) { temp:=input+1.0; if(input>0.0) { return temp; } return 0.0; } on Tick { temp:=9.0; count=Add(2.0); } }")
    );
    const source_presentation_origin *call = find(SOURCE_PRESENTATION_EXPRESSION, "Add(2.0)");
    REQUIRE(call);
    authoring_edit_request r = request(AUTHORING_EDIT_INLINE_CALL, call);
    REQUIRE(edit(r));
    REQUIRE(inspect_result());
    REQUIRE(strstr(result.text, "goto inline_1_exit;"));
    REQUIRE(strstr(result.text, "inline_1_exit:"));
    REQUIRE(strstr(result.text, "temp:=9.0;"));
    REQUIRE(!strstr(result.text, "count=Add(2.0)"));
    REQUIRE(strstr(analysis->text, "count=Add(2.0)"));
    cleanup();
    REQUIRE(
        project("behavior Edit { count:double=0; function Add(input:double)->(amount:double) { return input; } on Tick { count=1.0+Add(2.0); } }")
    );
    call = find(SOURCE_PRESENTATION_EXPRESSION, "Add(2.0)");
    REQUIRE(call);
    r = request(AUTHORING_EDIT_INLINE_CALL, call);
    REQUIRE(!edit(r) && error[0] && !result.text);
    cleanup();
    REQUIRE(
        project("behavior Edit { count:double=1; function Read(input:double)->(amount:double) { return count; } on Tick { count:=9.0; current:=Read(0.0); } }")
    );
    call = find(SOURCE_PRESENTATION_EXPRESSION, "Read(0.0)");
    REQUIRE(call);
    r = request(AUTHORING_EDIT_INLINE_CALL, call);
    REQUIRE(!edit(r) && strstr(error, "shadows") && !result.text);
    return true;
}

TEST_CASE(source_signatures_and_bindings_preserve_resolved_body_and_scope)
{
    REQUIRE(
        project("behavior Edit { a:double=0.5; count:double=0; function Add(a:double)->(amount:double) { return a; } on Tick { count=Add(2); } }")
    );
    const source_presentation_origin *fn = find(SOURCE_PRESENTATION_FUNCTION, "function Add");
    const source_presentation_origin *target = find(SOURCE_PRESENTATION_EXPRESSION, "2);");
    const source_presentation_origin *state = find(SOURCE_PRESENTATION_SYMBOL, "a:double=0.5");
    REQUIRE(fn && target && state);
    authoring_edit_request r = request(AUTHORING_EDIT_SIGNATURE, fn);
    r.signature = "(a:float)->(amount:double)";
    REQUIRE(edit(r));
    REQUIRE(
        strstr(result.text, "Add(a:float)->(amount:double)") &&
        strstr(result.text, "{ return a; }")
    );
    authoring_edit_result_free(&result);
    r.signature = "(b:double)->(amount:double)";
    REQUIRE(!edit(r) && error[0] && !result.text);
    r.signature = "()->() {} function Escaped()->()";
    REQUIRE(!edit(r) && error[0] && !result.text);
    r = request(AUTHORING_EDIT_BIND_INPUT, target);
    r.binding = AUTHORING_BIND_SYMBOL;
    r.symbol = analysis->symbols[state->record].id;
    REQUIRE(edit(r));
    REQUIRE(strstr(result.text, "count=Add(a)"));
    authoring_edit_result_free(&result);
    cleanup();
    REQUIRE(project("behavior Edit { a:double=1; count:double=0; on Tick { a:=9; count=2; } }"));
    state = find(SOURCE_PRESENTATION_SYMBOL, "a:double=1;");
    target = find(SOURCE_PRESENTATION_EXPRESSION, "2;");
    REQUIRE(state && target);
    r = request(AUTHORING_EDIT_BIND_INPUT, target);
    r.binding = AUTHORING_BIND_SYMBOL;
    r.symbol = analysis->symbols[state->record].id;
    REQUIRE(!edit(r) && error[0] && !result.text);
    return true;
}

TEST_CASE(source_layout_rebase_survives_new_document_identity_and_rejects_nonrename_edits)
{
    REQUIRE(project("behavior Edit { count:double=0; on Tick { count=2; } }"));
    const source_presentation_origin *o = find(SOURCE_PRESENTATION_EXPRESSION, "2;");
    REQUIRE(o);
    graph *g = behavior_asset_find_graph(authoring_source_presentation_view(view), o->graph);
    node *n = graph_node_by_id(g, o->node);
    n->x = 12;
    n->y = 34;
    n->placed = true;
    REQUIRE(graph_set_pin_watch(g, n, 1, 0, true) == 0);
    REQUIRE(graph_add_comment_box(g, "Renamed layout", 5, 6, 80, 40));
    bool routed = false;
    for (int i = 0; i < acount(g->edges); i++) {
        edge *e = &g->edges[i];
        if (e->from_id == n->id && e->kind == GRAPH_EDGE_DATA) {
            e->reroute_count = 1;
            e->reroutes[0].x = 19;
            e->reroutes[0].y = 23;
            routed = true;
        }
    }
    REQUIRE(routed);
    char *metadata = authoring_source_presentation_metadata(view);
    REQUIRE(metadata);
    text_edit rename = { 9, 13, "Renamed" };
    result.text = text_edits_apply(analysis->text, &rename, 1);
    REQUIRE(result.text);
    candidate_syntax = script_parse(result.text);
    candidate_analysis = script_analyze_document(candidate_syntax, &catalog, "tests/renamed");
    REQUIRE(candidate_analysis->valid);
    candidate_view = authoring_source_presentation_new(candidate_analysis, &catalog, NULL);
    REQUIRE(candidate_view);
    result.metadata = authoring_source_presentation_rebase(view, candidate_view, metadata, &rename, 1);
    REQUIRE(result.metadata);
    /* A new projection has no retained origin IDs or graph allocation from the
     * original generation; the persisted sidecar is the only layout source. */
    authoring_source_presentation_free(candidate_view);
    candidate_view = authoring_source_presentation_new(candidate_analysis, &catalog, NULL);
    REQUIRE(authoring_source_presentation_restore(candidate_view, result.metadata));
    bool placed = false, watched = false, box = false;
    routed = false;
    for (int i = 0; i < authoring_source_presentation_origin_count(candidate_view);
         i++) {
        const source_presentation_origin
            *origin = authoring_source_presentation_origin_at(candidate_view, i);
        if (origin->kind != SOURCE_PRESENTATION_EXPRESSION || origin->pin ||
            origin->range.end - origin->range.start != 1 ||
            result.text[origin->range.start] != '2') {
            continue;
        }
        g = behavior_asset_find_graph(
            authoring_source_presentation_view(candidate_view),
            origin->graph
        );
        n = graph_node_by_id(g, origin->node);
        placed = n->placed && n->x == 12 && n->y == 34;
        watched = (n->output_pins[0].flags & PIN_FLAG_WATCH) != 0;
        box = acount(g->comment_boxes) == 1 &&
              !strcmp(g->comment_boxes[0].title, "Renamed layout");
        for (int j = 0; j < acount(g->edges); j++) {
            const edge *e = &g->edges[j];
            routed |= e->reroute_count == 1 && e->reroutes[0].x == 19 &&
                      e->reroutes[0].y == 23;
        }
    }
    REQUIRE(placed && watched && box && routed);
    yyjson_doc *parsed = yyjson_read(result.metadata, strlen(result.metadata), 0);
    REQUIRE(parsed);
    yyjson_mut_doc *malformed = yyjson_doc_mut_copy(parsed, NULL);
    yyjson_doc_free(parsed);
    yyjson_mut_val *anchors = yyjson_mut_obj_get(yyjson_mut_doc_get_root(malformed), "source_origins");
    yyjson_mut_obj_put(
        yyjson_mut_arr_get(anchors, 0),
        yyjson_mut_str(malformed, "anchor"),
        yyjson_mut_uint(malformed, 0)
    );
    char *invalid = yyjson_mut_write(malformed, 0, NULL);
    yyjson_mut_doc_free(malformed);
    REQUIRE(invalid);
    n->x = 99;
    bool accepted = authoring_source_presentation_restore(candidate_view, invalid);
    free(invalid);
    REQUIRE(!accepted && n->x == 99);
    REQUIRE(
        authoring_source_presentation_restore(candidate_view, result.metadata) &&
        n->x == 12
    );
    text_edit wrong = { 9, 13, "Different" };
    char *rejected = authoring_source_presentation_rebase(view, candidate_view, metadata, &wrong, 1);
    REQUIRE(!rejected);
    authoring_source_presentation_free(candidate_view);
    script_analysis_free(candidate_analysis);
    script_syntax_free(candidate_syntax);
    free(result.text);
    int literal = (int)(strstr(analysis->text, "2;") - analysis->text);
    text_edit changed[] = { rename, { literal, literal + 1, "3" } };
    result.text = text_edits_apply(analysis->text, changed, 2);
    candidate_syntax = script_parse(result.text);
    candidate_analysis = script_analyze_document(candidate_syntax, &catalog, "tests/renamed");
    candidate_view = authoring_source_presentation_new(candidate_analysis, &catalog, NULL);
    REQUIRE(candidate_analysis->valid && candidate_view);
    rejected = authoring_source_presentation_rebase(view, candidate_view, metadata, changed, 2);
    free(metadata);
    REQUIRE(!rejected);
    return true;
}

void test_source_edit()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_expression_edits_validate_value_scope_and_generation);
    RUN_TEST_CASE(source_connections_reference_retained_results_and_reject_repeated_effects);
    RUN_TEST_CASE(source_statement_moves_keep_comments_and_validate_declaration_order);
    RUN_TEST_CASE(source_selection_transactions_preserve_names_and_comments);
    RUN_TEST_CASE(source_optional_arguments_and_typed_values_are_checked_atomically);
    RUN_TEST_CASE(source_extraction_preserves_ref_inputs_and_escaping_locals);
    RUN_TEST_CASE(source_inline_renames_resolved_locals_and_preserves_early_return);
    RUN_TEST_CASE(source_signatures_and_bindings_preserve_resolved_body_and_scope);
    RUN_TEST_CASE(source_layout_rebase_survives_new_document_identity_and_rejects_nonrename_edits);
    pu_clear_setup();
}
