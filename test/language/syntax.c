#include "language/syntax.h"

#include "syntax_corpus.h"
#include "test/parallel.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local script_syntax *original, *edited, *fresh;
static _Thread_local char *original_tree, *edited_tree, *fresh_tree;
static _Thread_local FILE *manifest_file;
static _Thread_local char *corpus_text;
static _Thread_local const char *corpus_manifest;

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
            pu_errors(),
            "syntax %d:%d: %s\n",
            original->diagnostics[i].range.line,
            original->diagnostics[i].range.column,
            original->diagnostics[i].message
        );
    }
}

static TSNode find_node(TSNode root, const char *kind, const char *text)
{
    if (!strcmp(ts_node_type(root), kind) && node_text(root, text)) {
        return root;
    }
    for (uint32_t i = 0; i < ts_node_named_child_count(root); i++) {
        TSNode found = find_node(ts_node_named_child(root, i), kind, text);
        if (!ts_node_is_null(found)) {
            return found;
        }
    }
    return (TSNode){ 0 };
}

TEST_CASE(language_managed_pointer_syntax_preserves_xor)
{
    parse(
        "Demo :: behavior { a:^int[]; b:^int?; c:^(int[]); d:^(int?); "
        "on Tick :: () { p:=&value; x:=p^ + 1; y:=p ^ +q; z:=p ^ q; "
        "a:=p^^; b:=p^.field; c:=p^[i]; d:=&p^; F(p); "
        "last:=p^\nnext:=1; } }"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode root = ts_tree_root_node(original->tree);
    TSNode pointers = find_node(root, "type", "^int[]");
    TSNode pointer = find_node(pointers, "pointer_type", "^int");
    REQUIRE(!ts_node_is_null(pointer));
    REQUIRE(node_text(field(pointer, "target"), "int"));
    pointer = find_node(root, "pointer_type", "^(int[])");
    REQUIRE(node_text(field(pointer, "target"), "(int[])"));
    REQUIRE(!ts_node_is_null(find_node(root, "parenthesized_type", "(int?)")));
    REQUIRE(!ts_node_is_null(find_node(root, "type", "^int?")));
    REQUIRE(!ts_node_is_null(find_node(root, "address_expression", "&value")));
    REQUIRE(!ts_node_is_null(find_node(root, "dereference_expression", "p^^")));
    TSNode sum = find_node(root, "binary_expression", "p^ + 1");
    REQUIRE(node_text(field(sum, "operator"), "+"));
    REQUIRE(!strcmp(ts_node_type(field(sum, "left")), "dereference_expression"));
    TSNode xor = find_node(root, "binary_expression", "p ^ +q");
    REQUIRE(node_text(field(xor, "operator"), "^"));
    REQUIRE(!strcmp(ts_node_type(field(xor, "right")), "unary_expression"));
    REQUIRE(node_text(field(find_node(root, "binary_expression", "p ^ q"), "operator"), "^"));
    REQUIRE(!ts_node_is_null(find_node(root, "member_expression", "p^.field")));
    REQUIRE(!ts_node_is_null(find_node(root, "index_expression", "p^[i]")));
    REQUIRE(!ts_node_is_null(find_node(root, "address_expression", "&p^")));
    return true;
}

TEST_CASE(language_declarations_actions_and_bindings)
{
    parse(
        "Demo :: behavior {\n"
        "count: retain int = 0; enabled: retain = true;\n"
        "target := self.Velocity.Parent;\n"
        "action pending = delay(Duration: 1);\n"
        "event changed = self.Health.CurrentChanged;\n"
        "bind self.ParticleEmitter.Active = enabled;\n"
        "on Tick :: (dt: number) { count += int(dt); }\n"
        "after pending.Completed { destroy(self); }\n"
        "events Other { on Begin :: () { pending.start(); } }\n"
        "}"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode body = declaration_body();
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(body, 3)), "action_declaration"));
    TSNode handler = ts_node_named_child(body, 6);
    REQUIRE(!strcmp(ts_node_type(handler), "handler"));
    REQUIRE(node_text(field(handler, "kind"), "on"));
    REQUIRE(node_text(field(handler, "binding"), "::"));
    return true;
}

/* `on X :: (a: T)` names payload fields: the handler keeps its kind, name,
 * parameters and body, and marks the binding. */
