#include "assets/code_source_intelligence.h"

#include "assets/code_source.h"
#include "assets/code_source_diagnostics.h"
#include "graph/document/library.h"
#include "graph/document/serialize.h"
#include "graph/nodes/behavior_expression.h"
#include "graph/nodes/component_nodes.h"
#include "graph/runtime/instance.h"
#include "support/array.h"
#include "support/text_file.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int at(const char *text, const char *part)
{
    const char *found = strstr(text, part);
    assert(found);
    return (int)(found - text);
}

static void imported_native_identity()
{
    code_source *first = code_source_parse("library First { pure function Same() -> (Result: int) { result Result: int = 1; } }");
    code_source *second = code_source_parse("library Second { pure function Same() -> (Result: int) { result Result: int = 2; } }");
    assert(first->asset && second->asset);
    graph *a = behavior_asset_find_graph_by_name(first->asset, "Same");
    graph *b = behavior_asset_find_graph_by_name(second->asset, "Same");
    assert(a && b);
    a->graph_id = b->graph_id = 77;
    const behavior_asset *libraries[] = { first->asset, second->asset };
    code_source_context context = { .libraries = libraries, .library_count = 2 };
    const char *text = "behavior Consumer { value one = First.Same(); value "
                       "two = Second.Same(); }";
    code_source *source = code_source_parse_context(text, NULL, &context);
    assert(source->asset);
    const code_semantic_symbol
        *one = code_intelligence_symbol_at(source, at(text, "First.Same") + 6);
    const code_semantic_symbol
        *two = code_intelligence_symbol_at(source, at(text, "Second.Same") + 7);
    assert(one && two && one->id != two->id);
    assert(one->asset_name && !strcmp(one->asset_name, "First"));
    assert(two->asset_name && !strcmp(two->asset_name, "Second"));
    assert(one->graph_id == 77 && two->graph_id == 77);
    char *hover = code_intelligence_hover(source, at(text, "First.Same") + 6);
    assert(hover && strstr(hover, "First.Same(") && !strstr(hover, "Call Pure Function("));
    free(hover);
    uint64_t first_id = one->id, second_id = two->id;
    code_semantic_symbol imported_port = *one;
    imported_port.id = 123457;
    imported_port.name = "Result";
    imported_port.kind = CODE_SYMBOL_FIELD;
    imported_port.node_id = 78;
    imported_port.pin_id = 79;
    code_intelligence_symbol(source, &imported_port);
    /* The same integer is legal in three independently owned assets. */
    code_semantic_symbol local = { .id = 123456,
                                   .name = "local",
                                   .kind = CODE_SYMBOL_LOCAL,
                                   .graph_id = 77,
                                   .node_id = 78,
                                   .pin_id = 79 };
    code_intelligence_symbol(source, &local);
    uint64_t from[] = { 77, 78, 79 }, to[] = { 177, 178, 179 };
    code_intelligence_remap_native(source, from, to, 3);
    one = code_intelligence_symbol_by_id(source, first_id);
    two = code_intelligence_symbol_by_id(source, second_id);
    assert(one->graph_id == 77 && two->graph_id == 77);
    const code_semantic_symbol
        *foreign = code_intelligence_symbol_by_id(source, imported_port.id);
    assert(foreign->graph_id == 77 && foreign->node_id == 78 && foreign->pin_id == 79);
    const code_semantic_symbol
        *owned = code_intelligence_symbol_by_id(source, local.id);
    assert(owned->graph_id == 177 && owned->node_id == 178 && owned->pin_id == 179);
    code_semantic_call *calls = code_intelligence_calls_for(source, first_id, true);
    assert(acount(calls) == 1 && calls[0].target_id == first_id);
    afree(calls);
    calls = code_intelligence_calls_for(source, second_id, true);
    assert(acount(calls) == 1 && calls[0].target_id == second_id);
    afree(calls);
    code_source_free(source);
    code_source_free(second);
    code_source_free(first);
}

static void imported_signature_refresh()
{
    code_source *old_library = code_source_parse("library Shared { pure function Seven() -> (Result: int) { result Result: int = 7; } }");
    code_source *new_library = code_source_parse_previous(
        "library Shared { pure function Seven(Extra: int = 4) -> (Result: int) "
        "{ result Result: int = 7; } }",
        old_library
    );
    assert(old_library->asset && new_library->asset);
    const behavior_asset *libraries[] = { old_library->asset };
    code_source_context context = { .libraries = libraries, .library_count = 1 };
    code_source *source = code_source_parse_context(
        "behavior Consumer { state count: int = 1; value chosen = "
        "Shared.Seven(); }",
        NULL,
        &context
    );
    assert(source->asset);
    behavior_asset *working = behavior_asset_clone(source->asset);
    behavior_variable *count = behavior_asset_find_variable_by_name(working, "count");
    pin_value changed = { .type = PIN_I32, .i = 9 };
    assert(!behavior_asset_set_variable_default(working, count->id, &changed));
    node *call = NULL;
    for (int i = 0; i < acount(working->graph->nodes); i++) {
        if (working->graph->nodes[i]->reference_asset) {
            call = working->graph->nodes[i];
            break;
        }
    }
    assert(call);
    uint64_t call_id = call->id;
    call->x = 137;
    call->y = 251;
    call->placed = true;
    behavior_asset *before = behavior_asset_clone(working);
    libraries[0] = new_library->asset;
    char error[256];
    behavior_asset *normalized = code_source_refresh_imports(source, working, error, sizeof error);
    if (!normalized) {
        fprintf(stderr, "import refresh: %s\n", error);
    }
    assert(normalized && behavior_asset_equal(working, before));
    call = graph_node_by_id(normalized->graph, call_id);
    assert(call && call->x == 137 && call->y == 251 && call->placed);
    assert(call->input_count == 1 && !strcmp(call->input_pins[0].name, "Extra"));
    assert(call->input_pins[0].default_value.i == 4);
    assert(
        behavior_asset_find_variable_by_name(normalized, "count")->default_value.i == 9
    );
    /* Reconciliation must still prove exact native equality, including the
     * signature's new pin and the independent unsaved graph mutation. */
    char *metadata = NULL;
    char *text = code_source_reconcile_with_metadata(source, normalized, &metadata, error, sizeof error);
    if (!text) {
        fprintf(stderr, "import reconcile: %s\n", error);
    }
    assert(text);
    code_source *candidate = code_source_parse_context(text, source, &context);
    assert(code_source_metadata_restore(candidate, metadata));
    free(metadata);
    assert(candidate->asset && behavior_asset_equal(candidate->asset, normalized));
    behavior_asset *compiled = code_source_compile_behavior(candidate);
    assert(compiled);
    behavior_asset_free(compiled);
    code_source_free(candidate);
    free(text);
    code_source *invalid_library = code_source_parse("library Shared { pure function Other() -> (Result: int) { result Result: int = 7; } }");
    assert(invalid_library->asset);
    libraries[0] = invalid_library->asset;
    assert(!code_source_refresh_imports(source, working, error, sizeof error));
    assert(strstr(error, "Shared.Seven") && behavior_asset_equal(working, before));
    assert(
        source->asset && behavior_asset_find_variable_by_name(source->asset, "count")
                                 ->default_value.i == 1
    );
    code_source_free(invalid_library);
    behavior_asset_free(normalized);
    behavior_asset_free(before);
    behavior_asset_free(working);
    code_source_free(source);
    code_source_free(new_library);
    code_source_free(old_library);
}

