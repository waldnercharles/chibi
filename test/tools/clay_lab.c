#include "authoring/graph_catalog.h"
#include "authoring/project_prepare.h"
#include "execution/module.h"
#include "host/native_default.h"
#include "render/shader_catalog.h"
#include "render/shader_parameters.h"
#include "render/shader_publish.h"
#include "runtime/component_catalog.h"
#include "runtime/native_prepared.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* One checked-in project preparation and bytecode player; eight specimens,
 * 26 fixed simulation steps plus the initial zero-time boundary, and at most
 * 208 model/material snapshot pairs. Source admission is capped before parsing:
 * 24 files, 32 total directory entries, depth three, 128 KiB combined text.
 * Prepared resources are embedded shader/model/value/part records only, at most
 * 16 records/1 MiB and 8 MiB decoded resource storage. The production resource
 * host validates and publishes real CPU shader/model IR; no fake shader host,
 * GPU upload, window, child process, sleep, screenshot, or timed assertion.
 * Sixteen reflected test-input writes and eight row commits change spacing
 * and dirty before Reset;
 * the full sequence performs exactly 24 model setter validations.
 * New selector execution requires independent workload admission. */
enum
{
    SPECIMENS = 8,
    SCENE_OBJECTS = 11,
    STEPS = 26,
    MAX_FILES = 24,
    MAX_ENTRIES = 32,
    MAX_SOURCE_BYTES = 128 * 1024,
    MAX_ASSETS = 16,
    MAX_PREPARED_BYTES = 1024 * 1024,
    MAX_RESOURCE_BYTES = 8 * 1024 * 1024
};
enum
{
    FORM,
    FINGERPRINTS,
    ROUGHNESS,
    VARIATION,
    SPACING,
    SETTINGS_COUNT
};

static const char *const specimen_names[SPECIMENS] = {
    "Neutral sphere",   "Neutral capsule",   "Neutral block",
    "Neutral sculpt",   "Terracotta sphere", "Terracotta capsule",
    "Terracotta block", "Terracotta sculpt"
};
static const char *const shapes[] = { "ClaySphere", "ClayCapsule", "ClayBlock", "ClaySculpt" };
static const char *const fields[] = { "form", "fingerprints", "roughness", "variation", "spacing" };
static const char *const parameters[] = { "Form", "Fingerprints", "Roughness", "Variation", "Spacing" };
static const double defaults[] = { 1, 1, .68, .06, 2.4 };
static const float pigments[][3] = { { .46f, .43f, .38f }, { .52f, .16f, .08f } };
static const ecs_component_desc *settings;
static ecs_entity specimens[SPECIMENS];
static native_capabilities *native;
static authoring_graph_catalog *palette;
static const registry_pack **previous_packs;
static bool palette_installed;
static authoring_project_artifact artifact;
static player *runtime;
static player_resource_host resources;
static int steps, files, entries, staged;
static size_t source_bytes;
static unsigned program_revision;

static void cleanup()
{
    if (runtime) {
        player_free(runtime);
    }
    runtime = NULL;
    settings = NULL;
    authoring_project_artifact_free(&artifact);
    if (palette_installed) {
        registry_set_packs(previous_packs, acount(previous_packs));
    }
    palette_installed = false;
    afree(previous_packs);
    previous_packs = NULL;
    authoring_graph_catalog_free(palette);
    palette = NULL;
    native_capabilities_free(native);
    native = NULL;
}

static bool bounded_sources(const char *path, int depth)
{
    if (depth > 3) {
        return false;
    }
    DIR *directory = opendir(path);
    if (!directory) {
        return false;
    }
    bool valid = true;
    struct dirent *entry;
    while (valid && (entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }
        char child[4096];
        struct stat info;
        int count = snprintf(child, sizeof child, "%s/%s", path, entry->d_name);
        valid = ++entries <= MAX_ENTRIES && count > 0 &&
                count < (int)sizeof child && !lstat(child, &info);
        if (!valid) {
            break;
        }
        if (S_ISDIR(info.st_mode)) {
            valid = bounded_sources(child, depth + 1);
            continue;
        }
        valid = S_ISREG(info.st_mode) && ++files <= MAX_FILES && info.st_size >= 0 &&
                (size_t)info.st_size <= MAX_SOURCE_BYTES - source_bytes;
        if (valid) {
            source_bytes += (size_t)info.st_size;
        }
    }
    int status = closedir(directory);
    return valid && !status;
}

