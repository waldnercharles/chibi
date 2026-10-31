#include "scalar_host.h"

#include "compiler_cache.h"
#include "execution/status.h"
#include "package_compiler_chibi_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/native_definitions.h"
#include "runtime/text_definition.h"
#include "support/xalloc.h"
#include "toolchain/compiler_bootstrap.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "toolchain/compiler_target.h"
#include "types/array.h"
#include "types/record.h"
#include "types/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(condition)                                                                \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            fprintf(stderr, "scalar_host: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return false;                                                                 \
        }                                                                                 \
    } while (0)

#ifndef CHIBI_SOURCE_ROOT
#error "scalar_host.c needs CHIBI_SOURCE_ROOT"
#endif

enum
{
    CALL_BUDGET = 200000000,
    ROW_FIELDS = 11,
    ROW_ID = 0x73636c61 /* "scla" */
};

/* The root of the consumer closure: thin entries over the packages the tests
 * call, because a compiled closure runs only the entries of its root. */
static const char ROOT_PATH[] = "/compiler/roots/Roots.chibi";
static const char ROOT_SOURCE[] =
    "package roots\n"
    "import abi \"project:native_abi\"\n"
    "import cases \"project:scalar_cases\"\n"
    "import facts \"project:scalar_facts\"\n"
    "import native \"project:llvm_native\"\n"
    "import scalar \"project:scalar_llvm\"\n"
    "Target :: (name: string) -> abi.Target {\n"
    "    if (name == \"MacOSArm64\") { return abi.MacOSArm64(); }\n"
    "    if (name == \"WindowsX64\") { return abi.WindowsX64(); }\n"
    "    return abi.LinuxX64();\n"
    "}\n"
    "pub EmitRows :: (target: string, rows: facts.Row[]) -> u8[] {\n"
    "    return scalar.EmitBytesFor(Target(target), rows);\n"
    "}\n"
    "pub EmitSize :: (rows: facts.Row[]) -> int {\n"
    "    return length(scalar.EmitBytesFor(abi.LinuxX64(), rows));\n"
    "}\n"
    "pub ValidateRows :: (rows: facts.Row[]) -> string { return "
    "scalar.Validate(rows); }\n"
    "pub CaseCount :: () -> int { return cases.Count(); }\n"
    "pub CaseName :: (index: int) -> string { return cases.CaseName(index); }\n"
    "pub CaseSource :: (index: int) -> string { return cases.Source(index); }\n"
    "pub CaseExpected :: (index: int) -> int { return "
    "cases.ExpectedValue(index); }\n"
    "pub CaseValue :: (rows: facts.Row[]) -> i64 { return cases.Value(rows); "
    "}\n"
    "pub CaseCheck :: (rows: facts.Row[]) -> int { return cases.Check(rows); "
    "}\n"
    "pub TextType :: () -> string { return abi.TextType(); }\n"
    "pub EmitModule :: (target: string, functions: native.Function[], "
    "parameters: int[], slots: int[],\n"
    "                   locals: int[], code: native.Instruction[], arguments: "
    "int[], records: native.Record[],\n"
    "                   fields: int[], texts: string[], source: native.Source, "
    "names: string[], lines: int[],\n"
    "                   columns: int[], main: bool, budget: int) -> string {\n"
    "    switch native.Emit(Target(target), functions, parameters, slots, "
    "locals, code, arguments, records, fields,\n"
    "                       texts, source, names, lines, columns, main, "
    "budget) {\n"
    "        case .Emitted(text):\n"
    "            return text;\n"
    "        case .Rejected(instruction, message):\n"
    "            return \"\";\n"
    "    }\n"
    "}\n";

static compiler_host *producer, *consumer;
static native_capabilities *native;
static char *target_json; /* The catalog the census sources may call, as JSON parts. */
static size_t target_length;
static arena scratch;

/* scalar_facts.Row, field by field. */
static const struct
{
    const char *name;
    pin_type type;
} FIELDS[ROW_FIELDS] = { { "kind", PIN_I32 },    { "type", PIN_I32 },
                         { "value", PIN_I32 },   { "count", PIN_I32 },
                         { "literal", PIN_I64 }, { "name", PIN_STR },
                         { "text", PIN_STR },    { "start", PIN_I32 },
                         { "end", PIN_I32 },     { "line", PIN_I32 },
                         { "column", PIN_I32 } };

