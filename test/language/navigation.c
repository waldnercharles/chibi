#include "assets/input_map.h"
#include "authoring/source_catalog.h"
#include "authoring/source_intelligence.h"
#include "authoring/source_rename.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static authoring_document documents[3];
static script_analysis *declarations[3];
static script_catalog_capability capabilities[10];
static script_catalog_parameter parameters[10][4], results[10][4];
static char qualified[10][128];
static authoring_navigation *navigation;
static authoring_rename_plan *plan;
static int document_count;
static char error[192];
static char *edited;
static authoring_completions completions;
static authoring_highlight *highlights;
static source_catalog *input_catalog;
static const script_catalog_field fields[] = {
    { .id = 20, .name = "x", .type = { .storage = PIN_I32 }, .writable = true },
    { .id = 21, .name = "y", .type = { .storage = PIN_I32 }, .offset = sizeof(int), .writable = true },
};
static const script_catalog_type types[] = {
    { .id = 10,
      .name = "Position",
      .type = { .storage = PIN_STRUCT, .nominal = 10 },
      .fields = fields,
      .field_count = 2,
      .size = 2 * sizeof(int) },
};
static script_catalog catalog;
static const script_catalog_global globals[] = {
    { .id = 300, .name = "hostValue", .type = { .storage = PIN_I32 } },
};

static void cleanup()
{
    authoring_navigation_free(navigation);
    authoring_rename_plan_free(plan);
    free(edited);
    authoring_completions_free(&completions);
    free(highlights);
    highlights = NULL;
    for (int i = 0; i < document_count; i++) {
        script_analysis_free((script_analysis *)documents[i].analysis);
        script_analysis_free(declarations[i]);
        script_syntax_free((script_syntax *)documents[i].syntax);
    }
    memset(documents, 0, sizeof documents);
    memset(declarations, 0, sizeof declarations);
    source_catalog_free(input_catalog);
    input_catalog = NULL;
    navigation = NULL;
    plan = NULL;
    edited = NULL;
    document_count = 0;
}

/* The test catalog uses actual discovered declaration IDs and signatures. No
 * executable backend, graph compiler or runtime is constructed. */
static void base_catalog()
{
    cleanup();
    memset(capabilities, 0, sizeof capabilities);
    capabilities[0] = (script_catalog_capability){ .id = 100,
                                                   .name = "Tick",
                                                   .kind = SCRIPT_CAPABILITY_EVENT };
    catalog = (script_catalog){ .types = types,
                                .type_count = 1,
                                .capabilities = capabilities,
                                .capability_count = 1,
                                .tick_event = 100,
                                .globals = globals,
                                .global_count = 1 };
}

/* Adds the sources' library functions to the catalog, then analyzes them. */
static bool load(const char *const *source, int count)
{
    document_count = count;
    static const char *paths[] = { "lib.chibi", "app.chibi", "other.chibi" };
    for (int d = 0; d < count; d++) {
        documents[d].path = paths[d];
        documents[d].syntax = script_parse(source[d]);
        declarations[d] = script_declarations(documents[d].syntax, &catalog);
        const script_analysis *a = declarations[d];
        for (int i = 0; i < a->function_count; i++) {
            const script_function *f = &a->functions[i];
            if (f->kind != SCRIPT_FUNCTION_AUTHORED || f->symbol < 0) {
                continue;
            }
            int at = catalog.capability_count++;
            if (at >= 10 || f->parameter_count > 4 || f->result_count > 4) {
                return false;
            }
            const script_symbol *s = &a->symbols[f->symbol];
            snprintf(qualified[at], sizeof qualified[at], "%s.%s", a->name, s->name);
            for (int j = 0; j < f->parameter_count; j++) {
                const script_symbol *p = &a->symbols[f->parameters[j]];
                parameters[at][j] = (script_catalog_parameter){ .name = p->name,
                                                                .type = p->type };
                /* Constant defaults travel with the signature, as in a project catalog. */
                if (p->initializer >= 0 &&
                    a->expressions[p->initializer].kind == SCRIPT_EXPR_CONSTANT) {
                    parameters[at][j].has_default = true;
                    parameters[at][j]
                        .default_value = a->expressions[p->initializer].constant;
                }
            }
            for (int j = 0; j < f->result_count; j++) {
                const script_symbol *r = &a->symbols[f->results[j]];
                results[at][j] = (script_catalog_parameter){ .name = r->name,
                                                             .type = r->type };
            }
            capabilities[at] = (script_catalog_capability){
                .id = s->id,
                .name = s->name,
                .qualified_name = qualified[at],
                .parameters = parameters[at],
                .parameter_count = f->parameter_count,
                .results = results[at],
                .result_count = f->result_count,
                .pure = f->pure,
                .completion_outcome = -1
            };
        }
    }
    bool valid = true;
    for (int d = 0; d < count; d++) {
        documents[d].analysis = script_analyze(documents[d].syntax, &catalog);
        const script_analysis *a = documents[d].analysis;
        valid &= a->valid && !documents[d].syntax->diagnostic_count;
        for (int i = 0; i < a->diagnostic_count; i++) {
            fprintf(stderr, "navigation: %s\n", a->diagnostics[i].message);
        }
    }
    navigation = authoring_navigation_new(documents, count, &catalog);
    return valid;
}

static bool project(const char *const *source, int count)
{
    base_catalog();
    return load(source, count);
}

static const authoring_reference *at(int document, const char *token)
{
    const char *text = documents[document].syntax->text;
    const char *found = strstr(text, token);
    return found ? authoring_navigation_at(navigation, document, (int)(found - text))
                 : NULL;
}

static int references(authoring_target target)
{
    int count = 0;
    for (int i = 0; i < authoring_navigation_reference_count(navigation); i++) {
        count += authoring_target_equal(
            authoring_navigation_reference_at(navigation, i)->target,
            target
        );
    }
    return count;
}

static bool plan_rename(authoring_target target, const char *name)
{
    authoring_rename_plan_free(plan);
    plan = authoring_source_rename(navigation, target, name, error, sizeof error);
    if (!plan) {
        fprintf(stderr, "rename: %s\n", error);
    }
    return plan != NULL;
}

static char *apply(int document)
{
    return authoring_rename_document(plan, document, documents[document].syntax->text, error, sizeof error);
}

TEST_CASE(navigation_resolves_shadowed_identity)
{
    const char *sources[] = { "library Scope { function F(x:int)->(r:int){ if "
                              "true { x:=2; r=x; } r=x; return(r); } }" };
    REQUIRE(project(sources, 1));
    const authoring_reference *outer = at(0, "x:int"), *inner = at(0, "x:=2");
    REQUIRE(outer && inner);
    REQUIRE(!authoring_target_equal(outer->target, inner->target));
    REQUIRE(references(outer->target) == 2);
    REQUIRE(references(inner->target) == 2);
    REQUIRE(authoring_navigation_definition(navigation, inner->target) == inner);
    REQUIRE(plan_rename(outer->target, "outer"));
    REQUIRE(plan->count == 2);
    edited = apply(0);
    REQUIRE(edited);
    REQUIRE(strstr(edited, "F(outer:int)"));
    REQUIRE(strstr(edited, "x:=2; r=x;"));
    REQUIRE(strstr(edited, "r=outer;"));
    return true;
}

