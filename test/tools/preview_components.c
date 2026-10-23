#include "assets/project.h"
#include "authoring/asset_compiler_host.h"
#include "authoring/prototype_preview.h"
#include "automation/controller.h"
#include "runtime/declared_components.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char directory[] = "/tmp/chibi-preview-components-XXXXXX";
static bool owns_directory;
static declared_components *previous;
static chibi_preview_run *run;
static prototype_preview *empty_preview;
static char error[512];

static void cleanup()
{
    if (pu_test_failed() && error[0]) {
        fprintf(stderr, "%s\n", error);
    }
    chibi_preview_free(run);
    run = NULL;
    prototype_preview_free(empty_preview);
    empty_preview = NULL;
    declared_components_free(previous);
    previous = NULL;
    if (owns_directory) {
        tool_remove_tree(directory);
        owns_directory = false;
    }
    error[0] = 0;
}

static void write_asset(const char *name, const char *text)
{
    char path[256];
    int size = snprintf(path, sizeof path, "%s/%s", directory, name);
    assert(size > 0 && size < (int)sizeof path);
    tool_write(path, text);
}

static bool registration_project(char *root, size_t size)
{
    strcpy(directory, "/tmp/chibi-preview-components-XXXXXX");
    if (!mkdtemp(directory)) {
        return false;
    }
    owns_directory = true;
    snprintf(root, size, "%s/game", directory);
    if (chibi_project_create(root, "Prototype registration", error, sizeof error)) {
        return false;
    }
    char path[256];
    snprintf(path, sizeof path, "%s/content/prototypes", root);
    return mkdir(path, 0700) == 0;
}

static void registration_prototype(const char *file, const char *name, const char *fields)
{
    char path[256], text[4096];
    snprintf(path, sizeof path, "game/content/prototypes/%s.chibi", file);
    snprintf(
        text,
        sizeof text,
        "package asset\n"
        "import data \"game:assets\"\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(data.Object([\n"
        "        data.Field.Named(\"name\", data.Value.Text(\"%s\"))%s\n"
        "    ]))\n"
        "}\n",
        name,
        fields
    );
    write_asset(path, text);
}

static int registration_preview(const char *root, const char *kind, const char *name)
{
    char request[256];
    snprintf(request, sizeof request, "{\"kind\":\"%s\",\"name\":\"%s\",\"frames\":0}", kind, name);
    error[0] = 0;
    return chibi_automation_preview(root, request, &run, error, sizeof error);
}

static void registration_scene(const char *prototype)
{
    char entities[512] = "", text[2048];
    if (prototype) {
        snprintf(
            entities,
            sizeof entities,
            "data.Object([data.Field.Named(\"name\", "
            "data.Value.Text(\"Probe\")),"
            "data.Field.Named(\"prototype\", data.Value.Text(\"%s\"))])",
            prototype
        );
    }
    snprintf(
        text,
        sizeof text,
        "package asset\nimport data \"game:assets\"\n"
        "pub Build :: () -> byte[] { return data.Encode(data.Object(["
        "data.Field.Named(\"version\", data.Value.UInt(1)),"
        "data.Field.Named(\"name\", data.Value.Text(\"Start\")),"
        "data.Field.Named(\"entities\", data.List([%s]))])) }\n",
        entities
    );
    write_asset("game/content/scenes/Start.chibi", text);
}

TEST_CASE(scene_entry_reuses_validated_names_without_evaluating_unrelated_prototypes)
{
    char root[256];
    REQUIRE(registration_project(root, sizeof root));
    uint64_t extra[3];
    for (int i = 0; i < 3; i++) {
        char file[32], name[32], fields[128];
        snprintf(file, sizeof file, "File%d", i);
        snprintf(name, sizeof name, "Declared%d", i);
        snprintf(fields, sizeof fields, ", data.Field.Named(\"identity\", data.Value.Text(\"counted-%d\"))", i);
        registration_prototype(file, name, fields);
        registration_scene(NULL);
        uint64_t before = asset_source_cache_stats().evaluations;
        REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_OK);
        uint64_t empty = asset_source_cache_stats().evaluations - before;
        chibi_preview_free(run);
        run = NULL;
        registration_scene("Declared0");
        before = asset_source_cache_stats().evaluations;
        REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_OK);
        uint64_t populated = asset_source_cache_stats().evaluations - before;
        REQUIRE(prototype_preview_entity_count(chibi_preview_runtime(run)) == 1);
        REQUIRE(populated >= empty);
        extra[i] = populated - empty;
        chibi_preview_free(run);
        run = NULL;
    }
    printf(
        "Scene entry extra Build evaluations: %llu, %llu, %llu\n",
        (unsigned long long)extra[0],
        (unsigned long long)extra[1],
        (unsigned long long)extra[2]
    );
    REQUIRE(extra[0] == 2 && extra[1] == 2 && extra[2] == 2);
    return true;
}

