#include "language/syntax.h"

#include "authoring/source_format.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static script_syntax *original, *edited, *fresh;
static char *original_tree, *edited_tree, *fresh_tree;
static FILE *manifest_file;
static char *corpus_text;
static const char *corpus_manifest;
static bool corpus_format;
static char *formatted_text, *reformatted_text;

enum
{
    CORPUS_MAX_FILES = 256,
    CORPUS_MAX_FILE_BYTES = 512 * 1024,
    CORPUS_MAX_TOTAL_BYTES = 8 * 1024 * 1024,
    CORPUS_MAX_PATH_BYTES = 4096,
};

static void cleanup()
{
    script_syntax_free(original);
    script_syntax_free(edited);
    script_syntax_free(fresh);
    original = edited = fresh = NULL;
    free(original_tree);
    free(edited_tree);
    free(fresh_tree);
    original_tree = edited_tree = fresh_tree = NULL;
    free(corpus_text);
    corpus_text = NULL;
    free(formatted_text);
    free(reformatted_text);
    formatted_text = reformatted_text = NULL;
    if (manifest_file) {
        fclose(manifest_file);
    }
    manifest_file = NULL;
}

static TSNode field(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}

static TSNode declaration_body()
{
    return field(ts_node_named_child(ts_tree_root_node(original->tree), 0), "body");
}

static bool node_text(TSNode node, const char *text)
{
    if (ts_node_is_null(node)) {
        return false;
    }
    script_range range = script_node_range(node);
    return range.end - range.start == (int)strlen(text) &&
           !memcmp(original->text + range.start, text, strlen(text));
}

static void parse(const char *text)
{
    cleanup();
    original = script_parse(text);
    for (int i = 0; i < original->diagnostic_count && i < 8; i++) {
        fprintf(
            stderr,
            "syntax %d:%d: %s\n",
            original->diagnostics[i].range.line,
            original->diagnostics[i].range.column,
            original->diagnostics[i].message
        );
    }
}

TEST_CASE(language_declarations_actions_and_bindings)
{
    parse(
        "behavior Demo {\n"
        "owner \"ecs.entity\"; description \"fixture\";\n"
        "state count: int = 0; shared enabled := true;\n"
        "target := self.Velocity.Parent;\n"
        "action pending = delay(Duration: 1);\n"
        "event changed = self.Health.CurrentChanged;\n"
        "bind self.ParticleEmitter.Active = enabled;\n"
        "on Tick as frame(dt: float) { count += cast<int>(dt); }\n"
        "after pending.Completed { destroy(self); }\n"
        "events Other { on Begin { pending.start(); } }\n"
        "}"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode body = declaration_body();
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(body, 5)), "action_declaration"));
    TSNode handler = ts_node_named_child(body, 8);
    REQUIRE(!strcmp(ts_node_type(handler), "handler"));
    REQUIRE(node_text(field(handler, "kind"), "on"));
    REQUIRE(node_text(field(handler, "alias"), "frame"));
    return true;
}

