#include "types/text.h"
/* This consumer compiles only with exported runtime SDK headers. */
#include "execution/function_builder.h"
#include "execution/module.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *directory;
static execution_module *module;
static const behavior_function *function;
static behavior_function_context context;
static behavior_function_activation activation;
static behavior_run_scope runs;
static arena storage, escaped;
static pin_value result, saved[5];
static unsigned char *bytes;
static FILE *file;
static void finish_call()
{
    if (runs.active) {
        behavior_run_scope_end(&runs);
    }
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    arena_free(&storage);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
}
static void cleanup()
{
    finish_call();
    if (file) {
        fclose(file);
    }
    file = NULL;
    free(bytes);
    bytes = NULL;
    execution_module_free(module);
    module = NULL;
    function = NULL;
    arena_free(&escaped);
    memset(saved, 0, sizeof saved);
}
static bool read_module(int index, behavior_backend backend)
{
    char path[1024];
    int count = snprintf(path, sizeof path, "%s/record-%d.prepared", directory, index);
    REQUIRE(count > 0 && count < (int)sizeof path);
    file = fopen(path, "rb");
    REQUIRE(file && !fseek(file, 0, SEEK_END));
    long length = ftell(file);
    REQUIRE(length > 0 && length <= 1024 * 1024 && !fseek(file, 0, SEEK_SET));
    bytes = malloc((size_t)length);
    REQUIRE(bytes && fread(bytes, 1, (size_t)length, file) == (size_t)length);
    REQUIRE(fgetc(file) == EOF && !ferror(file));
    int closed = fclose(file);
    file = NULL;
    REQUIRE(!closed);
    char error[256];
    module = execution_prepared_read(bytes, (size_t)length, NULL, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "installed record consumer: %s\n", error);
    }
    REQUIRE(module && execution_module_backend(module) == backend);
    memset(bytes, 0xa5, (size_t)length);
    free(bytes);
    bytes = NULL;
    REQUIRE(execution_module_function_count(module) == 1);
    execution_module_function_info info = execution_module_function_at(module, 0);
    REQUIRE(info.id && info.body);
    function = info.body;
    return true;
}
static pin_value member(const pin_value *row, int index)
{
    pin_value value;
    {
        const char *error = pin_read_field(
            &storage,
            row->ptr,
            &row->record->type.fields[index],
            &value
        );
        if (error) {
            abort();
        }
    };
    return value;
}
static bool check_row(const pin_value *value, const char *key, const char *label)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_SINGLE);
    REQUIRE(pin_record_valid(value->record) && value->record->type.field_count == 12);
    REQUIRE(value->ptr && !strcmp(value->type_name, "Rows.Row"));
    REQUIRE(!strcmp(pin_text(member(value, 0)).bytes, key));
    REQUIRE(!strcmp(pin_text(member(value, 1)).bytes, label));
    REQUIRE(!strcmp(pin_text(member(value, 2)).bytes, "node"));
    REQUIRE(
        !strcmp(pin_text(member(value, 3)).bytes, "") &&
        !strcmp(pin_text(member(value, 4)).bytes, "")
    );
    REQUIRE(member(value, 5).i == 1 && !member(value, 6).i && member(value, 7).b);
    for (int i = 8; i < 12; i++) {
        REQUIRE(!member(value, i).b);
    }
    return true;
}
static bool owned_row(const pin_value *value)
{
    REQUIRE(arena_contains(&storage, value->ptr));
    for (int field = 0; field < 5; field++) {
        const char *text = pin_text(member(value, field)).bytes;
        REQUIRE(text && arena_contains(&storage, text));
    }
    return true;
}
static bool run(int index, behavior_backend backend)
{
    finish_call();
    int count = 0;
    const pin_value *results = behavior_function_results(function, &count);
    REQUIRE(count == 1 && results && results[0].record);
    char text[] = "installed-owner";
    const int counts[] = { -1, 0, 1, 3 };
    pin_value args[2] = {
        { .type = PIN_STR,
          .text = &(const pin_string){ .bytes = text, .length = (uint32_t)strlen(text) } },
        results[0]
    };
    if (index) {
        args[0] = (pin_value){ .type = PIN_I32, .i = counts[index - 1] };
        args[1].ptr = NULL;
    }
    result = results[0];
    behavior_function_prepare_results(function, &result, &storage);
    context = (behavior_function_context){ .activation = &activation,
                                           .arguments = args,
                                           .argument_count = index ? 2 : 1,
                                           .results = &result,
                                           .budget = 10000 };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(function, &context, NULL, 0));
    behavior_run_scope_reset(&runs);
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(function, &context, backend != BEHAVIOR_BYTECODE);
    behavior_run_scope_end(&runs);
    if (status != FLOW_OK) {
        fprintf(
            stderr,
            "installed record run: %d %s\n",
            status,
            context.error_message ? context.error_message : ""
        );
    }
    REQUIRE(status == FLOW_OK && context.returned);
    REQUIRE(
        !runs.counts.overflow && !runs.counts.wasm_entries && !runs.counts.inspection_bytecode_fallbacks
    );
    REQUIRE(
        backend == BEHAVIOR_BYTECODE
            ? runs.counts.bytecode_entries > 0 && !runs.counts.sljit_entries
            : runs.counts.sljit_entries > 0 && !runs.counts.bytecode_entries
    );
    REQUIRE(result.record && arena_contains(&storage, result.record));
    memset(text, 'x', sizeof text - 1);
    if (!index) {
        REQUIRE(check_row(&result, "installed-owner", "installed-owner"));
        REQUIRE(owned_row(&result));
    } else {
        REQUIRE(result.type == PIN_STRUCT && result.container == PIN_CONTAINER_ARRAY);
        const pin_array *array = result.ptr;
        if (index == 1) {
            REQUIRE(!array);
        } else {
            REQUIRE(array && array->count == counts[index - 1]);
            REQUIRE(array->record == result.record);
            for (int i = 0; i < array->count; i++) {
                pin_value item = pin_array_get(array, i);
                REQUIRE(pin_array_get(array, i).record == array->record);
                REQUIRE(check_row(&item, "a", "A"));
                REQUIRE(owned_row(&item));
            }
        }
    }
    REQUIRE(!pin_value_snapshot_to_arena(&escaped, &saved[index], &result));
    return true;
}
TEST_CASE(installed_records_outlive_producer_and_modules)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    REQUIRE(!type_catalog_find("Rows.Row"));
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(read_module(0, backends[b]));
        REQUIRE(run(0, backends[b]));
        finish_call();
        execution_module_free(module);
        module = NULL;
        function = NULL;
        REQUIRE(check_row(&saved[0], "installed-owner", "installed-owner"));
        REQUIRE(read_module(1, backends[b]));
        for (int i = 1; i < 5; i++) {
            REQUIRE(run(i, backends[b]));
        }
        finish_call();
        execution_module_free(module);
        module = NULL;
        function = NULL;
        for (int i = 0; i < 5; i++) {
            REQUIRE(pin_record_valid(saved[i].record));
            REQUIRE(arena_contains(&escaped, saved[i].record));
        }
        REQUIRE(check_row(&saved[0], "installed-owner", "installed-owner"));
        REQUIRE(!saved[1].ptr && saved[1].record);
        REQUIRE(((pin_array *)saved[2].ptr)->count == 0);
        REQUIRE(((pin_array *)saved[3].ptr)->count == 1);
        REQUIRE(((pin_array *)saved[4].ptr)->count == 3);
        for (int i = 3; i < 5; i++) {
            const pin_array *array = saved[i].ptr;
            for (int item = 0; item < array->count; item++) {
                pin_value row = pin_array_get(array, item);
                REQUIRE(check_row(&row, "a", "A"));
            }
        }
        REQUIRE(!type_catalog_find("Rows.Row"));
    }
    return true;
}
void test_tool_record_runtime(const char *path)
{
    directory = path;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(installed_records_outlive_producer_and_modules);
    pu_clear_setup();
}