TEST_CASE(language_handlers_name_payload_fields)
{
    parse("Demo :: behavior { on Tick :: (dt: s) { } on Begin :: () { } }");
    REQUIRE(original->diagnostic_count == 0);
    TSNode body = declaration_body();
    for (uint32_t i = 0; i < 2; i++) {
        TSNode handler = ts_node_named_child(body, i);
        REQUIRE(!strcmp(ts_node_type(handler), "handler"));
        REQUIRE(node_text(field(handler, "kind"), "on"));
        REQUIRE(node_text(field(handler, "binding"), "::"));
        REQUIRE(
            !ts_node_is_null(field(handler, "parameters")) &&
            !ts_node_is_null(field(handler, "body"))
        );
    }
    TSNode parameter = ts_node_named_child(field(ts_node_named_child(body, 0), "parameters"), 0);
    REQUIRE(
        node_text(field(parameter, "name"), "dt") &&
        node_text(field(parameter, "type"), "s")
    );
    parse("Demo :: behavior { after spawned :: () { } }");
    REQUIRE(original->diagnostic_count > 0);
    /* Version 30 removed the positional, aliased and bare `on` forms and the
     * `after` alias; `after` keeps its results list. */
    const char *removed[] = {
        "Demo :: behavior { on Tick { } }",
        "Demo :: behavior { on Tick(dt) { } }",
        "Demo :: behavior { on Tick as tick { } }",
        "Demo :: behavior { action go = Go(); after go as done { } }",
    };
    for (size_t i = 0; i < sizeof removed / sizeof *removed; i++) {
        parse(removed[i]);
        REQUIRE(original->diagnostic_count > 0);
    }
    parse("Demo :: behavior { action go = delay(Duration: 1); after go(seconds) { } }");
    REQUIRE(original->diagnostic_count == 0);
    return true;
}
/* Systems always receive dt and reject the former event/query clauses. */
TEST_CASE(language_systems_use_flat_queries_and_implicit_step)
{
    parse("Motion :: system(p: ^Position, if p.x > 0) { p.x += dt }");
    REQUIRE(original->diagnostic_count == 0);
    TSNode system = ts_node_named_child(ts_tree_root_node(original->tree), 0);
    REQUIRE(!strcmp(ts_node_type(field(system, "parameters")), "query_list"));
    REQUIRE(ts_node_is_null(field(system, "event")));
    static const char *const removed[] = {
        "Motion :: system(p: Position) on Tick :: (dt: s) {}",
        "Motion :: system(p: Position) without Frozen {}",
        "Motion :: system(p: Position) where p.x > 0 {}",
        "Motion :: system(p: Position) join target in self.Targets {}",
        "Demo :: behavior { query components(); }",
        "Motion :: system(p: Position split self) {}",
        ("Demo :: behavior { on Tick :: () { rows := query(p: Position split "
         "self); } }"),
    };
    for (size_t i = 0; i < sizeof removed / sizeof *removed; i++) {
        parse(removed[i]);
        REQUIRE(original->diagnostic_count > 0);
    }
    return true;
}
TEST_CASE(language_retained_binding_syntax)
{
    parse(
        "Counters :: system() { visits: retain int = 0; total: retain "
        "= 0; "
        "pending: retain task; Run :: () { calls: retain = 0; "
        "shared_calls: retain int = 0; } }"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode body = declaration_body();
    TSNode visits = ts_node_named_child(body, 0);
    REQUIRE(!strcmp(ts_node_type(visits), "state_declaration"));
    REQUIRE(node_text(field(visits, "name"), "visits"));
    REQUIRE(node_text(field(visits, "storage"), "retain"));
    REQUIRE(node_text(field(visits, "type"), "int"));
    REQUIRE(node_text(field(visits, "value"), "0"));
    REQUIRE(ts_node_is_null(field(visits, "owner")));
    TSNode total = ts_node_named_child(body, 1);
    REQUIRE(ts_node_is_null(field(total, "owner")));
    REQUIRE(node_text(field(total, "storage"), "retain"));
    REQUIRE(ts_node_is_null(field(total, "type")));
    TSNode callable = ts_node_named_child(body, 3);
    TSNode calls = ts_node_named_child(field(callable, "body"), 0);
    REQUIRE(!strcmp(ts_node_type(calls), "state_declaration"));
    REQUIRE(node_text(field(calls, "storage"), "retain"));
    return true;
}

TEST_CASE(language_unified_declarations_and_generic_map_shapes)
{
    parse(
        "Types :: library { Vector :: struct($Element: Type, $Size: int) { "
        "items: Element[]; } Identity :: (value: $T) -> T => { return value; } "
        "Product :: (left: $L, right: $R) -> ProductType(L, R) { return left; "
        "} "
        "Composed :: (value: ^(Vector(number, 2)[])) -> Vector(number, 2)? { "
        "return nil; } "
        "Use :: () { values: [string: int] = [\"Ada\": 1, key: 2, 3: 4]; "
        "for key, value in values { consume(key, value); } "
        "for item in items { consume(item); } "
        "delete values[key]; gone := key not in values; empty: [int: bool] = "
        "[:]; } }"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode body = declaration_body();
    TSNode record = ts_node_named_child(body, 0);
    REQUIRE(!strcmp(ts_node_type(record), "record_declaration"));
    TSNode parameter = ts_node_named_child(field(record, "parameters"), 0);
    REQUIRE(node_text(field(parameter, "comptime"), "$"));
    REQUIRE(node_text(field(parameter, "name"), "Element"));
    TSNode identity = ts_node_named_child(body, 1);
    parameter = ts_node_named_child(field(identity, "parameters"), 0);
    TSNode binder = field(field(parameter, "type"), "name");
    REQUIRE(!strcmp(ts_node_type(binder), "type_binder"));
    REQUIRE(node_text(field(binder, "name"), "T"));
    TSNode product = ts_node_named_child(body, 2);
    TSNode results = field(product, "results");
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(results, 0)), "type_application"));
    TSNode composed = ts_node_named_child(body, 3);
    parameter = ts_node_named_child(field(composed, "parameters"), 0);
    TSNode composed_type = field(parameter, "type");
    TSNode composed_pointer = ts_node_named_child(composed_type, 0);
    REQUIRE(!strcmp(ts_node_type(composed_pointer), "pointer_type"));
    REQUIRE(node_text(composed_type, "^(Vector(number, 2)[])"));
    REQUIRE(!ts_node_is_null(
        find_node(composed_pointer, "type_application", "Vector(number, 2)")
    ));
    REQUIRE(node_text(field(composed, "results"), "Vector(number, 2)?"));
    TSNode use = ts_node_named_child(body, 4);
    TSNode statements = field(use, "body");
    TSNode value = field(ts_node_named_child(statements, 0), "value");
    TSNode map = field(field(ts_node_named_child(statements, 0), "type"), "map");
    REQUIRE(node_text(field(map, "key"), "string") && node_text(field(map, "value"), "int"));
    REQUIRE(!strcmp(ts_node_type(value), "map_expression"));
    REQUIRE(node_text(field(ts_node_named_child(value, 0), "key"), "\"Ada\""));
    REQUIRE(node_text(field(ts_node_named_child(value, 1), "key"), "key"));
    REQUIRE(node_text(field(ts_node_named_child(value, 2), "value"), "4"));
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(statements, 3)), "delete_statement"));
    TSNode gone = field(ts_node_named_child(statements, 4), "value");
    REQUIRE(!strcmp(ts_node_type(field(gone, "operator")), "not_in"));
    TSNode empty = field(ts_node_named_child(statements, 5), "value");
    REQUIRE(!strcmp(ts_node_type(empty), "map_expression") && !ts_node_named_child_count(empty));
    TSNode loop = ts_node_named_child(statements, 1);
    REQUIRE(!strcmp(ts_node_type(loop), "for_statement"));
    REQUIRE(node_text(field(loop, "key"), "key"));
    REQUIRE(node_text(field(loop, "name"), "value"));
    loop = ts_node_named_child(statements, 2);
    REQUIRE(node_text(field(loop, "name"), "item"));
    return true;
}