TEST_CASE(language_callable_types_entries_and_results)
{
    parse(
        "library Helpers {\n"
        "Identity :: pure fn<T: plain int>(ref int source, item: T = 1) "
        "-> (Result: T) { return item }\n"
        "Apply :: fn(amount: int) -> () { entry Run { return(); } exit "
        "Done; }\n"
        "macro Route(key: name) -> (Success: bool) { return.Success(true); }\n"
        "converted := engine[\"To Real\"](Value: int = 3);\n"
        "chosen := Identity<int>(source: ref int from state.count, "
        "item: 2);\n"
        "}"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode callable = ts_node_named_child(declaration_body(), 0);
    REQUIRE(!strcmp(ts_node_type(callable), "function_declaration"));
    REQUIRE(node_text(field(callable, "name"), "Identity"));
    REQUIRE(!ts_node_is_null(field(callable, "type_parameters")));
    TSNode parameters = field(callable, "parameters");
    REQUIRE(ts_node_named_child_count(parameters) == 2);
    REQUIRE(node_text(field(ts_node_named_child(parameters, 0), "name"), "source"));
    return true;
}

TEST_CASE(language_system_queries_and_control_flow)
{
    parse(
        "system Motion(position: Transform, velocity: Velocity, damping: "
        "Igniter?) "
        "without Collider, Body {\n"
        "query components();\n"
        "position += velocity * dt;\n"
        "if damping { damping.heat = 0; } else if dt > 0 { position = "
        "velocity; }\n"
        "while dt > 0 { break; }\n"
        "for (i in 0..3) { continue; }\n"
        "foreach (item in [1, 2]) { consume(item); }\n"
        "switch (choice) { case 0: { consume(0); } case: { consume(1); } "
        "}\n"
        "sequence { branch { destroy(self); } branch { await delay(Duration: "
        "1); } }\n"
        "finish: goto finish;\n"
        "}"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode system = ts_node_named_child(ts_tree_root_node(original->tree), 0);
    REQUIRE(!strcmp(ts_node_type(system), "system"));
    REQUIRE(!ts_node_is_null(field(system, "without")));
    TSNode body = field(system, "body");
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(body, 0)), "query_declaration"));
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(body, 4)), "for_statement"));
    return true;
}

TEST_CASE(language_definitions_unions_and_type_patterns)
{
    parse(
        "library Combat {\n"
        "BaseDamage :: 12\n"
        "DamageFor :: pure fn(level: int) -> number { return level * 2 }\n"
        "OtherDamage :: (level: int) -> number { return level + BaseDamage }\n"
        "AttackState :: union { Idle | Windup(remaining: number) | "
        "Active(damage: number) }\n"
        "Status :: union { \"Yes\" | \"No\" };\n"
        "Evaluate :: (result: number | Cancelled) {\n"
        "if result is number num_result { consume(num_result) }\n"
        "if result is not Cancelled { consume(result as number) }\n"
        "switch result {\n"
        "case .Windup(remaining): consume(remaining)\n"
        "case .Idle: { consume(0) }\n"
        "case: consume(1)\n"
        "}\n"
        "}\n"
        "}"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode body = declaration_body();
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(body, 0)), "constant_declaration"));
    REQUIRE(node_text(field(ts_node_named_child(body, 1), "results"), "number"));
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(body, 3)), "union_declaration"));
    TSNode callable = ts_node_named_child(body, 5);
    TSNode condition = field(ts_node_named_child(field(callable, "body"), 0), "condition");
    REQUIRE(!strcmp(ts_node_type(condition), "is_expression"));
    REQUIRE(node_text(field(condition, "binding"), "num_result"));
    return true;
}

