#include "scalar_lowering_chibi.h"

#include "backend.h"
#include "compiler_fixture.h"
#include "execution/definition.h"
#include "execution/module_builder.h"
#include "execution/prepared.h"
#include "execution/scalar_lowering_plan.h"
#include "execution/status.h"
#include "language/scalar_lowering_reference.h"
#include "scalar_lowering_chibi_source.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"
#include "types/union.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Finite workload. Eight raw requests on two backends:
 * 16 authored Lower calls, six accepted plans x two state vectors x two
 * backends x authored/reference = 48 target runs. Two Lower.chibi preparations
 * also run 31 named-constant bootstrap initializers each: 62 additional BYTECODE
 * initializer VM entries. Preparation is inside the supervised fixture, not just
 * parsing/build work. No user-source parsing. Four decoder mutations per backend (8 total)
 * add no authored or target invocation. Context budget 500000 is supplied to
 * Lower; it is inherited/reset by child calls and is NOT a global work ceiling.
 * Target bodies contain no calls, loops, native callbacks, tasks or IO; their
 * supplied budget is 128. Configuration/build timing is separate. */
enum
{
    CASES = 8,
    ACCEPTED = 6,
    EXPRESSIONS = 24
};
static const char PACKAGE[] = "project:scalar_lowering";
static compiler_fixture fixture;
static arena input, outputs[2][CASES];
static pin_value plans[2][CASES];
static uint64_t ready_tags[2], rejected_tags[2];
static int authored_calls, target_runs, decoder_rejections;