static void close_hosts()
{
    compiler_host_free(consumer);
    consumer = NULL;
    compiler_host_free(producer);
    producer = NULL;
    native_capabilities_free(native);
    native = NULL;
    free(target_json);
    target_json = NULL;
    arena_free(&scratch);
}
void scalar_host_close()
{
    close_hosts();
}
bool scalar_host_open()
{
    if (producer) {
        return true;
    }
    const native_definition_pack packs[] = { NATIVE_COMPILER_TEXT_DEFINITION_PACK,
                                             NATIVE_ARRAY_DEFINITION_PACK,
                                             COMPILER_JSON_DEFINITION_PACK,
                                             COMPILER_SYNTAX_DEFINITION_PACK };
    char error[512];
    native = native_capabilities_from_definition_packs(NULL, 0, packs, 4, error, sizeof error);
    REQUIRE(native);
    target_json = compiler_target_json(native_capabilities_catalog(native), &target_length);
    REQUIRE(target_json);
    const compiler_bootstrap options = {
        .folder = CHIBI_SOURCE_ROOT "/tools/compiler/bootstrap",
        .compiler = CHIBI_SOURCE_ROOT "/tools/compiler",
        .core = CHIBI_SOURCE_ROOT "/packages/core",
        .cache = test_compiler_cache()
    };
    producer = compiler_bootstrap_load(&options, error, sizeof error);
    if (!producer) {
        fprintf(stderr, "scalar_host bootstrap: %s\n", error);
    }
    REQUIRE(producer);
    return true;
}

/* Runs entry `symbol` of host with the arguments JSON, giving the parsed
 * result, whose root is the array of the entry's results. */
static bool invoke(compiler_host *host, const char *symbol, yyjson_mut_doc *arguments, yyjson_doc **result)
{
    size_t length;
    char *json = yyjson_mut_write(arguments, 0, &length);
    yyjson_mut_doc_free(arguments);
    REQUIRE(json);
    compiler_host_result ran = { 0 };
    bool called = compiler_host_invoke(host, compiler_host_entry(symbol), json, length, CALL_BUDGET, &ran);
    free(json);
    if (!called) {
        fprintf(stderr, "scalar_host %s: %s\n", symbol, ran.diagnostic);
    }
    REQUIRE(called && ran.flow_status == FLOW_OK);
    *result = yyjson_read(ran.json, ran.length, COMPILER_HOST_JSON_READ_FLAGS);
    free(ran.json);
    REQUIRE(*result);
    return true;
}

static const pin_record *row_shape(arena *storage)
{
    type_field fields[ROW_FIELDS];
    for (int i = 0; i < ROW_FIELDS; i++) {
        fields[i] = (type_field){ .name = FIELDS[i].name, .type = FIELDS[i].type };
    }
    return pin_record_create(storage, "scalar_facts.Row", ROW_ID, fields, ROW_FIELDS);
}

/* The rows `array` holds as records in storage. */
static bool rows_from_json(yyjson_val *array, arena *storage, pin_value *out)
{
    REQUIRE(yyjson_is_arr(array));
    int count = (int)yyjson_arr_size(array);
    const pin_record *shape = row_shape(storage);
    REQUIRE(shape);
    pin_array
        *items = pin_array_snapshot_allocate(storage, PIN_STRUCT, shape->type.name, count);
    items->record = shape;
    for (int i = 0; i < count; i++) {
        yyjson_val *object = yyjson_arr_get(array, (size_t)i);
        pin_value row = { .type = PIN_STRUCT,
                          .ptr = arena_alloc(storage, shape->type.size),
                          .record = shape,
                          .type_name = shape->type.name };
        memset(row.ptr, 0, shape->type.size);
        for (int f = 0; f < ROW_FIELDS; f++) {
            yyjson_val *field_json = yyjson_obj_get(object, FIELDS[f].name);
            REQUIRE(field_json);
            const type_field
                *field = pin_type_field_path(pin_value_type(&row), FIELDS[f].name);
            REQUIRE(field);
            pin_value value = { 0 };
            if (FIELDS[f].type == PIN_STR) {
                REQUIRE(yyjson_is_str(field_json));
                REQUIRE(
                    !pin_value_text(storage, &value, yyjson_get_str(field_json), yyjson_get_len(field_json))
                );
            } else if (FIELDS[f].type == PIN_I64) {
                value = (pin_value){ .type = PIN_I64,
                                     .i64 = yyjson_get_sint(field_json) };
            } else {
                value = (pin_value){ .type = PIN_I32,
                                     .i = (int32_t)yyjson_get_sint(field_json) };
            }
            REQUIRE(!pin_write_field(storage, row.ptr, field, value));
        }
        items->items[i] = row;
    }
    *out = (pin_value){ .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = items,
                        .record = shape,
                        .type_name = shape->type.name };
    return true;
}