static void *stage(void *context, const player_asset *assets, int count, size_t *remaining, char *error, size_t size)
{
    if (++staged != 1 || count < 0 || count > MAX_ASSETS) {
        snprintf(error, size, "Clay lab resource count exceeds the admitted workload");
        return NULL;
    }
    size_t bytes = 0;
    for (int i = 0; i < count; i++) {
        if (assets[i].external ||
            (assets[i].kind != PLAYER_ASSET_SHADER && assets[i].kind != PLAYER_ASSET_MODEL &&
             assets[i].kind != PLAYER_ASSET_SHADER_VALUES &&
             assets[i].kind != PLAYER_ASSET_PARTS) ||
            assets[i].length > MAX_PREPARED_BYTES - bytes) {
            snprintf(error, size, "Clay lab resource type or size exceeds the admitted workload");
            return NULL;
        }
        bytes += assets[i].length;
    }
    size_t budget = *remaining < MAX_RESOURCE_BYTES ? *remaining : MAX_RESOURCE_BYTES;
    size_t initial = budget;
    void *generation = resources.stage(context, assets, count, &budget, error, size);
    if (generation) {
        *remaining -= initial - budget;
    }
    return generation;
}

static bool programs_valid()
{
    const char *const names[] = { "ClayControls", "ApplyClay" };
    source_registry *sources = world_sources(player_world(runtime));
    for (int i = 0; i < 2; i++) {
        execution_program *program = source_program(sources, names[i]);
        REQUIRE(program);
        REQUIRE(execution_module_backend(execution_program_module(program)) == BEHAVIOR_BYTECODE);
        const execution_program_failure *failure = execution_program_error(program);
        if (failure) {
            fprintf(stderr, "%s: %s\n", names[i], failure->message);
        }
        REQUIRE(!failure);
    }
    return true;
}

static bool prepare()
{
    /* Graph defaults need the shader vector pack as well as native ECS types. */
    const type_desc *vector = type_catalog_find("ShaderVec3");
    REQUIRE(vector && vector->size == 3 * sizeof(float) && vector->field_count == 3);
    const char *const coordinates[] = { "x", "y", "z" };
    for (int i = 0; i < 3; i++) {
        const type_field *field = &vector->fields[i];
        REQUIRE(!strcmp(field->name, coordinates[i]));
        REQUIRE(
            field->type == PIN_F32 && field->offset == (size_t)i * sizeof(float) &&
            field->size == sizeof(float)
        );
    }
    char project[4096], error[512] = { 0 };
    int count = snprintf(project, sizeof project, "%s/test/fixtures/clay-lab", tool_root());
    REQUIRE(count > 0 && count < (int)sizeof project);
    REQUIRE(bounded_sources(project, 0));
    /* Preparation can migrate old projects. Refuse them before opening source. */
    chibi_project manifest = { 0 };
    int read = chibi_project_read(&manifest, project, error, sizeof error);
    chibi_project_free(&manifest);
    if (read) {
        fprintf(stderr, "Clay lab manifest: %s\n", error);
    }
    REQUIRE(!read);
    native = host_native_default_catalog_new(
        NULL,
        0,
        &RENDER_SHADER_PARAMETER_DEFINITION_PACK,
        1,
        error,
        sizeof error
    );
    REQUIRE(native);
    palette = authoring_graph_catalog_new(
        (authoring_graph_catalog_config){
            .native = native,
            .types = TYPE_PACK,
            .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
        },
        error,
        sizeof error
    );
    REQUIRE(palette);
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous_packs, registry_pack_at(i));
    }
    const registry_pack *const *packs = authoring_graph_catalog_packs(palette, &count);
    registry_set_packs(packs, count);
    palette_installed = true;
    authoring_project_options options = {
        .project = project,
        .native = native,
        .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
        .modules = WORLD_INPUT,
    };
    bool prepared = authoring_project_prepare(&options, &artifact, error, sizeof error);
    if (!prepared) {
        fprintf(stderr, "Clay lab preparation: %s\n", error);
    }
    REQUIRE(prepared);
    REQUIRE(artifact.size <= MAX_PREPARED_BYTES);
    native_prepared_context context = { .native = native };
    execution_prepared_linker linker = native_capabilities_prepared_linker(&context);
    resources = render_prepared_resources(NULL);
    player_host host = {
        .components = { REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
        .linker = &linker,
        .resources = resources,
    };
    host.resources.stage = stage;
    runtime = player_prepared_read(artifact.data, artifact.size, &host, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!runtime) {
        fprintf(stderr, "Clay lab player: %s\n", error);
    }
    REQUIRE(runtime);
    REQUIRE(staged == 1);
    ecs_world *ecs = world_ecs(player_world(runtime));
    for (int i = 0; i < ecs_world_component_count(ecs); i++) {
        const ecs_component_desc *desc = ecs_world_component_at(ecs, i);
        if (!strcmp(desc->name, "ClaySettings")) {
            settings = desc;
        }
    }
    REQUIRE(settings && settings->type);
    const scene_instance *instance = player_scene(runtime);
    const scene *scene = scene_instance_scene(instance);
    REQUIRE(scene_object_count(scene) == SCENE_OBJECTS);
    for (int i = 0; i < SPECIMENS; i++) {
        REQUIRE(!strcmp(scene_object_name(scene, i + 3), specimen_names[i]));
        REQUIRE(scene_object_behavior_count(scene, i + 3) == 1);
        REQUIRE(!strcmp(scene_object_behavior(scene, i + 3, 0), "ClayControls"));
        specimens[i] = scene_instance_entity(instance, i + 3);
    }
    engine *engine = world_engine(player_world(runtime));
    REQUIRE(engine_set_focus(engine, ENGINE_FOCUS_VIEWPORT) == 0);
    REQUIRE(engine_set_playing(engine, true) == 0);
    REQUIRE(engine_frame(engine, 0) == 0);
    REQUIRE(engine_last_timing(engine)->simulation_steps == 0);
    render_shader_model_stats_reset();
    program_revision = render_shader_revision();
    return programs_valid();
}

