#include "bounded_compiler_chibi.h"

#include "backend.h"
#include "bounded_compiler_chibi_source.h"
#include "compiler_fixture.h"
#include "compiler_prepared_fixture.h"
#include "execution/definition.h"
#include "execution/module.h"
#include "execution/prepared.h"
#include "execution/service.h"
#include "execution/status.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_prepared_scalar.h"
#include "toolchain/compiler_syntax.h"
#include "types/array.h"
#include "types/union.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bootstrap parses only the 18 shipped compiler/dependency files, never any
 * CASE source below. The authored driver alone chooses semantic/lowering facts
 * from each user source. Its private raw
 * parser and prepared-payload callback are interposed only for work counting.
 * One fixed corpus, two compiler backends, four compiler/target combinations;
 * no timing loops, subprocesses, runtime files, network, rendering or GPU.
 * One production shared 60-second deadline bounds the run. Context budgets
 * reset in child activations and are not an aggregate instruction ceiling.
 * See docs/bounded-authored-compiler.md. */
enum
{
    ACCEPTED = 14,
    REJECTED = 28,
    CASES = ACCEPTED + REJECTED,
    SOURCE_CAP = 1024,
    PATH_CAP = 240,
    ARTIFACT_CAP = 64 * 1024 + 56 + 240,
    COMPILE_BUDGET = 3000000,
    TARGET_BUDGET = 128
};
static const char PACKAGE[] = "project:bounded_compiler";
static const char
    VALID[] = "package facts\nAnswer :: pure fn()->int { return 42; }";
typedef struct
{
    const char *name;
    const char *project;
    const char *paths[4];
    const char *sources[4];
    int path_count, source_count;
    const char *expected;
    bool wrong_abi;
} request;
static request cases[CASES];
/* Each rejected case's exact diagnostic and 1-based line:column, in order. */
typedef struct
{
    const char *message;
    int line, column;
} rejection;
static const rejection rejections[REJECTED] = {
    { "Expected }", 2, 36 },
    { "Package sibling headers differ", 1, 9 },
    { "unsupported declaration in bounded package", 2, 1 },
    { "bounded function body needs exactly one ordinary return", 2, 26 },
    { "Imported package is missing from the snapshot", 2, 12 },
    { "bounded return expression differs from its declared result type", 2, 35 },
    { "bounded Boolean context cannot take a number", 2, 36 },
    { "bounded compiler requires one to three complete source files", 1, 1 },
    { "bounded source path or text exceeds its limit", 1, 1 },
    { "bounded compiler requires one to three complete source files", 1, 1 },
    { "bounded compiler requires one to three complete source files", 1, 1 },
    { "bounded compiler supports zero-argument functions only", 2, 1 },
    { "bounded compiler requires a nongeneric pure function", 2, 1 },
    { "bounded package supports exactly one function", 3, 1 },
    { "bounded compiler needs a decimal literal in the int range", 2, 35 },
    { "bounded compiler requires one ordinary int or bool result", 2, 22 },
    { "bounded integer operands need explicit int(value)", 2, 35 },
    { "bounded compiler supports no call except int(value)", 2, 35 },
    { "bounded function body needs exactly one ordinary return", 2, 26 },
    { "Expected package header", 1, 1 },
    { "bounded compiler supports no call except int(value)", 2, 36 },
    { "bounded compiler supports no call except int(value)", 2, 35 },
    { "bounded compiler reserves int and bool for conversions", 2, 1 },
    { "bounded compiler requires one ordinary int or bool result", 2, 1 },
    { "bounded expression exceeds its 24-row limit", 2, 36 },
    { "unsupported bounded compiler root or target", 1, 1 },
    { "bounded source path or text exceeds its limit", 1, 1 },
    { "unsupported bounded compiler root or target", 1, 1 },
};
typedef struct
{
    unsigned char *bytes;
    size_t length;
    uint64_t module, entry;
    int location[5];
    char diagnostic[512];
} saved;
static saved outputs[2][CASES];
static compiler_fixture fixture;
static arena input, producer, detached, bridge_input;
static pin_value call_result, bridge_plan;
static compiler_prepared_scalar_declaration bridge_declaration;
static uint64_t bridge_ready;
static unsigned char *bridge_bytes;
static size_t bridge_length;
static int bridge_rejections;
static behavior_run_counts bootstrap_counts;
static behavior_function_context context;
static behavior_function_activation activation;
static service_binding *services;
static compiler_prepared_fixture_service configuration;
static node_host_call payload_call, syntax_call;
static native_definition_binding payload_binding, syntax_binding;
static compiler_host *host;
static compiler_host_result target;
static unsigned char *load_bytes;
static int compile_calls, payload_calls, raw_parses, target_runs, offered_files;
static char padded[SOURCE_CAP + 1], oversized[SOURCE_CAP + 2];
static char long_path[PATH_CAP + 2], expression24[192], expression25[192];
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };

