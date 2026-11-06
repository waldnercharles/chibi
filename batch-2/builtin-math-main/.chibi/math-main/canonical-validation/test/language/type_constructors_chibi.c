#include "type_constructors_chibi.h"

#include "compiler_fixture.h"
#include "language/type_constructors.h"
#include "lexical_binding_chibi_source.h"
#include "named_binding_chibi_source.h"
#include "package_core_source.h"
#include "support/array.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_json.h"
#include "type_constructors_chibi_source.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"

#include <stdio.h>
#include <string.h>

/* Twenty raw requests plus four actual analyzed facts, forty-eight authored calls
 * capped at200,000 instructions. Five compiler sources per backend and four
 * tiny analyzed sources give fourteen parses. Compiler inputs are below32KiB;
 * analyzed fact sources are below256bytes. No IO/tasks execute. The JSON
 * primitive only admits the imported Named package; these entries never call it.
 * Input caps:64types/128IDs,8definitions/variants,2capabilities,4functions,
 * 32symbols,8result indices,4members. Output caps:4variants/2fields/1diagnostic.
 * Metadata materialization is the shared mechanical snapshot, never an input
 * decision supplied to authored code. */
enum
{
    MAX_TYPES = 64,
    MAX_IDS = 128,
    MAX_DEFINITIONS = 8,
    MAX_VARIANTS = 8,
    MAX_SYMBOLS = 32,
    MAX_FUNCTIONS = 4,
    MAX_RESULTS = 8
};
typedef enum
{
    UNION,
    TASK,
    AWAIT,
    POINTER
} operation;
typedef struct
{
    operation op;
    script_type members[4];
    int count;
    uint64_t function;
    script_range range;
} query;
static const char PACKAGE[] = "project:type_constructors",
                  NAMED[] = "project:named_binding";