TEST_CASE(language_authored_asset_declarations)
{
    parse(
        "Boss :: prototype(Enemy, Armored) { identity: \"stable\", "
        "links: {Targets: child.enemy}, Health {current: 100, maximum: 100} }"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode prototype = ts_node_named_child(ts_tree_root_node(original->tree), 0);
    REQUIRE(!strcmp(ts_node_type(prototype), "prototype_declaration"));
    REQUIRE(node_text(field(prototype, "name"), "Boss"));
    REQUIRE(ts_node_named_child_count(field(prototype, "bases")) == 2);
    TSNode component = ts_node_named_child(field(prototype, "body"), 2);
    REQUIRE(!strcmp(ts_node_type(component), "prototype_component"));
    REQUIRE(node_text(field(component, "name"), "Health"));
    parse("Targets :: relationship {exclusive: true, traversable: false, on_delete_target: Remove}");
    REQUIRE(original->diagnostic_count == 0);
    TSNode relationship = ts_node_named_child(ts_tree_root_node(original->tree), 0);
    REQUIRE(!strcmp(ts_node_type(relationship), "relationship_declaration"));
    REQUIRE(node_text(field(relationship, "name"), "Targets"));
    return true;
}

TEST_CASE(language_obsolete_symbol_spellings_rejected)
{
    const char *invalid[] = {
        "behavior Old {}",
        "Old :: system {}",
        "Old :: system query p: Position {}",
        "Old :: library { Run :: fn() {} }",
        "Old :: library { Run :: pure () {} }",
        "Old :: library { Run :: (int value) {} }",
        "Old :: behavior { int value = 0; }",
        "Old :: library { \"Run\" :: () {} }",
        "Old :: behavior { \"count\" := 0; }",
        "Old :: behavior { on Tick :: () { foreach value in [] {} } }",
        "Old :: behavior { on Tick :: () { \"done\": goto \"done\"; } }",
        "Old :: library { Run :: () { return.\"Done\"; } }",
        "Old :: library { Run :: (value: struct<\"Position\">) {} }",
        "Old :: library { Run :: () { \"Call\"(); } }",
        "Old :: behavior { on Tick :: () { self.\"Health\" = 0; } }",
        "Old :: behavior { on Tick :: () { call(\"Value\": 0); } }",
        "Old :: library { return :: () {} }",
        "Old :: behavior { on Tick :: () { for ref item in items {} } }",
        "Old :: behavior { on Tick :: () { for item: ref in items {} } }",
        "Old :: library { Run :: (value: struct<Position>) {} }",
        "Old :: library { Run :: (value: task<int>) {} }",
        "Old :: library { Run :: (value: array<int>) {} }",
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        parse(invalid[i]);
        REQUIRE(original->diagnostic_count > 0);
    }
    return true;
}

TEST_CASE(language_keywords_are_contextual_data_member_names)
{
    parse(
        "Use :: library { Identity :: (value: int) -> int { return value; } "
        "join :: () {} without :: () {} where :: () {} "
        "Names :: () { join(); without(); where(); } "
        "Run :: () { component := 1; from := 2; split := 3; join := 4; "
        "unordered := 5; not := 6; struct := 7; without := 8; where := 9; "
        "Identity(component); Identity(from); Identity(split); Identity(join); "
        "Identity(unordered); Identity(not); Identity(struct); "
        "Identity(without); Identity(where); } }"
    );
    REQUIRE(!original->diagnostic_count);
    cleanup();
    parse(
        "Rows :: library { Row :: struct { prototype: string; system: int; if: "
        "bool; } "
        "Make :: () -> Row { return {prototype: \"Drone\", system: 1, if: "
        "true}; } "
        "Read :: (row: Row) -> string { return row.prototype; } }"
    );
    REQUIRE(!original->diagnostic_count);
    cleanup();
    parse("Metadata :: component { prototype: string; system: int; if: bool; }");
    REQUIRE(!original->diagnostic_count);
    return true;
}

TEST_CASE(language_not_callable_and_unary_spellings_disambiguate)
{
    parse(
        "Negation :: library { Evaluate :: (enabled: bool) -> bool { "
        "value: bool = not(Value: not(Value: enabled)); return not(value); } "
        "Unary :: (enabled: bool) -> bool { return not enabled; } "
        "not :: (Value: bool) -> bool { return Value; } "
        "Narrow :: (value: bool) { if value is not Cancelled {} } }"
    );
    REQUIRE(!original->diagnostic_count);
    TSNode body = declaration_body();
    TSNode evaluate = ts_node_named_child(body, 0);
    TSNode statements = field(evaluate, "body");
    TSNode call = field(ts_node_named_child(statements, 0), "value");
    REQUIRE(!strcmp(ts_node_type(call), "call_expression"));
    REQUIRE(node_text(field(call, "function"), "not"));
    TSNode arguments = field(call, "arguments");
    REQUIRE(
        !strcmp(ts_node_type(field(ts_node_named_child(arguments, 0), "value")), "call_expression")
    );
    TSNode unary = ts_node_named_child(body, 1);
    TSNode returned = ts_node_named_child(field(unary, "body"), 0);
    REQUIRE(!strcmp(ts_node_type(field(returned, "value")), "unary_expression"));
    TSNode narrow = ts_node_named_child(body, 3);
    TSNode conditional = ts_node_named_child(field(narrow, "body"), 0);
    TSNode pattern = field(conditional, "condition");
    REQUIRE(!strcmp(ts_node_type(pattern), "is_expression"));
    REQUIRE(node_text(field(pattern, "negated"), "not"));
    REQUIRE(node_text(field(pattern, "type"), "Cancelled"));
    REQUIRE(ts_node_is_null(field(pattern, "binding")));
    return true;
}

TEST_CASE(language_retained_bindings_reject_prefixes_and_value_types)
{
    const char *invalid[] = {
        "Old :: system() { count: retain(system) int = 0; }",
        "Old :: behavior { count: retain(system) = 0; }",
        "Old :: behavior { state count: int = 0; }",
        "Old :: system() { shared count := 0; }",
        "Old :: behavior { Run :: () { state count := 0; } }",
        "Bad :: behavior { count: retain; }",
        "Bad :: behavior { count: retain(owner) int = 0; }",
        "Bad :: library { Run :: (count: retain int) {} }",
        "Bad :: library { Run :: () -> retain int {} }",
        "Bad :: behavior { counts: [string: retain int] = [:]; }",
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        parse(invalid[i]);
        REQUIRE(original->diagnostic_count > 0);
    }
    return true;
}

TEST_CASE(language_callable_types_entries_and_results)
{
    parse(
        "Helpers :: library {\n"
        "Identity :: (source: ^int, item: $T = 1) "
        "-> (Result: T) { return item }\n"
        "Apply :: (amount: int) -> () { entry Run { return(); } exit "
        "Done; }\n"
        "macro Route(key: name) -> (Success: bool) { return.Success(true); }\n"
        "converted := to_real(Value: int = 3);\n"
        "chosen := Identity(source: ^int from state.count, "
        "item: 2);\n"
        "}"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode callable = ts_node_named_child(declaration_body(), 0);
    REQUIRE(!strcmp(ts_node_type(callable), "function_declaration"));
    REQUIRE(node_text(field(callable, "name"), "Identity"));
    REQUIRE(ts_node_is_null(field(callable, "type_parameters")));
    TSNode parameters = field(callable, "parameters");
    REQUIRE(ts_node_named_child_count(parameters) == 2);
    REQUIRE(node_text(field(ts_node_named_child(parameters, 0), "name"), "source"));
    cleanup();
    parse(
        "Use :: library { Run :: () { result: Units.ProductType(A, B) = {}; "
        "speed: Units.Quantity(f64, 1, -1) = {}; return speed; } }"
    );
    REQUIRE(!original->diagnostic_count);
    TSNode body = field(ts_node_named_child(declaration_body(), 0), "body");
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(body, 0)), "local_declaration"));
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(body, 1)), "local_declaration"));
    return true;
}

