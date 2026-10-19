#include "authoring/native_bindings.h"
#include "authoring/source_package_prepare.h"
#include "backend.h"
#include "execution/function.h"
#include "execution/module.h"
#include "execution/status.h"
#include "package_core_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/text_definition.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Four cases, two backends. Each preparation has two files, one import and
 * three-element arrays at most. Each call has a fixed 10000-instruction budget;
 * no filesystem, services, workers, frames or timing loops. The generated header
 * embeds the actual installed-source candidate, not a second wrapper copy. */
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static authoring_packages *owners[3];
static native_capabilities *tables[2];
static execution_module *retained;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage;
static pin_value output;
static const char APP
    [] = "package app\nimport strings \"core:strings\"\n"
         "Row :: struct { value: int; tag: string; }\n"
         "Check :: fn() -> int {\n"
         " numbers: int[] = [1, 2]; third: int = 3; numbers = "
         "array_add(numbers, third);\n"
         " words: string[] = [\"alpha\"]; words = array_add(words, \"beta\");\n"
         " rows: Row[] = []; row: Row = {value: 7, tag: \"seven\"};\n"
         " rows = array_add(rows, row); picked: Row = rows[0];\n"
         " if length(numbers) != 3 || numbers[2] != 3 { return -1; }\n"
         " if length(words) != 2 || words[1] != \"beta\" { return -2; }\n"
         " if length(rows) != 1 || picked.value != 7 || picked.tag != "
         "\"seven\" { return -3; }\n"
         " if !strings.IsIdentifier(strings.BaseName(\"root/valid_name\")) { "
         "return -4; }\n"
         " return strings.ByteLength(strings.Slice(words[1], 1, 3));\n"
         "}\n";