static void implicit_producer_context_refresh()
{
    const char *path = "projects/arena2d/content/behaviors/ProjectileHit.code";
    char *text = text_file_read(path);
    assert(text);
    const char *library_text = "library Shared { pure function Seven() -> "
                               "(Result: int) { result Result: int = 7; } }";
    char *event_text = text_file_read("projects/arena2d/content/events/Damage.kdl");
    assert(event_text);
    code_source *library = code_source_parse(library_text);
    behavior_asset *event = behavior_asset_load(event_text);
    assert(library->asset && event);
    const behavior_asset *libraries[] = { library->asset }, *events[] = { event };
    code_source_context context = { .libraries = libraries,
                                    .library_count = 1,
                                    .events = events,
                                    .event_count = 1 };
    code_source *source = code_source_parse_context(text, NULL, &context);
    assert(source->asset && code_source_metadata_load(source, path));
    behavior_asset *baseline = behavior_asset_clone(source->asset);
    /* Equivalent catalog generations must preserve implicit component and Self
     * producers, even when several nodes share their native descriptor. */
    for (int i = 0; i < 3; i++) {
        code_source *next_library = code_source_parse(library_text);
        behavior_asset *next_event = behavior_asset_load(event_text);
        assert(next_library->asset && next_event);
        libraries[0] = next_library->asset;
        events[0] = next_event;
        code_source *next = code_source_parse_context(text, source, &context);
        assert(next->asset && behavior_asset_equal(next->asset, baseline));
        char *metadata = code_source_metadata_export_source(next);
        code_source *reopened = code_source_parse_context(text, NULL, &context);
        assert(code_source_metadata_restore(reopened, metadata));
        assert(behavior_asset_equal(reopened->asset, baseline));
        code_source_free(reopened);
        free(metadata);
        code_source_free(source);
        code_source_free(library);
        behavior_asset_free(event);
        source = next;
        library = next_library;
        event = next_event;
    }
    behavior_asset_free(baseline);
    code_source_free(source);
    code_source_free(library);
    behavior_asset_free(event);
    free(event_text);
    free(text);
}

static void sequenced_statement_identity()
{
    const char *text = "behavior Repeated { on Tick() { destroy_entity(Entity: "
                       "self); destroy_entity(Entity: self); } }";
    const char *edited = "// authored comment\nbehavior Repeated { on Tick() { "
                         "destroy_entity(Entity: self); destroy_entity(Entity: "
                         "self); } }";
    code_source *original = code_source_parse(text);
    assert(original->asset);
    node *first = NULL;
    for (int i = 0; i < acount(original->asset->graph->nodes); i++) {
        node *n = original->asset->graph->nodes[i];
        if (!strcmp(n->desc->name, "Destroy Entity")) {
            first = n;
            break;
        }
    }
    assert(first);
    first->x = 137;
    first->placed = true;
    first->breakpoint = true;
    code_source *next = code_source_parse_previous(edited, original);
    assert(next->asset && behavior_asset_equal(next->asset, original->asset));
    char *metadata = code_source_metadata_export_source(next);
    code_source *independent = code_source_parse(edited);
    assert(code_source_metadata_restore(independent, metadata));
    assert(behavior_asset_equal(independent->asset, next->asset));
    code_source_free(independent);
    free(metadata);
    code_source_free(next);
    code_source_free(original);
}

static void inherited_import_defaults()
{
    code_source *old_library = code_source_parse("library Shared { pure function Seven(Extra: int = 4) -> (Result: int) { result Result: int = 7; } }");
    code_source *new_library = code_source_parse_previous(
        "library Shared { pure function Seven(Extra: int = 6) -> (Result: int) "
        "{ result Result: int = 7; } }",
        old_library
    );
    assert(old_library->asset && new_library->asset);
    const behavior_asset *libraries[] = { old_library->asset };
    code_source_context context = { .libraries = libraries, .library_count = 1 };
    const char *texts[] = { "behavior Consumer { state count: int = 1; value "
                            "chosen = Shared.Seven(); }",
                            "behavior Consumer { state count: int = 1; value "
                            "chosen = Shared.Seven(Extra: 4); }" };
    for (int explicit = 0; explicit < 2; explicit++) {
        libraries[0] = old_library->asset;
        code_source *previous = code_source_parse_context(texts[explicit], NULL, &context);
        assert(previous->asset);
        behavior_asset *working = behavior_asset_clone(previous->asset);
        pin_value changed = { .type = PIN_I32, .i = 9 };
        behavior_variable *count = behavior_asset_find_variable_by_name(working, "count");
        assert(!behavior_asset_set_variable_default(working, count->id, &changed));
        libraries[0] = new_library->asset;
        code_source *latest = code_source_parse_context(texts[explicit], previous, &context);
        assert(latest->asset);
        char error[256];
        behavior_asset *normalized = code_source_refresh_imports(previous, working, error, sizeof error);
        assert(normalized);
        node *call = NULL;
        for (int i = 0; i < acount(normalized->graph->nodes); i++) {
            if (normalized->graph->nodes[i]->reference_asset) {
                call = normalized->graph->nodes[i];
                break;
            }
        }
        assert(call && call->input_pins[0].default_value.i == (explicit ? 4 : 6));
        char *text = code_source_reconcile(latest, normalized, error, sizeof error);
        assert(text);
        code_source *candidate = code_source_parse_context(text, latest, &context);
        assert(candidate->asset && behavior_asset_equal(candidate->asset, normalized));
        code_source_free(candidate);
        free(text);
        uint64_t call_id = call->id;
        behavior_asset_free(normalized);
        /* A pending graph override belongs to the draft even for an argument
         * the source omitted; a catalog default must never replace it. */
        call = graph_node_by_id(working->graph, call_id);
        changed.i = 13;
        assert(!graph_set_pin_default(working->graph, call, false, 0, &changed));
        normalized = code_source_refresh_imports(previous, working, error, sizeof error);
        assert(normalized);
        call = graph_node_by_id(normalized->graph, call->id);
        assert(call->input_pins[0].default_value.i == 13);
        behavior_asset_free(normalized);
        behavior_asset_free(working);
        code_source_free(latest);
        code_source_free(previous);
    }
    code_source_free(new_library);
    code_source_free(old_library);
}

