#include "assets/code_source_internal.h"
#include "graph/nodes/component_nodes.h"
#include "support/array.h"

#include <stdio.h>
#include <string.h>

void test_check(int ok, const char *message);

static void check_parse(const char *text, const char *expected)
{
    code_source *source = code_source_parse(text);
    bool found = !expected && source->asset && !acount(source->diagnostics);
    for (int i = 0; i < acount(source->diagnostics); i++) {
        code_source_diagnostic *d = &source->diagnostics[i];
        found |= expected && strstr(d->message, expected) != NULL;
        test_check(
            d->start >= 0 && d->end >= d->start && d->end <= (int)strlen(text),
            "Name diagnostics use valid source byte ranges"
        );
    }
    test_check(found, expected ? expected : "Distinct scoped names and ordinary comments parse successfully");
    code_source_free(source);
}

static void check_program(const char *text, const char *expected)
{
    code_source *source = code_source_parse(text);
    bool found = !expected && source->asset && !acount(source->diagnostics);
    for (int i = 0; i < acount(source->diagnostics); i++) {
        code_source_diagnostic *d = &source->diagnostics[i];
        test_check(
            d->start >= 0 && d->end >= d->start && d->end <= (int)strlen(text),
            "Readable callable name diagnostics use valid source byte ranges"
        );
        found |= expected && strstr(d->message, expected);
        if (!expected) {
            fprintf(stderr, "Callable name fixture: %d-%d: %s\n", d->start, d->end, d->message);
        }
    }
    test_check(found, expected ? expected : "Readable callable names remain local to their native function scope");
    code_source_free(source);
}

void run_code_source_name_checks()
{
    check_parse("behavior Test { state int event = 0; }", "reserved language name");
    check_program("behavior CastName { state cast: int = 0; }", "reserved language name");
    check_program("library CastName { pure function \"cast\"() -> (Result: int) { result Result: int = 3; } }", NULL);
    check_parse("behavior Test { state int[] values = [1]; on Tick() { for (event in values) { destroy(self); } } }", "reserved language name");
    check_parse("behavior Test { state int[] values = [1]; state int count = 0; on Tick() { for (count in values) { destroy(self); } } }", "shadows a state variable");
    check_parse("behavior Test { state int[] values = [1]; on Tick() { for (item in values) { for (item in values) { destroy(self); } } } }", "enclosing loop binding");
    check_parse("behavior Test { state string title = \"prefix\\u0000suffix\"; }", "embedded NUL byte");
    check_parse("behavior Test { state string title = \"prefix\\u0001suffix\"; }", NULL);
    check_parse("behavior Test { on Tick() { // @node same\n destroy(self); // @node same\n destroy(self); } }", NULL);
    check_parse("behavior Test { on Tick() { // @node 01\n destroy(self); } on Other() { // @node 1\n destroy(self); } }", NULL);
    check_parse("behavior Test { on Tick() { // @node 0\n destroy(self); } }", NULL);
    check_parse("behavior Test { on Tick() { // @node 18446744073709551616\n destroy(self); } }", NULL);
    check_parse("behavior Test { state int[] values = [1]; on Tick() { // @node first\n for (item in values) { destroy(self); } // @node second\n for (item in values) { destroy(self); } } }", NULL);

    check_parse("behavior Test { on Tick() { // @node first\n // @node second\n destroy(self); } }", NULL);
    check_parse("behavior Test { on Tick() { // @node\n destroy(self); } }", NULL);
    check_parse("behavior Test { on Tick() { // @node -1\n destroy(self); } }", NULL);
    check_parse("behavior Test { /* @authoring {not valid metadata} */ on Tick() { destroy(self); } }", NULL);
    check_program(
        "library Names { state count: int = 1; "
        "function First() -> (Result: int) { value chosen = "
        "get_variable<\"count\">(); result Result: int = chosen.Value; } "
        "function Second() -> (Result: int) { value chosen = "
        "get_variable<\"count\">(); result Result: int = chosen.Value; } }",
        NULL
    );
    check_program("behavior Names { value chosen = get_variable<\"count\">(); value chosen = get_variable<\"count\">(); }", "Duplicate callable name");
    check_program("behavior Names { state count: int = 0; state count: int = 1; }", "Duplicate state name");
    check_program("behavior Names { state title: string = \"prefix\\u0000suffix\"; }", "embedded NUL byte");
    check_program("behavior Names { state title: string = \"prefix\\u0001suffix\"; }", NULL);
    check_program(
        "behavior QuotedEvent { state \"event\": int = 2; state delta: float = "
        "0; "
        "on Tick() { delta = event.dt; &state.\"event\" = 3; } }",
        NULL
    );
    check_program(
        "library Quoted { state \"for\": int = 2; "
        "pure function \"value\"() -> (Result: int) { "
        "result Result: int = &state.\"for\"; } }",
        NULL
    );
    const ecs_component_desc *component = component_by_key("GameMovement");
    test_check(component != NULL, "Name safety checks have a reflected component fixture");
    if (!component) {
        return;
    }
    check_parse("behavior Test { on Tick(GameMovement event) {} }", "reserved language name");
    check_parse("behavior Test { on Tick(GameMovement movement) {} state int movement = 0; }", "shadows a state variable");
    check_parse("behavior Test { state int[] values = [1]; on Tick(GameMovement movement) { for (movement in values) { destroy(self); } } }", "shadows a component alias");
    check_parse("behavior Test { on Tick(GameMovement movement) {} on Other(GameMovement movement) {} }", NULL);
}