TEST_CASE(scene_entry_preserves_invalid_shape_filename_fallback_conflicts)
{
    char root[256];
    REQUIRE(registration_project(root, sizeof root));
    registration_prototype("Good", "Wanted", "");
    /* Generic declaration discovery accepts Other, but prototype name lookup
     * rejects its shape and falls back to the filename Wanted. */
    registration_prototype("Wanted", "Other", ", data.Field.Named(\"includes\", data.Value.Int(1))");
    registration_scene("Wanted");
    REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_CONFLICT);
    REQUIRE(!run);
    REQUIRE(strstr(error, "both declare the prototype 'Wanted'"));
    REQUIRE(strstr(error, "Good.chibi") && strstr(error, "Wanted.chibi"));
    return true;
}

TEST_CASE(scene_entry_preserves_lookup_errors_when_enumeration_fails)
{
    char root[256], scenarios[256], unreadable[256];
    REQUIRE(registration_project(root, sizeof root));
    registration_prototype("Good", "Available", "");
    registration_prototype("Wanted", "Other", ", data.Field.Named(\"includes\", data.Value.Int(1))");
    snprintf(scenarios, sizeof scenarios, "%s/content/scenarios", root);
    REQUIRE(!mkdir(scenarios, 0700));
    snprintf(unreadable, sizeof unreadable, "%s/Unreadable.chibi", scenarios);
    tool_write(unreadable, "unreadable unrelated asset");
    REQUIRE(!chmod(unreadable, 0));
    /* The generic declaration exists, but effective prototype naming rejects
     * its shape. Shared source preparation therefore reaches scene lookup. */
    registration_scene("Other");
    REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_NOT_FOUND);
    REQUIRE(!run);
    REQUIRE(strstr(error, "no prototype is named 'Other'"));
    registration_scene("Available");
    REQUIRE(registration_preview(root, "scene", "Start") != CHIBI_AUTOMATION_OK);
    REQUIRE(!run);
    REQUIRE(strstr(error, "Unreadable.chibi"));
    REQUIRE(!chmod(unreadable, 0600));
    return true;
}

TEST_CASE(scene_preparation_reuses_names_for_includes_and_children)
{
    char root[256];
    REQUIRE(registration_project(root, sizeof root));
    registration_prototype("DifferentBaseFile", "Base", ", data.Field.Named(\"identity\", data.Value.Text(\"counted-base\"))");
    registration_prototype("DifferentLeafFile", "Leaf", ", data.Field.Named(\"identity\", data.Value.Text(\"counted-leaf\"))");
    registration_scene(NULL);
    const char *identity = ", data.Field.Named(\"identity\", "
                           "data.Value.Text(\"counted-parent\"))";
    uint64_t extra[3];
    for (int i = 0; i < 3; i++) {
        if (i) {
            char file[32], fields[128];
            snprintf(file, sizeof file, "Filler%d", i);
            snprintf(fields, sizeof fields, ", data.Field.Named(\"identity\", data.Value.Text(\"counted-filler-%d\"))", i);
            registration_prototype(file, file, fields);
        }
        registration_prototype("DifferentParentFile", "Parent", identity);
        uint64_t before = asset_source_cache_stats().evaluations;
        REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_OK);
        uint64_t after = asset_source_cache_stats().evaluations;
        REQUIRE(after >= before);
        uint64_t plain = after - before;
        chibi_preview_free(run);
        run = NULL;
        registration_prototype(
            "DifferentParentFile",
            "Parent",
            ", data.Field.Named(\"identity\", "
            "data.Value.Text(\"counted-parent\"))"
            ", data.Field.Named(\"includes\", "
            "data.List([data.Value.Text(\"Base\")]))"
            ", data.Field.Named(\"children\", data.List(["
            "data.Object([data.Field.Named(\"name\", "
            "data.Value.Text(\"base-child\")),"
            "data.Field.Named(\"prototype\", data.Value.Text(\"Base\"))]),"
            "data.Object([data.Field.Named(\"name\", "
            "data.Value.Text(\"leaf-child\")),"
            "data.Field.Named(\"prototype\", data.Value.Text(\"Leaf\"))])]))"
        );
        before = asset_source_cache_stats().evaluations;
        REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_OK);
        after = asset_source_cache_stats().evaluations;
        REQUIRE(after >= before && after - before >= plain);
        extra[i] = after - before - plain;
        REQUIRE(prototype_preview_entity_count(chibi_preview_runtime(run)) == 0);
        chibi_preview_free(run);
        run = NULL;
    }
    printf(
        "Scene include/child extra Build evaluations: %llu, %llu, %llu\n",
        (unsigned long long)extra[0],
        (unsigned long long)extra[1],
        (unsigned long long)extra[2]
    );
    registration_prototype(
        "ZZInvalid",
        "Invalid",
        ", data.Field.Named(\"components\", data.Object(["
        "data.Field.Named(\"MissingComponent\", data.Object([]))]))"
    );
    REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_VALIDATION_ERROR);
    REQUIRE(!run);
    registration_prototype("ZZInvalid", "Invalid", "");
    REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_OK);
    REQUIRE(extra[0] == 3 && extra[1] == 3 && extra[2] == 3);
    return true;
}