TEST_CASE(rename_rejects_capture_in_both_directions)
{
    const char *sources[] = {
        "library Capture { function F(x:int)->(r:int){ y:=2; if true { z:=3; "
        "r=x+y+z; } return(r); } }"
    };
    REQUIRE(project(sources, 1));
    const authoring_reference *x = at(0, "x:int"), *z = at(0, "z:=3");
    REQUIRE(x && z);
    REQUIRE(!plan_rename(x->target, "y"));
    REQUIRE(!plan_rename(x->target, "z"));
    REQUIRE(!plan_rename(z->target, "y"));
    REQUIRE(strstr(error, "capture"));
    REQUIRE(!plan_rename(x->target, "two words"));
    return true;
}

TEST_CASE(rename_cross_file_callable_parameters_results_and_program)
{
    const char *sources[] = {
        "library Utils { pure function "
        "Pair(amount:int)->(first:int,second:int){ return(amount,amount+1); } "
        "}",
        "behavior App { on Tick { Split:=99; answer:=Utils.Pair(amount:2); "
        "total:=answer.first; } }",
    };
    REQUIRE(project(sources, 2));
    const authoring_reference *function = at(0, "Pair("),
                              *parameter = at(0, "amount:int"),
                              *result = at(0, "first:int"),
                              *program = at(0, "Utils");
    REQUIRE(function && parameter && result && program);
    REQUIRE(plan_rename(function->target, "Split"));
    REQUIRE(plan->count == 2);
    edited = apply(1);
    REQUIRE(edited);
    REQUIRE(strstr(edited, "Utils.Split(amount:2)"));
    free(edited);
    edited = NULL;
    REQUIRE(plan_rename(parameter->target, "input"));
    REQUIRE(plan->count == 4);
    edited = apply(1);
    REQUIRE(edited);
    REQUIRE(strstr(edited, "Pair(input:2)"));
    free(edited);
    edited = NULL;
    REQUIRE(plan_rename(result->target, "head"));
    REQUIRE(plan->count == 2);
    edited = apply(1);
    REQUIRE(edited);
    REQUIRE(strstr(edited, "answer.head"));
    free(edited);
    edited = NULL;
    REQUIRE(program->target.kind == AUTHORING_PROGRAM);
    REQUIRE(plan_rename(program->target, "Helpers"));
    REQUIRE(plan->count == 2);
    edited = apply(1);
    REQUIRE(edited);
    REQUIRE(strstr(edited, "Helpers.Pair(amount:2)"));
    REQUIRE(!plan_rename(parameter->target, "second"));
    return true;
}

TEST_CASE(rename_preserves_quoted_names_and_escapes)
{
    const char *sources[] = {
        "library \"Odd Library\" { pure function "
        "\"quoted\\\" fn\"(arg:int)->(output:int){ return(arg); } }",
        "behavior App { on Tick { answer:=\"Odd Library\".\"quoted\\\" "
        "fn\"(3); "
        "} }",
    };
    REQUIRE(project(sources, 2));
    const authoring_reference *function = at(0, "\"quoted\\\" fn\"");
    REQUIRE(function);
    REQUIRE(plan_rename(function->target, "next\" function"));
    REQUIRE(plan->count == 2);
    REQUIRE(!strcmp(plan->edits[0].replacement, "\"next\\\" function\""));
    edited = apply(1);
    REQUIRE(edited);
    REQUIRE(strstr(edited, "\"Odd Library\".\"next\\\" function\"(3)"));
    return true;
}

TEST_CASE(navigation_tracks_native_field_access_roles)
{
    const char *sources[] = { "behavior Fields { p:Position={x:1,y:2}; on Tick "
                              "{ p.x=3; total:=p.y; } }" };
    REQUIRE(project(sources, 1));
    const authoring_reference *write = at(0, "x=3"), *read = at(0, "y;");
    REQUIRE(write && read);
    REQUIRE(write->target.kind == AUTHORING_FIELD && write->target.id == 20);
    REQUIRE(write->kind == AUTHORING_WRITE);
    REQUIRE(read->kind == AUTHORING_READ && read->target.id == 21);
    REQUIRE(!plan_rename(write->target, "renamed"));
    REQUIRE(strstr(error, "editable source"));
    return true;
}

TEST_CASE(rename_rejects_collisions_and_invalid_project_drafts)
{
    const char *sources[] = {
        "library First { pure function F()->(r:int){ return(1); } pure "
        "function G()->(r:int){ return(2); } }",
        "library Other { pure function H()->(r:int){ return(3); } }",
    };
    REQUIRE(project(sources, 2));
    const authoring_reference *function = at(0, "F()"), *program = at(0, "First");
    REQUIRE(function && program);
    REQUIRE(!plan_rename(function->target, "G"));
    REQUIRE(!plan_rename(program->target, "Other"));
    const char *invalid[] = { sources[0], "behavior Broken { on Tick { missing(); } }" };
    REQUIRE(!project(invalid, 2));
    function = at(0, "F()");
    REQUIRE(function);
    REQUIRE(!plan_rename(function->target, "Better"));
    REQUIRE(strstr(error, "valid project"));
    return true;
}

TEST_CASE(rename_rejects_capture_before_hoisted_declaration)
{
    const char *sources[] = {
        "behavior Forward { early:=hostValue; later:=1; on Tick { "
        "total:=later; } }",
    };
    REQUIRE(project(sources, 1));
    const authoring_reference *target = at(0, "later:=1");
    REQUIRE(target);
    REQUIRE(!plan_rename(target->target, "hostValue"));
    REQUIRE(strstr(error, "capture"));
    return true;
}

TEST_CASE(rename_groups_event_alias_handlers)
{
    const char *sources[] = {
        "behavior Events { event pulse=Tick; on pulse { count:=1; } }",
    };
    REQUIRE(project(sources, 1));
    const authoring_reference *declaration = at(0, "pulse=Tick"),
                              *handler = at(0, "pulse {");
    REQUIRE(declaration && handler);
    REQUIRE(authoring_target_equal(declaration->target, handler->target));
    REQUIRE(plan_rename(declaration->target, "changed"));
    REQUIRE(plan->count == 2);
    edited = apply(0);
    REQUIRE(edited);
    REQUIRE(strstr(edited, "event changed=Tick; on changed"));
    return true;
}

static const authoring_completion *completion(const char *name)
{
    for (int i = 0; i < completions.count; i++) {
        if (!strcmp(completions.items[i].label, name)) {
            return &completions.items[i];
        }
    }
    return NULL;
}

