#include "assets/prototype.h"
#include "assets/scene_document.h"
#include "authoring/native_prototype.h"
#include "runtime/component_catalog.h"
#include "support/arena.h"
#include "support/array.h"
#include "test/asset_fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/text.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Prototype and scene documents over the game's reflected components:
 * includes, rebased and detached components, spawning, captured overrides and
 * scene round trips. */

static ecs_component_catalog components()
{
    return (ecs_component_catalog){ REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT };
}
static prototype *test_prototype, *copy;
static ecs_world *world;
static char *saved, *again;
static scene_document *scene, *loaded;
static av_value *values;
static char **owned_strings;

static arena text_reads;
static void cleanup()
{
    arena_free(&text_reads);
    prototype_free(test_prototype);
    prototype_free(copy);
    ecs_world_free(world);
    free(saved);
    free(again);
    scene_document_free(scene);
    scene_document_free(loaded);
    av_value_free(values);
    prototype_owned_strings_free(owned_strings);
    test_prototype = copy = NULL;
    world = NULL;
    saved = again = NULL;
    scene = loaded = NULL;
    values = NULL;
    owned_strings = NULL;
}

static bool rebases(const char *local, const char *previous, const char *current, const char *expected)
{
    av_value *input = test_read_asset_value(local);
    av_value *before = test_read_asset_value(previous);
    av_value *after = test_read_asset_value(current);
    av_value *wanted = test_read_asset_value(expected);
    av_value *actual = prototype_rebase_component_value(input, before, after);
    bool same = actual && av_equals(actual, wanted);
    av_value_free(input);
    av_value_free(before);
    av_value_free(after);
    av_value_free(wanted);
    av_value_free(actual);
    return same;
}

static bool refuses_rebase(const char *local, const char *previous, const char *current)
{
    av_value *input = test_read_asset_value(local);
    av_value *before = test_read_asset_value(previous);
    av_value *after = test_read_asset_value(current);
    av_value *actual = prototype_rebase_component_value(input, before, after);
    av_value_free(input);
    av_value_free(before);
    av_value_free(after);
    av_value_free(actual);
    return !actual;
}

/* Local values survive evolving defaults, nested fields included, with or
 * without a base; invalid snapshots are refused. */
