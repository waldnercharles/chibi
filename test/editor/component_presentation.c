#include "authoring/component_presentation.h"

#include "support/array.h"
#include "support/text_file.h"
#include "test/suite_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ecs_component_catalog registry;

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
        "components {\n    GAME.GameCollider {\n        tab \"Physics\"\n      "
        "  fields {\n            friction {\n                (array)range {\n  "
        "                  - 0\n                    - 1\n                }\n   "
        "         }\n            radius {\n                unit \"u\"\n        "
        "    }\n        }\n    }\n    GameHealth {\n        tab \"Gameplay\"\n "
        "       icon \"health\"\n    }\n    GAME.GameInput {\n        icon "
        "\"gamepad\"\n    }\n    GAME.GameWeapon {\n        fields {\n         "
        "   damage {\n                (array)range {\n                    - "
        "1\n                    - 9\n                }\n                unit "
        "\"hp\"\n            }\n        }\n    }\n}\n",
        registry,
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
        component_presentation_read("", registry, &empty, error, sizeof error) &&
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
        const char *code;
        const char *error;
    } cases[] = {
        { "components {\n"
          "    GAME.GameMissing {\n"
          "        tab \"Physics\"\n"
          "    }\n"
          "}\n",
          "unknown component GAME.GameMissing" },
        { "components {\n"
          "    GAME.GameCollider {\n"
          "        fields {\n"
          "            grip {\n"
          "                unit \"u\"\n"
          "            }\n"
          "        }\n"
          "    }\n"
          "}\n",
          "unknown field grip" },
        { "(object)components\n"
          "style \"dark\"\n",
          "unknown key style" },
        { "components {\n"
          "    GAME.GameBody {\n"
          "        group \"Physics\"\n"
          "    }\n"
          "}\n",
          "unknown key group" },
        { "components {\n"
          "    GAME.GameCollider {\n"
          "        fields {\n"
          "            radius {\n"
          "                step 1\n"
          "            }\n"
          "        }\n"
          "    }\n"
          "}\n",
          "unknown key step" },
        { "components {\n"
          "    GAME.GameBody {\n"
          "        fields {\n"
          "            continuous {\n"
          "                (array)range {\n"
          "                    - 0\n"
          "                    - 1\n"
          "                }\n"
          "            }\n"
          "        }\n"
          "    }\n"
          "}\n",
          "numeric field" },
        { "components {\n"
          "    GAME.GameAppearance {\n"
          "        fields {\n"
          "            shape {\n"
          "                unit \"u\"\n"
          "            }\n"
          "        }\n"
          "    }\n"
          "}\n",
          "numeric field" },
        { "components {\n"
          "    GAME.GameCollider {\n"
          "        fields {\n"
          "            friction {\n"
          "                (array)range {\n"
          "                    - 1\n"
          "                    - 0\n"
          "                }\n"
          "            }\n"
          "        }\n"
          "    }\n"
          "}\n",
          "minimum below" },
        { "components {\n"
          "    GAME.GameCollider {\n"
          "        fields {\n"
          "            friction {\n"
          "                (array)range {\n"
          "                    - 1\n"
          "                    - 1\n"
          "                }\n"
          "            }\n"
          "        }\n"
          "    }\n"
          "}\n",
          "minimum below" },
        { "components {\n"
          "    GAME.GameCollider {\n"
          "        fields {\n"
          "            friction {\n"
          "                (array)range {\n"
          "                    - 0\n"
          "                }\n"
          "            }\n"
          "        }\n"
          "    }\n"
          "}\n",
          "minimum below" },
        { "components {\n"
          "    GAME.GameCollider {\n"
          "        fields {\n"
          "            radius {\n"
          "                unit \"\"\n"
          "            }\n"
          "        }\n"
          "    }\n"
          "}\n",
          "unit must not be empty" },
        { "components {\n"
          "    GAME.GameBody {\n"
          "        tab \"Object\"\n"
          "    }\n"
          "}\n",
          "unknown tab Object" },
        { "components {\n"
          "    GAME.GameBody {\n"
          "        tab \"Rendering\"\n"
          "    }\n"
          "}\n",
          "unknown tab Rendering" },
        { "components {\n"
          "    GAME.GameCamera {\n"
          "        icon \"eye\"\n"
          "    }\n"
          "}\n",
          "unknown icon eye" },
        { "components {\n"
          "    GAME.GameBody {\n"
          "        tab \"Physics\"\n"
          "    }\n"
          "    GameBody {\n"
          "        tab \"Gameplay\"\n"
          "    }\n"
          "}\n",
          "listed twice" },
        { "(array)components {\n"
          "    - \"GAME.GameBody\"\n"
          "}\n",
          "components must be a mapping" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        component_presentation p = { 0 };
        char error[256] = { 0 };
        bool ok = component_presentation_read(cases[i].code, registry, &p, error, sizeof error);
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

/* A synthetic subject keeps the gamepad icon and its components' tabs. */
static void shipped_document()
{
    char *text = text_file_read("test/fixtures/component-presentation/content/" COMPONENT_PRESENTATION_PATH);
    char *player_text = text_file_read("test/fixtures/component-presentation/content/prototypes/InspectorSubject.kdl");
    component_presentation p = { 0 };
    char error[256] = { 0 };
    bool ok = text &&
              component_presentation_read(text, registry, &p, error, sizeof error);
    game_prototype *player = player_text
                               ? game_prototype_load(
                                     player_text,
                                     &(game_prototype_registry){
                                         .components = registry.components,
                                         .component_count = registry.component_count },
                                     error,
                                     sizeof error
                                 )
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
        "The component presentation fixture keeps the subject's icon and "
        "component "
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
        "components {\n"
        "    GAME.GameCollider {\n"
        "        tab \"Physics\"\n"
        "    }\n"
        "}\n",
        registry,
        &p,
        error,
        sizeof error
    );
    bool rejected = !component_presentation_read(
        "components {\n"
        "    GAME.GameCollider {\n"
        "        tab \"Nowhere\"\n"
        "    }\n"
        "}\n",
        registry,
        &p,
        error,
        sizeof error
    );
    component_tab kept = component_presentation_tab(&p, &GAME_COMPONENT_GameCollider);
    bool replaced = component_presentation_read(
        "components {\n"
        "    GAME.GameBody {\n"
        "        tab \"Physics\"\n"
        "    }\n"
        "}\n",
        registry,
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
    registry = (ecs_component_catalog){
        .components = GAME_REGISTRY_COMPONENTS,
        .component_count = GAME_REGISTRY_COMPONENT_COUNT,
    };
    valid_document();
    rejected_documents();
    shipped_document();
    last_valid_document();
}