TEST_CASE(language_query_header_and_cancel_deadline)
{
    parse(
        "system Home query position: Transform, aim: Targets.Transform, "
        "parent: Velocity? from ancestor(ChildOf) "
        "where position and (aim or not parent) {\n"
        "on Tick { charge := Charge(); cancel charge after 1.5 }\n"
        "}"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode system = ts_node_named_child(ts_tree_root_node(original->tree), 0);
    REQUIRE(!ts_node_is_null(field(system, "where")));
    REQUIRE(ts_node_named_child_count(field(system, "parameters")) == 3);
    TSNode handler = ts_node_named_child(field(system, "body"), 0);
    TSNode cancel = ts_node_named_child(field(handler, "body"), 1);
    REQUIRE(node_text(field(cancel, "duration"), "1.5"));
    return true;
}

TEST_CASE(language_obsolete_declaration_tokens_rejected)
{
    parse("library Old { function Compute() { return 1 } }");
    REQUIRE(original->diagnostic_count > 0);
    parse("behavior Old { on Tick { match (1) { case 1 { return } } } }");
    REQUIRE(original->diagnostic_count > 0);
    return true;
}

TEST_CASE(language_expressions_precedence_and_continuation)
{
    parse(
        "behavior Expressions {\n"
        "product := 1 + 2 * 3\n"
        "difference := 9 - 3 - 1\n"
        "vector := {x: 1, y: 2}\n"
        "items := [1,\n2, 3,]\n"
        "selected := items[1]\n"
        "fallback_value := fallback(default: 0)\n"
        "on Begin {\n"
        "computed := 1 // newline continuation\n+ 2\n"
        "other := 3 + /* operator continuation */\n4\n"
        "consume(\nvector.x,\nselected\n)\n"
        "}\n}"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode body = declaration_body();
    TSNode product = field(ts_node_named_child(body, 0), "value");
    REQUIRE(node_text(field(product, "operator"), "+"));
    REQUIRE(node_text(field(field(product, "right"), "operator"), "*"));
    TSNode difference = field(ts_node_named_child(body, 1), "value");
    REQUIRE(node_text(field(difference, "right"), "1"));
    REQUIRE(node_text(field(field(difference, "left"), "operator"), "-"));
    parse(
        "behavior Calls { on Begin {\n"
        "await set_variable<\"flash\">(Value: bool = true);\n"
        "consume(Owner: other, Position: target.Position, Label: \"text\");\n"
        "consume(Value: convert<\"to_vec\">(component(\"Transform\")));\n"
        "} }"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode statements = field(ts_node_named_child(declaration_body(), 0), "body");
    TSNode waiting = ts_node_named_child(ts_node_named_child(statements, 0), 0);
    REQUIRE(!strcmp(ts_node_type(waiting), "await_expression"));
    TSNode call = field(waiting, "value");
    REQUIRE(!strcmp(ts_node_type(call), "call_expression"));
    REQUIRE(node_text(field(call, "function"), "set_variable"));
    TSNode arguments = field(call, "arguments");
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(arguments, 0)), "typed_argument"));
    call = ts_node_named_child(ts_node_named_child(statements, 1), 0);
    arguments = field(call, "arguments");
    REQUIRE(ts_node_named_child_count(arguments) == 3);
    const char *values[] = { "other", "target.Position", "\"text\"" };
    for (int i = 0; i < 3; i++) {
        TSNode argument = ts_node_named_child(arguments, i);
        REQUIRE(!strcmp(ts_node_type(argument), "named_argument"));
        REQUIRE(node_text(field(argument, "value"), values[i]));
    }
    call = ts_node_named_child(ts_node_named_child(statements, 2), 0);
    TSNode converted = field(ts_node_named_child(field(call, "arguments"), 0), "value");
    REQUIRE(!strcmp(ts_node_type(converted), "call_expression"));
    REQUIRE(node_text(field(converted, "function"), "convert"));
    REQUIRE(!ts_node_is_null(field(converted, "type_arguments")));
    return true;
}

TEST_CASE(language_invalid_source_has_positioned_diagnostics)
{
    const char *cases[] = {
        "behavior Broken { first := 1 second := 2 }",
        "behavior Broken { true := 1 }",
        "behavior Broken { on Begin { value := ; } }",
        "behavior Broken { on Begin { if true { }",
        "library Broken { MissingBody :: fn() -> () }",
        "behavior Broken { on Begin { nudge(&speed) } }",
    };
    for (int i = 0; i < 6; i++) {
        cleanup();
        original = script_parse(cases[i]);
        if (!original->diagnostic_count) {
            fprintf(stderr, "Unexpected valid syntax: %s\n", cases[i]);
        }
        REQUIRE(original->diagnostic_count > 0);
        REQUIRE(ts_node_has_error(ts_tree_root_node(original->tree)));
        script_range range = original->diagnostics[0].range;
        REQUIRE(range.start >= 0 && range.end >= range.start);
        REQUIRE(range.end <= (int)strlen(cases[i]));
        REQUIRE(range.line >= 1 && range.column >= 1);
    }
    cleanup();
    original = script_parse("behavior Broken { broken := ; later := 7 }");
    REQUIRE(original->diagnostic_count > 0);
    TSNode body = declaration_body();
    bool found = false;
    for (uint32_t i = 0; i < ts_node_named_child_count(body); i++) {
        TSNode declaration = ts_node_named_child(body, i);
        if (!strcmp(ts_node_type(declaration), "state_declaration") &&
            node_text(field(declaration, "name"), "later") &&
            node_text(field(declaration, "value"), "7")) {
            found = true;
        }
    }
    REQUIRE(found);
    return true;
}