TEST_CASE(intelligence_scope_identity_and_owned_outputs)
{
    const char *sources[] = { "library Scope { function F(x:int)->(r:int){ if "
                              "true { x:=2; r=x; } r=x; return(r); } }" };
    REQUIRE(project(sources, 1));
    const char *text = documents[0].syntax->text;
    int cursor = (int)(strstr(text, "r=x;") - text) + 3;
    authoring_source_complete(navigation, 0, cursor, &completions);
    const authoring_completion *item = completion("x");
    const authoring_reference *inner = at(0, "x:=2");
    REQUIRE(item && inner);
    REQUIRE(authoring_target_equal(item->target, inner->target));
    REQUIRE(!strcmp(item->detail, "x: double"));
    REQUIRE(completions.range.start == cursor - 1 && completions.range.end == cursor);
    REQUIRE(completions.range.line == 1 && completions.range.column == cursor);
    edited = authoring_source_hover(navigation, 0, cursor - 1);
    REQUIRE(edited && !strcmp(edited, "x: double"));
    /* Owned presentation data outlives the borrowed navigation generation. */
    authoring_navigation_free(navigation);
    navigation = NULL;
    REQUIRE(!strcmp(item->insert, "x") && !strcmp(edited, "x: double"));
    return true;
}

TEST_CASE(intelligence_signatures_and_semantic_highlights)
{
    const char *sources[] = {
        "library Math { function Twice(input:int)->(answer:int) "
        "{ answer=input*2; return(answer); } }",
        "behavior App { score:=0; on Tick { score=Math.Twice(3); } }"
    };
    REQUIRE(project(sources, 2));
    int cursor = (int)(strstr(documents[1].syntax->text, "Twice") -
                       documents[1].syntax->text);
    edited = authoring_source_hover(navigation, 1, cursor);
    REQUIRE(edited && !strcmp(edited, "Math.Twice(input: int) -> (answer: int)"));
    int count = 0;
    highlights = authoring_source_highlights(navigation, 1, &count);
    bool state = false, callable = false, keyword = false, number = false;
    int last = -1;
    for (int i = 0; i < count; i++) {
        REQUIRE(highlights[i].range.start >= last);
        last = highlights[i].range.end;
        state |= !strcmp(highlights[i].cls, "tok-state");
        callable |= !strcmp(highlights[i].cls, "tok-function");
        keyword |= !strcmp(highlights[i].cls, "tok-keyword");
        number |= !strcmp(highlights[i].cls, "tok-number");
    }
    REQUIRE(state && callable && keyword && number);
    return true;
}

TEST_CASE(library_signatures_infer_results_from_returns)
{
    const char *sources[] = {
        "library Math { function Twice(input:int) { return input * 2 }"
        " function Both(input:int) { return(Low: Twice(input), High: input * "
        "4) } }",
        "behavior App { score:=0; on Tick { score=Math.Twice(3) + "
        "Math.Both(1).High; } }"
    };
    REQUIRE(project(sources, 2));
    int cursor = (int)(strstr(documents[1].syntax->text, "Twice") -
                       documents[1].syntax->text);
    edited = authoring_source_hover(navigation, 1, cursor);
    REQUIRE(edited && !strcmp(edited, "Math.Twice(input: int) -> (Result: int)"));
    /* A's Math.Twice is the library's, not the behavior's Twice, which calls A. */
    const char *qualified[] = {
        "library Math { function Twice(input:int) { return input * 2 } }",
        "behavior App { score:=0; function A(x:int) { return Math.Twice(x) }"
        " function Twice(x:int) { return A(x) } on Tick { score=Twice(3); } }"
    };
    REQUIRE(project(qualified, 2));
    return true;
}

TEST_CASE(intelligence_recovers_reflected_members_and_rejects_comments)
{
    const char *sources[] = {
        "behavior Fields { p:Position={x:1,y:2}; on Tick { p. } }"
    };
    REQUIRE(!project(sources, 1));
    int cursor = (int)(strstr(documents[0].syntax->text, "p. }") -
                       documents[0].syntax->text) +
                 2;
    authoring_source_complete(navigation, 0, cursor, &completions);
    const authoring_completion *x = completion("x"), *y = completion("y");
    REQUIRE(x && y && completions.count == 2);
    REQUIRE(x->target.kind == AUTHORING_FIELD && x->target.id == 20 && x->target.owner == 10);
    REQUIRE(!strcmp(x->detail, "x: int"));
    const char *comment[] = {
        "behavior Comments { on Tick { // hostValue\n } }"
    };
    REQUIRE(project(comment, 1));
    cursor = (int)(strstr(documents[0].syntax->text, "hostValue") -
                   documents[0].syntax->text) +
             4;
    authoring_source_complete(navigation, 0, cursor, &completions);
    REQUIRE(completions.count == 0);
    return true;
}

static void diagnose_syntax(TSNode node, int depth, int *remaining)
{
    if (!*remaining || depth > 6) {
        return;
    }
    (*remaining)--;
    fprintf(
        stderr,
        "%*s%s %u..%u\n",
        depth * 2,
        "",
        ts_node_type(node),
        ts_node_start_byte(node),
        ts_node_end_byte(node)
    );
    for (uint32_t i = 0; i < ts_node_named_child_count(node) && *remaining; i++) {
        diagnose_syntax(ts_node_named_child(node, i), depth + 1, remaining);
    }
}

TEST_CASE(intelligence_ranks_typed_visible_symbols_and_recovers_library_members)
{
    const char *ranked[] = {
        "behavior Suggestions { target:int=0; velocity_global:int=0; "
        "vel:int=0; vel_boolean:bool=false; on Tick { velocity:int=1; "
        "very_eventful_loop:int=2; target=vel; } }"
    };
    REQUIRE(project(ranked, 1));
    int cursor = (int)(strstr(documents[0].syntax->text, "target=vel;") -
                       documents[0].syntax->text) +
                 10;
    authoring_source_complete(navigation, 0, cursor, &completions);
    REQUIRE(completions.count == 4);
    REQUIRE(!strcmp(completions.items[0].label, "vel"));
    REQUIRE(!strcmp(completions.items[1].label, "velocity"));
    REQUIRE(!strcmp(completions.items[2].label, "velocity_global"));
    REQUIRE(!strcmp(completions.items[3].label, "very_eventful_loop"));
    REQUIRE(!completion("vel_boolean"));
    const char *drafts[] = {
        "behavior Draft { score:int=0; on Tick { score=sco",
        "behavior Draft { score:int=0; on Tick { score=",
        "behavior Draft { score:int=0; on Tick { score=score+sco",
    };
    for (int i = 0; i < 3; i++) {
        const char *source[] = { drafts[i] };
        REQUIRE(!project(source, 1));
        cursor = (int)strlen(drafts[i]);
        authoring_source_complete(navigation, 0, cursor, &completions);
        const authoring_completion *score = completion("score");
        const authoring_reference *definition = at(0, "score:int");
        if (!score || !definition) {
            char *tree = ts_node_string(ts_tree_root_node(documents[0].syntax->tree));
            fprintf(stderr, "assignment completion context %d: %s\nCST: %.2048s\n", i, drafts[i], tree);
            free(tree);
            int remaining = 64;
            diagnose_syntax(ts_tree_root_node(documents[0].syntax->tree), 0, &remaining);
        }
        REQUIRE(score && definition);
        REQUIRE(authoring_target_equal(score->target, definition->target));
    }
    const char *qualified[] = {
        "library Shared { function "
        "Multiply(x:int)->(answer:int){return(x);} }",
        "behavior Names { on Tick { chosen:=Shared.Mul; } }"
    };
    REQUIRE(!project(qualified, 2));
    cursor = (int)(strstr(documents[1].syntax->text, "Shared.Mul") -
                   documents[1].syntax->text) +
             10;
    authoring_source_complete(navigation, 1, cursor, &completions);
    const authoring_completion *multiply = completion("Multiply");
    REQUIRE(multiply && !strcmp(multiply->insert, "Multiply"));
    REQUIRE(multiply->target.kind == AUTHORING_CALLABLE);
    const authoring_reference *definition = at(0, "Multiply(");
    REQUIRE(definition && authoring_target_equal(multiply->target, definition->target));
    REQUIRE(
        completions.count == 1 && completions.range.start == cursor - 3 &&
        completions.range.end == cursor
    );
    const char *shadowed[] = {
        qualified[0],
        "behavior Names { on Tick { Shared:=1; chosen:=Shared.Mul; } }"
    };
    REQUIRE(!project(shadowed, 2));
    cursor = (int)(strstr(documents[1].syntax->text, "Shared.Mul") -
                   documents[1].syntax->text) +
             10;
    authoring_source_complete(navigation, 1, cursor, &completions);
    REQUIRE(!completion("Multiply"));
    return true;
}

