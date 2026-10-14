#include "authoring/project_semantics.h"

#include "assets/ui_document.h"
#include "authoring/document_semantics.h"
#include "authoring/ui_binding_navigation.h"
#include "authoring/workspace_assets_scope.h"
#include "graph/document/serialize.h"
#include "host/native_catalog.h"
#include "runtime/generated_capabilities.h"
#include "runtime/ui_definition.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static workspace_documents *documents;
static project_semantics *service;
static workspace_assets_service *assets;
static arena storage;
static char root[128], error[256];
static int math_document;
static script_catalog native;
static native_capabilities *ui_capabilities;
static const char
    *math_text = "library Math { Twice :: pure fn(value:int)->(result:int) { "
                 "return value * 2; } Clash :: pure fn()->(result:int) { "
                 "return 0; } }";
static const char
    *other_text = "library Other { Use :: pure fn(value:int)->(result:int) { "
                  "return Math.Twice(value); } }";
static void cleanup(void)
{
    project_semantics_free(service);
    service = NULL;
    workspace_assets_service_free(assets);
    assets = NULL;
    native = (script_catalog){ 0 };
    native_capabilities_free(ui_capabilities);
    ui_capabilities = NULL;
    workspace_documents_free(documents);
    documents = NULL;
    arena_free(&storage);
    if (*root) {
        tool_remove_tree(root);
        *root = 0;
    }
}
static workspace_document_view view(int handle)
{
    workspace_document_view out;
    assert(workspace_document_read(documents, handle, &out) == WORKSPACE_DOCUMENT_OK);
    return out;
}
static bool setup_project(void)
{
    strcpy(root, "/tmp/chibi-project-semantics-XXXXXX");
    REQUIRE(mkdtemp(root));
    char path[256];
    snprintf(path, sizeof path, "%s/content", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/libraries", root);
    REQUIRE(!mkdir(path, 0700));
    workspace_document_status status;
    documents = workspace_documents_new(root, &status);
    REQUIRE(status == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_create(documents, "content/libraries/Math.chibi", math_text, &math_document) ==
        WORKSPACE_DOCUMENT_OK
    );
    int other;
    REQUIRE(
        workspace_document_create(documents, "content/libraries/Other.chibi", other_text, &other) ==
        WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(workspace_document_close(documents, other) == WORKSPACE_DOCUMENT_OK);
    service = project_semantics_new(documents, root, &native);
    REQUIRE(service);
    return true;
}
TEST_CASE(project_semantics_calls_and_references_resolve_closed_sources_and_own_rows)
{
    REQUIRE(setup_project());
    int offset = (int)(strstr(math_text, "Twice") - math_text), count, total;
    document_call *calls;
    REQUIRE(
        project_semantics_calls(service, math_document, 1, offset, 0, 0, 10, &storage, &calls, &count, &total, error, sizeof error)
    );
    REQUIRE(count == 1 && total == 1);
    REQUIRE(!strcmp(calls[0].path, "content/libraries/Other.chibi"));
    REQUIRE(!strcmp(calls[0].target, "content/libraries/Other.chibi"));
    REQUIRE(strstr(calls[0].label, "Use"));
    project_reference *references;
    REQUIRE(project_semantics_references(
        service,
        math_document,
        1,
        offset,
        0,
        10,
        &storage,
        &references,
        &count,
        &total,
        error,
        sizeof error
    ));
    REQUIRE(count >= 2 && total == count);
    REQUIRE(project_semantics_references(
        service,
        math_document,
        1,
        offset,
        1,
        1,
        &storage,
        &references,
        &count,
        &total,
        error,
        sizeof error
    ));
    REQUIRE(count == 1 && total >= 2);
    const char *owned = references[0].name;
    project_semantics_free(service);
    service = NULL;
    REQUIRE(strstr(owned, "Twice"));
    return true;
}
TEST_CASE(project_semantics_rename_adopts_complete_candidates_as_one_shared_undo)
{
    REQUIRE(setup_project());
    int offset = (int)(strstr(math_text, "Twice") - math_text), changed[64], count;
    REQUIRE(
        project_semantics_rename(service, math_document, 1, offset, "Double", changed, &count, error, sizeof error)
    );
    REQUIRE(count == 2);
    int other = changed[0] == math_document ? changed[1] : changed[0];
    REQUIRE(
        strstr(view(math_document).text, "Double") &&
        !strstr(view(math_document).text, "Twice")
    );
    REQUIRE(strstr(view(other).text, "Math.Double"));
    REQUIRE(view(math_document).dirty && view(other).dirty);
    char path[256], bytes[512];
    snprintf(path, sizeof path, "%s/content/libraries/Math.chibi", root);
    FILE *file = fopen(path, "rb");
    REQUIRE(file);
    size_t n = fread(bytes, 1, sizeof bytes - 1, file);
    bytes[n] = 0;
    fclose(file);
    REQUIRE(!strcmp(bytes, math_text));
    REQUIRE(
        workspace_document_undo(documents, math_document, view(math_document).revision) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(!strcmp(view(math_document).text, math_text) && !strcmp(view(other).text, other_text));
    REQUIRE(workspace_document_redo(documents, other, view(other).revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(strstr(view(math_document).text, "Double") && strstr(view(other).text, "Math.Double"));
    return true;
}
TEST_CASE(project_semantics_rename_refusals_preserve_drafts_and_open_membership)
{
    REQUIRE(setup_project());
    int offset = (int)(strstr(math_text, "Twice") - math_text), changed[64], count;
    REQUIRE(
        !project_semantics_rename(service, math_document, 1, offset, "Clash", changed, &count, error, sizeof error)
    );
    REQUIRE(
        count == 0 && view(math_document).revision == 1 && !view(math_document).can_undo
    );
    int handles[64];
    REQUIRE(workspace_document_handles(documents, handles, 64) == 1);
    REQUIRE(
        !project_semantics_rename(service, math_document, 2, offset, "Double", changed, &count, error, sizeof error)
    );
    REQUIRE(workspace_document_handles(documents, handles, 64) == 1);
    int other;
    REQUIRE(
        workspace_document_open(documents, "content/libraries/Other.chibi", &other) == WORKSPACE_DOCUMENT_OK
    );
    workspace_document_view before = view(other);
    REQUIRE(
        workspace_document_edit(
            documents,
            other,
            before.revision,
            0,
            (int)before.bytes,
            "library Broken { invalid"
        ) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(
        !project_semantics_rename(service, math_document, 1, offset, "Double", changed, &count, error, sizeof error)
    );
    REQUIRE(!strcmp(view(math_document).text, math_text));
    REQUIRE(!strcmp(view(other).text, "library Broken { invalid"));
    return true;
}
TEST_CASE(project_semantic_catalog_generations_cache_and_outlive_service)
{
    REQUIRE(setup_project());
    const script_catalog *first, *second;
    void *owner1, *owner2;
    REQUIRE(project_semantics_catalog_acquire(service, &first, &owner1, error, sizeof error));
    REQUIRE(project_semantics_catalog_acquire(service, &second, &owner2, error, sizeof error));
    REQUIRE(first == second && owner1 == owner2);
    int other;
    REQUIRE(
        workspace_document_open(documents, "content/libraries/Other.chibi", &other) == WORKSPACE_DOCUMENT_OK
    );
    document_semantics_service semantics = {
        .documents = documents,
        .catalog = &native,
        .catalog_acquire = project_semantics_catalog_acquire,
        .catalog_release = project_semantics_catalog_release,
        .catalog_user = service
    };
    document_semantics *
        source = document_semantics_service_snapshot(&semantics, other, 1, error, sizeof error);
    REQUIRE(source && document_semantics_valid(source));
    REQUIRE(document_semantics_graph(source));
    REQUIRE(script_catalog_find_capability(first, "Math.Twice", SCRIPT_CAPABILITY_FUNCTION) >= 0);
    project_semantics_refresh(service);
    project_semantics_free(service);
    service = NULL;
    REQUIRE(script_catalog_find_capability(first, "Math.Twice", SCRIPT_CAPABILITY_FUNCTION) >= 0);
    REQUIRE(document_semantics_valid(source));
    document_semantics_free(source);
    project_semantics_catalog_release(NULL, owner1);
    project_semantics_catalog_release(NULL, owner2);
    return true;
}
TEST_CASE(project_catalog_event_arrays_preserve_canonical_serialized_container)
{
    REQUIRE(setup_project());
    behavior_asset *event = behavior_asset_new("Rows");
    event->domain = BEHAVIOR_DOMAIN_EVENT_DEFINITION;
    REQUIRE(
        behavior_asset_add_variable(event, "values", PIN_STR, NULL, PIN_CONTAINER_ARRAY, NULL, NULL)
    );
    char *text = behavior_asset_save(event);
    behavior_asset_free(event);
    char path[256], content[256];
    snprintf(content, sizeof content, "%s/content", root);
    snprintf(path, sizeof path, "%s/events/Rows.kdl", content);
    source_catalog_edit overlay = { .path = path, .text = text };
    source_catalog *catalog = source_catalog_edited(content, &native, &overlay, 1);
    free(text);
    REQUIRE(source_catalog_diagnostic_count(catalog) == 0);
    const script_catalog *view = source_catalog_view(catalog);
    int index = script_catalog_find_capability(view, "Rows", SCRIPT_CAPABILITY_EVENT);
    REQUIRE(index >= 0 && view->capabilities[index].parameter_count == 1);
    REQUIRE(view->capabilities[index].parameters[0].type.storage == PIN_STR);
    REQUIRE(view->capabilities[index].parameters[0].type.container == PIN_CONTAINER_ARRAY);
    source_catalog_free(catalog);
    return true;
}
static const script_catalog *component_catalog(void *user, char *error, size_t size)
{
    workspace_assets_service *assets = user;
    return workspace_assets_service_sync(assets, error, size)
             ? workspace_assets_service_component_catalog(assets)
             : NULL;
}
static const script_catalog *candidate_catalog(
    void *user,
    const workspace_document_replacement *rows,
    int count,
    void **owner,
    char *error,
    size_t size
)
{
    workspace_assets_service
        *candidate = workspace_assets_service_candidate(user, rows, count, error, size);
    *owner = candidate;
    return candidate ? workspace_assets_service_component_catalog(candidate) : NULL;
}
static void candidate_release(void *, void *owner)
{
    workspace_assets_service_free(owner);
}
static bool component_project(int *component, int *scene, int *library)
{
    REQUIRE(setup_project());
    char path[256];
    snprintf(path, sizeof path, "%s/content/components", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/content/scenes", root);
    REQUIRE(!mkdir(path, 0700));
    REQUIRE(
        workspace_document_create(
            documents,
            "content/components/Charge.chibi",
            "component NavigationCharge {\n level: float\n limit: float\n}\n",
            component
        ) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(
        workspace_document_create(
            documents,
            "content/scenes/Example.kdl",
            "version 1\nname \"Example\"\n(array)entities { - name=\"Unit\" { "
            "components "
            "{ NavigationCharge level=2.0 limit=5.0 } } }\n",
            scene
        ) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(
        workspace_document_create(documents, "content/libraries/ChargeUse.chibi", "library ChargeUse { Read :: pure fn(value:NavigationCharge)->(result:float) { return value.level; } }", library) ==
        WORKSPACE_DOCUMENT_OK
    );
    assets = workspace_assets_service_new(documents, root, &native, error, sizeof error);
    REQUIRE(assets && workspace_assets_service_sync(assets, error, sizeof error));
    project_semantics_set_catalog(service, component_catalog, assets);
    project_semantics_set_candidate_catalog(service, candidate_catalog, candidate_release, assets);
    return true;
}
TEST_CASE(component_field_navigation_renames_exact_chibi_and_kdl_tokens_with_grouped_history)
{
    int component, scene, library;
    REQUIRE(component_project(&component, &scene, &library));
    REQUIRE(!type_catalog_find("NavigationCharge") && !type_catalog_find("NavigationEnergy"));
    const type_catalog_pack *published = type_catalog_project();
    int offset = (int)(strstr(view(component).text, "level") - view(component).text);
    project_reference *rows;
    int count, total;
    REQUIRE(
        project_semantics_references(service, component, 1, offset, 0, 64, &storage, &rows, &count, &total, error, sizeof error)
    );
    REQUIRE(count == 3 && total == 3);
    bool definition = false, kdl = false, source = false;
    for (int i = 0; i < count; i++) {
        definition |= rows[i].kind == AUTHORING_DEFINE &&
                      strstr(rows[i].path, "components/");
        kdl |= strstr(rows[i].path, "scenes/") != NULL;
        source |= strstr(rows[i].path, "ChargeUse.chibi") != NULL;
    }
    REQUIRE(definition && kdl && source);
    int changed[64];
    bool renamed = project_semantics_rename(
        service,
        component,
        1,
        offset,
        "amount",
        changed,
        &count,
        error,
        sizeof error
    );
    if (!renamed) {
        fprintf(stderr, "component rename: %s\n", error);
    }
    REQUIRE(renamed && count == 3);
    REQUIRE(
        strstr(view(component).text, "amount:") &&
        strstr(view(scene).text, "amount=") && strstr(view(library).text, "value.amount")
    );
    REQUIRE(workspace_document_undo(documents, scene, view(scene).revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        strstr(view(component).text, "level:") &&
        strstr(view(scene).text, "level=") && strstr(view(library).text, "value.level")
    );
    REQUIRE(
        workspace_document_redo(documents, component, view(component).revision) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(strstr(view(library).text, "value.amount"));
    REQUIRE(type_catalog_project() == published && !type_catalog_find("NavigationCharge"));
    return true;
}
TEST_CASE(component_type_rename_validates_real_candidate_registry_without_global_publication)
{
    int component, scene, library;
    REQUIRE(component_project(&component, &scene, &library));
    int offset = (int)(strstr(view(component).text, "NavigationCharge") -
                       view(component).text),
        changed[64], count;
    REQUIRE(
        project_semantics_rename(service, component, 1, offset, "NavigationEnergy", changed, &count, error, sizeof error)
    );
    REQUIRE(
        count == 3 && strstr(view(component).text, "component NavigationEnergy") &&
        strstr(view(scene).text, "NavigationEnergy") &&
        strstr(view(library).text, "value:NavigationEnergy")
    );
    REQUIRE(
        workspace_document_undo(documents, component, view(component).revision) == WORKSPACE_DOCUMENT_OK
    );
    int level = (int)(strstr(view(component).text, "level") - view(component).text);
    int revision = view(component).revision;
    REQUIRE(
        !project_semantics_rename(service, component, revision, level, "limit", changed, &count, error, sizeof error)
    );
    REQUIRE(
        view(component).revision == revision && view(component).can_redo &&
        !view(component).dirty
    );
    workspace_document_replacement bad = {
        component,
        revision,
        "component NavigationCharge {\n level: NoSuchType\n}\n"
    };
    workspace_assets_service
        *candidate = workspace_assets_service_candidate(assets, &bad, 1, error, sizeof error);
    REQUIRE(
        !candidate && *error && view(component).revision == revision &&
        view(component).can_redo
    );
    return true;
}
TEST_CASE(project_content_directory_matches_local_and_compiler_module_identities)
{
    REQUIRE(setup_project());
    char path[256];
    snprintf(path, sizeof path, "%s/assets", root);
    REQUIRE(!mkdir(path, 0700));
    snprintf(path, sizeof path, "%s/assets/libraries", root);
    REQUIRE(!mkdir(path, 0700));
    int math, other;
    REQUIRE(
        workspace_document_create(documents, "assets/libraries/Math.chibi", math_text, &math) ==
        WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(
        workspace_document_create(documents, "assets/libraries/Other.chibi", other_text, &other) ==
        WORKSPACE_DOCUMENT_OK
    );
    project_semantics_free(service);
    service = project_semantics_new_content(documents, root, "assets", &native);
    REQUIRE(service);
    project_reference *rows;
    int count, total;
    int offset = (int)(strstr(math_text, "Twice") - math_text);
    REQUIRE(
        project_semantics_references(service, math, 1, offset, 0, 10, &storage, &rows, &count, &total, error, sizeof error)
    );
    REQUIRE(count >= 2);
    for (int i = 0; i < count; i++) {
        REQUIRE(!strncmp(rows[i].path, "assets/libraries/", 17));
    }
    document_semantics_service semantics = {
        .documents = documents,
        .catalog = &native,
        .catalog_acquire = project_semantics_catalog_acquire,
        .catalog_release = project_semantics_catalog_release,
        .catalog_user = service,
        .content_prefix = "assets"
    };
    document_semantics *
        snapshot = document_semantics_service_snapshot(&semantics, math, 1, error, sizeof error);
    REQUIRE(snapshot && document_semantics_valid(snapshot));
    const script_analysis *analysis = document_semantics_analysis(snapshot);
    REQUIRE(!strcmp(analysis->identity, "libraries/Math.chibi"));
    document_semantics_free(snapshot);
    return true;
}
TEST_CASE(rml_field_references_follow_typed_expressions_alias_scope_and_exact_entity_ranges)
{
    int component, scene, library;
    REQUIRE(component_project(&component, &scene, &library));
    const script_catalog *catalog = workspace_assets_service_component_catalog(assets);
    const script_catalog_type *charge = script_catalog_type_by_id(
        catalog,
        script_catalog_id("type", "NavigationCharge")
    );
    REQUIRE(charge);
    authoring_ui_binding env[] = { { "rows", charge->type, false },
                                   { "record", charge->type, false } };
    env[0].type.container = PIN_CONTAINER_ARRAY;
    const char *text = "<div data-for=\"charge, index: self.rows\"><span "
                       "data-if=\"charge.level &amp;&amp; "
                       "self.record[&quot;level&quot;]\" "
                       "data-event-click=\"emit('self.fake.level',charge.level)"
                       "\">{{(self.record).level}} {{true ? charge.level : "
                       "self.record.level}}</span><p "
                       "title=\"self.record.level\">self.record.level</"
                       "p><!--{{self.fake.level}}--></div><b>{{charge.level}}</"
                       "b><i data-if=\"self.record.lev&#101;l\"/>";
    ui_source *source = ui_source_parse(text, UI_SOURCE_RML);
    authoring_ui_binding_reference *rows;
    int count;
    REQUIRE(
        authoring_ui_binding_references(source, catalog, env, 2, &storage, &rows, &count, error, sizeof error)
    );
    int resolved = 0, unresolved = 0, encoded = 0;
    uint64_t owner = charge->id,
             field = script_catalog_id("field", "NavigationCharge.level");
    for (int i = 0; i < count; i++) {
        if (rows[i].unresolved) {
            unresolved++;
            continue;
        }
        REQUIRE(
            rows[i].owner == owner && rows[i].field == field &&
            !strcmp(rows[i].name, "level")
        );
        resolved++;
        int length = rows[i].range.end - rows[i].range.start;
        if (length == 10 && !strncmp(text + rows[i].range.start, "lev&#101;l", 10)) {
            encoded++;
        } else {
            REQUIRE(length == 5 && !strncmp(text + rows[i].range.start, "level", 5));
        }
    }
    REQUIRE(resolved == 7 && unresolved == 1 && encoded == 1);
    ui_source_free(source);
    return true;
}
TEST_CASE(rml_conflicting_binding_contexts_keep_receiver_ambiguity_explicit)
{
    int component, scene, library;
    REQUIRE(component_project(&component, &scene, &library));
    const script_catalog *catalog = workspace_assets_service_component_catalog(assets);
    const script_catalog_type *charge = script_catalog_type_by_id(
        catalog,
        script_catalog_id("type", "NavigationCharge")
    );
    authoring_ui_binding env = { "record", charge->type, true };
    ui_source *source = ui_source_parse("<div>{{self.record.level}} {{(self.record == self.record).level}} {{'self.record.level'}}</div>", UI_SOURCE_RML);
    authoring_ui_binding_reference *rows;
    int count;
    REQUIRE(
        authoring_ui_binding_references(source, catalog, &env, 1, &storage, &rows, &count, error, sizeof error)
    );
    REQUIRE(
        count == 1 && rows[0].unresolved && !rows[0].field && !strcmp(rows[0].name, "level")
    );
    ui_source_free(source);
    return true;
}
TEST_CASE(ui_emit_capacity_is_shared_by_source_diagnostics_and_transactional_loading)
{
    const char
        *valid = "<button data-event-click = \"emit ('Ok', choose(1, 2), "
                 "'a,b', values[index], self.ready ? 1 : 0); emit('Also', "
                 "'literal emit(\\'Fake\\',1,2,3,4,5)')\"/>";
    ui_document_event_call *calls = NULL;
    ui_document_event_calls(valid, &calls);
    REQUIRE(acount(calls) == 2 && calls[0].arguments == 4 && calls[1].arguments == 1);
    afree(calls);
    ui_document_event *events = NULL;
    ui_document_events(valid, &events);
    REQUIRE(
        acount(events) == 2 && !strcmp(events[0].name, "Ok") &&
        !strcmp(events[1].name, "Also")
    );
    ui_document_events_free(events);
    ui_document document = { 0 };
    REQUIRE(ui_document_read(valid, NULL, NULL, NULL, &document, error, sizeof error));
    char *prior = document.rml;
    const char *bad = "<div>\n<button "
                      "data-event-click=\"emit('TooMany',1,2,3,4,5,6,7,8,9,10,"
                      "11,12,13,14,15,16,17)\"/></div>";
    REQUIRE(!ui_document_read(bad, NULL, NULL, NULL, &document, error, sizeof error));
    REQUIRE(document.rml == prior && strstr(error, "17 explicit") && strstr(error, "maximum is 16"));
    ui_source *source = ui_source_parse(bad, UI_SOURCE_RML);
    bool capacity = false;
    for (int i = 0; i < acount(source->diagnostics); i++) {
        const ui_source_diagnostic *d = &source->diagnostics[i];
        if (strstr(d->message, "maximum is 16")) {
            capacity = true;
            REQUIRE(d->line == 2 && !strncmp(bad + d->start, "emit(", 5));
        }
    }
    REQUIRE(capacity);
    ui_source_free(source);
    ui_document_free(&document);
    REQUIRE(
        ui_document_validate_events("<button data-event-change=\"emit('StudioModelParameter',self.modelDocument,parameter.node,0,ev.value,true,ev.canceled)\"/>", error, sizeof error)
    );
    REQUIRE(
        ui_document_validate_events("<!--<button data-event-click=\"emit('Ignored',1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17)\"/>-->", error, sizeof error)
    );
    return true;
}

static const prototype_registry *project_registry(void *current, void *candidate, char *message, size_t size)
{
    workspace_assets_service *scope = candidate ? candidate : current;
    return workspace_assets_service_sync(scope, message, size) ? scope->registry : NULL;
}
static bool ui_native_catalog(void)
{
    ui_capabilities = host_native_catalog_new(&NATIVE_PACK, 1, &UI_DEFINITION_PACK, 1, error, sizeof error);
    REQUIRE(ui_capabilities);
    native = *native_capabilities_catalog(ui_capabilities);
    REQUIRE(script_catalog_find_capability(&native, "set_ui_data", SCRIPT_CAPABILITY_ACTION) >= 0);
    return true;
}

static bool ui_diagnosed(bool ok)
{
    if (!ok) {
        fprintf(stderr, "typed project UI: %s\n", error);
    }
    return ok;
}
static bool typed_ui_project(int *component, int *scene, int *library, int *behavior, int *ui, int *widget)
{
    REQUIRE(ui_native_catalog());
    REQUIRE(component_project(component, scene, library));
    char path[256];
    const char *folders[] = { "behaviors", "prototypes", "ui" };
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/content/%s", root, folders[i]);
        REQUIRE(!mkdir(path, 0700));
    }
    int base, child;
    REQUIRE(
        workspace_document_create(
            documents,
            "content/behaviors/Publish.chibi",
            "behavior Publish { Send :: fn(charge:NavigationCharge) { "
            "set_ui_data(self,\"charge\",charge); "
            "set_ui_data(self,\"charge.level\",charge.level); } }",
            behavior
        ) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(
        workspace_document_create(
            documents,
            "content/prototypes/Base.kdl",
            "name \"Base\"\ncomponents { Ui document=\"Panel\" "
            "}\n",
            &base
        ) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(
        workspace_document_create(
            documents,
            "content/prototypes/Child.kdl",
            "name \"Child\"\n(array)includes { - \"Base\" }\n(array)behaviors "
            "{ - \"Publish\" }\n",
            &child
        ) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(
        workspace_document_create(
            documents,
            "content/ui/Panel.rml",
            "<rml><body><use ui=\"Details\"/><p>{{self.charge.level}}</p><span "
            "title=\"level\">level</span></body></rml>",
            ui
        ) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(
        workspace_document_create(
            documents,
            "content/ui/Details.rml",
            "<p data-if=\"self.charge.level > 0\">{{self.charge.limit}}</p>",
            widget
        ) == WORKSPACE_DOCUMENT_OK
    );
    workspace_document_view scene_before = view(*scene);
    REQUIRE(
        workspace_document_edit(
            documents,
            *scene,
            scene_before.revision,
            0,
            (int)scene_before.bytes,
            "version 1\nname \"Example\"\n(array)entities { - name=\"Unit\" "
            "prototype=\"Child\" { components { NavigationCharge level=2.0 "
            "limit=5.0 } } }\n"
        ) == WORKSPACE_DOCUMENT_OK
    );
    project_semantics_set_registry(service, project_registry, assets);
    return true;
}
TEST_CASE(project_rml_references_derive_real_inherited_ui_context_and_rename_atomically)
{
    int component, scene, library, behavior, ui, widget;
    REQUIRE(ui_diagnosed(
        typed_ui_project(&component, &scene, &library, &behavior, &ui, &widget)
    ));
    project_reference *rows;
    int count, total;
    int offset = (int)(strstr(view(component).text, "level") - view(component).text);
    REQUIRE(ui_diagnosed(project_semantics_references(
        service,
        component,
        view(component).revision,
        offset,
        0,
        64,
        &storage,
        &rows,
        &count,
        &total,
        error,
        sizeof error
    )));
    bool panel = false, details = false;
    for (int i = 0; i < count; i++) {
        panel |= strstr(rows[i].path, "Panel.rml") != NULL;
        details |= strstr(rows[i].path, "Details.rml") != NULL;
    }
    REQUIRE(ui_diagnosed(panel && details));
    int changed[WORKSPACE_DOCUMENT_COUNT_MAX], changed_count = 0;
    REQUIRE(ui_diagnosed(project_semantics_rename(
        service,
        component,
        view(component).revision,
        offset,
        "energy",
        changed,
        &changed_count,
        error,
        sizeof error
    )));
    REQUIRE(ui_diagnosed(changed_count == 6));
    REQUIRE(ui_diagnosed(
        strstr(view(behavior).text, "\"charge.energy\"") &&
        strstr(view(behavior).text, "charge.energy")
    ));
    REQUIRE(ui_diagnosed(
        strstr(view(ui).text, "self.charge.energy") &&
        strstr(view(ui).text, "title=\"level\">level")
    ));
    REQUIRE(ui_diagnosed(
        strstr(view(widget).text, "self.charge.energy") &&
        strstr(view(widget).text, "self.charge.limit")
    ));
    REQUIRE(ui_diagnosed(
        workspace_document_undo(documents, component, view(component).revision) == WORKSPACE_DOCUMENT_OK
    ));
    REQUIRE(ui_diagnosed(
        strstr(view(ui).text, "self.charge.level") && strstr(view(widget).text, "self.charge.level")
    ));
    REQUIRE(ui_diagnosed(
        workspace_document_redo(documents, ui, view(ui).revision) == WORKSPACE_DOCUMENT_OK
    ));
    REQUIRE(ui_diagnosed(
        strstr(view(component).text, "energy") && strstr(view(widget).text, "self.charge.energy")
    ));
    (void)scene;
    (void)library;
    (void)behavior;
    return true;
}
TEST_CASE(project_rml_unknown_receiver_refuses_field_rename_without_consuming_redo)
{
    REQUIRE(ui_diagnosed(ui_native_catalog()));
    int component, scene, library;
    REQUIRE(ui_diagnosed(component_project(&component, &scene, &library)));
    char path[256];
    snprintf(path, sizeof path, "%s/content/ui", root);
    REQUIRE(ui_diagnosed(!mkdir(path, 0700)));
    int ui;
    REQUIRE(ui_diagnosed(
        workspace_document_create(documents, "content/ui/Unbound.rml", "<p>{{self.unknown.level}}</p>", &ui) ==
        WORKSPACE_DOCUMENT_OK
    ));
    workspace_document_view scene_before = view(scene);
    REQUIRE(
        workspace_document_edit(
            documents,
            scene,
            scene_before.revision,
            0,
            (int)scene_before.bytes,
            "version 1\nname \"Example\"\n(array)entities {}\n"
        ) == WORKSPACE_DOCUMENT_OK
    );
    project_semantics_set_registry(service, project_registry, assets);
    workspace_document_view before = view(component);
    REQUIRE(ui_diagnosed(
        workspace_document_edit(documents, component, before.revision, 0, 0, "// temporary\n") == WORKSPACE_DOCUMENT_OK
    ));
    REQUIRE(ui_diagnosed(
        workspace_document_undo(documents, component, view(component).revision) == WORKSPACE_DOCUMENT_OK
    ));
    before = view(component);
    REQUIRE(ui_diagnosed(before.can_redo));
    int changed[WORKSPACE_DOCUMENT_COUNT_MAX], count = 0;
    int offset = (int)(strstr(before.text, "level") - before.text);
    REQUIRE(ui_diagnosed(
        !project_semantics_rename(service, component, before.revision, offset, "energy", changed, &count, error, sizeof error)
    ));
    REQUIRE(ui_diagnosed(strstr(error, "Cannot prove UI binding receiver") && count == 0));
    REQUIRE(ui_diagnosed(
        view(component).revision == before.revision &&
        view(component).can_redo && strstr(view(ui).text, "unknown.level")
    ));
    (void)scene;
    (void)library;
    return true;
}
TEST_CASE(project_rml_scene_behavior_override_makes_conflicting_receiver_explicit_and_preserves_redo)
{
    int component, scene, library, behavior, ui, widget;
    REQUIRE(ui_diagnosed(
        typed_ui_project(&component, &scene, &library, &behavior, &ui, &widget)
    ));
    int alternate;
    REQUIRE(ui_diagnosed(
        workspace_document_create(
            documents,
            "content/behaviors/Alternate.chibi",
            "behavior Alternate { Send :: fn() { "
            "set_ui_data(self,\"charge\",2.0); } }",
            &alternate
        ) == WORKSPACE_DOCUMENT_OK
    ));
    workspace_document_view original = view(scene);
    REQUIRE(ui_diagnosed(
        workspace_document_edit(
            documents,
            scene,
            original.revision,
            0,
            (int)original.bytes,
            "version 1\nname \"Example\"\n(array)entities { - name=\"Unit\" "
            "prototype=\"Child\" { (array)behaviors { - \"Alternate\" } } }\n"
        ) == WORKSPACE_DOCUMENT_OK
    ));
    original = view(component);
    REQUIRE(ui_diagnosed(
        workspace_document_edit(documents, component, original.revision, 0, 0, "// temporary\n") == WORKSPACE_DOCUMENT_OK
    ));
    REQUIRE(ui_diagnosed(
        workspace_document_undo(documents, component, view(component).revision) == WORKSPACE_DOCUMENT_OK
    ));
    original = view(component);
    REQUIRE(ui_diagnosed(original.can_redo));
    int changed[WORKSPACE_DOCUMENT_COUNT_MAX], count = 0;
    int offset = (int)(strstr(original.text, "level") - original.text);
    REQUIRE(ui_diagnosed(
        !project_semantics_rename(service, component, original.revision, offset, "energy", changed, &count, error, sizeof error)
    ));
    REQUIRE(ui_diagnosed(strstr(error, "Cannot prove UI binding receiver") && count == 0));
    REQUIRE(ui_diagnosed(
        view(component).revision == original.revision && view(component).can_redo
    ));
    REQUIRE(ui_diagnosed(
        strstr(view(behavior).text, "charge.level") &&
        strstr(view(ui).text, "self.charge.level") &&
        strstr(view(widget).text, "self.charge.level")
    ));
    REQUIRE(ui_diagnosed(
        view(behavior).revision == 1 && view(ui).revision == 1 && view(widget).revision == 1
    ));
    return true;
}
void test_project_semantics(void)
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(project_rml_references_derive_real_inherited_ui_context_and_rename_atomically);
    RUN_TEST_CASE(
        project_rml_scene_behavior_override_makes_conflicting_receiver_explicit_and_preserves_redo
    );
    RUN_TEST_CASE(project_rml_unknown_receiver_refuses_field_rename_without_consuming_redo);
    RUN_TEST_CASE(ui_emit_capacity_is_shared_by_source_diagnostics_and_transactional_loading);
    RUN_TEST_CASE(rml_field_references_follow_typed_expressions_alias_scope_and_exact_entity_ranges);
    RUN_TEST_CASE(rml_conflicting_binding_contexts_keep_receiver_ambiguity_explicit);
    RUN_TEST_CASE(project_content_directory_matches_local_and_compiler_module_identities);
    RUN_TEST_CASE(component_field_navigation_renames_exact_chibi_and_kdl_tokens_with_grouped_history);
    RUN_TEST_CASE(component_type_rename_validates_real_candidate_registry_without_global_publication);
    RUN_TEST_CASE(project_catalog_event_arrays_preserve_canonical_serialized_container);
    RUN_TEST_CASE(project_semantic_catalog_generations_cache_and_outlive_service);
    RUN_TEST_CASE(project_semantics_calls_and_references_resolve_closed_sources_and_own_rows);
    RUN_TEST_CASE(project_semantics_rename_adopts_complete_candidates_as_one_shared_undo);
    RUN_TEST_CASE(project_semantics_rename_refusals_preserve_drafts_and_open_membership);
    pu_setup(NULL, NULL);
}