static void imported_diagnostic_provenance()
{
    code_source *library = code_source_parse("library Shared { pure function Seven() -> (Result: int) { result Result: int = 7; } }");
    assert(library->asset);
    const behavior_asset *libraries[] = { library->asset };
    code_source_context context = { .libraries = libraries, .library_count = 1 };
    const char *text = "behavior Consumer { value seven = Shared.Seven(); }";
    code_source *source = code_source_parse_context(text, NULL, &context);
    assert(source->asset);
    char error[256];
    behavior_asset *linked = behavior_asset_link_libraries(source->asset, libraries, 1, error, sizeof error);
    assert(linked);
    graph *authored = behavior_asset_find_graph_by_name(library->asset, "Seven");
    const graph *imported = NULL;
    for (int i = 0; i < acount(linked->graphs); i++) {
        if (linked->graphs[i]->source_asset &&
            !strcmp(linked->graphs[i]->source_asset, "Shared")) {
            imported = linked->graphs[i];
            break;
        }
    }
    assert(imported && acount(imported->nodes));
    const node *n = imported->nodes[0];
    graph_node_source origin = graph_node_authored(imported, n->id);
    compile_report report = { 0 };
    compile_diagnostic item = { .severity = CMPL_ERROR,
                                .rule = CMPL_RULE_CALL_SIGNATURE,
                                .graph_id = imported->graph_id,
                                .node_id = n->id };
    strcpy(item.message, "Invalid library input");
    apush(report.items, item);
    int previous = acount(source->diagnostics);
    code_source_compile_diagnostics(source, linked, &report);
    assert(acount(source->diagnostics) == previous + 1);
    const code_source_diagnostic *d = &source->diagnostics[previous];
    assert(d->asset_name && !strcmp(d->asset_name, "Shared"));
    assert(d->graph_name && !strcmp(d->graph_name, "Seven"));
    assert(d->graph_id == authored->graph_id && d->node_id == origin.node_id);
    assert(d->start == at(text, "Seven") && d->end == d->start + 5);
    assert(strstr(d->message, "Shared.Seven") && strstr(d->message, "Invalid library input"));
    code_source_compile_diagnostics(source, linked, &report);
    assert(acount(source->diagnostics) == previous + 1);
    report.items[0].severity = CMPL_WARNING;
    code_source_compile_diagnostics(source, linked, &report);
    code_source_compile_diagnostics(source, linked, &report);
    assert(acount(source->diagnostics) == previous + 2);
    assert(source->diagnostics[previous + 1].severity == CMPL_WARNING);
    afree(report.items);
    behavior_asset_free(linked);
    code_source_free(source);
    code_source_free(library);
}

static void local_references()
{
    const char
        *text = "behavior LocalReferences { state count: int = 0; "
                "value number = integer(Value: 2); local named = number.Value; "
                "on Tick() { count = named; count = named + 1; } }";
    code_source *source = code_source_parse(text);
    assert(source->asset);
    int declaration = at(text, "local named") + 6;
    int use = at(text, "count = named") + 8;
    asset_source_span definition;
    assert(code_intelligence_definition(source, use, &definition));
    assert(definition.start == declaration && definition.end == declaration + 5);
    asset_source_span *references = code_intelligence_references(source, declaration);
    bool found = false;
    for (int i = 0; i < acount(references); i++) {
        found |= references[i].start == use && references[i].end == use + 5;
    }
    assert(found);
    afree(references);
    char error[256];
    char *renamed = code_intelligence_rename(source, declaration, "renamed", error, sizeof error);
    assert(renamed && strstr(renamed, "local renamed") && strstr(renamed, "count = renamed"));
    assert(strstr(renamed, "count = renamed + 1"));
    code_source *candidate = code_source_parse(renamed);
    assert(candidate->asset);
    code_source_free(candidate);
    free(renamed);
    code_source_free(source);
}

static void label_navigation()
{
    const char *text = "behavior Labels { state count: int = 0; "
                       "on Tick() { shared_route: count += 1; } "
                       "on Begin() { goto shared_route; } }";
    code_source *source = code_source_parse(text);
    assert(source->asset);
    int declaration = at(text, "shared_route:");
    int use = at(text, "goto shared_route") + 5;
    const code_semantic_symbol *symbol = code_intelligence_symbol_at(source, use);
    assert(symbol && symbol->kind == CODE_SYMBOL_LABEL);
    asset_source_span definition;
    assert(code_intelligence_definition(source, use, &definition));
    assert(definition.start == declaration && definition.end == declaration + 12);
    char error[256];
    char *renamed = code_intelligence_rename(source, use, "route", error, sizeof error);
    assert(renamed && strstr(renamed, "route:") && strstr(renamed, "goto route;"));
    code_source *candidate = code_source_parse(renamed);
    assert(candidate->asset && behavior_asset_equal(source->asset, candidate->asset));
    code_source_free(candidate);
    free(renamed);
    code_source_free(source);
}

static void callable_completion_collisions()
{
    code_source *library = code_source_parse("library Shared { pure function Multiply() -> (Result: int) { result Result: int = 7; } }");
    assert(library->asset);
    const behavior_asset *catalog[] = { library->asset };
    code_source_context context = { .libraries = catalog, .library_count = 1 };
    const char
        *text = "behavior Names { pure function Multiply() -> (Result: int) "
                "{ result Result: int = 9; } value chosen = Mul";
    code_source *source = code_source_parse_context(text, NULL, &context);
    asset_source_completions items;
    code_intelligence_complete(source, (int)strlen(text), &items);
    bool authored = false, native = false, external = false;
    for (int i = 0; i < acount(items.items); i++) {
        const asset_source_completion *item = &items.items[i];
        authored |= !strcmp(item->label, "Multiply") &&
                    !strcmp(item->insert, "Multiply");
        native |= !strcmp(item->insert, "engine[\"Multiply\"]");
        if (!strcmp(item->insert, "engine[\"Multiply\"]")) {
            assert(!strncmp(item->detail, "engine[\"Multiply\"](", 19));
        }
        external |= !strcmp(item->insert, "Shared.Multiply");
        assert(strcmp(item->insert, "multiply"));
    }
    assert(authored && native && external);
    asset_source_completions_free(&items);
    code_source_free(source);
    text = "behavior Names { value chosen = Shared.Mul";
    source = code_source_parse_context(text, NULL, &context);
    code_intelligence_complete(source, (int)strlen(text), &items);
    external = false;
    for (int i = 0; i < acount(items.items); i++) {
        const asset_source_completion *item = &items.items[i];
        external |= !strcmp(item->label, "Multiply") &&
                    !strcmp(item->insert, "Multiply");
        assert(!strstr(item->insert, "engine["));
    }
    assert(external);
    asset_source_completions_free(&items);
    code_source_free(source);
    code_source_free(library);
}