static int parameter_slot(int shader, const char *name)
{
    for (int i = 0; i < SHADER_PARAMETER_MAX; i++) {
        const char *actual = render_shader_parameter_name(shader, i);
        if (actual && !strcmp(actual, name)) {
            return i;
        }
    }
    return -1;
}

static bool check(const double expected[SETTINGS_COUNT], bool dirty)
{
    ecs_world *ecs = world_ecs(player_world(runtime));
    for (int i = 0; i < SPECIMENS; i++) {
        ecs_entity entity = specimens[i];
        const void *row = ecs_component_data(ecs, entity, settings->id);
        REQUIRE(row);
        pin_value value;
        for (int field = 0; field < SETTINGS_COUNT; field++) {
            REQUIRE(!pin_read_field_path(row, settings->type, fields[field], &value));
            REQUIRE(value.type == PIN_F64);
            REQUIRE(fabs(value.d - expected[field]) < 1e-6);
        }
        bool has_form = i % 4 == 3;
        REQUIRE(!pin_read_field_path(row, settings->type, "has_form", &value));
        REQUIRE(value.type == PIN_BOOL && value.b == has_form);
        REQUIRE(!pin_read_field_path(row, settings->type, "dirty", &value));
        REQUIRE(value.type == PIN_BOOL && value.b == dirty);
        const Shader *binding = Shader_get_const(ecs, entity);
        REQUIRE(binding && !strcmp(binding->shape, shapes[i % 4]));
        REQUIRE(!strcmp(binding->material, "ClaySurface"));
        if (dirty) {
            continue;
        }
        int shape = render_shader_find(binding->shape);
        int material = render_shader_find(binding->material);
        REQUIRE(shape && material && render_shader_is_model(shape));
        float values[SHADER_PARAMETER_MAX][4], min[3], max[3];
        bool empty;
        shader_error error = { 0 };
        REQUIRE(
            render_shader_model_snapshot(shape, ecs, entity, binding->parameters, values, min, max, &empty, &error)
        );
        REQUIRE(!empty);
        int slot = parameter_slot(shape, "Form");
        REQUIRE(has_form ? slot >= 0 : slot == -1);
        if (has_form) {
            REQUIRE(fabsf(values[slot][0] - (float)expected[FORM]) < 1e-6f);
        }
        float bindings[MODEL_MATERIAL_SLOT_MAX + 1][4];
        render_shader_set set = { 0 };
        REQUIRE(render_shader_model_material_snapshot(
            shape,
            ecs,
            entity,
            binding->parameters,
            material,
            bindings,
            values,
            &set,
            &error
        ));
        REQUIRE((int)bindings[0][0] == material);
        for (int field = FINGERPRINTS; field < SETTINGS_COUNT; field++) {
            slot = parameter_slot(material, parameters[field]);
            REQUIRE(slot >= 0 && render_shader_parameter_width(material, slot) == 1);
            REQUIRE(fabsf(values[slot][0] - (float)expected[field]) < 1e-6f);
        }
        slot = parameter_slot(material, "BaseColor");
        REQUIRE(slot >= 0 && render_shader_parameter_width(material, slot) == 3);
        for (int channel = 0; channel < 3; channel++) {
            REQUIRE(fabsf(values[slot][channel] - pigments[i / 4][channel]) < 1e-6f);
        }
    }
    return true;
}

