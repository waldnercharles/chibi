#include "unit_analysis_chibi.h"

#include "compiler_fixture.h"
#include "language/unit_analysis.h"
#include "lexical_binding_chibi_source.h"
#include "package_core_source.h"
#include "support/array.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"
#include "unit_algebra_chibi_source.h"
#include "unit_analysis_chibi_source.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Twenty-five fixed raw typed-IR requests and four actual analyzed programs:
 * 60 authored calls at500k steps. Five compiler sources per backend and four
 * analyzed sources give14 parses. Compiler sources <48KiB, fact sources <256B.
 * Actual caps:64expressions/32symbols/4functions/64statements/8bindings;
 * raw caps:16expressions/8symbols/2functions/4statements/1binding, except one
 * fixed81expression cast chain proving deep graphs do not inherit helper caps.
 * Both have
 * <=3capabilities/2catalogtypes/2fields/6ports/4arguments/2globals,128IDs,
 * 256ranges/32symbolindices. Outputs:81cache rows,512terms,32diagnostics.
 * No IO, host callbacks, tasks or generated combinations. The adapter packs
 * raw IR/catalog facts only; all unit decisions belong to Analyze. Diagnostics
 * compare complete range/message multisets; the adapter's final qsort and C
 * expression-argument evaluation provide no equal-position ordering contract. */
enum
{
    MAX_EXPRESSIONS = 81,
    MAX_SYMBOLS = 32,
    MAX_IDS = 128,
    MAX_RANGES = 256
};
static const char PACKAGE[] = "project:unit_analysis";
static compiler_fixture fixture;
static arena input, detached, oracle, saved_storage[2], saved_oracle[2];
static pin_value saved[2][2];
static script_unit_analysis_result saved_expected[2];
static uint64_t saved_tag[2];
static script_syntax *syntax;
static script_analysis *analysis;
static bool prepared, preparation_attempted;
static int calls;
static int selected_case = -1, selected_backend = -1;
static uint64_t ids[MAX_IDS];
static int id_count;
static script_range ranges[MAX_RANGES];
static int range_count;

