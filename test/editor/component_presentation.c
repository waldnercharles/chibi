#include "editor/documents/component_presentation.h"

#include "support/array.h"
#include "support/text_file.h"
#include "test/suite_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static game_prototype_registry registry;

static int field_index(const ecs_component_desc *desc, const char *name)
{
    for (int i = 0; i < desc->type->field_count; i++) {
        if (strcmp(desc->type->fields[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static const presented_field *field(
    const component_presentation *p,
    const ecs_component_desc *desc,
    const char *name
)
{
    return component_presentation_field(p, desc, field_index(desc, name));
}

static void valid_document()
{
    component_presentation p = { 0 };
    char error[256] = { 0 };
    bool ok = component_presentation_read(
        "components:\n"
        "  GAME.GameCollider:\n"
        "    tab: Physics\n"
        "    fields:\n"
        "      friction: {range: [0, 1]}\n"
        "      radius: {unit: u}\n"
        "  GameHealth: {tab: Gameplay, icon: health}\n"
        "  GAME.GameInput: {icon: gamepad}\n"
        "  GAME.GameWeapon:\n"
        "    fields:\n"
        "      damage: {range: [1, 9], unit: hp}\n",
        &registry,
        &p,
        error,
        sizeof error
    );
    const presented_field *friction = field(&p, &GAME_COMPONENT_GameCollider, "friction");
    const presented_field *radius = field(&p, &GAME_COMPONENT_GameCollider, "radius");
    const presented_field *damage = field(&p, &GAME_COMPONENT_GameWeapon, "damage");
    test_check(
        ok && component_presentation_tab(&p, &GAME_COMPONENT_GameCollider) == COMPONENT_TAB_PHYSICS &&
            component_presentation_tab(&p, &GAME_COMPONENT_GameHealth) ==
                COMPONENT_TAB_GAMEPLAY &&
            component_presentation_tab(&p, &GAME_COMPONENT_GameInput) == COMPONENT_TAB_OBJECT &&
            component_presentation_icon(&p, &GAME_COMPONENT_GameHealth) ==
                COMPONENT_ICON_HEALTH &&
            component_presentation_icon(&p, &GAME_COMPONENT_GameInput) ==
                COMPONENT_ICON_GAMEPAD &&
            component_presentation_icon(&p, &GAME_COMPONENT_GameCollider) ==
                COMPONENT_ICON_NONE &&
            friction && friction->min == 0 && friction->max == 1 &&
            !friction->unit && radius && radius->min == radius->max && radius->unit &&
            strcmp(radius->unit, "u") == 0 && damage && damage->min == 1 &&
            damage->max == 9 && strcmp(damage->unit, "hp") == 0 &&
            !field(&p, &GAME_COMPONENT_GameCollider, "restitution"),
        "component presentation maps tabs, icons, ranges and units by "
        "prototype component names"
    );
    component_presentation_free(&p);
    component_presentation empty = { 0 };
    test_check(
        component_presentation_read("{}", &registry, &empty, error, sizeof error) &&
            component_presentation_tab(&empty, &GAME_COMPONENT_GameBody) ==
                COMPONENT_TAB_OBJECT &&
            component_presentation_icon(&empty, &GAME_COMPONENT_GameCamera) ==
                COMPONENT_ICON_NONE &&
            !field(&empty, &GAME_COMPONENT_GameCollider, "friction"),
        "an empty component presentation presents every component generically"
    );
    component_presentation_free(&empty);
}

static void rejected_documents()
{
    static const struct
    {
        const char *yaml;
        const char *error;
    } cases[] = {
        { "components: {GAME.GameMissing: {tab: Physics}}",
          "unknown component GAME.GameMissing" },
        { "components: {GAME.GameCollider: {fields: {grip: {unit: u}}}}",
          "unknown field grip" },
        { "components: {}\nstyle: dark", "unknown key style" },
        { "components: {GAME.GameBody: {group: Physics}}", "unknown key group" },
        { "components: {GAME.GameCollider: {fields: {radius: {step: 1}}}}",
          "unknown key step" },
        { "components: {GAME.GameBody: {fields: {continuous: {range: [0, "
          "1]}}}}",
          "numeric field" },
        { "components: {GAME.GameAppearance: {fields: {shape: {unit: u}}}}",
          "numeric field" },
        { "components: {GAME.GameCollider: {fields: {friction: {range: [1, "
          "0]}}}}",
          "minimum below" },
        { "components: {GAME.GameCollider: {fields: {friction: {range: [1, "
          "1]}}}}",
          "minimum below" },
        { "components: {GAME.GameCollider: {fields: {friction: {range: [0]}}}}",
          "minimum below" },
        { "components: {GAME.GameCollider: {fields: {radius: {unit: ''}}}}",
          "unit must not be empty" },
        { "components: {GAME.GameBody: {tab: Object}}", "unknown tab Object" },
        { "components: {GAME.GameBody: {tab: Rendering}}", "unknown tab Rendering" },
        { "components: {GAME.GameCamera: {icon: eye}}", "unknown icon eye" },
        { "components: {GAME.GameBody: {tab: Physics}, GameBody: {tab: "
          "Gameplay}}",
          "listed twice" },
        { "components: [GAME.GameBody]", "components must be a mapping" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        component_presentation p = { 0 };
        char error[256] = { 0 };
        bool ok = component_presentation_read(cases[i].yaml, &registry, &p, error, sizeof error);
        char message[320];
        snprintf(
            message,
            sizeof message,
            "component presentation rejects: %s",
            cases[i].error
        );
        test_check(!ok && strstr(error, cases[i].error) && !p.components && !p.fields, message);
    }
}

/* Arena2D's Player keeps the gamepad icon and its components' tabs. */
static void shipped_document()
{
    char *text = text_file_read("projects/arena2d/content/" COMPONENT_PRESENTATION_PATH);
    char *player_text = text_file_read("projects/arena2d/content/prototypes/Player.yaml");
    component_presentation p = { 0 };
    char error[256] = { 0 };
    bool ok = text &&
              component_presentation_read(text, &registry, &p, error, sizeof error);
    game_prototype *player = player_text
                               ? game_prototype_load(player_text, &registry, error, sizeof error)
                               : NULL;
    int tabs = 0, icons = 0;
    for (int i = 0; ok && i < acount(p.components); i++) {
        tabs += p.components[i].tab != COMPONENT_TAB_OBJECT;
        icons += p.components[i].icon != COMPONENT_ICON_NONE;
    }
    component_icon first = COMPONENT_ICON_COUNT;
    for (int i = 0; ok && player && i < acount(player->components); i++) {
        component_icon
            icon = component_presentation_icon(&p, player->components[i].desc);
        if (icon != COMPONENT_ICON_NONE && icon < first) {
            first = icon;
        }
    }
    test_check(
        ok && player && tabs == 22 && icons == 12 && first == COMPONENT_ICON_GAMEPAD &&
            component_presentation_tab(&p, &GAME_COMPONENT_GameCollider) ==
                COMPONENT_TAB_PHYSICS &&
            component_presentation_tab(&p, &GAME_COMPONENT_GameMaterial) ==
                COMPONENT_TAB_MATERIAL &&
            component_presentation_tab(&p, &GAME_COMPONENT_GameHealth) ==
                COMPONENT_TAB_GAMEPLAY &&
            component_presentation_tab(&p, &GAME_COMPONENT_GameAppearance) ==
                COMPONENT_TAB_OBJECT,
        "Arena2D's editor/Components.yaml keeps Player's icon and component "
        "tabs"
    );
    game_prototype_free(player);
    component_presentation_free(&p);
    free(player_text);
    free(text);
}

/* An invalid edit keeps the last valid presentation; a valid one replaces it. */
static void last_valid_document()
{
    component_presentation p = { 0 };
    char error[256] = { 0 };
    bool first = component_presentation_read(
        "components: {GAME.GameCollider: {tab: Physics}}",
        &registry,
        &p,
        error,
        sizeof error
    );
    bool rejected = !component_presentation_read(
        "components: {GAME.GameCollider: {tab: Nowhere}}",
        &registry,
        &p,
        error,
        sizeof error
    );
    component_tab kept = component_presentation_tab(&p, &GAME_COMPONENT_GameCollider);
    bool replaced = component_presentation_read(
        "components: {GAME.GameBody: {tab: Physics}}",
        &registry,
        &p,
        error,
        sizeof error
    );
    test_check(
        first && rejected && kept == COMPONENT_TAB_PHYSICS && replaced &&
            component_presentation_tab(&p, &GAME_COMPONENT_GameCollider) ==
                COMPONENT_TAB_OBJECT &&
            component_presentation_tab(&p, &GAME_COMPONENT_GameBody) == COMPONENT_TAB_PHYSICS,
        "an invalid component presentation keeps the last valid one"
    );
    component_presentation_free(&p);
}

void test_component_presentation()
{
    registry = (game_prototype_registry){
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
    valid_document();
    rejected_documents();
    shipped_document();
    last_valid_document();
}