static bool step(uint64_t controls, bool changed, const double expected[SETTINGS_COUNT])
{
    REQUIRE(steps < STEPS);
    engine *engine = world_engine(player_world(runtime));
    render_model_stats before = render_shader_model_stats();
    bool firing;
    REQUIRE(engine_sample_controls(engine, controls, &firing) == 0);
    REQUIRE(!firing);
    int result = engine_frame(engine, (++steps + .5) * ENGINE_FIXED_SECONDS);
    if (result) {
        fprintf(stderr, "Clay lab frame: %s\n", engine_error(engine));
    }
    REQUIRE(!result);
    REQUIRE(engine_last_timing(engine)->simulation_steps == (uint64_t)steps);
    REQUIRE(programs_valid());
    REQUIRE(check(expected, false));
    render_model_stats after = render_shader_model_stats();
    REQUIRE(after.compilations == 0 && after.program_revision == program_revision);
    /* The public counter measures model setter validation, not material calls.
     * Dirty=false and unchanged material snapshots are checked above. */
    REQUIRE(after.setter_validations - before.setter_validations == (changed ? 2 : 0));
    return true;
}

TEST_CASE(clay_lab_authored_controls_apply_presets_and_reset)
{
    REQUIRE(prepare());
    REQUIRE(check(defaults, true));
    REQUIRE(step(0, true, defaults));
    const Control controls[] = { CONTROL_F, CONTROL_G, CONTROL_V, CONTROL_O };
    const int changed_fields[] = { FORM, FINGERPRINTS, VARIATION, ROUGHNESS };
    const double disabled[] = { 0, 0, 0, .5 };
    uint64_t combined = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t control = UINT64_C(1) << controls[i];
        combined |= control;
        double expected[SETTINGS_COUNT];
        memcpy(expected, defaults, sizeof expected);
        expected[changed_fields[i]] = disabled[i];
        REQUIRE(step(control, true, expected));
        REQUIRE(step(control, false, expected));
        REQUIRE(step(0, false, expected));
        REQUIRE(step(control, true, defaults));
        REQUIRE(step(0, false, defaults));
    }
    const double disabled_settings[] = { 0, 0, .5, 0, 2.4 };
    REQUIRE(step(combined, true, disabled_settings));
    /* Model the inspector's parameter edit using the real reflected fields.
     * Reset must restore a changed spacing, not merely retain its default. */
    ecs_world *ecs = world_ecs(player_world(runtime));
    const type_field *spacing = pin_type_field_path(settings->type, "spacing");
    const type_field *dirty = pin_type_field_path(settings->type, "dirty");
    REQUIRE(spacing && spacing->type == PIN_F64 && dirty && dirty->type == PIN_BOOL);
    for (int i = 0; i < SPECIMENS; i++) {
        void *row = ecs_component_data(ecs, specimens[i], settings->id);
        REQUIRE(row);
        pin_write_field(row, spacing, (pin_value){ .type = PIN_F64, .d = 3.6 });
        pin_write_field(row, dirty, (pin_value){ .type = PIN_BOOL, .b = true });
        REQUIRE(ecs_component_modified(ecs, specimens[i], settings->id) == ECS_OK);
    }
    const double edited_settings[] = { 0, 0, .5, 0, 3.6 };
    REQUIRE(step(0, true, edited_settings));
    REQUIRE(step(UINT64_C(1) << CONTROL_R, true, defaults));
    REQUIRE(step(UINT64_C(1) << CONTROL_R, false, defaults));
    REQUIRE(step(0, false, defaults));
    REQUIRE(steps == STEPS);
    REQUIRE(render_shader_model_stats().setter_validations == 24);
    return true;
}

void test_clay_lab()
{
    steps = files = entries = staged = 0;
    source_bytes = 0;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(clay_lab_authored_controls_apply_presets_and_reset);
    pu_clear_setup();
}