TEST_CASE(language_incremental_snapshot_matches_fresh)
{
    original = script_parse("behavior Edit {\n// café\ncount := 1\n}\n");
    REQUIRE(original->diagnostic_count == 0);
    original_tree = ts_node_string(ts_tree_root_node(original->tree));
    int offset = (int)(strstr(original->text, "é") - original->text);
    edited = script_edit(original, offset, offset + (int)strlen("é"), "π\n// extra");
    REQUIRE(edited->diagnostic_count == 0);
    REQUIRE(!strcmp(edited->text, "behavior Edit {\n// cafπ\n// extra\ncount := 1\n}\n"));
    fresh = script_parse(edited->text);
    edited_tree = ts_node_string(ts_tree_root_node(edited->tree));
    fresh_tree = ts_node_string(ts_tree_root_node(fresh->tree));
    REQUIRE(!strcmp(edited_tree, fresh_tree));
    REQUIRE(fresh->diagnostic_count == edited->diagnostic_count);
    REQUIRE(!strcmp(original->text, "behavior Edit {\n// café\ncount := 1\n}\n"));
    free(fresh_tree);
    fresh_tree = ts_node_string(ts_tree_root_node(original->tree));
    REQUIRE(!strcmp(original_tree, fresh_tree));
    TSNode behavior = ts_node_named_child(ts_tree_root_node(edited->tree), 0);
    TSNode body = field(behavior, "body");
    TSNode declaration = ts_node_named_child(body, ts_node_named_child_count(body) - 1);
    script_range range = script_node_range(field(declaration, "value"));
    REQUIRE(range.line == 4 && range.column == 10);
    REQUIRE(range.start == (int)(strstr(edited->text, "1") - edited->text));
    return true;
}

TEST_CASE(language_incremental_errors_match_fresh)
{
    original = script_parse("behavior Edit { first := 1; later := 2 }");
    REQUIRE(original->diagnostic_count == 0);
    int start = (int)(strstr(original->text, "1") - original->text);
    edited = script_edit(original, start, start + 1, "");
    fresh = script_parse(edited->text);
    REQUIRE(edited->diagnostic_count > 0);
    REQUIRE(edited->diagnostic_count == fresh->diagnostic_count);
    edited_tree = ts_node_string(ts_tree_root_node(edited->tree));
    fresh_tree = ts_node_string(ts_tree_root_node(fresh->tree));
    REQUIRE(!strcmp(edited_tree, fresh_tree));
    for (int i = 0; i < edited->diagnostic_count; i++) {
        REQUIRE(
            edited->diagnostics[i].range.start == fresh->diagnostics[i].range.start
        );
        REQUIRE(edited->diagnostics[i].range.end == fresh->diagnostics[i].range.end);
        REQUIRE(!strcmp(edited->diagnostics[i].message, fresh->diagnostics[i].message));
    }
    REQUIRE(original->diagnostic_count == 0);
    return true;
}

static char *read_corpus_source(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || length > CORPUS_MAX_FILE_BYTES || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    char *text = malloc((size_t)length + 1);
    if (!text) {
        fclose(file);
        return NULL;
    }
    bool complete = fread(text, 1, (size_t)length, file) == (size_t)length;
    complete = complete && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (!complete || memchr(text, '\0', (size_t)length)) {
        free(text);
        return NULL;
    }
    text[length] = '\0';
    *size = (size_t)length;
    return text;
}