static void prototype_completion_drafts()
{
    const code_reference references[] = { { CODE_REFERENCE_PROTOTYPE, "MovementEmitter" },
                                          { CODE_REFERENCE_PROTOTYPE, "BoostEmitter" } };
    code_source_context context = { .references = references, .reference_count = 2 };
    const char *drafts[] = {
        "behavior Effects { on Begin { movement := await "
        "spawn(MovementEmitter); } }",
        "behavior Effects { on Begin { movement := await "
        "spawn(MovmentEmitter); } }",
        "behavior Effects { on Begin { movement := await spawn(Move",
        "behavior Effects { on Begin { movement := await spawn(",
        "behavior Effects { on Begin { await spawn(Prototype: "
        "MovementEmitter); } }"
    };
    for (int i = 0; i < (int)(sizeof drafts / sizeof *drafts); i++) {
        const char *text = drafts[i];
        const char *token = strstr(text, "MovementEmitter");
        if (!token) {
            token = strstr(text, "MovmentEmitter");
        }
        if (!token) {
            token = strstr(text, "Move");
        }
        int start = token ? (int)(token - text) : (int)strlen(text);
        int offset = token ? start + 3 : start;
        code_source *source = code_source_parse_context(text, NULL, &context);
        asset_source_completions items;
        code_intelligence_complete(source, offset, &items);
        bool emitter = false;
        for (int j = 0; j < acount(items.items); j++) {
            const asset_source_completion *item = &items.items[j];
            emitter |= !strcmp(item->label, "MovementEmitter") &&
                       !strcmp(item->insert, "MovementEmitter");
            assert(strcmp(item->label, "Prototype"));
        }
        assert(emitter && items.start == start && items.end >= offset);
        asset_source_completions_free(&items);
        code_source_free(source);
    }
}

static void completion_spelling_and_relevance()
{
    const char *draft = "behavior Suggestions { on Begin { set_component";
    code_source *parsed = code_source_parse(draft);
    asset_source_completions items;
    code_intelligence_complete(parsed, (int)strlen(draft), &items);
    assert(acount(items.items) && !strcmp(items.items[0].label, "set_component"));
    assert(!strncmp(items.items[0].detail, "set_component(", 14));
    assert(!strstr(items.items[0].detail, "exec"));
    asset_source_completions_free(&items);
    code_source_free(parsed);

    char text[] = "vel";
    code_source source = { .text = text };
    code_semantic_type number = { .type = PIN_F64 };
    code_semantic_type boolean = { .type = PIN_BOOL };
    code_semantic_symbol symbols[] = {
        { .id = 1,
          .name = "velocity_global",
          .kind = CODE_SYMBOL_STATE,
          .scope_end = 3,
          .type = number },
        { .id = 2, .name = "velocity", .kind = CODE_SYMBOL_LOCAL, .scope_end = 3, .type = number },
        { .id = 3,
          .name = "very_eventful_loop",
          .kind = CODE_SYMBOL_LOCAL,
          .scope_end = 3,
          .type = number },
        { .id = 4, .name = "vel", .kind = CODE_SYMBOL_STATE, .scope_end = 3, .type = number },
        { .id = 5,
          .name = "vel_hidden",
          .kind = CODE_SYMBOL_LOCAL,
          .scope_start = 4,
          .scope_end = 5,
          .type = number },
        { .id = 6, .name = "vel_boolean", .kind = CODE_SYMBOL_LOCAL, .scope_end = 3, .type = boolean }
    };
    for (int i = 0; i < (int)(sizeof symbols / sizeof *symbols); i++) {
        code_intelligence_symbol(&source, &symbols[i]);
    }
    code_completion_context context = { .end = 3,
                                        .kinds = CODE_SYMBOL_MASK(CODE_SYMBOL_LOCAL) |
                                                 CODE_SYMBOL_MASK(CODE_SYMBOL_STATE),
                                        .typed = true,
                                        .expected = number };
    code_intelligence_context(&source, &context);
    code_intelligence_complete(&source, 3, &items);
    assert(acount(items.items) == 4);
    assert(!strcmp(items.items[0].label, "vel"));
    assert(!strcmp(items.items[1].label, "velocity"));
    assert(!strcmp(items.items[2].label, "velocity_global"));
    assert(!strcmp(items.items[3].label, "very_eventful_loop"));
    assert(!strcmp(items.items[1].detail, "double"));
    asset_source_completions_free(&items);
    code_intelligence_clear(&source);
}

static bool has_completion(const asset_source_completions *items, const char *label)
{
    for (int i = 0; i < acount(items->items); i++) {
        if (!strcmp(items->items[i].label, label)) {
            return true;
        }
    }
    return false;
}
static void incomplete_body_intelligence()
{
    const char *malformed[] = { "behavior Draft { on",
                                "behavior Draft { on Tick as",
                                "behavior Draft { on Tick(GameMovement",
                                "behavior Draft { on Tick(GameMovement move",
                                "behavior Draft { local",
                                "behavior Draft { local named =",
                                "behavior Draft { on Tick() { print_string(" };
    for (int i = 0; i < (int)(sizeof malformed / sizeof *malformed); i++) {
        code_source *source = code_source_parse(malformed[i]);
        assert(!source->asset && acount(source->diagnostics));
        asset_source_completions items;
        code_intelligence_complete(source, (int)strlen(malformed[i]), &items);
        asset_source_completions_free(&items);
        code_source_free(source);
    }
    const char *drafts[] = {
        "behavior Draft { state score: int = 0; on Tick() { score = sco",
        "behavior Draft { state score: int = 0; on Tick() { score = ",
        "behavior Draft { state score: int = 0; on Tick() { score = score + sco"
    };
    for (int i = 0; i < (int)(sizeof drafts / sizeof *drafts); i++) {
        code_source *source = code_source_parse(drafts[i]);
        assert(!source->asset && acount(source->diagnostics));
        asset_source_completions items;
        code_intelligence_complete(source, (int)strlen(drafts[i]), &items);
        assert(has_completion(&items, "score"));
        asset_source_completions_free(&items);
        code_source_free(source);
    }
    const char *text = "behavior Draft { state score: int = 0; on Tick() { "
                       "score = score + 1;";
    code_source *source = code_source_parse(text);
    assert(!source->asset && acount(source->diagnostics));
    int use = at(text, "score = score") + 8;
    asset_source_span definition;
    assert(code_intelligence_definition(source, use, &definition));
    assert(definition.start == at(text, "score:"));
    char *hover = code_intelligence_hover(source, use);
    assert(hover && strstr(hover, "score: int"));
    free(hover);
    asset_source_span *references = code_intelligence_references(source, use);
    assert(acount(references) == 3);
    afree(references);
    code_source_free(source);
    text = "behavior Draft { on Tick() { print_str";
    source = code_source_parse(text);
    asset_source_completions items;
    code_intelligence_complete(source, (int)strlen(text), &items);
    assert(has_completion(&items, "print_string"));
    asset_source_completions_free(&items);
    code_source_free(source);
    text = "behavior Draft { on Tick() { print_string(InString: \"hello\");";
    source = code_source_parse(text);
    hover = code_intelligence_hover(source, at(text, "print_string") + 1);
    assert(hover && strstr(hover, "Print String"));
    free(hover);
    code_source_free(source);
}

