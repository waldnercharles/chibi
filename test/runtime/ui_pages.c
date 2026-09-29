/* A scene's page in the reference index: the scene's own ui:, or the
 * project's default, and what rename makes of each. */
#include "assets/refactor.h"
#include "assets/reference_index.h"
#include "runtime/generated.h"
#include "support/array.h"
#include "test/suite_internal.h"

#include <stdio.h>
#include <string.h>

static const reference *only_use(const reference_index *index, int asset)
{
    const reference **uses = NULL;
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_ASSET, .asset = asset };
    reference_index_find_uses(index, symbol, &uses);
    const reference *use = acount(uses) == 1 ? uses[0] : NULL;
    afree(uses);
    return use;
}

void test_ui_pages()
{
    reference_index *index = reference_index_new();
    int dock = reference_index_add_asset(index, REFERENCE_ASSET_SCENE, "Dock", "content/scenes/Dock.yaml", NULL);
    int bay = reference_index_add_asset(index, REFERENCE_ASSET_SCENE, "Bay", "content/scenes/Bay.yaml", NULL);
    int shell = reference_index_add_asset(index, REFERENCE_ASSET_UI, "Shell", "content/ui/Shell.rml", NULL);
    int page = reference_index_add_asset(index, REFERENCE_ASSET_UI, "Page", "content/ui/Page.rml", NULL);
    reference_index_add_page(index, dock, "Shell", "Page");
    reference_index_add_page(index, bay, NULL, "Page");
    reference_index_add_page(index, bay, "", NULL);
    const reference *own = only_use(index, shell), *inherited = only_use(index, page);
    bool edges = own && own->source == dock && own->role == REFERENCE_BIND &&
                 !strcmp(own->component, REFERENCE_SCENE_PAGE) &&
                 reference_dependency(own) == shell && inherited &&
                 inherited->source == bay &&
                 !strcmp(inherited->component, REFERENCE_PROJECT_PAGE);
    test_check(edges, "a scene binds its page: its own ui:, else the project's");

    const char *texts[] = {
        "version: 1\nname: Dock\nui: Shell\nentities: []\n",
        "version: 1\nname: Bay\nentities: []\n",
        "<slot/>\n",
        "<slot/>\n",
    };
    game_prototype_registry registry = { .components = GAME_REGISTRY_COMPONENTS,
                                         .component_count = GAME_REGISTRY_COMPONENT_COUNT };
    refactor_project project = { index, texts, &registry };
    refactor_plan plan = { 0 };
    char error[256] = "";
    reference_symbol symbol = { .kind = REFERENCE_SYMBOL_ASSET, .asset = shell };
    bool renamed = refactor_rename(&project, symbol, "Frame", &plan, error, sizeof error) ==
                   0;
    bool followed = false;
    for (int i = 0; renamed && i < acount(plan.edits); i++) {
        followed |= plan.edits[i].asset == dock &&
                    strstr(plan.edits[i].text, "ui: Frame");
    }
    if (!renamed) {
        printf("  %s\n", error);
    }
    refactor_plan_free(&plan);
    symbol.asset = page;
    bool refused = refactor_rename(&project, symbol, "Frame", &plan, error, sizeof error) !=
                       0 &&
                   strstr(error, "project.yaml");
    test_check(followed && refused, "renaming a page rewrites the scenes naming it and refuses the project's default");
    refactor_plan_free(&plan);
    reference_index_free(index);
}
