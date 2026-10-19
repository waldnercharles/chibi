#include "package_compiler_chibi.h"

#include "compiler_fixture.h"
#include "execution/definition.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "package_compiler_chibi_source.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "types/array.h"
#include "types/union.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The authored package compiler, prepared by stage zero, compiles each sample
 * package to a closure. A fresh runtime host loads it and calls every function
 * on bytecode and SLJIT, and stage zero's own build of the same package must
 * agree. Rejections report their exact message and position. */
static const char PACKAGE[] = "project:package_compiler";
enum
{
    COMPILE_BUDGET = 20000000,
    TARGET_BUDGET = 100000
};
typedef struct
{
    const char *function, *arguments, *expected;
} sample_call;
typedef struct
{
    const char *name, *source;
    sample_call calls[4];
    const char *message;
    int line, column;
} sample;
static const sample samples[] = {
    { .name = "constant",
      .source = "package facts\nAnswer :: pure fn() -> int { return 42; }",
      .calls = { { "Answer", "[]", "[42]" } } },
    { .name = "parameters",
      .source = "package facts\nAdd :: pure fn(a: int, b: int) -> int { return "
                "a + b * 2 - (a - b) % 3; }",
      .calls = { { "Add", "[3,4]", "[12]" }, { "Add", "[-5,2]", "[0]" } } },
    { .name = "loops",
      .source = "package facts\nSum :: pure fn(n: int) -> int {\n"
                "    total: int = 0;\n    i: int = 0;\n"
                "    while (i < n) { total += i; i += 1; }\n    return "
                "total;\n}",
      .calls = { { "Sum", "[10]", "[45]" }, { "Sum", "[0]", "[0]" } } },
    { .name = "recursion",
      .source = "package facts\nFib :: pure fn(n: int) -> int {\n"
                "    if (n < 2) { return n; }\n    return Fib(n - 1) + Fib(n - "
                "2);\n}",
      .calls = { { "Fib", "[15]", "[610]" } } },
    { .name = "control",
      .source = "package facts\nCount :: pure fn(n: int) -> int {\n"
                "    found: int = 0;\n    i: int = 0;\n"
                "    while (true) {\n        i += 1;\n        if (i > n) { "
                "break; }\n"
                "        if (i % 3 == 0 || i % 5 == 0) { continue; } else { "
                "found += 1; }\n    }\n"
                "    return found;\n}",
      .calls = { { "Count", "[15]", "[8]" } } },
    { .name = "logic",
      .source = "package facts\nEven :: pure fn(n: int) -> bool { return !(n % "
                "2 != 0) && n >= 0; }\n"
                "Both :: pure fn(a: int, b: int) -> bool { return Even(a) && "
                "Even(b) || a == b; }",
      .calls = { { "Even", "[4]", "[true]" },
                 { "Even", "[-4]", "[false]" },
                 { "Both", "[2,4]", "[true]" },
                 { "Both", "[3,3]", "[true]" } } },
    { .name = "unknown name",
      .source = "package facts\nAnswer :: pure fn() -> int { return missing; }",
      .message = "Unknown name 'missing'",
      .line = 2,
      .column = 37 },
    { .name = "arity",
      .source = "package facts\nOne :: pure fn(a: int) -> int { return a; "
                "}\nTwo :: pure fn() -> int { return One(); }",
      .message = "call has the wrong number of arguments",
      .line = 3,
      .column = 34 },
    { .name = "types",
      .source = "package facts\nAnswer :: pure fn() -> int { return true; }",
      .message = "returned value differs from the result type",
      .line = 2,
      .column = 30 },
};
enum
{
    SAMPLES = sizeof samples / sizeof *samples
};
static compiler_fixture fixture, oracle;
static arena input, detached;
static compiler_host *host;
static compiler_host_result result;

