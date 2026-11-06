#include "assets/refactor.h"
#include "assets/reference_adapters.h"
#include "assets/scene_document.h"
#include "graph/authoring/asset_edit.h"
#include "graph/document/serialize.h"
#include "graph/model/registry.h"
#include "graph/nodes/core_nodes.h"
#include "runtime/component_catalog.h"
#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

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

static const prototype_registry registry = { .include = find_prototype };

/* Whether the plan rewrites `asset` so that the child at `path` uses
 * `expected` and the asset still includes what it did. */
static bool rewritten(const refactor_plan *plan, int asset, const char *path, const char *expected)
{
    char error[256];
    for (int i = 0; i < acount(plan->edits); i++) {
        if (plan->edits[i].asset != asset) {
            continue;
        }
        prototype *saved = prototype_load_authored(plan->edits[i].text, &registry, error, sizeof error);
        int child = saved ? scene_document_entity_index(saved->children, path) : -1;
        prototype *original = prototype_load_authored(texts[asset], &registry, error, sizeof error);
        bool ok = child >= 0 &&
                  !strcmp(saved->children->entities[child].prototype, expected) &&
                  acount(saved->children->entities) ==
                      acount(original->children->entities) &&
                  acount(saved->includes) == acount(original->includes);
        prototype_free(original);
        prototype_free(saved);
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
        prototype *loaded = prototype_load_authored(texts[i], &registry, error, sizeof error);
        reference_index_add_prototype(index, i, loaded);
        prototype_free(loaded);
    }
    refactor_project project = { index, texts, &registry };
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

static reference_index *particle_index;
static behavior_asset *particle_library;
static prototype *particle_prototype;
static scene_document *particle_scene;
static char *particle_library_text;
static const registry_pack **previous_packs;
static refactor_plan particle_plan;
static const ecs_component_desc *const particle_components[] = { &COMPONENT_Shader };
static const prototype_registry particle_registry = {
    .components = particle_components,
    .component_count = 1,
};
static const char *const
    particle_prototype_text = "name \"Cloud\"\ncomponents {\n"
                              "    Shader material=\"RainParticles.Fall\" "
                              "parameters=\"Speed 2\\n\"\n}\n";
static const char *const
    particle_scene_text = "version 1\nname \"RainScene\"\n(array)entities {\n"
                          "    - name=\"Drops\" prototype=\"Cloud\" {\n"
                          "        components {\n"
                          "            Shader material=\"RainParticles.Fall\"\n"
                          "        }\n"
                          "    }\n}\n";

static void particle_cleanup()
{
    refactor_plan_free(&particle_plan);
    reference_index_free(particle_index);
    particle_index = NULL;
    behavior_asset_free(particle_library);
    particle_library = NULL;
    prototype_free(particle_prototype);
    particle_prototype = NULL;
    scene_document_free(particle_scene);
    particle_scene = NULL;
    free(particle_library_text);
    particle_library_text = NULL;
    registry_set_packs(previous_packs, acount(previous_packs));
    afree(previous_packs);
}

static void particle_setup()
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous_packs, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &CORE_FLOW_PACK };
    registry_set_packs(packs, 1);
    particle_index = reference_index_new();
}