TEST_CASE(prototype_registration_evaluates_a_linear_number_of_sources)
{
    char root[256];
    REQUIRE(registration_project(root, sizeof root));
    uint64_t evaluations[3];
    for (int i = 0; i < 3; i++) {
        char file[32], name[32], fields[128];
        snprintf(file, sizeof file, "File%d", i);
        snprintf(name, sizeof name, "Declared%d", i);
        snprintf(fields, sizeof fields, ", data.Field.Named(\"identity\", data.Value.Text(\"counted-%d\"))", i);
        registration_prototype(file, name, fields);
        uint64_t before = asset_source_cache_stats().evaluations;
        REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_OK);
        uint64_t after = asset_source_cache_stats().evaluations;
        REQUIRE(after >= before);
        evaluations[i] = after - before;
        REQUIRE(prototype_preview_entity_count(chibi_preview_runtime(run)) == 0);
        chibi_preview_free(run);
        run = NULL;
    }
    printf(
        "Prototype registration Build evaluations: %llu, %llu, %llu\n",
        (unsigned long long)evaluations[0],
        (unsigned long long)evaluations[1],
        (unsigned long long)evaluations[2]
    );
    REQUIRE(evaluations[1] > evaluations[0] && evaluations[2] > evaluations[1]);
    REQUIRE(evaluations[2] - evaluations[1] == evaluations[1] - evaluations[0]);
    return true;
}

static bool preview_position(float x, float y)
{
    prototype_preview *preview = chibi_preview_runtime(run);
    if (prototype_preview_entity_count(preview) != 1) {
        return false;
    }
    const ecs_component_desc *transform = ecs_component_catalog_find(project_components(), "Transform");
    ecs_entity entity = prototype_preview_entity_at(preview, 0);
    const CF_V2 *position = transform ? ecs_component_data(
                                            prototype_preview_world(preview),
                                            entity,
                                            transform->id
                                        )
                                      : NULL;
    return position && position->x == x && position->y == y;
}