TEST_CASE(language_callable_arguments_keep_declarations_and_expression_boundaries)
{
    parse(
        "package callbacks\n"
        "pub Identity :: (value: $T) -> T { return value; }\n"
        "Apply :: (value: int, transform: fn(value: int) -> int) -> int {\n"
        "    return transform(value);\n"
        "}\n"
        "Main :: () -> int {\n"
        "    result := Apply(4, (value: int) => value * 2 + 1);\n"
        "    block := Apply(4, fn(value: int) -> int { return value + 1; });\n"
        "    return Apply(transform: (value: int) => value < 5, value: 4);\n"
        "}\n"
    );
    REQUIRE(!original->diagnostic_count);
    TSNode root = ts_tree_root_node(original->tree);
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(root, 1)), "function_declaration"));
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(root, 2)), "function_declaration"));
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(root, 3)), "function_declaration"));
    REQUIRE(!ts_node_is_null(find_node(root, "callable_type", "fn(value: int) -> int")));
    TSNode arrow = find_node(root, "lambda_expression", "(value: int) => value * 2 + 1");
    REQUIRE(!ts_node_is_null(arrow));
    TSNode body = field(arrow, "body");
    REQUIRE(!strcmp(ts_node_type(body), "callable_body"));
    TSNode returned = ts_node_named_child(body, 0);
    REQUIRE(!strcmp(ts_node_type(returned), "return_statement"));
    REQUIRE(node_text(field(field(returned, "value"), "operator"), "+"));
    REQUIRE(node_text(field(field(field(returned, "value"), "left"), "operator"), "*"));
    TSNode named = find_node(root, "named_argument", "transform: (value: int) => value < 5");
    REQUIRE(!ts_node_is_null(named));
    REQUIRE(!strcmp(ts_node_type(field(named, "value")), "lambda_expression"));
    TSNode next = ts_node_next_named_sibling(named);
    REQUIRE(node_text(next, "value: 4"));
    REQUIRE(!ts_node_is_null(
        find_node(root, "lambda_expression", "fn(value: int) -> int { return value + 1; }")
    ));
    return true;
}

