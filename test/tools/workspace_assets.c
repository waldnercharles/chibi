#include "authoring/workspace_assets.h"

#include "authoring/component_presentation.h"
#include "authoring/workspace_assets_definition.h"
#include "authoring/workspace_assets_scope.h"
#include "authoring/workspace_presentation.h"
#include "execution/function.h"
#include "graph/modeling/model_compile.h"
#include "graph/nodes/field_nodes.h"
#include "graph/shader/shader_graph.h"
#include "runtime/component_catalog.h"
#include "runtime/declared_components.h"
#include "runtime/scenes/physics_settings.h"
#include "runtime/value_json.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/asset_text.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static const registry_pack **previous_packs;
static bool project_types;
static workspace_documents *object_documents;
static declared_components *object_components;
static const ecs_component_desc **object_descriptors;
static workspace_assets_service *scope_service;
static char scope_root[1024];
static char *prototype_candidates[3];
static arena inactive_storage;
static char **clipboard_paths;
static int clipboard_path_count;
static void setup()
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous_packs, registry_pack_at(i));
    }
    const registry_pack *packs[] = { &SHADER_PACK, &MODEL_PACK, &FIELD_NODE_PACK };
    registry_set_packs(packs, 3);
}
static void cleanup()
{
    arena_free(&inactive_storage);
    for (int i = 0; i < clipboard_path_count; i++) {
        free(clipboard_paths[i]);
    }
    free(clipboard_paths);
    clipboard_paths = NULL;
    clipboard_path_count = 0;
    for (int i = 0; i < 3; i++) {
        free(prototype_candidates[i]);
        prototype_candidates[i] = NULL;
    }
    workspace_assets_service_free(scope_service);
    scope_service = NULL;
    if (*scope_root) {
        tool_remove_tree(scope_root);
        scope_root[0] = 0;
    }
    workspace_documents_free(object_documents);
    object_documents = NULL;
    declared_components_free(object_components);
    object_components = NULL;
    afree(object_descriptors);
    registry_set_packs(previous_packs, acount(previous_packs));
    afree(previous_packs);
    if (project_types) {
        type_catalog_set_project(NULL);
        project_types = false;
    }
}
TEST_CASE(workspace_templates_cover_canonical_kinds_without_disk_mutation)
{
    char error[256];
    for (int i = 0; i < workspace_asset_kind_count(); i++) {
        workspace_asset_template_result out = { 0 };
        REQUIRE(
            workspace_asset_template(workspace_asset_kind_name(i), "Example", &out, error, sizeof error)
        );
        REQUIRE(out.text && *out.text && strstr(out.path, "content/") == out.path);
        REQUIRE(strstr(out.path, "Example"));
        REQUIRE(strstr(out.path, workspace_asset_kind_extension(i)));
        if (i < NEW_ASSET_KIND_COUNT &&
            !strcmp(workspace_asset_kind_extension(i), ".kdl")) {
            av_doc *doc = av_doc_read(out.text, error, sizeof error);
            REQUIRE(doc);
            av_doc_free(doc);
        }
        workspace_asset_template_free(&out);
        REQUIRE(!out.path && !out.text);
    }
    return true;
}
TEST_CASE(workspace_templates_refuse_unsafe_names_and_offer_authored_ui)
{
    workspace_asset_template_result out = { 0 };
    char error[256];
    REQUIRE(!workspace_asset_template("scene", "../Escape", &out, error, sizeof error));
    REQUIRE(!out.path && !out.text && *error);
    REQUIRE(!workspace_asset_template("unknown", "Example", &out, error, sizeof error));
    REQUIRE(workspace_asset_template("ui-widget", "Inspector", &out, error, sizeof error));
    REQUIRE(!strcmp(out.path, "content/ui/Inspector.rml") && strstr(out.text, "<slot/>"));
    workspace_asset_template_free(&out);
    REQUIRE(workspace_asset_template("ui-document", "Inspector", &out, error, sizeof error));
    REQUIRE(strstr(out.text, "<rml>") && strstr(out.text, "<title>Inspector</title>"));
    workspace_asset_template_free(&out);
    REQUIRE(workspace_asset_template("stylesheet", "Inspector", &out, error, sizeof error));
    REQUIRE(!strcmp(out.path, "content/ui/Inspector.rcss") && strstr(out.text, ".Inspector"));
    workspace_asset_template_free(&out);
    return true;
}