static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
    arena_free(&oracle);
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
}
static pin_value integer(int n)
{
    return (pin_value){ .type = PIN_I32, .i = n };
}
static pin_value boolean(bool b)
{
    return (pin_value){ .type = PIN_BOOL, .b = b };
}
static pin_value text(const char *s)
{
    s = s ? s : "";
    pin_value result;
    const char *error = pin_value_text(&input, &result, s, strlen(s));
    if (error) {
        abort();
    }
    return result;
}
static bool put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, row->ptr, field, value));
    return true;
}
static bool rows(int backend, const char *name, int count, pin_value *out)
{
    REQUIRE(count >= 0 && count <= MAX_RANGES);
    const pin_record *shape = compiler_fixture_record(&fixture, backend, PACKAGE, name, &input);
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
        array->items[i] = (pin_value){ .type = PIN_STRUCT,
                                       .record = shape,
                                       .type_name = shape->type.name,
                                       .ptr = arena_alloc(&input, shape->type.size) };
        memset(array->items[i].ptr, 0, shape->type.size);
    }
    return true;
}
static pin_value *row(pin_value *array, int index)
{
    return &((pin_array *)array->ptr)->items[index];
}
static int slot(uint64_t id)
{
    if (!id) {
        return -1;
    }
    for (int i = 0; i < id_count; i++) {
        if (ids[i] == id) {
            return i;
        }
    }
    if (id_count == MAX_IDS) {
        return -2;
    }
    ids[id_count] = id;
    return id_count++;
}
static int location(script_range range)
{
    if (range_count == MAX_RANGES) {
        return -1;
    }
    ranges[range_count] = range;
    return range_count++;
}
static const script_catalog_variant *variant(int backend)
{
    const authoring_package *package = compiler_fixture_package(&fixture, backend, PACKAGE);
    if (!package || !package->exports || !package->exports->metadata) {
        return NULL;
    }
    const script_catalog_type *type = script_catalog_type_by_id(
        package->exports->metadata,
        compiler_fixture_export(&fixture, backend, PACKAGE, "Analysis")
    );
    for (int i = 0; type && i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, "Completed")) {
            return &type->variants[i];
        }
    }
    return NULL;
}
static bool environment(int b, const script_unit_analysis_facts *facts, pin_value *out)
{
    const script_catalog *catalog = facts->catalog;
    REQUIRE(
        catalog && facts->expression_count <= MAX_EXPRESSIONS &&
        facts->symbol_count <= MAX_SYMBOLS && facts->function_count <= 4 &&
        facts->statement_count <= 64 && facts->binding_count <= 8 &&
        catalog->capability_count <= 3 && catalog->type_count <= 2 &&
        catalog->global_count <= 2
    );
    int arguments = 0, indices = 0, ports = 0, fields = 0;
    for (int i = 0; i < facts->expression_count; i++) {
        arguments += facts->expressions[i].argument_count;
    }
    for (int i = 0; i < facts->function_count; i++) {
        indices += facts->functions[i].parameter_count + facts->functions[i].result_count;
    }
    for (int i = 0; i < catalog->capability_count; i++) {
        ports += catalog->capabilities[i].parameter_count +
                 catalog->capabilities[i].result_count;
    }
    for (int i = 0; i < catalog->type_count; i++) {
        fields += catalog->types[i].field_count;
    }
    REQUIRE(arguments <= 4 && indices <= 32 && ports <= 6 && fields <= 2);
    pin_value a[15];
    const char *names[] = { "Type",       NULL,          "Symbol",
                            "Expression", "Argument",    "Function",
                            NULL,         "Capability",  "Port",
                            "Field",      "CatalogType", "Global",
                            "Statement",  "Binding",     "Range" };
    int counts[] = { facts->expression_count,
                     0,
                     facts->symbol_count,
                     facts->expression_count,
                     arguments,
                     facts->function_count,
                     indices,
                     catalog->capability_count,
                     ports,
                     fields,
                     catalog->type_count,
                     catalog->global_count,
                     facts->statement_count,
                     facts->binding_count,
                     0 };
    memset(a, 0, sizeof a);
    for (int i = 0; i < 15; i++) {
        if (names[i] && i != 14) {
            REQUIRE(rows(b, names[i], counts[i], &a[i]));
        }
    }
    pin_array *symbol_indices = pin_array_snapshot_allocate(&input, PIN_I32, NULL, indices);
    REQUIRE(symbol_indices);
    a[6] = (pin_value){ .type = PIN_I32,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = symbol_indices };
    id_count = 0;
    range_count = 0;
    int argument = 0, index = 0, port = 0, field = 0;
    for (int i = 0; i < facts->symbol_count; i++) {
        const script_symbol *s = &facts->symbols[i];
        REQUIRE(!s->unit || strlen(s->unit) <= 240);
        pin_value *v = row(&a[2], i);
        int id = slot(s->id), range = location(s->declaration);
        REQUIRE(id >= -1 && range >= 0);
        REQUIRE(
            put(v, "id_slot", integer(id)) && put(v, "kind", integer(s->kind)) &&
            put(v, "range", integer(range)) &&
            put(v, "initializer", integer(s->initializer)) &&
            put(v, "has_unit", boolean(s->unit != NULL)) &&
            put(v, "unit", text(s->unit))
        );
    }
    for (int i = 0; i < facts->expression_count; i++) {
        const script_expression *e = &facts->expressions[i];
        pin_value *t = row(&a[0], i), *v = row(&a[3], i);
        int nominal = slot(e->type.nominal), capability = slot(e->capability),
            member = slot(e->member), offset = slot((uint64_t)e->offset),
            range = location(e->range);
        REQUIRE(nominal >= -1 && capability >= -1 && member >= -1 && offset >= -1 && range >= 0);
        REQUIRE(
            put(t, "storage", integer(e->type.storage)) &&
            put(t, "container", integer(e->type.container)) &&
            put(t, "flags", integer((int)e->type.flags)) &&
            put(t, "nominal_slot", integer(nominal))
        );
        const char *keys[] = { "kind",
                               "op",
                               "type",
                               "range",
                               "symbol",
                               "left",
                               "right",
                               "arg_start",
                               "arg_count",
                               "result_count",
                               "capability_slot",
                               "member_slot",
                               "offset_slot" };
        int values[] = { e->kind,         e->op,      i,
                         range,           e->symbol,  e->left,
                         e->right,        argument,   e->argument_count,
                         e->result_count, capability, member,
                         offset };
        for (int k = 0; k < 13; k++) {
            REQUIRE(put(v, keys[k], integer(values[k])));
        }
        REQUIRE(put(v, "task", boolean(e->task)));
        for (int j = 0; j < e->argument_count; j++) {
            const script_argument *arg = &e->arguments[j];
            pin_value *r = row(&a[4], argument++);
            int ar = location(arg->range);
            REQUIRE(ar >= 0);
            REQUIRE(
                put(r, "value", integer(arg->value)) &&
                put(r, "parameter", integer(arg->parameter)) &&
                put(r, "range", integer(ar))
            );
        }
    }
    for (int i = 0; i < facts->function_count; i++) {
        const script_function *f = &facts->functions[i];
        pin_value *v = row(&a[5], i);
        REQUIRE(
            put(v, "param_start", integer(index)) &&
            put(v, "param_count", integer(f->parameter_count))
        );
        for (int j = 0; j < f->parameter_count; j++) {
            symbol_indices->items[index++] = integer(f->parameters[j]);
        }
        REQUIRE(
            put(v, "result_start", integer(index)) &&
            put(v, "result_count", integer(f->result_count))
        );
        for (int j = 0; j < f->result_count; j++) {
            symbol_indices->items[index++] = integer(f->results[j]);
        }
    }
    for (int i = 0; i < catalog->capability_count; i++) {
        const script_catalog_capability *c = &catalog->capabilities[i];
        pin_value *v = row(&a[7], i);
        int id = slot(c->id);
        REQUIRE(id >= -1);
        REQUIRE(
            put(v, "id_slot", integer(id)) && put(v, "kind", integer(c->kind)) &&
            put(v, "param_start", integer(port)) &&
            put(v, "param_count", integer(c->parameter_count)) &&
            put(v, "result_start", integer(port + c->parameter_count)) &&
            put(v, "result_count", integer(c->result_count)) &&
            put(v, "event_sender", boolean(c->event_sender)) &&
            put(v, "variadic", boolean(c->variadic))
        );
        for (int j = 0; j < c->parameter_count + c->result_count; j++) {
            const script_catalog_parameter *p = j < c->parameter_count
                                                  ? &c->parameters[j]
                                                  : &c->results[j - c->parameter_count];
            REQUIRE(
                (!p->name || strlen(p->name) <= 240) &&
                (!p->unit || strlen(p->unit) <= 240)
            );
            pin_value *vport = row(&a[8], port++);
            REQUIRE(
                put(vport, "name", text(p->name)) &&
                put(vport, "unit", text(p->unit)) &&
                put(vport, "has_unit", boolean(p->unit != NULL))
            );
        }
    }
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *t = &catalog->types[i];
        pin_value *v = row(&a[10], i);
        int id = slot(t->id);
        REQUIRE(id >= -1);
        REQUIRE(
            put(v, "id_slot", integer(id)) && put(v, "field_start", integer(field)) &&
            put(v, "field_count", integer(t->field_count))
        );
        for (int j = 0; j < t->field_count; j++) {
            const script_catalog_field *f = &t->fields[j];
            REQUIRE(!f->unit || strlen(f->unit) <= 240);
            pin_value *vf = row(&a[9], field++);
            int id = slot(f->id);
            REQUIRE(id >= -1);
            REQUIRE(
                put(vf, "id_slot", integer(id)) &&
                put(vf, "has_unit", boolean(f->unit != NULL)) &&
                put(vf, "unit", text(f->unit))
            );
        }
    }
    for (int i = 0; i < catalog->global_count; i++) {
        const script_catalog_global *g = &catalog->globals[i];
        REQUIRE(!g->unit || strlen(g->unit) <= 240);
        pin_value *v = row(&a[11], i);
        int id = slot(g->id);
        REQUIRE(id >= -1);
        REQUIRE(
            put(v, "id_slot", integer(id)) &&
            put(v, "has_unit", boolean(g->unit != NULL)) &&
            put(v, "unit", text(g->unit))
        );
    }
    for (int i = 0; i < facts->statement_count; i++) {
        const script_statement *s = &facts->statements[i];
        pin_value *v = row(&a[12], i);
        int range = location(s->range);
        REQUIRE(range >= 0);
        REQUIRE(
            put(v, "kind", integer(s->kind)) &&
            put(v, "range", integer(range)) && put(v, "op", integer(s->op)) &&
            put(v, "expression", integer(s->expression)) &&
            put(v, "symbol", integer(s->symbol)) && put(v, "second", integer(s->second))
        );
    }
    for (int i = 0; i < facts->binding_count; i++) {
        const script_binding *binding = &facts->bindings[i];
        pin_value *v = row(&a[13], i);
        int range = location(binding->range);
        REQUIRE(range >= 0);
        REQUIRE(
            put(v, "target", integer(binding->target)) &&
            put(v, "value", integer(binding->value)) && put(v, "range", integer(range))
        );
    }
    REQUIRE(rows(b, "Range", range_count, &a[14]));
    for (int i = 0; i < range_count; i++) {
        pin_value *v = row(&a[14], i);
        script_range r = ranges[i];
        REQUIRE(
            put(v, "start", integer(r.start)) && put(v, "end", integer(r.end)) &&
            put(v, "line", integer(r.line)) && put(v, "column", integer(r.column)) &&
            put(v, "document", integer(r.document))
        );
    }
    pin_array *numbers = pin_array_snapshot_allocate(&input, PIN_U64, NULL, id_count);
    REQUIRE(numbers);
    a[1] = (pin_value){ .type = PIN_U64, .container = PIN_CONTAINER_ARRAY, .ptr = numbers };
    for (int i = 0; i < id_count; i++) {
        numbers->items[i] = (pin_value){ .type = PIN_U64, .u64 = ids[i] };
    }
    const authoring_package *package = compiler_fixture_package(&fixture, b, PACKAGE);
    REQUIRE(package && package->exports && package->exports->metadata);
    const script_catalog_type *type = script_catalog_type_by_id(
        package->exports->metadata,
        compiler_fixture_export(&fixture, b, PACKAGE, "Environment")
    );
    REQUIRE(type && type->variant_count == 1 && !strcmp(type->variants[0].name, "Tables"));
    REQUIRE(!compiler_fixture_union(&input, type->variants[0].tag, a, 15, out));
    return true;
}
static bool get(const pin_value *value, const char *name, pin_value *out)
{
    REQUIRE(value->type == PIN_STRUCT && value->ptr && value->record);
    const type_field *field = pin_type_field_path(pin_value_type(value), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&input, value->ptr, field, out));
    return true;
}
static bool int_field(const pin_value *v, const char *name, int *out)
{
    pin_value field;
    REQUIRE(get(v, name, &field) && field.type == PIN_I32);
    *out = field.i;
    return true;
}
static bool same_int(const pin_value *v, const char *name, int expected)
{
    int actual;
    REQUIRE(int_field(v, name, &actual) && actual == expected);
    return true;
}
static bool string_field(const pin_value *v, const char *name, const char **out)
{
    pin_value field;
    REQUIRE(get(v, name, &field) && field.type == PIN_STR && pin_text(field).bytes);
    *out = pin_text(field).bytes;
    return true;
}
static bool array(const pin_value *value, int count, const pin_array **out)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_ARRAY);
    *out = value->ptr;
    REQUIRE(
        count >= 0 && ((!*out && !count) || (*out && (*out)->count == count &&
                                             (!count || (*out)->items)))
    );
    return true;
}
static bool diagnostic(const pin_value *value, script_diagnostic *out)
{
    const char *message;
    REQUIRE(
        int_field(value, "start", &out->range.start) &&
        int_field(value, "end", &out->range.end) &&
        int_field(value, "line", &out->range.line) &&
        int_field(value, "column", &out->range.column) &&
        int_field(value, "document", &out->range.document) &&
        string_field(value, "message", &message)
    );
    REQUIRE(strlen(message) < sizeof out->message);
    strcpy(out->message, message);
    return true;
}
static bool equal_diagnostic(const script_diagnostic *a, const script_diagnostic *b)
{
    return a->range.start == b->range.start && a->range.end == b->range.end &&
           a->range.line == b->range.line && a->range.column == b->range.column &&
           a->range.document == b->range.document && !strcmp(a->message, b->message);
}
static bool result_value(const pin_value *value, uint64_t tag, const script_unit_analysis_result *expected)
{
    REQUIRE(value->type == PIN_UNION && value->ptr);
    const pin_union *payload = value->ptr;
    REQUIRE(payload->tag == tag && payload->count == 3);
    const pin_array *cache, *terms, *diagnostics;
    if (selected_case == 7) {
        REQUIRE(array(&payload->values[0], 3, &cache));
        REQUIRE(
            payload->values[2].type == PIN_STRUCT && payload->values[2].container == PIN_CONTAINER_ARRAY
        );
        const pin_array *observed = payload->values[2].ptr;
        int count = observed ? observed->count : 0, kind;
        REQUIRE(count >= 0 && count <= 32 && int_field(&cache->items[2], "kind", &kind));
        fprintf(
            stderr,
            "unit analysis zero-ID observation: backend=%s diagnostics=%d "
            "result=%s\n",
            selected_backend == 0 ? "bytecode" : "SLJIT",
            count,
            kind == SCRIPT_UNIT_UNKNOWN ? "unknown"
            : kind == SCRIPT_UNIT_KNOWN ? "known"
                                        : "other"
        );
    }
    REQUIRE(
        array(&payload->values[0], expected->unit_count, &cache) &&
        array(&payload->values[2], expected->diagnostic_count, &diagnostics)
    );
    REQUIRE(
        payload->values[1].type == PIN_STRUCT && payload->values[1].container == PIN_CONTAINER_ARRAY
    );
    terms = payload->values[1].ptr;
    int term_count = terms ? terms->count : 0;
    REQUIRE(term_count >= 0 && term_count <= 512 && (!term_count || terms->items));
    for (int i = 0; i < expected->unit_count; i++) {
        const script_unit *unit = &expected->units[i];
        const pin_value *entry = &cache->items[i];
        int start, count;
        REQUIRE(
            same_int(entry, "kind", unit->kind) &&
            int_field(entry, "term_start", &start) &&
            int_field(entry, "term_count", &count) && count == unit->count &&
            start >= 0 && start <= term_count - count
        );
        for (int j = 0; j < count; j++) {
            const char *name;
            int power;
            const pin_value *term = &terms->items[start + j];
            const script_unit_term *wanted = &unit->terms[j];
            REQUIRE(string_field(term, "name", &name) && int_field(term, "power", &power));
            REQUIRE(
                power == wanted->power && strlen(name) <= 240 &&
                strlen(name) == (size_t)wanted->length &&
                !memcmp(name, wanted->name, (size_t)wanted->length)
            );
        }
    }

    bool found[32] = { false };
    for (int i = 0; i < expected->diagnostic_count; i++) {
        script_diagnostic actual;
        REQUIRE(diagnostic(&diagnostics->items[i], &actual));
        int match = -1;
        for (int j = 0; j < expected->diagnostic_count; j++) {
            if (!found[j] && equal_diagnostic(&actual, &expected->diagnostics[j])) {
                match = j;
                break;
            }
        }
        REQUIRE(match >= 0);
        found[match] = true;
    }
    return true;
}
static bool owned(arena *owner, const pin_value *value, int depth)
{
    REQUIRE(depth <= 5);
    if (value->container == PIN_CONTAINER_ARRAY) {
        if (!value->ptr) {
            return true;
        }
        const pin_array *a = value->ptr;
        REQUIRE(
            arena_contains(owner, a) && a->count >= 0 && a->count <= 512 &&
            (!a->count || (a->items && arena_contains(owner, a->items)))
        );
        for (int i = 0; i < a->count; i++) {
            REQUIRE(owned(owner, &a->items[i], depth + 1));
        }
        return true;
    }
    if (value->type == PIN_UNION) {
        const pin_union *u = value->ptr;
        REQUIRE(u && arena_contains(owner, u) && u->count == 3);
        for (int i = 0; i < u->count; i++) {
            REQUIRE(owned(owner, &u->values[i], depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STRUCT) {
        REQUIRE(
            value->ptr && value->record && arena_contains(owner, value->ptr) &&
            arena_contains(owner, value->record)
        );
        const type_desc *type = pin_value_type(value);
        REQUIRE(
            type && type->name && arena_contains(owner, type->name) &&
            type->field_count <= 6 &&
            (!type->field_count || (type->fields && arena_contains(owner, type->fields)))
        );
        for (int i = 0; i < type->field_count; i++) {
            pin_value field;
            REQUIRE(!pin_read_field(owner, value->ptr, &type->fields[i], &field));
            REQUIRE(owned(owner, &field, depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STR) {
        REQUIRE(pin_text(*value).bytes && arena_contains(owner, pin_text(*value).bytes));
        return true;
    }
    REQUIRE(value->type == PIN_I32);
    return true;
}
static bool compare(const script_unit_analysis_facts *facts, int retain, script_unit_analysis_result *out)
{
    arena_free(&oracle);
    script_unit_analysis_result expected = script_unit_analysis(&oracle, facts);
    REQUIRE(
        expected.unit_count == facts->expression_count &&
        expected.unit_count <= MAX_EXPRESSIONS &&
        expected.diagnostic_count >= 0 && expected.diagnostic_count <= 32
    );
    for (int i = 0; i < expected.unit_count; i++) {
        for (int j = 0; j < expected.units[i].count; j++) {
            REQUIRE(arena_contains(&oracle, expected.units[i].terms[j].name));
        }
    }
    /* Establish the independent C witness before the authored red assertion. */
    if (selected_case == 7) {
        REQUIRE(expected.unit_count == 3 && expected.diagnostic_count == 1);
        REQUIRE(!strcmp(expected.diagnostics[0].message, "Units differ: u argument s"));
        const script_range *range = &expected.diagnostics[0].range;
        REQUIRE(
            range->start == 300 && range->end == 303 && range->line == 43 &&
            range->column == 42 && range->document == 1
        );
        REQUIRE(expected.units[2].kind == SCRIPT_UNIT_KNOWN && expected.units[2].count == 1);
        const script_unit_term *term = &expected.units[2].terms[0];
        REQUIRE(term->length == 1 && term->name[0] == 's' && term->power == 1);
    }
    if (selected_case == 24) {
        REQUIRE(expected.unit_count == 81 && expected.diagnostic_count == 0);
        for (int i = 0; i < expected.unit_count; i++) {
            REQUIRE(
                expected.units[i].kind == SCRIPT_UNIT_KNOWN && expected.units[i].count == 1
            );
            const script_unit_term *term = &expected.units[i].terms[0];
            REQUIRE(term->length == 1 && term->name[0] == 'u' && term->power == 1);
        }
    }
    int first = selected_backend < 0 ? 0 : selected_backend;
    int last = selected_backend < 0 ? 2 : selected_backend + 1;
    for (int b = first; b < last; b++) {
        arena_free(&input);
        arena_free(&detached);
        pin_value argument, result;
        REQUIRE(environment(b, facts, &argument));
        const script_catalog_variant *v = variant(b);
        REQUIRE(v);
        uint64_t tag = v->tag;
        if (selected_case >= 0) {
            fprintf(
                stderr,
                "unit analysis regression case %d backend %s: C witness "
                "passed\n",
                selected_case,
                b == 0 ? "bytecode" : "SLJIT"
            );
        }
        calls++;
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, "Analyze", &argument, 1, 500000, &detached, &result)
        );
        arena_free(&input);
        REQUIRE(owned(&detached, &result, 0) && result_value(&result, tag, &expected));
        if (retain >= 0) {
            REQUIRE(!compiler_fixture_snapshot(&saved_storage[b], &saved[b][retain], &result));
            saved_tag[b] = tag;
        }
    }
    if (retain >= 0) {
        saved_expected[retain] = expected;
        saved_oracle[retain] = oracle;
        oracle = (arena){ 0 };
    }
    if (out) {
        *out = expected;
    }
    return true;
}
static bool prepare()
{
    source_package_input files[] = {
        { "/compiler/unit_analysis/Analysis.chibi",
          UNIT_ANALYSIS_CHIBI_SOURCE,
          sizeof UNIT_ANALYSIS_CHIBI_SOURCE - 1 },
        { "/compiler/unit_algebra/Units.chibi",
          UNIT_ALGEBRA_CHIBI_SOURCE,
          sizeof UNIT_ALGEBRA_CHIBI_SOURCE - 1 },
        { "/compiler/syntax_diagnostics/Diagnostics.chibi",
          SYNTAX_CHIBI_SOURCE,
          sizeof SYNTAX_CHIBI_SOURCE - 1 },
        { "/compiler/lexical_binding/Binding.chibi",
          LEXICAL_BINDING_CHIBI_SOURCE,
          sizeof LEXICAL_BINDING_CHIBI_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi", CORE_STRINGS_SOURCE, sizeof CORE_STRINGS_SOURCE - 1 }
    };
    for (int i = 0; i < 5; i++) {
        REQUIRE(files[i].size < 49152);
    }
    prepared = compiler_fixture_prepare(&fixture, files, 5);
    REQUIRE(prepared);
    return true;
}
typedef struct
{
    script_unit_analysis_facts facts;
    script_catalog catalog;
    script_symbol symbols[8];
    script_expression expressions[MAX_EXPRESSIONS];
    script_function functions[2];
    script_statement statements[4];
    script_binding bindings[1];
    script_catalog_capability capabilities[3];
    script_catalog_type types[2];
    script_catalog_field fields[2];
    script_catalog_global globals[2];
    script_catalog_parameter parameters[3], results[3];
    script_argument arguments[4];
    int function_parameters[2], function_results[2];
} raw_facts;
static script_range range_at(int i)
{
    return (script_range){ 20 + i * 7, 23 + i * 7, 3 + i, 2 + i, i % 3 };
}
static int add_symbol(raw_facts *r, script_symbol_kind kind, const char *unit, int initializer)
{
    int i = r->facts.symbol_count++;
    r->symbols[i] = (script_symbol){ .id = UINT64_MAX - 100 - (uint64_t)i,
                                     .name = "Fact",
                                     .unit = (char *)unit,
                                     .kind = kind,
                                     .type = { .storage = PIN_F64 },
                                     .declaration = range_at(i + 20),
                                     .initializer = initializer };
    return i;
}
static int expression(raw_facts *r, script_expression_kind kind, int left, int right, int symbol)
{
    int i = r->facts.expression_count++;
    r->expressions[i] = (script_expression){ .kind = kind,
                                             .left = left,
                                             .right = right,
                                             .symbol = symbol,
                                             .type = { .storage = PIN_F64 },
                                             .range = range_at(i) };
    return i;
}
static int binary(raw_facts *r, script_operator op, int left, int right)
{
    int i = expression(r, SCRIPT_EXPR_BINARY, left, right, -1);
    r->expressions[i].op = op;
    return i;
}
static void native(raw_facts *r)
{
    r->parameters[0] = (script_catalog_parameter){ .name = "Amount",
                                                   .unit = "u",
                                                   .type = { .storage = PIN_F64 } };
    r->results[0] = (script_catalog_parameter){ .name = "Out",
                                                .unit = "s",
                                                .type = { .storage = PIN_F64 } };
    r->capabilities[0] = (script_catalog_capability){ .id = UINT64_MAX - 500,
                                                      .name = "Native",
                                                      .kind = SCRIPT_CAPABILITY_FUNCTION,
                                                      .parameters = r->parameters,
                                                      .parameter_count = 1,
                                                      .results = r->results,
                                                      .result_count = 1,
                                                      .pure = true };
    r->catalog.capability_count = 1;
}
static int call(raw_facts *r)
{
    int i = expression(r, SCRIPT_EXPR_CALL, -1, -1, -1);
    r->expressions[i].capability = r->capabilities[0].id;
    r->expressions[i].result_count = 1;
    return i;
}
static void argument(raw_facts *r, int owner, int index, int value, int parameter)
{
    r->arguments[index] = (script_argument){ .value = value,
                                             .parameter = parameter,
                                             .range = range_at(40 + index) };
    r->expressions[owner].arguments = &r->arguments[index];
    r->expressions[owner].argument_count = 1;
}
static void reflected(raw_facts *r, uint64_t id, const char *unit)
{
    r->fields[0] = (script_catalog_field){ .id = UINT64_MAX - 900,
                                           .name = "Value",
                                           .unit = unit,
                                           .type = { .storage = PIN_F64 } };
    r->types[0] = (script_catalog_type){ .id = id,
                                         .name = "Row",
                                         .fields = r->fields,
                                         .field_count = 1,
                                         .type = { .storage = PIN_STRUCT,
                                                   .nominal = id } };
    r->catalog.type_count = 1;
}
static int member(raw_facts *r, int receiver, uint64_t id, size_t offset)
{
    int i = expression(r, SCRIPT_EXPR_MEMBER, receiver, -1, -1);
    r->expressions[i].member = id;
    r->expressions[i].offset = offset;
    return i;
}
static void assignment(raw_facts *r, int i, int target, int value, script_operator op)
{
    r->statements[i] = (script_statement){ .kind = SCRIPT_STMT_ASSIGN,
                                           .range = range_at(60 + i),
                                           .expression = target,
                                           .symbol = -1,
                                           .second = value,
                                           .op = op };
    r->facts.statement_count = i + 1;
}
static void raw_init(raw_facts *r)
{
    memset(r, 0, sizeof *r);
    r->catalog = (script_catalog){ .capabilities = r->capabilities,
                                   .types = r->types,
                                   .globals = r->globals };
    r->facts = (script_unit_analysis_facts){
        &r->catalog, r->symbols,    0, r->expressions, 0, r->functions,
        0,           r->statements, 0, r->bindings,    0
    };
    add_symbol(r, SCRIPT_PARAMETER, "u", -1);
    add_symbol(r, SCRIPT_PARAMETER, "s", -1);
    expression(r, SCRIPT_EXPR_SYMBOL, -1, -1, 0);
    expression(r, SCRIPT_EXPR_SYMBOL, -1, -1, 1);
}
TEST_CASE(unit_analysis_raw_ir_and_catalog_match)
{
    if (!preparation_attempted) {
        preparation_attempted = true;
        REQUIRE(prepare());
    }
    REQUIRE(prepared);
    char invalid[241];
    memset(invalid, 'x', 240);
    invalid[176] = (char)0xc3;
    invalid[177] = (char)0xa9;
    invalid[240] = 0;
    int first = selected_case < 0 ? 0 : selected_case;
    int last = selected_case < 0 ? 25 : selected_case + 1;
    for (int c = first; c < last; c++) {
        raw_facts r;
        raw_init(&r);
        switch (c) {
            case 0: {
                expression(&r, SCRIPT_EXPR_CONSTANT, -1, -1, -1);
                int string = expression(&r, SCRIPT_EXPR_CONSTANT, -1, -1, -1);
                r.expressions[string].type.storage = PIN_STR;
                int neg = expression(&r, SCRIPT_EXPR_UNARY, 0, -1, -1);
                r.expressions[neg].op = SCRIPT_OP_NEG;
                int not = expression(&r, SCRIPT_EXPR_UNARY, 0, -1, -1);
                r.expressions[not].op = SCRIPT_OP_NOT;
                expression(&r, SCRIPT_EXPR_CAST, 0, -1, -1);
                expression(&r, SCRIPT_EXPR_UNWRAP, 0, -1, -1);
                expression(&r, SCRIPT_EXPR_AS, 0, -1, -1);
                int array = expression(&r, SCRIPT_EXPR_CONSTANT, -1, -1, -1);
                r.expressions[array].type.container = PIN_CONTAINER_ARRAY;
                int optional = expression(&r, SCRIPT_EXPR_CONSTANT, -1, -1, -1);
                r.expressions[optional].type.flags = SCRIPT_TYPE_OPTIONAL;
                break;
            }
            case 1: {
                int global = add_symbol(&r, SCRIPT_GLOBAL, NULL, -1);
                r.globals[0] = (script_catalog_global){ .id = r.symbols[global].id,
                                                        .name = "World",
                                                        .unit = "s" };
                r.catalog.global_count = 1;
                int g = expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, global);
                int local = add_symbol(&r, SCRIPT_LOCAL, NULL, g);
                expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, local);
                int literal = expression(&r, SCRIPT_EXPR_CONSTANT, -1, -1, -1);
                int unknown = add_symbol(&r, SCRIPT_LOCAL, NULL, literal);
                expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, unknown);
                int explicit = add_symbol(&r, SCRIPT_GLOBAL, "u", -1);
                r.symbols[explicit].id = r.symbols[global].id;
                expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, explicit);
                break;
            }
            case 2: {
                int local = add_symbol(&r, SCRIPT_LOCAL, NULL, -1);
                expression(&r, SCRIPT_EXPR_IS, 0, -1, local);
                expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, local);
                int cyclic = add_symbol(&r, SCRIPT_LOCAL, NULL, 4);
                expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, cyclic);
                expression(&r, SCRIPT_EXPR_CAST, 4, -1, -1);
                break;
            }
            case 3:
                binary(&r, SCRIPT_OP_ADD, 0, 1);
                binary(&r, SCRIPT_OP_EQ, 0, 1);
                binary(&r, SCRIPT_OP_SUB, 0, 0);
                binary(&r, SCRIPT_OP_MOD, 0, 0);
                break;
            case 4: {
                int dims = add_symbol(&r, SCRIPT_PARAMETER, "a·b·c·d·e·f·g·h", -1);
                int extra = add_symbol(&r, SCRIPT_PARAMETER, "i", -1),
                    speed = add_symbol(&r, SCRIPT_PARAMETER, "u/s", -1);
                int a = expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, dims),
                    b = expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, extra),
                    v = expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, speed);
                binary(&r, SCRIPT_OP_MUL, a, b);
                binary(&r, SCRIPT_OP_MUL, v, 1);
                int literal = expression(&r, SCRIPT_EXPR_CONSTANT, -1, -1, -1);
                binary(&r, SCRIPT_OP_MUL, 0, literal);
                binary(&r, SCRIPT_OP_DIV, v, 1);
                break;
            }
            case 5: {
                int fallback = expression(&r, SCRIPT_EXPR_FALLBACK, 0, -1, -1);
                argument(&r, fallback, 0, 0, 0);
                r.arguments[1] = (script_argument){ .value = 1,
                                                    .parameter = 1,
                                                    .range = range_at(40) };
                r.expressions[fallback].argument_count = 2;
                expression(&r, SCRIPT_EXPR_CAST, fallback, -1, -1);
                break;
            }
            case 6: {
                int symbol = add_symbol(&r, SCRIPT_PARAMETER, invalid, -1);
                expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, symbol);
                break;
            }
            case 7: {
                native(&r);
                /* Raw catalog lookup includes a row whose ID is zero. */
                r.capabilities[0].id = 0;
                int invoke = call(&r);
                argument(&r, invoke, 0, 1, 0);
                break;
            }
            case 8: {
                native(&r);
                r.capabilities[0].variadic = true;
                int invoke = call(&r);
                argument(&r, invoke, 0, 1, 3);
                break;
            }
            case 9: {
                native(&r);
                r.capabilities[0].variadic = true;
                r.capabilities[0].event_sender = true;
                r.parameters[1] = (script_catalog_parameter){
                    .name = "EventAmount",
                    .unit = "s"
                };
                r.capabilities[1] = (script_catalog_capability){
                    .id = UINT64_MAX - 501,
                    .name = "Event",
                    .kind = SCRIPT_CAPABILITY_EVENT,
                    .parameters = &r.parameters[1],
                    .parameter_count = 1
                };
                r.catalog.capability_count = 2;
                int invoke = call(&r);
                r.expressions[invoke].member = r.capabilities[1].id;
                argument(&r, invoke, 0, 0, 1);
                break;
            }
            case 10: {
                native(&r);
                r.capabilities[0].variadic = true;
                r.capabilities[0].event_sender = true;
                int invoke = call(&r);
                r.expressions[invoke].member = UINT64_MAX - 502;
                argument(&r, invoke, 0, 1, 1);
                break;
            }
            case 11: {
                native(&r);
                r.parameters[0].unit = "s";
                int result = add_symbol(&r, SCRIPT_RESULT, "m", -1);
                r.function_parameters[0] = 0;
                r.function_results[0] = result;
                r.functions[0] = (script_function){ .parameters = r.function_parameters,
                                                    .parameter_count = 1,
                                                    .results = r.function_results,
                                                    .result_count = 1 };
                r.facts.function_count = 1;
                int invoke = call(&r);
                r.expressions[invoke].right = 0;
                argument(&r, invoke, 0, 1, 0);
                break;
            }
            case 12: {
                native(&r);
                int invoke = call(&r);
                r.expressions[invoke].task = true;
                r.expressions[invoke].type.nominal = r.capabilities[0].id;
                int join = expression(&r, SCRIPT_EXPR_JOIN, invoke, -1, -1);
                r.expressions[join].result_count = 1;
                int result = add_symbol(&r, SCRIPT_RESULT, "m", -1);
                r.function_results[0] = result;
                r.functions[0] = (script_function){ .results = r.function_results,
                                                    .result_count = 1 };
                r.facts.function_count = 1;
                int authored = expression(&r, SCRIPT_EXPR_JOIN, invoke, 0, -1);
                r.expressions[authored].result_count = 1;
                break;
            }
            case 13: {
                native(&r);
                int result = add_symbol(&r, SCRIPT_RESULT, "u", -1);
                r.symbols[result].id = script_catalog_id("port", "Out");
                int invoke = call(&r);
                member(&r, invoke, r.symbols[result].id, 0);
                break;
            }
            case 14: {
                native(&r);
                int invoke = call(&r),
                    unwrap = expression(&r, SCRIPT_EXPR_UNWRAP, invoke, -1, -1);
                member(&r, unwrap, script_catalog_id("port", "Out"), 0);
                break;
            }
            case 15: {
                native(&r);
                r.capabilities[0].kind = SCRIPT_CAPABILITY_EVENT;
                r.parameters[0].unit = "s";
                reflected(&r, r.capabilities[0].id, "kg");
                int receiver = expression(&r, SCRIPT_EXPR_CONSTANT, -1, -1, -1);
                r.expressions[receiver].type = (script_type){
                    .storage = PIN_STRUCT,
                    .nominal = r.capabilities[0].id,
                    .flags = SCRIPT_TYPE_RESULTS
                };
                member(&r, receiver, r.fields[0].id, 0);
                member(&r, receiver, r.fields[0].id, SIZE_MAX);
                break;
            }
            case 16: {
                reflected(&r, UINT64_MAX - 700, "kg");
                int receiver = expression(&r, SCRIPT_EXPR_CONSTANT, -1, -1, -1);
                r.expressions[receiver].type = r.types[0].type;
                member(&r, receiver, r.fields[0].id, 0);
                member(&r, receiver, UINT64_MAX - 901, 0);
                break;
            }
            case 17: {
                reflected(&r, UINT64_MAX - 700, "u");
                int value = expression(&r, SCRIPT_EXPR_STRUCT, -1, -1, -1);
                r.expressions[value].type = r.types[0].type;
                argument(&r, value, 0, 1, 0);
                break;
            }
            case 18:
                assignment(&r, 0, 0, 1, SCRIPT_OP_ADD);
                assignment(&r, 1, 0, 1, SCRIPT_OP_MUL);
                assignment(&r, 2, -1, 1, SCRIPT_OP_NONE);
                r.statements[2].kind = SCRIPT_STMT_REDUCE;
                r.statements[2].symbol = 0;
                r.bindings[0] = (script_binding){ .target = 0,
                                                  .value = 1,
                                                  .range = range_at(65) };
                r.facts.binding_count = 1;
                break;
            case 19: {
                int a = add_symbol(&r, SCRIPT_PARAMETER, "a·b·c·d·e·f·g·h", -1),
                    b = add_symbol(&r, SCRIPT_PARAMETER, "i", -1);
                int left = expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, a),
                    right = expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, b);
                assignment(&r, 0, left, right, SCRIPT_OP_MUL);
                break;
            }
            case 20: {
                native(&r);
                r.results[1] = (script_catalog_parameter){ .name = "Other",
                                                           .unit = "m" };
                r.capabilities[0].result_count = 2;
                int invoke = call(&r);
                r.expressions[invoke].result_count = 2;
                member(&r, invoke, script_catalog_id("port", "Other"), 1);
                break;
            }
            case 21:
                for (int k = 0; k < 3; k++) {
                    script_symbol_kind kind[] = { SCRIPT_STATE,
                                                  SCRIPT_SHARED_STATE,
                                                  SCRIPT_CONSTANT };
                    int s = add_symbol(&r, kind[k], NULL, 0);
                    expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, s);
                }
                {
                    int missing = add_symbol(&r, SCRIPT_GLOBAL, NULL, -1);
                    expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, missing);
                }
                break;
            case 22: {
                native(&r);
                r.capabilities[0].parameter_count = 0;
                r.capabilities[0].variadic = true;
                int invoke = call(&r);
                r.expressions[invoke].result_count = 0;
                argument(&r, invoke, 0, 0, 1);
                r.arguments[1] = (script_argument){ .value = 0,
                                                    .parameter = -1,
                                                    .range = range_at(41) };
                r.expressions[invoke].argument_count = 2;
                break;
            }
            case 23: {
                native(&r);
                r.results[0].unit = "m";
                r.functions[0] = (script_function){ 0 };
                r.facts.function_count = 1;
                int invoke = call(&r);
                r.expressions[invoke].right = 0;
                break;
            }
            case 24: {
                /* A fixed forward chain exceeds total call depth64 in the
                 * recursive authored implementation, even with SCC limits0:
                 * its metadata helpers still have the ordinary default64. */
                int symbol = add_symbol(&r, SCRIPT_PARAMETER, "u", -1);
                r.facts.expression_count = 0;
                for (int i = 0; i < 80; i++) {
                    expression(&r, SCRIPT_EXPR_CAST, i + 1, -1, -1);
                }
                expression(&r, SCRIPT_EXPR_SYMBOL, -1, -1, symbol);
                break;
            }
        }
        REQUIRE(
            r.facts.expression_count <= (c == 24 ? 81 : 16) &&
            r.facts.symbol_count <= 8 && r.facts.function_count <= 2 &&
            r.facts.statement_count <= 4 && r.facts.binding_count <= 1
        );
        script_unit_analysis_result expected;
        REQUIRE(compare(&r.facts, c == 6 ? 0 : -1, &expected));
        const int diagnostics[] = { 0, 0, 0, 2, 1, 1, 2, 1, 1, 1, 0, 1, 0,
                                    0, 0, 0, 0, 1, 4, 2, 0, 0, 0, 0, 0 };
        REQUIRE(expected.diagnostic_count == diagnostics[c]);
        if (c == 2) {
            REQUIRE(
                expected.units[3].kind == SCRIPT_UNIT_KNOWN &&
                expected.units[4].kind == SCRIPT_UNIT_UNKNOWN
            );
        }
        if (c == 6) {
            REQUIRE(
                strlen(expected.diagnostics[0].message) == 190 &&
                strlen(expected.diagnostics[1].message) == 190
            );
        }
        if (c == 7) {
            REQUIRE(
                expected.diagnostic_count == 1 &&
                !strcmp(expected.diagnostics[0].message, "Units differ: u argument s")
            );
            REQUIRE(
                expected.units[2].kind == SCRIPT_UNIT_KNOWN && expected.units[2].count == 1
            );
            const script_unit_term *term = &expected.units[2].terms[0];
            REQUIRE(term->length == 1 && term->name[0] == 's' && term->power == 1);
        }
        if (c == 10 || c == 14) {
            REQUIRE(expected.units[2].kind == SCRIPT_UNIT_KNOWN);
        }
        if (c == 12 || c == 20 || c == 23) {
            REQUIRE(expected.units[2].kind == SCRIPT_UNIT_UNKNOWN);
        }
        if (c == 15) {
            REQUIRE(
                expected.units[3].count == 1 && expected.units[4].count == 1 &&
                !strcmp(expected.units[3].terms[0].name, "s") &&
                !strcmp(expected.units[4].terms[0].name, "kg")
            );
        }
        if (c == 19) {
            REQUIRE(
                expected.diagnostics[0].range.start ==
                expected.diagnostics[1].range.start
            );
        }
        if (c == 24) {
            REQUIRE(expected.unit_count == 81);
            for (int i = 0; i < expected.unit_count; i++) {
                REQUIRE(
                    expected.units[i].kind == SCRIPT_UNIT_KNOWN &&
                    expected.units[i].count == 1
                );
                const script_unit_term *term = &expected.units[i].terms[0];
                REQUIRE(term->length == 1 && term->name[0] == 'u' && term->power == 1);
            }
        }
    }
    memset(invalid, 'z', 240);
    if (selected_case < 0) {
        REQUIRE(calls == 50);
    }
    return true;
}
TEST_CASE(unit_analysis_actual_analyzer_facts_match)
{
    REQUIRE(prepared);
    const char *sources[] = {
        "M :: library { Run :: (x: u, dt: s) -> u { return x + dt; } }",
        "M :: library { Run :: (x: u, dt: s) -> u { value := x; value *= dt; "
        "return value; } }",
        "M :: library { Run :: (x: s) -> number { return Sum(x, x); } }",
        "M :: library { Run :: (x: s) -> s { row: Row = {Value: x}; return "
        "row.Value; } }",
        "M :: library { Run :: (x: u, dt: s) -> u { q := query(amount := x); "
        "for row in q { return row.amount + dt; } return x; } }"
    };
    for (int c = 0; c < 5; c++) {
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(strlen(sources[c]) < 256);
        syntax = script_parse(sources[c]);
        if (syntax && syntax->diagnostic_count) {
            fprintf(
                stderr,
                "unit fact source %d: %s\n",
                c,
                syntax->diagnostics[0].message
            );
        }
        REQUIRE(syntax && !syntax->diagnostic_count);
        script_catalog_parameter parameter = { .name = "Value",
                                               .type = { .storage = PIN_F64 },
                                               .unit = "u" };
        script_catalog_parameter result = { .name = "Out",
                                            .type = { .storage = PIN_F64 },
                                            .unit = "u" };
        script_catalog_capability sum = { .id = 70001,
                                          .name = "Sum",
                                          .kind = SCRIPT_CAPABILITY_FUNCTION,
                                          .pure = true,
                                          .variadic = true,
                                          .parameters = &parameter,
                                          .parameter_count = 1,
                                          .results = &result,
                                          .result_count = 1 };
        script_catalog_field native_field = { .id = 70003,
                                              .name = "Value",
                                              .type = { .storage = PIN_F64 },
                                              .unit = "u",
                                              .writable = true };
        script_catalog_type native_type = { .id = 70002,
                                            .name = "Row",
                                            .type = { .storage = PIN_STRUCT,
                                                      .nominal = 70002 },
                                            .fields = &native_field,
                                            .field_count = 1,
                                            .size = sizeof(double) };
        script_catalog native_catalog = { .capabilities = c == 2 ? &sum : NULL,
                                          .capability_count = c == 2 ? 1 : 0,
                                          .types = c == 3 ? &native_type : NULL,
                                          .type_count = c == 3 ? 1 : 0 };
        analysis = script_analyze_document(syntax, &native_catalog, "unit-fact-source");
        REQUIRE(
            analysis && analysis->expression_count > 0 &&
            analysis->expression_count <= 64 && analysis->symbol_count <= 32 &&
            analysis->function_count <= 4 && analysis->statement_count <= 64 &&
            analysis->binding_count <= 8 && analysis->type_count <= 2
        );
        script_catalog catalog = native_catalog;
        script_catalog_type definitions[2];
        REQUIRE(native_catalog.type_count + analysis->type_count <= 2);
        for (int i = 0; i < native_catalog.type_count; i++) {
            definitions[i] = native_catalog.types[i];
        }
        for (int i = 0; i < analysis->type_count; i++) {
            definitions[native_catalog.type_count + i] = analysis->types[i];
        }
        catalog.types = definitions;
        catalog.type_count = native_catalog.type_count + analysis->type_count;
        script_unit_analysis_facts facts = { &catalog,
                                             analysis->symbols,
                                             analysis->symbol_count,
                                             analysis->expressions,
                                             analysis->expression_count,
                                             analysis->functions,
                                             analysis->function_count,
                                             analysis->statements,
                                             analysis->statement_count,
                                             analysis->bindings,
                                             analysis->binding_count };
        script_unit_analysis_result expected;
        REQUIRE(compare(&facts, c == 3 ? 1 : -1, &expected));
        REQUIRE(
            expected.diagnostic_count > 0 &&
            expected.diagnostic_count == analysis->diagnostic_count
        );
        bool found[32] = { false };
        for (int i = 0; i < analysis->diagnostic_count; i++) {
            int match = -1;
            for (int j = 0; j < expected.diagnostic_count; j++) {
                if (!found[j] &&
                    equal_diagnostic(&analysis->diagnostics[i], &expected.diagnostics[j])) {
                    match = j;
                    break;
                }
            }
            REQUIRE(match >= 0);
            found[match] = true;
        }
    }
    REQUIRE(calls == 60);
    return true;
}
TEST_CASE(unit_analysis_cache_and_diagnostics_outlive_all_inputs)
{
    cleanup();
    compiler_fixture_free(&fixture);
    REQUIRE(calls == 60);
    for (int i = 0; i < 2; i++) {
        const script_unit_analysis_result *expected = &saved_expected[i];
        REQUIRE(
            expected->unit_count > 0 && expected->diagnostic_count > 0 &&
            arena_contains(&saved_oracle[i], expected->units) &&
            arena_contains(&saved_oracle[i], expected->diagnostics)
        );
        bool known = false;
        for (int j = 0; j < expected->unit_count; j++) {
            for (int k = 0; k < expected->units[j].count; k++) {
                REQUIRE(
                    arena_contains(&saved_oracle[i], expected->units[j].terms[k].name)
                );
                known = true;
            }
        }
        REQUIRE(known);
        for (int b = 0; b < 2; b++) {
            REQUIRE(
                saved[b][i].type == PIN_UNION && saved[b][i].ptr &&
                owned(&saved_storage[b], &saved[b][i], 0) &&
                result_value(&saved[b][i], saved_tag[b], expected)
            );
        }
    }
    return true;
}
/* Each regression backend is a separate case: an expected baseline failure
 * cannot suppress the other backend or the other regression command. */