static void cleanup()
{
    compiler_fixture_free(&fixture);
    compiler_fixture_free(&oracle);
    arena_free(&input);
    arena_free(&detached);
    compiler_host_free(host);
    host = NULL;
    free(result.json);
    result = (compiler_host_result){ 0 };
}
static pin_value text(const char *s)
{
    size_t bytes = strlen(s) + 1;
    char *copy = arena_alloc(&input, bytes);
    memcpy(copy, s, bytes);
    return (pin_value){ .type = PIN_STR, .str = copy };
}
static pin_value strings(const char *const *items, int count)
{
    pin_array *array = pin_array_snapshot_allocate(&input, PIN_STR, NULL, count);
    for (int i = 0; i < count; i++) {
        array->items[i] = text(items[i]);
    }
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}
static bool put(pin_value *row, const char *name, int value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == PIN_I32);
    pin_write_field(row->ptr, field, (pin_value){ .type = PIN_I32, .i = value });
    return true;
}
static bool get(const pin_value *row, const char *name, pin_value *out)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    pin_read_field(row->ptr, field, out);
    return true;
}
static uint64_t tag(int b, const char *variant)
{
    const authoring_package *owner = compiler_fixture_package(&fixture, b, PACKAGE);
    const script_catalog_type *type = script_catalog_type_by_id(
        owner->exports->metadata,
        compiler_fixture_export(&fixture, b, PACKAGE, "Result")
    );
    for (int i = 0; type && i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, variant)) {
            return type->variants[i].tag;
        }
    }
    return 0;
}
static bool prepare()
{
    const source_package_input sources[] = { PACKAGE_COMPILER_SOURCE_INPUTS };
    native_definition_pack packs[] = { COMPILER_JSON_DEFINITION_PACK,
                                       COMPILER_SYNTAX_DEFINITION_PACK };
    REQUIRE(
        compiler_fixture_prepare_packs(&fixture, sources, (int)(sizeof sources / sizeof *sources), packs, 2)
    );
    return true;
}
static bool compile(int b, const sample *s, pin_value *out)
{
    const char *paths[] = { "facts/Main.chibi" };
    pin_value args[6];
    args[0] = text("/project");
    args[1] = strings(paths, 1);
    args[2] = strings(&s->source, 1);
    const pin_record *record = compiler_fixture_record(&fixture, b, PACKAGE, "Target", &input);
    REQUIRE(record);
    args[3] = (pin_value){ .type = PIN_STRUCT,
                           .ptr = arena_alloc(&input, record->type.size),
                           .record = record,
                           .type_name = record->type.name };
    memset(args[3].ptr, 0, record->type.size);
    REQUIRE(put(&args[3], "pointer_storage", BF_POINTER_TYPE));
    REQUIRE(put(&args[3], "value_size", (int)sizeof(pin_value)));
    REQUIRE(put(&args[3], "payload_offset", (int)offsetof(pin_value, d)));
    REQUIRE(put(&args[3], "results_offset", (int)offsetof(behavior_function_context, results)));
    args[4] = (pin_value){ .type = PIN_U64, .u64 = execution_prepared_abi() };
    int count = execution_prepared_instruction_count();
    pin_array *names = pin_array_snapshot_allocate(&input, PIN_STR, NULL, count);
    for (int i = 0; i < count; i++) {
        names->items[i] = text(execution_prepared_instruction_name(i));
    }
    args[5] = (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = names };
    REQUIRE(
        compiler_fixture_call(&fixture, b, PACKAGE, "Compile", args, 6, COMPILE_BUDGET, &detached, out)
    );
    return true;
}
static uint64_t function_id(const char *name)
{
    char text[256];
    snprintf(text, sizeof text, "project:facts:1:%s:callable", name);
    return script_catalog_id("package-symbol", text);
}
static bool run(const unsigned char *bytes, size_t length, const sample_call *call, int backend)
{
    char error[256];
    host = compiler_host_load(bytes, length, backend ? BEHAVIOR_SLJIT : BEHAVIOR_BYTECODE, error, sizeof error);
    if (!host) {
        fprintf(stderr, "package compiler load: %s\n", error);
    }
    REQUIRE(host);
    bool valid = compiler_host_invoke(
        host,
        function_id(call->function),
        call->arguments,
        strlen(call->arguments),
        TARGET_BUDGET,
        &result
    );
    if (!valid || strcmp(result.json, call->expected)) {
        fprintf(
            stderr,
            "package compiler run %s%s: %s %s\n",
            call->function,
            call->arguments,
            valid ? result.json : "",
            result.diagnostic
        );
    }
    REQUIRE(valid && result.flow_status == FLOW_OK && !strcmp(result.json, call->expected));
    compiler_host_free(host);
    host = NULL;
    free(result.json);
    result = (compiler_host_result){ 0 };
    return true;
}
/* Stage zero builds the same package; its result must match. */
static bool agree(const sample *s, const sample_call *call)
{
    const source_package_input inputs[] = {
        { "facts/Main.chibi", s->source, strlen(s->source) }
    };
    compiler_fixture_free(&oracle);
    REQUIRE(compiler_fixture_prepare(&oracle, inputs, 1));
    /* JSON integer and bool arguments only. */
    pin_value args[4];
    int count = 0;
    const char *at = call->arguments + 1;
    while (*at && *at != ']') {
        args[count++] = (pin_value){ .type = PIN_I32,
                                     .i = (int)strtol(at, (char **)&at, 10) };
        at += *at == ',';
    }
    pin_value out;
    arena scratch = { 0 };
    REQUIRE(
        compiler_fixture_call(&oracle, 0, "project:facts", call->function, args, count, TARGET_BUDGET, &scratch, &out)
    );
    char text[64];
    snprintf(text, sizeof text, out.type == PIN_BOOL ? (out.b ? "[true]" : "[false]") : "[%d]", out.i);
    arena_free(&scratch);
    REQUIRE(!strcmp(text, call->expected));
    return true;
}
TEST_CASE(package_compiler_builds_runnable_scalar_packages)
{
    REQUIRE(prepare());
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < (int)SAMPLES; i++) {
            const sample *s = &samples[i];
            pin_value out;
            REQUIRE(compile(b, s, &out));
            const pin_union *value = out.ptr;
            REQUIRE(out.type == PIN_UNION && value);
            if (s->message) {
                REQUIRE(value->tag == tag(b, "Rejected"));
                pin_value message, line, column;
                REQUIRE(get(&value->values[0], "message", &message));
                REQUIRE(
                    get(&value->values[0], "line", &line) &&
                    get(&value->values[0], "column", &column)
                );
                if (strcmp(message.str, s->message) || line.i != s->line ||
                    column.i != s->column) {
                    fprintf(
                        stderr,
                        "package compiler %s: %s at %d:%d\n",
                        s->name,
                        message.str,
                        line.i,
                        column.i
                    );
                }
                REQUIRE(
                    !strcmp(message.str, s->message) && line.i == s->line &&
                    column.i == s->column
                );
                arena_free(&detached);
                continue;
            }
            if (value->tag != tag(b, "Compiled")) {
                pin_value message;
                get(&value->values[0], "message", &message);
                fprintf(
                    stderr,
                    "package compiler %s rejected: %s\n",
                    s->name,
                    message.str
                );
            }
            REQUIRE(value->tag == tag(b, "Compiled") && value->count == 2);
            const pin_array *array = value->values[1].ptr;
            size_t length = (size_t)array->count;
            unsigned char *bytes = xmalloc(length);
            for (size_t k = 0; k < length; k++) {
                bytes[k] = array->items[k].u8;
            }
            arena_free(&detached);
            for (int c = 0; c < 4 && s->calls[c].function; c++) {
                for (int backend = 0; backend < 2; backend++) {
                    bool ran = run(bytes, length, &s->calls[c], backend);
                    if (!ran) {
                        free(bytes);
                    }
                    REQUIRE(ran);
                }
                if (!b) {
                    REQUIRE(agree(s, &s->calls[c]));
                }
            }
            free(bytes);
        }
    }
    return true;
}
void test_language_package_compiler_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_compiler_builds_runnable_scalar_packages);
    pu_clear_setup();
}