TEST_CASE(intelligence_prototype_arguments_use_declared_reference_context)
{
    static const script_catalog_parameter parameter = {
        .name = "Prototype",
        .type = { .storage = PIN_STR },
        .reference_kind = SCRIPT_REFERENCE_PROTOTYPE
    };
    static const script_catalog_parameter configured[] = {
        { .name = "Count", .type = { .storage = PIN_F64 } },
        { .name = "Prototype",
          .type = { .storage = PIN_STR },
          .reference_kind = SCRIPT_REFERENCE_PROTOTYPE },
    };
    static const script_catalog_capability caps[] = {
        { .id = 100, .name = "Tick", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 },
        { .id = 600,
          .name = "MovementEmitter",
          .kind = SCRIPT_CAPABILITY_PROTOTYPE,
          .completion_outcome = -1 },
        { .id = 601,
          .name = "BoostEmitter",
          .kind = SCRIPT_CAPABILITY_PROTOTYPE,
          .completion_outcome = -1 },
        { .id = 602,
          .name = "Create",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = &parameter,
          .parameter_count = 1,
          .completion_outcome = -1 },
        { .id = 603,
          .name = "Configure",
          .kind = SCRIPT_CAPABILITY_FUNCTION,
          .parameters = configured,
          .parameter_count = 2,
          .completion_outcome = -1 },
    };
    const char *drafts[] = {
        "behavior Effects { on Tick { Create(MovementEmitter); } }",
        "behavior Effects { on Tick { Create(MovmentEmitter); } }",
        "behavior Effects { on Tick { Create(Move",
        "behavior Effects { on Tick { Create(",
        "behavior Effects { on Tick { Create(Prototype: MovementEmitter); } }",
        "behavior Effects { on Tick { Create(\"Prototype\": MovementEmitter); "
        "} }",
        "behavior Effects { on Tick { Configure(Count: 1, \"Prototype\": "
        "MovementEmitter); } }",
    };
    for (int i = 0; i < 7; i++) {
        cleanup();
        catalog = (script_catalog){ .capabilities = caps,
                                    .capability_count = 5,
                                    .tick_event = 100 };
        document_count = 1;
        documents[0].path = "effects.chibi";
        documents[0].syntax = script_parse(drafts[i]);
        documents[0].analysis = script_analyze(documents[0].syntax, &catalog);
        navigation = authoring_navigation_new(documents, 1, &catalog);
        const char *token = strstr(drafts[i], "MovementEmitter");
        if (!token) {
            token = strstr(drafts[i], "MovmentEmitter");
        }
        if (!token) {
            token = strstr(drafts[i], "Move");
        }
        int start = token ? (int)(token - drafts[i]) : (int)strlen(drafts[i]);
        int cursor = token ? start + 3 : start;
        authoring_source_complete(navigation, 0, cursor, &completions);
        const authoring_completion *emitter = completion("MovementEmitter");
        if (!emitter || emitter->target.kind != AUTHORING_PROTOTYPE ||
            emitter->target.id != 600) {
            char *tree = ts_node_string(ts_tree_root_node(documents[0].syntax->tree));
            fprintf(
                stderr,
                "prototype completion context %d at %d: %s\nCST: %.2048s\n",
                i,
                cursor,
                drafts[i],
                tree
            );
            free(tree);
            if (emitter) {
                fprintf(
                    stderr,
                    "completion target kind=%d id=%llu\n",
                    emitter->target.kind,
                    (unsigned long long)emitter->target.id
                );
            }
            const script_analysis *a = documents[0].analysis;
            for (int j = 0; j < a->expression_count; j++) {
                const script_expression *e = &a->expressions[j];
                if (e->kind != SCRIPT_EXPR_CALL) {
                    continue;
                }
                fprintf(
                    stderr,
                    "CALL %d..%d capability=%llu arguments=%d\n",
                    e->range.start,
                    e->range.end,
                    (unsigned long long)e->capability,
                    e->argument_count
                );
                for (int k = 0; k < e->argument_count; k++) {
                    fprintf(
                        stderr,
                        " argument %d..%d parameter=%d\n",
                        e->arguments[k].range.start,
                        e->arguments[k].range.end,
                        e->arguments[k].parameter
                    );
                }
            }
        }
        REQUIRE(emitter && !strcmp(emitter->insert, "MovementEmitter"));
        REQUIRE(emitter->target.kind == AUTHORING_PROTOTYPE && emitter->target.id == 600);
        REQUIRE(completions.range.start == start && completions.range.end >= cursor);
        REQUIRE(!completion("Prototype") && !completion("Create"));
    }
    return true;
}