typedef struct
{
    script_analysis analysis;
    script_catalog catalog;
    script_expression expressions[EXPRESSIONS];
    script_symbol symbols[2];
    script_source_document documents[3];
    script_type types[3];
    execution_module_state variables[2];
    int expression_types[EXPRESSIONS], result_expressions[2], result_types[2];
    int type_count, result_count;
    pin_value vectors[2][2], expected[2][2];
    int expected_code, expected_slots, expected_locals;
} facts;
static facts cases[CASES];
static pin_value integer(int n)
{
    return (pin_value){ .type = PIN_I32, .i = n };
}
static pin_value boolean(bool n)
{
    return (pin_value){ .type = PIN_BOOL, .b = n };
}
static pin_value id_value(uint64_t n)
{
    return (pin_value){ .type = PIN_U64, .u64 = n };
}
static script_range location(int n)
{
    return (script_range){ 10 + n * 5, 13 + n * 5, n + 1, n + 2, n % 3 };
}
static void init(facts *f)
{
    memset(f, 0, sizeof *f);
    f->types[0] = (script_type){ .storage = PIN_BOOL };
    f->types[1] = (script_type){ .storage = PIN_I32 };
    f->types[2] = (script_type){ .storage = PIN_BOOL, .flags = SCRIPT_TYPE_OPTIONAL };
    f->type_count = 2;
    f->documents[0] = (script_source_document){ "first.chibi", "first.chibi", "" };
    f->documents[1] = (script_source_document){ "second.chibi", "second.chibi", "" };
    f->documents[2] = (script_source_document){ "evaluator.chibi", "evaluator.chibi", "" };
    f->analysis = (script_analysis){ .name = "scalar-fixture",
                                     .symbols = f->symbols,
                                     .symbol_count = 2,
                                     .expressions = f->expressions,
                                     .documents = f->documents,
                                     .document_count = 3,
                                     .owner_symbol = -1,
                                     .valid = true };
    f->symbols[0] = (script_symbol){ .id = UINT64_C(0xfedcba9876543211),
                                     .kind = SCRIPT_STATE,
                                     .type = f->types[1],
                                     .initializer = -1 };
    f->symbols[1] = (script_symbol){ .id = UINT64_C(0x8000000000000003),
                                     .kind = SCRIPT_SHARED_STATE,
                                     .type = f->types[1],
                                     .initializer = -1 };
    /* Deliberately different signature/symbol orders, full-width exact IDs. */
    f->variables[0] = (execution_module_state){ .id = f->symbols[1].id,
                                                .initial = integer(19),
                                                .shared = true };
    f->variables[1] = (execution_module_state){ .id = f->symbols[0].id,
                                                .initial = integer(-7) };
    f->vectors[0][0] = integer(5);
    f->vectors[0][1] = integer(1);
    f->vectors[1][0] = integer(4);
    f->vectors[1][1] = integer(4);
    f->result_count = 1;
}
static int expr(facts *f, script_expression_kind kind, int type, int op, int left, int right)
{
    int n = f->analysis.expression_count++;
    assert(n < EXPRESSIONS);
    f->expressions[n] = (script_expression){ .kind = kind,
                                             .type = f->types[type],
                                             .op = op,
                                             .left = left,
                                             .right = right,
                                             .symbol = -1,
                                             .range = location(n) };
    f->expression_types[n] = type;
    return n;
}
static int symbol(facts *f, int which)
{
    int type = f->symbols[which].type.storage == PIN_BOOL ? 0 : 1;
    int n = expr(f, SCRIPT_EXPR_SYMBOL, type, 0, -1, -1);
    f->expressions[n].symbol = which;
    return n;
}
static int constant(facts *f, int n)
{
    int e = expr(f, SCRIPT_EXPR_CONSTANT, 1, 0, -1, -1);
    f->expressions[e].constant = integer(n);
    return e;
}
static int binary(facts *f, int op, int left, int right)
{
    bool boolean_result = op >= SCRIPT_OP_AND && op <= SCRIPT_OP_GE;
    return expr(f, SCRIPT_EXPR_BINARY, boolean_result ? 0 : 1, op, left, right);
}
static void result(facts *f, int ordinal, int expression)
{
    f->result_expressions[ordinal] = expression;
    f->result_types[ordinal] = f->expression_types[expression];
}
static void corpus()
{
    for (int i = 0; i < CASES; i++) {
        init(&cases[i]);
    }
    facts *f = &cases[0];
    int x = symbol(f, 0), two = constant(f, 2),
        sum = binary(f, SCRIPT_OP_ADD, x, two);
    result(f, 0, binary(f, SCRIPT_OP_MUL, sum, sum));
    f->expected[0][0] = integer(9);
    f->expected[1][0] = integer(36);
    int five = constant(f, 5), remainder = binary(f, SCRIPT_OP_MOD, x, five);
    int seven = constant(f, 7),
        masked = binary(f, SCRIPT_OP_BIT_AND, remainder, seven);
    int eight = constant(f, 8), combined = binary(f, SCRIPT_OP_BIT_OR, masked, eight);
    int toggled = binary(f, SCRIPT_OP_BIT_XOR, combined, two), shift = constant(f, 1);
    int shifted = binary(f, SCRIPT_OP_SHIFT_LEFT, toggled, shift);
    shifted = binary(f, SCRIPT_OP_SHIFT_RIGHT, shifted, shift);
    int negative = expr(f, SCRIPT_EXPR_UNARY, 1, SCRIPT_OP_NEG, shifted, -1);
    result(f, 1, expr(f, SCRIPT_EXPR_UNARY, 1, SCRIPT_OP_BIT_NOT, negative, -1));
    f->result_count = 2;
    f->expected[0][1] = integer(10);
    f->expected[1][1] = integer(13);
    f->expected_code = 32;
    f->expected_slots = 28;
    f = &cases[1];
    x = symbol(f, 0);
    int zero = constant(f, 0);
    int less = binary(f, SCRIPT_OP_LT, x, zero);
    int not_less = expr(f, SCRIPT_EXPR_UNARY, 0, SCRIPT_OP_NOT, less, -1);
    int at_most = binary(f, SCRIPT_OP_LE, x, zero),
        at_least = binary(f, SCRIPT_OP_GE, x, zero);
    int equal_zero = binary(f, SCRIPT_OP_AND, at_most, at_least);
    result(f, 0, expr(f, SCRIPT_EXPR_CAST, 0, SCRIPT_OP_NONE, not_less, -1));
    result(f, 1, expr(f, SCRIPT_EXPR_UNARY, 0, SCRIPT_OP_NONE, equal_zero, -1));
    f->result_count = 2;
    f->vectors[0][1] = integer(-2);
    f->vectors[1][1] = integer(0);
    f->expected[0][0] = boolean(false);
    f->expected[1][0] = boolean(true);
    f->expected[0][1] = boolean(false);
    f->expected[1][1] = boolean(true);
    f->expected_code = 26;
    f->expected_slots = 18;
    f->expected_locals = 1;
    for (int i = 2; i <= 3; i++) {
        f = &cases[i];
        x = symbol(f, 0);
        zero = constant(f, 0);
        int guard = binary(f, i == 2 ? SCRIPT_OP_NE : SCRIPT_OP_EQ, x, zero);
        int twelve = constant(f, 12), divide = binary(f, SCRIPT_OP_DIV, twelve, x);
        int one = constant(f, 1), greater = binary(f, SCRIPT_OP_GT, divide, one);
        result(f, 0, binary(f, i == 2 ? SCRIPT_OP_AND : SCRIPT_OP_OR, guard, greater));
        f->vectors[0][1] = integer(0);
        f->vectors[1][1] = integer(i == 2 ? 3 : 12);
        f->expected[0][0] = boolean(i == 3);
        f->expected[1][0] = boolean(i == 2);
        f->expected_code = i == 2 ? 19 : 18;
        f->expected_slots = 12;
        f->expected_locals = 1;
    }
    f = &cases[4];
    x = symbol(f, 0);
    int y = symbol(f, 1);
    result(f, 0, binary(f, SCRIPT_OP_LT, x, y));
    result(f, 1, binary(f, SCRIPT_OP_SUB, y, x));
    f->result_count = 2;
    f->vectors[0][1] = integer(2);
    f->vectors[1][1] = integer(9);
    f->expected[0][0] = boolean(true);
    f->expected[0][1] = integer(3);
    f->expected[1][0] = boolean(false);
    f->expected[1][1] = integer(-5);
    f->expected_code = 16;
    f->expected_slots = 12;
    f = &cases[5];
    f->symbols[1].type = f->types[0];
    f->variables[0].initial = boolean(true);
    x = symbol(f, 0);
    y = symbol(f, 1);
    zero = constant(f, 0);
    int nonzero = binary(f, SCRIPT_OP_NE, x, zero), twelve = constant(f, 12);
    int divide = binary(f, SCRIPT_OP_DIV, twelve, x), one = constant(f, 1);
    int greater = binary(f, SCRIPT_OP_GT, divide, one);
    int first = binary(f, SCRIPT_OP_AND, y, nonzero);
    int second = binary(f, SCRIPT_OP_AND, nonzero, greater);
    result(f, 0, binary(f, SCRIPT_OP_OR, first, second));
    f->vectors[0][0] = boolean(true);
    f->vectors[0][1] = integer(0);
    f->vectors[1][0] = boolean(false);
    f->vectors[1][1] = integer(3);
    f->expected[0][0] = boolean(false);
    f->expected[1][0] = boolean(true);
    f->expected_code = 32;
    f->expected_slots = 18;
    f->expected_locals = 3;
    f = &cases[6];
    f->type_count = 3;
    int unsupported = expr(f, SCRIPT_EXPR_CONSTANT, 2, 0, -1, -1);
    f->expressions[unsupported].constant = boolean(false);
    result(f, 0, unsupported);
    f = &cases[7];
    result(f, 0, expr(f, SCRIPT_EXPR_CALL, 1, 0, -1, -1));
}
static bool put(pin_value *v, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(v), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, v->ptr, field, value));
    return true;
}
static bool ints(pin_value *v, const char *const *names, const int *values, int count)
{
    for (int i = 0; i < count; i++) {
        REQUIRE(put(v, names[i], integer(values[i])));
    }
    return true;
}
static bool record(int b, const char *name, pin_value *out)
{
    const pin_record *shape = compiler_fixture_record(&fixture, b, PACKAGE, name, &input);
    REQUIRE(shape);
    *out = (pin_value){ .type = PIN_STRUCT,
                        .record = shape,
                        .type_name = shape->type.name,
                        .ptr = arena_alloc(&input, shape->type.size) };
    memset(out->ptr, 0, shape->type.size);
    return true;
}
static bool rows(int b, const char *name, int count, pin_value *out)
{
    /* Empty record arrays still carry their shape, just like nonempty rows. */
    const pin_record *shape = compiler_fixture_record(&fixture, b, PACKAGE, name, &input);
    REQUIRE(shape);
    pin_array
        *array = pin_array_snapshot_allocate(&input, PIN_STRUCT, shape->type.name, count);
    REQUIRE(array);
    array->record = shape;
    *out = (pin_value){ .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .record = shape,
                        .type_name = shape->type.name,
                        .ptr = array };
    for (int i = 0; i < count; i++) {
        REQUIRE(record(b, name, &array->items[i]));
    }
    return true;
}
static pin_value *at(pin_value *v, int i)
{
    return &((pin_array *)v->ptr)->items[i];
}
static uint64_t tag(int b, const char *name, const char *variant)
{
    const authoring_package *p = compiler_fixture_package(&fixture, b, PACKAGE);
    const script_catalog_type *t = script_catalog_type_by_id(
        p->exports->metadata,
        compiler_fixture_export(&fixture, b, PACKAGE, name)
    );
    for (int i = 0; t && i < t->variant_count; i++) {
        if (!strcmp(t->variants[i].name, variant)) {
            return t->variants[i].tag;
        }
    }
    return 0;
}
static bool pack(int b, const facts *f, pin_value *out)
{
    pin_value a[11];
    int constants = 2;
    for (int i = 0; i < f->analysis.expression_count; i++) {
        constants += f->expressions[i].kind == SCRIPT_EXPR_CONSTANT;
    }
    REQUIRE(rows(b, "Type", f->type_count, &a[0]));
    REQUIRE(rows(b, "Expression", f->analysis.expression_count, &a[1]));
    REQUIRE(rows(b, "Symbol", 2, &a[2]) && rows(b, "Variable", 2, &a[3]));
    REQUIRE(
        rows(b, "Result", f->result_count, &a[4]) &&
        rows(b, "Literal", constants, &a[5])
    );
    REQUIRE(rows(b, "Range", f->analysis.expression_count + 1, &a[7]));
    REQUIRE(record(b, "Target", &a[9]));
    for (int i = 0; i < f->type_count; i++) {
        const char *keys[] = { "storage", "container", "flags", "nominal_slot", "parameter_slot" };
        int values[] = { f->types[i].storage,
                         f->types[i].container,
                         f->types[i].flags,
                         -1,
                         -1 };
        REQUIRE(ints(at(&a[0], i), keys, values, 5));
    }
    int next_literal = 2;
    for (int i = 0; i < f->analysis.expression_count; i++) {
        const script_expression *e = &f->expressions[i];
        int literal = e->kind == SCRIPT_EXPR_CONSTANT ? next_literal++ : -1;
        const char *keys[] = { "kind",   "op",   "left",    "right",
                               "symbol", "type", "literal", "range" };
        int values[] = { e->kind,  e->op,     e->left,
                         e->right, e->symbol, f->expression_types[i],
                         literal,  i };
        REQUIRE(ints(at(&a[1], i), keys, values, 8));
        if (literal >= 0) {
            const char *fields[] = { "storage", "payload" };
            int payload[] = { e->constant.type,
                              e->constant.type == PIN_BOOL ? e->constant.b
                                                           : e->constant.i };
            REQUIRE(ints(at(&a[5], literal), fields, payload, 2));
        }
    }
    for (int i = 0; i < 2; i++) {
        const char *symbol_keys[] = { "kind", "id_slot", "type" };
        int symbol_values[] = { f->symbols[i].kind,
                                i,
                                f->symbols[i].type.storage == PIN_BOOL ? 0 : 1 };
        REQUIRE(ints(at(&a[2], i), symbol_keys, symbol_values, 3));
        const char *variable_keys[] = { "id_slot", "type", "literal" };
        int variable_values[] = { 1 - i,
                                  f->variables[i].initial.type == PIN_BOOL ? 0 : 1,
                                  i };
        REQUIRE(ints(at(&a[3], i), variable_keys, variable_values, 3));
        const char *literal_keys[] = { "storage", "payload" };
        pin_value value = f->variables[i].initial;
        int literal_values[] = { value.type,
                                 value.type == PIN_BOOL ? value.b : value.i };
        REQUIRE(ints(at(&a[5], i), literal_keys, literal_values, 2));
    }
    for (int i = 0; i < f->result_count; i++) {
        REQUIRE(put(at(&a[4], i), "expression", integer(f->result_expressions[i])));
        REQUIRE(put(at(&a[4], i), "type", integer(f->result_types[i])));
    }
    pin_array *ids = pin_array_snapshot_allocate(&input, PIN_U64, NULL, 2);
    REQUIRE(ids);
    ids->items[0] = id_value(f->symbols[0].id);
    ids->items[1] = id_value(f->symbols[1].id);
    a[6] = (pin_value){ .type = PIN_U64, .container = PIN_CONTAINER_ARRAY, .ptr = ids };
    for (int i = 0; i <= f->analysis.expression_count; i++) {
        script_range r = i == f->analysis.expression_count ? location(23)
                                                           : f->expressions[i].range;
        const char *keys[] = { "start", "end", "line", "column", "document" };
        int values[] = { r.start, r.end, r.line, r.column, r.document };
        REQUIRE(ints(at(&a[7], i), keys, values, 5));
    }
    pin_array *documents = pin_array_snapshot_allocate(&input, PIN_STR, NULL, 3);
    REQUIRE(documents);
    for (int i = 0; i < 3; i++) {
        size_t n = strlen(f->documents[i].path) + 1;
        char *text = arena_alloc(&input, n);
        memcpy(text, f->documents[i].path, n);
        REQUIRE(!pin_value_text(&input, &documents->items[i], text, n - 1));
    }
    a[8] = (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = documents };
    const char *keys[] = { "schema",     "prepared",       "pointer_storage",
                           "value_size", "payload_offset", "results_offset" };
    int values[] = { 1,
                     EXECUTION_PREPARED_VERSION,
                     BF_POINTER_TYPE,
                     sizeof(pin_value),
                     offsetof(pin_value, d),
                     offsetof(behavior_function_context, results) };
    REQUIRE(ints(&a[9], keys, values, 6));
    a[10] = integer(f->analysis.expression_count);
    REQUIRE(!compiler_fixture_union(&input, tag(b, "Request", "Tables"), a, 11, out));
    return true;
}
static bool same_value(pin_value a, pin_value b)
{
    if (a.type != b.type || a.container != b.container || a.type_name ||
        b.type_name || a.record || b.record) {
        return false;
    }
    if (a.type == PIN_BOOL) {
        return a.b == b.b;
    }
    if (a.type == PIN_I32) {
        return a.i == b.i;
    }
    return a.type == PIN_FLOW && !a.u64 && !b.u64;
}
static bool same_instruction(const behavior_function_instruction *a, const behavior_function_instruction *b)
{
    return a->op == b->op && a->type == b->type && a->output == b->output &&
           a->left == b->left && a->right == b->right && a->target == b->target &&
           a->frame == b->frame && a->source == b->source &&
           a->stack == b->stack && a->pin == b->pin && a->offset == b->offset &&
           a->indirect == b->indirect && !a->left_slot && !b->left_slot &&
           !a->right_slot && !b->right_slot && a->component == b->component &&
           a->node_id == b->node_id && a->layout == b->layout && !a->message &&
           !b->message && same_value(a->value, b->value);
    /* left_slot/right_slot are derived runtime metadata, absent on this wire. */
}
static bool same_plan(const facts *facts, const behavior_function *a, const behavior_function *b)
{
    REQUIRE(a->argument_count == 0 && b->argument_count == 0);
    REQUIRE(a->result_count == b->result_count && a->result_count == facts->result_count);
    REQUIRE(a->variable_count == b->variable_count && a->variable_count == 2);
    REQUIRE(
        acount(a->instructions) == facts->expected_code &&
        acount(b->instructions) == facts->expected_code
    );
    REQUIRE(
        acount(a->slots) == facts->expected_slots && acount(b->slots) == facts->expected_slots
    );
    REQUIRE(
        acount(a->locals) == facts->expected_locals &&
        acount(b->locals) == facts->expected_locals
    );
    REQUIRE(
        acount(a->sources) == acount(a->instructions) + 1 &&
        acount(b->sources) == acount(b->instructions) + 1
    );
    REQUIRE(
        !acount(a->source_points) && !acount(b->source_points) &&
        !acount(a->source_watches) && !acount(b->source_watches) &&
        !acount(a->value_roots) && !acount(b->value_roots)
    );
    for (int i = 0; i < a->result_count; i++) {
        REQUIRE(same_value(a->result_defaults[i], b->result_defaults[i]));
        REQUIRE(a->result_kinds[i] == b->result_kinds[i] && a->result_kinds[i] == BEHAVIOR_VALUE);
    }
    for (int i = 0; i < 2; i++) {
        REQUIRE(same_value(a->variable_defaults[i], b->variable_defaults[i]));
    }
    for (int i = 0; i < acount(a->slots); i++) {
        REQUIRE(a->slots[i] == b->slots[i] && a->slot_kinds[i] == b->slot_kinds[i]);
    }
    for (int i = 0; i < acount(a->locals); i++) {
        const behavior_function_local *x = &a->locals[i], *y = &b->locals[i];
        REQUIRE(
            same_value(x->initial, y->initial) && x->kind == y->kind &&
            x->frame == y->frame && x->node_id == y->node_id && x->pin == y->pin &&
            x->offset == y->offset && x->parameter == y->parameter &&
            x->array_default == y->array_default && x->data_index == y->data_index
        );
    }
    REQUIRE(
        !a->sources[0].asset_name && !b->sources[0].asset_name &&
        a->sources[0].parent == -1 && b->sources[0].parent == -1
    );
    for (int i = 0; i < acount(a->instructions); i++) {
        REQUIRE(same_instruction(&a->instructions[i], &b->instructions[i]));
        const behavior_function_source *x = &a->sources[i + 1],
                                       *y = &b->sources[i + 1];
        REQUIRE(
            !strcmp(x->asset_name, y->asset_name) && x->start == y->start &&
            x->end == y->end && x->line == y->line && x->column == y->column &&
            x->parent == -1 && y->parent == -1
        );
    }
    return true;
}
static bool owned(arena *owner, const pin_value *v, int depth)
{
    REQUIRE(depth <= 4);
    if (v->container == PIN_CONTAINER_ARRAY) {
        const pin_array *a = v->ptr;
        if (!a) {
            return true;
        }
        REQUIRE(arena_contains(owner, a) && a->count <= 256 && a->count >= 0);
        REQUIRE(!a->count || (a->items && arena_contains(owner, a->items)));
        for (int i = 0; i < a->count; i++) {
            REQUIRE(owned(owner, &a->items[i], depth + 1));
        }
        return true;
    }
    if (v->type == PIN_UNION) {
        const pin_union *u = v->ptr;
        REQUIRE(u && arena_contains(owner, u) && (u->count == 1 || u->count == 9));
        for (int i = 0; i < u->count; i++) {
            REQUIRE(owned(owner, &u->values[i], depth + 1));
        }
        return true;
    }
    if (v->type == PIN_STRUCT) {
        REQUIRE(
            v->ptr && v->record && arena_contains(owner, v->ptr) &&
            arena_contains(owner, v->record)
        );
        const type_desc *type = pin_value_type(v);
        REQUIRE(
            type && type->field_count >= 0 && type->field_count <= 19 &&
            arena_contains(owner, type->name)
        );
        for (int i = 0; i < type->field_count; i++) {
            pin_value value;
            REQUIRE(!pin_read_field(owner, v->ptr, &type->fields[i], &value));
            REQUIRE(owned(owner, &value, depth + 1));
        }
        return true;
    }
    if (v->type == PIN_STR) {
        REQUIRE(pin_text(*v).bytes && arena_contains(owner, v->text));
        return true;
    }
    REQUIRE(v->type == PIN_I32 || v->type == PIN_BOOL || v->type == PIN_U64);
    return true;
}
static bool get(const pin_value *v, const char *key, pin_value *out)
{
    const type_field *f = pin_type_field_path(pin_value_type(v), key);
    REQUIRE(f);
    REQUIRE(!pin_read_field(&input, v->ptr, f, out));
    return true;
}
static scalar_lowering_builder holder;
static behavior_function_builder *oracle_builder;
static behavior_function *finished[2];
static execution_module *modules[2];
static execution_module_plan *module_plan;
static unsigned char *serialized;
static void cleanup()
{
    arena_free(&input);
    compiler_fixture_free(&fixture);
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < CASES; i++) {
            arena_free(&outputs[b][i]);
        }
        execution_module_free(modules[b]);
        modules[b] = NULL;
        behavior_function_free(finished[b]);
        finished[b] = NULL;
    }
    scalar_lowering_builder_free(&holder);
    if (oracle_builder) {
        behavior_function_builder_free(oracle_builder);
        oracle_builder = NULL;
    }
    execution_module_plan_free(module_plan);
    module_plan = NULL;
    free(serialized);
    serialized = NULL;
}
static bool mutations(int b)
{
    pin_union *u = plans[b][2].ptr;
    REQUIRE(u->tag == ready_tags[b] && u->count == 9);
    pin_array *code = u->values[8].ptr;
    pin_value *targets[4] = { &u->values[0], &code->items[0], NULL, NULL };
    const char *keys[] = { "results_offset", "source", "target", "payload" };
    int wrong[] = { -1, 255, 256, 2 };
    for (int i = 0; i < code->count; i++) {
        pin_value op;
        REQUIRE(get(&code->items[i], "op", &op));
        if (op.i == 7 && !targets[2]) {
            targets[2] = &code->items[i];
        }
        /* Result ordinal CONSTANT immediately precedes the first INDEX. */
        if (op.i == 4 && !targets[3]) {
            REQUIRE(i > 0);
            targets[3] = &code->items[i - 1];
        }
    }
    for (int i = 0; i < 4; i++) {
        REQUIRE(targets[i]);
        pin_value old;
        REQUIRE(get(targets[i], keys[i], &old));
        REQUIRE(put(targets[i], keys[i], integer(wrong[i])));
        scalar_lowering_builder rejected = { 0 };
        char message[192];
        bool decoded = scalar_lowering_decode(&plans[b][2], ready_tags[b], &rejected, message, sizeof message);
        bool empty = !rejected.builder && !rejected.variable_count;
        scalar_lowering_builder_free(&rejected);
        REQUIRE(put(targets[i], keys[i], old));
        REQUIRE(!decoded && empty && message[0]);
        decoder_rejections++;
    }
    return true;
}
/* Explicit fixture-only role wrapper for prepared serialization. The evaluator
 * body and its direct context-results return protocol are unchanged. This does
 * not turn the lowering component into ordinary authored-function lowering. */
