#include "authoring/native_bindings.h"
#include "authoring/source_package_prepare.h"
#include "backend.h"
#include "compiler_fixture.h"
#include "core_strings_tests_source.h"
#include "execution/function.h"
#include "execution/module.h"
#include "execution/status.h"
#include "package_compiler_chibi.h"
#include "package_core_source.h"
#include "runtime/arrays_definition.h"
#include "runtime/core_definition.h"
#include "runtime/text_definition.h"
#include "runtime/text_format.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Native boundaries on two backends. Each preparation has two files and one import.
 * Dense fixtures return at most 33 fields from 96 bytes. Each call has a fixed
 * 10000-instruction budget;
 * no filesystem, services, workers, frames or timing loops. The
 * generated header embeds the actual installed-source candidate, not a second
 * wrapper copy. Expected values follow Odin's core:strings over bytes. */
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static authoring_packages *owners[3];
static compiler_fixture authored;
static native_capabilities *tables[2];
static execution_module *retained;
static behavior_function_context context;
static behavior_function_activation activation;
static arena storage, inputs_storage;
static pin_value output;
static size_t slice_calls, slice_bytes, byte_reads;
/* Count actual owned slice payloads through the existing native binding seam.
 * Delegate unchanged to the runtime; no allocator or runtime instrumentation. */