static bool external_project()
{
    cleanup();
    static const script_catalog_parameter spawn_parameter = {
        .name = "prototype",
        .type = { .storage = PIN_STR },
        .reference_kind = SCRIPT_REFERENCE_PROTOTYPE
    };
    static const script_catalog_parameter component_parameter = {
        .name = "component",
        .type = { .storage = PIN_STR },
        .reference_kind = SCRIPT_REFERENCE_COMPONENT
    };
    static const script_catalog_type component_type = { .id = 10,
                                                        .name = "Position",
                                                        .type = { .storage = PIN_STRUCT,
                                                                  .nominal = 10 },
                                                        .fields = fields,
                                                        .field_count = 2,
                                                        .size = 2 * sizeof(int),
                                                        .component = true,
                                                        .component_id = 900 };
    static const script_catalog_capability caps[] = {
        { .id = 400,
          .name = "Spark",
          .kind = SCRIPT_CAPABILITY_PROTOTYPE,
          .completion_outcome = -1 },
        { .id = 401, .name = "Pulse", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 },
        { .id = 402,
          .name = "Spawn",
          .parameters = &spawn_parameter,
          .parameter_count = 1,
          .completion_outcome = -1 },
        { .id = 403,
          .name = "Has",
          .parameters = &component_parameter,
          .parameter_count = 1,
          .completion_outcome = -1 },
        { .id = 404, .name = "Taken", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 },
    };
    static const source_catalog_definition definitions[] = {
        { .id = 400,
          .kind = SCRIPT_CAPABILITY_PROTOTYPE,
          .name = "Spark",
          .path = "prototypes/Spark.kdl",
          .text = "name \"Spark\"\n",
          .range = { 5, 12, 1, 6 } },
        { .id = 401,
          .kind = SCRIPT_CAPABILITY_EVENT,
          .name = "Pulse",
          .path = "events/Pulse.kdl",
          .text = "name \"Pulse\"\n",
          .range = { 5, 12, 1, 6 } },
    };
    static const source_catalog_reference references[] = {
        { .id = 400,
          .kind = SOURCE_REFERENCE_PROTOTYPE,
          .name = "Spark",
          .path = "scenes/Start.kdl",
          .text = "prototype \"Spark\"\nnote \"Spark\"\n",
          .range = { 10, 17, 1, 11 } },
    };
    catalog = (script_catalog){ .types = &component_type,
                                .type_count = 1,
                                .capabilities = caps,
                                .capability_count = 5 };
    document_count = 1;
    documents[0] = (authoring_document){
        .path = "behaviors/Consumer.chibi",
        .syntax = script_parse("behavior Consumer { p:Position={x:1,y:2}; label:=\"Spark\"; event pulse=Pulse; on pulse { Spawn(\"Spark\"); Has(Position); } }")
    };
    documents[0].analysis = script_analyze_document(
        documents[0].syntax,
        &catalog,
        documents[0].path
    );
    const script_analysis *analysis = documents[0].analysis;
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "external navigation: %s\n", analysis->diagnostics[i].message);
    }
    navigation = authoring_navigation_new_project(documents, 1, &catalog, definitions, 2, references, 1);
    return analysis->valid && !documents[0].syntax->diagnostic_count;
}
TEST_CASE(navigation_resolves_external_definitions_and_selected_references)
{
    REQUIRE(external_project());
    REQUIRE(authoring_navigation_document_count(navigation) == 4);
    const authoring_reference *prototype = at(0, "\"Spark\"); Has"),
                              *event = at(0, "Pulse;"),
                              *component = at(0, "Position);"),
                              *literal = at(0, "\"Spark\"; event");
    REQUIRE(
        prototype && prototype->target.kind == AUTHORING_PROTOTYPE &&
        prototype->target.id == 400
    );
    REQUIRE(event && event->target.kind == AUTHORING_CALLABLE && event->target.id == 401);
    REQUIRE(
        component && component->target.kind == AUTHORING_TYPE &&
        component->target.id == 10
    );
    REQUIRE(!literal);
    const authoring_reference
        *definition = authoring_navigation_definition(navigation, prototype->target);
    REQUIRE(definition && definition->location.document == 1);
    REQUIRE(
        !authoring_navigation_document_at(navigation, 1)->syntax &&
        !authoring_navigation_document_at(navigation, 1)->analysis
    );
    REQUIRE(!strcmp(authoring_navigation_document_text(navigation, 1), "name \"Spark\"\n"));
    REQUIRE(definition->location.range.start == 5 && definition->location.range.end == 12);
    const authoring_reference *scene = authoring_navigation_at(navigation, 3, 12);
    REQUIRE(scene && authoring_target_equal(scene->target, prototype->target));
    REQUIRE(references(prototype->target) == 3);
    REQUIRE(!authoring_navigation_definition(navigation, component->target));
    const authoring_reference *alias = at(0, "pulse=Pulse");
    REQUIRE(
        alias && authoring_target_equal(
                     authoring_navigation_callable(navigation, alias->target),
                     event->target
                 )
    );
    const authoring_reference *handler = at(0, "pulse {");
    REQUIRE(handler && authoring_target_equal(handler->target, alias->target));
    int count = 0;
    highlights = authoring_source_highlights(navigation, 0, &count);
    bool prototype_highlight = false;
    for (int i = 0; i < count; i++) {
        prototype_highlight |= highlights[i].range.start ==
                                   prototype->location.range.start &&
                               !strcmp(highlights[i].cls, "tok-type");
    }
    REQUIRE(prototype_highlight);
    edited = authoring_source_hover(navigation, 1, 7);
    REQUIRE(edited && !strcmp(edited, "prototype Spark"));
    authoring_source_complete(navigation, 1, 7, &completions);
    REQUIRE(completions.count == 0);
    int external_count = -1;
    authoring_highlight *external = authoring_source_highlights(navigation, 1, &external_count);
    bool empty = !external && external_count == 0;
    free(external);
    REQUIRE(empty);
    return true;
}
TEST_CASE(rename_external_declaration_uses_exact_source_and_catalog_identity)
{
    REQUIRE(external_project());
    const authoring_reference *prototype = at(0, "\"Spark\"); Has");
    REQUIRE(prototype && plan_rename(prototype->target, "Bright Spark"));
    REQUIRE(plan->count == 3);
    edited = authoring_rename_document(plan, 0, documents[0].syntax->text, error, sizeof error);
    REQUIRE(edited && strstr(edited, "Spawn(\"Bright Spark\")") && strstr(edited, "label:=\"Spark\""));
    free(edited);
    edited = authoring_rename_document(
        plan,
        1,
        authoring_navigation_document_text(navigation, 1),
        error,
        sizeof error
    );
    REQUIRE(edited && !strcmp(edited, "name \"Bright Spark\"\n"));
    free(edited);
    edited = authoring_rename_document(
        plan,
        3,
        authoring_navigation_document_text(navigation, 3),
        error,
        sizeof error
    );
    REQUIRE(edited && !strcmp(edited, "prototype \"Bright Spark\"\nnote \"Spark\"\n"));
    char *stale = authoring_rename_document(plan, 3, "prototype \"Other\"\nnote \"Spark\"\n", error, sizeof error);
    bool rejected = !stale && error[0];
    free(stale);
    REQUIRE(rejected);
    const authoring_reference *event = at(0, "Pulse;");
    REQUIRE(event && !plan_rename(event->target, "Taken"));
    REQUIRE(plan_rename(event->target, "Advance"));
    if (plan->count != 2) {
        for (int i = 0; i < plan->count; i++) {
            fprintf(
                stderr,
                "event rename edit %s:%d-%d '%s' -> '%s'\n",
                plan->edits[i].path,
                plan->edits[i].range.start,
                plan->edits[i].range.end,
                plan->edits[i].expected,
                plan->edits[i].replacement
            );
        }
    }
    REQUIRE(plan->count == 2);
    free(edited);
    edited = authoring_rename_document(plan, 0, documents[0].syntax->text, error, sizeof error);
    REQUIRE(edited && strstr(edited, "event pulse=Advance; on pulse {"));
    const authoring_reference *component = at(0, "Position);");
    REQUIRE(component && !plan_rename(component->target, "MovedPosition"));
    return true;
}