TEST_CASE(qualified_shader_bindings_resolve_functions)
{
    int library = reference_index_add_asset(particle_index, REFERENCE_ASSET_LIBRARY, "RainParticles", NULL, "library");
    int source = reference_index_add_asset(particle_index, REFERENCE_ASSET_PROTOTYPE, "Cloud", NULL, "prototype");
    reference_index_add_named_field(
        particle_index,
        (reference){ .source = source },
        "Shader",
        "material",
        "RainParticles.Fall"
    );
    REQUIRE(
        reference_index_find_name(particle_index, REFERENCE_ASSET_MATERIAL, "RainParticles.Fall") < 0
    );
    reference_symbol function = { .kind = REFERENCE_SYMBOL_GRAPH,
                                  .asset = library,
                                  .id = 42,
                                  .name = "Fall" };
    reference_index_add_reference(
        particle_index,
        (reference){ .role = REFERENCE_DEFINE, .symbol = function, .source = library }
    );
    reference_index_add_named_field(
        particle_index,
        (reference){ .source = source, .object = "Second" },
        "Shader",
        "material",
        "RainParticles.Fall"
    );
    REQUIRE(reference_index_count_uses(particle_index, function, REFERENCE_USES) == 2);
    REQUIRE(reference_index_definition(
        particle_index,
        reference_index_at(particle_index, 0)->symbol
    ));
    REQUIRE(reference_dependency(reference_index_at(particle_index, 0)) == library);
    reference_symbol asset = { .kind = REFERENCE_SYMBOL_ASSET, .asset = library };
    REQUIRE(reference_index_count_uses(particle_index, asset, REFERENCE_USES) == 2);
    reference_index_add_named_field(
        particle_index,
        (reference){ .source = source },
        "Shader",
        "material",
        "RainParticles.Missing"
    );
    const reference *missing = reference_index_at(
        particle_index,
        reference_index_count(particle_index) - 1
    );
    REQUIRE(missing->symbol.asset == library && !missing->symbol.id);
    REQUIRE(!reference_index_definition(particle_index, missing->symbol));
    REQUIRE(reference_index_count_uses(particle_index, function, REFERENCE_USES) == 2);
    reference_symbol missing_symbol = missing->symbol;
    reference_index_add_named_field(
        particle_index,
        (reference){ .source = source },
        "Shader",
        "material",
        "RainParticles.OtherMissing"
    );
    REQUIRE(reference_index_count_uses(particle_index, missing_symbol, REFERENCE_USES) == 1);
    /* A literal material asset keeps its identity even when its name has a dot. */
    int material = reference_index_add_asset(
        particle_index,
        REFERENCE_ASSET_MATERIAL,
        "RainParticles.Solid",
        NULL,
        "material"
    );
    reference_index_add_named_field(
        particle_index,
        (reference){ .source = source },
        "Shader",
        "material",
        "RainParticles.Solid"
    );
    const reference *solid = reference_index_at(particle_index, reference_index_count(particle_index) - 1);
    REQUIRE(solid->symbol.kind == REFERENCE_SYMBOL_ASSET && solid->symbol.asset == material);
    return true;
}

static bool particle_refactor_fixture()
{
    particle_library = behavior_asset_new("RainParticles");
    particle_library->domain = BEHAVIOR_DOMAIN_SHADER_LIBRARY;
    REQUIRE(behavior_asset_add_function(particle_library, "Fall", true));
    particle_library_text = behavior_asset_save(particle_library);
    REQUIRE(particle_library_text);
    REQUIRE(
        reference_index_add_asset(
            particle_index,
            REFERENCE_ASSET_LIBRARY,
            "RainParticles",
            "content/shader-libraries/RainParticles.kdl",
            NULL
        ) == 0
    );
    REQUIRE(
        reference_index_add_asset(particle_index, REFERENCE_ASSET_PROTOTYPE, "Cloud", "content/prototypes/Cloud.kdl", NULL) ==
        1
    );
    REQUIRE(
        reference_index_add_asset(particle_index, REFERENCE_ASSET_SCENE, "RainScene", "content/scenes/RainScene.kdl", NULL) ==
        2
    );
    char error[256] = { 0 };
    particle_prototype = prototype_load_authored(particle_prototype_text, &particle_registry, error, sizeof error);
    REQUIRE(particle_prototype);
    reference_index_add_prototype(particle_index, 1, particle_prototype);
    particle_scene = scene_document_load(particle_scene_text, error, sizeof error);
    if (!particle_scene) {
        fprintf(stderr, "particle refactor scene: %s\n", error);
    }
    REQUIRE(particle_scene && acount(particle_scene->entities) == 1);
    reference_object object = { .name = "Drops",
                                .prototype = "Cloud",
                                .overrides = particle_scene->entities[0].components };
    reference_index_add_object(particle_index, 2, &object);
    reference_index_add_graphs(particle_index, 0, particle_library);
    return true;
}