TEST_CASE(workspace_declarative_fields_and_edits_share_revision_history)
{
    workspace_asset_template_result source = { 0 };
    char error[256];
    REQUIRE(workspace_asset_template("scene", "Example", &source, error, sizeof error));
    workspace_document_status status;
    workspace_documents *documents = workspace_documents_new(NULL, &status);
    REQUIRE(status == WORKSPACE_DOCUMENT_OK);
    int handle;
    REQUIRE(workspace_document_memory(documents, source.text, &handle) == WORKSPACE_DOCUMENT_OK);
    workspace_asset_template_free(&source);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    workspace_asset_inspection inspection = { 0 };
    REQUIRE(
        workspace_asset_inspect(documents, handle, view.revision, "scene", &inspection, error, sizeof error)
    );
    bool optional = false, required = false;
    for (int i = 0; i < acount(inspection.fields); i++) {
        const workspace_asset_field *row = &inspection.fields[i];
        if (!strcmp(row->path, "/presentation")) {
            optional = !row->present && !strcmp(row->type, "string");
        }
        if (!strcmp(row->path, "/name")) {
            required = row->required && row->present && !strcmp(row->text, "Example");
        }
    }
    REQUIRE(optional && required && acount(inspection.choices) == 2);
    workspace_asset_inspection_free(&inspection);
    int exact;
    REQUIRE(
        workspace_document_memory(documents, "name \"Exact\"; unsigned 18446744073709551615; signed -9223372036854775808; escaped \"snow\\\"\"", &exact) ==
        WORKSPACE_DOCUMENT_OK
    );
    workspace_document_view exact_view;
    REQUIRE(workspace_document_read(documents, exact, &exact_view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_asset_inspect(documents, exact, exact_view.revision, "scene", &inspection, error, sizeof error)
    );
    bool wide_unsigned = false, wide_signed = false, escaped = false;
    for (int i = 0; i < acount(inspection.fields); i++) {
        const workspace_asset_field *row = &inspection.fields[i];
        if (!strcmp(row->path, "/unsigned")) {
            wide_unsigned = !strcmp(row->json, "18446744073709551615");
        }
        if (!strcmp(row->path, "/signed")) {
            wide_signed = !strcmp(row->json, "-9223372036854775808");
        }
        if (!strcmp(row->path, "/escaped")) {
            escaped = !strcmp(row->json, "\"snow\\\"\"");
        }
        if (!strcmp(row->type, "object") || !row->present) {
            REQUIRE(!row->json[0]);
        }
    }
    REQUIRE(wide_unsigned && wide_signed && escaped);
    workspace_asset_inspection_free(&inspection);
    REQUIRE(workspace_document_close(documents, exact) == WORKSPACE_DOCUMENT_OK);
    int candidate_revision = view.revision;
    char *candidate = NULL;
    REQUIRE(workspace_asset_prepare_registry(
        NULL,
        documents,
        handle,
        candidate_revision,
        "scene",
        "set",
        "/presentation",
        "\"3d\"",
        &candidate,
        error,
        sizeof error
    ));
    REQUIRE(candidate && strstr(candidate, "presentation"));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        view.revision == candidate_revision &&
        !strstr(view.text, "presentation") && !view.can_undo
    );
    REQUIRE(workspace_document_handles(documents, NULL, 0) == 1);
    free(candidate);
    REQUIRE(
        workspace_asset_edit(documents, handle, view.revision, "scene", "set", "/presentation", "\"3d\"", error, sizeof error)
    );
    REQUIRE(
        !workspace_asset_edit(documents, handle, view.revision, "scene", "set", "/name", "\"Stale\"", error, sizeof error)
    );
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    int revision = view.revision;
    REQUIRE(
        !workspace_asset_edit(documents, handle, revision, "scene", "remove", "/name", "", error, sizeof error)
    );
    REQUIRE(
        !workspace_asset_edit(documents, handle, revision, "scene", "set", "/presentation", "\"4d\"", error, sizeof error)
    );
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == revision);
    REQUIRE(workspace_document_undo(documents, handle, revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!strstr(view.text, "presentation"));
    REQUIRE(workspace_asset_edit(
        documents,
        handle,
        view.revision,
        "scene",
        "append",
        "/entities",
        "name \"Cube\"; prototype \"Cube\"",
        error,
        sizeof error
    ));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    revision = view.revision;
    REQUIRE(!workspace_asset_edit(
        documents,
        handle,
        revision,
        "scene",
        "append",
        "/entities",
        "name \"Cube\"; prototype \"Cube\"",
        error,
        sizeof error
    ));
    REQUIRE(
        workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK &&
        view.revision == revision
    );
    workspace_documents_free(documents);
    return true;
}

typedef struct
{
    uint64_t target;
} test_binding;
static const type_field BINDING_FIELDS[] = { { .name = "target",
                                               .type = PIN_HANDLE,
                                               .type_name = "ecs.entity",
                                               .offset = 0,
                                               .size = sizeof(uint64_t) } };
static const type_desc BINDING_TYPE = { .name = "Binding",
                                        .size = sizeof(test_binding),
                                        .fields = BINDING_FIELDS,
                                        .field_count = 1 };
static const type_desc *const BINDING_TYPES[] = { &BINDING_TYPE };
static const type_catalog_pack BINDING_PACK = { .types = BINDING_TYPES,
                                                .type_count = 1 };