/* Spawn takes a prototype by name; Spark and Ember are prototypes. */
static bool spawn_project(const char *const *source, int count)
{
    static const script_catalog_parameter spawn_parameter = {
        .name = "prototype",
        .type = { .storage = PIN_NAME },
        .reference_kind = SCRIPT_REFERENCE_PROTOTYPE
    };
    static const source_catalog_definition definitions[] = {
        { .id = 400,
          .kind = SCRIPT_CAPABILITY_PROTOTYPE,
          .name = "Spark",
          .path = "prototypes/Spark.kdl",
          .text = "name \"Spark\"\n",
          .range = { 5, 12, 1, 6 } },
    };
    base_catalog();
    capabilities[1] = (script_catalog_capability){ .id = 400,
                                                   .name = "Spark",
                                                   .kind = SCRIPT_CAPABILITY_PROTOTYPE,
                                                   .completion_outcome = -1 };
    capabilities[2] = (script_catalog_capability){ .id = 401,
                                                   .name = "Ember",
                                                   .kind = SCRIPT_CAPABILITY_PROTOTYPE,
                                                   .completion_outcome = -1 };
    capabilities[3] = (script_catalog_capability){ .id = 402,
                                                   .name = "Spawn",
                                                   .parameters = &spawn_parameter,
                                                   .parameter_count = 1,
                                                   .completion_outcome = -1 };
    catalog.capability_count = 4;
    bool valid = load(source, count);
    authoring_navigation_free(navigation);
    navigation = authoring_navigation_new_project(documents, count, &catalog, definitions, 1, NULL, 0);
    return valid;
}

static bool names_prototype(int document, const char *token, uint64_t id)
{
    const authoring_reference *reference = at(document, token);
    return reference && reference->target.kind == AUTHORING_PROTOTYPE &&
           reference->target.id == id && reference->kind == AUTHORING_HANDLE;
}

TEST_CASE(navigation_follows_prototype_names_into_spawn)
{
    const char *sources[] = {
        "library Effects { function Launch(kind:name=\"\"){ Spawn(kind); } "
        "function Burst(kind:name=\"Ember\"){ Launch(kind:kind); } }",
        "behavior App { label:=\"Spark\"; aim:name=\"Spark\"; on Tick { "
        "pick:=\"Spark\"; Spawn(pick); aim=\"Ember\"; Spawn(aim); "
        "Effects.Burst(kind:\"Spark\"); miss:=\"Nothing\"; "
        "Spawn(miss); } }",
    };
    REQUIRE(spawn_project(sources, 2));
    REQUIRE(names_prototype(1, "\"Spark\"; Spawn(pick)", 400)); /* A local. */
    REQUIRE(names_prototype(1, "\"Spark\"; on Tick", 400));     /* State. */
    REQUIRE(names_prototype(1, "\"Ember\"; Spawn(aim)", 401)); /* Its assignment. */
    /* Through two library parameters, from an argument and from a default. */
    REQUIRE(names_prototype(1, "\"Spark\"); miss", 400));
    REQUIRE(names_prototype(0, "\"Ember\"){", 401));
    REQUIRE(!at(0, "\"\"){"));          /* Names no prototype. */
    REQUIRE(!at(1, "\"Nothing\""));     /* Names no prototype. */
    REQUIRE(!at(1, "\"Spark\"; aim:")); /* Never reaches a spawn. */
    const authoring_reference *spark = at(1, "\"Spark\"; Spawn(pick)");
    REQUIRE(references(spark->target) == 4);
    REQUIRE(plan_rename(spark->target, "Bright Spark"));
    edited = apply(1);
    REQUIRE(edited);
    REQUIRE(strstr(edited, "label:=\"Spark\"; aim:name=\"Bright Spark\";"));
    REQUIRE(strstr(edited, "pick:=\"Bright Spark\";"));
    REQUIRE(strstr(edited, "Effects.Burst(kind:\"Bright Spark\")"));
    return true;
}
/* A call that omits a defaulted parameter references the prototype only at its default. */
TEST_CASE(prototype_rename_skips_omitted_defaults)
{
    const char *sources[] = {
        "library Effects { function Burst(kind:name=\"Spark\"){ Spawn(kind); } "
        "}",
        "behavior App { on Tick { Effects.Burst(); } }",
    };
    REQUIRE(spawn_project(sources, 2));
    const authoring_reference *spark = at(0, "\"Spark\"){");
    REQUIRE(spark && references(spark->target) == 2); /* The default and the prototype. */
    REQUIRE(plan_rename(spark->target, "Bright Spark"));
    edited = apply(0);
    REQUIRE(edited && strstr(edited, "kind:name=\"Bright Spark\""));
    return true;
}
TEST_CASE(rename_declared_events_preserves_exact_rml_tokens)
{
    static const script_catalog_capability pulse = { .id = 401,
                                                     .name = "Pulse",
                                                     .kind = SCRIPT_CAPABILITY_EVENT,
                                                     .completion_outcome = -1 };
    static const source_catalog_definition definition = {
        .id = 401,
        .kind = SCRIPT_CAPABILITY_EVENT,
        .name = "Pulse",
        .path = "events/Pulse.kdl",
        .text = "name \"Pulse\"\n",
        .range = { 5, 12, 1, 6 },
        .text_kind = SOURCE_TEXT_KDL
    };
    static const char
        rml[] = "<button onclick=\"emit Pulse\" data-keys=\"Space:Pulse\" "
                "data-area-keys=\"Left:Pulse Right:Other\">Pulse</button><!-- "
                "Pulse -->";
    const source_catalog_reference rml_references[] = {
        { .id = 401,
          .kind = SOURCE_REFERENCE_EVENT,
          .name = "Pulse",
          .path = "ui/Hud.rml",
          .text = rml,
          .range = { 22, 27, 1, 23 },
          .text_kind = SOURCE_TEXT_RML },
        { .id = 401,
          .kind = SOURCE_REFERENCE_EVENT,
          .name = "Pulse",
          .path = "ui/Hud.rml",
          .text = rml,
          .range = { 46, 51, 1, 47 },
          .text_kind = SOURCE_TEXT_RML },
        { .id = 401,
          .kind = SOURCE_REFERENCE_EVENT,
          .name = "Pulse",
          .path = "ui/Hud.rml",
          .text = rml,
          .range = { 74, 79, 1, 75 },
          .text_kind = SOURCE_TEXT_RML },
    };
    catalog = (script_catalog){ .capabilities = &pulse, .capability_count = 1 };
    document_count = 1;
    documents[0] = (authoring_document){
        .path = "behaviors/Consumer.chibi",
        .syntax = script_parse("behavior Consumer { on Pulse {} }")
    };
    documents[0].analysis = script_analyze_document(
        documents[0].syntax,
        &catalog,
        documents[0].path
    );
    REQUIRE(documents[0].analysis->valid);
    navigation = authoring_navigation_new_project(documents, 1, &catalog, &definition, 1, rml_references, 3);
    const authoring_reference *event = at(0, "Pulse {}");
    REQUIRE(event && event->target.kind == AUTHORING_CALLABLE && event->target.id == 401);
    REQUIRE(authoring_navigation_document_count(navigation) == 3);
    REQUIRE(authoring_navigation_document_kind(navigation, 2) == SOURCE_TEXT_RML);
    const authoring_reference *rml_event = authoring_navigation_at(navigation, 2, 24);
    REQUIRE(rml_event && authoring_target_equal(rml_event->target, event->target));
    REQUIRE(references(event->target) == 5);
    REQUIRE(plan_rename(event->target, "Advance"));
    REQUIRE(plan->count == 5);
    edited = authoring_rename_document(plan, 2, rml, error, sizeof error);
    REQUIRE(
        edited &&
        !strcmp(
            edited,
            "<button onclick=\"emit Advance\" data-keys=\"Space:Advance\" "
            "data-area-keys=\"Left:Advance Right:Other\">Pulse</button><!-- "
            "Pulse -->"
        )
    );
    char *stale = authoring_rename_document(
        plan,
        2,
        "<button onclick=\"emit Other\" data-keys=\"Space:Pulse\" "
        "data-area-keys=\"Left:Pulse Right:Other\">Pulse</button><!-- Pulse "
        "-->",
        error,
        sizeof error
    );
    bool rejected = !stale && error[0];
    free(stale);
    REQUIRE(rejected);
    const char *const unsafe[] = { "Bad\"Name", "Bad Name", "Bad:Name", "Bad\\Name" };
    for (int i = 0; i < 4; i++) {
        REQUIRE(!plan_rename(event->target, unsafe[i]) && error[0]);
    }
    return true;
}

