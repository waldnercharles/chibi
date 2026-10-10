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
    "name \"Escort\"\n"
    "(array)includes \"Ship\"\n"
    "(array)children {\n"
    "    - name=\"Rear\" prototype=\"Barrel\"\n"
    "}\n",
};
static const char *const names[] = { "Ship", "Turret", "Barrel", "Escort" };
enum
{
    SHIP,
    TURRET,
    BARREL,
    ESCORT,
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

/* Whether the plan rewrites `asset` so that the child at `path` uses
 * `prototype` and the asset still includes what it did. */
static bool rewritten(const refactor_plan *plan, int asset, const char *path, const char *prototype)
{
    char error[256];
    for (int i = 0; i < acount(plan->edits); i++) {
        if (plan->edits[i].asset != asset) {
            continue;
        }
        game_prototype *saved = game_prototype_load_authored(plan->edits[i].text, &registry, error, sizeof error);
        int child = saved ? game_scene_document_entity_index(saved->children, path)
                          : -1;
        game_prototype *original = game_prototype_load_authored(texts[asset], &registry, error, sizeof error);
        bool ok = child >= 0 &&
                  !strcmp(saved->children->entities[child].prototype, prototype) &&
                  acount(saved->children->entities) ==
                      acount(original->children->entities) &&
                  acount(saved->includes) == acount(original->includes);
        game_prototype_free(original);
        game_prototype_free(saved);
        return ok;
    }
    return false;
}

/* Renaming a prototype a child uses rewrites the child in its parent, and a
 * parent that includes another keeps only its own children. */
TEST_CASE(prototype_children_follow_a_rename)
{
    char error[256] = "";
    reference_index *index = reference_index_new();
    for (int i = 0; i < PROTOTYPE_COUNT; i++) {
        char path[64];
        snprintf(path, sizeof path, "content/prototypes/%s.kdl", names[i]);
        reference_index_add_asset(index, REFERENCE_ASSET_PROTOTYPE, names[i], path, NULL);
        game_prototype *prototype = game_prototype_load_authored(texts[i], &registry, error, sizeof error);
        reference_index_add_prototype(index, i, prototype);
        game_prototype_free(prototype);
    }
    refactor_project project = { index, texts, &registry, NULL };
    refactor_plan plan = { 0 };
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_ASSET, .asset = BARREL };
    bool renamed = refactor_rename(&project, symbol, "Cannon", &plan, error, sizeof error) ==
                   0;
    if (!renamed) {
        fprintf(stderr, "rename: %s\n", error);
    }
    bool followed = renamed && rewritten(&plan, SHIP, "Left.Muzzle", "Cannon") &&
                    rewritten(&plan, ESCORT, "Rear", "Cannon");
    refactor_plan_free(&plan);
    reference_index_free(index);
    REQUIRE(followed);
    return true;
}

void test_asset_refactor()
{
    RUN_TEST_CASE(prototype_children_follow_a_rename);
}