TEST_CASE(workspace_scene_edits_preserve_handle_bindings_and_component_defaults)
{
    type_catalog_set_project(&BINDING_PACK);
    project_types = true;
    workspace_asset_template_result source = { 0 };
    char error[256];
    REQUIRE(workspace_asset_template("scene", "Example", &source, error, sizeof error));
    workspace_documents *documents = workspace_documents_new(NULL, NULL);
    int handle;
    REQUIRE(workspace_document_memory(documents, source.text, &handle) == WORKSPACE_DOCUMENT_OK);
    workspace_asset_template_free(&source);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_scene_edit(
        documents,
        handle,
        view.revision,
        WORKSPACE_SCENE_ADD,
        "",
        "",
        "",
        "Group",
        "Cube",
        error,
        sizeof error
    ));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_scene_edit(
        documents,
        handle,
        view.revision,
        WORKSPACE_SCENE_ADD,
        "",
        "/entities/0",
        "",
        "Child",
        "Cube",
        error,
        sizeof error
    ));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_scene_edit(
        documents,
        handle,
        view.revision,
        WORKSPACE_SCENE_ADD,
        "",
        "",
        "",
        "Watcher",
        "Cube",
        error,
        sizeof error
    ));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_asset_edit(
        documents,
        handle,
        view.revision,
        "scene",
        "set",
        "/entities/1/components",
        "Binding { target \"Group.Child\"; }",
        error,
        sizeof error
    ));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    int rename_revision = view.revision;
    char *renamed = NULL;
    REQUIRE(workspace_asset_prepare_registry(
        NULL,
        documents,
        handle,
        rename_revision,
        "scene",
        "set",
        "/entities/0/name",
        "\"NewGroup\"",
        &renamed,
        error,
        sizeof error
    ));
    REQUIRE(renamed && strstr(renamed, "NewGroup.Child"));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        view.revision == rename_revision && strstr(view.text, "Group.Child") &&
        !strstr(view.text, "NewGroup.Child")
    );
    free(renamed);
    REQUIRE(workspace_scene_edit(
        documents,
        handle,
        view.revision,
        WORKSPACE_SCENE_RENAME,
        "/entities/0",
        "",
        "",
        "NewGroup",
        "",
        error,
        sizeof error
    ));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(strstr(view.text, "NewGroup.Child"));
    REQUIRE(workspace_scene_edit(
        documents,
        handle,
        view.revision,
        WORKSPACE_SCENE_MOVE,
        "/entities/0/children/0",
        "",
        "",
        "",
        "",
        error,
        sizeof error
    ));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!strstr(view.text, "NewGroup.Child"));
    REQUIRE(!workspace_scene_edit(
        documents,
        handle,
        view.revision,
        WORKSPACE_SCENE_REMOVE,
        "/entities/2",
        "",
        "",
        "",
        "",
        error,
        sizeof error
    ));
    REQUIRE(workspace_scene_edit(
        documents,
        handle,
        view.revision,
        WORKSPACE_SCENE_DUPLICATE,
        "/entities/2",
        "",
        "",
        "Copy",
        "",
        error,
        sizeof error
    ));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_asset_component(
        documents,
        handle,
        view.revision,
        "scene",
        "/entities/2",
        "Transform",
        true,
        error,
        sizeof error
    ));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(strstr(view.text, "Transform"));
    REQUIRE(workspace_asset_edit(
        documents,
        handle,
        view.revision,
        "scene",
        "set-text",
        "/entities/2/name",
        "Quoted \"Name\"",
        error,
        sizeof error
    ));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_asset_component(
        documents,
        handle,
        view.revision,
        "scene",
        "/entities/2",
        "Transform",
        false,
        error,
        sizeof error
    ));
    workspace_documents_free(documents);
    return true;
}
static const char *object_base_source(const char *name, void *unused)
{
    (void)unused;
    return !strcmp(name, "Base") ? "name \"Base\"; components { "
                                   "AuthoringCharge { level 2; shade 0.42; } }"
                                 : NULL;
}
static const char *behavior_base_source(const char *name, void *unused)
{
    (void)unused;
    return !strcmp(name, "BehaviorBase")
             ? "name \"BehaviorBase\"; (array)behaviors \"Run\""
             : NULL;
}
static bool inspected_behavior(
    const prototype_registry *registry,
    workspace_documents *documents,
    int document,
    int revision,
    const char *entity,
    bool present,
    const char *expected
)
{
    workspace_asset_inspection inspection = { 0 };
    char error[256] = { 0 };
    bool ok = workspace_object_inspect(
        registry,
        documents,
        document,
        revision,
        "scene",
        entity,
        &inspection,
        error,
        sizeof error
    );
    bool array = false, found = false;
    int names = 0;
    for (int i = 0; ok && i < acount(inspection.fields); i++) {
        const workspace_asset_field *field = &inspection.fields[i];
        if (!strcmp(field->relative, "/behaviors")) {
            array = field->present == present;
        }
        if (field->parent >= 0 &&
            !strcmp(inspection.fields[field->parent].relative, "/behaviors")) {
            names++;
            found |= expected && !strcmp(field->text, expected);
        }
    }
    workspace_asset_inspection_free(&inspection);
    return ok && array && (expected ? found && names == 1 : names == 0);
}
TEST_CASE(workspace_objects_behaviors_inherit_and_override_without_source_mutation)
{
    char error[256] = { 0 };
    prototype_registry registry = { .components = REGISTRY_COMPONENTS,
                                    .component_count = REGISTRY_COMPONENT_COUNT,
                                    .include = behavior_base_source };
    object_documents = workspace_documents_new(NULL, NULL);
    int document;
    REQUIRE(
        workspace_document_memory(
            object_documents,
            "version 1; name \"BehaviorScene\"; (array)entities { - name=\"A\" "
            "prototype=\"BehaviorBase\"; - name=\"B\" "
            "prototype=\"BehaviorBase\" { (array)behaviors; }; }",
            &document
        ) == WORKSPACE_DOCUMENT_OK
    );
    workspace_document_view view;
    REQUIRE(workspace_document_read(object_documents, document, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        inspected_behavior(&registry, object_documents, document, view.revision, "/entities/0", false, "Run")
    );
    REQUIRE(
        inspected_behavior(&registry, object_documents, document, view.revision, "/entities/1", true, NULL)
    );
    const char *targets[] = { "/entities/0" };
    bool candidate_ok = workspace_objects_candidate(
        &registry,
        object_documents,
        document,
        view.revision,
        "scene",
        targets,
        1,
        "/behaviors",
        "set",
        "(array)- \"Run\" \"Jump\"",
        "",
        &prototype_candidates[0],
        error,
        sizeof error
    );
    if (!candidate_ok) {
        fprintf(stderr, "Inherited behavior candidate refusal: %s\n", error);
    }
    REQUIRE(candidate_ok);
    REQUIRE(strstr(prototype_candidates[0], "Jump"));
    workspace_document_view unchanged;
    REQUIRE(workspace_document_read(object_documents, document, &unchanged) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        unchanged.revision == view.revision &&
        !strcmp(unchanged.text, view.text) && !unchanged.can_undo
    );
    REQUIRE(workspace_objects_candidate(
        &registry,
        object_documents,
        document,
        view.revision,
        "scene",
        targets,
        1,
        "/behaviors",
        "set",
        "(array)- \"Jump\"",
        prototype_candidates[0],
        &prototype_candidates[1],
        error,
        sizeof error
    ));
    int candidate;
    REQUIRE(
        workspace_document_memory(object_documents, prototype_candidates[1], &candidate) ==
        WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(
        inspected_behavior(&registry, object_documents, candidate, 1, "/entities/0", true, "Jump")
    );
    REQUIRE(
        inspected_behavior(&registry, object_documents, candidate, 1, "/entities/1", true, NULL)
    );
    return true;
}
TEST_CASE(workspace_objects_inherit_custom_fields_and_apply_atomically)
{
    char error[256] = { 0 };
    const declared_field fields[] = {
        { .name = "level", .type = "double", .min = 0, .max = 10 },
        { .name = "shade", .type = "float" }
    };
    const declared_component declaration = { .name = "AuthoringCharge",
                                             .fields = fields,
                                             .field_count = 2 };
    int types_before = type_catalog_count();
    declared_components *custom = declared_components_authoring(&declaration, 1, error, sizeof error);
    object_components = custom;
    REQUIRE(custom);
    REQUIRE(type_catalog_count() == types_before && !type_catalog_find("AuthoringCharge"));
    const ecs_component_desc **descriptors = NULL;
    for (int i = 0; i < REGISTRY_COMPONENT_COUNT; i++) {
        apush(descriptors, REGISTRY_COMPONENTS[i]);
    }
    ecs_component_catalog catalog = declared_components_ecs(custom);
    apush(descriptors, catalog.components[0]);
    object_descriptors = descriptors;
    prototype_registry registry = { .components = descriptors,
                                    .component_count = acount(descriptors),
                                    .include = object_base_source };
    workspace_documents *documents = workspace_documents_new(NULL, NULL);
    object_documents = documents;
    workspace_asset_template_result source = { 0 };
    REQUIRE(workspace_asset_template("scene", "Objects", &source, error, sizeof error));
    int handle;
    REQUIRE(workspace_document_memory(documents, source.text, &handle) == WORKSPACE_DOCUMENT_OK);
    workspace_asset_template_free(&source);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_scene_edit_registry(
        &registry,
        documents,
        handle,
        view.revision,
        WORKSPACE_SCENE_ADD,
        "",
        "",
        "",
        "A",
        "Base",
        error,
        sizeof error
    ));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_scene_edit_registry(
        &registry,
        documents,
        handle,
        view.revision,
        WORKSPACE_SCENE_ADD,
        "",
        "",
        "",
        "B",
        "Base",
        error,
        sizeof error
    ));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    const char *targets[] = { "/entities/0", "/entities/1" };
    workspace_asset_inspection inspection = { 0 };
    REQUIRE(workspace_objects_inspect(
        &registry,
        documents,
        handle,
        view.revision,
        "scene",
        targets,
        2,
        &inspection,
        error,
        sizeof error
    ));
    bool inherited = false, quantized = false;
    pin_value live_float = { .type = PIN_F32, .f = .42f };
    char *live_json = NULL;
    REQUIRE(value_to_json(&live_float, &live_json, error, sizeof error));
    for (int i = 0; i < acount(inspection.fields); i++) {
        workspace_asset_field *row = &inspection.fields[i];
        if (!strcmp(row->relative, "/components/AuthoringCharge/shade")) {
            fprintf(
                stderr,
                "inherited float present=%d JSON=%s live JSON=%s number=%.17g "
                "native=%.17g\n",
                row->present,
                row->json,
                live_json,
                row->number,
                (double).42f
            );
            quantized = !row->present && !strcmp(row->json, live_json) &&
                        (float)row->number == live_float.f;
        }
        if (!strcmp(row->relative, "/components/AuthoringCharge/level")) {
            fprintf(stderr, "resolved inherited double JSON: %s\n", row->json);
            av_doc *json = av_json_read(row->json);
            inherited = row->common && !row->mixed && !row->present &&
                        row->number == 2 && json && av_is_num(av_doc_root(json)) &&
                        av_get_num(av_doc_root(json)) == 2;
            av_doc_free(json);
        }
    }
    free(live_json);
    REQUIRE(inherited && quantized);
    workspace_asset_inspection_free(&inspection);
    int original_revision = view.revision;
    bool applied = workspace_objects_edit(
        &registry,
        documents,
        handle,
        original_revision,
        "scene",
        targets,
        2,
        "/components/AuthoringCharge/level",
        "set-number",
        "7",
        error,
        sizeof error
    );
    if (!applied) {
        fprintf(stderr, "workspace objects edit refusal: %s\n", error);
    }
    REQUIRE(applied);
    REQUIRE(
        workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK &&
        view.revision == original_revision + 1
    );
    REQUIRE(workspace_document_undo(documents, handle, view.revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK &&
        view.can_redo && !strstr(view.text, "AuthoringCharge")
    );
    const char *bad_targets[] = { "/entities/0", "/entities/99" };
    int unchanged_revision = view.revision;
    REQUIRE(!workspace_objects_edit(
        &registry,
        documents,
        handle,
        view.revision,
        "scene",
        bad_targets,
        2,
        "/components/AuthoringCharge/level",
        "set-number",
        "8",
        error,
        sizeof error
    ));
    REQUIRE(
        workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK &&
        view.revision == unchanged_revision && view.can_redo
    );
    workspace_documents_free(documents);
    object_documents = NULL;
    afree(descriptors);
    object_descriptors = NULL;
    declared_components_free(custom);
    object_components = NULL;
    REQUIRE(type_catalog_count() == types_before);
    return true;
}
TEST_CASE(workspace_prototype_children_use_revisioned_hierarchy_operations)
{
    char error[256] = { 0 };
    workspace_documents *documents = workspace_documents_new(NULL, NULL);
    object_documents = documents;
    int handle;
    REQUIRE(workspace_document_memory(documents, "name \"Parent\"", &handle) == WORKSPACE_DOCUMENT_OK);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_scene_edit(
        documents,
        handle,
        view.revision,
        WORKSPACE_SCENE_ADD,
        "",
        "",
        "",
        "Child",
        "Base",
        error,
        sizeof error
    ));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_scene_edit(
        documents,
        handle,
        view.revision,
        WORKSPACE_SCENE_RENAME,
        "/children/0",
        "",
        "",
        "Renamed",
        "",
        error,
        sizeof error
    ));
    REQUIRE(
        workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK &&
        strstr(view.text, "Renamed")
    );
    REQUIRE(workspace_document_undo(documents, handle, view.revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK &&
        strstr(view.text, "Child")
    );
    workspace_documents_free(documents);
    object_documents = NULL;
    return true;
}
#if !defined(_WIN32) && defined(O_NOFOLLOW)
TEST_CASE(workspace_property_metadata_uses_overlay_policy_for_scene_and_prototype)
{
    snprintf(scope_root, sizeof scope_root, "/tmp/chibi-property-policy-XXXXXX");
    REQUIRE(mkdtemp(scope_root));
    char directory[2048];
    snprintf(directory, sizeof directory, "%s/editor", scope_root);
    REQUIRE(!mkdir(directory, 0700));
    object_documents = workspace_documents_new(scope_root, NULL);
    REQUIRE(object_documents);
    char error[256] = "";
    int policy;
    REQUIRE(
        workspace_document_create(
            object_documents,
            "editor/Components.kdl",
            "components { Transform tab=\"Physics\" icon=\"shape\" { fields { "
            "x unit=\"meters\" { (array)range -10 10; } } } }",
            &policy
        ) == WORKSPACE_DOCUMENT_OK
    );
    int project;
    REQUIRE(
        workspace_document_create(object_documents, "project.kdl", "version 25\nname \"Fixture\"\ncontent \"content\"\nui \"ProjectUi\"", &project) ==
        WORKSPACE_DOCUMENT_OK
    );
    int settings;
    REQUIRE(
        workspace_document_memory(object_documents, "version 1\nname \"Settings\"\n(array)entities {}", &settings) ==
        WORKSPACE_DOCUMENT_OK
    );
    workspace_asset_inspection effective = { 0 };
    REQUIRE(
        workspace_object_inspect(NULL, object_documents, settings, 1, "scene", "", &effective, error, sizeof error)
    );
    bool substeps = false, sleep = false, project_ui = false;
    physics_settings defaults = physics_defaults();
    for (int i = 0; i < acount(effective.fields); i++) {
        const workspace_asset_field *row = &effective.fields[i];
        if (!strcmp(row->path, "/physics/substeps")) {
            REQUIRE(!row->present && row->number == defaults.substeps);
            substeps = true;
        }
        if (!strcmp(row->path, "/physics/sleep")) {
            REQUIRE(!row->present && row->boolean == defaults.sleep);
            sleep = true;
        }
        if (!strcmp(row->path, "/ui")) {
            REQUIRE(!row->present && !strcmp(row->text, "ProjectUi"));
            project_ui = true;
        }
    }
    REQUIRE(substeps && sleep && project_ui);
    workspace_asset_inspection_free(&effective);
    const char *kinds[] = { "prototype", "scene" };
    const char *sources[] = { "name \"Prop\"\ncomponents { Transform x=2 y=3 }", "version 1\nname \"Scene\"\n(array)entities { - name=\"Actor\" { components { Transform x=4 y=5 } } }" };
    for (int i = 0; i < 2; i++) {
        int handle;
        REQUIRE(
            workspace_document_memory(object_documents, sources[i], &handle) == WORKSPACE_DOCUMENT_OK
        );
        workspace_document_view view;
        REQUIRE(workspace_document_read(object_documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
        workspace_asset_inspection result = { 0 };
        REQUIRE(
            workspace_asset_inspect(object_documents, handle, view.revision, kinds[i], &result, error, sizeof error)
        );
        bool found = false;
        for (int f = 0; f < acount(result.fields); f++) {
            const workspace_asset_field *row = &result.fields[f];
            if (strcmp(row->name, "x") || !*row->component) {
                continue;
            }
            REQUIRE(
                !strcmp(row->presentation_category, "physics") &&
                !strcmp(row->icon, "shape")
            );
            REQUIRE(
                !strcmp(row->unit, "meters") && row->bounded &&
                row->min == -10 && row->max == 10
            );
            found = true;
        }
        REQUIRE(found);
        workspace_asset_inspection_free(&result);
        REQUIRE(workspace_document_close(object_documents, handle) == WORKSPACE_DOCUMENT_OK);
    }
    workspace_document_view view;
    REQUIRE(workspace_document_read(object_documents, policy, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_edit(
            object_documents,
            policy,
            view.revision,
            0,
            (int)view.bytes,
            "components { Transform tab=\"Gameplay\" }"
        ) == WORKSPACE_DOCUMENT_OK
    );
    component_presentation presentation = { 0 };
    REQUIRE(workspace_presentation_load(
        object_documents,
        (ecs_component_catalog){ REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
        &presentation,
        error,
        sizeof error
    ));
    const ecs_component_desc *transform = ecs_component_catalog_find(
        (ecs_component_catalog){ REGISTRY_COMPONENTS, REGISTRY_COMPONENT_COUNT },
        "Transform"
    );
    REQUIRE(transform && component_presentation_tab(&presentation, transform) == COMPONENT_TAB_GAMEPLAY);
    component_presentation_free(&presentation);
    return true;
}
TEST_CASE(workspace_asset_scope_opens_invalid_source_and_recovers_from_overlay)
{
    snprintf(scope_root, sizeof scope_root, "%s/asset-scope-XXXXXX", tool_root());
    REQUIRE(mkdtemp(scope_root));
    char path[1200];
    snprintf(path, sizeof path, "%s/content", scope_root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/components", scope_root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/components/Editable.chibi", scope_root);
    tool_write(path, "component Editable { value: }");
    object_documents = workspace_documents_new(scope_root, NULL);
    REQUIRE(object_documents);
    const script_catalog native = { 0 };
    char error[256] = { 0 };
    int types_before = type_catalog_count();
    scope_service = workspace_assets_service_new(object_documents, scope_root, &native, error, sizeof error);
    REQUIRE(scope_service && !*error);
    REQUIRE(!workspace_assets_service_sync(scope_service, error, sizeof error) && *error);
    REQUIRE(!workspace_assets_service_component_catalog(scope_service));
    int handle;
    REQUIRE(
        workspace_document_open(object_documents, "content/components/Editable.chibi", &handle) ==
        WORKSPACE_DOCUMENT_OK
    );
    workspace_document_view view;
    REQUIRE(workspace_document_read(object_documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_edit(
            object_documents,
            handle,
            view.revision,
            0,
            (int)view.bytes,
            "component Editable { value: double }"
        ) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(workspace_assets_service_sync(scope_service, error, sizeof error));
    const script_catalog *catalog = workspace_assets_service_component_catalog(scope_service);
    REQUIRE(catalog && script_catalog_find_type(catalog, "Editable") >= 0);
    REQUIRE(type_catalog_count() == types_before && !type_catalog_find("Editable"));
    REQUIRE(workspace_document_close(object_documents, handle) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!workspace_assets_service_sync(scope_service, error, sizeof error) && *error);
    REQUIRE(type_catalog_count() == types_before);
    return true;
}
#endif
static const char *candidate_base(const char *name, void *)
{
    return !strcmp(name, "Base")
             ? "name \"Base\"\ncomponents { Transform x=1 y=2 }\n"
             : NULL;
}
TEST_CASE(workspace_object_metadata_distinguishes_inherited_overridden_and_added)
{
    object_documents = workspace_documents_new(NULL, NULL);
    int handle;
    char error[256] = "";
    REQUIRE(
        workspace_document_memory(
            object_documents,
            "version 1\nname \"Scene\"\n(array)entities { - name=\"A\" "
            "prototype=\"Base\" { components { Transform x=7 } }; - name=\"B\" "
            "prototype=\"Base\"; }",
            &handle
        ) == WORKSPACE_DOCUMENT_OK
    );
    prototype_registry registry = { .components = REGISTRY_COMPONENTS,
                                    .component_count = REGISTRY_COMPONENT_COUNT,
                                    .include = candidate_base };
    for (int i = 0; i < 2; i++) {
        const char *entity = i ? "/entities/1" : "/entities/0";
        workspace_asset_inspection inspection = { 0 };
        REQUIRE(
            workspace_object_inspect(&registry, object_documents, handle, 1, "scene", entity, &inspection, error, sizeof error)
        );
        bool found = false;
        for (int f = 0; f < acount(inspection.fields); f++) {
            const workspace_asset_field *row = &inspection.fields[f];
            if (!strcmp(row->relative, "/components/Transform/x")) {
                REQUIRE(!row->added && !row->computed && row->overridden == !i);
                found = true;
            }
        }
        REQUIRE(found);
        workspace_asset_inspection_free(&inspection);
    }
    REQUIRE(workspace_asset_component_registry(
        &registry,
        object_documents,
        handle,
        1,
        "scene",
        "/entities/0",
        "Orientation",
        true,
        error,
        sizeof error
    ));
    workspace_document_view view;
    REQUIRE(workspace_document_read(object_documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    workspace_asset_inspection inspection = { 0 };
    REQUIRE(workspace_object_inspect(
        &registry,
        object_documents,
        handle,
        view.revision,
        "scene",
        "/entities/0",
        &inspection,
        error,
        sizeof error
    ));
    bool found = false;
    for (int f = 0; f < acount(inspection.fields); f++) {
        const workspace_asset_field *row = &inspection.fields[f];
        if (!strcmp(row->name, "Orientation")) {
            REQUIRE(row->added && !row->computed && !row->overridden);
            found = true;
        }
    }
    REQUIRE(found);
    workspace_asset_inspection_free(&inspection);
    return true;
}
TEST_CASE(workspace_prototype_candidates_preserve_source_until_grouped_adoption)
{
    char error[256] = { 0 };
    object_documents = workspace_documents_new(NULL, NULL);
    int scene, base;
    REQUIRE(
        workspace_document_memory(
            object_documents,
            "version 1\nname \"Scene\"\n(array)entities { - name=\"A\" "
            "prototype=\"Base\" "
            "{ components { Transform x=7 } } }\n",
            &scene
        ) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(
        workspace_document_memory(object_documents, candidate_base("Base", NULL), &base) ==
        WORKSPACE_DOCUMENT_OK
    );
    prototype_registry registry = { .components = REGISTRY_COMPONENTS,
                                    .component_count = REGISTRY_COMPONENT_COUNT,
                                    .include = candidate_base };
    const char *paths[] = { "/components/Transform/x" };
    bool candidate_ok = workspace_prototype_candidate(
        &registry,
        object_documents,
        scene,
        1,
        "scene",
        "/entities/0",
        base,
        1,
        paths,
        1,
        "apply",
        "",
        &prototype_candidates[0],
        &prototype_candidates[1],
        &prototype_candidates[2],
        error,
        sizeof error
    );
    if (!candidate_ok) {
        fprintf(stderr, "prototype candidate refusal: %s\n", error);
    }
    REQUIRE(candidate_ok);
    workspace_document_view a, b;
    REQUIRE(
        workspace_document_read(object_documents, scene, &a) == WORKSPACE_DOCUMENT_OK &&
        a.revision == 1 && !a.can_undo
    );
    REQUIRE(
        workspace_document_read(object_documents, base, &b) == WORKSPACE_DOCUMENT_OK &&
        b.revision == 1 && !b.can_undo
    );
    av_doc *parsed = av_doc_read(prototype_candidates[1], error, sizeof error);
    REQUIRE(parsed);
    REQUIRE(
        av_get_num(
            av_obj_get(av_obj_get(av_obj_get(av_doc_root(parsed), "components"), "Transform"), "x")
        ) == 7
    );
    av_doc_free(parsed);
    workspace_document_replacement replacements[] = {
        { scene, 1, prototype_candidates[0] },
        { base, 1, prototype_candidates[1] }
    };
    REQUIRE(workspace_document_replace_many(object_documents, replacements, 2) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(object_documents, scene, &a) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_undo(object_documents, scene, a.revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_read(object_documents, scene, &a) == WORKSPACE_DOCUMENT_OK &&
        a.can_redo && strstr(a.text, "x=7")
    );
    REQUIRE(
        workspace_document_read(object_documents, base, &b) == WORKSPACE_DOCUMENT_OK &&
        b.can_redo && strstr(b.text, "x=1")
    );
    for (int i = 0; i < 3; i++) {
        free(prototype_candidates[i]);
        prototype_candidates[i] = NULL;
    }
    REQUIRE(!workspace_prototype_candidate(
        &registry,
        object_documents,
        scene,
        1,
        "scene",
        "/entities/0",
        base,
        b.revision,
        paths,
        1,
        "apply",
        "",
        &prototype_candidates[0],
        &prototype_candidates[1],
        &prototype_candidates[2],
        error,
        sizeof error
    ));
    REQUIRE(workspace_prototype_candidate(
        &registry,
        object_documents,
        scene,
        a.revision,
        "scene",
        "/entities/0",
        0,
        0,
        NULL,
        0,
        "save-as",
        "Tuned",
        &prototype_candidates[0],
        &prototype_candidates[1],
        &prototype_candidates[2],
        error,
        sizeof error
    ));
    REQUIRE(!strcmp(prototype_candidates[2], "content/prototypes/Tuned.kdl"));
    REQUIRE(
        strstr(prototype_candidates[0], "Tuned") && strstr(prototype_candidates[1], "Base")
    );
    REQUIRE(
        workspace_document_read(object_documents, scene, &a) == WORKSPACE_DOCUMENT_OK &&
        a.can_redo
    );
    return true;
}

TEST_CASE(workspace_scene_clipboard_remaps_roots_without_mutating_source)
{
    type_catalog_set_project(&BINDING_PACK);
    project_types = true;
    object_documents = workspace_documents_new(NULL, NULL);
    int scene;
    const char *source = "version 1\nname \"Clipboard\"\n(array)entities { "
                         "- name=\"A\" prototype=\"Cube\" { components { "
                         "Binding target=\"A2\"; }; (array)children { - "
                         "name=\"T\" prototype=\"Cube\"; }; }; "
                         "- name=\"A2\" prototype=\"Cube\" { components { "
                         "Binding target=\"A.T\"; }; }; }\n";
    REQUIRE(workspace_document_memory(object_documents, source, &scene) == WORKSPACE_DOCUMENT_OK);
    prototype_registry registry = { .components = REGISTRY_COMPONENTS,
                                    .component_count = REGISTRY_COMPONENT_COUNT };
    const char *selected[] = { "/entities/0", "/entities/0/children/0", "/entities/1" };
    char error[256] = { 0 };
    REQUIRE(workspace_scene_clipboard(
        &registry,
        object_documents,
        scene,
        1,
        selected,
        3,
        false,
        &prototype_candidates[0],
        &prototype_candidates[1],
        error,
        sizeof error
    ));
    REQUIRE(!strcmp(prototype_candidates[1], source));
    REQUIRE(workspace_scene_paste(
        &registry,
        object_documents,
        scene,
        1,
        "",
        prototype_candidates[0],
        &prototype_candidates[2],
        &clipboard_paths,
        &clipboard_path_count,
        error,
        sizeof error
    ));
    REQUIRE(
        clipboard_path_count == 2 && !strcmp(clipboard_paths[0], "/entities/2") &&
        !strcmp(clipboard_paths[1], "/entities/3")
    );
    av_doc *parsed = av_doc_read(prototype_candidates[2], error, sizeof error);
    REQUIRE(parsed);
    av_value *roots = av_obj_get(av_doc_root(parsed), "entities");
    REQUIRE(av_arr_size(roots) == 4);
    av_value *a = av_arr_get(roots, 2), *b = av_arr_get(roots, 3);
    bool remapped = !strcmp(av_get_str(av_obj_get(a, "name")), "A3") &&
                    !strcmp(av_get_str(av_obj_get(b, "name")), "A22") &&
                    !strcmp(
                        av_get_str(
                            av_obj_get(av_obj_get(av_obj_get(a, "components"), "Binding"), "target")
                        ),
                        "A22"
                    ) &&
                    !strcmp(
                        av_get_str(
                            av_obj_get(av_obj_get(av_obj_get(b, "components"), "Binding"), "target")
                        ),
                        "A3.T"
                    );
    av_doc_free(parsed);
    REQUIRE(remapped);
    workspace_document_view view;
    REQUIRE(
        workspace_document_read(object_documents, scene, &view) == WORKSPACE_DOCUMENT_OK &&
        view.revision == 1 && !view.can_undo && !strcmp(view.text, source)
    );
    char *invalid_text = NULL, **invalid_paths = NULL;
    int invalid_count = 0;
    bool accepted_invalid = workspace_scene_paste(
        &registry,
        object_documents,
        scene,
        1,
        "",
        "format \"chibi-scene-clipboard-1\"\n(array)objects { - "
        "source=\"Other\" { object name=\"A\" prototype=\"Cube\"; }; }\n",
        &invalid_text,
        &invalid_paths,
        &invalid_count,
        error,
        sizeof error
    );
    for (int i = 0; i < invalid_count; i++) {
        free(invalid_paths[i]);
    }
    free(invalid_paths);
    free(invalid_text);
    REQUIRE(!accepted_invalid && strstr(error, "source name"));
    char *payload = NULL, *text = NULL;
    REQUIRE(
        !workspace_scene_clipboard(&registry, object_documents, scene, 1, selected, 1, true, &payload, &text, error, sizeof error)
    );
    REQUIRE(!payload && !text && strstr(error, "referenced"));
    REQUIRE(
        workspace_scene_clipboard(&registry, object_documents, scene, 1, selected, 3, true, &payload, &text, error, sizeof error)
    );
    parsed = av_doc_read(text, error, sizeof error);
    bool empty = parsed &&
                 av_arr_size(av_obj_get(av_doc_root(parsed), "entities")) == 0;
    av_doc_free(parsed);
    free(payload);
    free(text);
    REQUIRE(empty);
    REQUIRE(
        workspace_document_edit(object_documents, scene, 1, 0, (int)view.bytes, prototype_candidates[2]) ==
        WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(workspace_document_undo(object_documents, scene, 2) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_read(object_documents, scene, &view) == WORKSPACE_DOCUMENT_OK &&
        !strcmp(view.text, source)
    );
    return true;
}
static const char *placement_base(const char *name, void *)
{
    if (!strcmp(name, "Scaled")) {
        return "name \"Scaled\"\ncomponents { Transform3D { position x=9 y=8 "
               "z=7; rotation x=0 y=0 z=0 w=1; scale x=3 y=4 z=5; }; }\n";
    }
    return !strcmp(name, "Empty") ? "name \"Empty\"\n" : NULL;
}
TEST_CASE(workspace_scene_placement_preserves_inherited_transform_and_private_draft)
{
    object_documents = workspace_documents_new(NULL, NULL);
    int document;
    const char *source = "version 1\nname \"Placement\"\n(array)entities { - "
                         "name=\"Parent\" prototype=\"Empty\"; }\n";
    REQUIRE(workspace_document_memory(object_documents, source, &document) == WORKSPACE_DOCUMENT_OK);
    prototype_registry registry = { .components = REGISTRY_COMPONENTS,
                                    .component_count = REGISTRY_COMPONENT_COUNT,
                                    .include = placement_base };
    char error[256] = { 0 };
    REQUIRE(workspace_scene_place_candidate(
        &registry,
        object_documents,
        document,
        1,
        "/entities/0",
        "Scaled",
        "Chair",
        2,
        3,
        4,
        "",
        &prototype_candidates[0],
        &clipboard_paths,
        &clipboard_path_count,
        error,
        sizeof error
    ));
    REQUIRE(clipboard_path_count == 1 && !strcmp(clipboard_paths[0], "/entities/0/children/0"));
    int draft;
    REQUIRE(
        workspace_document_memory(object_documents, prototype_candidates[0], &draft) == WORKSPACE_DOCUMENT_OK
    );
    workspace_asset_inspection fields = { 0 };
    REQUIRE(workspace_object_inspect(
        &registry,
        object_documents,
        draft,
        1,
        "scene",
        clipboard_paths[0],
        &fields,
        error,
        sizeof error
    ));
    bool positioned = false, scaled = false, rotated = false;
    for (int i = 0; i < acount(fields.fields); i++) {
        workspace_asset_field *field = &fields.fields[i];
        if (!strcmp(field->relative, "/components/Transform3D/position/x")) {
            positioned = field->number == 2;
        }
        if (!strcmp(field->relative, "/components/Transform3D/scale/x")) {
            scaled = field->number == 3 && !field->present;
        }
        if (!strcmp(field->relative, "/components/Transform3D/rotation/w")) {
            rotated = field->number == 1 && !field->present;
        }
    }
    workspace_asset_inspection_free(&fields);
    REQUIRE(positioned && scaled && rotated);
    free(clipboard_paths[0]);
    free(clipboard_paths);
    clipboard_paths = NULL;
    clipboard_path_count = 0;
    REQUIRE(workspace_scene_place_candidate(
        &registry,
        object_documents,
        document,
        1,
        "",
        "Empty",
        "Other",
        1,
        2,
        3,
        prototype_candidates[0],
        &prototype_candidates[1],
        &clipboard_paths,
        &clipboard_path_count,
        error,
        sizeof error
    ));
    REQUIRE(clipboard_path_count == 1 && !strcmp(clipboard_paths[0], "/entities/1"));
    int second;
    REQUIRE(
        workspace_document_memory(object_documents, prototype_candidates[1], &second) == WORKSPACE_DOCUMENT_OK
    );
    bool inspected = workspace_object_inspect(
        &registry,
        object_documents,
        second,
        1,
        "scene",
        clipboard_paths[0],
        &fields,
        error,
        sizeof error
    );
    if (!inspected) {
        fprintf(stderr, "placement second inspection (%s): %s\n", clipboard_paths[0], error);
    }
    REQUIRE(inspected);
    bool unit_scale = false;
    for (int i = 0; i < acount(fields.fields); i++) {
        if (!strcmp(fields.fields[i].relative, "/components/Transform3D/scale/x")) {
            unit_scale = fields.fields[i].number == 1 && fields.fields[i].present;
        }
    }
    workspace_asset_inspection_free(&fields);
    REQUIRE(unit_scale);
    workspace_document_view current;
    REQUIRE(
        workspace_document_read(object_documents, document, &current) == WORKSPACE_DOCUMENT_OK &&
        current.revision == 1 && !current.can_undo && !strcmp(current.text, source)
    );
    REQUIRE(workspace_asset_component_registry(
        &registry,
        object_documents,
        document,
        1,
        "scene",
        "/entities/0",
        "Transform3D",
        true,
        error,
        sizeof error
    ));
    bool added_inspected = workspace_object_inspect(
        &registry,
        object_documents,
        document,
        2,
        "scene",
        "/entities/0",
        &fields,
        error,
        sizeof error
    );
    if (!added_inspected) {
        fprintf(stderr, "added Transform3D inspection: %s\n", error);
    }
    REQUIRE(added_inspected);
    bool added_position = false;
    for (int i = 0; i < acount(fields.fields); i++) {
        if (!strcmp(fields.fields[i].relative, "/components/Transform3D/position/x")) {
            added_position = fields.fields[i].number == 0 && fields.fields[i].present;
        }
    }
    workspace_asset_inspection_free(&fields);
    REQUIRE(added_position);
    return true;
}
TEST_CASE(workspace_asset_bindings_refuse_absent_project_without_effects)
{
    behavior_function_context context = { .value_storage = &inactive_storage };
    int tested = 0;
    for (int binding = 0; binding < WORKSPACE_ASSETS_DEFINITION_PACK.binding_count;
         binding++) {
        const native_definition_binding *bound = &WORKSPACE_ASSETS_DEFINITION_PACK
                                                      .bindings[binding];
        const node_host_call *call = bound->implementation.call;
        if (!call || !call->service) {
            continue;
        }
        REQUIRE(
            !strcmp(call->service, "workspace_assets") &&
            call->optional_service && call->context
        );
        const native_capability_definition *definition = NULL;
        for (int i = 0; i < WORKSPACE_ASSETS_DEFINITION_PACK.definition_count; i++) {
            if (!strcmp(
                    bound->canonical_name,
                    WORKSPACE_ASSETS_DEFINITION_PACK.definitions[i].canonical_name
                )) {
                definition = &WORKSPACE_ASSETS_DEFINITION_PACK.definitions[i];
                break;
            }
        }
        REQUIRE(definition);
        pin_value inputs[16] = { 0 }, outputs[16] = { 0 };
        int input_count = 0, output_count = 0;
        for (int i = 0; i < definition->input_count; i++) {
            const native_parameter_definition *port = &definition->inputs[i];
            if (port->kind == NATIVE_PARAMETER_EXEC) {
                continue;
            }
            REQUIRE(input_count < 16);
            inputs[input_count] = (pin_value){ .type = port->storage,
                                               .type_name = port->type_name,
                                               .container = port->container };
            if (port->storage == PIN_STR && port->container == PIN_CONTAINER_SINGLE) {
                inputs[input_count].str = "";
            }
            input_count++;
        }
        for (int i = 0; i < definition->output_count; i++) {
            const native_parameter_definition *port = &definition->outputs[i];
            if (port->kind == NATIVE_PARAMETER_EXEC) {
                continue;
            }
            REQUIRE(output_count < 16);
            outputs[output_count++] = (pin_value){ .type = port->storage,
                                                   .type_name = port->type_name,
                                                   .container = port->container };
        }
        REQUIRE(!call->context(&context, NULL, inputs, outputs));
        int at = 0;
        bool refused = false, explained = false;
        for (int i = 0; i < definition->output_count; i++) {
            const native_parameter_definition *port = &definition->outputs[i];
            if (port->kind == NATIVE_PARAMETER_EXEC) {
                continue;
            }
            pin_value *value = &outputs[at++];
            if (!strcmp(port->name, "Ok")) {
                refused = !value->b;
            }
            if (!strcmp(port->name, "Error")) {
                explained = value->str && strstr(value->str, "unavailable");
            }
            if (port->container == PIN_CONTAINER_ARRAY) {
                const pin_array *array = value->ptr;
                REQUIRE(array && array->count == 0);
            }
        }
        if (!refused || !explained) {
            fprintf(stderr, "inactive asset callback: %s\n", bound->canonical_name);
        }
        REQUIRE(refused && explained);
        tested++;
    }
    REQUIRE(tested == 21);
    return true;
}
void test_workspace_assets()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(workspace_asset_bindings_refuse_absent_project_without_effects);
    RUN_TEST_CASE(workspace_templates_cover_canonical_kinds_without_disk_mutation);
    RUN_TEST_CASE(workspace_templates_refuse_unsafe_names_and_offer_authored_ui);
    RUN_TEST_CASE(workspace_declarative_fields_and_edits_share_revision_history);
    RUN_TEST_CASE(workspace_scene_edits_preserve_handle_bindings_and_component_defaults);
    RUN_TEST_CASE(workspace_objects_inherit_custom_fields_and_apply_atomically);
    RUN_TEST_CASE(workspace_objects_behaviors_inherit_and_override_without_source_mutation);
    RUN_TEST_CASE(workspace_prototype_children_use_revisioned_hierarchy_operations);
    RUN_TEST_CASE(workspace_object_metadata_distinguishes_inherited_overridden_and_added);
    RUN_TEST_CASE(workspace_prototype_candidates_preserve_source_until_grouped_adoption);
    RUN_TEST_CASE(workspace_scene_clipboard_remaps_roots_without_mutating_source);
    RUN_TEST_CASE(workspace_scene_placement_preserves_inherited_transform_and_private_draft);
#if !defined(_WIN32) && defined(O_NOFOLLOW)
    RUN_TEST_CASE(workspace_property_metadata_uses_overlay_policy_for_scene_and_prototype);
    RUN_TEST_CASE(workspace_asset_scope_opens_invalid_source_and_recovers_from_overlay);
#endif
    pu_clear_setup();
}