/* The project catalog reads content/input.kdl, so `input.Jump`, `on Jump` and
 * the map's `Jump` node name one action. */
static bool input_map_project()
{
    static const char source[] = "behavior Jumper {\n"
                                 "    held: bool = false;\n"
                                 "    on Jump {\n"
                                 "        held = input.Jump.held;\n"
                                 "        held = input.jump.pressed;\n"
                                 "    }\n"
                                 "}\n";
    static const script_catalog_parameter action = { .name = "Action",
                                                     .type = { .storage = PIN_STR } };
    static const script_catalog_parameter value = { .name = "Value",
                                                    .type = { .storage = PIN_BOOL } };
    for (int i = 0; i < 2; i++) {
        const char *name = i ? INPUT_ACTION_PRESSED : INPUT_ACTION_HELD;
        capabilities[i] = (script_catalog_capability){
            .id = script_catalog_id("capability", name),
            .name = name,
            .kind = SCRIPT_CAPABILITY_FUNCTION,
            .parameters = &action,
            .parameter_count = 1,
            .results = &value,
            .result_count = 1,
            .effects = SCRIPT_EFFECT_READ,
            .completion_outcome = -1,
        };
    }
    catalog = (script_catalog){ .capabilities = capabilities, .capability_count = 2 };
    char root[] = "/tmp/chibi-input-navigation-XXXXXX", path[256];
    REQUIRE(mkdtemp(root));
    snprintf(path, sizeof path, "%s/" INPUT_MAP_FILE, root);
    FILE *file = fopen(path, "wb");
    REQUIRE(file);
    fputs("Jump {\n    (array)sources \"Space\"\n}\nDash {\n    (array)sources \"X\"\n}\n", file);
    fclose(file);
    input_catalog = source_catalog_new(root, &catalog);
    unlink(path);
    rmdir(root);
    REQUIRE(!source_catalog_diagnostic_count(input_catalog));
    const script_catalog *view = source_catalog_view(input_catalog);
    document_count = 1;
    documents[0] = (authoring_document){ .path = "behaviors/Jumper.chibi",
                                         .syntax = script_parse(source) };
    documents[0].analysis = script_analyze_document(
        documents[0].syntax,
        view,
        documents[0].path
    );
    for (int i = 0; i < documents[0].analysis->diagnostic_count; i++) {
        fprintf(
            stderr,
            "input navigation: %s\n",
            documents[0].analysis->diagnostics[i].message
        );
    }
    REQUIRE(documents[0].analysis->valid);
    navigation = authoring_navigation_new_project(
        documents,
        1,
        view,
        source_catalog_definitions(input_catalog),
        source_catalog_definition_count(input_catalog),
        source_catalog_references(input_catalog),
        source_catalog_reference_count(input_catalog)
    );
    return true;
}
TEST_CASE(navigation_connects_input_actions_to_the_input_map)
{
    REQUIRE(input_map_project());
    const authoring_reference *use = at(0, "Jump.held");
    REQUIRE(use && use->kind == AUTHORING_READ);
    REQUIRE(
        use->target.kind == AUTHORING_CALLABLE &&
        use->target.id == script_catalog_id("event", "Jump")
    );
    REQUIRE(use->location.range.end - use->location.range.start == 4);
    const authoring_reference *handler = at(0, "Jump {"),
                              *other = at(0, "jump.pressed");
    REQUIRE(handler && authoring_target_equal(handler->target, use->target));
    REQUIRE(other && authoring_target_equal(other->target, use->target));
    const authoring_reference *definition = authoring_navigation_definition(navigation, use->target);
    REQUIRE(definition && definition->location.document == 1);
    const char *map = authoring_navigation_document_text(navigation, 1);
    REQUIRE(authoring_navigation_document_kind(navigation, 1) == SOURCE_TEXT_KDL);
    REQUIRE(definition->location.range.start == 0 && definition->location.range.end == 4);
    REQUIRE(definition->location.range.line == 1 && definition->location.range.column == 1);
    REQUIRE(!strncmp(map, "Jump {", 6));
    const authoring_reference
        *dash = authoring_navigation_at(navigation, 1, (int)(strstr(map, "Dash") - map));
    REQUIRE(dash && dash->kind == AUTHORING_DEFINE && dash->location.range.line == 4);
    REQUIRE(references(use->target) == 4);
    return true;
}
TEST_CASE(rename_input_action_edits_the_input_map_and_scripts)
{
    REQUIRE(input_map_project());
    authoring_target jump = at(0, "Jump.held")->target;
    REQUIRE(!plan_rename(jump, "Dash"));
    REQUIRE(plan_rename(jump, "Leap"));
    REQUIRE(plan->count == 4);
    edited = apply(0);
    REQUIRE(
        edited && !strcmp(
                      edited,
                      "behavior Jumper {\n"
                      "    held: bool = false;\n"
                      "    on Leap {\n"
                      "        held = input.Leap.held;\n"
                      "        held = input.Leap.pressed;\n"
                      "    }\n"
                      "}\n"
                  )
    );
    free(edited);
    edited = authoring_rename_document(
        plan,
        1,
        authoring_navigation_document_text(navigation, 1),
        error,
        sizeof error
    );
    REQUIRE(
        edited && !strcmp(edited, "Leap {\n    (array)sources \"Space\"\n}\nDash {\n    (array)sources \"X\"\n}\n")
    );
    return true;
}

/* Scene binders can define one global name in multiple scenes. Each declaration
 * and qualified use must share the same field identity. */