TEST_CASE(language_callable_arguments_preserve_view_and_range_syntax)
{
    parse(
        "package callbacks\n"
        "Read :: (window: view int[], accept: fn(value: int) -> bool) -> bool "
        "{\n"
        "    return accept(window[0]);\n"
        "}\n"
        "Use :: (owner: ^(int[])) {\n"
        "    consume(owner.view(0..<2), (value: int) => value > 0);\n"
        "}\n"
    );
    REQUIRE(!original->diagnostic_count);
    TSNode root = ts_tree_root_node(original->tree);
    TSNode read = ts_node_named_child(root, 1);
    REQUIRE(!strcmp(ts_node_type(read), "function_declaration"));
    TSNode window = ts_node_named_child(field(read, "parameters"), 0);
    REQUIRE(node_text(field(window, "type"), "view int[]"));
    TSNode range = find_node(root, "range_expression", "0..<2");
    REQUIRE(!ts_node_is_null(range));
    REQUIRE(node_text(field(range, "start"), "0"));
    REQUIRE(node_text(field(range, "end"), "2"));
    TSNode method = find_node(root, "member_expression", "owner.view");
    REQUIRE(!ts_node_is_null(method));
    REQUIRE(node_text(field(method, "member"), "view"));
    REQUIRE(!ts_node_is_null(find_node(root, "lambda_expression", "(value: int) => value > 0")));
    return true;
}

TEST_CASE(language_system_queries_and_control_flow)
{
    parse(
        "Motion :: system(position: ^Transform, velocity: Velocity, damping: "
        "^Igniter?, if !self.Collider && !self.Body) {\n"
        "matches := query(component: Transform);\n"
        "position += velocity * dt;\n"
        "if damping { damping.heat = 0; } else if dt > 0 { position = "
        "velocity; }\n"
        "while dt > 0 { break; }\n"
        "for (i in 0..3) { continue; }\n"
        "for (item in [1, 2]) { consume(item); }\n"
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
    REQUIRE(ts_node_named_child_count(field(system, "parameters")) == 4);
    TSNode body = field(system, "body");
    REQUIRE(!strcmp(ts_node_type(field(ts_node_named_child(body, 0), "value")), "query_expression"));
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(body, 4)), "for_statement"));
    return true;
}