static void qualified_statement_intelligence()
{
    code_source *library = code_source_parse("library Weapons { function \"Fire Projectile\"() -> () { after entry { return(); } } }");
    assert(library->asset);
    const behavior_asset *libraries[] = { library->asset };
    code_source_context context = { .libraries = libraries, .library_count = 1 };
    const char *texts[] = {
        "behavior Consumer { on Tick() { await Weapons.\"Fire Projectile\"(); "
        "} }",
        "behavior Consumer { on Tick() { await Weapons.\"Fire Projectile\"();"
    };
    for (int i = 0; i < 2; i++) {
        code_source *source = code_source_parse_context(texts[i], NULL, &context);
        assert((source->asset != NULL) == (i == 0));
        int member = at(texts[i], "Fire Projectile") + 1;
        const code_semantic_symbol *symbol = code_intelligence_symbol_at(source, member);
        assert(symbol && symbol->asset_name && !strcmp(symbol->asset_name, "Weapons"));
        assert(!strcmp(symbol->name, "Weapons.Fire Projectile"));
        assert(!strcmp(symbol->signature, "Weapons.Fire Projectile() -> ()"));
        assert(!code_intelligence_symbol_at(source, at(texts[i], "Weapons") + 1));
        char *hover = code_intelligence_hover(source, member);
        assert(hover && strstr(hover, "Weapons.Fire Projectile() -> ()"));
        assert(!strstr(hover, "Exec: exec") && !strstr(hover, "Then: exec"));
        free(hover);
        asset_source_span *references = code_intelligence_references(source, member);
        assert(acount(references) == 1);
        assert(references[0].start == at(texts[i], "\"Fire Projectile\""));
        assert(references[0].end == references[0].start + (int)strlen("\"Fire Projectile\""));
        afree(references);
        code_semantic_call *calls = code_intelligence_calls(source, member, true);
        assert(acount(calls) == 1 && calls[0].target_id == symbol->id);
        afree(calls);
        code_source_free(source);
    }
    const char *drafts[] = {
        "behavior Consumer { on Tick() { await Weapons.",
        "behavior Consumer { on Tick() { await Weapons. } }",
        "behavior Consumer { on Tick() { await Weapons.; } }",
        "behavior Consumer { on Tick() { await Weapons.\n/* @authoring "
        "{\"version\":2} */",
        "behavior Consumer { on Tick() { await Weapons. } }\n/* @authoring "
        "{\"version\":2} */"
    };
    for (int d = 0; d < 5; d++) {
        code_source *partial = code_source_parse_context(drafts[d], NULL, &context);
        int cursor = at(drafts[d], "Weapons.") + (int)strlen("Weapons.");
        asset_source_completions items;
        code_intelligence_complete(partial, cursor, &items);
        bool offered = false;
        for (int i = 0; i < acount(items.items); i++) {
            const asset_source_completion *item = &items.items[i];
            assert(strcmp(item->label, "Weapons.Fire Projectile"));
            if (!strcmp(item->label, "Fire Projectile")) {
                assert(!strcmp(item->detail, "Fire Projectile() -> ()"));
                assert(!strcmp(item->insert, "\"Fire Projectile\""));
                offered = true;
            }
        }
        assert(offered && items.start == cursor && items.end == cursor);
        asset_source_completions_free(&items);
        code_source_free(partial);
    }
    code_source_free(library);
}

static void source_capture_labels()
{
    code_source *source = code_source_parse("behavior Labels { state observed := 0; on Tick(GameMovement movement) { observed = movement.speed + event.dt; } }");
    assert(source->asset);
    behavior_asset *working = behavior_asset_clone(source->asset);
    bool member = false, event = false;
    for (int g = 0; g < acount(working->graphs); g++) {
        graph *graph = working->graphs[g];
        for (int n = 0; n < acount(graph->nodes); n++) {
            node *expression = graph->nodes[n];
            if (expression->desc != &NODE_BehaviorExpression) {
                continue;
            }
            for (int i = 0; i < expression->input_count; i++) {
                char compact[80];
                code_source_expression_input_name(source, working, expression, i, compact, sizeof compact);
                assert(compact[0] && !strstr(compact, "_code_input_"));
                if (expression->input_pins[i].kind == PIN_EXEC) {
                    assert(!strcmp(compact, expression->input_pins[i].name));
                }
                expression->input_pins[i].display_name = NULL;
                const char *label = code_source_expression_input_label_at(source, working, expression, i);
                member |= !strcmp(label, "movement") ||
                          !strcmp(label, "movement.speed");
                event |= !strcmp(label, "event.dt");
                assert(!expression->input_pins[i].display_name);
                assert(code_source_expression_input_label_at(NULL, working, expression, i));
            }
            char *readable = code_source_expression(source, working, expression);
            assert(readable && strstr(readable, "movement.speed") && strstr(readable, "event.dt"));
            assert(!strstr(readable, "_code_input_"));
            char error[256];
            char *resolved = code_source_resolve_expression(
                source,
                working,
                expression,
                readable,
                error,
                sizeof error
            );
            assert(
                resolved &&
                !strcmp(resolved, behavior_expression_source(working, expression))
            );
            free(resolved);
            free(readable);
        }
    }
    assert(member && event);
    behavior_asset_free(working);
    code_source_free(source);

    source = code_source_parse("behavior Labels { value total=(real(Value:1)+real(Value:2)); }");
    assert(source->asset);
    working = behavior_asset_clone(source->asset);
    for (int g = 0; g < acount(working->graphs); g++) {
        for (int n = 0; n < acount(working->graphs[g]->nodes); n++) {
            node *expression = working->graphs[g]->nodes[n];
            if (expression->desc != &NODE_BehaviorExpression) {
                continue;
            }
            char previous[80] = "";
            for (int input = 0; input < expression->input_count; input++) {
                if (expression->input_pins[input].kind != PIN_DATA) {
                    continue;
                }
                expression->input_pins[input].display_name = NULL;
                char label[80];
                code_source_expression_input_name(source, working, expression, input, label, sizeof label);
                assert(label[0] && !strstr(label, "_code_input_") && !strchr(label, '('));
                assert(strcmp(previous, label));
                snprintf(previous, sizeof previous, "%s", label);
                char native[80];
                code_source_expression_input_name(NULL, working, expression, input, native, sizeof native);
                assert(native[0] && !strstr(native, "_code_input_"));
                const char *full = code_source_expression_input_label_at(source, working, expression, input);
                assert(full && (strstr(full, "real(") || !strcmp(full, "event.dt")));
            }
            char *readable = code_source_expression(source, working, expression);
            assert(readable && !strstr(readable, "_code_input_") && strstr(readable, "real(Value:"));
            char error[256];
            char *resolved = code_source_resolve_expression(
                source,
                working,
                expression,
                readable,
                error,
                sizeof error
            );
            assert(
                resolved &&
                !strcmp(resolved, behavior_expression_source(working, expression))
            );
            free(readable);
            free(resolved);
        }
    }
    behavior_asset_free(working);
    code_source_free(source);
}

