#include "assets/refactor.h"
#include "assets/reference_adapters.h"
#include "assets/scene_document.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const texts[] = {
    "name \"Ship\"\n"
    "(array)children {\n"
    "    - name=\"Left\" prototype=\"Turret\" {\n"
    "        (array)children {\n"
    "            - name=\"Muzzle\" prototype=\"Barrel\"\n"
    "        }\n"
    "    }\n"
    "}\n",
    "name \"Turret\"\n",
    "name \"Barrel\"\n",
};
static const char *const names[] = { "Ship", "Turret", "Barrel" };
enum
{
    SHIP,
    TURRET,
    BARREL,
    PROTOTYPE_COUNT
};

static const char *find_prototype(const char *name, void *)
{
    for (int i = 0; i < PROTOTYPE_COUNT; i++) {
        if (!strcmp(names[i], name)) {
            return texts[i];
        }
    }
    return NULL;
}

static const game_prototype_registry registry = { .include = find_prototype };

/* Renaming a prototype a child uses rewrites the child in its parent. */
TEST_CASE(prototype_children_follow_a_rename)
{
    char error[256] = "";
    reference_index *index = reference_index_new();
    for (int i = 0; i < PROTOTYPE_COUNT; i++) {
        char path[64];
        snprintf(path, sizeof path, "content/prototypes/%s.kdl", names[i]);
        reference_index_add_asset(index, REFERENCE_ASSET_PROTOTYPE, names[i], path, NULL);
    }
    game_prototype *ship = game_prototype_load_authored(texts[SHIP], &registry, error, sizeof error);
    reference_index_add_prototype(index, SHIP, ship);
    game_prototype_free(ship);
    refactor_project project = { index, texts, &registry };
    refactor_plan plan = { 0 };
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_ASSET, .asset = BARREL };
    bool renamed = refactor_rename(&project, symbol, "Cannon", &plan, error, sizeof error) ==
                   0;
    if (!renamed) {
        fprintf(stderr, "rename: %s\n", error);
    }
    game_prototype *saved = NULL;
    for (int i = 0; renamed && i < acount(plan.edits); i++) {
        if (plan.edits[i].asset == SHIP) {
            saved = game_prototype_load_authored(plan.edits[i].text, &registry, error, sizeof error);
        }
    }
    refactor_plan_free(&plan);
    reference_index_free(index);
    int muzzle = saved ? game_scene_document_entity_index(saved->children, "Left.Muzzle")
                       : -1;
    bool followed = muzzle >= 0 &&
                    !strcmp(saved->children->entities[muzzle].prototype, "Cannon");
    game_prototype_free(saved);
    REQUIRE(followed);
    return true;
}

void test_asset_refactor()
{
    RUN_TEST_CASE(prototype_children_follow_a_rename);
}