TEST_CASE(language_nested_typed_records_and_explicit_labels)
{
    parse(
        "Parser :: library { Package :: struct { directory: string; name: "
        "string; "
        "member_start: int; member_count: int; }\n"
        "Main :: (enabled: bool) -> int {\n"
        " result: int = 0;\n"
        " if enabled {\n"
        "  item: Package = {directory: \"app\", name: \"sample\",\n"
        "                   member_start: 3, member_count: 4};\n"
        "  result = item.member_start + item.member_count;\n"
        " }\n"
        " if true {\n"
        " ordinary:\n"
        " result = result + 1;\n"
        " compound: result += 1;\n"
        " }\n"
        " return result;\n"
        "} }"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode library = ts_node_named_child(ts_tree_root_node(original->tree), 0);
    TSNode body = field(library, "body");
    TSNode function = ts_node_named_child(body, 1);
    body = field(function, "body");
    REQUIRE(ts_node_named_child_count(body) == 4);
    TSNode branch = ts_node_named_child(body, 1);
    TSNode nested = field(branch, "consequence");
    REQUIRE(ts_node_named_child_count(nested) == 2);
    TSNode item = ts_node_named_child(nested, 0);
    REQUIRE(!strcmp(ts_node_type(item), "local_declaration"));
    REQUIRE(node_text(field(item, "name"), "item"));
    REQUIRE(node_text(field(item, "type"), "Package"));
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(nested, 1)), "assignment"));
    TSNode labels = field(ts_node_named_child(body, 2), "consequence");
    REQUIRE(ts_node_named_child_count(labels) == 4);
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(labels, 0)), "label_statement"));
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(labels, 1)), "assignment"));
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(labels, 2)), "label_statement"));
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(labels, 3)), "assignment"));
    parse("State :: system() { inferred: retain = 1; typed: retain int = 2; }");
    REQUIRE(original->diagnostic_count == 0);
    body = declaration_body();
    REQUIRE(ts_node_named_child_count(body) == 2);
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(body, 0)), "state_declaration"));
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(body, 1)), "state_declaration"));
    return true;
}

TEST_CASE(language_definitions_unions_and_type_patterns)
{
    parse(
        "Combat :: library {\n"
        "BaseDamage :: 12\n"
        "DamageFor :: (level: int) -> number { return level * 2 }\n"
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

TEST_CASE(language_type_attributes_are_declaration_children)
{
    parse(
        "package facts\n"
        "@error Failure :: struct { code: int; }\n"
        "@error pub PublicFailure :: struct { code: int; }\n"
        "@error Cause :: union { Missing | Invalid(code: int) }\n"
        "@error pub PublicCause :: union { Missing | Invalid(code: int) }\n"
        "@test pub Works :: () {}\n"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode root = ts_tree_root_node(original->tree);
    const char *kinds[] = {
        "record_declaration", "record_declaration",   "union_declaration",
        "union_declaration",  "function_declaration",
    };
    const char *names[] = { "Failure", "PublicFailure", "Cause", "PublicCause", "Works" };
    REQUIRE(ts_node_named_child_count(root) == 6);
    for (uint32_t i = 0; i < 5; i++) {
        TSNode declaration = ts_node_named_child(root, i + 1);
        REQUIRE(!strcmp(ts_node_type(declaration), kinds[i]));
        REQUIRE(node_text(field(declaration, "name"), names[i]));
        TSNode attribute = field(declaration, "attribute");
        REQUIRE(!strcmp(ts_node_type(attribute), "attribute"));
        REQUIRE(ts_node_eq(ts_node_parent(attribute), declaration));
        REQUIRE(node_text(field(attribute, "name"), i == 4 ? "test" : "error"));
        REQUIRE(
            node_text(field(declaration, "visibility"), "pub") ==
            (i == 1 || i == 3 || i == 4)
        );
    }
    return true;
}

TEST_CASE(language_query_header_and_cancel_deadline)
{
    parse(
        "Home :: system(position: Transform, aim: Transform from Targets, "
        "parent: Velocity? from ancestor(ChildOf), "
        "if position and (aim or not parent)) {\n"
        "on Tick :: () { charge := Charge(); cancel charge after 1.5 }\n"
        "}"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode system = ts_node_named_child(ts_tree_root_node(original->tree), 0);
    REQUIRE(ts_node_named_child_count(field(system, "parameters")) == 4);
    TSNode handler = ts_node_named_child(field(system, "body"), 0);
    TSNode cancel = ts_node_named_child(field(handler, "body"), 1);
    REQUIRE(node_text(field(cancel, "duration"), "1.5"));
    return true;
}

TEST_CASE(language_query_generators_preserve_authored_order)
{
    parse(
        "Track :: system(p: Position, target: entity in self.Targets, "
        "next in target.Targets, aim: ^Position? from next, "
        "if aim and aim.x > p.x and changed(p), "
        "amount := p.x * 2, total: int = amount + 1,) {}"
    );
    REQUIRE(original->diagnostic_count == 0);
    TSNode system = ts_node_named_child(ts_tree_root_node(original->tree), 0);
    TSNode list = field(system, "parameters");
    REQUIRE(ts_node_named_child_count(list) == 7);
    TSNode first = ts_node_named_child(list, 1);
    TSNode second = ts_node_named_child(list, 2);
    REQUIRE(!strcmp(ts_node_type(first), "query_generator"));
    REQUIRE(node_text(field(first, "name"), "target"));
    REQUIRE(node_text(field(first, "type"), "entity"));
    REQUIRE(node_text(field(first, "source"), "self.Targets"));
    REQUIRE(node_text(field(second, "name"), "next"));
    REQUIRE(ts_node_is_null(field(second, "type")));
    REQUIRE(node_text(field(second, "source"), "target.Targets"));
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(list, 3)), "parameter"));
    REQUIRE(
        node_text(field(ts_node_named_child(list, 4), "condition"), "aim and aim.x > p.x and changed(p)")
    );
    REQUIRE(!strcmp(ts_node_type(ts_node_named_child(list, 5)), "query_binding"));
    REQUIRE(ts_node_is_null(field(ts_node_named_child(list, 5), "type")));
    REQUIRE(node_text(field(ts_node_named_child(list, 6), "type"), "int"));
    return true;
}

TEST_CASE(language_query_expressions_share_system_terms)
{
    parse(
        "Demo :: behavior { on Tick :: () { "
        "matches := query(damage: Damage, target in self.Targets, "
        "health: ^Health from target, if health.value > 0, "
        "amount := damage.amount,); for row in matches { "
        "row.health.value -= row.amount; } } }"
    );
    REQUIRE(!original->diagnostic_count);
    TSNode root = ts_tree_root_node(original->tree);
    TSNode query = find_node(
        root,
        "query_expression",
        "query(damage: Damage, target in self.Targets, "
        "health: ^Health from target, if health.value > 0, "
        "amount := damage.amount,)"
    );
    REQUIRE(!ts_node_is_null(query));
    REQUIRE(ts_node_named_child_count(field(query, "parameters")) == 5);
    return true;
}

/* `ref` is an ordinary name; pointers replaced reference arguments. */
TEST_CASE(language_ref_arguments_are_not_syntax)
{
    static const char *invalid[] = {
        "R :: library { F :: () { G(ref y); } }",
        "R :: library { F :: () { G(x: ref y); } }",
        "R :: library { F :: () { G(x: int from ref y = z); } }",
        "R :: library { F :: () { G(x: int split ref y = z); } }",
    };
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++) {
        parse(invalid[i]);
        REQUIRE(original->diagnostic_count);
    }
    return true;
}