/* The rows in `rows` as a JSON array in doc. */
static yyjson_mut_val *rows_to_json(yyjson_mut_doc *doc, const pin_value *rows)
{
    const pin_array *items = rows->ptr;
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    for (int i = 0; i < items->count; i++) {
        const pin_value *row = &items->items[i];
        yyjson_mut_val *object = yyjson_mut_obj(doc);
        for (int f = 0; f < ROW_FIELDS; f++) {
            const type_field
                *field = pin_type_field_path(pin_value_type(row), FIELDS[f].name);
            pin_value value;
            if (!field || pin_read_field(&scratch, row->ptr, field, &value)) {
                return NULL;
            }
            if (FIELDS[f].type == PIN_STR) {
                pin_string text = pin_text(value);
                yyjson_mut_obj_add_strncpy(
                    doc,
                    object,
                    FIELDS[f].name,
                    text.bytes ? text.bytes : "",
                    text.length
                );
            } else if (FIELDS[f].type == PIN_I64) {
                yyjson_mut_obj_add_sint(doc, object, FIELDS[f].name, value.i64);
            } else {
                yyjson_mut_obj_add_sint(doc, object, FIELDS[f].name, value.i);
            }
        }
        yyjson_mut_arr_append(array, object);
    }
    return array;
}

/* CollectScalar's trailing arguments: the target and, past the prepared ABI,
 * the instruction names, natives, ports and identities. */
static bool append_target(yyjson_mut_doc *doc, yyjson_mut_val *arguments)
{
    yyjson_doc *parts = yyjson_read(target_json, target_length, 0);
    REQUIRE(parts);
    for (size_t part = 0; part < 6; part++) {
        if (part != 1) {
            yyjson_mut_arr_append(
                arguments,
                yyjson_val_mut_copy(doc, yyjson_arr_get(yyjson_doc_get_root(parts), part))
            );
        }
    }
    yyjson_doc_free(parts);
    return true;
}

bool scalar_host_collect(const char *path, const char *source, arena *storage, pin_value *rows)
{
    REQUIRE(scalar_host_open());
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *arguments = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, arguments);
    yyjson_mut_arr_add_str(doc, arguments, "/project");
    yyjson_mut_arr_add_str(doc, arguments, path);
    yyjson_mut_arr_add_str(doc, arguments, source);
    REQUIRE(append_target(doc, arguments));
    yyjson_doc *result;
    REQUIRE(invoke(producer, "project:package_compiler:1:CollectScalar:callable", doc, &result));
    bool read = rows_from_json(yyjson_arr_get(yyjson_doc_get_root(result), 0), storage, rows);
    yyjson_doc_free(result);
    return read;
}

bool scalar_host_collect_packages(const source_package_input *inputs, int count, arena *storage, pin_value *rows)
{
    return scalar_host_collect_collection(NULL, NULL, inputs, count, storage, rows);
}

