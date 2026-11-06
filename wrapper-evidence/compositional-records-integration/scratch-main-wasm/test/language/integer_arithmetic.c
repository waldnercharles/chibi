#include "compiler_fixture.h"
#include "integer_arithmetic_source.h"
#include "package_compiler_chibi.h"
#include "runtime/core_definition.h"
#include "third_party/pico_headers/pico_unit.h"

static compiler_fixture fixture;

static void cleanup()
{
    compiler_fixture_free(&fixture);
}
TEST_CASE(integer_arithmetic_matches_compiler_backends)
{
    const source_package_input sources[] = {
        { "integer_arithmetic/Arithmetic.chibi",
          INTEGER_ARITHMETIC_SOURCE,
          sizeof INTEGER_ARITHMETIC_SOURCE - 1 },
    };
    const native_definition_pack core[] = { CORE_DEFINITION_PACK };
    REQUIRE(compiler_fixture_prepare_packs(&fixture, sources, 1, core, 1));
    bool native_passed = compiler_fixture_tests_budget(&fixture, 10000);
    bool stage_one_passed = package_compiler_authored_tests(&fixture, sources, 1, 10000);
    REQUIRE(native_passed && stage_one_passed);
    return true;
}
void test_language_integer_arithmetic()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(integer_arithmetic_matches_compiler_backends);
    pu_clear_setup();
}