TEST_CASE(scene_follow_and_override_clone_resolve_after_preparation)
{
    char root[256], behaviors[256];
    REQUIRE(registration_project(root, sizeof root));
    snprintf(behaviors, sizeof behaviors, "%s/content/behaviors", root);
    REQUIRE(!mkdir(behaviors, 0700));
    write_asset("game/content/behaviors/Switcher.chibi", "Switcher :: behavior { on Tick :: () { load_scene(\"Next\") } }\n");
    registration_prototype("RootFile", "Root", ", data.Field.Named(\"behaviors\", data.List([data.Value.Text(\"Switcher\")]))");
    registration_prototype("TargetFile", "Target", "");
    registration_prototype("BaseFile", "Base", "");
    registration_scene("Root");
    write_asset(
        "game/content/scenes/Next.chibi",
        "package asset\nimport data \"game:assets\"\n"
        "pub Build :: () -> byte[] { return data.Encode(data.Object(["
        "data.Field.Named(\"version\", data.Value.UInt(1)),"
        "data.Field.Named(\"name\", data.Value.Text(\"Next\")),"
        "data.Field.Named(\"entities\", data.List([data.Object(["
        "data.Field.Named(\"name\", data.Value.Text(\"Probe\")),"
        "data.Field.Named(\"prototype\", data.Value.Text(\"Target\")),"
        "data.Field.Named(\"components\", data.Object([]))])]))])) }\n"
    );
    REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_OK);
    REQUIRE(chibi_preview_loads(run) == 0);
    /* Base was not an include during preparation. Following the new scene
     * must resolve its first use after the request-local phase has ended. */
    registration_prototype(
        "BaseFile",
        "Base",
        ", data.Field.Named(\"components\", data.Object(["
        "data.Field.Named(\"Transform\", data.Object(["
        "data.Field.Named(\"x\", data.Value.Real(9.0)),"
        "data.Field.Named(\"y\", data.Value.Real(3.0))]))]))"
    );
    registration_prototype("TargetFile", "Target", ", data.Field.Named(\"includes\", data.List([data.Value.Text(\"Base\")]))");
    REQUIRE(chibi_preview_step(run, error, sizeof error) == CHIBI_AUTOMATION_OK);
    REQUIRE(chibi_preview_loads(run) == 1);
    REQUIRE(preview_position(9, 3));
    return true;
}

TEST_CASE(prototype_registration_preserves_declarations_validation_and_fresh_sources)
{
    char root[256];
    REQUIRE(registration_project(root, sizeof root));
    registration_prototype(
        "DifferentBaseFile",
        "Base",
        ", data.Field.Named(\"identity\", data.Value.Text(\"authored-base\"))"
        ", data.Field.Named(\"components\", data.Object(["
        "data.Field.Named(\"Transform\", data.Object(["
        "data.Field.Named(\"x\", data.Value.Real(2.0)),"
        "data.Field.Named(\"y\", data.Value.Real(3.0))]))]))"
    );
    registration_prototype("DifferentChildFile", "Child", ", data.Field.Named(\"includes\", data.List([data.Value.Text(\"Base\")]))");
    REQUIRE(registration_preview(root, "prototype", "Base") == CHIBI_AUTOMATION_OK);
    REQUIRE(preview_position(2, 3));
    prototype_preview *preview = chibi_preview_runtime(run);
    REQUIRE(ecs_component_has(
        prototype_preview_world(preview),
        prototype_preview_entity_at(preview, 0),
        ecs_component_marker_id("authored-base")
    ));
    chibi_preview_free(run);
    run = NULL;
    REQUIRE(registration_preview(root, "prototype", "Child") == CHIBI_AUTOMATION_OK);
    REQUIRE(preview_position(2, 3));
    chibi_preview_free(run);
    run = NULL;
    registration_prototype(
        "DifferentBaseFile",
        "Base",
        ", data.Field.Named(\"identity\", data.Value.Text(\"authored-base\"))"
        ", data.Field.Named(\"components\", data.Object(["
        "data.Field.Named(\"Transform\", data.Object(["
        "data.Field.Named(\"x\", data.Value.Real(9.0)),"
        "data.Field.Named(\"y\", data.Value.Real(3.0))]))]))"
    );
    REQUIRE(registration_preview(root, "prototype", "Child") == CHIBI_AUTOMATION_OK);
    REQUIRE(preview_position(9, 3));
    chibi_preview_free(run);
    run = NULL;
    registration_scene("Child");
    REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_OK);
    REQUIRE(preview_position(9, 3));
    chibi_preview_free(run);
    run = NULL;

    registration_prototype("Base", "Base", "");
    REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_VALIDATION_ERROR);
    REQUIRE(!run);
    char duplicate[256];
    snprintf(duplicate, sizeof duplicate, "%s/content/prototypes/Base.chibi", root);
    REQUIRE(!unlink(duplicate));

    write_asset("game/content/prototypes/Unused.chibi", "package asset\npub Build :: () -> byte[] {\n");
    REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_VALIDATION_ERROR);
    REQUIRE(!run);
    registration_prototype(
        "Unused",
        "Unused",
        ", data.Field.Named(\"components\", data.Object(["
        "data.Field.Named(\"MissingComponent\", data.Object([]))]))"
    );
    REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_VALIDATION_ERROR);
    REQUIRE(!run);
    /* Isolate the direct name cap from an include resolver's error context. */
    registration_prototype("DifferentChildFile", "Child", "");
    char oversized[PROTOTYPE_NAME_MAX + 2];
    memset(oversized, 'N', sizeof oversized - 1);
    oversized[sizeof oversized - 1] = 0;
    registration_prototype("Unused", oversized, "");
    REQUIRE(registration_preview(root, "scene", "Start") == CHIBI_AUTOMATION_VALIDATION_ERROR);
    REQUIRE(!run);
    REQUIRE(strstr(error, "longer than 1024 bytes"));
    return true;
}

