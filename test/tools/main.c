#define PICO_UNIT_IMPLEMENTATION
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

void test_tool_execution_policy();
void test_tool_package();
void test_tool_collab();
void test_tool_studio_selectors(const char *binary);

int main(int argc, char **argv)
{
    if (argc != 1 && (argc != 3 || strcmp(argv[1], "--studio-selectors"))) {
        fprintf(stderr, "Usage: %s [--studio-selectors test_suite]\n", argv[0]);
        return 2;
    }
    tool_init();
    pu_display_time(true);
    if (argc == 3) {
        test_tool_studio_selectors(argv[2]);
    } else {
        test_tool_execution_policy();
        test_tool_package();
        test_tool_collab();
    }
    pu_print_stats();
    return pu_test_failed() ? 1 : 0;
}