TEST_CASE(unit_analysis_regression_bytecode)
{
    selected_backend = 0;
    return unit_analysis_raw_ir_and_catalog_match();
}
TEST_CASE(unit_analysis_regression_sljit)
{
    selected_backend = 1;
    return unit_analysis_raw_ir_and_catalog_match();
}
void test_language_unit_analysis_chibi(int regression)
{
    prepared = false;
    preparation_attempted = false;
    calls = 0;
    selected_case = regression == 1 ? 7 : regression == 2 ? 24 : -1;
    selected_backend = -1;
    /* Only the deep diagnostic needs capture before nested frames retire. */
    fixture.observe_failures = selected_case == 24;
    pu_setup(NULL, cleanup);
    if (regression) {
        RUN_TEST_CASE(unit_analysis_regression_bytecode);
        RUN_TEST_CASE(unit_analysis_regression_sljit);
    } else {
        RUN_TEST_CASE(unit_analysis_raw_ir_and_catalog_match);
        RUN_TEST_CASE(unit_analysis_actual_analyzer_facts_match);
        RUN_TEST_CASE(unit_analysis_cache_and_diagnostics_outlive_all_inputs);
    }
    pu_clear_setup();
    compiler_fixture_free(&fixture);
    for (int i = 0; i < 2; i++) {
        arena_free(&saved_storage[i]);
        arena_free(&saved_oracle[i]);
    }
    memset(saved, 0, sizeof saved);
    memset(saved_expected, 0, sizeof saved_expected);
}
