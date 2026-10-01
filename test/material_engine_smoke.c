#include "graph/model/registry.h"

#include <string.h>

void test_materials();
void test_material_reactions();
void test_fields();
void test_system_schedule();
int bench_fields();
int bench_field_slices();

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--bench-fields")) {
        return bench_fields();
    }
    if (argc > 1 && !strcmp(argv[1], "--bench-field-slices")) {
        return bench_field_slices();
    }
    registry_validate();
    test_materials();
    test_material_reactions();
    test_fields();
    test_system_schedule();
    return 0;
}