static void nested_control_execution()
{
    const char *text =
        "behavior Nested { state phase: float = 0; state limit: float = 2; "
        "state total: int = 0; state trace: int = 0; "
        "function Count(Index: int) -> () { after entry { total += 1; "
        "trace = trace * 10 + entry.Index; return(); } } "
        "on Tick() { if(cast<double>(&state.phase) < "
        "cast<double>(&state.limit)) { "
        "while(&state.phase < &state.limit) { for(item in 0..2) { "
        "if(item < 2) { await Count(Index: item); } else {} } phase += 1; } "
        "total += 10; } else { total += 100; } } }";
    code_source *source = code_source_parse(text);
    assert(source->asset);
    char *saved = code_source_persist(source);
    code_source *reopened = code_source_parse(saved);
    assert(reopened->asset && behavior_asset_equal(source->asset, reopened->asset));
    behavior_asset *compiled = code_source_compile_behavior(reopened);
    assert(compiled);
    int total = -1, trace = -1, phase = -1;
    for (int i = 0; i < acount(compiled->variables); i++) {
        if (!strcmp(compiled->variables[i].name, "total")) {
            total = i;
        }
        if (!strcmp(compiled->variables[i].name, "trace")) {
            trace = i;
        }
        if (!strcmp(compiled->variables[i].name, "phase")) {
            phase = i;
        }
    }
    assert(total >= 0 && trace >= 0 && phase >= 0);
    const behavior_backend backends[] = { BEHAVIOR_UNAVAILABLE,
                                          BEHAVIOR_BYTECODE,
                                          BEHAVIOR_ORACLE };
    for (int backend = 0; backend < 3; backend++) {
        behavior_instance *instance = behavior_instance_create(compiled, NULL, NULL, NULL);
        assert(instance);
        if (backend) {
            behavior_instance_set_backend(instance, backends[backend]);
        }
        flow_error error = { 0 };
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
        assert(!behavior_instance_last_failure(instance));
        assert(instance->variables[total].i == 14 && instance->variables[trace].i == 101);
        assert(instance->variables[phase].f == 2);
        behavior_instance_dispatch(instance, "Tick", (flow_inputs){ 0 }, &error);
        assert(!behavior_instance_last_failure(instance));
        assert(instance->variables[total].i == 114 && instance->variables[trace].i == 101);
        behavior_instance_destroy(instance);
    }
    behavior_asset_free(compiled);
    code_source_free(reopened);
    free(saved);
    code_source_free(source);
    /* Unsupported inline syntax must produce an invalid draft, never silently
     * truncate the following execution body into a successful empty graph. */
    source = code_source_parse("behavior Invalid { on Tick() { while(cast<double>(1) cast<double>(2)) { destroy(self); } } }");
    assert(!source->asset && acount(source->diagnostics));
    code_source_free(source);
}

static void incomplete_loop_intelligence()
{
    const char *prefixes[] = {
        "behavior Probe { state values: int[] = [1,2]; state score: int = 0; "
        "on Tick() { for(item in values) { score = ",
        "behavior Probe { state values: int[] = [1,2]; state score: int = 0; "
        "on Tick() { foreach(item in values) { score = ",
        "behavior Probe { state score: int = 0; on Tick() { for(item in 0..3) "
        "{ score = "
    };
    for (int i = 0; i < 3; i++) {
        char text[512];
        snprintf(text, sizeof text, "%site", prefixes[i]);
        code_source *source = code_source_parse(text);
        assert(!source->asset && acount(source->diagnostics));
        asset_source_completions items;
        code_intelligence_complete(source, (int)strlen(text), &items);
        assert(has_completion(&items, "item"));
        asset_source_completions_free(&items);
        code_source_free(source);
        snprintf(text, sizeof text, "%sitem;", prefixes[i]);
        source = code_source_parse(text);
        assert(!source->asset && acount(source->diagnostics));
        int use = (int)strlen(prefixes[i]), declaration = at(text, "item in");
        asset_source_span definition;
        assert(code_intelligence_definition(source, use + 1, &definition));
        assert(definition.start == declaration && definition.end == declaration + 4);
        char *hover = code_intelligence_hover(source, use + 1);
        assert(hover && strstr(hover, "item: int"));
        free(hover);
        asset_source_span *references = code_intelligence_references(source, use + 1);
        assert(acount(references) == 2);
        afree(references);
        asset_source_span *spans = code_intelligence_highlights(source);
        for (int j = 0; j < acount(spans); j++) {
            if (spans[j].start == declaration) {
                assert(!strcmp(spans[j].cls, "tok-variable"));
            }
        }
        afree(spans);
        code_source_free(source);
    }
}