bool scalar_host_collect_collection(
    const char *collection,
    const char *collection_path,
    const source_package_input *inputs,
    int count,
    arena *storage,
    pin_value *rows
)
{
    REQUIRE(scalar_host_open());
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *arguments = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, arguments);
    yyjson_mut_arr_add_str(doc, arguments, "/project");
    yyjson_mut_val *collection_names = yyjson_mut_arr_add_arr(doc, arguments);
    yyjson_mut_val *collection_paths = yyjson_mut_arr_add_arr(doc, arguments);
    if (collection) {
        yyjson_mut_arr_add_str(doc, collection_names, collection);
        yyjson_mut_arr_add_str(doc, collection_paths, collection_path);
    }
    yyjson_mut_val *paths = yyjson_mut_arr_add_arr(doc, arguments);
    yyjson_mut_val *sources = yyjson_mut_arr_add_arr(doc, arguments);
    for (int i = 0; i < count; i++) {
        yyjson_mut_arr_add_str(doc, paths, inputs[i].path);
        yyjson_mut_arr_add_strncpy(
            doc,
            sources,
            inputs[i].text,
            inputs[i].size ? inputs[i].size : strlen(inputs[i].text)
        );
    }
    REQUIRE(append_target(doc, arguments));
    yyjson_doc *result;
    REQUIRE(
        invoke(producer, "project:package_compiler:1:CollectScalarPackages:callable", doc, &result)
    );
    bool read = rows_from_json(yyjson_arr_get(yyjson_doc_get_root(result), 0), storage, rows);
    yyjson_doc_free(result);
    return read;
}

/* Compiles the consumer packages behind ROOT_SOURCE to one closure and loads
 * it. */
static bool open_consumer()
{
    if (consumer) {
        return true;
    }
    REQUIRE(scalar_host_open());
    static const source_package_input files[] = {
        { ROOT_PATH, ROOT_SOURCE, sizeof ROOT_SOURCE - 1 },
        SCALAR_CONSUMER_SOURCE_INPUTS,
        PACKAGE_COMPILER_SOURCE("scalar_cases/Cases.chibi", SCALAR_CASES_SOURCE)
    };
    enum
    {
        FILES = (int)(sizeof files / sizeof *files)
    };
    const char *paths[FILES], *texts[FILES];
    size_t sizes[FILES];
    char *owned[FILES] = { 0 };
    for (int i = 0; i < FILES; i++) {
        const char *path = files[i].path;
        if (path[0] != '/') {
            size_t length = strlen(path) + sizeof "/compiler/";
            owned[i] = xmalloc(length);
            snprintf(owned[i], length, "/compiler/%s", path);
            path = owned[i];
        }
        paths[i] = path;
        texts[i] = files[i].text;
        sizes[i] = files[i].size;
    }
    const char *names[] = { "core" }, *roots[] = { "/sdk/core" };
    size_t length;
    char *json = compiler_package_request_json(
        "/compiler",
        names,
        roots,
        1,
        paths,
        texts,
        sizes,
        FILES,
        native_capabilities_catalog(native),
        &length
    );
    for (int i = 0; i < FILES; i++) {
        free(owned[i]);
    }
    REQUIRE(json);
    compiler_host_bytes closure = { 0 };
    bool compiled = compiler_host_invoke_bytes(
        producer,
        compiler_host_entry("project:package_compiler:1:CompileClosureWithEffects:callable"),
        json,
        length,
        CALL_BUDGET,
        COMPILER_HOST_MAX_OUTPUT_BYTES,
        &closure
    );
    free(json);
    if (!compiled || !closure.length) {
        fprintf(
            stderr,
            "scalar_host consumer closure: %s (%s:%d:%d)\n",
            closure.diagnostic,
            closure.source.asset,
            closure.source.line,
            closure.source.column
        );
    }
    REQUIRE(compiled && closure.length);
    char error[256];
    consumer = compiler_host_load(closure.bytes, closure.length, behavior_default_backend(), error, sizeof error);
    free(closure.bytes);
    if (!consumer) {
        fprintf(stderr, "scalar_host consumer load: %s\n", error);
    }
    REQUIRE(consumer);
    return true;
}

bool scalar_host_consumer(const char *entry, yyjson_mut_doc *arguments, yyjson_doc **result)
{
    REQUIRE(open_consumer());
    char symbol[128];
    snprintf(symbol, sizeof symbol, "project:roots:1:%s:callable", entry);
    return invoke(consumer, symbol, arguments, result);
}

/* A call whose only result is the first of the result array. */
static bool call_one(const char *entry, yyjson_mut_doc *arguments, yyjson_doc **result, yyjson_val **value)
{
    REQUIRE(scalar_host_consumer(entry, arguments, result));
    *value = yyjson_arr_get(yyjson_doc_get_root(*result), 0);
    REQUIRE(*value);
    return true;
}
static yyjson_mut_doc *arguments_doc(yyjson_mut_val **arguments)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    *arguments = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, *arguments);
    return doc;
}
static char *copy_text(yyjson_val *text)
{
    if (!yyjson_is_str(text)) {
        return NULL;
    }
    size_t length = yyjson_get_len(text);
    char *copy = xmalloc(length + 1);
    memcpy(copy, yyjson_get_str(text), length);
    copy[length] = 0;
    return copy;
}

