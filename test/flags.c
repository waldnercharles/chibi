#include "editor/ui/flags.h"

#include "graph/model/graph.h"
#include "graph/model/registry.h"
#include "graph/runtime/eval.h"
#include "support/arena.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_format(unsigned value, const editor_flags *flags, const char *expected)
{
    char text[128];
    editor_flags_format(value, flags, text, sizeof text);
    assert(strcmp(text, expected) == 0);
}

static void flag_query()
{
    graph g;
    graph_init(&g);
    node *query = graph_add_node(&g, registry_node_by_name("Has Any Flags"));
    assert(query);
    arena memory;
    arena_init(&memory, 4096);
    eval_context context;
    eval_context_init(&context);
    const unsigned values[] = { 2, 3, 1, 0, 0x80000000u };
    const unsigned masks[] = { 2, 2, 2, 0, 0x80000000u };
    const bool expected[] = { true, true, false, false, true };
    for (int i = 0; i < 5; i++) {
        assert(
            graph_set_pin_default(&g, query, 0, 0, &(pin_value){ .type = PIN_U32, .u = values[i] }) == 0
        );
        assert(
            graph_set_pin_default(&g, query, 0, 1, &(pin_value){ .type = PIN_U32, .u = masks[i] }) == 0
        );
        eval_context_reset_runtime(&context);
        pin_value result;
        assert(graph_eval_output(&g, query, 0, &memory, &result, &context) == 0);
        assert(result.type == PIN_BOOL && result.b == expected[i]);
    }
    eval_context_free(&context);
    arena_free(&memory);
    graph_free(&g);
}

void test_flags()
{
    flag_query();
    const editor_flag items[] = { { 1u, "Player" }, { 2u, "Enemy" }, { 4u, "Projectile" } };
    const editor_flags flags = { items, 3 };
    const editor_flags empty = { NULL, 0 };
    expect_format(0, &flags, "None");
    expect_format(1, &flags, "Player");
    expect_format(3, &flags, "Player, Enemy");
    expect_format(7, &flags, "Player, Enemy, Projectile");
    expect_format(0x80, &flags, "0x80");
    expect_format(0x83, &flags, "Player, Enemy, 0x80");
    expect_format(0, &empty, "None");
    expect_format(0x80, &empty, "0x80");
    assert(editor_flags_toggle(0x80, 1) == 0x81);
    assert(editor_flags_toggle(0x81, 1) == 0x80);
    assert(editor_flags_toggle(0x81, 3) == 0x83);
    assert(editor_flags_toggle(0x83, 3) == 0x80);
    assert(editor_flags_toggle(0x87, 3) == 0x84);
    char guarded[] = { 'L', 'x', 'x', 'x', 'x', 'R' };
    editor_flags_format(7, &flags, guarded + 1, 4);
    assert(guarded[0] == 'L' && guarded[5] == 'R');
    assert(guarded[4] == 0);
    assert(strcmp(guarded + 1, "Pla") == 0);
    guarded[1] = 'x';
    editor_flags_format(7, &flags, guarded + 1, 1);
    assert(guarded[1] == 0 && guarded[0] == 'L' && guarded[5] == 'R');
    guarded[1] = 'x';
    editor_flags_format(7, &flags, guarded + 1, 0);
    assert(guarded[1] == 'x');
    puts("flags PASS: named combinations, unknown bits, composite toggles and bounded formatting");
}