static const char *measured_slice(arena *memory, const pin_value *args, int count, pin_value *result)
{
    const char *error = NATIVE_TEXT_SLICE_CALL.value(memory, args, count, result);
    if (!error) {
        slice_calls++;
        slice_bytes += pin_text(*result).length;
    }
    return error;
}
static const char *measured_byte_at(arena *memory, const pin_value *args, int count, pin_value *result)
{
    byte_reads++;
    return NATIVE_TEXT_BYTE_AT_CALL.value(memory, args, count, result);
}
static const node_host_call MEASURED_SLICE = { .value = measured_slice };
static const node_host_call MEASURED_BYTE_AT = { .value = measured_byte_at };
static const char APP
    [] = "package app\nimport strings \"core:strings\"\n"
         "Row :: struct { value: int; tag: string; }\n"
         "pub Check :: () -> int {\n"
         " numbers: int[] = [1, 2]; third: int = 3; numbers = "
         "append(numbers, third);\n"
         " words: string[] = [\"alpha\"]; words = append(words, \"beta\");\n"
         " rows: Row[] = []; row: Row = {value: 7, tag: \"seven\"};\n"
         " rows = append(rows, row); picked: Row = rows[0];\n"
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
    compiler_fixture_free(&authored);
    finish_call();
    arena_free(&inputs_storage);
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
    native_definition_binding bindings[16];
    REQUIRE(packs[0].binding_count <= 16);
    for (int i = 0; i < packs[0].binding_count; i++) {
        bindings[i] = packs[0].bindings[i];
        if (!strcmp(bindings[i].canonical_name, NATIVE_TEXT_SLICE_NAME)) {
            bindings[i].implementation.call = &MEASURED_SLICE;
        }
        if (!strcmp(bindings[i].canonical_name, NATIVE_TEXT_BYTE_AT_NAME)) {
            bindings[i].implementation.call = &MEASURED_BYTE_AT;
        }
    }
    packs[0].bindings = bindings;
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
    REQUIRE(files[1].size < 4096);
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
    int document_count = 0;
    const script_package_document *documents = source_package_snapshot_documents(snapshot, &document_count);
    REQUIRE(document_count == 2);
    REQUIRE(strlen(documents[0].syntax->text) == sizeof CORE_STRINGS_SOURCE - 1);
    REQUIRE(!memcmp(documents[0].syntax->text, CORE_STRINGS_SOURCE, sizeof CORE_STRINGS_SOURCE));
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
    REQUIRE(module && id && count <= 4);
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
    slice_calls = slice_bytes = byte_reads = 0;
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
    pin_value result;
    const char *error = pin_value_text(&inputs_storage, &result, value, strlen(value));
    assert(!error);
    (void)error;
    return result;
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
            pin_value item = pin_array_get(read, i);
            REQUIRE(item.type == PIN_I32 && item.i == expected[i + 1]);
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
        REQUIRE(
            pin_text(output).bytes && pin_text(output).bytes != source + 1 &&
            !strcmp(pin_text(output).bytes, "bcd")
        );
        memset(source, 'x', 6);
        REQUIRE(!strcmp(pin_text(output).bytes, "bcd"));
        args[0] = text("abcdef");
        const int starts[] = { -2, 99, 2, 4 };
        const int counts[] = { 2, 2, -1, 99 };
        const char *slices[] = { "ab", "", "", "ef" };
        for (int i = 0; i < 4; i++) {
            args[1] = integer(starts[i]);
            args[2] = integer(counts[i]);
            REQUIRE(core_call("Slice", args, 3, PIN_STR, backends[b]));
            REQUIRE(!strcmp(pin_text(output).bytes, slices[i]));
        }
        const char binary[] = { 'A', 0, 'B', (char)0xff };
        REQUIRE(!pin_value_text(&inputs_storage, &args[0], binary, sizeof binary));
        REQUIRE(core_call("ByteLength", args, 1, PIN_I32, backends[b]) && output.i == 4);
        args[1] = integer(1);
        REQUIRE(core_call("ByteAt", args, 2, PIN_I32, backends[b]) && output.i == 0);
        args[1] = integer(3);
        REQUIRE(core_call("ByteAt", args, 2, PIN_I32, backends[b]) && output.i == 255);
        args[1] = integer(1);
        args[2] = integer(3);
        REQUIRE(core_call("Slice", args, 3, PIN_STR, backends[b]));
        REQUIRE(
            pin_text(output).length == 3 &&
            !memcmp(pin_text(output).bytes, binary + 1, 3)
        );
        pin_value format_args[] = { text("{:>4.2s}"), args[0] }, formatted;
        REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, format_args, 2, &formatted));
        const char padded[] = { ' ', ' ', 'A', 0 };
        REQUIRE(
            pin_text(formatted).length == sizeof padded &&
            !memcmp(pin_text(formatted).bytes, padded, sizeof padded)
        );
        pin_value character_args[] = { text("{:3c}"), integer(0) };
        REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, character_args, 2, &formatted));
        const char character[] = { ' ', ' ', 0 };
        REQUIRE(
            pin_text(formatted).length == sizeof character &&
            !memcmp(pin_text(formatted).bytes, character, sizeof character)
        );
        const char invalid_mods[] = { '{', ':', 0, 'g', '}' };
        REQUIRE(
            !pin_value_text(&inputs_storage, &character_args[0], invalid_mods, sizeof invalid_mods)
        );
        character_args[1] = integer(42);
        REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, character_args, 2, &formatted));
        REQUIRE(pin_text(formatted).length == 2 && !memcmp(pin_text(formatted).bytes, "42", 2));
        const char format[] = { 'X', 0, '{', '}' };
        REQUIRE(!pin_value_text(&inputs_storage, &format_args[0], format, sizeof format));
        REQUIRE(!NATIVE_FORMAT_TEXT_CALL.value(&storage, format_args, 2, &formatted));
        const char joined[] = { 'X', 0, 'A', 0, 'B', (char)0xff };
        REQUIRE(
            pin_text(formatted).length == sizeof joined &&
            !memcmp(pin_text(formatted).bytes, joined, sizeof joined)
        );
        arena_free(&inputs_storage);
        REQUIRE(
            pin_text(output).length == 3 &&
            !memcmp(pin_text(output).bytes, binary + 1, 3)
        );
        REQUIRE(
            pin_text(formatted).length == sizeof joined &&
            !memcmp(pin_text(formatted).bytes, joined, sizeof joined)
        );
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
        REQUIRE(core && app);
        int functions = 0, types = 0;
        for (int i = 0; i < core->exports->member_count; i++) {
            const script_package_export *member = &core->exports->members[i];
            if (member->kind == SCRIPT_PACKAGE_TYPE) {
                types++;
                continue;
            }
            REQUIRE(member->kind == SCRIPT_PACKAGE_FUNCTION);
            const script_catalog_capability *signature = script_catalog_capability_by_id(
                core->exports->metadata,
                member->id
            );
            REQUIRE(signature && signature->pure && !signature->effects);
            functions++;
        }
        /* The splitters and cursor search helpers stay private. */
        REQUIRE(functions == 49 && types == 1 && core->exports->private_count == 4);
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
TEST_CASE(package_core_strings_dense_copy_budget)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(table(0) && prepare(0, -1, 0, backends[b]));
        for (int variant = 0; variant < 4; variant++) {
            int count = variant % 2 ? 32 : 16;
            const char *sep = variant < 2 ? "::" : "\xc3\xa9";
            char dense[97];
            for (int i = 0; i < count; i++) {
                dense[3 * i] = 'x';
                memcpy(dense + 3 * i + 1, sep, 2);
            }
            dense[3 * count] = 0;
            pin_value args[] = { text(dense), text(sep), integer(-1) };
            REQUIRE(core_call("Count", args, 2, PIN_I32, backends[b]) && output.i == count);
            REQUIRE(slice_calls == 0 && slice_bytes == 0);
            REQUIRE(byte_reads <= (size_t)(12 * count));
            fprintf(
                stderr,
                "dense backend=%d variant=%d matches=%d Count slices=%zu "
                "bytes=%zu reads=%zu\n",
                b,
                variant,
                count,
                slice_calls,
                slice_bytes,
                byte_reads
            );
            const char *names[] = { "Split", "SplitAfter", "SplitN", "SplitAfterN" };
            for (int k = 0; k < 4; k++) {
                REQUIRE(core_call(names[k], args, k < 2 ? 2 : 3, PIN_STR, backends[b]));
                const pin_array *parts = output.ptr;
                REQUIRE(
                    output.container == PIN_CONTAINER_ARRAY && parts &&
                    parts->count == count + 1
                );
                for (int i = 0; i < count; i++) {
                    pin_string part = pin_text(pin_array_get(parts, i));
                    size_t size = k % 2 ? 3 : 1;
                    REQUIRE(part.length == size && !memcmp(part.bytes, dense, size));
                }
                REQUIRE(pin_text(pin_array_get(parts, count)).length == 0);
                REQUIRE(slice_calls == (size_t)count + 1);
                REQUIRE(slice_bytes == (size_t)count * (k % 2 ? 3 : 1));
                REQUIRE(byte_reads <= (size_t)(12 * count));
                fprintf(
                    stderr,
                    "dense backend=%d variant=%d matches=%d %s slices=%zu "
                    "bytes=%zu "
                    "reads=%zu\n",
                    b,
                    variant,
                    count,
                    names[k],
                    slice_calls,
                    slice_bytes,
                    byte_reads
                );
            }
        }
        cleanup();
    }
    return true;
}
/* A bounded, direct byte oracle independent of the rolling hash. Empty
 * separators split into bytes, with no trailing empty field. */