static void finish_call()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    output = (pin_value){ 0 };
}
static void cleanup()
{
    finish_call();
    execution_module_free(retained);
    retained = NULL;
    for (int i = 0; i < 3; i++) {
        authoring_packages_free(owners[i]);
        owners[i] = NULL;
    }
    for (int i = 0; i < 2; i++) {
        native_capabilities_free(tables[i]);
        tables[i] = NULL;
    }
}
static bool table(int index)
{
    native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                       NATIVE_ARRAY_DEFINITION_PACK };
    char error[192];
    tables[index] = native_capabilities_from_definition_packs(NULL, 0, packs, 2, error, sizeof error);
    if (!tables[index]) {
        fprintf(stderr, "core native: %s\n", error);
    }
    REQUIRE(tables[index]);
    return true;
}
static bool prepare(int index, int previous, int native, behavior_backend backend)
{
    char *source = strdup(CORE_STRINGS_SOURCE), *app = strdup(APP),
         *root = strdup("/explicit/project"), *core = strdup("/explicit/sdk/core");
    REQUIRE(source && app && root && core);
    source_package_input files[] = {
        { "/explicit/sdk/core/strings/Strings.chibi", source, strlen(source) },
        { "app/Main.chibi", app, strlen(app) },
    };
    REQUIRE(files[0].size < 16384 && files[1].size < 2048);
    source_package_collection collections[] = { { "core", core } };
    source_package_snapshot_options options = {
        .project_root = root,
        .files = files,
        .file_count = 2,
        .collections = collections,
        .collection_count = 1,
    };
    source_package_snapshot *snapshot = source_package_snapshot_new(&options);
    memset(source, '?', files[0].size);
    memset(app, '?', files[1].size);
    free(source);
    free(app);
    free(root);
    free(core);
    REQUIRE(!source_package_snapshot_error_at(snapshot));
    script_lower_bindings bindings = authoring_native_bindings(tables[native]);
    owners[index] = authoring_packages_prepare(
        snapshot,
        native_capabilities_catalog(tables[native]),
        &bindings,
        backend,
        previous >= 0 ? owners[previous] : NULL
    );
    REQUIRE(owners[index]);
    if (!authoring_packages_valid(owners[index])) {
        const char *error = authoring_packages_error(owners[index]);
        if (error) {
            fprintf(stderr, "core owner: %s\n", error);
        }
        for (int i = 0; i < authoring_packages_count(owners[index]); i++) {
            const authoring_package *item = authoring_packages_at(owners[index], i);
            for (int j = 0; j < item->diagnostic_count; j++) {
                const script_diagnostic *diagnostic = &item->diagnostics[j];
                const char *path = "unknown";
                if (diagnostic->range.document >= 0 &&
                    diagnostic->range.document < item->analysis->document_count) {
                    path = item->analysis->documents[diagnostic->range.document].key;
                }
                fprintf(
                    stderr,
                    "%s %s:%d:%d [%d,%d): %s\n",
                    item->identity,
                    path,
                    diagnostic->range.line,
                    diagnostic->range.column,
                    diagnostic->range.start,
                    diagnostic->range.end,
                    diagnostic->message
                );
            }
        }
    }
    REQUIRE(authoring_packages_valid(owners[index]));
    REQUIRE(authoring_packages_count(owners[index]) == 2);
    return true;
}
static const authoring_package *package(int index, const char *identity)
{
    for (int i = 0; i < authoring_packages_count(owners[index]); i++) {
        const authoring_package *item = authoring_packages_at(owners[index], i);
        if (!strcmp(item->identity, identity)) {
            return item;
        }
    }
    return NULL;
}
static uint64_t exported(const authoring_package *item, const char *name)
{
    for (int i = 0; item && i < item->exports->member_count; i++) {
        const script_package_export *member = &item->exports->members[i];
        if (!strcmp(member->name, name)) {
            return member->id;
        }
    }
    return 0;
}
static bool invoke(
    execution_module *module,
    uint64_t id,
    const pin_value *arguments,
    int count,
    pin_type type,
    behavior_backend backend
)
{
    finish_call();
    REQUIRE(module && id && count <= 3);
    const behavior_function *function = NULL;
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info info = execution_module_function_at(module, i);
        if (info.id == id) {
            function = info.body;
        }
    }
    REQUIRE(function);
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &activation);
    }
    output = (pin_value){ .type = type };
    context = (behavior_function_context){
        .activation = behavior_function_resumable(function) ? &activation : NULL,
        .arguments = arguments,
        .argument_count = count,
        .results = &output,
        .budget = 10000,
    };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(function, &context, backend == TEST_COMPILED_BACKEND);
    behavior_run_scope_end(&runs);
    REQUIRE(status == FLOW_OK && context.returned && output.type == type);
    REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
    REQUIRE(
        backend == BEHAVIOR_BYTECODE ? runs.counts.bytecode_entries > 0
                                     : !runs.counts.bytecode_entries
    );
    REQUIRE(backend != BEHAVIOR_SLJIT || runs.counts.sljit_entries > 0);
    REQUIRE(backend != BEHAVIOR_WASM || runs.counts.wasm_entries > 0);
    return true;
}
static bool core_call(const char *name, const pin_value *arguments, int count, pin_type type, behavior_backend backend)
{
    const authoring_package *core = package(0, "core:strings");
    REQUIRE(core && core->valid && core->module);
    return invoke(core->module, exported(core, name), arguments, count, type, backend);
}
static pin_value text(const char *value)
{
    return (pin_value){ .type = PIN_STR, .str = value };
}
static pin_value integer(int value)
{
    return (pin_value){ .type = PIN_I32, .i = value };
}
TEST_CASE(package_core_strings_bytes_bounds_and_owned_slices)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(table(0) && prepare(0, -1, 0, backends[b]));
        char bytes[] = { 'A', (char)0x80, (char)0xff, 0 };
        pin_value args[] = { text(bytes), integer(0), integer(0) };
        REQUIRE(core_call("ByteLength", args, 1, PIN_I32, backends[b]) && output.i == 3);
        const int indices[] = { -1, 0, 1, 2, 3 };
        const int expected[] = { -1, 65, 128, 255, -1 };
        for (int i = 0; i < 5; i++) {
            args[1] = integer(indices[i]);
            REQUIRE(
                core_call("ByteAt", args, 2, PIN_I32, backends[b]) &&
                output.i == expected[i]
            );
        }
        REQUIRE(core_call("Bytes", args, 1, PIN_I32, backends[b]));
        const pin_array *read = output.ptr;
        REQUIRE(output.container == PIN_CONTAINER_ARRAY && read && read->count == 3);
        for (int i = 0; i < 3; i++) {
            REQUIRE(read->items[i].type == PIN_I32 && read->items[i].i == expected[i + 1]);
        }
        args[0] = text("");
        REQUIRE(core_call("ByteLength", args, 1, PIN_I32, backends[b]) && output.i == 0);
        REQUIRE(core_call("Bytes", args, 1, PIN_I32, backends[b]));
        read = output.ptr;
        REQUIRE(output.container == PIN_CONTAINER_ARRAY && read && read->count == 0);
        args[1] = integer(0);
        REQUIRE(core_call("ByteAt", args, 2, PIN_I32, backends[b]) && output.i == -1);
        char source[] = "abcdef";
        args[0] = text(source);
        args[1] = integer(1);
        args[2] = integer(3);
        REQUIRE(core_call("Slice", args, 3, PIN_STR, backends[b]));
        REQUIRE(output.str && output.str != source + 1 && !strcmp(output.str, "bcd"));
        memset(source, 'x', 6);
        REQUIRE(!strcmp(output.str, "bcd"));
        args[0] = text("abcdef");
        const int starts[] = { -2, 99, 2, 4 };
        const int counts[] = { 2, 2, -1, 99 };
        const char *slices[] = { "ab", "", "", "ef" };
        for (int i = 0; i < 4; i++) {
            args[1] = integer(starts[i]);
            args[2] = integer(counts[i]);
            REQUIRE(core_call("Slice", args, 3, PIN_STR, backends[b]));
            REQUIRE(!strcmp(output.str, slices[i]));
        }
        cleanup();
    }
    return true;
}
TEST_CASE(package_core_strings_comparison_identifiers_and_basename)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(table(0) && prepare(0, -1, 0, backends[b]));
        const char high[] = { (char)0xff, 0 }, low[] = { (char)0x80, 0 };
        const char *left[] = { "", "abc", "ab", "b", high, low };
        const char *right[] = { "", "abc", "abc", "a", low, high };
        const int expected[] = { 0, 0, -1, 1, 1, -1 };
        for (int i = 0; i < 6; i++) {
            pin_value args[] = { text(left[i]), text(right[i]) };
            REQUIRE(
                core_call("Compare", args, 2, PIN_I32, backends[b]) &&
                output.i == expected[i]
            );
        }
        const char *names[] = { "", "a", "_A9", "9a", "a-b", high };
        const bool identifiers[] = { false, true, true, false, false, false };
        for (int i = 0; i < 6; i++) {
            pin_value arg = text(names[i]);
            REQUIRE(
                core_call("IsIdentifier", &arg, 1, PIN_BOOL, backends[b]) &&
                output.b == identifiers[i]
            );
        }
        const char *paths[] = { "", "file", "/", "a/b/", "a/b", "a\\b" };
        const char *base[] = { "", "file", "", "", "b", "a\\b" };
        for (int i = 0; i < 6; i++) {
            pin_value arg = text(paths[i]);
            REQUIRE(core_call("BaseName", &arg, 1, PIN_STR, backends[b]));
            REQUIRE(!strcmp(output.str, base[i]));
        }
        cleanup();
    }
    return true;
}
TEST_CASE(package_core_pure_exports_and_generic_native_arrays)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(table(0) && prepare(0, -1, 0, backends[b]));
        const authoring_package *core = package(0, "core:strings"),
                                *app = package(0, "project:app");
        REQUIRE(core && app && core->exports->member_count == 7);
        for (int i = 0; i < core->exports->member_count; i++) {
            const script_catalog_capability *signature = script_catalog_capability_by_id(
                core->exports->metadata,
                core->exports->members[i].id
            );
            REQUIRE(signature && signature->pure && !signature->effects);
        }
        REQUIRE(invoke(app->module, exported(app, "Check"), NULL, 0, PIN_I32, backends[b]));
        REQUIRE(output.i == 3);
        cleanup();
    }
    return true;
}
TEST_CASE(package_core_inputs_modules_and_binding_generations)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(table(0) && prepare(0, -1, 0, backends[b]));
        REQUIRE(prepare(1, 0, 0, backends[b]));
        REQUIRE(authoring_packages_reused(owners[1]) == 2);
        REQUIRE(table(1) && prepare(2, 1, 1, backends[b]));
        REQUIRE(
            native_capabilities_generation(tables[0]) !=
            native_capabilities_generation(tables[1])
        );
        REQUIRE(authoring_packages_reused(owners[2]) == 0);
        REQUIRE(
            package(1, "core:strings")->generation != package(2, "core:strings")->generation
        );
        const authoring_package *app = package(0, "project:app");
        REQUIRE(app);
        uint64_t id = exported(app, "Check");
        retained = app->module;
        execution_module_retain(retained);
        for (int i = 0; i < 3; i++) {
            authoring_packages_free(owners[i]);
            owners[i] = NULL;
        }
        REQUIRE(invoke(retained, id, NULL, 0, PIN_I32, backends[b]));
        REQUIRE(output.i == 3);
        cleanup();
    }
    return true;
}
void test_language_package_core()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_core_strings_bytes_bounds_and_owned_slices);
    RUN_TEST_CASE(package_core_strings_comparison_identifiers_and_basename);
    RUN_TEST_CASE(package_core_pure_exports_and_generic_native_arrays);
    RUN_TEST_CASE(package_core_inputs_modules_and_binding_generations);
    pu_clear_setup();
}