static void cleanup_call()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    arena_free(&producer);
    arena_free(&input);
    arena_free(&detached);
    afree(services);
}
static void cleanup()
{
    cleanup_call();
    compiler_host_free(host);
    host = NULL;
    free(target.json);
    target = (compiler_host_result){ 0 };
    free(load_bytes);
    load_bytes = NULL;
    free(bridge_bytes);
    bridge_bytes = NULL;
    arena_free(&bridge_input);
    bridge_plan = (pin_value){ 0 };
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < CASES; i++) {
            free(outputs[b][i].bytes);
            outputs[b][i] = (saved){ 0 };
        }
    }
    compiler_fixture_free(&fixture);
}
static const char *observe_payload(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *arguments,
    pin_value *results
)
{
    payload_calls++;
    if (!bridge_ready) {
        if (pin_value_snapshot_to_arena(&bridge_input, &bridge_plan, &arguments[0])) {
            return "Cannot retain bounded compiler plan for bridge rejection "
                   "checks";
        }
        const compiler_prepared_fixture_service *config = service;
        bridge_ready = config->ready_tag;
        size_t bytes = strlen(arguments[3].str) + 1;
        char *entry = arena_alloc(&bridge_input, bytes);
        memcpy(entry, arguments[3].str, bytes);
        bridge_declaration = (compiler_prepared_scalar_declaration){
            .module = arguments[1].u64,
            .function = arguments[2].u64,
            .entry = entry,
            .result = arguments[4].i
        };
    }
    return COMPILER_PREPARED_FIXTURE_DEFINITION_PACK.bindings[0]
        .implementation.call->context(ctx, service, arguments, results);
}
static const char *observe_syntax(
    const behavior_function_context *ctx,
    void *service,
    const pin_value *arguments,
    pin_value *results
)
{
    raw_parses++;
    if (arguments[0].type != PIN_STR || !arguments[0].str ||
        strlen(arguments[0].str) > SOURCE_CAP) {
        return "Bounded compiler passed oversized source to its raw parser";
    }
    return COMPILER_SYNTAX_DEFINITION_PACK.bindings[0]
        .implementation.call->context(ctx, service, arguments, results);
}
static pin_value integer(int n)
{
    return (pin_value){ .type = PIN_I32, .i = n };
}
static pin_value text(const char *s)
{
    size_t bytes = strlen(s) + 1;
    char *copy = arena_alloc(&input, bytes);
    memcpy(copy, s, bytes);
    return (pin_value){ .type = PIN_STR, .str = copy };
}
static bool put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    pin_write_field(row->ptr, field, value);
    return true;
}
static bool get(const pin_value *row, const char *name, pin_value *out)
{
    REQUIRE(row->type == PIN_STRUCT && row->ptr);
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field);
    pin_read_field(row->ptr, field, out);
    return true;
}
static uint64_t tag(int b, const char *package, const char *name, const char *variant)
{
    const authoring_package *owner = compiler_fixture_package(&fixture, b, package);
    if (!owner || !owner->exports) {
        return 0;
    }
    const script_catalog_type *type = script_catalog_type_by_id(
        owner->exports->metadata,
        compiler_fixture_export(&fixture, b, package, name)
    );
    for (int i = 0; type && i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, variant)) {
            return type->variants[i].tag;
        }
    }
    return 0;
}
static pin_value strings(const char *const *items, int count)
{
    pin_array *array = pin_array_snapshot_allocate(&input, PIN_STR, NULL, count);
    for (int i = 0; i < count; i++) {
        array->items[i] = text(items[i]);
    }
    return (pin_value){ .type = PIN_STR, .container = PIN_CONTAINER_ARRAY, .ptr = array };
}
static bool pack(int b, const request *r, pin_value *args)
{
    args[0] = text(r->project);
    args[1] = strings(r->paths, r->path_count);
    args[2] = strings(r->sources, r->source_count);
    const pin_record *record = compiler_fixture_record(&fixture, b, "project:scalar_lowering", "Target", &input);
    REQUIRE(record && record->type.field_count == 6);
    args[3] = (pin_value){ .type = PIN_STRUCT,
                           .ptr = arena_alloc(&input, record->type.size),
                           .record = record,
                           .type_name = record->type.name };
    memset(args[3].ptr, 0, record->type.size);
    const char *fields[] = { "schema",     "prepared",       "pointer_storage",
                             "value_size", "payload_offset", "results_offset" };
    int values[] = { 1,
                     EXECUTION_PREPARED_VERSION,
                     BF_POINTER_TYPE,
                     sizeof(pin_value),
                     offsetof(pin_value, d),
                     offsetof(behavior_function_context, results) };
    for (int i = 0; i < 6; i++) {
        REQUIRE(put(&args[3], fields[i], integer(values[i])));
    }
    if (r->wrong_abi) {
        REQUIRE(put(&args[3], "prepared", integer(EXECUTION_PREPARED_VERSION + 1)));
    }
    return true;
}
static bool compile(int b, pin_value *args, pin_value *out)
{
    const authoring_package *package = compiler_fixture_package(&fixture, b, PACKAGE);
    REQUIRE(package && package->valid && package->module);
    uint64_t entry = compiler_fixture_export(&fixture, b, PACKAGE, "Compile");
    REQUIRE(entry);
    const script_catalog_capability
        *signature = script_catalog_capability_by_id(package->exports->metadata, entry);
    REQUIRE(signature && signature->result_count == 1);
    const behavior_function *function = NULL;
    for (int i = 0; i < execution_module_function_count(package->module); i++) {
        execution_module_function_info
            info = execution_module_function_at(package->module, i);
        if (info.id == entry) {
            function = info.body;
        }
    }
    REQUIRE(function);
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &activation);
    }
    call_result = (pin_value){ .type = signature->results[0].type.storage,
                               .container = signature->results[0].type.container };
    context = (behavior_function_context){
        .activation = behavior_function_resumable(function) ? &activation : NULL,
        .arguments = args,
        .argument_count = 4,
        .results = &call_result,
        .budget = COMPILE_BUDGET
    };
    behavior_function_bind_storage(&context, &producer);
    configuration.ready_tag = tag(b, "project:scalar_lowering", "Plan", "Ready");
    REQUIRE(configuration.ready_tag);
    apush(services, ((service_binding){ COMPILER_PREPARED_FIXTURE_SERVICE, &configuration }));
    REQUIRE(behavior_function_bind(function, &context, services, 0));
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    compile_calls++;
    int status = behavior_function_run(function, &context, b != 0);
    behavior_run_scope_end(&runs);
    if (status != FLOW_OK || !context.returned) {
        const behavior_function_source *at = context.error_source >= 0 &&
                                                     context.error_source <
                                                         acount(function->sources)
                                               ? &function->sources[context.error_source]
                                               : NULL;
        fprintf(
            stderr,
            "bounded compiler: backend=%d status=%d returned=%d failure=%d at "
            "%s:%d:%d: %s\n",
            b,
            status,
            context.returned,
            (int)context.failure,
            at && at->asset_name ? at->asset_name : "?",
            at ? at->line : 0,
            at ? at->column : 0,
            context.error_message ? context.error_message : ""
        );
    }
    REQUIRE(status == FLOW_OK && context.returned);
    REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
    REQUIRE(!runs.counts.wasm_entries);
    REQUIRE(
        b ? runs.counts.sljit_entries > 0 && !runs.counts.bytecode_entries
          : runs.counts.bytecode_entries > 0 && !runs.counts.sljit_entries
    );
    REQUIRE(!pin_value_snapshot_to_arena(&detached, out, &call_result));
    /* Deliberately retire arguments, activation and producer before examining
     * detached output, including diagnostic text and each byte element. */
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    arena_free(&producer);
    arena_free(&input);
    afree(services);
    return true;
}
static uint64_t little_endian(const unsigned char *bytes)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value |= (uint64_t)bytes[i] << (8 * i);
    }
    return value;
}
static bool retain(int b, int index, const pin_value *result)
{
    saved *out = &outputs[b][index];
    REQUIRE(result->type == PIN_UNION && result->ptr);
    const pin_union *value = result->ptr;
    if (index >= ACCEPTED) {
        REQUIRE(value->tag == tag(b, PACKAGE, "Result", "Rejected") && value->count == 1);
        const char *fields[] = { "start", "end", "line", "column", "document" };
        for (int i = 0; i < 5; i++) {
            pin_value field;
            REQUIRE(get(&value->values[0], fields[i], &field) && field.type == PIN_I32);
            out->location[i] = field.i;
        }
        pin_value diagnostic;
        REQUIRE(get(&value->values[0], "message", &diagnostic));
        REQUIRE(diagnostic.type == PIN_STR && diagnostic.str && diagnostic.str[0]);
        REQUIRE(strlen(diagnostic.str) < sizeof out->diagnostic);
        memcpy(out->diagnostic, diagnostic.str, strlen(diagnostic.str) + 1);
        return true;
    }
    REQUIRE(value->tag == tag(b, PACKAGE, "Result", "Compiled") && value->count == 3);
    REQUIRE(value->values[0].type == PIN_U64 && value->values[0].u64);
    REQUIRE(value->values[1].type == PIN_U64 && value->values[1].u64);
    out->module = value->values[0].u64;
    out->entry = value->values[1].u64;
    REQUIRE(out->module == script_catalog_id("module", "project:facts"));
    REQUIRE(out->entry == script_catalog_id("package-symbol", "project:facts:1:Answer:callable"));
    const pin_value *bytes = &value->values[2];
    REQUIRE(
        bytes->type == PIN_U8 && bytes->container == PIN_CONTAINER_ARRAY &&
        bytes->ptr
    );
    const pin_array *array = bytes->ptr;
    REQUIRE(array->count > 56 && array->count <= ARTIFACT_CAP);
    out->length = (size_t)array->count;
    out->bytes = xmalloc(out->length);
    for (int i = 0; i < array->count; i++) {
        REQUIRE(array->items[i].type == PIN_U8 && array->items[i].container == PIN_CONTAINER_SINGLE);
        out->bytes[i] = array->items[i].u8;
    }
    REQUIRE(!memcmp(out->bytes, "CHIBICLO", 8));
    REQUIRE(little_endian(out->bytes + 8) == 1);
    REQUIRE(little_endian(out->bytes + 16) == out->module);
    REQUIRE(little_endian(out->bytes + 24) == 1);
    REQUIRE(little_endian(out->bytes + 32) == out->module);
    size_t name_length = (size_t)little_endian(out->bytes + 40);
    size_t payload_length = (size_t)little_endian(out->bytes + 48);
    REQUIRE(name_length == strlen("project:facts"));
    REQUIRE(payload_length > 0 && 56 + name_length <= out->length);
    REQUIRE(payload_length == out->length - 56 - name_length);
    REQUIRE(!memcmp(out->bytes + 56, "project:facts", name_length));
    return true;
}
static bool same_artifact(const saved *a, const saved *b)
{
    REQUIRE(a->module == b->module && a->entry == b->entry);
    REQUIRE(a->length == b->length && a->length > 0);
    REQUIRE(!memcmp(a->bytes, b->bytes, a->length));
    return true;
}
static bool run_target(int producer_backend, int index, int target_backend)
{
    const saved *artifact = &outputs[producer_backend][index];
    load_bytes = xmalloc(artifact->length);
    memcpy(load_bytes, artifact->bytes, artifact->length);
    char error[256];
    host = compiler_host_load(load_bytes, artifact->length, backends[target_backend], error, sizeof error);
    free(load_bytes);
    load_bytes = NULL; /* Load must own all data before the offered bytes retire. */
    if (!host) {
        fprintf(stderr, "bounded artifact load: %s\n", error);
    }
    REQUIRE(host);
    target_runs++;
    bool valid = compiler_host_invoke(host, artifact->entry, "[]", 2, TARGET_BUDGET, &target);
    compiler_host_free(host);
    host = NULL; /* Result JSON must outlive the consuming runtime as well. */
    if (!valid) {
        fprintf(stderr, "bounded artifact run: %s\n", target.diagnostic);
    }
    REQUIRE(valid && target.flow_status == FLOW_OK);
    REQUIRE(!target.counts.overflow && !target.counts.inspection_bytecode_fallbacks);
    REQUIRE(!target.counts.wasm_entries);
    REQUIRE(
        target_backend
            ? target.counts.sljit_entries == 1 && !target.counts.bytecode_entries
            : target.counts.bytecode_entries == 1 && !target.counts.sljit_entries
    );
    REQUIRE(target.json && target.length == strlen(cases[index].expected));
    REQUIRE(!strcmp(target.json, cases[index].expected));
    free(target.json);
    target = (compiler_host_result){ 0 };
    return true;
}
static void single(int index, const char *name, const char *source, const char *expected)
{
    cases[index] = (request){ .name = name,
                              .project = "/project",
                              .paths = { "facts/Main.chibi" },
                              .sources = { source },
                              .path_count = 1,
                              .source_count = 1,
                              .expected = expected };
}
static bool corpus()
{
    single(0, "unchanged original", VALID, "[42]");
    single(1, "unchanged repeat", VALID, "[42]");
    single(2, "changed expression", "package facts\nAnswer :: pure fn()->int { return 43; }", "[43]");
    single(
        3,
        "Boolean operators",
        "package facts\nAnswer :: pure fn()->bool { return "
        "(not false and (false or true)) && !false || false; }",
        "[true]"
    );
    single(4, "signed minimum", "package facts\nAnswer :: pure fn()->int { return -2147483648; }", "[-2147483648]");
    single(
        5,
        "wrapping addition",
        "package facts\nAnswer :: pure fn()->int { return "
        "int(2147483647) + int(1); }",
        "[-2147483648]"
    );
    single(
        6,
        "integer arithmetic",
        "package facts\nAnswer :: pure fn()->int { return "
        "-(int(3) * int(4) - int(5)); }",
        "[-7]"
    );
    single(
        7,
        "integer bits",
        "package facts\nAnswer :: pure fn()->int { return "
        "~(int(6) & int(3)) | (int(8) ^ int(1)); }",
        "[-3]"
    );
    single(
        8,
        "ordered equality",
        "package facts\nAnswer :: pure fn()->bool { return "
        "(int(1) < int(2)) && (int(2) == int(2)) && "
        "(int(2) != int(3)); }",
        "[true]"
    );
    single(
        9,
        "ordered comparison",
        "package facts\nAnswer :: pure fn()->bool { return "
        "(int(2) <= int(2)) && (int(3) > int(2)) && "
        "(int(3) >= int(3)); }",
        "[true]"
    );
    cases[10] = (request){
        .name = "three-file comments and headers",
        .project = "/project",
        .paths = { "facts/Header.chibi", "facts/Comments.chibi", "facts/Main.chibi" },
        .sources = { "// header only\npackage facts\n",
                     "package facts\n/* complete package member */\n",
                     "package facts\n// entry is in the third member\n"
                     "Answer :: pure fn()->int { return (42); }" },
        .path_count = 3,
        .source_count = 3,
        .expected = "[42]"
    };
    size_t prefix = strlen(VALID);
    memcpy(padded, VALID, prefix);
    padded[prefix++] = '\n';
    padded[prefix++] = '/';
    padded[prefix++] = '/';
    memset(padded + prefix, 'x', SOURCE_CAP - prefix);
    padded[SOURCE_CAP] = 0;
    memcpy(oversized, padded, SOURCE_CAP);
    oversized[SOURCE_CAP] = 'x';
    oversized[SOURCE_CAP + 1] = 0;
    single(11, "source exactly at cap", padded, "[42]");
    const char *head = "package facts\nAnswer :: pure fn()->bool { return ";
    prefix = strlen(head);
    memcpy(expression24, head, prefix);
    memset(expression24 + prefix, '!', 23);
    strcpy(expression24 + prefix + 23, "false; }");
    memcpy(expression25, head, prefix);
    memset(expression25 + prefix, '!', 24);
    strcpy(expression25 + prefix + 24, "false; }");
    single(12, "24 expression rows", expression24, "[true]");
    single(
        13,
        "Boolean equality",
        "package facts\nAnswer :: pure fn()->bool { return "
        "(true == true) != false; }",
        "[true]"
    );
    int n = ACCEPTED;
    single(n++, "syntax invalid", "package facts\nAnswer :: pure fn()->int { return ;", NULL);
    cases[n++] = (request){ .name = "mixed package names",
                            .project = "/project",
                            .paths = { "facts/One.chibi", "facts/Two.chibi" },
                            .sources = { VALID, "package other\n" },
                            .path_count = 2,
                            .source_count = 2 };
    single(n++, "extra declaration", "package facts\nExtra :: 7;\nAnswer :: pure fn()->int { return 42; }", NULL);
    single(n++, "extra return statement", "package facts\nAnswer :: pure fn()->int { return 42; return 43; }", NULL);
    single(n++, "import", "package facts\nimport dep \"project:other\"\nAnswer :: pure fn()->int { return 42; }", NULL);
    single(n++, "Boolean returned as int", "package facts\nAnswer :: pure fn()->int { return true; }", NULL);
    single(n++, "int returned as Boolean", "package facts\nAnswer :: pure fn()->bool { return 42; }", NULL);
    cases[n++] = (request){ .name = "empty source arrays", .project = "/project" };
    single(n++, "source over cap", oversized, NULL);
    single(n, "mismatched source arrays", VALID, NULL);
    cases[n++].source_count = 0;
    cases[n++] = (request){
        .name = "four source files",
        .project = "/project",
        .paths = { "facts/A.chibi", "facts/B.chibi", "facts/C.chibi", "facts/D.chibi" },
        .sources = { VALID, "package facts", "package facts", "package facts" },
        .path_count = 4,
        .source_count = 4
    };
    single(n++, "function parameter", "package facts\nAnswer :: pure fn(value:int)->int { return value; }", NULL);
    single(n++, "non-pure function", "package facts\nAnswer :: fn()->int { return 42; }", NULL);
    single(
        n++,
        "two functions",
        "package facts\nAnswer :: pure fn()->int { return 42; }\n"
        "Other :: pure fn()->int { return 43; }",
        NULL
    );
    single(n++, "floating literal", "package facts\nAnswer :: pure fn()->int { return 1.5; }", NULL);
    single(n++, "unsupported scalar result", "package facts\nAnswer :: pure fn()->uint64 { return 42; }", NULL);
    single(n++, "untyped binary expression", "package facts\nAnswer :: pure fn()->int { return 19 + 23; }", NULL);
    single(n++, "function call", "package facts\nAnswer :: pure fn()->int { return length(\"hello\"); }", NULL);
    single(n++, "local statement", "package facts\nAnswer :: pure fn()->int { value:int=42; return value; }", NULL);
    single(n++, "library wrapper", "library Facts { Answer :: pure fn()->int { return 42; } }", NULL);
    single(n++, "unsupported conversion", "package facts\nAnswer :: pure fn()->bool { return bool(true); }", NULL);
    single(n++, "conversion type arguments", "package facts\nAnswer :: pure fn()->int { return int<bool>(5); }", NULL);
    single(n++, "function named int", "package facts\nint :: pure fn()->int { return int(42); }", NULL);
    single(n++, "no result", "package facts\nAnswer :: pure fn() { return 42; }", NULL);
    single(n++, "25 expression rows", expression25, NULL);
    single(n, "empty project root", VALID, NULL);
    cases[n++].project = "";
    memset(long_path, 'x', PATH_CAP + 1);
    long_path[PATH_CAP + 1] = 0;
    single(n, "path over cap", VALID, NULL);
    cases[n++].paths[0] = long_path;
    single(n, "unsupported prepared ABI", VALID, NULL);
    cases[n++].wrong_abi = true;
    REQUIRE(n == CASES);
    size_t offered = 0;
    for (int i = 0; i < CASES; i++) {
        REQUIRE(cases[i].project && cases[i].name);
        REQUIRE(cases[i].path_count >= 0 && cases[i].path_count <= 4);
        REQUIRE(cases[i].source_count >= 0 && cases[i].source_count <= 4);
        for (int j = 0; j < cases[i].source_count; j++) {
            REQUIRE(cases[i].sources[j] && strlen(cases[i].sources[j]) <= SOURCE_CAP + 1);
            offered += strlen(cases[i].sources[j]);
        }
    }
    REQUIRE(offered <= 16 * 1024);
    return true;
}
static bool reject_bridge(
    const pin_value *plan,
    uint64_t ready,
    const compiler_prepared_scalar_declaration *declaration,
    const char *message
)
{
    char error[192] = { 0 };
    bridge_length = 17;
    bridge_rejections++;
    bool valid = compiler_prepared_scalar_write(
        plan,
        ready,
        declaration,
        &bridge_bytes,
        &bridge_length,
        error,
        sizeof error
    );
    REQUIRE(!valid && !bridge_bytes && !bridge_length && !strcmp(error, message));
    return true;
}
static bool reject_malformed_bridge_inputs()
{
    REQUIRE(bridge_ready && bridge_plan.type == PIN_UNION && bridge_plan.ptr);
    REQUIRE(bridge_declaration.result == PIN_I32);
    REQUIRE(reject_bridge(&bridge_plan, 0, &bridge_declaration, "invalid scalar callable declaration"));
    compiler_prepared_scalar_declaration wrong = bridge_declaration;
    wrong.result = PIN_BOOL;
    REQUIRE(
        reject_bridge(&bridge_plan, bridge_ready, &wrong, "scalar callable requires no arguments/state and one declared result")
    );
    wrong = bridge_declaration;
    wrong.module = 0;
    REQUIRE(reject_bridge(&bridge_plan, bridge_ready, &wrong, "invalid scalar callable declaration"));
    wrong = bridge_declaration;
    wrong.function = 0;
    REQUIRE(reject_bridge(&bridge_plan, bridge_ready, &wrong, "invalid scalar callable declaration"));
    wrong = bridge_declaration;
    wrong.entry = "";
    REQUIRE(reject_bridge(&bridge_plan, bridge_ready, &wrong, "invalid scalar callable declaration"));
    pin_value invalid = integer(1);
    REQUIRE(reject_bridge(&invalid, bridge_ready, &bridge_declaration, "scalar plan is not a union"));
    pin_union *plan = bridge_plan.ptr;
    REQUIRE(plan->count == 9);
    pin_value old_ids = plan->values[1], old_defaults = plan->values[2];
    pin_array *ids = pin_array_snapshot_allocate(&bridge_input, PIN_U64, NULL, 1);
    ids->items[0] = (pin_value){ .type = PIN_U64, .u64 = 17 };
    plan->values[1] = (pin_value){ .type = PIN_U64,
                                   .container = PIN_CONTAINER_ARRAY,
                                   .ptr = ids };
    /* The existing zero I32 result header is also a valid variable default.
     * Only the bridge's zero-state contract changes; do not fabricate a plan. */
    plan->values[2] = plan->values[3];
    bool rejected = reject_bridge(
        &bridge_plan,
        bridge_ready,
        &bridge_declaration,
        "scalar callable requires no arguments/state and one declared result"
    );
    plan->values[1] = old_ids;
    plan->values[2] = old_defaults;
    REQUIRE(rejected);
    arena_free(&bridge_input);
    bridge_plan = (pin_value){ 0 };
    return true;
}
static bool prepare()
{
    /* This table contains compiler implementation files only. User inputs are
     * not passed to source_package_snapshot_new/authoring_packages_prepare. */
    const source_package_input sources[] = { BOUNDED_COMPILER_SOURCE_INPUTS };
    REQUIRE((int)(sizeof sources / sizeof sources[0]) == 18);
    size_t total = 0;
    for (int i = 0; i < 18; i++) {
        REQUIRE(sources[i].size < 32768);
        total += sources[i].size;
    }
    REQUIRE(total < 256 * 1024);
    REQUIRE(COMPILER_PREPARED_FIXTURE_DEFINITION_PACK.binding_count == 1);
    REQUIRE(COMPILER_PREPARED_FIXTURE_DEFINITION_PACK.definition_count == 1);
    REQUIRE(COMPILER_SYNTAX_DEFINITION_PACK.binding_count == 1);
    payload_binding = COMPILER_PREPARED_FIXTURE_DEFINITION_PACK.bindings[0];
    syntax_binding = COMPILER_SYNTAX_DEFINITION_PACK.bindings[0];
    REQUIRE(
        payload_binding.implementation.call &&
        payload_binding.implementation.call->context
    );
    REQUIRE(
        syntax_binding.implementation.call &&
        syntax_binding.implementation.call->context
    );
    payload_call = *payload_binding.implementation.call;
    syntax_call = *syntax_binding.implementation.call;
    payload_call.context = observe_payload;
    syntax_call.context = observe_syntax;
    payload_binding.implementation.call = &payload_call;
    syntax_binding.implementation.call = &syntax_call;
    native_definition_pack packs[] = { COMPILER_JSON_DEFINITION_PACK,
                                       COMPILER_SYNTAX_DEFINITION_PACK,
                                       COMPILER_PREPARED_FIXTURE_DEFINITION_PACK };
    packs[1].bindings = &syntax_binding;
    packs[2].bindings = &payload_binding;
    behavior_run_scope bootstrap = { 0 };
    behavior_run_scope_begin(&bootstrap);
    bool prepared = compiler_fixture_prepare_packs(&fixture, sources, 18, packs, 3);
    behavior_run_scope_end(&bootstrap);
    bootstrap_counts = bootstrap.counts;
    REQUIRE(prepared && !bootstrap_counts.overflow);
    REQUIRE(authoring_packages_count(fixture.owners[0]) == 16);
    REQUIRE(authoring_packages_count(fixture.owners[1]) == 16);
    REQUIRE(!payload_calls && !raw_parses);
    return true;
}
TEST_CASE(bounded_authored_compiler_builds_detached_runnable_closures)
{
    REQUIRE(TEST_COMPILED_BACKEND == BEHAVIOR_SLJIT);
    REQUIRE(behavior_backend_available(BEHAVIOR_SLJIT));
    REQUIRE(corpus() && prepare());
    for (int b = 0; b < 2; b++) {
        REQUIRE(tag(b, PACKAGE, "Result", "Compiled"));
        REQUIRE(tag(b, PACKAGE, "Result", "Rejected"));
        for (int i = 0; i < CASES; i++) {
            fprintf(
                stderr,
                "bounded compiler request: backend=%d case=%s\n",
                b,
                cases[i].name
            );
            pin_value args[4], result;
            REQUIRE(pack(b, &cases[i], args));
            int previous_calls = payload_calls, previous_parses = raw_parses;
            offered_files += cases[i].source_count;
            REQUIRE(compile(b, args, &result));
            REQUIRE(retain(b, i, &result));
            REQUIRE(payload_calls - previous_calls == (i < ACCEPTED ? 1 : 0));
            REQUIRE(
                raw_parses - previous_parses >= 0 &&
                raw_parses - previous_parses <= cases[i].source_count
            );
            arena_free(&detached);
        }
    }
    compiler_fixture_free(&fixture); /* No compiler/source/producer owner survives. */
    REQUIRE(reject_malformed_bridge_inputs());
    for (int i = 0; i < CASES; i++) {
        if (i >= ACCEPTED) {
            const rejection *expected = &rejections[i - ACCEPTED];
            REQUIRE(!strcmp(outputs[0][i].diagnostic, expected->message));
            REQUIRE(
                outputs[0][i].location[2] == expected->line &&
                outputs[0][i].location[3] == expected->column
            );
            REQUIRE(!strcmp(outputs[0][i].diagnostic, outputs[1][i].diagnostic));
            REQUIRE(!memcmp(
                outputs[0][i].location,
                outputs[1][i].location,
                sizeof outputs[0][i].location
            ));
            continue;
        }
        REQUIRE(same_artifact(&outputs[0][i], &outputs[1][i]));
        for (int b = 0; b < 2; b++) {
            REQUIRE(run_target(b, i, 0) && run_target(b, i, 1));
        }
    }
    REQUIRE(same_artifact(&outputs[0][0], &outputs[0][1]));
    REQUIRE(
        outputs[0][0].module == outputs[0][2].module &&
        outputs[0][0].entry == outputs[0][2].entry
    );
    REQUIRE(
        outputs[0][0].length != outputs[0][2].length ||
        memcmp(outputs[0][0].bytes, outputs[0][2].bytes, outputs[0][0].length)
    );
    REQUIRE(compile_calls == 2 * CASES && payload_calls == 2 * ACCEPTED);
    REQUIRE(offered_files == 92 && raw_parses == 76);
    REQUIRE(target_runs == 4 * ACCEPTED && bridge_rejections == 7);
    return true;
}
void test_language_bounded_compiler_chibi()
{
    compile_calls = payload_calls = raw_parses = target_runs = offered_files = 0;
    bridge_rejections = 0;
    bridge_ready = 0;
    bootstrap_counts = (behavior_run_counts){ 0 };
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(bounded_authored_compiler_builds_detached_runnable_closures);
    pu_clear_setup();
    fprintf(
        stderr,
        "bounded compiler attempted work: calls=%d payloads=%d raw_parses=%d "
        "target_runs=%d offered_files=%d bridge_rejections=%d\n",
        compile_calls,
        payload_calls,
        raw_parses,
        target_runs,
        offered_files,
        bridge_rejections
    );
    fprintf(
        stderr,
        "bounded compiler bootstrap entries: bytecode=%llu sljit=%llu "
        "wasm=%llu fallback=%llu\n",
        (unsigned long long)bootstrap_counts.bytecode_entries,
        (unsigned long long)bootstrap_counts.sljit_entries,
        (unsigned long long)bootstrap_counts.wasm_entries,
        (unsigned long long)bootstrap_counts.inspection_bytecode_fallbacks
    );
    cleanup();
}