TEST_CASE(preview_owns_project_components_until_its_world_is_freed)
{
    REQUIRE(mkdtemp(directory));
    owns_directory = true;
    const char *folders[] = { "content",
                              "content/components",
                              "content/prototypes",
                              "content/scenes" };
    for (int i = 0; i < 4; i++) {
        char path[256];
        snprintf(path, sizeof path, "%s/%s", directory, folders[i]);
        REQUIRE(mkdir(path, 0700) == 0);
    }
    char manifest[16384];
    snprintf(
        manifest,
        sizeof manifest,
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.Int(%d)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Preview "
        "components\")),\n"
        "            data.Field.Named(\"content\", "
        "data.Value.Text(\"content\"))\n"
        "        ])\n"
        "    )\n"
        "}\n",
        CHIBI_PROJECT_VERSION
    );
    write_asset("project.chibi", manifest);
    write_asset(
        "content/components/PreviewHeat.chibi",
        "PreviewHeat :: component {\n"
        "    level: float\n"
        "}\n"
    );
    write_asset(
        "content/prototypes/Probe.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Probe\")),\n"
        "            data.Field.Named(\"components\", data.Object([\n"
        "                data.Field.Named(\"PreviewHeat\", data.Object([\n"
        "                    data.Field.Named(\"level\", data.Value.Int(7))\n"
        "                ]))\n"
        "            ]))\n"
        "        ])\n"
        "    );\n"
        "}\n"
    );
    write_asset(
        "content/scenes/Probe.chibi",
        "package asset\n"
        "\n"
        "import data \"game:assets\"\n"
        "\n"
        "pub Build :: () -> byte[] {\n"
        "    return data.Encode(\n"
        "        data.Object([\n"
        "            data.Field.Named(\"version\", data.Value.UInt(1)),\n"
        "            data.Field.Named(\"name\", data.Value.Text(\"Probe\")),\n"
        "            data.Field.Named(\"entities\", data.List([\n"
        "                data.Object([\n"
        "                    data.Field.Named(\"name\", "
        "data.Value.Text(\"Probe\")),\n"
        "                    data.Field.Named(\"prototype\", "
        "data.Value.Text(\"Probe\"))\n"
        "                ])\n"
        "            ]))\n"
        "        ])\n"
        "    )\n"
        "}\n"
    );
    /* Explicit preview targets do not need a default launch target. Full
     * project/player opening still requires one. */
    chibi_project launch = { 0 };
    REQUIRE(chibi_project_read(&launch, directory, error, sizeof error) != 0);
    REQUIRE(strstr(error, "no start scene"));
    chibi_project_free(&launch);

    const declared_field field = { .name = "value", .type = "float" };
    const declared_component component = { .name = "PreviousPreview",
                                           .fields = &field,
                                           .field_count = 1 };
    previous = declared_components_new(&component, 1, error, sizeof error);
    REQUIRE(previous);
    const type_desc *previous_type = type_catalog_find("PreviousPreview");
    REQUIRE(previous_type);

    const char *kinds[] = { "scene", "prototype" };
    for (int i = 0; i < 2; i++) {
        char request[128];
        snprintf(request, sizeof request, "{\"kind\":\"%s\",\"name\":\"Probe\",\"frames\":1}", kinds[i]);
        REQUIRE(
            chibi_automation_preview(directory, request, &run, error, sizeof error) == CHIBI_AUTOMATION_OK
        );
        REQUIRE(run);
        const type_desc *heat_type = type_catalog_find("PreviewHeat");
        REQUIRE(heat_type);
        REQUIRE(!type_catalog_find("PreviousPreview"));
        const ecs_component_desc *heat = ecs_component_catalog_find(project_components(), "PreviewHeat");
        REQUIRE(heat && heat->type == heat_type);
        prototype_preview *preview = chibi_preview_runtime(run);
        REQUIRE(prototype_preview_entity_count(preview) == 1);
        ecs_world *world = prototype_preview_world(preview);
        ecs_entity entity = prototype_preview_entity_at(preview, 0);
        float *level = ecs_component_data(world, entity, heat->id);
        REQUIRE(level && *level == 7);
        *level = 9;
        REQUIRE(chibi_preview_step(run, error, sizeof error) == CHIBI_AUTOMATION_OK);
        level = ecs_component_data(world, entity, heat->id);
        REQUIRE(level && *level == 9);
        REQUIRE(type_catalog_find("PreviewHeat") == heat_type);
        chibi_preview_free(run);
        run = NULL;
        REQUIRE(!type_catalog_find("PreviewHeat"));
        REQUIRE(type_catalog_find("PreviousPreview") == previous_type);
    }

    REQUIRE(
        chibi_automation_preview(
            directory,
            "{\"kind\":\"scene\",\"name\":\"Missing\",\"frames\":0}",
            &run,
            error,
            sizeof error
        ) == CHIBI_AUTOMATION_NOT_FOUND
    );
    REQUIRE(!run && !type_catalog_find("PreviewHeat"));
    REQUIRE(type_catalog_find("PreviousPreview") == previous_type);

    write_asset(
        "content/components/PreviewHeat.chibi",
        "PreviewHeat :: component {\n"
        "    level: MissingPreviewType\n"
        "}\n"
    );
    REQUIRE(
        chibi_automation_preview(
            directory,
            "{\"kind\":\"scene\",\"name\":\"Probe\",\"frames\":0}",
            &run,
            error,
            sizeof error
        ) == CHIBI_AUTOMATION_VALIDATION_ERROR
    );
    REQUIRE(!run && !type_catalog_find("PreviewHeat"));
    REQUIRE(type_catalog_find("PreviousPreview") == previous_type);
    return true;
}