static bool split_bytes(const char *source, int size, const char *sep, int width, behavior_backend backend)
{
    pin_value args[3];
    REQUIRE(!pin_value_text(&inputs_storage, &args[0], source, (size_t)size));
    REQUIRE(!pin_value_text(&inputs_storage, &args[1], sep, (size_t)width));
    int matches = 0, first = width ? -1 : 0;
    for (int i = 0; width && i <= size - width; i++) {
        if (!memcmp(source + i, sep, (size_t)width)) {
            if (first < 0) {
                first = i;
            }
            matches++;
            i += width - 1;
        }
    }
    REQUIRE(core_call("Index", args, 2, PIN_I32, backend) && output.i == first);
    REQUIRE(core_call("Count", args, 2, PIN_I32, backend));
    REQUIRE(output.i == (width ? matches : size + 1));
    REQUIRE(slice_calls == 0 && slice_bytes == 0);
    /* Repeated-prefix absent fixtures distinguish rolling search from a naive
     * comparison at every position, without a wall-clock threshold. */
    REQUIRE(byte_reads <= (size_t)(4 * size + 2 * width));
    const int limits[] = { -7, -1, 0, 1, 2, 3, 4, 100 };
    for (int k = 0; k < 4; k++) {
        for (int n = 0; n < (k < 2 ? 1 : 8); n++) {
            int limit = k < 2 ? -1 : limits[n];
            int starts[33], sizes[33], count = 0, start = 0;
            if (!width) {
                count = limit < 0 || limit > size ? size : limit;
                for (int i = 0; i < count; i++) {
                    starts[i] = i;
                    sizes[i] = i == count - 1 ? size - i : 1;
                }
            } else if (limit) {
                for (int i = 0; i <= size - width && (limit < 0 || count < limit - 1);
                     i++) {
                    if (!memcmp(source + i, sep, (size_t)width)) {
                        starts[count] = start;
                        sizes[count++] = i - start + (k % 2 ? width : 0);
                        start = i + width;
                        i += width - 1;
                    }
                }
                starts[count] = start;
                sizes[count++] = size - start;
            }
            const char *names[] = { "Split", "SplitAfter", "SplitN", "SplitAfterN" };
            args[2] = integer(limit);
            REQUIRE(core_call(names[k], args, k < 2 ? 2 : 3, PIN_STR, backend));
            const pin_array *parts = output.ptr;
            REQUIRE(output.container == PIN_CONTAINER_ARRAY && parts && parts->count == count);
            size_t payload = 0;
            for (int i = 0; i < count; i++) {
                pin_string part = pin_text(pin_array_get(parts, i));
                REQUIRE(part.length == (size_t)sizes[i]);
                REQUIRE(!memcmp(part.bytes, source + starts[i], part.length));
                payload += part.length;
            }
            REQUIRE(slice_calls <= (size_t)count && slice_bytes <= payload);
            REQUIRE(byte_reads <= (size_t)(4 * size + 2 * width));
        }
    }
    return true;
}
TEST_CASE(package_core_strings_binary_overlap_collision_and_limits)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(table(0) && prepare(0, -1, 0, backends[b]));
        const char binary[] = { 'A', 0, (char)0xff, (char)0x80,
                                'A', 0, (char)0xff, (char)0x80 };
        REQUIRE(split_bytes(binary, sizeof binary, binary + 1, 3, backends[b]));
        REQUIRE(split_bytes(binary, sizeof binary, binary + 2, 1, backends[b]));
        REQUIRE(split_bytes(binary, sizeof binary, "", 0, backends[b]));
        REQUIRE(split_bytes("\xc3\xa9\xc3\xa9\xc3\xa9", 6, "\xc3\xa9", 2, backends[b]));
        REQUIRE(split_bytes("\xc3\xa9\xc3\xa9", 4, "\xa9", 1, backends[b]));
        REQUIRE(split_bytes("aaaaa", 5, "aa", 2, backends[b]));
        REQUIRE(split_bytes("abababa", 7, "aba", 3, backends[b]));
        REQUIRE(split_bytes("ababab", 6, "ac", 2, backends[b]));
        REQUIRE(split_bytes("", 0, "", 0, backends[b]));
        REQUIRE(split_bytes("", 0, "ab", 2, backends[b]));
        REQUIRE(split_bytes("ab", 2, "abcd", 4, backends[b]));
        /* Both six-byte strings hash to 0x1d967b07. Reject the first hit. */
        const char
            collision[] = "\x43\x3c\xce\xb9\xc0\x87\x78\x92\xfb\x4d\x2b\x1e";
        uint32_t left = 0, right = 0;
        for (int i = 0; i < 6; i++) {
            left = left * 16777619u + (unsigned char)collision[i];
            right = right * 16777619u + (unsigned char)collision[6 + i];
        }
        REQUIRE(left == right && left == 0x1d967b07u);
        REQUIRE(split_bytes(collision, 12, collision + 6, 6, backends[b]));
        char prefix[96], absent[16];
        memset(prefix, 'a', sizeof prefix);
        memset(absent, 'a', sizeof absent);
        absent[15] = 'b';
        REQUIRE(split_bytes(prefix, sizeof prefix, absent, sizeof absent, backends[b]));
        cleanup();
    }
    return true;
}
TEST_CASE(package_core_authored_strings)
{
    source_package_input sources[] = { CORE_STRINGS_TEST_SOURCE_INPUTS };
    const native_definition_pack core[] = { CORE_DEFINITION_PACK };
    REQUIRE(
        compiler_fixture_prepare_packs(&authored, sources, (int)(sizeof sources / sizeof *sources), core, 1)
    );
    bool native_passed = compiler_fixture_tests_budget(&authored, 10000);
    /* Stage one chooses its root package from the first source path. */
    source_package_input core_source = sources[0];
    sources[0] = sources[1];
    sources[1] = core_source;
    bool stage_one_passed = package_compiler_authored_tests(
        &authored,
        sources,
        (int)(sizeof sources / sizeof *sources),
        10000
    );
    REQUIRE(native_passed && stage_one_passed);
    return true;
}
void test_language_package_core()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(package_core_strings_bytes_bounds_and_owned_slices);
    RUN_TEST_CASE(package_core_pure_exports_and_generic_native_arrays);
    RUN_TEST_CASE(package_core_inputs_modules_and_binding_generations);
    RUN_TEST_CASE(package_core_strings_dense_copy_budget);
    RUN_TEST_CASE(package_core_strings_binary_overlap_collision_and_limits);
    RUN_TEST_CASE(package_core_authored_strings);
    pu_clear_setup();
}
