#include "graph/model/registry.h"

#define PICO_UNIT_IMPLEMENTATION
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>

void run_code_source_checks();
void run_code_method_checks();
void run_code_declaration_checks();
void run_code_query_checks();
void run_code_value_checks();
void run_code_call_checks();
void test_code_print();
void test_code_source_intelligence();
void run_code_source_name_checks();
void run_code_source_location_checks();
void run_code_source_metadata_checks();
void run_code_components_checks();
void run_code_ecs_intelligence_checks();
void test_code_system_query_inference();
void test_code_ecs_syntax();
void run_code_ecs_print_checks();
void test_code_reactive();
void test_asset_source();

static int failures;
void test_check(int ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

TEST_CASE(native_source_contract)
{
    registry_validate();
    run_code_declaration_checks();
    run_code_components_checks();
    run_code_ecs_intelligence_checks();
    test_code_system_query_inference();
    test_code_ecs_syntax();
    run_code_ecs_print_checks();
    test_code_reactive();
    run_code_method_checks();
    run_code_query_checks();
    run_code_value_checks();
    run_code_call_checks();
    test_code_print();
    test_code_source_intelligence();
    run_code_source_checks();
    run_code_source_name_checks();
    run_code_source_location_checks();
    run_code_source_metadata_checks();
    test_asset_source();
    REQUIRE(failures == 0);
    return true;
}

int main()
{
    RUN_TEST_CASE(native_source_contract);
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}