bool scalar_host_validate(const pin_value *rows, char **message)
{
    yyjson_mut_val *arguments;
    yyjson_mut_doc *doc = arguments_doc(&arguments);
    arena_free(&scratch);
    yyjson_mut_val *json = rows_to_json(doc, rows);
    REQUIRE(json);
    yyjson_mut_arr_append(arguments, json);
    yyjson_doc *result;
    yyjson_val *text;
    REQUIRE(call_one("ValidateRows", doc, &result, &text));
    *message = copy_text(text);
    yyjson_doc_free(result);
    return *message;
}

bool scalar_host_emit(const char *target, const pin_value *rows, char **ir)
{
    yyjson_mut_val *arguments;
    yyjson_mut_doc *doc = arguments_doc(&arguments);
    yyjson_mut_arr_add_str(doc, arguments, target);
    arena_free(&scratch);
    yyjson_mut_val *json = rows_to_json(doc, rows);
    REQUIRE(json);
    yyjson_mut_arr_append(arguments, json);
    yyjson_doc *result;
    yyjson_val *bytes;
    REQUIRE(call_one("EmitRows", doc, &result, &bytes));
    REQUIRE(yyjson_is_arr(bytes));
    size_t count = yyjson_arr_size(bytes);
    *ir = xmalloc(count + 1);
    size_t index, max;
    yyjson_val *item;
    yyjson_arr_foreach(bytes, index, max, item)
    {
        (*ir)[index] = (char)yyjson_get_uint(item);
    }
    (*ir)[count] = 0;
    yyjson_doc_free(result);
    return true;
}

bool scalar_host_case_count(int *count)
{
    yyjson_mut_val *arguments;
    yyjson_mut_doc *doc = arguments_doc(&arguments);
    yyjson_doc *result;
    yyjson_val *value;
    REQUIRE(call_one("CaseCount", doc, &result, &value));
    *count = (int)yyjson_get_sint(value);
    yyjson_doc_free(result);
    return true;
}

bool scalar_host_case_text(bool source, int index, char **text)
{
    yyjson_mut_val *arguments;
    yyjson_mut_doc *doc = arguments_doc(&arguments);
    yyjson_mut_arr_add_sint(doc, arguments, index);
    yyjson_doc *result;
    yyjson_val *value;
    REQUIRE(call_one(source ? "CaseSource" : "CaseName", doc, &result, &value));
    *text = copy_text(value);
    yyjson_doc_free(result);
    return *text;
}

bool scalar_host_case_expected(int index, int *expected)
{
    yyjson_mut_val *arguments;
    yyjson_mut_doc *doc = arguments_doc(&arguments);
    yyjson_mut_arr_add_sint(doc, arguments, index);
    yyjson_doc *result;
    yyjson_val *value;
    REQUIRE(call_one("CaseExpected", doc, &result, &value));
    *expected = (int)yyjson_get_sint(value);
    yyjson_doc_free(result);
    return true;
}

static bool case_rows(const char *entry, const pin_value *rows, int64_t *out)
{
    yyjson_mut_val *arguments;
    yyjson_mut_doc *doc = arguments_doc(&arguments);
    arena_free(&scratch);
    yyjson_mut_val *json = rows_to_json(doc, rows);
    REQUIRE(json);
    yyjson_mut_arr_append(arguments, json);
    yyjson_doc *result;
    yyjson_val *value;
    REQUIRE(call_one(entry, doc, &result, &value));
    *out = yyjson_get_sint(value);
    yyjson_doc_free(result);
    return true;
}
bool scalar_host_case_value(const pin_value *rows, int64_t *value)
{
    return case_rows("CaseValue", rows, value);
}
bool scalar_host_case_check(const pin_value *rows, int *verdict)
{
    int64_t checked;
    REQUIRE(case_rows("CaseCheck", rows, &checked));
    *verdict = (int)checked;
    return true;
}