static bool roundtrip(const facts *f, int backend, int which)
{
    module_plan = execution_module_plan_new();
    module_plan->valid = true;
    module_plan->id = UINT64_C(0x7363616c6172706c);
    for (int i = 0; i < 2; i++) {
        apush(module_plan->states, f->variables[i]);
    }
    module_plan->state_count = 2;
    execution_module_function wrapper = { .id = UINT64_C(0x7363616c6172666e),
                                          .kind = EXECUTION_FUNCTION_AUTHORED,
                                          .entry = xstrdup("scalar_evaluator_fixture"),
                                          .body = finished[which] };
    apush(module_plan->functions, wrapper);
    finished[which] = NULL;
    module_plan->function_count = 1;
    char message[192];
    behavior_backend selected = backend == 0 ? BEHAVIOR_BYTECODE : BEHAVIOR_SLJIT;
    modules[which] = execution_module_finish(&module_plan, selected, message, sizeof message);
    REQUIRE(modules[which] && !module_plan);
    size_t length = 0;
    REQUIRE(execution_prepared_write(modules[which], &serialized, &length, message, sizeof message));
    REQUIRE(length > 0 && length < 65536);
    execution_module_free(modules[which]);
    modules[which] = NULL;
    modules[which] = execution_prepared_read(serialized, length, NULL, selected, message, sizeof message);
    free(serialized);
    serialized = NULL;
    REQUIRE(modules[which] && execution_module_function_count(modules[which]) == 1);
    return true;
}
static bool run(const facts *f, int backend, int which, int vector, pin_value *output)
{
    const behavior_function *body = execution_module_function_at(modules[which], 0)
                                        .body;
    REQUIRE(body && body->argument_count == 0 && body->result_count == f->result_count);
    pin_value variables[2] = { f->vectors[vector][0], f->vectors[vector][1] };
    arena storage = { 0 };
    behavior_function_activation activation = { 0 };
    if (behavior_function_resumable(body)) {
        behavior_function_activation_init(body, &activation);
    }
    for (int i = 0; i < body->result_count; i++) {
        output[i] = body->result_defaults[i];
    }
    behavior_function_prepare_results(body, output, &storage);
    behavior_function_context context = {
        .variables = variables,
        .results = output,
        .activation = behavior_function_resumable(body) ? &activation : NULL,
        .budget = 128
    };
    behavior_function_bind_storage(&context, &storage);
    bool bound = behavior_function_bind(body, &context, NULL, 0);
    behavior_run_scope runs = { 0 };
    int status = -1;
    if (bound) {
        behavior_run_scope_begin(&runs);
        status = behavior_function_run(body, &context, backend != 0);
        behavior_run_scope_end(&runs);
        target_runs++;
    }
    bool passed = bound && status == FLOW_OK && context.returned &&
                  !runs.counts.overflow &&
                  !runs.counts.inspection_bytecode_fallbacks &&
                  (backend == 0 ? runs.counts.bytecode_entries == 1
                                : runs.counts.sljit_entries == 1 &&
                                      runs.counts.bytecode_entries == 0);
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    REQUIRE(passed);
    for (int i = 0; i < f->result_count; i++) {
        REQUIRE(same_value(output[i], f->expected[vector][i]));
    }
    return true;
}
TEST_CASE(scalar_lowering_fixed_plans_and_detached_executables_match)
{
    REQUIRE(TEST_COMPILED_BACKEND == BEHAVIOR_SLJIT);
    REQUIRE(behavior_backend_available(BEHAVIOR_SLJIT));
    corpus();
    source_package_input source = { "/compiler/scalar_lowering/Lower.chibi",
                                    SCALAR_LOWERING_CHIBI_SOURCE,
                                    sizeof SCALAR_LOWERING_CHIBI_SOURCE - 1 };
    REQUIRE(source.size < 32768 && compiler_fixture_prepare(&fixture, &source, 1));
    for (int b = 0; b < 2; b++) {
        ready_tags[b] = tag(b, "Plan", "Ready");
        rejected_tags[b] = tag(b, "Plan", "Rejected");
        REQUIRE(ready_tags[b] && rejected_tags[b]);
        for (int i = 0; i < CASES; i++) {
            pin_value request;
            REQUIRE(pack(b, &cases[i], &request));
            REQUIRE(compiler_fixture_call(
                &fixture,
                b,
                PACKAGE,
                "Lower",
                &request,
                1,
                500000,
                &outputs[b][i],
                &plans[b][i]
            ));
            authored_calls++;
            arena_free(&input);
        }
    }
    compiler_fixture_free(&fixture); /* All 16 outputs must outlive package owners. */
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < CASES; i++) {
            REQUIRE(owned(&outputs[b][i], &plans[b][i], 0));
        }
        REQUIRE(mutations(b));
        for (int i = 0; i < CASES; i++) {
            const facts *f = &cases[i];
            const pin_union *u = plans[b][i].ptr;
            if (i >= ACCEPTED) {
                REQUIRE(u->tag == rejected_tags[b] && u->count == 1);
                const pin_array *diagnostics = u->values[0].ptr;
                REQUIRE(diagnostics && diagnostics->count == 1);
                pin_value message, range;
                REQUIRE(
                    get(&diagnostics->items[0], "message", &message) &&
                    get(&diagnostics->items[0], "range", &range)
                );
                REQUIRE(
                    !strcmp(pin_text(message).bytes, i == 6 ? "scalar lowering supports only ordinary Bool and I32 types" : "unsupported scalar lowering expression kind")
                );
                REQUIRE(range.i == (i == 6 ? f->analysis.expression_count : 0));
                arena_free(&outputs[b][i]);
                continue;
            }
            char message[192];
            REQUIRE(
                scalar_lowering_decode(&plans[b][i], ready_tags[b], &holder, message, sizeof message)
            );
            REQUIRE(
                holder.variable_count == 2 &&
                holder.variable_ids[0] == f->variables[0].id &&
                holder.variable_ids[1] == f->variables[1].id
            );
            script_type results[2];
            for (int n = 0; n < f->result_count; n++) {
                results[n] = f->types[f->result_types[n]];
            }
            oracle_builder = script_lower_evaluator_reference(
                &f->analysis,
                &f->catalog,
                f->variables,
                2,
                f->result_expressions,
                results,
                f->result_count,
                location(23),
                message,
                sizeof message
            );
            REQUIRE(oracle_builder);
            REQUIRE(same_plan(
                f,
                behavior_function_builder_definition(holder.builder),
                behavior_function_builder_definition(oracle_builder)
            ));
            finished[0] = scalar_lowering_finish(&holder, message, sizeof message);
            finished[1] = behavior_function_builder_finish(oracle_builder, message, sizeof message);
            REQUIRE(finished[0] && finished[1]);
            scalar_lowering_builder_free(&holder);
            behavior_function_builder_free(oracle_builder);
            oracle_builder = NULL;
            arena_free(&outputs[b][i]); /* Plans/paths retired before loaded execution. */
            REQUIRE(roundtrip(f, b, 0) && roundtrip(f, b, 1));
            for (int vector = 0; vector < 2; vector++) {
                pin_value actual[2], expected[2];
                REQUIRE(run(f, b, 0, vector, actual) && run(f, b, 1, vector, expected));
                for (int n = 0; n < f->result_count; n++) {
                    REQUIRE(same_value(actual[n], expected[n]));
                }
            }
            execution_module_free(modules[0]);
            modules[0] = NULL;
            execution_module_free(modules[1]);
            modules[1] = NULL;
        }
    }
    REQUIRE(authored_calls == 16 && target_runs == 48 && decoder_rejections == 8);
    return true;
}
void test_language_scalar_lowering_chibi()
{
    authored_calls = target_runs = decoder_rejections = 0;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(scalar_lowering_fixed_plans_and_detached_executables_match);
    pu_clear_setup();
    cleanup();
}