TEST_CASE(language_retained_corpus)
{
    manifest_file = fopen(corpus_manifest, "rb");
    REQUIRE(manifest_file != NULL);
    char path[CORPUS_MAX_PATH_BYTES];
    size_t total = 0;
    int files = 0, failures = 0;
    for (; files < CORPUS_MAX_FILES && fgets(path, sizeof path, manifest_file);
         files++) {
        size_t length = strlen(path);
        REQUIRE(length > 0 && path[length - 1] == '\n');
        path[--length] = '\0';
        if (length && path[length - 1] == '\r') {
            path[--length] = '\0';
        }
        REQUIRE(length > 0);
        size_t bytes = 0;
        corpus_text = read_corpus_source(path, &bytes);
        if (!corpus_text) {
            fprintf(stderr, "Cannot read bounded corpus source: %s\n", path);
        }
        REQUIRE(corpus_text != NULL);
        total += bytes;
        REQUIRE(total <= CORPUS_MAX_TOTAL_BYTES);
        original = script_parse(corpus_text);
        if (original->diagnostic_count) {
            failures++;
            fprintf(stderr, "Corpus syntax errors: %s\n", path);
            for (int i = 0; i < original->diagnostic_count && i < 8; i++) {
                fprintf(
                    stderr,
                    "  %d:%d %s\n",
                    original->diagnostics[i].range.line,
                    original->diagnostics[i].range.column,
                    original->diagnostics[i].message
                );
            }
        }
        if (corpus_format && !original->diagnostic_count) {
            char error[192];
            formatted_text = authoring_source_format(original, error, sizeof error);
            bool valid = formatted_text != NULL;
            if (valid) {
                fresh = script_parse(formatted_text);
                reformatted_text = authoring_source_format(fresh, error, sizeof error);
                valid = reformatted_text &&
                        !strcmp(formatted_text, reformatted_text) &&
                        !strcmp(original->text, corpus_text);
            }
            if (!valid) {
                failures++;
                fprintf(stderr, "Corpus formatting failed: %s: %s\n", path, error[0] ? error : "formatting is not idempotent");
            }
            script_syntax_free(fresh);
            fresh = NULL;
            free(formatted_text);
            free(reformatted_text);
            formatted_text = reformatted_text = NULL;
        }
        script_syntax_free(original);
        original = NULL;
        free(corpus_text);
        corpus_text = NULL;
    }
    REQUIRE(files > 0);
    REQUIRE(!ferror(manifest_file));
    REQUIRE(fgetc(manifest_file) == EOF);
    printf(
        "Retained %s corpus: %d files, %zu bytes, %d failures\n",
        corpus_format ? "formatter" : "syntax",
        files,
        total,
        failures
    );
    REQUIRE(failures == 0);
    return true;
}

void test_language_syntax()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(language_declarations_actions_and_bindings);
    RUN_TEST_CASE(language_callable_types_entries_and_results);
    RUN_TEST_CASE(language_system_queries_and_control_flow);
    RUN_TEST_CASE(language_definitions_unions_and_type_patterns);
    RUN_TEST_CASE(language_query_header_and_cancel_deadline);
    RUN_TEST_CASE(language_obsolete_declaration_tokens_rejected);
    RUN_TEST_CASE(language_expressions_precedence_and_continuation);
    RUN_TEST_CASE(language_invalid_source_has_positioned_diagnostics);
    RUN_TEST_CASE(language_incremental_snapshot_matches_fresh);
    RUN_TEST_CASE(language_incremental_errors_match_fresh);
    pu_clear_setup();
}

void test_language_corpus(const char *manifest)
{
    corpus_manifest = manifest;
    corpus_format = false;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(language_retained_corpus);
    pu_clear_setup();
}

void test_language_format_corpus(const char *manifest)
{
    corpus_manifest = manifest;
    corpus_format = true;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(language_retained_corpus);
    pu_clear_setup();
}