static bool scene_globals_project()
{
    static const script_catalog_parameter key = { .name = "key",
                                                  .type = { .storage = PIN_STR } };
    static const script_catalog_parameter entity = { .name = "entity",
                                                     .type = { .storage = PIN_HANDLE,
                                                               .nominal = 31 } };
    static const script_catalog_field bindings[] = {
        { .id = 40,
          .name = "Level",
          .type = { .storage = PIN_HANDLE, .nominal = 31 },
          .read = 200,
          .read_key = "Level" },
        { .id = 41,
          .name = "Camera",
          .type = { .storage = PIN_HANDLE, .nominal = 31 },
          .read = 200,
          .read_key = "Camera" },
    };
    static const script_catalog_type scene_types[] = {
        { .id = 30,
          .name = "SceneGlobals",
          .type = { .storage = PIN_STR, .nominal = 30, .flags = SCRIPT_TYPE_NAMESPACE },
          .fields = bindings,
          .field_count = 2 },
        { .id = 31, .name = "entity", .type = { .storage = PIN_HANDLE, .nominal = 31 } },
    };
    static const script_catalog_global scene_globals[] = {
        { .id = 301,
          .name = "globals",
          .type = { .storage = PIN_STR, .nominal = 30, .flags = SCRIPT_TYPE_NAMESPACE } },
    };
    static const source_catalog_definition definitions[] = {
        { .id = 40,
          .field = true,
          .owner = 30,
          .name = "Level",
          .path = "scenes/First.kdl",
          .text = "GlobalBinder {\n    Name \"Level\"\n}\n",
          .range = { 24, 31, 2, 10 } },
        { .id = 40,
          .field = true,
          .owner = 30,
          .name = "Level",
          .path = "scenes/Second.kdl",
          .text = "GlobalBinder {\n    Name \"Level\"\n}\n",
          .range = { 24, 31, 2, 10 } },
        { .id = 41,
          .field = true,
          .owner = 30,
          .name = "Camera",
          .path = "scenes/Camera.kdl",
          .text = "GlobalBinder {\n    Name \"Camera\"\n}\n",
          .range = { 24, 32, 2, 10 } },
    };
    const char *sources[] = { "behavior App { on Tick { level:=globals.Level; "
                              "other:=globals.level; label:=\"Level\"; } }" };
    base_catalog();
    capabilities[1] = (script_catalog_capability){ .id = 200,
                                                   .name = "GetGlobal",
                                                   .parameters = &key,
                                                   .parameter_count = 1,
                                                   .results = &entity,
                                                   .result_count = 1,
                                                   .effects = SCRIPT_EFFECT_READ,
                                                   .completion_outcome = -1 };
    catalog.capability_count = 2;
    catalog.types = scene_types;
    catalog.type_count = 2;
    catalog.globals = scene_globals;
    catalog.global_count = 1;
    catalog.entity_type = 31;
    bool valid = load(sources, 1);
    authoring_navigation_free(navigation);
    navigation = authoring_navigation_new_declarations(documents, 1, &catalog, definitions, 3);
    return valid;
}

TEST_CASE(navigation_connects_scene_globals_and_completes_typed_bindings)
{
    REQUIRE(scene_globals_project());
    const authoring_reference *level = at(0, "Level;"), *other = at(0, "level; label");
    REQUIRE(level && other);
    REQUIRE(
        level->target.kind == AUTHORING_FIELD && level->target.owner == 30 &&
        level->target.id == 40
    );
    REQUIRE(authoring_target_equal(level->target, other->target));
    REQUIRE(references(level->target) == 4);
    const authoring_reference
        *definition = authoring_navigation_definition(navigation, level->target);
    REQUIRE(definition && definition->location.document == 1);
    REQUIRE(definition->type.storage == PIN_HANDLE && definition->type.nominal == 31);
    const authoring_reference *second = authoring_navigation_at(navigation, 2, 25);
    REQUIRE(
        second && second->kind == AUTHORING_DEFINE &&
        authoring_target_equal(second->target, level->target)
    );
    const char *text = documents[0].syntax->text;
    int cursor = (int)(strstr(text, "globals.Level") - text) + 8;
    authoring_source_complete(navigation, 0, cursor, &completions);
    const authoring_completion *item = completion("Level");
    REQUIRE(item && authoring_target_equal(item->target, level->target));
    REQUIRE(completion("Camera"));
    return true;
}

TEST_CASE(rename_scene_global_updates_all_binders_and_qualified_uses)
{
    REQUIRE(scene_globals_project());
    const authoring_reference *level = at(0, "Level;");
    REQUIRE(level);
    REQUIRE(!plan_rename(level->target, "Camera"));
    REQUIRE(!plan_rename(level->target, "camera"));
    REQUIRE(plan_rename(level->target, "World"));
    REQUIRE(plan->count == 4);
    edited = apply(0);
    REQUIRE(edited && strstr(edited, "globals.World; other:=globals.World; label:=\"Level\""));
    for (int document = 1; document <= 2; document++) {
        free(edited);
        edited = authoring_rename_document(
            plan,
            document,
            authoring_navigation_document_text(navigation, document),
            error,
            sizeof error
        );
        REQUIRE(edited && !strcmp(edited, "GlobalBinder {\n    Name \"World\"\n}\n"));
    }
    REQUIRE(plan_rename(level->target, "Main Level"));
    free(edited);
    edited = apply(0);
    REQUIRE(edited && strstr(edited, "globals.\"Main Level\""));
    return true;
}

void test_language_navigation()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(navigation_resolves_shadowed_identity);
    RUN_TEST_CASE(rename_rejects_capture_in_both_directions);
    RUN_TEST_CASE(rename_cross_file_callable_parameters_results_and_program);
    RUN_TEST_CASE(rename_preserves_quoted_names_and_escapes);
    RUN_TEST_CASE(navigation_tracks_native_field_access_roles);
    RUN_TEST_CASE(rename_rejects_collisions_and_invalid_project_drafts);
    RUN_TEST_CASE(rename_rejects_capture_before_hoisted_declaration);
    RUN_TEST_CASE(rename_groups_event_alias_handlers);
    RUN_TEST_CASE(intelligence_scope_identity_and_owned_outputs);
    RUN_TEST_CASE(intelligence_signatures_and_semantic_highlights);
    RUN_TEST_CASE(library_signatures_infer_results_from_returns);
    RUN_TEST_CASE(intelligence_recovers_reflected_members_and_rejects_comments);
    RUN_TEST_CASE(intelligence_ranks_typed_visible_symbols_and_recovers_library_members);
    RUN_TEST_CASE(intelligence_prototype_arguments_use_declared_reference_context);
    RUN_TEST_CASE(navigation_resolves_external_definitions_and_selected_references);
    RUN_TEST_CASE(navigation_follows_prototype_names_into_spawn);
    RUN_TEST_CASE(prototype_rename_skips_omitted_defaults);
    RUN_TEST_CASE(rename_external_declaration_uses_exact_source_and_catalog_identity);
    RUN_TEST_CASE(rename_declared_events_preserves_exact_rml_tokens);
    RUN_TEST_CASE(navigation_connects_input_actions_to_the_input_map);
    RUN_TEST_CASE(rename_input_action_edits_the_input_map_and_scripts);
    RUN_TEST_CASE(navigation_connects_scene_globals_and_completes_typed_bindings);
    RUN_TEST_CASE(rename_scene_global_updates_all_binders_and_qualified_uses);
    pu_setup(NULL, NULL);
}