TEST_CASE(language_obsolete_declaration_tokens_rejected)
{
    parse("Old :: library { function Compute() { return 1 } }");
    REQUIRE(original->diagnostic_count > 0);
    parse("Old :: behavior { on Tick :: () { match (1) { case 1 { return } } } }");
    REQUIRE(original->diagnostic_count > 0);
    return true;
}

TEST_CASE(language_expressions_precedence_and_continuation)
{
    parse(syntax_corpus_cases[SYNTAX_CORPUS_PRECEDENCE].text);
    REQUIRE(original->diagnostic_count == 0);
    TSNode body = declaration_body();
    TSNode product = field(ts_node_named_child(body, 0), "value");
    REQUIRE(node_text(field(product, "operator"), "+"));
    REQUIRE(node_text(field(field(product, "right"), "operator"), "*"));
    TSNode difference = field(ts_node_named_child(body, 1), "value");
    REQUIRE(node_text(field(difference, "right"), "1"));
    REQUIRE(node_text(field(field(difference, "left"), "operator"), "-"));
    /* `??` groups to the right and binds above comparison. */
    parse("Expressions :: behavior { chosen := a ?? b ?? c + 1 < d }");
    REQUIRE(original->diagnostic_count == 0);
    TSNode comparison = field(ts_node_named_child(declaration_body(), 0), "value");
    REQUIRE(node_text(field(comparison, "operator"), "<"));
    TSNode coalesce = field(comparison, "left");
    REQUIRE(
        node_text(field(coalesce, "operator"), "??") &&
        node_text(field(coalesce, "left"), "a")
    );
    REQUIRE(node_text(field(coalesce, "right"), "b ?? c + 1"));
    parse(syntax_corpus_cases[SYNTAX_CORPUS_CALLS].text);
    REQUIRE(original->diagnostic_count == 0);
    TSNode statements = field(ts_node_named_child(declaration_body(), 0), "body");
    TSNode waiting = ts_node_named_child(ts_node_named_child(statements, 0), 0);
    REQUIRE(!strcmp(ts_node_type(waiting), "await_expression"));
    TSNode call = field(waiting, "value");
    REQUIRE(!strcmp(ts_node_type(call), "emit_expression"));
    REQUIRE(node_text(field(call, "event"), "self.Flash"));
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
    REQUIRE(node_text(field(converted, "function"), "to_vec"));
    TSNode operand = ts_node_named_child(field(converted, "arguments"), 0);
    REQUIRE(!strcmp(ts_node_type(operand), "call_expression"));
    REQUIRE(node_text(field(operand, "function"), "component"));
    REQUIRE(node_text(ts_node_named_child(field(operand, "arguments"), 0), "Transform"));
    return true;
}