TEST_CASE(empty_scene_launch_accepts_zero_entities_and_rejects_invalid_arrays)
{
    prototype_registry registry = { 0 };
    empty_preview = prototype_preview_new(&registry);
    REQUIRE(empty_preview);
    REQUIRE(prototype_preview_launch_scene(NULL, NULL, 0, error, sizeof error) == ECS_ERR_ARGUMENT);
    REQUIRE(
        prototype_preview_launch_scene(empty_preview, NULL, -1, error, sizeof error) == ECS_ERR_ARGUMENT
    );
    REQUIRE(
        prototype_preview_launch_scene(empty_preview, NULL, 1, error, sizeof error) == ECS_ERR_ARGUMENT
    );
    REQUIRE(prototype_preview_launch_scene(empty_preview, NULL, 0, error, sizeof error) == ECS_OK);
    REQUIRE(prototype_preview_entity_count(empty_preview) == 0);
    REQUIRE(prototype_preview_tick(empty_preview, 0) == 0);
    REQUIRE(prototype_preview_tick(empty_preview, 1.0 / 60) == 0);
    REQUIRE(prototype_preview_entity_count(empty_preview) == 0);
    prototype_preview_stop(empty_preview);
    prototype_preview_scene_entity unused = { 0 };
    REQUIRE(
        prototype_preview_launch_scene(empty_preview, &unused, 0, error, sizeof error) == ECS_OK
    );
    REQUIRE(prototype_preview_tick(empty_preview, 0) == 0);
    REQUIRE(prototype_preview_entity_count(empty_preview) == 0);
    return true;
}

void test_preview_components()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(preview_owns_project_components_until_its_world_is_freed);
    RUN_TEST_CASE(empty_scene_launch_accepts_zero_entities_and_rejects_invalid_arrays);
    RUN_TEST_CASE(prototype_registration_evaluates_a_linear_number_of_sources);
    RUN_TEST_CASE(prototype_registration_preserves_declarations_validation_and_fresh_sources);
    RUN_TEST_CASE(scene_entry_preserves_invalid_shape_filename_fallback_conflicts);
    RUN_TEST_CASE(scene_entry_preserves_lookup_errors_when_enumeration_fails);
    RUN_TEST_CASE(scene_entry_reuses_validated_names_without_evaluating_unrelated_prototypes);
    RUN_TEST_CASE(scene_preparation_reuses_names_for_includes_and_children);
    RUN_TEST_CASE(scene_follow_and_override_clone_resolve_after_preparation);
    pu_clear_setup();
}
