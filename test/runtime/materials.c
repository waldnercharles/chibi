/* CPU material checks grouped by capability; each case owns its state. */
void test_material_mask_damage();
void test_material_cave_terrain();
void test_material_body_destruction();
void test_material_fragments();
void test_material_debris();

void test_material_destruction()
{
    test_material_mask_damage();
    test_material_cave_terrain();
    test_material_body_destruction();
    test_material_fragments();
    test_material_debris();
}