static bool particle_bindings_rewritten(const char *expected)
{
    int rewritten = 0;
    for (int i = 0; i < acount(particle_plan.edits); i++) {
        const refactor_edit *edit = &particle_plan.edits[i];
        char error[256] = { 0 };
        if (edit->asset == 1) {
            prototype *saved = prototype_load_authored(edit->text, &particle_registry, error, sizeof error);
            pin_value material = { 0 }, parameters = { 0 };
            arena text_reads = { 0 };
            bool ok = saved &&
                      !prototype_read_field(&text_reads, saved, "Shader", "material", &material) &&
                      !prototype_read_field(&text_reads, saved, "Shader", "parameters", &parameters) &&
                      !strcmp(pin_text(material).bytes, expected) &&
                      !strcmp(pin_text(parameters).bytes, "Speed 2\n");
            prototype_free(saved);
            arena_free(&text_reads);
            REQUIRE(ok);
            rewritten++;
        }
        if (edit->asset == 2) {
            scene_document *saved = scene_document_load(edit->text, error, sizeof error);
            const char *material = saved && acount(saved->entities) == 1
                                     ? av_get_str(av_obj_get(
                                           av_obj_get(saved->entities[0].components, "Shader"),
                                           "material"
                                       ))
                                     : NULL;
            bool ok = material && !strcmp(material, expected);
            scene_document_free(saved);
            REQUIRE(ok);
            rewritten++;
        }
    }
    REQUIRE(rewritten == 2);
    return true;
}

TEST_CASE(qualified_shader_bindings_follow_library_and_function_renames)
{
    REQUIRE(particle_refactor_fixture());
    const char *texts[] = { particle_library_text,
                            particle_prototype_text,
                            particle_scene_text };
    refactor_project project = { particle_index, texts, &particle_registry };
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_ASSET, .asset = 0 };
    char error[256] = { 0 };
    REQUIRE(
        refactor_rename(&project, symbol, "SnowParticles", &particle_plan, error, sizeof error) == 0
    );
    REQUIRE(particle_bindings_rewritten("SnowParticles.Fall"));
    refactor_plan_free(&particle_plan);
    symbol = (reference_symbol){
        .kind = REFERENCE_SYMBOL_GRAPH,
        .asset = 0,
        .id = behavior_asset_find_graph_by_name(particle_library, "Fall")->graph_id
    };
    REQUIRE(refactor_rename(&project, symbol, "Drift", &particle_plan, error, sizeof error) == 0);
    REQUIRE(particle_bindings_rewritten("RainParticles.Drift"));
    const reference **blockers = NULL;
    refactor_blockers(particle_index, symbol, &blockers);
    int count = acount(blockers);
    afree(blockers);
    REQUIRE(count == 2);
    return true;
}

TEST_CASE(qualified_shader_bindings_reject_stale_refactors)
{
    REQUIRE(particle_refactor_fixture());
    const char *texts[] = { particle_library_text,
                            "name \"Cloud\"\ncomponents {\n    Shader "
                            "material=\"RainParticles.Other\"\n}\n",
                            particle_scene_text };
    refactor_project project = { particle_index, texts, &particle_registry };
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_ASSET, .asset = 0 };
    char error[256] = { 0 };
    REQUIRE(
        refactor_rename(&project, symbol, "SnowParticles", &particle_plan, error, sizeof error) != 0
    );
    REQUIRE(strstr(error, "changed since it was indexed"));
    REQUIRE(!acount(particle_plan.edits));
    return true;
}

void test_asset_refactor()
{
    RUN_TEST_CASE(prototype_children_follow_a_rename);
    pu_setup(particle_setup, particle_cleanup);
    RUN_TEST_CASE(qualified_shader_bindings_resolve_functions);
    RUN_TEST_CASE(qualified_shader_bindings_follow_library_and_function_renames);
    RUN_TEST_CASE(qualified_shader_bindings_reject_stale_refactors);
    pu_clear_setup();
}
