#include "graph/model/registry.h"

void test_materials();
void test_material_reactions();
void test_fields();
void test_system_schedule();

int main()
{
    registry_validate();
    test_materials();
    test_material_reactions();
    test_fields();
    test_system_schedule();
    return 0;
}