TEST_CASE(rebase_keeps_local_values_over_evolving_defaults)
{
    REQUIRE(rebases(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([\n"
        "                data.Field.Named(\"current\", data.Value.UInt(73)),\n"
        "                data.Field.Named(\"maximum\", data.Value.UInt(100))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([\n"
        "                data.Field.Named(\"current\", data.Value.UInt(100)),\n"
        "                data.Field.Named(\"maximum\", data.Value.UInt(100))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([\n"
        "                data.Field.Named(\"current\", data.Value.UInt(200)),\n"
        "                data.Field.Named(\"maximum\", data.Value.UInt(200))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([\n"
        "                data.Field.Named(\"current\", data.Value.UInt(73)),\n"
        "                data.Field.Named(\"maximum\", data.Value.UInt(200))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    ));
    REQUIRE(rebases(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Input\", data.Object([\n"
        "                data.Field.Named(\"move\", data.Object([\n"
        "                    data.Field.Named(\"x\", data.Value.UInt(3)),\n"
        "                    data.Field.Named(\"y\", data.Value.UInt(0))\n"
        "                ])),\n"
        "                data.Field.Named(\"label\", "
        "data.Value.Text(\"custom\"))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Input\", data.Object([\n"
        "                data.Field.Named(\"move\", data.Object([\n"
        "                    data.Field.Named(\"x\", data.Value.UInt(0)),\n"
        "                    data.Field.Named(\"y\", data.Value.UInt(0))\n"
        "                ])),\n"
        "                data.Field.Named(\"label\", "
        "data.Value.Text(\"default\"))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Input\", data.Object([\n"
        "                data.Field.Named(\"move\", data.Object([\n"
        "                    data.Field.Named(\"x\", data.Value.UInt(1)),\n"
        "                    data.Field.Named(\"y\", data.Value.UInt(2))\n"
        "                ])),\n"
        "                data.Field.Named(\"label\", "
        "data.Value.Text(\"new\")),\n"
        "                data.Field.Named(\"enabled\", data.Value.Bool(true))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Input\", data.Object([\n"
        "                data.Field.Named(\"move\", data.Object([\n"
        "                    data.Field.Named(\"x\", data.Value.UInt(3)),\n"
        "                    data.Field.Named(\"y\", data.Value.UInt(2))\n"
        "                ])),\n"
        "                data.Field.Named(\"label\", "
        "data.Value.Text(\"custom\")),\n"
        "                data.Field.Named(\"enabled\", data.Value.Bool(true))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    ));
    REQUIRE(rebases(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([\n"
        "                data.Field.Named(\"current\", data.Value.UInt(73))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([\n"
        "                data.Field.Named(\"current\", data.Value.UInt(200)),\n"
        "                data.Field.Named(\"maximum\", data.Value.UInt(200))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([\n"
        "                data.Field.Named(\"current\", data.Value.UInt(73)),\n"
        "                data.Field.Named(\"maximum\", data.Value.UInt(200))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    ));
    REQUIRE(rebases(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([\n"
        "                data.Field.Named(\"current\", data.Value.UInt(73))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([\n"
        "                data.Field.Named(\"current\", data.Value.UInt(100))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([\n"
        "                data.Field.Named(\"current\", data.Value.UInt(73))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    ));

    av_value *empty = test_read_asset_value("");
    av_doc *null_doc = av_doc_new();
    av_value *rebased = prototype_rebase_component_value(av_null(null_doc), empty, empty);
    av_doc_free(null_doc);
    av_value_free(empty);
    REQUIRE(!rebased);
    REQUIRE(refuses_rebase("", "", ""));
    REQUIRE(refuses_rebase(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Value.Null)\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "",
        ""
    ));
    REQUIRE(refuses_rebase(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([])),\n"
        "            data.Field.Named(\"Input\", data.Object([]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "",
        ""
    ));
    REQUIRE(refuses_rebase(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Input\", data.Object([]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        ""
    ));
    REQUIRE(refuses_rebase(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        "",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Value.Null)\n"
        "        ])\n"
        "    )\n"
        "}\n"
    ));
    return true;
}

/* Owned strings survive cloning, and stable component keys survive
 * display-name changes. */
TEST_CASE(retained_strings_survive_clones_and_renames)
{
    static const type_field fields[] = { { .name = "text",
                                           .type = PIN_STR,
                                           .offset = 0,
                                           .size = sizeof(char *),
                                           .type_name = "const char *" } };
    static const type_desc type = { "Label", sizeof(char *), fields, 1, NULL };
    static const ecs_component_desc component = { .id = 901,
                                                  .key = "test.Label",
                                                  .name = "Label",
                                                  .element_size = sizeof(char *),
                                                  .type = &type };
    const ecs_component_desc *components[] = { &component };
    prototype_registry registry = { .components = components,
                                    .component_count = 1,
                                    .include = NULL,
                                    .include_user = NULL };
    char error[256];
    test_prototype = prototype_load(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", "
        "data.Value.Text(\"StringOwner\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"test.Label\", data.Object([\n"
        "                    data.Field.Named(\"text\", "
        "data.Value.Text(\"customized\"))\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(test_prototype && prototype_detach_component(test_prototype, "Label") == 0);
    copy = prototype_clone(test_prototype, error, sizeof error);
    prototype_free(test_prototype);
    test_prototype = NULL;
    REQUIRE(copy && prototype_add_component(copy, &component) == 0);
    pin_value value;
    REQUIRE(prototype_read_field(&text_reads, copy, "test.Label", "text", &value) == 0);
    REQUIRE(value.type == PIN_STR && strcmp(pin_text(value).bytes, "customized") == 0);
    REQUIRE(prototype_detach_component(copy, "test.Label") == 0);
    saved = prototype_save(copy);

    ecs_component_desc renamed = component;
    renamed.name = "DisplayLabel";
    const ecs_component_desc *renamed_components[] = { &renamed };
    prototype_registry renamed_registry = { .components = renamed_components,
                                            .component_count = 1,
                                            .include = NULL,
                                            .include_user = NULL };
    test_prototype = prototype_load(saved, &renamed_registry, error, sizeof error);
    REQUIRE(test_prototype && prototype_add_component(test_prototype, &renamed) == 0);
    REQUIRE(
        prototype_read_field(&text_reads, test_prototype, "DisplayLabel", "text", &value) == 0
    );
    REQUIRE(value.type == PIN_STR && strcmp(pin_text(value).bytes, "customized") == 0);
    return true;
}

/* Detached components save, reload and clone; they are absent from spawned
 * entities until reattached with their retained values. */
TEST_CASE(detached_components_spawn_absent_and_reattach)
{
    prototype_registry registry = { .components = REGISTRY_COMPONENTS,
                                    .component_count = REGISTRY_COMPONENT_COUNT };
    char error[256];
    test_prototype = prototype_load(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", "
        "data.Value.Text(\"Retained\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Transform\", data.Object([\n"
        "                    data.Field.Named(\"x\", data.Value.UInt(10)),\n"
        "                    data.Field.Named(\"y\", data.Value.UInt(20))\n"
        "                ])),\n"
        "                data.Field.Named(\"Health\", data.Object([\n"
        "                    data.Field.Named(\"current\", "
        "data.Value.UInt(73)),\n"
        "                    data.Field.Named(\"maximum\", "
        "data.Value.UInt(100))\n"
        "                ])),\n"
        "                data.Field.Named(\"Input\", data.Object([\n"
        "                    data.Field.Named(\"move\", data.Object([\n"
        "                        data.Field.Named(\"x\", data.Value.UInt(3)),\n"
        "                        data.Field.Named(\"y\", data.Value.UInt(4))\n"
        "                    ]))\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(test_prototype);
    again = prototype_save(test_prototype);
    REQUIRE(prototype_detach_component(test_prototype, "Health") == 0);
    REQUIRE(prototype_detach_component(test_prototype, "Input") == 0);
    REQUIRE(prototype_detach_component(test_prototype, "Health") == -1);
    pin_value value;
    REQUIRE(
        prototype_read_field(&text_reads, test_prototype, "Health", "current", &value) == -1
    );
    saved = prototype_save(test_prototype);
    prototype_free(test_prototype);
    test_prototype = prototype_load(saved, &registry, error, sizeof error);
    REQUIRE(test_prototype && acount(test_prototype->detached_components) == 2);
    copy = prototype_clone(test_prototype, error, sizeof error);
    REQUIRE(copy && acount(copy->detached_components) == 2);

    world = ecs_world_new();
    for (int i = 0; i < registry.component_count; i++) {
        REQUIRE(ecs_world_register_component(world, registry.components[i], NULL) == ECS_OK);
    }
    ecs_entity entity;
    REQUIRE(prototype_spawn_components(test_prototype, world, &entity, error, sizeof error) == 0);
    REQUIRE(Transform_has(world, entity));
    REQUIRE(!Health_has(world, entity));
    REQUIRE(!Input_has(world, entity));
    REQUIRE(prototype_add_component(copy, &COMPONENT_Input) == 0);
    REQUIRE(prototype_add_component(copy, &COMPONENT_Health) == 0);
    REQUIRE(acount(copy->detached_components) == 0);
    REQUIRE(acount(test_prototype->detached_components) == 2);
    free(saved);
    saved = prototype_save(copy);
    REQUIRE(strcmp(again, saved) == 0);
    REQUIRE(prototype_spawn_components(copy, world, &entity, error, sizeof error) == 0);
    REQUIRE(Health_get(world, entity)->current == 73);
    REQUIRE(Input_get(world, entity)->move.x == 3);
    REQUIRE(Input_get(world, entity)->move.y == 4);

    prototype *invalid = prototype_load(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", "
        "data.Value.Text(\"Invalid\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Health\", data.Object([]))\n"
        "            ])),\n"
        "            data.Field.Named(\"detached_components\", data.Object([\n"
        "                data.Field.Named(\"Health\", data.Object([]))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(!invalid && strstr(error, "both attached and detached"));
    return true;
}

static const char *base_enemy =
    "package asset\n"
    "\n"
    "import data \"game:assets\"\n"
    "\n"
    "pub Build :: () -> byte[] {\n"
    "    return data.Encode(\n"
    "        data.Object([\n"
    "            data.Field.Named(\"name\", data.Value.Text(\"BaseEnemy\")),\n"
    "            data.Field.Named(\"components\", data.Object([\n"
    "                data.Field.Named(\"Health\", data.Object([\n"
    "                    data.Field.Named(\"current\", data.Value.UInt(50)),\n"
    "                    data.Field.Named(\"maximum\", data.Value.UInt(50))\n"
    "                ])),\n"
    "                data.Field.Named(\"Transform\", data.Object([\n"
    "                    data.Field.Named(\"x\", data.Value.UInt(1)),\n"
    "                    data.Field.Named(\"y\", data.Value.UInt(2))\n"
    "                ]))\n"
    "            ])),\n"
    "            data.Field.Named(\"behaviors\", data.List([\n"
    "                data.Value.Text(\"idle.graph\")\n"
    "            ]))\n"
    "        ])\n"
    "    )\n"
    "}\n";
static const char *derived_enemy =
    "package asset\n"
    "\n"
    "import data \"game:assets\"\n"
    "\n"
    "pub Build :: () -> byte[] {\n"
    "    return data.Encode(\n"
    "        data.Object([\n"
    "            data.Field.Named(\"name\", data.Value.Text(\"Enemy\")),\n"
    "            data.Field.Named(\"identity\", "
    "data.Value.Text(\"enemy-id\")),\n"
    "            data.Field.Named(\"includes\", data.List([\n"
    "                data.Value.Text(\"BaseEnemy\")\n"
    "            ])),\n"
    "            data.Field.Named(\"components\", data.Object([\n"
    "                data.Field.Named(\"Health\", data.Object([\n"
    "                    data.Field.Named(\"current\", data.Value.UInt(100))\n"
    "                ]))\n"
    "            ])),\n"
    "            data.Field.Named(\"behaviors\", data.List([\n"
    "                data.Value.Text(\"attack.graph\")\n"
    "            ]))\n"
    "        ])\n"
    "    )\n"
    "}\n";
static const char
    *cycle_a = "package asset\n"
               "\n"
               "import data \"game:assets\"\n"
               "\n"
               "pub Build :: () -> byte[] {\n"
               "    return data.Encode(\n"
               "        data.Object([\n"
               "            data.Field.Named(\"name\", "
               "data.Value.Text(\"CycleA\")),\n"
               "            data.Field.Named(\"includes\", data.List([\n"
               "                data.Value.Text(\"CycleB\")\n"
               "            ]))\n"
               "        ])\n"
               "    )\n"
               "}\n";
static const char
    *cycle_b = "package asset\n"
               "\n"
               "import data \"game:assets\"\n"
               "\n"
               "pub Build :: () -> byte[] {\n"
               "    return data.Encode(\n"
               "        data.Object([\n"
               "            data.Field.Named(\"name\", "
               "data.Value.Text(\"CycleB\")),\n"
               "            data.Field.Named(\"includes\", data.List([\n"
               "                data.Value.Text(\"CycleA\")\n"
               "            ]))\n"
               "        ])\n"
               "    )\n"
               "}\n";
static int attached;

static const char *find_base(const char *name, void *)
{
    return strcmp(name, "BaseEnemy") == 0 ? base_enemy : NULL;
}

static const char *find_cycle(const char *name, void *)
{
    if (strcmp(name, "CycleA") == 0) {
        return cycle_a;
    }
    return strcmp(name, "CycleB") == 0 ? cycle_b : NULL;
}

static int attach(ecs_world *, ecs_entity, const char *graph_name, void *)
{
    attached += graph_name && graph_name[0] != '\0';
    return 0;
}

/* The game's components, with BaseEnemy to include. */
static prototype_registry enemies()
{
    return (prototype_registry){ .components = REGISTRY_COMPONENTS,
                                 .component_count = REGISTRY_COMPONENT_COUNT,
                                 .include = find_base,
                                 .include_user = NULL };
}

static const Health *health_of(const prototype *p)
{
    for (int i = 0; i < acount(p->components); i++) {
        if (strcmp(p->components[i].desc->name, "Health") == 0) {
            return p->components[i].data;
        }
    }
    return NULL;
}

/* Includes load first and local fields override them by name; saving is
 * deterministic, names derive identities, and missing components and include
 * cycles are refused. */
TEST_CASE(prototype_includes_override_and_save_deterministically)
{
    prototype_registry registry = enemies();
    char error[256];
    test_prototype = prototype_load(derived_enemy, &registry, error, sizeof error);
    REQUIRE(test_prototype && strcmp(test_prototype->name, "Enemy") == 0);
    REQUIRE(acount(test_prototype->components) == 2);
    const Health *health = health_of(test_prototype);
    REQUIRE(health && health->current == 100 && health->maximum == 50);
    saved = prototype_save(test_prototype);
    copy = prototype_load(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"behaviors\", data.List([\n"
        "                data.Value.Text(\"attack.graph\"),\n"
        "                data.Value.Text(\"idle.graph\")\n"
        "            ])),\n"
        "            data.Field.Named(\"identity\", "
        "data.Value.Text(\"enemy-id\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"Transform\", data.Object([\n"
        "                    data.Field.Named(\"y\", data.Value.UInt(2)),\n"
        "                    data.Field.Named(\"x\", data.Value.UInt(1))\n"
        "                ])),\n"
        "                data.Field.Named(\"Health\", data.Object([\n"
        "                    data.Field.Named(\"maximum\", "
        "data.Value.UInt(50)),\n"
        "                    data.Field.Named(\"current\", "
        "data.Value.UInt(100))\n"
        "                ]))\n"
        "            ])),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Enemy\"))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    REQUIRE(copy);
    again = prototype_save(copy);
    REQUIRE(saved && again && strcmp(saved, again) == 0);
    REQUIRE(strstr(saved, "Transform") && strstr(saved, "Health"));
    prototype_free(test_prototype);
    prototype_free(copy);
    free(again);
    test_prototype = prototype_load(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Plain\"))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    copy = prototype_load(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Other\"))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        &registry,
        error,
        sizeof error
    );
    again = test_prototype ? prototype_save(test_prototype) : NULL;
    REQUIRE(test_prototype && copy && again);
    REQUIRE(strcmp(prototype_identity(test_prototype), prototype_identity(copy)) != 0);
    REQUIRE(!strstr(again, "identity"));

    prototype_registry missing = { .components = REGISTRY_COMPONENTS,
                                   .component_count = 1,
                                   .include = find_base,
                                   .include_user = NULL };
    REQUIRE(!prototype_load(derived_enemy, &missing, error, sizeof error));
    REQUIRE(strstr(error, "Health"));
    const prototype_registry cycles = {
        .components = REGISTRY_COMPONENTS,
        .component_count = REGISTRY_COMPONENT_COUNT,
        .include = find_cycle,
        .include_user = NULL,
    };
    REQUIRE(!prototype_load(cycle_a, &cycles, error, sizeof error));
    REQUIRE(strstr(error, "cycle"));
    return true;
}

/* Spawning writes component fields and attaches each behavior; prototypes
 * stay editable data, and a live entity captures as sparse overrides. */
TEST_CASE(prototype_spawns_edits_and_captures_overrides)
{
    prototype_registry registry = enemies();
    char error[256];
    test_prototype = prototype_load(derived_enemy, &registry, error, sizeof error);
    REQUIRE(test_prototype);
    world = ecs_world_new();
    REQUIRE(ecs_world_register_component(world, &COMPONENT_Transform, NULL) == ECS_OK);
    REQUIRE(ecs_world_register_component(world, &COMPONENT_Health, NULL) == ECS_OK);
    attached = 0;
    const prototype_spawn_hooks hooks = { attach, NULL };
    ecs_entity entity = { 0 };
    /* Behavior-bearing prototypes cannot silently skip attachment. */
    REQUIRE(
        prototype_spawn(test_prototype, world, NULL, &entity, error, sizeof error) == ECS_ERR_ARGUMENT
    );
    REQUIRE(ecs_world_live_count(world) == 0 && strstr(error, "attach hook"));
    REQUIRE(prototype_spawn(test_prototype, world, &hooks, &entity, error, sizeof error) == ECS_OK);
    REQUIRE(attached == 2 && ecs_entity_is_alive(world, entity));

    pin_value field = { 0 };
    REQUIRE(prototype_read_field(&text_reads, test_prototype, "Health", "current", &field) == 0);
    REQUIRE(field.i == 100);
    pin_value edited = { .type = PIN_I32, .i = 101 };
    REQUIRE(prototype_write_field(test_prototype, "Health", "current", edited) == 0);
    REQUIRE(prototype_read_field(&text_reads, test_prototype, "Health", "current", &field) == 0);
    REQUIRE(field.i == 101);
    edited.i = 100;
    REQUIRE(prototype_write_field(test_prototype, "Health", "current", edited) == 0);
    REQUIRE(prototype_add_graph(test_prototype, "debug.graph") == 0);
    REQUIRE(prototype_has_graph(test_prototype, "debug.graph"));
    REQUIRE(prototype_remove_graph(test_prototype, "debug.graph") == 0);
    REQUIRE(!prototype_has_graph(test_prototype, "debug.graph"));
    REQUIRE(prototype_remove_component(test_prototype, "Transform") == 0);
    REQUIRE(acount(test_prototype->components) == 1);
    REQUIRE(prototype_add_native_component(test_prototype, &COMPONENT_Transform) == 0);
    REQUIRE(acount(test_prototype->components) == 2);

    copy = prototype_clone(test_prototype, error, sizeof error);
    REQUIRE(copy);
    Health_get(world, entity)->current = 7;
    values = prototype_capture_overrides_value(copy, world, entity);
    REQUIRE(values);
    const av_value *health = av_obj_get(values, "Health");
    REQUIRE(av_get_int(av_obj_get(health, "current")) == 7);
    REQUIRE(!av_obj_get(health, "maximum"));
    av_value_free(values);

    /* Sparse field overrides apply without zeroing sibling fields. */
    values = test_read_asset_value(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([\n"
        "                data.Field.Named(\"current\", data.Value.UInt(7))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    ecs_entity overridden = { 0 };
    REQUIRE(prototype_spawn(copy, world, &hooks, &overridden, error, sizeof error) == ECS_OK);
    REQUIRE(
        prototype_apply_components_value(world, overridden, values, &registry, &owned_strings, error, sizeof error) ==
        0
    );
    REQUIRE(Health_get(world, overridden)->current == 7);
    REQUIRE(Health_get(world, overridden)->maximum == 50);
    av_value_free(values);
    values = NULL;

    Health_get(world, entity)->current = 100;
    prototype_free(copy);
    copy = prototype_capture_entity("CapturedEnemy", world, entity, &registry);
    REQUIRE(copy);
    values = prototype_components_value(copy);
    REQUIRE(values && av_obj_get(values, "Health"));

    /* Freeing a prototype does not change what it spawned. */
    prototype_free(test_prototype);
    test_prototype = NULL;
    const Health *spawned = Health_get(world, entity);
    REQUIRE(spawned && spawned->current == 100 && spawned->maximum == 50);
    return true;
}

/* Reflected values retain enum labels, unsigned colors and exact floating
 * values in canonical constructor source, unchanged by a second round trip. */
TEST_CASE(scene_documents_save_reflected_values_readably)
{
    char error[256];
    scene = scene_document_load(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", "
        "data.Value.Text(\"Readable\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Fluid\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Base\")),\n"
        "                    data.Field.Named(\"components\", data.Object([\n"
        "                        data.Field.Named(\"FluidVolume\", "
        "data.Object([\n"
        "                            data.Field.Named(\"kind\", "
        "data.Value.UInt(1)),\n"
        "                            data.Field.Named(\"color\", "
        "data.Value.UInt(966781183)),\n"
        "                            data.Field.Named(\"absorption\", "
        "data.Value.Real(0.035))\n"
        "                        ])),\n"
        "                        data.Field.Named(\"Transform\", "
        "data.Object([\n"
        "                            data.Field.Named(\"x\", "
        "data.Value.Real(0.15000000596046448)),\n"
        "                            data.Field.Named(\"y\", "
        "data.Value.Real(-0.0))\n"
        "                        ])),\n"
        "                        data.Field.Named(\"Collider\", data.Object([\n"
        "                            data.Field.Named(\"category\", "
        "data.Value.UInt(16))\n"
        "                        ]))\n"
        "                    ]))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        error,
        sizeof error
    );
    REQUIRE(scene);
    saved = scene_document_save(scene, components());
    REQUIRE(saved && strstr(saved, "data.Value.Text(\"Liquid\")"));
    REQUIRE(strstr(saved, "data.Value.UInt(966781183)"));
    loaded = scene_document_load(saved, error, sizeof error);
    REQUIRE(loaded);
    av_value *fluid = av_obj_get(loaded->entities[0].components, "FluidVolume");
    REQUIRE(
        av_is_str(av_obj_get(fluid, "kind")) &&
        !strcmp(av_get_str(av_obj_get(fluid, "kind")), "Liquid")
    );
    REQUIRE(av_get_uint(av_obj_get(fluid, "color")) == 0x399fe8ffu);
    REQUIRE((float)av_get_num(av_obj_get(fluid, "absorption")) == 0.035f);
    av_value *collider = av_obj_get(loaded->entities[0].components, "Collider");
    REQUIRE(av_get_uint(av_obj_get(collider, "category")) == 16);
    again = scene_document_save(loaded, components());
    REQUIRE(again && !strcmp(saved, again));
    av_value *before = av_obj_get(scene->entities[0].components, "Transform");
    av_value *after = av_obj_get(loaded->entities[0].components, "Transform");
    float a = (float)av_get_num(av_obj_get(before, "x"));
    float b = (float)av_get_num(av_obj_get(after, "x"));
    REQUIRE(!memcmp(&a, &b, sizeof a));
    REQUIRE(signbit(av_get_num(av_obj_get(after, "y"))));
    return true;
}

/* A scene entity's prototype, behaviors and component overrides round-trip,
 * enum overrides save as labels without truncation, and physics settings
 * round-trip or fall back to defaults. */
TEST_CASE(scene_documents_round_trip_entities_and_physics)
{
    const char *behaviors[] = { "SceneBehavior", "Damage" };
    scene = scene_document_new("CombatScene");
    REQUIRE(scene && scene_document_add_entity(scene, -1, "Player", "PlayerPrototype") == 0);
    REQUIRE(scene_document_set_entity_behaviors(scene, 0, behaviors, 2) == 0);
    values = test_read_asset_value(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([\n"
        "                data.Field.Named(\"current\", data.Value.UInt(7)),\n"
        "                data.Field.Named(\"maximum\", data.Value.UInt(9))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(scene_document_set_entity_components(scene, 0, values) == 0);
    REQUIRE(av_obj_get(scene->entities[0].components, "Health"));
    av_value_free(values);

    values = test_read_asset_value(
        "package asset\n"
        "import data \"game:assets\"\n"
        "pub Build :: () -> byte[] { return data.Encode(data.Object(["
        "data.Field.Named(\"Appearance\", data.Object(["
        "data.Field.Named(\"shape\", data.Value.Int(0))]))])) }\n"
    );
    REQUIRE(values);
    REQUIRE(scene_document_set_entity_components(scene, 0, values) == 0);
    saved = scene_document_save(scene, components());
    char error[256];
    av_doc *labeled = asset_document_read(saved, error, sizeof error);
    av_value *entity = labeled
                         ? av_arr_get(av_obj_get(av_doc_root(labeled), "entities"), 0)
                         : NULL;
    av_value *appearance = av_obj_get(av_obj_get(entity, "components"), "Appearance");
    bool label = labeled && av_is_str(av_obj_get(appearance, "shape"));
    av_doc_free(labeled);
    REQUIRE(label);
    REQUIRE(av_get_int(av_obj_get(av_obj_get(values, "Appearance"), "shape")) == 0);
    av_value_free(values);
    free(saved);
    saved = NULL;

    values = test_read_asset_value(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"Health\", data.Object([\n"
        "                data.Field.Named(\"current\", data.Value.UInt(7)),\n"
        "                data.Field.Named(\"maximum\", data.Value.UInt(9))\n"
        "            ])),\n"
        "            data.Field.Named(\"Appearance\", data.Object([\n"
        "                data.Field.Named(\"shape\", "
        "data.Value.UInt(4294967296))\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    REQUIRE(scene_document_set_entity_components(scene, 0, values) == 0);
    scene->physics = (physics_settings){ .workers = 8,
                                         .substeps = 2,
                                         .sleep = false,
                                         .continuous = true,
                                         .force_drive = true,
                                         .contact_hertz = 15,
                                         .contact_damping = 1 };
    saved = scene_document_save(scene, components());
    REQUIRE(saved && strstr(saved, "4294967296"));
    loaded = scene_document_load(saved, error, sizeof error);
    REQUIRE(loaded && physics_settings_equal(scene->physics, loaded->physics));
    REQUIRE(acount(loaded->entities) == 1);
    const scene_entity *player = &loaded->entities[0];
    REQUIRE(!strcmp(player->name, "Player") && !strcmp(player->prototype, "PlayerPrototype"));
    REQUIRE(av_obj_get(player->components, "Health"));
    REQUIRE(acount(player->behaviors) == 2 && !strcmp(player->behaviors[1], "Damage"));
    again = scene_document_save(loaded, components());
    REQUIRE(again && !strcmp(saved, again));

    scene_document_free(loaded);
    loaded = scene_document_load(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", "
        "data.Value.Text(\"Minimal\")),\n"
        "            data.Field.Named(\"entities\", data.List([]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        error,
        sizeof error
    );
    REQUIRE(loaded && physics_settings_equal(loaded->physics, physics_defaults()));
    return true;
}

TEST_CASE(scene_documents_refuse_invalid_physics_names_and_versions)
{
    const struct
    {
        const char *name, *value;
    } invalid_physics[] = { { "substeps", "data.Value.Int(0)" },
                            { "substeps", "data.Value.Real(1.5)" },
                            { "workers", "data.Value.Int(33)" },
                            { "contact_damping", "data.Value.Int(-1)" },
                            { "sleep", "data.Value.Int(0)" },
                            { "unknown", "data.Value.Bool(true)" } };
    char error[256];
    for (int i = 0; i < 6; i++) {
        char text[16384];
        snprintf(
            text,
            sizeof text,
            "package asset\n"
            "\n"
            "import data \"game:assets\"\n"
            "\n"
            "pub Build :: () -> byte[] {\n"
            "    return data.Encode(\n"
            "        data.Object([\n"
            "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
            "            data.Field.Named(\"name\", "
            "data.Value.Text(\"Invalid\")),\n"
            "            data.Field.Named(\"entities\", data.List([])),\n"
            "            data.Field.Named(\"physics\", data.Object([\n"
            "                data.Field.Named(\"%s\", %s)\n"
            "            ]))\n"
            "        ])\n"
            "    )\n"
            "}\n",
            invalid_physics[i].name,
            invalid_physics[i].value
        );
        loaded = scene_document_load(text, error, sizeof error);
        REQUIRE(!loaded && strstr(error, "invalid scene physics settings"));
    }
    scene = scene_document_new("Duplicate");
    REQUIRE(scene && scene_document_add_entity(scene, -1, "A", "P") == 0);
    REQUIRE(scene_document_add_entity(scene, -1, "A", "P") != 0);
    loaded = scene_document_load(
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(2)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Bad\")),\n"
        "            data.Field.Named(\"entities\", data.List([]))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        error,
        sizeof error
    );
    REQUIRE(!loaded && strstr(error, "version 1"));
    return true;
}

void test_prototype_documents()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(rebase_keeps_local_values_over_evolving_defaults);
    RUN_TEST_CASE(retained_strings_survive_clones_and_renames);
    RUN_TEST_CASE(detached_components_spawn_absent_and_reattach);
    RUN_TEST_CASE(prototype_includes_override_and_save_deterministically);
    RUN_TEST_CASE(prototype_spawns_edits_and_captures_overrides);
    RUN_TEST_CASE(scene_documents_save_reflected_values_readably);
    RUN_TEST_CASE(scene_documents_round_trip_entities_and_physics);
    RUN_TEST_CASE(scene_documents_refuse_invalid_physics_names_and_versions);
    pu_clear_setup();
}