static compiler_fixture fixture;
static arena input, detached, oracle, retained[2], retained_plan, metadata_storage;
static pin_value kept[2];
static script_constructor_plan saved;
static script_catalog_type kept_metadata;
static uint64_t kept_tags[2], kept_value_tags[2], union_id, task_id;
static script_type pool[MAX_TYPES];
static uint64_t ids[MAX_IDS];
static int type_count, id_count, calls, materializations;
static bool prepared;
static script_syntax *syntax;
static script_analysis *analysis;

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
static pin_value number(uint64_t n)
{
    return (pin_value){ .type = PIN_U64, .u64 = n };
}
static bool put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, row->ptr, field, value));
    return true;
}
static bool record(int b, const char *package, const char *name, pin_value *out)
{
    const pin_record *shape = compiler_fixture_record(&fixture, b, package, name, &input);
    REQUIRE(shape);
    *out = (pin_value){ .type = PIN_STRUCT,
                        .record = shape,
                        .type_name = shape->type.name,
                        .ptr = arena_alloc(&input, shape->type.size) };
    memset(out->ptr, 0, shape->type.size);
    return true;
}
static bool rows(int b, const char *package, const char *name, int count, pin_value *out)
{
    REQUIRE(count >= 0 && count <= MAX_TYPES);
    const pin_record *shape = compiler_fixture_record(&fixture, b, package, name, &input);
    REQUIRE(shape);
    pin_array *a = pin_array_snapshot_allocate(&input, PIN_STRUCT, shape->type.name, count);
    REQUIRE(a);
    a->record = shape;
    *out = (pin_value){ .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .record = shape,
                        .type_name = shape->type.name,
                        .ptr = a };
    for (int i = 0; i < count; i++) {
        REQUIRE(record(b, package, name, &a->items[i]));
    }
    return true;
}
static pin_value array(pin_type type, int count)
{
    return (pin_value){ .type = type,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = pin_array_snapshot_allocate(&input, type, NULL, count) };
}
static int slot(uint64_t value)
{
    if (!value) {
        return -1;
    }
    for (int i = 0; i < id_count; i++) {
        if (ids[i] == value) {
            return i;
        }
    }
    if (id_count == MAX_IDS) {
        return -2;
    }
    ids[id_count] = value;
    return id_count++;
}
static int add_type(script_type value)
{
    if (type_count == MAX_TYPES) {
        return -1;
    }
    pool[type_count] = value;
    return type_count++;
}
static const script_catalog_variant *variant(
    int b,
    const char *package,
    const char *type_name,
    const char *name
)
{
    const authoring_package *owner = compiler_fixture_package(&fixture, b, package);
    if (!owner || !owner->exports || !owner->exports->metadata) {
        return NULL;
    }
    const script_catalog_type *type = script_catalog_type_by_id(
        owner->exports->metadata,
        compiler_fixture_export(&fixture, b, package, type_name)
    );
    for (int i = 0; type && i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, name)) {
            return &type->variants[i];
        }
    }
    return NULL;
}
static bool pack(int b, const script_constructor_facts *facts, int symbol_count, query q, pin_value values[3])
{
    const script_catalog *catalog = facts->catalog;
    REQUIRE(
        catalog && catalog->type_count <= MAX_DEFINITIONS &&
        catalog->capability_count <= 2 && facts->function_count <= MAX_FUNCTIONS &&
        symbol_count <= MAX_SYMBOLS && q.count > 0 && q.count <= 4
    );
    type_count = 0;
    id_count = 0;
    int members[4], element_types[MAX_DEFINITIONS], variant_types[MAX_VARIANTS],
        variant_count = 0, result_count = 0;
    for (int i = 0; i < q.count; i++) {
        members[i] = add_type(q.members[i]);
        REQUIRE(members[i] >= 0);
    }
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *type = &catalog->types[i];
        element_types[i] = add_type(type->element_type);
        REQUIRE(element_types[i] >= 0);
        for (int j = 0; j < type->variant_count; j++) {
            REQUIRE(variant_count < MAX_VARIANTS);
            variant_types[variant_count++] = add_type(type->variants[j].type);
            REQUIRE(variant_types[variant_count - 1] >= 0);
        }
    }
    for (int i = 0; i < facts->function_count; i++) {
        REQUIRE(acount(facts->functions[i].results) <= 2);
        result_count += acount(facts->functions[i].results);
    }
    REQUIRE(result_count <= MAX_RESULTS);
    pin_value tables[8];
    REQUIRE(
        rows(b, NAMED, "Type", type_count, &tables[0]) &&
        rows(b, PACKAGE, "Definition", catalog->type_count, &tables[2]) &&
        rows(b, PACKAGE, "RawVariant", variant_count, &tables[3]) &&
        rows(b, PACKAGE, "Capability", catalog->capability_count, &tables[4]) &&
        rows(b, PACKAGE, "Function", facts->function_count, &tables[5]) &&
        rows(b, PACKAGE, "Symbol", symbol_count, &tables[6])
    );
    tables[7] = array(PIN_I32, result_count);
    REQUIRE(tables[7].ptr);
    pin_array *types = tables[0].ptr, *definitions = tables[2].ptr,
              *variants = tables[3].ptr, *capabilities = tables[4].ptr,
              *functions = tables[5].ptr, *symbols = tables[6].ptr,
              *results = tables[7].ptr;
    for (int i = 0; i < type_count; i++) {
        int nominal = slot(pool[i].nominal), parameter = slot(pool[i].parameter);
        REQUIRE(nominal >= -1 && parameter >= -1);
        REQUIRE(
            put(&types->items[i], "storage", integer(pool[i].storage)) &&
            put(&types->items[i], "container", integer(pool[i].container)) &&
            put(&types->items[i], "flags", integer((int)pool[i].flags)) &&
            put(&types->items[i], "nominal_slot", integer(nominal)) &&
            put(&types->items[i], "parameter_slot", integer(parameter)) &&
            put(&types->items[i], "collection", number(pool[i].collection))
        );
    }
    int offset = 0;
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *type = &catalog->types[i];
        int id = slot(type->id);
        REQUIRE(id >= -1);
        REQUIRE(
            put(&definitions->items[i], "id_slot", integer(id)) &&
            put(&definitions->items[i], "variant_start", integer(offset)) &&
            put(&definitions->items[i], "variant_count", integer(type->variant_count)) &&
            put(&definitions->items[i], "element_type", integer(element_types[i]))
        );
        for (int j = 0; j < type->variant_count; j++) {
            int tag = slot(type->variants[j].tag);
            REQUIRE(tag >= -1);
            REQUIRE(
                put(&variants->items[offset], "tag_slot", integer(tag)) &&
                put(&variants->items[offset], "type", integer(variant_types[offset]))
            );
            offset++;
        }
    }
    for (int i = 0; i < catalog->capability_count; i++) {
        int id = slot(catalog->capabilities[i].id);
        REQUIRE(id >= -1 && catalog->capabilities[i].result_count <= 2);
        REQUIRE(
            put(&capabilities->items[i], "id_slot", integer(id)) &&
            put(&capabilities->items[i],
                "result_count",
                integer(catalog->capabilities[i].result_count))
        );
    }
    offset = 0;
    for (int i = 0; i < facts->function_count; i++) {
        const script_function *f = &facts->functions[i];
        REQUIRE(f->symbol >= -1 && f->symbol < symbol_count);
        REQUIRE(
            put(&functions->items[i], "symbol", integer(f->symbol)) &&
            put(&functions->items[i], "result_start", integer(offset)) &&
            put(&functions->items[i], "result_count", integer(acount(f->results)))
        );
        for (int j = 0; j < acount(f->results); j++) {
            REQUIRE(f->results[j] >= 0 && f->results[j] < symbol_count);
            results->items[offset++] = integer(f->results[j]);
        }
    }
    for (int i = 0; i < symbol_count; i++) {
        int id = slot(facts->symbols[i].id);
        REQUIRE(id >= -1);
        REQUIRE(put(&symbols->items[i], "id_slot", integer(id)));
    }
    tables[1] = array(PIN_U64, id_count);
    REQUIRE(tables[1].ptr);
    pin_array *numbers = tables[1].ptr;
    for (int i = 0; i < id_count; i++) {
        numbers->items[i] = number(ids[i]);
    }
    const script_catalog_variant *form = variant(b, PACKAGE, "Environment", "Tables");
    REQUIRE(form && form->field_count == 8);
    REQUIRE(!compiler_fixture_union(&input, form->tag, tables, 8, &values[0]));
    if (q.op == UNION) {
        values[1] = array(PIN_I32, q.count);
        REQUIRE(values[1].ptr);
        pin_array *m = values[1].ptr;
        for (int i = 0; i < q.count; i++) {
            m->items[i] = integer(members[i]);
        }
        values[2] = integer(sizeof(void *));
    } else {
        values[1] = integer(members[0]);
        if (q.op == POINTER) {
            values[2] = integer(sizeof(void *));
        } else if (q.op == TASK) {
            values[2] = number(q.function);
        } else {
            REQUIRE(record(b, PACKAGE, "Request", &values[2]));
            REQUIRE(
                put(&values[2], "pointer_size", integer(sizeof(void *))) &&
                put(&values[2], "start", integer(q.range.start)) &&
                put(&values[2], "end", integer(q.range.end)) &&
                put(&values[2], "line", integer(q.range.line)) &&
                put(&values[2], "column", integer(q.range.column)) &&
                put(&values[2], "document", integer(q.range.document))
            );
        }
    }
    return true;
}
static bool read(const pin_value *row, const char *name, pin_value *out)
{
    REQUIRE(row->type == PIN_STRUCT && row->ptr);
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&input, row->ptr, field, out));
    return true;
}
static bool same_int(const pin_value *row, const char *name, int expected)
{
    pin_value value;
    REQUIRE(read(row, name, &value) && value.type == PIN_I32 && value.i == expected);
    return true;
}
static bool type_value(const pin_value *value, uint64_t tag, script_type expected)
{
    REQUIRE(value->type == PIN_UNION && value->ptr);
    const pin_union *data = value->ptr;
    REQUIRE(data->tag == tag && data->count == 5);
    const pin_value *v = data->values;
    REQUIRE(
        v[0].type == PIN_I32 && v[0].i == (int)expected.storage &&
        v[1].type == PIN_I32 && v[1].i == (int)expected.container &&
        v[2].type == PIN_I32 && v[2].i == (int)expected.flags &&
        v[3].type == PIN_U64 && v[3].u64 == expected.nominal &&
        v[4].type == PIN_U64 && v[4].u64 == expected.parameter
    );
    return true;
}
static bool output_array(const pin_value *value, pin_type type, int count, const pin_array **out)
{
    REQUIRE(value->container == PIN_CONTAINER_ARRAY && value->type == type);
    *out = value->ptr;
    REQUIRE((*out ? (*out)->count : 0) == count);
    return true;
}
static bool output_id(const pin_array *ids, int index, uint64_t *out)
{
    if (index < 0) {
        REQUIRE(index == -1);
        *out = 0;
        return true;
    }
    REQUIRE(ids && index < ids->count && ids->items[index].type == PIN_U64);
    *out = ids->items[index].u64;
    return true;
}
static bool plan_value(const pin_value *value, uint64_t tag, uint64_t value_tag, const script_constructor_plan *expected)
{
    REQUIRE(value->type == PIN_UNION && value->ptr);
    const pin_union *data = value->ptr;
    REQUIRE(data->tag == tag && data->count == 16);
    const pin_value *v = data->values;
    REQUIRE(
        type_value(&v[0], value_tag, expected->type) && v[1].type == PIN_BOOL &&
        v[1].b == expected->publish && v[2].type == PIN_U64 &&
        v[2].u64 == expected->id && v[3].type == PIN_STR && pin_text(v[3]).bytes &&
        !strcmp(pin_text(v[3]).bytes, expected->name ? expected->name : "")
    );
    REQUIRE(
        type_value(&v[4], value_tag, expected->definition_type) &&
        type_value(&v[11], value_tag, expected->element_type) &&
        v[12].type == PIN_U64 && v[12].u64 == expected->element_type.collection &&
        v[5].type == PIN_I32 && v[5].i == (int)expected->size &&
        v[6].type == PIN_BOOL && v[6].b == expected->internal
    );
    const pin_array *variants, *fields, *numbers, *diagnostics;
    REQUIRE(
        output_array(&v[7], PIN_STRUCT, expected->variant_count, &variants) &&
        output_array(&v[8], PIN_STRUCT, expected->field_count, &fields)
    );
    REQUIRE(v[9].type == PIN_U64 && v[9].container == PIN_CONTAINER_ARRAY);
    numbers = v[9].ptr;
    REQUIRE(!numbers || numbers->count <= 12);
    for (int i = 0; i < expected->variant_count; i++) {
        const script_constructor_variant *s = &expected->variants[i];
        const pin_value *row = &variants->items[i];
        REQUIRE(
            same_int(row, "catalog_type", s->catalog_type) &&
            same_int(row, "variant", s->variant) && same_int(row, "member", s->member) &&
            same_int(row, "storage", s->type.storage) &&
            same_int(row, "container", s->type.container) &&
            same_int(row, "flags", (int)s->type.flags)
        );
        pin_value collection;
        REQUIRE(
            read(row, "collection", &collection) &&
            collection.type == PIN_U64 && collection.u64 == s->type.collection
        );
        const char *keys[] = { "nominal_slot", "parameter_slot", "tag_slot" };
        const uint64_t wanted[] = { s->type.nominal, s->type.parameter, s->tag };
        for (int j = 0; j < 3; j++) {
            pin_value index;
            uint64_t actual;
            REQUIRE(
                read(row, keys[j], &index) && index.type == PIN_I32 &&
                output_id(numbers, index.i, &actual) && actual == wanted[j]
            );
        }
        if (i) {
            REQUIRE(expected->variants[i - 1].tag < s->tag);
        }
    }
    for (int i = 0; i < expected->field_count; i++) {
        const script_constructor_field *s = &expected->fields[i];
        REQUIRE(
            same_int(&fields->items[i], "capability", s->capability) &&
            same_int(&fields->items[i], "parameter", s->parameter) &&
            same_int(&fields->items[i], "symbol", s->symbol)
        );
    }
    REQUIRE(output_array(&v[10], PIN_STRUCT, expected->diagnostic_count, &diagnostics));
    for (int i = 0; i < expected->diagnostic_count; i++) {
        const script_diagnostic *d = &expected->diagnostics[i];
        const pin_value *row = &diagnostics->items[i];
        REQUIRE(
            same_int(row, "start", d->range.start) &&
            same_int(row, "end", d->range.end) && same_int(row, "line", d->range.line) &&
            same_int(row, "column", d->range.column) &&
            same_int(row, "document", d->range.document)
        );
        pin_value message;
        REQUIRE(
            read(row, "message", &message) && message.type == PIN_STR &&
            pin_text(message).bytes && !strcmp(pin_text(message).bytes, d->message)
        );
    }
    if (!expected->publish) {
        REQUIRE(!numbers || numbers->count == 0);
    }
    return true;
}
static bool owned(arena *owner, const pin_value *value, int depth)
{
    REQUIRE(depth <= 6);
    if (value->container == PIN_CONTAINER_ARRAY) {
        if (!value->ptr) {
            return true;
        }
        const pin_array *a = value->ptr;
        REQUIRE(
            arena_contains(owner, a) && a->count >= 0 && a->count <= 12 &&
            (!a->count || (a->items && arena_contains(owner, a->items)))
        );
        for (int i = 0; i < a->count; i++) {
            REQUIRE(owned(owner, &a->items[i], depth + 1));
        }
        return true;
    }
    if (value->type == PIN_UNION) {
        const pin_union *u = value->ptr;
        REQUIRE(u && arena_contains(owner, u) && u->count >= 0 && u->count <= 13);
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
            type && type->field_count <= 10 && type->name &&
            arena_contains(owner, type->name)
        );
        REQUIRE(!type->field_count || (type->fields && arena_contains(owner, type->fields)));
        for (int i = 0; i < type->field_count; i++) {
            pin_value field;
            REQUIRE(!pin_read_field(owner, value->ptr, &type->fields[i], &field));
            REQUIRE(owned(owner, &field, depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STR) {
        REQUIRE(pin_text(*value).bytes && arena_contains(owner, value->text));
        return true;
    }
    REQUIRE(value->type == PIN_I32 || value->type == PIN_U64 || value->type == PIN_BOOL);
    return true;
}
static bool compare(
    const script_constructor_facts *facts,
    int symbols,
    query q,
    bool retain,
    script_constructor_plan *out
)
{
    arena_free(&oracle);
    script_constructor_plan
        expected = q.op == POINTER ? (script_constructor_plan){ 0 }
                 : q.op == UNION
                     ? script_constructor_union(&oracle, facts, q.members, q.count, sizeof(void *))
                 : q.op == TASK
                     ? script_constructor_task(&oracle, facts, q.members[0], q.function)
                     : script_constructor_await(&oracle, facts, q.members[0], q.range, sizeof(void *));
    if (q.op == POINTER) {
        script_catalog_type
            pointer = script_constructor_pointer(&oracle, q.members[0], sizeof(void *));
        expected = (script_constructor_plan){ .type = pointer.type,
                                              .publish = true,
                                              .id = pointer.id,
                                              .name = pointer.name,
                                              .definition_type = pointer.type,
                                              .element_type = pointer.element_type,
                                              .size = pointer.size,
                                              .internal = true };
        for (int i = 0; i < facts->catalog->type_count; i++) {
            if (facts->catalog->types[i].id == pointer.id) {
                expected = (script_constructor_plan){ .type = pointer.type };
                break;
            }
        }
    }
    REQUIRE(
        expected.variant_count >= 0 && expected.variant_count <= 4 &&
        expected.field_count >= 0 && expected.field_count <= 2 &&
        expected.diagnostic_count >= 0 && expected.diagnostic_count <= 1
    );
    script_catalog_type materialized = { 0 };
    bool copied = script_constructor_materialize(&oracle, facts, &expected, &materialized);
    REQUIRE(copied == expected.publish);
    if (copied) {
        materializations++;
        REQUIRE(
            materialized.id == expected.id && materialized.internal &&
            materialized.variant_count == expected.variant_count &&
            materialized.field_count == expected.field_count &&
            script_type_equal(materialized.element_type, expected.element_type)
        );
    }
    if (out) {
        *out = expected;
    }
    for (int b = 0; b < 2; b++) {
        arena_free(&input);
        arena_free(&detached);
        pin_value values[3], result;
        REQUIRE(pack(b, facts, symbols, q, values));
        REQUIRE(compiler_fixture_call(
            &fixture,
            b,
            PACKAGE,
            q.op == UNION     ? "Union"
            : q.op == TASK    ? "Task"
            : q.op == POINTER ? "Pointer"
                              : "Await",
            values,
            3,
            200000,
            &detached,
            &result
        ));
        calls++;
        arena_free(&input);
        const script_catalog_variant *form = variant(b, PACKAGE, "Plan", "Constructed");
        const script_catalog_variant *typed = variant(b, NAMED, "Value", "Typed");
        REQUIRE(form && typed);
        REQUIRE(
            owned(&detached, &result, 0) &&
            plan_value(&result, form->tag, typed->tag, &expected)
        );
        if (retain) {
            REQUIRE(!compiler_fixture_snapshot(&retained[b], &kept[b], &result));
            kept_tags[b] = form->tag;
            kept_value_tags[b] = typed->tag;
        }
    }
    if (retain) {
        saved = expected;
        if (expected.name) {
            size_t n = strlen(expected.name) + 1;
            char *name = arena_alloc(&retained_plan, n);
            memcpy(name, expected.name, n);
            saved.name = name;
        }
        if (expected.variant_count) {
            script_constructor_variant *v = arena_alloc(
                &retained_plan,
                (size_t)expected.variant_count * sizeof *v
            );
            memcpy(v, expected.variants, (size_t)expected.variant_count * sizeof *v);
            saved.variants = v;
        }
        if (expected.field_count) {
            script_constructor_field *f = arena_alloc(
                &retained_plan,
                (size_t)expected.field_count * sizeof *f
            );
            memcpy(f, expected.fields, (size_t)expected.field_count * sizeof *f);
            saved.fields = f;
        }
    }
    return true;
}
static script_function raw_function;
TEST_CASE(type_constructor_raw_facts_match_plans_and_selected_metadata)
{
    const source_package_input files[] = {
        { "type_constructors/Constructors.chibi",
          TYPE_CONSTRUCTORS_CHIBI_SOURCE,
          sizeof TYPE_CONSTRUCTORS_CHIBI_SOURCE - 1 },
        { "named_binding/Binding.chibi",
          NAMED_BINDING_CHIBI_SOURCE,
          sizeof NAMED_BINDING_CHIBI_SOURCE - 1 },
        { "lexical_binding/Binding.chibi",
          LEXICAL_BINDING_CHIBI_SOURCE,
          sizeof LEXICAL_BINDING_CHIBI_SOURCE - 1 },
        { "syntax_diagnostics/Diagnostics.chibi",
          SYNTAX_CHIBI_SOURCE,
          sizeof SYNTAX_CHIBI_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
    };
    for (int i = 0; i < 5; i++) {
        REQUIRE(files[i].size < 32768);
    }
    REQUIRE(compiler_fixture_prepare_packs(&fixture, files, 5, &COMPILER_JSON_DEFINITION_PACK, 1));
    prepared = true;
    const script_type integer_type = { .storage = PIN_I32 },
                      number_type = { .storage = PIN_F64 };
    for (int i = 0; i < 16; i++) {
        char name[32] = "Borrowed", field_name[32] = "message",
             default_text[32] = "owned default", literal[32] = "owned literal";
        script_catalog_field field = {
            .name = field_name,
            .type = { .storage = PIN_STR },
            .has_default = true,
            .default_value = { .type = PIN_STR,
                               .text = &(
                                   const pin_string
                               ){ .bytes = default_text,
                                  .length = (uint32_t)strlen(default_text) } }
        };
        script_type nested = { .storage = PIN_UNION,
                               .nominal = 9001,
                               .flags = SCRIPT_TYPE_UNION };
        script_catalog_variant variants[3] = {
            { .tag = UINT64_MAX - 3,
              .name = name,
              .type = { .storage = PIN_UNION,
                        .nominal = 9001,
                        .parameter = UINT64_MAX - 3,
                        .flags = SCRIPT_TYPE_UNION },
              .tagged = true,
              .fields = &field,
              .field_count = 1 },
            { .tag = 17,
              .name = literal,
              .type = { .storage = PIN_STR, .parameter = 17, .flags = SCRIPT_TYPE_LITERAL },
              .is_literal = true,
              .literal = { .type = PIN_STR,
                           .text = &(const pin_string){ .bytes = literal,
                                                        .length = (uint32_t)strlen(literal) } } },
        };
        script_catalog_type definitions[2] = { { .id = 9001,
                                                 .name = "Nested",
                                                 .type = nested,
                                                 .variants = variants,
                                                 .variant_count = 2 } };
        script_catalog_field duplicate_field = { .name = "later_field",
                                                 .type = { .storage = PIN_I32 } };
        script_catalog_variant duplicate = {
            .tag = UINT64_MAX - 3,
            .name = "Later",
            .type = { .storage = PIN_UNION,
                      .nominal = 9002,
                      .parameter = UINT64_MAX - 3,
                      .flags = SCRIPT_TYPE_UNION },
            .tagged = true,
            .fields = &duplicate_field,
            .field_count = 1,
        };
        script_catalog_parameter results[] = {
            { .name = "Count", .type = integer_type },
            { .name = "Message", .type = { .storage = PIN_STR } }
        };
        script_catalog_capability capability = { .id = 70003,
                                                 .name = "Native",
                                                 .results = results,
                                                 .result_count = 2 };
        script_symbol symbols[] = {
            { .id = 70003, .name = "Work", .kind = SCRIPT_FUNCTION },
            { .id = 70004, .name = "Count", .kind = SCRIPT_RESULT, .type = integer_type },
            { .id = 70005, .name = "Message", .kind = SCRIPT_RESULT, .type = { .storage = PIN_STR } }
        };
        afree(raw_function.results);
        raw_function = (script_function){ .symbol = 0 };
        apush(raw_function.results, 1);
        apush(raw_function.results, 2);
        script_catalog catalog = { .types = definitions,
                                   .type_count = 1,
                                   .capabilities = &capability,
                                   .capability_count = 1 };
        script_constructor_facts facts = { &catalog, symbols, &raw_function, 1 };
        query q = { .op = UNION,
                    .members = { integer_type, number_type },
                    .count = 2,
                    .function = 70003,
                    .range = { 23, 36, 4, 6, 1 } };
        if (i == 1) {
            q.members[0] = number_type;
            q.members[1] = integer_type;
        }
        if (i == 2) {
            /* The final insertion shares the high half with MAX-3 and crosses
             * the low-half signed boundary. Tag17 also tests unequal highs. */
            variants[2] = (script_catalog_variant){
                .tag = UINT64_C(0xffffffff7fffffff),
                .name = "Earlier",
                .type = { .storage = PIN_UNION,
                          .nominal = 9001,
                          .parameter = UINT64_C(0xffffffff7fffffff),
                          .flags = SCRIPT_TYPE_UNION },
                .tagged = true,
            };
            definitions[0].variant_count = 3;
            definitions[1] = (script_catalog_type){
                .id = 9002,
                .name = "LaterUnion",
                .type = { .storage = PIN_UNION, .nominal = 9002, .flags = SCRIPT_TYPE_UNION },
                .variants = &duplicate,
                .variant_count = 1
            };
            catalog.type_count = 2;
            q.members[0] = nested;
            q.members[1] = nested;
            q.members[1].nominal = 9002;
        }
        if (i == 3) {
            q.members[0] = nested;
            q.members[0].parameter = UINT64_MAX - 3;
            q.count = 1;
        }
        if (i == 4) {
            q.members[0] = nested;
            q.members[0].flags |= SCRIPT_TYPE_BOXED_OPTIONAL;
            q.count = 1;
        }
        if (i == 5) {
            q.members[0] = nested;
            q.members[0].container = PIN_CONTAINER_ARRAY;
            q.members[0].collection = UINT64_MAX - 31;
            q.count = 1;
        }
        if (i == 6) {
            q.members[0] = nested;
            q.members[0].nominal = 9002;
            q.count = 1;
        }
        if (i == 7) {
            definitions[1] = (script_catalog_type){ .id = union_id,
                                                    .name = "CachedUnion" };
            catalog.type_count = 2;
        }
        if (i >= 8 && i <= 12) {
            q.op = TASK;
            q.count = 1;
            q.members[0] = number_type;
        }
        if (i >= 9 && i <= 11) {
            q.members[0] = (script_type){ .storage = PIN_STRUCT,
                                          .nominal = 70003,
                                          .flags = SCRIPT_TYPE_RESULTS };
        }
        if (i == 10) {
            capability.result_count = 0;
        }
        if (i == 11) {
            catalog.capability_count = 0;
        }
        if (i == 12) {
            definitions[1] = (script_catalog_type){ .id = task_id, .name = "CachedTask" };
            catalog.type_count = 2;
        }
        if (i >= 13) {
            q.op = AWAIT;
            q.count = 1;
            q.members[0] = number_type;
        }
        if (i == 14) {
            q.members[0] = script_cancelled_type();
        }
        if (i == 15) {
            variants[1] = (script_catalog_variant){ .tag = PIN_UNION_CANCELLED_TAG,
                                                    .type = script_cancelled_type() };
            q.members[0] = nested;
            q.members[0].parameter = UINT64_MAX - 3;
        }
        script_constructor_plan expected;
        REQUIRE(compare(&facts, 3, q, i == 15, &expected));
        if (i == 0) {
            union_id = expected.type.nominal;
            REQUIRE(expected.publish && expected.variant_count == 2);
        }
        if (i == 1) {
            REQUIRE(expected.type.nominal == union_id);
        }
        if (i == 2) {
            REQUIRE(
                expected.variant_count == 3 && expected.variants[0].tag == 17 &&
                expected.variants[1].tag == UINT64_C(0xffffffff7fffffff) &&
                expected.variants[2].tag == UINT64_MAX - 3
            );
            REQUIRE(
                expected.variants[2].catalog_type == 0 &&
                expected.variants[2].variant == 0 &&
                expected.variants[2].type.nominal == 9001
            );
            REQUIRE(
                script_constructor_materialize(&metadata_storage, &facts, &expected, &kept_metadata)
            );
            materializations++;
            strcpy(name, "changed");
            strcpy(field_name, "changed");
            strcpy(default_text, "changed");
            strcpy(literal, "changed");
        }
        if (i == 3) {
            REQUIRE(
                expected.variant_count == 1 && expected.variants[0].catalog_type == 0 &&
                expected.variants[0].variant == 0
            );
        }
        if (i >= 4 && i <= 6) {
            REQUIRE(
                expected.variant_count == 1 &&
                expected.variants[0].catalog_type == -1 &&
                expected.variants[0].member == 0
            );
        }
        if (i == 7 || i == 12) {
            REQUIRE(
                !expected.publish && !expected.id && !expected.name &&
                !expected.variant_count && !expected.field_count
            );
        }
        if (i == 8) {
            task_id = expected.type.parameter;
            REQUIRE(expected.publish && !expected.field_count);
        }
        if (i == 9) {
            REQUIRE(
                expected.field_count == 2 && expected.fields[0].capability == 0 &&
                expected.fields[1].parameter == 1
            );
        }
        if (i == 10) {
            REQUIRE(expected.publish && expected.field_count == 0);
        }
        if (i == 11) {
            REQUIRE(
                expected.field_count == 2 && expected.fields[0].symbol == 1 &&
                expected.fields[1].symbol == 2
            );
        }
        if (i >= 14) {
            REQUIRE(
                expected.diagnostic_count == 1 &&
                expected.diagnostics[0].range.document == 1
            );
        }
    }
    REQUIRE(calls == 32 && materializations == 15);
    return true;
}
static bool same_type(script_type a, script_type b)
{
    REQUIRE(
        a.storage == b.storage && a.container == b.container && a.flags == b.flags &&
        a.nominal == b.nominal && a.parameter == b.parameter && a.collection == b.collection
    );
    return true;
}
TEST_CASE(type_constructor_actual_analyzer_facts_match)
{
    REQUIRE(prepared);
    static const char *sources[] = {
        "Facts :: library { Run :: (value: int | number) -> int { return 0; } "
        "}",
        "Facts :: library { Run :: (pending: task(number)) {} }",
        "Facts :: library { Work :: () -> (Count: int, Message: string) { "
        "yield; return(3, \"ok\"); } Run :: () { pending := Work(); } }",
        "Facts :: library { Run :: (pending: task(number)) -> number { result "
        ":= await pending; if result is not Cancelled { return result; } "
        "return 0; } }",
    };
    const script_catalog native = { 0 };
    for (int c = 0; c < 4; c++) {
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(strlen(sources[c]) < 256);
        syntax = script_parse(sources[c]);
        for (int i = 0; syntax && i < syntax->diagnostic_count; i++) {
            fprintf(
                stderr,
                "constructor source%d:%d:%d %s\n",
                c,
                syntax->diagnostics[i].range.line,
                syntax->diagnostics[i].range.column,
                syntax->diagnostics[i].message
            );
        }
        REQUIRE(syntax && !syntax->diagnostic_count);
        analysis = script_analyze_document(syntax, &native, "facts/Constructors.chibi");
        for (int i = 0; analysis && i < analysis->diagnostic_count; i++) {
            fprintf(
                stderr,
                "constructor analysis%d:%d:%d %s\n",
                c,
                analysis->diagnostics[i].range.line,
                analysis->diagnostics[i].range.column,
                analysis->diagnostics[i].message
            );
        }
        REQUIRE(
            analysis && analysis->valid && analysis->type_count <= MAX_DEFINITIONS &&
            analysis->symbol_count <= MAX_SYMBOLS && analysis->function_count <= MAX_FUNCTIONS
        );
        query q = { .op = c == 0 ? UNION
                        : c == 3 ? AWAIT
                                 : TASK,
                    .count = c == 0 ? 2 : 1 };
        script_type expected_type = { 0 };
        uint64_t descriptor = 0;
        if (c == 0) {
            q.members[0] = (script_type){ .storage = PIN_I32 };
            q.members[1] = (script_type){ .storage = PIN_F64 };
            for (int i = 0; i < analysis->symbol_count; i++) {
                if (!strcmp(analysis->symbols[i].name, "value")) {
                    expected_type = analysis->symbols[i].type;
                }
            }
            REQUIRE(expected_type.flags & SCRIPT_TYPE_UNION);
            descriptor = expected_type.nominal;
        }
        if (c == 1 || c == 2) {
            for (int i = 0; i < analysis->symbol_count; i++) {
                if (!strcmp(analysis->symbols[i].name, "pending")) {
                    expected_type = analysis->symbols[i].type;
                }
            }
            REQUIRE(expected_type.storage == PIN_TASK);
            descriptor = expected_type.parameter;
            q.function = expected_type.nominal;
            const script_catalog_type *definition = NULL;
            for (int i = 0; i < analysis->type_count; i++) {
                if (analysis->types[i].id == descriptor) {
                    definition = &analysis->types[i];
                }
            }
            REQUIRE(definition);
            q.members[0] = definition->type;
            if (c == 2) {
                REQUIRE(q.members[0].flags & SCRIPT_TYPE_RESULTS);
            }
        }
        if (c == 3) {
            bool found = false;
            for (int i = 0; i < analysis->expression_count; i++) {
                const script_expression *expression = &analysis->expressions[i];
                if (expression->kind == SCRIPT_EXPR_JOIN) {
                    q.members[0] = expression->tested;
                    q.range = expression->range;
                    expected_type = expression->type;
                    found = true;
                    break;
                }
            }
            REQUIRE(found);
            descriptor = expected_type.nominal;
        }
        script_catalog_type definitions[MAX_DEFINITIONS];
        int count = 0;
        bool removed = false;
        for (int i = 0; i < analysis->type_count; i++) {
            if (analysis->types[i].id == descriptor) {
                removed = true;
                continue;
            }
            definitions[count++] = analysis->types[i];
        }
        REQUIRE(removed);
        script_catalog catalog = { .types = definitions, .type_count = count };
        script_constructor_facts facts = { &catalog,
                                           analysis->symbols,
                                           analysis->functions,
                                           analysis->function_count };
        script_constructor_plan expected;
        REQUIRE(
            compare(&facts, analysis->symbol_count, q, false, &expected) &&
            expected.publish && same_type(expected.type, expected_type)
        );
        if (c == 2) {
            REQUIRE(
                expected.field_count == 2 && expected.fields[0].symbol >= 0 &&
                expected.fields[1].symbol >= 0
            );
        }
    }
    REQUIRE(calls == 40 && materializations == 19);
    return true;
}
TEST_CASE(type_constructor_results_and_metadata_outlive_producers)
{
    cleanup();
    compiler_fixture_free(&fixture);
    prepared = false;
    for (int b = 0; b < 2; b++) {
        REQUIRE(
            kept[b].type == PIN_UNION && kept[b].ptr &&
            owned(&retained[b], &kept[b], 0) &&
            plan_value(&kept[b], kept_tags[b], kept_value_tags[b], &saved)
        );
    }
    REQUIRE(
        kept_metadata.variant_count == 3 && kept_metadata.variants &&
        arena_contains(&metadata_storage, kept_metadata.variants)
    );
    const script_catalog_variant *literal = &kept_metadata.variants[0],
                                 *earlier = &kept_metadata.variants[1],
                                 *tagged = &kept_metadata.variants[2];
    REQUIRE(
        earlier->tag == UINT64_C(0xffffffff7fffffff) && earlier->tagged &&
        earlier->name && arena_contains(&metadata_storage, earlier->name) &&
        !strcmp(earlier->name, "Earlier") && !earlier->field_count
    );
    REQUIRE(
        literal->is_literal && literal->literal.type == PIN_STR &&
        pin_text(literal->literal).bytes &&
        arena_contains(&metadata_storage, literal->literal.text) &&
        !strcmp(pin_text(literal->literal).bytes, "owned literal")
    );
    REQUIRE(
        tagged->tag == UINT64_MAX - 3 && tagged->type.nominal == 9001 && tagged->tagged &&
        tagged->name && arena_contains(&metadata_storage, tagged->name) &&
        !strcmp(tagged->name, "Borrowed") && tagged->field_count == 1 &&
        tagged->fields && arena_contains(&metadata_storage, tagged->fields)
    );
    const script_catalog_field *field = &tagged->fields[0];
    REQUIRE(
        field->name && arena_contains(&metadata_storage, field->name) &&
        !strcmp(field->name, "message") && field->has_default &&
        field->default_value.type == PIN_STR && pin_text(field->default_value).bytes &&
        arena_contains(&metadata_storage, field->default_value.text) &&
        !strcmp(pin_text(field->default_value).bytes, "owned default")
    );
    return true;
}
TEST_CASE(type_constructor_pointer_preserves_complete_pointees)
{
    REQUIRE(prepared);
    int before = calls;
    script_catalog catalog = { 0 };
    script_constructor_facts facts = { .catalog = &catalog };
    query q = { .op = POINTER, .members = { { .storage = PIN_I32 } }, .count = 1 };
    script_constructor_plan first, composite;
    REQUIRE(compare(&facts, 0, q, false, &first));
    q.members[0] = (script_type){ .storage = PIN_STRUCT,
                                  .container = PIN_CONTAINER_MAP,
                                  .nominal = UINT64_MAX - 3,
                                  .parameter = UINT64_MAX - 7,
                                  .collection = UINT64_MAX - 11,
                                  .flags = SCRIPT_TYPE_OPTIONAL |
                                           SCRIPT_TYPE_BOXED_OPTIONAL };
    REQUIRE(compare(&facts, 0, q, false, &composite));
    REQUIRE(first.type.nominal != composite.type.nominal);
    uint64_t composite_id = composite.type.nominal;
    q.members[0].collection--;
    script_constructor_plan different;
    REQUIRE(compare(&facts, 0, q, false, &different));
    REQUIRE(different.type.nominal != composite_id);
    q.members[0].collection++;
    script_catalog_type cached = { .id = composite_id,
                                   .type = composite.type,
                                   .element_type = q.members[0] };
    catalog.types = &cached;
    catalog.type_count = 1;
    script_constructor_plan hit;
    REQUIRE(compare(&facts, 0, q, false, &hit));
    REQUIRE(!hit.publish && hit.type.nominal == composite_id && calls == before + 8);
    return true;
}
void test_language_type_constructors_chibi()
{
    calls = 0;
    materializations = 0;
    prepared = false;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(type_constructor_raw_facts_match_plans_and_selected_metadata);
    RUN_TEST_CASE(type_constructor_actual_analyzer_facts_match);
    RUN_TEST_CASE(type_constructor_pointer_preserves_complete_pointees);
    RUN_TEST_CASE(type_constructor_results_and_metadata_outlive_producers);
    pu_clear_setup();
    compiler_fixture_free(&fixture);
    afree(raw_function.results);
    raw_function = (script_function){ 0 };
    for (int b = 0; b < 2; b++) {
        arena_free(&retained[b]);
        kept[b] = (pin_value){ 0 };
    }
    arena_free(&retained_plan);
    arena_free(&metadata_storage);
    kept_metadata = (script_catalog_type){ 0 };
    saved = (script_constructor_plan){ 0 };
}
