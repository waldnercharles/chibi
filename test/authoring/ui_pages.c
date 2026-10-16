/* A scene's page in the reference index: the scene's own ui:, or the
 * project's default, and what rename makes of each. */
#include "assets/refactor.h"
#include "authoring/reference_index.h"
#include "runtime/component_catalog.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <string.h>

static const reference *only_use(const reference_index *refs, int asset)
{
    const reference **uses = NULL;
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_ASSET, .asset = asset };
    reference_index_find_uses(refs, symbol, &uses);
    const reference *use = acount(uses) == 1 ? uses[0] : NULL;
    afree(uses);
    return use;
}

static reference_index *refs;
static refactor_plan plan;

static void cleanup()
{
    refactor_plan_free(&plan);
    reference_index_free(refs);
    refs = NULL;
}

TEST_CASE(scene_pages_bind_and_rename_with_their_scenes)
{
    refs = reference_index_new();
    int dock = reference_index_add_asset(refs, REFERENCE_ASSET_SCENE, "Dock", "content/scenes/Dock.kdl", NULL);
    int bay = reference_index_add_asset(refs, REFERENCE_ASSET_SCENE, "Bay", "content/scenes/Bay.kdl", NULL);
    int shell = reference_index_add_asset(refs, REFERENCE_ASSET_UI, "Shell", "content/ui/Shell.rml", NULL);
    int page = reference_index_add_asset(refs, REFERENCE_ASSET_UI, "Page", "content/ui/Page.rml", NULL);
    reference_index_add_page(refs, dock, "Shell", "Page");
    reference_index_add_page(refs, bay, NULL, "Page");
    reference_index_add_page(refs, bay, "", NULL);
    const reference *own = only_use(refs, shell), *inherited = only_use(refs, page);
    bool edges = own && own->source == dock && own->role == REFERENCE_BIND &&
                 !strcmp(own->component, REFERENCE_SCENE_PAGE) &&
                 reference_dependency(own) == shell && inherited &&
                 inherited->source == bay &&
                 !strcmp(inherited->component, REFERENCE_PROJECT_PAGE);
    /* A scene binds its page: its own ui:, else the project's. */
    REQUIRE(edges);

    const char *texts[] = {
        "version 1\n"
        "name \"Dock\"\n"
        "ui \"Shell\"\n"
        "(array)entities\n",
        "version 1\n"
        "name \"Bay\"\n"
        "(array)entities\n",
        "<slot/>\n",
        "<slot/>\n",
    };
    prototype_registry registry = { .components = REGISTRY_COMPONENTS,
                                    .component_count = REGISTRY_COMPONENT_COUNT };
    refactor_project project = { .index = refs, .texts = texts, .registry = &registry };
    char error[256] = "";
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_ASSET, .asset = shell };
    bool renamed = refactor_rename(&project, symbol, "Frame", &plan, error, sizeof error) ==
                   0;
    bool followed = false;
    for (int i = 0; renamed && i < acount(plan.edits); i++) {
        followed |= plan.edits[i].asset == dock &&
                    strstr(plan.edits[i].text, "ui \"Frame\"");
    }
    if (!renamed) {
        printf("  %s\n", error);
    }
    refactor_plan_free(&plan);
    symbol.asset = page;
    bool refused = refactor_rename(&project, symbol, "Frame", &plan, error, sizeof error) !=
                       0 &&
                   strstr(error, "project.kdl");
    /* Renaming a page rewrites the scenes naming it and refuses the project's default. */
    REQUIRE(followed && refused);
    return true;
}

void test_ui_pages()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(scene_pages_bind_and_rename_with_their_scenes);
    pu_clear_setup();
}