static void parser_completion()
{
    const char
        *ambiguous = "behavior Completion { value joined = engine[\"Append\"](";
    code_source *ambiguous_source = code_source_parse(ambiguous);
    asset_source_completions ambiguous_items;
    code_intelligence_complete(ambiguous_source, (int)strlen(ambiguous), &ambiguous_items);
    assert(!has_completion(&ambiguous_items, "Array"));
    asset_source_completions_free(&ambiguous_items);
    code_source_free(ambiguous_source);
    const char *text = "behavior Test { state title: string = \"hello\""
                       "; state count: int = 1; action log = "
                       "print_string(InString: title); }";
    code_source *source = code_source_parse(text);
    assert(source->asset);
    int offset = at(text, "InString: title") + 10;
    asset_source_completions items;
    code_intelligence_complete(source, offset + 2, &items);
    assert(has_completion(&items, "title") && !has_completion(&items, "count"));
    asset_source_completions_free(&items);
    code_source_free(source);
    text = "behavior Test { state title: string = \"hello\""
           "; state count: int = 1; action log = print_string(InString: ti";
    source = code_source_parse(text);
    code_intelligence_complete(source, (int)strlen(text), &items);
    assert(has_completion(&items, "title") && !has_completion(&items, "count"));
    asset_source_completions_free(&items);
    code_source_free(source);
    text = "behavior Test { action log = print_string(In";
    source = code_source_parse(text);
    code_intelligence_complete(source, (int)strlen(text), &items);
    assert(has_completion(&items, "InString"));
    asset_source_completions_free(&items);
    code_source_free(source);
    text = "behavior Test { action scale = engine[\"Multiply\"](A: double = "
           "cas";
    source = code_source_parse(text);
    code_intelligence_complete(source, (int)strlen(text), &items);
    assert(has_completion(&items, "cast<double>"));
    for (int i = 0; i < acount(items.items); i++) {
        if (!strcmp(items.items[i].label, "cast<double>")) {
            assert(!strcmp(items.items[i].insert, "cast<double>("));
            assert(strstr(items.items[i].detail, "-> double"));
        }
    }
    asset_source_completions_free(&items);
    code_source_free(source);
    text = "behavior Test { action scale = engine[\"Multiply\"](A: double = "
           "cast<";
    source = code_source_parse(text);
    code_intelligence_complete(source, (int)strlen(text), &items);
    assert(has_completion(&items, "double") && has_completion(&items, "int"));
    assert(
        !has_completion(&items, "entity") && !has_completion(&items, "struct") &&
        !has_completion(&items, "string") && !has_completion(&items, "set")
    );
    asset_source_completions_free(&items);
    code_source_free(source);
    text = "behavior Test { state total: double = 0; on Tick() { total = "
           "cast<dou";
    source = code_source_parse(text);
    code_intelligence_complete(source, (int)strlen(text), &items);
    assert(has_completion(&items, "double") && !has_completion(&items, "entity"));
    asset_source_completions_free(&items);
    code_source_free(source);
    text = "behavior Test { state title: str";
    source = code_source_parse(text);
    code_intelligence_complete(source, (int)strlen(text), &items);
    assert(has_completion(&items, "string"));
    asset_source_completions_free(&items);
    code_source_free(source);
    text = "behavior Test { on Ti";
    source = code_source_parse(text);
    code_intelligence_complete(source, (int)strlen(text), &items);
    assert(has_completion(&items, "Tick"));
    asset_source_completions_free(&items);
    code_source_free(source);
    text = "behavior Test { value movement = "
           "get_component<\"GameMovement\">(); action log = "
           "print_string(InString: movement.spe";
    source = code_source_parse(text);
    code_intelligence_complete(source, (int)strlen(text), &items);
    assert(has_completion(&items, "speed"));
    asset_source_completions_free(&items);
    code_source_free(source);
    text = "behavior Test { on Tick(GameMovement movement) { "
           "print_string(InString: movement.spe";
    source = code_source_parse(text);
    code_intelligence_complete(source, (int)strlen(text), &items);
    assert(has_completion(&items, "speed"));
    asset_source_completions_free(&items);
    code_source_free(source);
    code_source *library = code_source_parse("library Shared { pure function Seven() -> (Result: int) { result Result: int = 7; } }");
    assert(library->asset);
    const behavior_asset *libraries[] = { library->asset };
    code_source_context context = { .libraries = libraries, .library_count = 1 };
    text = "behavior Test { value chosen = Sha";
    source = code_source_parse_context(text, NULL, &context);
    code_intelligence_complete(source, (int)strlen(text), &items);
    assert(has_completion(&items, "Shared.Seven"));
    asset_source_completions_free(&items);
    code_source_free(source);
    text = "behavior Test { value chosen = Shared.Se";
    source = code_source_parse_context(text, NULL, &context);
    code_intelligence_complete(source, (int)strlen(text), &items);
    assert(has_completion(&items, "Seven"));
    for (int i = 0; i < acount(items.items); i++) {
        if (!strcmp(items.items[i].label, "Seven")) {
            assert(items.items[i].insert && !strcmp(items.items[i].insert, "Seven"));
        }
    }
    asset_source_completions_free(&items);
    code_source_free(source);
    code_source_free(library);
}

static void scoped_event_alias()
{
    const char *text = "behavior Alias { state elapsed: float = 0; on Tick() { "
                       "elapsed += event.dt; } on Begin() { elapsed = 0; } }";
    code_source *source = code_source_parse(text);
    assert(source->asset);
    int count, aliases = 0;
    uint64_t ids[2] = { 0 };
    const code_semantic_symbol *symbols = code_intelligence_symbols(source, &count);
    for (int i = 0; i < count; i++) {
        if (symbols[i].kind != CODE_SYMBOL_EVENT || strcmp(symbols[i].name, "event")) {
            continue;
        }
        assert(
            aliases < 2 && symbols[i].definition_id &&
            symbols[i].graph_id == source->asset->graph->graph_id && symbols[i].node_id
        );
        ids[aliases++] = symbols[i].id;
        const code_semantic_symbol
            *target = code_intelligence_symbol_by_id(source, symbols[i].definition_id);
        assert(target && strcmp(target->name, "event"));
        assert(symbols[i].scope_start < symbols[i].scope_end);
    }
    assert(aliases == 2 && ids[0] != ids[1]);
    int root = at(text, "event.dt");
    asset_source_span definition;
    assert(
        code_intelligence_definition(source, root, &definition) &&
        definition.start == at(text, "Tick")
    );
    asset_source_completions items;
    code_intelligence_complete(source, root + 2, &items);
    assert(has_completion(&items, "event.dt"));
    asset_source_completions_free(&items);
    code_source_free(source);
}

static void shared_handler_references()
{
    const char
        *text = "behavior Handlers { state enabled: bool = false; "
                "action chooser = flip_flop(); after chooser.A { enabled = "
                "event.\"Is A\"; } on Tick() { chooser.exec(); } }";
    code_source *source = code_source_parse(text);
    if (!source->asset) {
        for (int i = 0; i < acount(source->diagnostics); i++) {
            fprintf(
                stderr,
                "shared handler %d:%d: %s\n",
                source->diagnostics[i].line,
                source->diagnostics[i].column,
                source->diagnostics[i].message
            );
        }
    }
    assert(source->asset);
    int declaration = at(text, "chooser ="), handler = at(text, "after chooser") + 6;
    asset_source_span definition;
    assert(
        code_intelligence_definition(source, handler, &definition) &&
        definition.start == declaration
    );
    asset_source_span *references = code_intelligence_references(source, declaration);
    bool handled = false;
    for (int i = 0; i < acount(references); i++) {
        handled |= references[i].start == handler;
    }
    assert(handled && acount(references) >= 3);
    afree(references);
    char error[256];
    char *renamed = code_intelligence_rename(source, declaration, "choice", error, sizeof error);
    assert(
        renamed && strstr(renamed, "action choice =") &&
        strstr(renamed, "after choice.A") && strstr(renamed, "choice.exec()")
    );
    assert(strstr(renamed, "event.\"Is A\""));
    code_source *candidate = code_source_parse(renamed);
    assert(candidate->asset);
    code_source_free(candidate);
    free(renamed);
    code_source_free(source);
}

