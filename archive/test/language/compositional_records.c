#include "compositional_records.h"

#include "backend.h"
#include "capabilities/catalog_snapshot.h"
#include "compiler_fixture.h"
#include "compositional_records_source.h"
#include "ecs/ecs.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/module.h"
#include "execution/prepared.h"
#include "execution/status.h"
#include "package_compiler_chibi.h"
#include "runtime/core_definition.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/record.h"
#include "types/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static compiler_fixture fixture;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module;
static execution_module *next;
static execution_program *program;
static ecs_world *world;
static behavior_function_activation activation;
static behavior_function_context context;
static behavior_managed_heap managed;
static arena storage, retained, catalog_storage;
static unsigned char *image;
const char COMPOSITIONAL_RECORD_ALIAS_REJECTION
    [] = "package facts\n"
         "Node :: struct { value: int; }\n"
         "Clear :: (slot: ^(^Node?)) { slot^ = nil; }\n"
         "pub Main :: () -> int { value: Node = {value: 7}; "
         "p: ^Node? = &value; if p != nil { Clear(&p); return p.value; } "
         "return 0; }\n";

bool compositional_records_depth_source(char *source, size_t size, int count, bool arrays)
{
    int used = snprintf(source, size, "package facts\n");
    if (used < 0 || (size_t)used >= size) {
        return false;
    }
    for (int i = count - 1; i >= 0; i--) {
        int wrote = i ? snprintf(
                            source + used,
                            size - (size_t)used,
                            "Depth%d :: struct { child: Depth%d%s; }\n",
                            i,
                            i - 1,
                            arrays || i % 2 ? "[]" : ""
                        )
                      : snprintf(source + used, size - (size_t)used, "Depth0 :: struct { value: int; }\n");
        if (wrote < 0 || (size_t)wrote >= size - (size_t)used) {
            return false;
        }
        used += wrote;
    }
    int wrote = snprintf(source + used, size - (size_t)used, "pub Main :: () -> int { return 0; }\n");
    return wrote > 0 && (size_t)wrote < size - (size_t)used;
}

bool compositional_records_fanout_source(char *source, size_t size)
{
    int used = snprintf(source, size, "package facts\nDepth0 :: struct { value: int; }\n");
    if (used < 0 || (size_t)used >= size) {
        return false;
    }
    for (int i = 1; i < 9; i++) {
        int wrote = snprintf(source + used, size - (size_t)used, "Depth%d :: struct {", i);
        if (wrote < 0 || (size_t)wrote >= size - (size_t)used) {
            return false;
        }
        used += wrote;
        for (int field = 0; field < 10; field++) {
            wrote = snprintf(source + used, size - (size_t)used, " child%d: Depth%d[];", field, i - 1);
            if (wrote < 0 || (size_t)wrote >= size - (size_t)used) {
                return false;
            }
            used += wrote;
        }
        wrote = snprintf(source + used, size - (size_t)used, " }\n");
        if (wrote < 0 || (size_t)wrote >= size - (size_t)used) {
            return false;
        }
        used += wrote;
    }
    int wrote = snprintf(source + used, size - (size_t)used, "pub Main :: () -> int { return 0; }\n");
    return wrote > 0 && (size_t)wrote < size - (size_t)used;
}

static void finish_call()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    behavior_managed_heap_free(&managed);
    arena_free(&storage);
}

static void clear_analysis()
{
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
}

static void cleanup()
{
    finish_call();
    compiler_fixture_free(&fixture);
    execution_program_free(program);
    program = NULL;
    execution_module_free(module);
    execution_module_free(next);
    next = NULL;
    if (world) {
        ecs_world_free(world);
        world = NULL;
    }
    execution_module_plan_free(plan);
    module = NULL;
    plan = NULL;
    clear_analysis();
    arena_free(&retained);
    arena_free(&catalog_storage);
    free(image);
    image = NULL;
}

TEST_CASE(compositional_records_authored_values)
{
    const source_package_input source = { "compositional_records/Values.chibi",
                                          COMPOSITIONAL_RECORDS_SOURCE,
                                          sizeof COMPOSITIONAL_RECORDS_SOURCE - 1 };
    const native_definition_pack core[] = { CORE_DEFINITION_PACK };
    REQUIRE(compiler_fixture_prepare_packs(&fixture, &source, 1, core, 1));
    REQUIRE(compiler_fixture_tests(&fixture));
    return true;
}
void test_language_compositional_records()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(compositional_records_authored_values);
    pu_clear_setup();
    test_language_package_compiler_chibi_records();
}