TEST_CASE(language_invalid_source_has_positioned_diagnostics)
{
    for (int i = 0; i < SYNTAX_CORPUS_INVALID_COUNT; i++) {
        cleanup();
        original = script_parse(syntax_corpus_cases[i].text);
        if (!original->diagnostic_count) {
            fprintf(
                pu_errors(),
                "Unexpected valid syntax: %s\n",
                syntax_corpus_cases[i].text
            );
        }
        REQUIRE(original->diagnostic_count > 0);
        REQUIRE(ts_node_has_error(ts_tree_root_node(original->tree)));
        script_range range = original->diagnostics[0].range;
        REQUIRE(range.start >= 0 && range.end >= range.start);
        REQUIRE(range.end <= (int)strlen(syntax_corpus_cases[i].text));
        REQUIRE(range.line >= 1 && range.column >= 1);
    }
    cleanup();
    original = script_parse(syntax_corpus_cases[SYNTAX_CORPUS_RECOVERY].text);
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
    original = script_parse(syntax_corpus_cases[SYNTAX_CORPUS_UTF8].text);
    REQUIRE(original->diagnostic_count == 0);
    original_tree = ts_node_string(ts_tree_root_node(original->tree));
    const syntax_corpus_edit *edit = &syntax_corpus_edits[0];
    edited = script_edit(original, edit->start, edit->end, edit->replacement);
    REQUIRE(edited->diagnostic_count == 0);
    REQUIRE(!strcmp(edited->text, edit->after));
    fresh = script_parse(edited->text);
    edited_tree = ts_node_string(ts_tree_root_node(edited->tree));
    fresh_tree = ts_node_string(ts_tree_root_node(fresh->tree));
    REQUIRE(!strcmp(edited_tree, fresh_tree));
    REQUIRE(fresh->diagnostic_count == edited->diagnostic_count);
    REQUIRE(!strcmp(original->text, syntax_corpus_cases[SYNTAX_CORPUS_UTF8].text));
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
    original = script_parse(syntax_corpus_cases[SYNTAX_CORPUS_DELETE].text);
    REQUIRE(original->diagnostic_count == 0);
    const syntax_corpus_edit *edit = &syntax_corpus_edits[1];
    edited = script_edit(original, edit->start, edit->end, edit->replacement);
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
            fprintf(pu_errors(), "Cannot read bounded corpus source: %s\n", path);
        }
        REQUIRE(corpus_text != NULL);
        total += bytes;
        REQUIRE(total <= CORPUS_MAX_TOTAL_BYTES);
        original = script_parse(corpus_text);
        if (original->diagnostic_count) {
            failures++;
            fprintf(pu_errors(), "Corpus syntax errors: %s\n", path);
            for (int i = 0; i < original->diagnostic_count && i < 8; i++) {
                fprintf(
                    pu_errors(),
                    "  %d:%d %s\n",
                    original->diagnostics[i].range.line,
                    original->diagnostics[i].range.column,
                    original->diagnostics[i].message
                );
            }
        }
        script_syntax_free(original);
        original = NULL;
        free(corpus_text);
        corpus_text = NULL;
    }
    REQUIRE(files > 0);
    REQUIRE(!ferror(manifest_file));
    REQUIRE(fgetc(manifest_file) == EOF);
    printf("Retained %s corpus: %d files, %zu bytes, %d failures\n", "syntax", files, total, failures);
    REQUIRE(failures == 0);
    return true;
}

void test_language_syntax()
{
    test_parallel_begin();
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(language_managed_pointer_syntax_preserves_xor);
    RUN_TEST_CASE(language_declarations_actions_and_bindings);
    RUN_TEST_CASE(language_handlers_name_payload_fields);
    RUN_TEST_CASE(language_systems_use_flat_queries_and_implicit_step);
    RUN_TEST_CASE(language_retained_binding_syntax);
    RUN_TEST_CASE(language_unified_declarations_and_generic_map_shapes);
    RUN_TEST_CASE(language_authored_asset_declarations);
    RUN_TEST_CASE(language_obsolete_symbol_spellings_rejected);
    RUN_TEST_CASE(language_retained_bindings_reject_prefixes_and_value_types);
    RUN_TEST_CASE(language_keywords_are_contextual_data_member_names);
    RUN_TEST_CASE(language_not_callable_and_unary_spellings_disambiguate);
    RUN_TEST_CASE(language_callable_types_entries_and_results);
    RUN_TEST_CASE(language_callable_arguments_keep_declarations_and_expression_boundaries);
    RUN_TEST_CASE(language_callable_arguments_preserve_view_and_range_syntax);
    RUN_TEST_CASE(language_system_queries_and_control_flow);
    RUN_TEST_CASE(language_nested_typed_records_and_explicit_labels);
    RUN_TEST_CASE(language_definitions_unions_and_type_patterns);
    RUN_TEST_CASE(language_type_attributes_are_declaration_children);
    RUN_TEST_CASE(language_query_header_and_cancel_deadline);
    RUN_TEST_CASE(language_query_generators_preserve_authored_order);
    RUN_TEST_CASE(language_query_expressions_share_system_terms);
    RUN_TEST_CASE(language_obsolete_declaration_tokens_rejected);
    RUN_TEST_CASE(language_ref_arguments_are_not_syntax);
    RUN_TEST_CASE(language_expressions_precedence_and_continuation);
    RUN_TEST_CASE(language_invalid_source_has_positioned_diagnostics);
    RUN_TEST_CASE(language_incremental_snapshot_matches_fresh);
    RUN_TEST_CASE(language_incremental_errors_match_fresh);
    pu_clear_setup();
    test_parallel_end();
}

void test_language_corpus(const char *manifest)
{
    corpus_manifest = manifest;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(language_retained_corpus);
    pu_clear_setup();
}