void test_code_source_intelligence()
{
    incomplete_body_intelligence();
    incomplete_loop_intelligence();
    nested_control_execution();
    source_capture_labels();
    qualified_statement_intelligence();
    callable_completion_collisions();
    completion_spelling_and_relevance();
    prototype_completion_drafts();
    label_navigation();
    local_references();
    imported_signature_refresh();
    implicit_producer_context_refresh();
    sequenced_statement_identity();
    inherited_import_defaults();
    imported_diagnostic_provenance();
    shared_handler_references();
    scoped_event_alias();
    parser_completion();
    imported_native_identity();
    char text[] = "state int score = 0; on Tick { int score = 1; score = score "
                  "+ 1; } "
                  "on Input { score = 2; int count = 3; }";
    code_source source = { .text = text };
    int length = (int)strlen(text), tick = at(text, "Tick"), input = at(text, "Input");
    int outer = at(text, "score"), inner = at(text, "score = 1");
    int write = at(text, "score = score"), read = write + 8,
        other = at(text, "score = 2");
    code_semantic_type integer = { .type = PIN_I32, .container = PIN_CONTAINER_SINGLE };
    code_semantic_symbol symbols[] = { { .id = 1,
                                         .name = "score",
                                         .kind = CODE_SYMBOL_STATE,
                                         .type = integer,
                                         .start = outer,
                                         .end = outer + 5,
                                         .scope_start = 0,
                                         .scope_end = length,
                                         .renameable = true },
                                       { .id = 2,
                                         .owner_id = 3,
                                         .name = "score",
                                         .kind = CODE_SYMBOL_LOCAL,
                                         .type = integer,
                                         .start = inner,
                                         .end = inner + 5,
                                         .scope_start = inner + 5,
                                         .scope_end = input - 5,
                                         .renameable = true },
                                       { .id = 3,
                                         .name = "Tick",
                                         .kind = CODE_SYMBOL_EVENT,
                                         .signature = "Tick(float dt)",
                                         .start = tick,
                                         .end = tick + 4,
                                         .scope_end = length },
                                       { .id = 4,
                                         .name = "count",
                                         .kind = CODE_SYMBOL_LOCAL,
                                         .type = integer,
                                         .start = at(text, "count"),
                                         .end = at(text, "count") + 5,
                                         .scope_start = input,
                                         .scope_end = length,
                                         .renameable = true } };
    for (int i = 0; i < 4; i++) {
        code_intelligence_symbol(&source, &symbols[i]);
    }
    code_intelligence_use(&source, (code_semantic_use){ 2, write, write + 5, CODE_USE_WRITE });
    code_intelligence_use(&source, (code_semantic_use){ 2, read, read + 5, CODE_USE_READ });
    code_intelligence_use(&source, (code_semantic_use){ 1, other, other + 5, CODE_USE_WRITE });
    asset_source_span definition;
    assert(
        code_intelligence_definition(&source, read, &definition) && definition.start == inner
    );
    assert(
        code_intelligence_definition(&source, other, &definition) && definition.start == outer
    );
    asset_source_span *references = code_intelligence_references(&source, write);
    assert(
        acount(references) == 3 && references[0].start == inner && references[2].start == read
    );
    afree(references);
    char error[256] = "";
    char *renamed = code_intelligence_rename(&source, write, "local_score", error, sizeof error);
    assert(renamed && strstr(renamed, "state int score") && strstr(renamed, "int local_score = 1"));
    assert(strstr(renamed, "local_score = local_score + 1") && strstr(renamed, "Input { score = 2"));
    free(renamed);
    renamed = code_intelligence_rename(&source, other, "count", error, sizeof error);
    assert(!renamed && strstr(error, "capture"));
    assert(!code_intelligence_rename(&source, write, "event", error, sizeof error));
    code_completion_context context = { .start = write,
                                        .end = write + 5,
                                        .kinds = CODE_SYMBOL_MASK(CODE_SYMBOL_STATE) |
                                                 CODE_SYMBOL_MASK(CODE_SYMBOL_LOCAL),
                                        .typed = true,
                                        .expected = integer };
    code_intelligence_context(&source, &context);
    asset_source_completions completion;
    code_intelligence_complete(&source, write + 3, &completion);
    assert(
        completion.start == write && completion.end == write + 5 &&
        acount(completion.items) == 1
    );
    assert(!strcmp(completion.items[0].label, "score"));
    asset_source_completions_free(&completion);
    context.expected.container = PIN_CONTAINER_ARRAY;
    code_intelligence_context(&source, &context);
    code_intelligence_complete(&source, write + 3, &completion);
    assert(!acount(completion.items));
    asset_source_completions_free(&completion);
    code_intelligence_call(&source, (code_semantic_call){ 3, 1, write, write + 5 });
    code_semantic_call *calls = code_intelligence_calls(&source, outer, true);
    assert(acount(calls) == 1 && calls[0].caller_id == 3 && calls[0].start == write);
    afree(calls);
    calls = code_intelligence_calls(&source, tick, false);
    assert(acount(calls) == 1 && calls[0].target_id == 1);
    afree(calls);
    char *hover = code_intelligence_hover(&source, tick);
    assert(hover && strstr(hover, "Tick(float dt)"));
    free(hover);
    code_intelligence_location(&source, 1, 11, 22, 33);
    const code_semantic_symbol *resolved = code_intelligence_symbol_at(&source, other);
    assert(resolved->graph_id == 11 && resolved->node_id == 22 && resolved->pin_id == 33);
    asset_source_span *highlights = code_intelligence_highlights(&source);
    bool variable = false, function = false;
    for (int i = 0; i < acount(highlights); i++) {
        variable |= highlights[i].start == read &&
                    !strcmp(highlights[i].cls, "tok-variable");
        function |= highlights[i].start == tick &&
                    !strcmp(highlights[i].cls, "tok-function");
    }
    assert(variable && function);
    afree(highlights);
    code_intelligence_clear(&source);
    assert(!source.intelligence);
}
