#include "graph/model/registry.h"
#include "runtime/ui.h"
#include "test/suite_internal.h"

#include <math.h>
#include <string.h>

static void element_state()
{
    game_ui *ui = game_ui_new();
    uint64_t revision = game_ui_revision(ui);
    bool set = game_ui_set_text(ui, 7, "score", "12") &&
               game_ui_set_class(ui, 7, "score", "hot", true) &&
               game_ui_set_class(ui, 7, "score", "hot", false) &&
               game_ui_set_visible(ui, 7, "banner", false) &&
               game_ui_set_value(ui, 7, "charge", 0.5f) &&
               game_ui_focus(ui, 8, "resume");
    game_ui_element *score = game_ui_element_at(ui, 0);
    float charge = 0, missing = 0;
    test_check(
        set && game_ui_element_count(ui) == 4 && game_ui_revision(ui) > revision &&
            strcmp(score->text, "12") == 0 && acount(score->classes) == 1 &&
            !score->classes[0].on && game_ui_element_at(ui, 1)->visible == 0 &&
            game_ui_value(ui, 7, "charge", &charge) && charge == 0.5f &&
            !game_ui_value(ui, 7, "score", &missing) &&
            game_ui_element_at(ui, 3)->focus,
        "game UI keeps each element's text, classes, visibility, value and "
        "focus"
    );
    revision = game_ui_revision(ui);
    bool rejected = !game_ui_set_text(ui, 7, "", "x") &&
                    !game_ui_set_text(ui, 7, "score", NULL) &&
                    !game_ui_set_value(ui, 7, "charge", NAN) &&
                    !game_ui_set_class(ui, 7, "score", "", true);
    test_check(rejected && game_ui_revision(ui) == revision, "game UI rejects empty elements and invalid values without a change");
    game_ui_forget(ui, 7);
    test_check(
        game_ui_element_count(ui) == 1 && game_ui_element_at(ui, 0)->entity == 8,
        "forgetting an entity drops only its elements"
    );
    game_ui_reset(ui);
    test_check(game_ui_element_count(ui) == 0, "resetting game UI clears every element");
    game_ui_free(ui);
}

static void nodes()
{
    static const char *const names[] = { "Set UI Text",      "Set UI Class",
                                         "Set UI Visible",   "Set UI Value",
                                         "Focus UI Element", "Get UI Value" };
    bool found = true;
    for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
        const node_desc *desc = NULL;
        for (int n = 0; n < GAME_UI_PACK.node_count; n++) {
            desc = strcmp(GAME_UI_PACK.nodes[n]->name, names[i]) == 0
                     ? GAME_UI_PACK.nodes[n]
                     : desc;
        }
        found &= desc && strcmp(desc->category, "UI") == 0 && desc->inputs &&
                 (desc->inputs[desc->pure_kind ? 0 : 1].flags & PIN_FLAG_DEFAULT_SELF);
    }
    test_check(found, "UI nodes target their own entity by default");
}

void test_game_ui()
{
    element_state();
    nodes();
}
