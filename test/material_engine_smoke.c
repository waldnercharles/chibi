#include "graph/model/registry.h"

#include <string.h>

void test_materials();
void test_material_reactions();
void test_material_thermal();
void test_material_detail();
void test_material_mpm();
void test_material_obstacles();
void test_material_particle_index();
void test_material_gravity();
void test_material_simulation();
void test_material_constitutive();
void test_material_mixed();
void test_material_blast();
void test_fields();
void test_system_schedule();
int bench_fields();
int bench_field_slices();

int main(int argc, char **argv)
{
    registry_set_packs(REGISTRY_PACKS, REGISTRY_PACK_COUNT);
    if (argc > 1 && !strcmp(argv[1], "--bench-fields")) {
        return bench_fields();
    }
    if (argc > 1 && !strcmp(argv[1], "--bench-field-slices")) {
        return bench_field_slices();
    }
    registry_validate();
    test_materials();
    test_material_reactions();
    test_material_thermal();
    test_material_detail();
    test_material_mpm();
    test_material_obstacles();
    test_material_particle_index();
    test_material_gravity();
    test_material_simulation();
    test_material_constitutive();
    test_material_mixed();
    test_material_blast();
    test_fields();
    test_system_schedule();
    return 0;
}
